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

#include "acoustics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

namespace Acoustics
{

    namespace
    {

        //! One full turn, in radians. Doubling is a bare exponent step, so this is the correctly
        //! rounded two-pi and not an accumulation of the standard constant's error.
        constexpr float TWO_PI{2.0f * std::numbers::pi_v<float>};

        /*!
            Deposits one arrival into the response: the whole energy model in one place.

            The gather and the call delivery must price a metre of path identically, or the sum of
            their responses — which is what an ear actually receives — would weight its two halves
            against each other. One function is what holds them to one price: explicit `1/r²`
            spreading floored at the one-metre reference, per-band air absorption in energy
            decibels, and a bin chosen by the accumulated path at the speed of sound. An arrival
            past the last bin is dropped rather than folded, so the last bin stays an ordinary bin.
        */
        void depositArrival(ImpulseResponse& response, float path, float scale, const std::array<float, BAND_COUNT>& spectrum,
            const std::array<float, BAND_COUNT>& air_absorption_db_per_km)
        {
            /*
                Spreading is explicit and is measured from a one-metre reference, so a source
                exactly one metre away arrives at unit strength and nothing closer than that is
                amplified. Without the floor, a ray that grazes a tube it is almost touching would
                divide by an arbitrarily small number and deposit an arbitrarily large amount of
                energy into one bin.
            */
            const float spreading{1.0f / std::max(path * path, 1.0f)};

            const uint32_t bin{static_cast<uint32_t>(path / (SPEED_OF_SOUND * BIN_SECONDS))};
            if (bin < BIN_COUNT) {
                for (uint32_t band{0u}; band < BAND_COUNT; ++band) {
                    // Energy, not pressure, so the decibels divide by ten rather than twenty.
                    const float air_db{air_absorption_db_per_km[band] * (path / 1000.0f)};
                    const float air{std::pow(10.0f, -air_db / 10.0f)};

                    response.at(band, bin) += scale * spectrum[band] * spreading * air;
                }
            }
        }

        /*!
            Deposits an arrival into the histogram *and* records it as a discrete arrival - what a
            call does, and the hum never does. The record keeps the deposited band energies, the
            exact onset and the radial velocity the caller hands down; a sixteenth arrival displaces
            the faintest only when it is louder, so the loudest sixteen are what survive.
        */
        void depositCallArrival(ImpulseResponse& response, float path, float scale, const CallConfig& config, float radial_velocity)
        {
            ImpulseResponse only_this{};
            depositArrival(only_this, path, scale, config.spectrum, config.air_absorption_db_per_km);
            Arrival arrival{};
            arrival.onset_seconds = path / SPEED_OF_SOUND;
            arrival.radial_velocity = radial_velocity;
            float total{0.0f};
            for (uint32_t band{0u}; band < BAND_COUNT; ++band) {
                for (uint32_t bin{0u}; bin < BIN_COUNT; ++bin) {
                    arrival.energy[band] += only_this.at(band, bin);
                    response.at(band, bin) += only_this.at(band, bin);
                }
                total += arrival.energy[band];
            }
            if (total <= 0.0f) {
                return; // Past the last bin, or silent: in neither histogram nor record.
            }
            if (response.arrival_count < ARRIVALS_MAX) {
                response.arrivals[response.arrival_count] = arrival;
                ++response.arrival_count;
                return;
            }
            uint32_t faintest{0u};
            float faintest_total{std::numeric_limits<float>::max()};
            for (uint32_t index{0u}; index < ARRIVALS_MAX; ++index) {
                float candidate{0.0f};
                for (const float energy : response.arrivals[index].energy) {
                    candidate += energy;
                }
                if (candidate < faintest_total) {
                    faintest_total = candidate;
                    faintest = index;
                }
            }
            if (total > faintest_total) {
                response.arrivals[faintest] = arrival;
            }
        }

        //! The world-space unit normal of the face a hit struck, exactly as the gather derives it.
        [[nodiscard]] MathLib::Vec3 struckFaceNormal(const BvhLib::Scene& scene, const BvhLib::Hit& hit)
        {
            const BvhLib::Instance& instance{scene.instances[hit.instance]};
            const BvhLib::Triangle& triangle{scene.geometries[instance.geometry].triangles[hit.triangle]};

            // Out to world space, because the edges are in the instance's own frame. Exact for a
            // rigid placement; under a non-uniform scale the inverse-transpose would be needed.
            const MathLib::Vec4 rotated{instance.to_world * MathLib::Vec4::fromVec3(triangle.geometricNormal(), 0.0f)};
            return MathLib::Vec3{rotated.x, rotated.y, rotated.z}.normalised();
        }

