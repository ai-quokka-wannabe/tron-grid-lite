/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#include "senses.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

GridSensesSource::GridSensesSource(const BvhLib::Scene& scene, std::vector<float> source_strengths, Acoustics::Reflectors reflectors, RadianceSolver* radiance_solver,
    Stage* stage) :
    m_scene(scene),
    m_source_strengths(std::move(source_strengths)),
    m_reflectors(std::move(reflectors)),
    m_radiance_solver(radiance_solver),
    m_stage(stage)
{
}

void GridSensesSource::beginTick(const std::vector<RosterLib::Creature>& creatures)
{
    /*
        Bodies first, because everything after depends on where they stand. A tick on which any
        body moved stales every traced-sense cache: the caches key on the listener's own pose
        because that used to be everything a solve read, and a body standing in the scene is read
        too. The comparison is exact, like the cache keys it guards.
    */
    m_bodies_moved = false;
    if (m_stage != nullptr) {
        // Every placement a body has - the head's and its trail's - because a tail that swung
        // into view is a body moving as much as a head is.
        std::vector<RosterLib::Pose> poses;
        for (const RosterLib::Creature& creature : creatures) {
            if (!creature.model.empty()) {
                poses.push_back(creature.pose);
                poses.insert(poses.end(), creature.trail.begin(), creature.trail.end());
            }
        }
        // The guests' poses count as bodies moving too: a neighbour that walked into view is
        // as much a reason to re-trace as the listener turning its head.
        for (const Stage::GuestTelling& guest : m_guests) {
            if (!m_stage->guestInstanceOf(guest.creature_id).empty()) {
                poses.push_back(guest.pose);
                poses.insert(poses.end(), guest.trail.begin(), guest.trail.end());
            }
        }

        if (poses.size() != m_last_body_poses.size()) {
            m_bodies_moved = true;
        } else {
            for (size_t index{0u}; index < poses.size(); ++index) {
                if ((!(poses[index].position == m_last_body_poses[index].position)) || (poses[index].yaw != m_last_body_poses[index].yaw)) {
                    m_bodies_moved = true;
                    break;
                }
            }
        }

        m_last_body_poses = std::move(poses);
        m_stage->update(creatures);
        m_stage->placeGuests(m_guests);
    }

    m_calls.clear();
    for (const RosterLib::Creature& creature : creatures) {
        if (creature.vocalisation > 0.0f) {
            // The call leaves from the body's own position, exactly as the ABI documents at
            // TglActions::vocalisation_strength — and it sees through its own hull, which is what
            // the caller instance is for.
            m_calls.push_back(Call{.position = creature.pose.position,
                .velocity = creature.velocity,
                .strength = creature.vocalisation,
                .caller_instance = (m_stage != nullptr) ? m_stage->instanceOf(creature.body.creature_id) : BvhLib::InstanceRange{}});
        }
    }
    // The guests' calls, exactly as the hosted ones: from where the world says they stand,
    // through their own hulls when they have one.
    for (const Stage::GuestTelling& guest : m_guests) {
        if (guest.vocalisation > 0.0f) {
            m_calls.push_back(Call{.position = guest.pose.position,
                .velocity = guest.velocity,
                .strength = guest.vocalisation,
                .caller_instance = (m_stage != nullptr) ? m_stage->guestInstanceOf(guest.creature_id) : BvhLib::InstanceRange{}});
        }
    }

    // The scratches the world sounded, as sources: the scraping body's own instance is the
    // caller's, so a body never gags its own scrape - hearing its own spikes drag along the
    // floor is the point. The velocity stays zero: only an arrival could carry Doppler, and
    // a scratch makes none.
    m_scratches.clear();
    for (const Stage::ScratchTelling& scratch : m_scratch_tellings) {
        BvhLib::InstanceRange scraper{};
        if (m_stage != nullptr) {
            scraper = scratch.own ? m_stage->instanceOf(scratch.creature) : m_stage->guestInstanceOf(static_cast<uint32_t>(scratch.creature));
        }
        m_scratches.push_back(Call{.position = scratch.position, .velocity = MathLib::Vec3{}, .strength = scratch.strength, .caller_instance = scraper});
    }
}

void GridSensesSource::tellGuests(std::vector<Stage::GuestTelling> guests)
{
    m_guests = std::move(guests);
}

void GridSensesSource::tellScratches(std::vector<Stage::ScratchTelling> scratches)
{
    m_scratch_tellings = std::move(scratches);
}

