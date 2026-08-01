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

#include "../geometry.hpp"
#include <testing/testing.hpp>
#include <cmath>
#include <cstdint>

namespace
{

    //! The scene's real floor settings, so the tests exercise what the renderer actually builds.
    [[nodiscard]] GridFloorConfig sceneConfig()
    {
        return GridFloorConfig{.cells = 64u, .cell_size = 2.0f, .height = 0.0f};
    }

    //! Largest disagreement tolerated between two float paths that should agree exactly.
    constexpr float EPSILON{1e-4f};

}

TEST_CASE(relief_is_deterministic_across_calls)
{
    const GridFloorConfig config{sceneConfig()};

    // The recording mode depends on this: a landscape that differed between runs would make every
    // recorded frame differ too.
    for (int32_t step{-40}; step <= 40; ++step) {
        const float x{static_cast<float>(step) * 1.7f};
        const float z{static_cast<float>(step) * -2.3f};
        TEST_CHECK(gridSurfaceHeight(x, z, config) == gridSurfaceHeight(x, z, config));
        TEST_CHECK(gridMeshHeight(x, z, config) == gridMeshHeight(x, z, config));
    }
}

TEST_CASE(relief_stays_within_its_stated_amplitude)
{
    const GridFloorConfig config{sceneConfig()};
    const float half_size{(static_cast<float>(config.cells) * config.cell_size) * 0.5f};

    for (uint32_t vertex_z{0u}; vertex_z <= config.cells; ++vertex_z) {
        for (uint32_t vertex_x{0u}; vertex_x <= config.cells; ++vertex_x) {
            const float x{(static_cast<float>(vertex_x) * config.cell_size) - half_size};
            const float z{(static_cast<float>(vertex_z) * config.cell_size) - half_size};
            const float height{gridSurfaceHeight(x, z, config)};

            TEST_CHECK(height >= config.height - EPSILON);
            TEST_CHECK(height <= config.height + config.relief_amplitude + EPSILON);
        }
    }
}

TEST_CASE(zero_amplitude_gives_a_flat_floor)
{
    GridFloorConfig config{sceneConfig()};
    config.relief_amplitude = 0.0f;
    config.height = 3.5f;

    for (int32_t step{-30}; step <= 30; ++step) {
        const float x{static_cast<float>(step) * 2.1f};
        const float z{static_cast<float>(step) * 1.3f};
        TEST_CHECK(std::abs(gridSurfaceHeight(x, z, config) - 3.5f) < EPSILON);
        TEST_CHECK(std::abs(gridMeshHeight(x, z, config) - 3.5f) < EPSILON);
    }
}

TEST_CASE(mesh_height_agrees_with_the_analytic_surface_at_grid_vertices)
{
    const GridFloorConfig config{sceneConfig()};
    const float half_size{(static_cast<float>(config.cells) * config.cell_size) * 0.5f};

    // The mesh samples the analytic surface exactly at its vertices and interpolates only between
    // them, so at a vertex the two must return the same number.
    for (uint32_t vertex_z{0u}; vertex_z <= config.cells; vertex_z += 3u) {
        for (uint32_t vertex_x{0u}; vertex_x <= config.cells; vertex_x += 3u) {
            const float x{(static_cast<float>(vertex_x) * config.cell_size) - half_size};
            const float z{(static_cast<float>(vertex_z) * config.cell_size) - half_size};
            TEST_CHECK(std::abs(gridMeshHeight(x, z, config) - gridSurfaceHeight(x, z, config)) < EPSILON);
        }
    }
}

/*
    The regression test for the defect this function was written to fix.

    `plantOnFloor` originally asked `gridSurfaceHeight`, which is a step function once the relief is
    terraced, while the drawn mesh ramps linearly across whichever cell a riser passes through. The
    two disagree by up to a full terrace step, and the glowing column ended up standing 0.29 m clear
    of its own reflection.

    Every triangle's centroid lies strictly inside that triangle, so the mesh's own height there is
    unambiguous, and `gridMeshHeight` must reproduce it.
*/
TEST_CASE(mesh_height_reproduces_the_drawn_floor_triangles)
{
    const GridFloorConfig config{sceneConfig()};
    const Mesh floor{generateGridFloor(config)};

    TEST_CHECK(floor.triangleCount() > 0u);

    std::size_t checked{0u};
    for (std::size_t index{0u}; (index + 2u) < floor.indices.size(); index += 3u) {
        const Vertex& a{floor.vertices[floor.indices[index]]};
        const Vertex& b{floor.vertices[floor.indices[index + 1u]]};
        const Vertex& c{floor.vertices[floor.indices[index + 2u]]};

        const float centroid_x{(a.position[0] + b.position[0] + c.position[0]) / 3.0f};
        const float centroid_y{(a.position[1] + b.position[1] + c.position[1]) / 3.0f};
        const float centroid_z{(a.position[2] + b.position[2] + c.position[2]) / 3.0f};

        TEST_CHECK(std::abs(gridMeshHeight(centroid_x, centroid_z, config) - centroid_y) < EPSILON);
        ++checked;
    }

    // Guards against the check above passing because the loop never ran.
    TEST_CHECK(checked == floor.triangleCount());
}

/*
    The neon tubes are laid along the floor's own grid lines, so a tube endpoint and the floor
    beneath it must be derived from the same surface. If they ever drift apart the tubes float above
    the terraces or sink into them, and because both call the same function with coordinates built
    by the same expression, drift can only come from someone changing one and not the other.
*/
TEST_CASE(neon_tube_endpoints_sit_on_the_floor_surface)
{
    const GridFloorConfig config{sceneConfig()};
    const NeonTubeConfig tube_config{};
    const NeonGrid neon{generateGridFloorNeon(config, tube_config)};

    TEST_CHECK(!neon.primary.empty());
    TEST_CHECK(!neon.accent.empty());

    // A tube is lifted vertically clear of the floor, and widening it horizontally on a slope costs
    // some of that clearance, so the band is bounded below by zero rather than by the full lift.
    const float widest_drop{tube_config.half_width * (config.relief_amplitude / config.cell_size)};

    for (const Mesh* mesh : {&neon.primary, &neon.accent}) {
        for (const Vertex& vertex : mesh->vertices) {
            const float floor_height{gridMeshHeight(vertex.position[0], vertex.position[2], config)};
            const float clearance{vertex.position[1] - floor_height};

            TEST_CHECK(clearance > -EPSILON);
            TEST_CHECK(clearance < tube_config.surface_offset + widest_drop + EPSILON);
        }
    }
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
