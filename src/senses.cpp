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
#include <stdexcept>
#include <string>
#include <utility>

GridSensesSource::GridSensesSource(const BvhLib::Scene& scene, std::vector<float> source_strengths) :
    m_scene(scene),
    m_source_strengths(std::move(source_strengths))
{
}

GridSensesSource::CreatureEars& GridSensesSource::stateFor(uint64_t creature_id, uint32_t ear_count)
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
    m_ear_states.push_back(std::move(state));
    return m_ear_states.back();
}

void GridSensesSource::fill(const RosterLib::Creature& creature, TglSenses& senses)
{
    const TglCreatureDesc& body{creature.body};

    if (body.eye_count != 0u || body.irradiance_sample_count != 0u) {
        // Refused loudly rather than silently unseeing: a Program would receive zeroed vision and
        // have no way to tell a dark Grid from an unbuilt sense.
        throw std::runtime_error{"Eyes and irradiance are not filled yet; this body declares senses the Grid cannot yet answer."};
    }

    if (body.ear_count == 0u) {
        return;
    }

    CreatureEars& state{stateFor(body.creature_id, body.ear_count)};
    m_ear_views.resize(body.ear_count);

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
        if ((!cached.solved) || (!(cached.world_position == world))) {
            Acoustics::GatherConfig config{};
            for (uint32_t band{0u}; band < Acoustics::BAND_COUNT; ++band) {
                config.air_absorption_db_per_km[band] = ear.air_absorption_db_per_km[band];
            }
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

        m_ear_views[index] = TglEarView{.energy = stored.energy.data(), .band_count = ear.band_count, .bin_count = ear.bin_count};
    }

    senses.ears = m_ear_views.data();
    senses.ear_count = body.ear_count;
}