        //! True when nothing stands between two points held apart in open air. The epsilon keeps a
        //! probe from striking the very surface one of its endpoints was nudged off.
        [[nodiscard]] bool segmentClear(const BvhLib::Scene& scene, const MathLib::Vec3& from, const MathLib::Vec3& to, uint32_t skip_a, uint32_t skip_b)
        {
            const MathLib::Vec3 offset{to - from};
            const float length{std::sqrt(offset.dot(offset))};
            if (length <= SURFACE_EPSILON) {
                return true; // Two points this close share their air.
            }

            const MathLib::Vec3 direction{offset * (1.0f / length)};
            return !BvhLib::intersectScene(scene, from, direction, length - SURFACE_EPSILON, skip_a, skip_b).valid;
        }

        /*!
            True when real geometry stands exactly at `point`, aligned with the mirror plane, and
            the way there from `from` is clear.

            This is the validation ray the enumeration's honesty rests on. The nearest hit must lie
            *at* the reflection point — nearer means the leg is blocked, farther or nothing means
            the plane is bare there — and the struck face must actually lie in the mirror plane
            rather than merely pass through the point, or a wall standing on a terrace would
            answer for the terrace with a vertical mirror's arithmetic applied to a horizontal one.
        */
        [[nodiscard]] bool mirrorPointStands(const BvhLib::Scene& scene, const MathLib::Vec3& from, const MathLib::Vec3& point, const MathLib::Vec3& plane_normal,
            uint32_t skip_a, uint32_t skip_b)
        {
            const MathLib::Vec3 offset{point - from};
            const float length{std::sqrt(offset.dot(offset))};
            if (length <= SURFACE_EPSILON) {
                return false; // A reflection point on top of an endpoint is a degenerate path.
            }

            const MathLib::Vec3 direction{offset * (1.0f / length)};
            const BvhLib::Hit hit{BvhLib::intersectScene(scene, from, direction, length + SURFACE_EPSILON, skip_a, skip_b)};
            if (!hit.valid || (std::fabs(hit.distance - length) > SURFACE_EPSILON)) {
                return false;
            }

            return std::fabs(struckFaceNormal(scene, hit).dot(plane_normal)) >= MIRROR_ALIGNMENT_MINIMUM;
        }

        /*!
            One validated image-source path, or nothing.

            The construction is the classical one: mirror the source in the plane, and the straight
            line from the image to the ear crosses the plane at the reflection point, with the
            image-to-ear distance equal to the two legs' sum exactly. Both endpoints must stand
            clear of the plane on the same side — a source on the far side has no reflection, and
            one *on* the plane has a degenerate one.

            \param plane_normal Unit normal of the mirror plane.
            \param plane_distance The plane as `dot(normal, x) == distance`.
            \return The reflection point and total path, or `valid == false`.
        */
        struct MirrorPath {
            MathLib::Vec3 point{};
            float path{0.0f};
            bool valid{false};
        };

        [[nodiscard]] MirrorPath mirrorInPlane(const MathLib::Vec3& source, const MathLib::Vec3& ear, const MathLib::Vec3& plane_normal, float plane_distance)
        {
            const float side_source{plane_normal.dot(source) - plane_distance};
            const float side_ear{plane_normal.dot(ear) - plane_distance};

            const bool same_side{((side_source > SURFACE_EPSILON) && (side_ear > SURFACE_EPSILON)) //
                || ((side_source < -SURFACE_EPSILON) && (side_ear < -SURFACE_EPSILON))};
            if (!same_side) {
                return MirrorPath{};
            }

            const MathLib::Vec3 image{source - (plane_normal * (2.0f * side_source))};
            const MathLib::Vec3 image_to_ear{ear - image};

            // Both sides carry the same sign, so the fraction is in (0, 1) and the crossing point
            // lies strictly between the endpoints' projections.
            const float fraction{side_source / (side_source + side_ear)};

            MirrorPath result{};
            result.point = image + (image_to_ear * fraction);
            result.path = std::sqrt(image_to_ear.dot(image_to_ear));
            result.valid = true;
            return result;
        }