GridSensesSource::CreatureEars& GridSensesSource::earStateFor(uint64_t creature_id, uint32_t ear_count)
{
    for (CreatureEars& state : m_ear_states) {
        if (state.creature_id == creature_id) {
            return state;
        }
    }

    CreatureEars state{};
    state.creature_id = creature_id;
    state.keys.resize(ear_count);
    state.responses.resize(ear_count);
    state.delivered.resize(ear_count);
    state.arrivals.resize(ear_count);
    state.arrival_counts.assign(ear_count, 0u);
    m_ear_states.push_back(std::move(state));
    return m_ear_states.back();
}

GridSensesSource::CreatureVision& GridSensesSource::visionStateFor(const TglCreatureDesc& body)
{
    for (CreatureVision& state : m_vision_states) {
        if (state.creature_id == body.creature_id) {
            return state;
        }
    }

    CreatureVision state{};
    state.creature_id = body.creature_id;
    state.eye_samples.resize(body.eye_count);
    for (uint32_t eye{0u}; eye < body.eye_count; ++eye) {
        const TglEyeDesc& desc{body.eyes[eye]};
        const uint32_t floats{desc.sample_count * desc.channels};
        state.eye_samples[eye].resize((floats + 3u) / 4u);
    }
    m_vision_states.push_back(std::move(state));
    return m_vision_states.back();
}

void GridSensesSource::fill(const RosterLib::Creature& creature, TglSenses& senses)
{
    const TglCreatureDesc& body{creature.body};

    if ((body.eye_count != 0u || body.irradiance_sample_count != 0u) && (m_radiance_solver == nullptr)) {
        // Refused loudly rather than silently unseeing: a Program would receive zeroed vision and
        // have no way to tell a dark Grid from an unbuilt sense.
        throw std::runtime_error{"This body declares eyes or irradiance and no radiance solver is attached."};
    }

    fillEars(creature, senses);
    fillVision(creature, senses);
}

