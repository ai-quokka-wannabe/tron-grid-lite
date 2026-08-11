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

TEST_CASE(mesh_height_agrees_with_the_analytic_surface_at_cell_centres)
{
    const GridFloorConfig config{sceneConfig()};
    const float half_size{(static_cast<float>(config.cells) * config.cell_size) * 0.5f};

    // A cell's level is the quantised relief at its own centre, so at every centre the drawn floor
    // and the analytic surface must return the same number — and across the rest of the cell the
    // drawn floor must hold that number flat.
    for (uint32_t cell_z{0u}; cell_z < config.cells; cell_z += 3u) {
        for (uint32_t cell_x{0u}; cell_x < config.cells; cell_x += 3u) {
            const float centre_x{((static_cast<float>(cell_x) + 0.5f) * config.cell_size) - half_size};
            const float centre_z{((static_cast<float>(cell_z) + 0.5f) * config.cell_size) - half_size};

            const float level{gridMeshHeight(centre_x, centre_z, config)};
            TEST_CHECK(std::abs(level - gridSurfaceHeight(centre_x, centre_z, config)) < EPSILON);

            // Anywhere else in the same cell: the same level, exactly.
            TEST_CHECK(gridMeshHeight(centre_x - (config.cell_size * 0.4f), centre_z + (config.cell_size * 0.3f), config) == level);
            TEST_CHECK(gridMeshHeight(centre_x + (config.cell_size * 0.45f), centre_z - (config.cell_size * 0.45f), config) == level);
        }
    }
}

/*
    What `gridMeshHeight` is for, and what goes wrong without it.

    `plantOnFloor` must ask it rather than `gridSurfaceHeight`: the analytic relief's terrace
    boundaries curve wherever the noise puts them, while the drawn floor snaps every cell flat at
    the level of its own centre. The two disagree wherever a point's analytic level differs from
    its cell's — enough to bury or float anything planted off the drawn surface.

    Every flat triangle's centroid lies strictly inside that triangle, so the mesh's own height
    there is unambiguous, and `gridMeshHeight` must reproduce it. Riser triangles are vertical —
    their footprint is a line on a cell boundary, exactly where the height jumps — so they are
    held to their verticality rather than to a height they deliberately do not have.
*/
TEST_CASE(mesh_height_reproduces_the_drawn_floor_and_the_walls_stand_vertical)
{
    const GridFloorConfig config{sceneConfig()};
    const Mesh floor{generateGridFloor(config)};

    TEST_CHECK(floor.triangleCount() > 0u);

    std::size_t flat_checked{0u};
    std::size_t walls_checked{0u};
    for (std::size_t index{0u}; (index + 2u) < floor.indices.size(); index += 3u) {
        const Vertex& a{floor.vertices[floor.indices[index]]};
        const Vertex& b{floor.vertices[floor.indices[index + 1u]]};
        const Vertex& c{floor.vertices[floor.indices[index + 2u]]};

        // Every facet of the drawn floor is horizontal or vertical; there is nothing in between
        // for a reflection to be deflected by, which is the whole point of the terraced mesh.
        const bool horizontal{std::abs(a.normal[1]) > 0.999f};
        const bool vertical{std::abs(a.normal[1]) < 0.001f};
        TEST_CHECK(horizontal || vertical);

        if (horizontal) {
            const float centroid_x{(a.position[0] + b.position[0] + c.position[0]) / 3.0f};
            const float centroid_y{(a.position[1] + b.position[1] + c.position[1]) / 3.0f};
            const float centroid_z{(a.position[2] + b.position[2] + c.position[2]) / 3.0f};

            TEST_CHECK(std::abs(gridMeshHeight(centroid_x, centroid_z, config) - centroid_y) < EPSILON);
            ++flat_checked;
        } else {
            ++walls_checked;
        }
    }

    // Guards against the checks above passing because a loop never ran: every cell contributes two
    // flat triangles, and this landscape genuinely steps.
    TEST_CHECK(flat_checked == (static_cast<std::size_t>(config.cells) * config.cells * 2u));
    TEST_CHECK(walls_checked > 0u);

    // The walls are the riser list made triangles, two each, and nothing else is vertical: the
    // mirror an echo comes back off is the wall the picture shows, because both come from
    // gridRiserWalls.
    TEST_CHECK(walls_checked == (gridRiserWalls(config).size() * 2u));
}

