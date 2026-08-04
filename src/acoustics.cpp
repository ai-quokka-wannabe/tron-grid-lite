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
#include <numeric>

namespace Acoustics
{

    namespace
    {

        //! One full turn, in radians.
        constexpr float TWO_PI{6.28318530717958648f};

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

                const BvhLib::Hit hit{BvhLib::intersectScene(scene, origin, direction, remaining)};
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
                    /*
                        Spreading is explicit and is measured from a one-metre reference, so a
                        source exactly one metre away arrives at unit strength and nothing closer
                        than that is amplified. Without the floor, a ray that grazes a tube it is
                        almost touching would divide by an arbitrarily small number and deposit an
                        arbitrarily large amount of energy into one bin.
                    */
                    const float spreading{1.0f / std::max(path * path, 1.0f)};

                    const uint32_t bin{static_cast<uint32_t>(path / (SPEED_OF_SOUND * BIN_SECONDS))};
                    if (bin < BIN_COUNT) {
                        for (uint32_t band{0u}; band < BAND_COUNT; ++band) {
                            // Energy, not pressure, so the decibels divide by ten rather than twenty.
                            const float air_db{config.air_absorption_db_per_km[band] * (path / 1000.0f)};
                            const float air{std::pow(10.0f, -air_db / 10.0f)};

                            response.at(band, bin) += source_strength * config.hum_spectrum[band] * spreading * air;
                        }
                    }
                }

                /*
                    Reflect losslessly. Nothing is subtracted here and nothing needs to be: every
                    surface on the Grid is a perfect acoustic mirror, and what bounds the response
                    is the range cap above, the order cap on this loop, and the spreading and air
                    terms applied at the deposit. See the note in the header for why that is safe on
                    an open plane and where it would stop being safe.
                */
                // Out to world space before reflecting, because the edges are in the instance's own
                // frame and the ray is not. Exact for a rigid placement; under a non-uniform scale
                // the inverse-transpose would be needed instead. `acoustics.slang` does the same
                // thing with the same three rows, and this is the line it is held to.
                const MathLib::Vec4 rotated{instance.to_world * MathLib::Vec4::fromVec3(triangle.edge1.cross(triangle.edge2), 0.0f)};
                const MathLib::Vec3 face_normal{MathLib::Vec3{rotated.x, rotated.y, rotated.z}.normalised()};

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

} // namespace Acoustics