void GridSensesSource::fillEars(const RosterLib::Creature& creature, TglSenses& senses)
{
    const TglCreatureDesc& body{creature.body};
    if (body.ear_count == 0u) {
        return;
    }

    CreatureEars& state{earStateFor(body.creature_id, body.ear_count)};
    m_ear_views.resize(body.ear_count);

    // The listener's own body, which its ears hear through rather than into.
    const BvhLib::InstanceRange own_instance{(m_stage != nullptr) ? m_stage->instanceOf(body.creature_id) : BvhLib::InstanceRange{}};

    for (uint32_t index{0u}; index < body.ear_count; ++index) {
        const TglEarDesc& ear{body.ears[index]};

        /*
            The gather's histogram is the ABI's histogram, bin for bin, so a body must ask for
            exactly the resolution the gather produces. A body wanting different bins needs a
            resampling stage, and building one before a body exists that needs it would be the
            abstraction-without-a-user this repository keeps deleting.
        */
        if ((ear.band_count != Acoustics::BAND_COUNT) || (ear.bin_count != Acoustics::BIN_COUNT) || (ear.bin_seconds != Acoustics::BIN_SECONDS)) {
            throw std::runtime_error{"Ear " + std::to_string(index) + " asks for a response shape the gather does not produce."};
        }

        const MathLib::Vec3 world{RosterLib::worldFromBody(creature.pose, MathLib::Vec3{ear.position[0], ear.position[1], ear.position[2]})};

        EarKey& cached{state.keys[index]};
        AlignedResponse& stored{state.responses[index]};
        if ((!cached.solved) || m_bodies_moved || (!(cached.world_position == world))) {
            Acoustics::GatherConfig config{};
            for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
                config.air_absorption_db_per_km[band] = ear.air_absorption_db_per_km[band];
            }
            config.skip_instance = own_instance;
            /*
                The hum spectrum stays at the config's unit default. The first body's band edges are
                chosen so that each band holds exactly one harmonic of the Grid's hum, which makes
                the unit vector the correct resolution of that hum into these bands. A preset body
                with a real audiogram resolves its own spectrum, and that resolver arrives with it.
            */

            const Acoustics::ImpulseResponse response{Acoustics::gather(m_scene, m_source_strengths, world, config)};
            std::copy(response.bins.begin(), response.bins.end(), stored.energy.begin());
            cached.world_position = world;
            cached.solved = true;
        }

        const float* delivery{stored.energy.data()};
        const bool sounding{(!m_calls.empty()) || (!m_scratches.empty())};
        if (sounding) {
            /*
                A tick with calls or scratches is delivered from mix storage: the cached hum, and
                then every sound's own response added bin for bin — the emitter's included, which
                is both how a creature hears itself speak and how it hears its own spikes drag
                along the floor. The cache itself is never written, so a stationary ear on the
                next silent tick reads exactly the gather again.
            */
            AlignedResponse& mixed{state.delivered[index]};
            mixed.energy = stored.energy;
            state.arrival_counts[index] = 0u;

            Acoustics::CallConfig call_config{};
            /*
                The call spectrum stays at the unit default for the same reason the hum's does: the
                first body's voice is white across the first body's bands. A preset body with a
                real voice resolves its own spectrum, and that resolver arrives with it.
            */
            for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
                call_config.air_absorption_db_per_km[band] = ear.air_absorption_db_per_km[band];
            }
            call_config.source_radius_metres = RosterLib::BODY_HALF_HEIGHT;
            call_config.listener_instance = own_instance;

            call_config.listener_velocity = creature.velocity;
            for (const Call& call : m_calls) {
                call_config.caller_instance = call.caller_instance;
                call_config.source_velocity = call.velocity;
                const Acoustics::ImpulseResponse arrivals{Acoustics::deliverCall(m_scene, m_reflectors, call.position, call.strength, world, call_config)};
                for (size_t bin{0u}; bin < arrivals.bins.size(); ++bin) {
                    mixed.energy[bin] += arrivals.bins[bin];
                }
                // The discrete arrivals, in delivery order, the loudest kept when the ear is full.
                for (uint32_t arrived{0u}; arrived < arrivals.arrival_count; ++arrived) {
                    const Acoustics::Arrival& record{arrivals.arrivals[arrived]};
                    TglArrival arrival{};
                    arrival.onset_seconds = record.onset_seconds;
                    arrival.radial_velocity = record.radial_velocity;
                    for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
                        arrival.energy[band] = record.energy[band];
                    }
                    uint32_t& count{state.arrival_counts[index]};
                    if (count < TGL_EAR_ARRIVALS_MAX) {
                        state.arrivals[index][count] = arrival;
                        ++count;
                    } else {
                        uint32_t faintest{0u};
                        float faintest_total{std::numeric_limits<float>::max()};
                        for (uint32_t at{0u}; at < TGL_EAR_ARRIVALS_MAX; ++at) {
                            float total{0.0f};
                            for (const float energy : state.arrivals[index][at].energy) {
                                total += energy;
                            }
                            if (total < faintest_total) {
                                faintest_total = total;
                                faintest = at;
                            }
                        }
                        float total{0.0f};
                        for (const float energy : arrival.energy) {
                            total += energy;
                        }
                        if (total > faintest_total) {
                            state.arrivals[index][faintest] = arrival;
                        }
                    }
                }
            }

            /*
                The scratches, bins only — never an arrival. A scratch is the Grid's one
                sustained source: noisy, modulated by its own gait, with no sharp onset to
                time, so it deposits energy a listener can detect but hands over nothing to
                range from — the asymmetry the acoustics documents as the rule's own yield.
            */
            for (const Call& scratch : m_scratches) {
                call_config.caller_instance = scratch.caller_instance;
                call_config.source_velocity = scratch.velocity;
                const Acoustics::ImpulseResponse rasp{Acoustics::deliverCall(m_scene, m_reflectors, scratch.position, scratch.strength, world, call_config)};
                for (size_t bin{0u}; bin < rasp.bins.size(); ++bin) {
                    mixed.energy[bin] += rasp.bins[bin];
                }
            }

            delivery = mixed.energy.data();
        }

        const uint32_t arrival_count{sounding ? state.arrival_counts[index] : 0u};
        m_ear_views[index] = TglEarView{.energy = delivery,
            .arrivals = (arrival_count > 0u) ? state.arrivals[index].data() : nullptr,
            .arrival_count = arrival_count,
            .band_count = ear.band_count,
            .bin_count = ear.bin_count,
            .reserved0 = 0u};
    }

    senses.ears = m_ear_views.data();
    senses.ear_count = body.ear_count;
}