        /*!
            Validates one image candidate end to end and deposits it if it survives.

            Two rays per candidate: the source-to-point leg doubles as the presence test — its
            nearest hit must *be* the reflection point, which proves both that the leg is clear and
            that the mirror is real there — and the point-to-ear leg needs only clearance, cast
            from a point nudged off the surface towards the listener's side.
        */
        void deliverImage(ImpulseResponse& response, const BvhLib::Scene& scene, const MathLib::Vec3& source, float strength, const MathLib::Vec3& ear,
            const CallConfig& config, const MathLib::Vec3& plane_normal, const MirrorPath& mirror, float radial_velocity)
        {
            if (!mirror.valid || (mirror.path > config.range_metres)) {
                return;
            }

            if (!mirrorPointStands(scene, source, mirror.point, plane_normal, config.caller_instance, config.listener_instance)) {
                return;
            }

            const float side{plane_normal.dot(source - mirror.point)};
            const MathLib::Vec3 nudged{mirror.point + (plane_normal * ((side > 0.0f) ? SURFACE_EPSILON : -SURFACE_EPSILON))};
            if (!segmentClear(scene, nudged, ear, config.caller_instance, config.listener_instance)) {
                return;
            }

            depositCallArrival(response, mirror.path, strength, config, radial_velocity);
        }

    } // namespace

    std::vector<float> makeAcousticSourceStrengths()
    {
        std::vector<float> strengths(MATERIAL_SLOT_COUNT, 0.0f);

        /*
            Only the tubes sing, and the two of them sing equally: they are identical hardware
            differing in gas colour, which is an optical property and not an acoustic one. The
            primary tube is the unit of the scale, so its strength is 1 by definition.

            Everything else is silent, and the pillars are the case that matters — they are
            `makeEmissive` and therefore optically bright, which is exactly why this table is
            authored rather than derived from the optical one.
        */
        strengths[MATERIAL_NEON_PRIMARY] = 1.0f;
        strengths[MATERIAL_NEON_ACCENT] = 1.0f;

        return strengths;
    }

    float ImpulseResponse::total() const
    {
        return std::accumulate(bins.begin(), bins.end(), 0.0f);
    }

    MathLib::Vec3 fibonacciDirection(uint32_t index, uint32_t count)
    {
        const uint32_t safe_count{std::max(count, 1u)};

        /*
            The half-offset matters. Without it the first point sits exactly at the north pole and
            the last exactly at the south, which wastes two of the directions on a pair that a
            uniform set would never place there and leaves the equator correspondingly thinner.
        */

        const float height{1.0f - ((2.0f * (static_cast<float>(index) + 0.5f)) / static_cast<float>(safe_count))};

        // Clamped because a height a hair outside [-1, 1] from rounding would take a square root of
        // a negative number, and the resulting NaN would poison a whole ray rather than one bin.
        const float radius{std::sqrt(std::max(0.0f, 1.0f - (height * height)))};

        const float turns{static_cast<float>(index) * GOLDEN_TURN_FRACTION};
        const float theta{(turns - std::floor(turns)) * TWO_PI};

        return MathLib::Vec3{radius * std::cos(theta), height, radius * std::sin(theta)};
    }

    ImpulseResponse gather(const BvhLib::Bvh& bvh, const std::vector<float>& source_strengths, const MathLib::Vec3& ear, const GatherConfig& config)
    {
        // One geometry at the identity, which is what the Grid is on the device as well. Expressing
        // the simple case as the general one rather than beside it means there is a single gather to
        // keep correct, and an instance sitting anywhere but the identity costs nothing to support.
        BvhLib::Scene scene{};
        scene.geometries.push_back(bvh);
        scene.instances.push_back(BvhLib::makeInstance(bvh, 0u, MathLib::Mat4::identity()));

        return gather(scene, source_strengths, ear, config);
    }

