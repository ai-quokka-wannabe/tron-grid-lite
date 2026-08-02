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

        /*!
            Octave centre frequencies of the tabulated air-absorption curve, in hertz.

            The eight ISO 266 preferred centres ISO 9613-2 tabulates, and no more. Extending the
            table upward is a documented open item — see `airAbsorptionDbPerKm`.
        */
        constexpr std::array<float, 8> AIR_ABSORPTION_FREQUENCIES{63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f};

        /*!
            Atmospheric absorption at those centres, in dB/km, at 20 °C and 70 % relative humidity.

            ISO 9613-2's tabulated row for those conditions. The curve climbs by roughly a factor of
            three per octave at the top end, which is why the Grid has a physical acoustic horizon
            for anything that hears above a few kilohertz.
        */
        constexpr std::array<float, 8> AIR_ABSORPTION_DB_PER_KM{0.1f, 0.3f, 1.1f, 2.8f, 5.0f, 9.0f, 22.9f, 76.6f};

        //! Golden angle, in radians: the increment that spaces a Fibonacci spiral evenly.
        const float GOLDEN_ANGLE{3.14159265358979323846f * (3.0f - std::sqrt(5.0f))};

    } // namespace

    std::vector<AcousticMaterial> makeAcousticMaterials()
    {
        std::vector<AcousticMaterial> materials(MATERIAL_SLOT_COUNT);

        // Polished hard surface. The most consequential number in the model, because every path
        // touches the floor.
        materials[MATERIAL_FLOOR] = AcousticMaterial{.absorption = 0.02f, .source_strength = 0.0f};

        // The two tube materials are identical hardware — they differ in gas colour, which is an
        // optical property and not an acoustic one. The primary tube is the unit of the scale.
        materials[MATERIAL_NEON_PRIMARY] = AcousticMaterial{.absorption = 0.02f, .source_strength = 1.0f};
        materials[MATERIAL_NEON_ACCENT] = AcousticMaterial{.absorption = 0.02f, .source_strength = 1.0f};

        // Optically emissive, acoustically silent — the whole reason this table is authored
        // separately from the optical one rather than derived from it.
        materials[MATERIAL_PILLAR] = AcousticMaterial{.absorption = 0.03f, .source_strength = 0.0f};

        materials[MATERIAL_GLASS] = AcousticMaterial{.absorption = 0.03f, .source_strength = 0.0f}; // 3 mm glass, mid-band.
        materials[MATERIAL_GLOWING_GLASS] = AcousticMaterial{.absorption = 0.03f, .source_strength = 0.0f}; // As the slabs, and likewise silent.

        return materials;
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
        const float theta{GOLDEN_ANGLE * static_cast<float>(index)};

        return MathLib::Vec3{radius * std::cos(theta), height, radius * std::sin(theta)};
    }

    float airAbsorptionDbPerKm(float frequency_hz)
    {
        if (frequency_hz <= 0.0f) {
            return 0.0f;
        }

        const std::size_t last{AIR_ABSORPTION_FREQUENCIES.size() - 1u};

        // Below the table the curve is flat enough, and nothing on the roster hears there anyway:
        // the lowest band edge in the whole roster is C. elegans' 100 Hz.
        if (frequency_hz <= AIR_ABSORPTION_FREQUENCIES.front()) {
            return AIR_ABSORPTION_DB_PER_KM.front();
        }

        /*
            Interpolation is linear in the logarithm of both axes, because that is the shape of the
            data: the frequencies are octave centres, so they are uniform in log f, and the levels
            climb by a roughly constant factor per octave rather than a constant amount. Linear
            interpolation between 22.9 and 76.6 would understate the middle of that octave by about
            a third.

            Above the table the same slope is continued from the last pair, which is the documented
            extrapolation. See the header: it is the right asymptotic shape and the wrong number,
            and it must not be relied on above 8 kHz.
        */
        std::size_t lower{last - 1u};
        if (frequency_hz < AIR_ABSORPTION_FREQUENCIES[last]) {
            for (std::size_t index{0u}; index < last; ++index) {
                if (frequency_hz <= AIR_ABSORPTION_FREQUENCIES[index + 1u]) {
                    lower = index;
                    break;
                }
            }
        }

        const float log_frequency{std::log(frequency_hz)};
        const float log_low{std::log(AIR_ABSORPTION_FREQUENCIES[lower])};
        const float log_high{std::log(AIR_ABSORPTION_FREQUENCIES[lower + 1u])};
        const float log_level_low{std::log(AIR_ABSORPTION_DB_PER_KM[lower])};
        const float log_level_high{std::log(AIR_ABSORPTION_DB_PER_KM[lower + 1u])};

        const float t{(log_frequency - log_low) / (log_high - log_low)};
        return std::exp(log_level_low + (t * (log_level_high - log_level_low)));
    }

    ImpulseResponse gather(const BvhLib::Bvh& bvh, const std::vector<AcousticMaterial>& materials, const MathLib::Vec3& ear, const GatherConfig& config)
    {
        ImpulseResponse response{};

        if (bvh.nodes.empty() || materials.empty() || (config.direction_count == 0u)) {
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

            // Energy still carried after the reflections so far, before spreading and air. Exactly
            // the role `throughput` plays in trace.slang.
            float throughput{1.0f};

            for (uint32_t order{0u}; order <= config.max_order; ++order) {
                // The remaining path budget is the ray's own limit, so a ray never travels further
                // than the cap even in one segment.
                const float remaining{range - path};
                if (remaining <= 0.0f) {
                    break;
                }

                const BvhLib::Hit hit{BvhLib::intersect(bvh, origin, direction, remaining)};
                if (!hit.valid) {
                    break; // Escaped. The Grid is an open plane, so most rays end here.
                }

                path += hit.distance;

                const BvhLib::Triangle& triangle{bvh.triangles[hit.triangle]};
                const AcousticMaterial& material{materials[triangle.material]};

                if (material.source_strength > 0.0f) {
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

                            response.at(band, bin) += material.source_strength * config.hum_spectrum[band] * throughput * spreading * air;
                        }
                    }
                }

                // Deposit first, then reflect — the same order in which trace.slang accumulates
                // emission before it follows the reflected branch.
                throughput *= (1.0f - material.absorption);
                if (throughput <= 0.0f) {
                    break;
                }

                const MathLib::Vec3 face_normal{triangle.edge1.cross(triangle.edge2).normalised()};

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