void GridSensesSource::fillVision(const RosterLib::Creature& creature, TglSenses& senses)
{
    const TglCreatureDesc& body{creature.body};
    if ((body.eye_count == 0u) && (body.irradiance_sample_count == 0u)) {
        return;
    }

    CreatureVision& state{visionStateFor(body)};

    const bool pose_changed{(!state.solved) || m_bodies_moved || (!(state.position == creature.pose.position)) || (state.yaw != creature.pose.yaw)};
    if (pose_changed) {
        /*
            One flat batch for everything this creature sees: eye samples first, in eye order and
            sample order, then the irradiance directions. The solver neither knows nor cares which
            is which, because both are the same question.
        */
        size_t ray_total{static_cast<size_t>(body.irradiance_sample_count)};
        for (uint32_t eye{0u}; eye < body.eye_count; ++eye) {
            ray_total += body.eyes[eye].sample_count;
        }

        std::vector<MathLib::Vec4> rays;
        rays.reserve(2u * ray_total);

        for (uint32_t eye{0u}; eye < body.eye_count; ++eye) {
            const TglEyeDesc& desc{body.eyes[eye]};

            if ((desc.channels != 1u) && (desc.channels != 3u)) {
                // A channel weighting beyond a scalar and the renderer's own three bands belongs to
                // a preset body that does not exist yet, and would be filled with invented numbers.
                throw std::runtime_error{"Eye " + std::to_string(eye) + " asks for " + std::to_string(desc.channels) + " channels; the Grid answers 1 or 3."};
            }
            if (desc.sample_count == 0u) {
                throw std::runtime_error{"Eye " + std::to_string(eye) + " declares no samples; an eye that cannot see is not an eye."};
            }

            const MathLib::Vec3 origin{RosterLib::worldFromBody(creature.pose, MathLib::Vec3{desc.position[0], desc.position[1], desc.position[2]})};
            for (uint32_t sample{0u}; sample < desc.sample_count; ++sample) {
                const MathLib::Vec3 body_direction{
                    desc.sample_directions[(sample * 3u) + 0u], desc.sample_directions[(sample * 3u) + 1u], desc.sample_directions[(sample * 3u) + 2u]};
                rays.push_back(MathLib::Vec4::fromVec3(origin, 0.0f));
                rays.push_back(MathLib::Vec4::fromVec3(RosterLib::worldDirectionFromBody(creature.pose, body_direction), 0.0f));
            }
        }

        /*
            Irradiance directions are the fixed spherical Fibonacci set in world space, exactly as
            the ABI documents — deliberately not rotated with the body, since a sphere integral has
            no facing.
        */
        const MathLib::Vec3 centre{RosterLib::worldFromBody(creature.pose, MathLib::Vec3{0.0f, 0.0f, 0.0f})};
        for (uint32_t sample{0u}; sample < body.irradiance_sample_count; ++sample) {
            rays.push_back(MathLib::Vec4::fromVec3(centre, 0.0f));
            rays.push_back(MathLib::Vec4::fromVec3(Acoustics::fibonacciDirection(sample, body.irradiance_sample_count), 0.0f));
        }

        if (m_stage != nullptr) {
            /*
                The tick's placements, with the creature's own body blanked to a zero node count:
                its eyes see through its own hull the way its ears hear through it, and the blank
                record is the flat spelling of the same skip — the shader never learns a special
                case.
            */
            std::vector<BvhLib::InstanceRecord> records{m_stage->flatInstances()};
            const BvhLib::InstanceRange own{m_stage->instanceOf(body.creature_id)};
            for (uint32_t index{0u}; index < records.size(); ++index) {
                if (own.contains(index)) {
                    records[index].node_count = 0u; // The whole chain: head and every segment.
                }
            }
            m_radiance_solver->stage(records);
        }

        const std::vector<MathLib::Vec4> radiance{m_radiance_solver->solve(rays, SENSES_MAX_BOUNCES)};

        size_t next{0u};
        for (uint32_t eye{0u}; eye < body.eye_count; ++eye) {
            const TglEyeDesc& desc{body.eyes[eye]};
            float* const samples{state.eye_samples[eye].front().v};

            for (uint32_t sample{0u}; sample < desc.sample_count; ++sample) {
                const MathLib::Vec4& answer{radiance[next]};
                ++next;

                if (desc.channels == 3u) {
                    samples[(sample * 3u) + 0u] = answer.x;
                    samples[(sample * 3u) + 1u] = answer.y;
                    samples[(sample * 3u) + 2u] = answer.z;
                } else {
                    // A scalar photoreceptor weights the renderer's three bands equally: intensity,
                    // with no colour opinion the body's descriptor did not state.
                    samples[sample] = (answer.x + answer.y + answer.z) / 3.0f;
                }
            }
        }

        float irradiance_total{0.0f};
        for (uint32_t sample{0u}; sample < body.irradiance_sample_count; ++sample) {
            const MathLib::Vec4& answer{radiance[next]};
            ++next;
            irradiance_total += (answer.x + answer.y + answer.z) / 3.0f;
        }
        state.irradiance = (body.irradiance_sample_count == 0u) ? 0.0f : (irradiance_total / static_cast<float>(body.irradiance_sample_count));

        state.position = creature.pose.position;
        state.yaw = creature.pose.yaw;
        state.solved = true;
    }

    if (body.eye_count != 0u) {
        m_eye_views.resize(body.eye_count);
        for (uint32_t eye{0u}; eye < body.eye_count; ++eye) {
            const TglEyeDesc& desc{body.eyes[eye]};
            m_eye_views[eye] = TglEyeView{.samples = state.eye_samples[eye].front().v, .sample_count = desc.sample_count, .channels = desc.channels};
        }
        senses.eyes = m_eye_views.data();
        senses.eye_count = body.eye_count;
    }

    senses.irradiance = state.irradiance;
}