    ImpulseResponse gather(const BvhLib::Scene& scene, const std::vector<float>& source_strengths, const MathLib::Vec3& ear, const GatherConfig& config)
    {
        ImpulseResponse response{};

        if (scene.instances.empty() || source_strengths.empty() || (config.direction_count == 0u)) {
            return response;
        }

        const float range{std::max(config.range_metres, 0.0f)};

        for (uint32_t direction_index{0u}; direction_index < config.direction_count; ++direction_index) {
            MathLib::Vec3 origin{ear};
            MathLib::Vec3 direction{fibonacciDirection(direction_index, config.direction_count)};

            // Accumulated path length is what makes this an acoustic ray rather than a visual one.
            // It only ever grows, it decides which bin an arrival lands in, and it is the primary
            // termination rule.
            float path{0.0f};

            for (uint32_t order{0u}; order <= config.max_order; ++order) {
                // The remaining path budget is the ray's own limit, so a ray never travels further
                // than the cap even in one segment.
                const float remaining{range - path};
                if (remaining <= 0.0f) {
                    break;
                }

                const BvhLib::Hit hit{BvhLib::intersectScene(scene, origin, direction, remaining, config.skip_instance)};
                if (!hit.valid) {
                    break; // Escaped. The Grid is an open plane, so most rays end here.
                }

                path += hit.distance;

                const BvhLib::Instance& instance{scene.instances[hit.instance]};
                const BvhLib::Triangle& triangle{scene.geometries[instance.geometry].triangles[hit.triangle]};
                /*
                    A material index past the end of the table is a caller error, but reading past a
                    vector's end is undefined behaviour and would be found by a crash somewhere else
                    entirely. One compare per hit makes it defined: an unknown surface is silent, and
                    still reflects, which is the only sane reading of a surface nobody described.
                */
                const float source_strength{(triangle.material < source_strengths.size()) ? source_strengths[triangle.material] : 0.0f};

                if (source_strength > 0.0f) {
                    depositArrival(response, path, source_strength, config.hum_spectrum, config.air_absorption_db_per_km);
                }

                /*
                    Reflect losslessly. Nothing is subtracted here and nothing needs to be: every
                    surface on the Grid is a perfect acoustic mirror, and what bounds the response
                    is the range cap above, the order cap on this loop, and the spreading and air
                    terms applied at the deposit. See the note in the header for why that is safe on
                    an open plane and where it would stop being safe.
                */
                // `acoustics.slang` derives the same normal from the same three rows, and
                // `struckFaceNormal` is the line it is held to.
                const MathLib::Vec3 face_normal{struckFaceNormal(scene, hit)};

                // Reflect about the face the ray actually arrived at, whichever side that is: a
                // creature standing in a terrace hollow hears its walls from the inside.
                const MathLib::Vec3 normal{(direction.dot(face_normal) < 0.0f) ? face_normal : -face_normal};

                const MathLib::Vec3 hit_position{origin + (direction * hit.distance)};
                origin = hit_position + (normal * SURFACE_EPSILON);
                direction = direction - (normal * (2.0f * direction.dot(normal)));
            }
        }

        return response;
    }

    std::array<RectFace, 5u> outwardBoxFaces(const MathLib::Vec3& centre, const MathLib::Vec3& half_extents)
    {
        const MathLib::Vec3 low{centre - half_extents};
        const MathLib::Vec3 high{centre + half_extents};

        const MathLib::Vec3 span_x{2.0f * half_extents.x, 0.0f, 0.0f};
        const MathLib::Vec3 span_y{0.0f, 2.0f * half_extents.y, 0.0f};
        const MathLib::Vec3 span_z{0.0f, 0.0f, 2.0f * half_extents.z};

        // Each corner and edge pair is chosen so that cross(edge_u, edge_v) points away from the
        // centre, which is the whole meaning of the winding.
        return std::array<RectFace, 5u>{
            RectFace{.origin = MathLib::Vec3{high.x, low.y, low.z}, .edge_u = span_y, .edge_v = span_z}, // +X: y cross z
            RectFace{.origin = MathLib::Vec3{low.x, low.y, high.z}, .edge_u = span_y, .edge_v = span_z * -1.0f}, // -X: y cross -z
            RectFace{.origin = MathLib::Vec3{low.x, high.y, low.z}, .edge_u = span_z, .edge_v = span_x}, // +Y, the top: z cross x
            RectFace{.origin = MathLib::Vec3{low.x, low.y, high.z}, .edge_u = span_x, .edge_v = span_y}, // +Z: x cross y
            RectFace{.origin = MathLib::Vec3{high.x, low.y, low.z}, .edge_u = span_x * -1.0f, .edge_v = span_y}, // -Z: -x cross y
        };
    }