TEST_CASE(riser_walls_are_vertical_and_face_the_lower_cell)
{
    const GridFloorConfig config{sceneConfig()};
    const std::vector<GridWall> walls{gridRiserWalls(config)};

    TEST_CHECK(!walls.empty());

    for (const GridWall& wall : walls) {
        // Along a boundary and straight up: nothing else is a riser.
        TEST_CHECK(wall.edge_u.y == 0.0f);
        TEST_CHECK(wall.edge_v.x == 0.0f);
        TEST_CHECK(wall.edge_v.z == 0.0f);
        TEST_CHECK(wall.edge_v.y > 0.0f);

        /*
            The winding is the statement of which side reflects. A step off the wall's outward side
            must land on the lower level — the wall's own foot — and a step the other way on the
            upper, because the outward side is the air a creature stands in and the other side is
            the inside of the hill.
        */
        const MathLib::Vec3 centre{wall.origin + ((wall.edge_u + wall.edge_v) * 0.5f)};
        const MathLib::Vec3 outward{wall.edge_u.cross(wall.edge_v).normalised()};
        const float quarter{config.cell_size * 0.25f};

        const float lower{gridMeshHeight(centre.x + (outward.x * quarter), centre.z + (outward.z * quarter), config)};
        const float upper{gridMeshHeight(centre.x - (outward.x * quarter), centre.z - (outward.z * quarter), config)};

        TEST_CHECK(std::abs(lower - wall.origin.y) < EPSILON);
        TEST_CHECK(std::abs(upper - (wall.origin.y + wall.edge_v.y)) < EPSILON);
    }
}

/*
    The neon tubes lie flat on the lips: each edge's tube takes the higher of the two cell levels
    it borders, lighting the terrace edges while the risers below stay dark cliffs. The tubes and
    the floor derive from the same cell levels, so drift can only come from someone changing one
    and not the other.
*/
TEST_CASE(neon_tubes_lie_flat_on_the_lip_of_their_edges)
{
    const GridFloorConfig config{sceneConfig()};
    const NeonTubeConfig tube_config{};
    const NeonGrid neon{generateGridFloorNeon(config, tube_config)};

    TEST_CHECK(!neon.primary.empty());
    TEST_CHECK(!neon.accent.empty());

    for (const Mesh* mesh : {&neon.primary, &neon.accent}) {
        // Every tube triangle is horizontal: a tube never ramps, because there is no ramp anywhere
        // on the drawn floor for it to follow.
        for (std::size_t index{0u}; (index + 2u) < mesh->indices.size(); index += 3u) {
            const float y0{mesh->vertices[mesh->indices[index]].position[1]};
            const float y1{mesh->vertices[mesh->indices[index + 1u]].position[1]};
            const float y2{mesh->vertices[mesh->indices[index + 2u]].position[1]};
            TEST_CHECK(y0 == y1);
            TEST_CHECK(y1 == y2);
        }

        /*
            And every tube lies at exactly its edge's lip: the higher of the two cell levels the
            edge borders, plus the lift. Measured at the quad's own midpoint, a quarter cell into
            each flank, because the midpoint is the one place along an edge with no corner
            ambiguity — an endpoint sits at a four-cell corner, where a tube dead-ending into a
            perpendicular wall pokes its last centimetre into the hill, invisibly and harmlessly.
        */
        for (std::size_t index{0u}; (index + 5u) < mesh->vertices.size(); index += 6u) {
            const Vertex& p0{mesh->vertices[index]};
            const Vertex& p1{mesh->vertices[index + 1u]};
            const Vertex& p2{mesh->vertices[index + 2u]};

            const float mid_x{(p0.position[0] + p2.position[0]) * 0.5f};
            const float mid_z{(p0.position[2] + p2.position[2]) * 0.5f};

            float side_x{p1.position[0] - p0.position[0]};
            float side_z{p1.position[2] - p0.position[2]};
            const float side_length{std::sqrt((side_x * side_x) + (side_z * side_z))};
            side_x /= side_length;
            side_z /= side_length;

            const float quarter{config.cell_size * 0.25f};
            const float one_flank{gridMeshHeight(mid_x + (side_x * quarter), mid_z + (side_z * quarter), config)};
            const float other_flank{gridMeshHeight(mid_x - (side_x * quarter), mid_z - (side_z * quarter), config)};
            const float lip{(one_flank > other_flank) ? one_flank : other_flank};

            TEST_CHECK(std::abs(p0.position[1] - (lip + tube_config.surface_offset)) < EPSILON);
        }
    }
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