    ImpulseResponse deliverCall(const BvhLib::Scene& scene, const Reflectors& reflectors, const MathLib::Vec3& source, float strength, const MathLib::Vec3& ear,
        const CallConfig& config)
    {
        ImpulseResponse response{};

        // A negative loudness is not a quieter sound but a meaningless one, and the sanitiser
        // upstream agrees; guarded here as well because this function's contract should not depend
        // on who called it. An empty Grid is deliberately not an early out: the direct path needs
        // air rather than triangles, so a call crosses an empty world at full strength while every
        // image candidate fails its validation for want of anything to stand on.
        if (strength <= 0.0f) {
            return response;
        }

        // The radial velocity: how fast the path from ear to source lengthens - positive
        // receding, negative approaching - from the settled velocities the gather already reads.
        const MathLib::Vec3 ear_to_source{source - ear};
        const float separation{ear_to_source.length()};
        const float radial_velocity{(separation > 1e-6f) ? (config.source_velocity - config.listener_velocity).dot(ear_to_source * (1.0f / separation)) : 0.0f};

        /*
            The direct path, graded rather than gated. The probe asks how much of a small sphere
            around the source the ear can see, and the fraction scales the arrival — so a thin post
            dims the call where a binary ray would silence it. The delay is the true source-to-ear
            distance regardless: the samples are an occlusion probe, not sources of their own.
        */
        const MathLib::Vec3 to_ear{ear - source};
        const float direct_distance{std::sqrt(to_ear.dot(to_ear))};
        if (direct_distance <= config.range_metres) {
            uint32_t clear_count{0u};
            uint32_t sample_count{1u};

            if ((config.source_radius_metres <= 0.0f) || (config.occlusion_sample_count <= 1u)) {
                clear_count = segmentClear(scene, ear, source, config.caller_instance, config.listener_instance) ? 1u : 0u;
            } else {
                sample_count = config.occlusion_sample_count;
                for (uint32_t sample{0u}; sample < sample_count; ++sample) {
                    const MathLib::Vec3 probe{source + (fibonacciDirection(sample, sample_count) * config.source_radius_metres)};
                    if (segmentClear(scene, ear, probe, config.caller_instance, config.listener_instance)) {
                        ++clear_count;
                    }
                }
            }

            if (clear_count > 0u) {
                const float fraction{static_cast<float>(clear_count) / static_cast<float>(sample_count)};
                depositCallArrival(response, direct_distance, strength * fraction, config, radial_velocity);
            }
        }

        // The terrace levels: horizontal mirror planes at the floor's own heights.
        const MathLib::Vec3 up{0.0f, 1.0f, 0.0f};
        for (const float height : reflectors.level_heights) {
            deliverImage(response, scene, source, strength, ear, config, up, mirrorInPlane(source, ear, up, height), radial_velocity);
        }

        // The box faces: finite rectangles, so the reflection point must land inside the face
        // before any ray is spent on it. The edges are perpendicular by RectFace's own contract,
        // which is what lets each coordinate be tested independently.
        for (const RectFace& face : reflectors.faces) {
            const MathLib::Vec3 cross{face.edge_u.cross(face.edge_v)};
            const float cross_length{std::sqrt(cross.dot(cross))};
            if (cross_length <= 0.0f) {
                continue; // A degenerate face has no plane to mirror in.
            }
            const MathLib::Vec3 normal{cross * (1.0f / cross_length)};

            const MirrorPath mirror{mirrorInPlane(source, ear, normal, normal.dot(face.origin))};
            if (!mirror.valid) {
                continue;
            }

            const MathLib::Vec3 local{mirror.point - face.origin};
            const float along_u{local.dot(face.edge_u) / face.edge_u.dot(face.edge_u)};
            const float along_v{local.dot(face.edge_v) / face.edge_v.dot(face.edge_v)};
            if ((along_u < 0.0f) || (along_u > 1.0f) || (along_v < 0.0f) || (along_v > 1.0f)) {
                continue;
            }

            deliverImage(response, scene, source, strength, ear, config, normal, mirror, radial_velocity);
        }

        return response;
    }

} // namespace Acoustics
