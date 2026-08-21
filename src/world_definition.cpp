/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify it under the terms of
    the GNU General Public License as published by the Free Software Foundation, either version
    3 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
    without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along with this program.
    If not, see https://www.gnu.org/licenses/.
*/

#include "world_definition.hpp"

#include <math/vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

    /*
        Thin tubes, sitting almost on the floor.

        A rasteriser would need them wide and lifted, because a strip narrower than a pixel breaks
        into dashes and a coplanar strip fights the depth buffer. The tracer has neither problem: it
        samples geometry analytically and has no depth buffer at all, so the tubes can be the slender
        lines the aesthetic actually wants.

        The lift clears the steepest terrace gradient across the tube's half width; at 0.01 m the
        outer edge of a strip dipped below the floor on riser cells.
    */
    constexpr NeonTubeConfig NEON_TUBE_CONFIG{.half_width = 0.025f, .surface_offset = 0.02f};

    //! Converts a mesh into hierarchy triangles, tagging each with the given material.
    void appendTriangles(std::vector<BvhLib::Triangle>& out, const Mesh& mesh, const uint32_t material)
    {
        for (size_t index{0u}; (index + 2u) < mesh.indices.size(); index += 3u) {
            const Vertex& a{mesh.vertices[mesh.indices[index]]};
            const Vertex& b{mesh.vertices[mesh.indices[index + 1u]]};
            const Vertex& c{mesh.vertices[mesh.indices[index + 2u]]};

            const MathLib::Vec3 v0{a.position[0], a.position[1], a.position[2]};
            const MathLib::Vec3 v1{b.position[0], b.position[1], b.position[2]};
            const MathLib::Vec3 v2{c.position[0], c.position[1], c.position[2]};

            out.push_back(BvhLib::Triangle{.v0 = v0, .material = material, .edge1 = v1 - v0, .padding0 = 0u, .edge2 = v2 - v0, .padding1 = 0u});
        }
    }

    /*!
        Plants a box on the floor: returns the centre that puts its base on the ground beneath it.

        Everything standing in the Grid goes through this. The floor is terraced rather than flat,
        so a fixed height would leave objects buried in a terrace or hovering over a hollow, and the
        error is worst exactly where the relief is most interesting.
    */
    [[nodiscard]] MathLib::Vec3 plantOnFloor(const float world_x, const float world_z, const MathLib::Vec3& half_extents, const GridFloorConfig& floor_config)
    {
        /*
            The ground under a box is not one height: a terrace step can run straight through its
            footprint, and the wider the box the likelier that is. Sitting the base on the LOWEST
            point of the footprint buries part of the box rather than leaving the rest of it
            hovering — something set into the ground reads as deliberate, whereas something floating
            above it reads as broken.

            The samples ask `gridMeshHeight` rather than `gridSurfaceHeight`, because the box has to
            stand on the floor as drawn rather than on the function it was drawn from. The drawn
            floor is piecewise constant per cell, so the lowest point over a rectangle is the lowest
            cell the rectangle overlaps — one sample anywhere inside each overlapped cell covers it,
            and the cell's own midline through the footprint is a point that is always inside both.
        */
        const float min_x{world_x - half_extents.x};
        const float max_x{world_x + half_extents.x};
        const float min_z{world_z - half_extents.z};
        const float max_z{world_z + half_extents.z};

        const float half_size{(static_cast<float>(floor_config.cells) * floor_config.cell_size) * 0.5f};
        const auto cellIndexOf = [&](float world_coordinate) {
            const float cells{static_cast<float>(floor_config.cells)};
            const float grid{std::clamp((world_coordinate + half_size) / floor_config.cell_size, 0.0f, cells)};
            return std::min(static_cast<uint32_t>(grid), floor_config.cells - 1u);
        };
        const auto cellMidline = [&](uint32_t cell_index) {
            return ((static_cast<float>(cell_index) + 0.5f) * floor_config.cell_size) - half_size;
        };

        float ground{gridMeshHeight(world_x, world_z, floor_config)};

        for (uint32_t cell_x{cellIndexOf(min_x)}; cell_x <= cellIndexOf(max_x); ++cell_x) {
            for (uint32_t cell_z{cellIndexOf(min_z)}; cell_z <= cellIndexOf(max_z); ++cell_z) {
                ground = std::min(ground, gridMeshHeight(cellMidline(cell_x), cellMidline(cell_z), floor_config));
            }
        }

        return MathLib::Vec3{world_x, ground + half_extents.y, world_z};
    }

    /*!
        One box standing on the Grid: the placement fact itself.

        Both of a box's public faces derive from this one struct — its mesh through `generateBox`,
        and its acoustic mirror faces through `Acoustics::outwardBoxFaces` — so the wall a
        creature's echo comes back off is the wall the User's window shows, by construction rather
        than by two lists kept in step.
    */
    struct PlacedBox {
        MathLib::Vec3 centre{};
        MathLib::Vec3 half_extents{};
    };

    /*!
        A few blocks standing off the floor.

        Their purpose is not decoration. A perfectly flat Grid has nothing to reflect: the tubes
        lie a centimetre above the mirror, so their reflection sits a centimetre below and merges
        with the tube itself. Geometry with height is what makes the second ray segment visible,
        and it is the only way to see at a glance whether the mirror is working.
    */
    [[nodiscard]] std::vector<PlacedBox> pillarPlacements(const GridFloorConfig& floor_config)
    {
        constexpr float FLOOR_HALF_EXTENT{64.0f};

        const std::array<MathLib::Vec3, 6u> positions{MathLib::Vec3{-24.0f, 0.0f, -18.0f}, MathLib::Vec3{18.0f, 0.0f, -30.0f}, MathLib::Vec3{34.0f, 0.0f, 6.0f},
            MathLib::Vec3{-38.0f, 0.0f, 14.0f}, MathLib::Vec3{6.0f, 0.0f, -52.0f}, MathLib::Vec3{-8.0f, 0.0f, 22.0f}};

        std::vector<PlacedBox> placements;
        for (size_t index{0u}; index < positions.size(); ++index) {
            const float height{6.0f + (static_cast<float>(index % 3u) * 4.0f)};
            const MathLib::Vec3 half_extents{0.45f, height * 0.5f, 0.45f};
            const MathLib::Vec3 centre{plantOnFloor(positions[index].x, positions[index].z, half_extents, floor_config)};

            if ((std::abs(centre.x) < FLOOR_HALF_EXTENT) && (std::abs(centre.z) < FLOOR_HALF_EXTENT)) {
                placements.push_back(PlacedBox{.centre = centre, .half_extents = half_extents});
            }
        }

        return placements;
    }

    /*!
        Glass standing in front of the emissive pillars.

        Placed deliberately between the camera's opening view and the lit geometry, because a
        refracting surface is only legible when there is something recognisable behind it to bend.
        Each slab is a solid box rather than a plane: a ray must cross two interfaces to pass
        through, which is what makes the refraction visible instead of merely a tint.
    */
    [[nodiscard]] std::vector<PlacedBox> glassSlabPlacements(const GridFloorConfig& floor_config)
    {
        // Broad upright panes, thin front to back, spread across the near view.
        const MathLib::Vec3 wide_slab{3.0f, 3.0f, 0.35f};
        const MathLib::Vec3 mid_slab{2.2f, 2.4f, 0.35f};
        const MathLib::Vec3 tall_slab{2.6f, 3.6f, 0.35f};

        return std::vector<PlacedBox>{
            PlacedBox{.centre = plantOnFloor(-9.0f, 8.0f, wide_slab, floor_config), .half_extents = wide_slab},
            PlacedBox{.centre = plantOnFloor(2.0f, 2.0f, mid_slab, floor_config), .half_extents = mid_slab},
            PlacedBox{.centre = plantOnFloor(13.0f, 10.0f, tall_slab, floor_config), .half_extents = tall_slab},
        };
    }

    //! A glowing translucent column: emission and transmission in the same surface.
    [[nodiscard]] std::vector<PlacedBox> glowingColumnPlacement(const GridFloorConfig& floor_config)
    {
        const MathLib::Vec3 column{0.7f, 5.0f, 0.7f};
        return std::vector<PlacedBox>{PlacedBox{.centre = plantOnFloor(-2.0f, -12.0f, column, floor_config), .half_extents = column}};
    }

    //! The meshes of a set of placed boxes, appended in placement order.
    [[nodiscard]] Mesh boxMesh(const std::vector<PlacedBox>& placements)
    {
        Mesh mesh{};
        for (const PlacedBox& box : placements) {
            mesh.append(generateBox(box.centre, box.half_extents));
        }
        return mesh;
    }

}

std::vector<Material> makeMaterials()
{
    std::vector<Material> materials(MATERIAL_SLOT_COUNT);
    materials[MATERIAL_FLOOR] = makeMirror(MathLib::Vec3{0.85f, 0.90f, 1.00f});

    /*
        A high index of refraction, because it is the only reflectivity knob this material
        model has. Fresnel derives the head-on reflectance entirely from it: ordinary glass at
        1.5 returns four per cent, which is honest for a window and far too dim for a floor
        that is meant to read as a mirror. At 2.4 it returns about seventeen per cent head-on
        and still climbs to everything at a grazing angle, which is the effect the aesthetic
        is after. Nothing else in the Grid uses this value while transmission stays at zero.
    */
    materials[MATERIAL_FLOOR].index_of_refraction = 2.4f;
    materials[MATERIAL_NEON_PRIMARY] = makeEmissive(MathLib::Vec3{0.05f, 0.35f, 0.55f}, MathLib::Vec3{0.10f, 2.60f, 4.20f});
    materials[MATERIAL_NEON_ACCENT] = makeEmissive(MathLib::Vec3{0.55f, 0.25f, 0.05f}, MathLib::Vec3{4.40f, 1.60f, 0.15f});
    materials[MATERIAL_PILLAR] = makeEmissive(MathLib::Vec3{0.30f, 0.45f, 0.60f}, MathLib::Vec3{0.60f, 3.20f, 5.00f});

    // Ordinary glass. The faint tint is what the transmitted ray picks up crossing it, so a
    // thicker slab does not darken more than a thin one — Beer-Lambert absorption would need
    // the path length through the medium, which is a later refinement.
    materials[MATERIAL_GLASS] = makeGlass(MathLib::Vec3{0.80f, 0.92f, 0.95f}, 1.52f);

    // Emission and transmission in the same surface, which is what a neon tube with a glass
    // envelope is — and the case a material model with fixed surface types cannot express.
    materials[MATERIAL_GLOWING_GLASS] = makeGlowingGlass(MathLib::Vec3{0.90f, 0.70f, 0.95f}, MathLib::Vec3{1.60f, 0.30f, 2.20f}, 1.46f, 0.85f);
    return materials;
}

std::vector<BvhLib::Triangle> buildGridTriangles()
{
    const Mesh floor{generateGridFloor(GRID_FLOOR_CONFIG)};
    const NeonGrid neon{generateGridFloorNeon(GRID_FLOOR_CONFIG, NEON_TUBE_CONFIG)};

    const Mesh pillars{boxMesh(pillarPlacements(GRID_FLOOR_CONFIG))};
    const Mesh glass{boxMesh(glassSlabPlacements(GRID_FLOOR_CONFIG))};
    const Mesh glowing_column{boxMesh(glowingColumnPlacement(GRID_FLOOR_CONFIG))};

    std::vector<BvhLib::Triangle> world_triangles;
    world_triangles.reserve(floor.triangleCount() + neon.primary.triangleCount() + neon.accent.triangleCount() + pillars.triangleCount() + glass.triangleCount()
        + glowing_column.triangleCount());
    appendTriangles(world_triangles, floor, MATERIAL_FLOOR);
    appendTriangles(world_triangles, neon.primary, MATERIAL_NEON_PRIMARY);
    appendTriangles(world_triangles, neon.accent, MATERIAL_NEON_ACCENT);
    appendTriangles(world_triangles, pillars, MATERIAL_PILLAR);
    appendTriangles(world_triangles, glass, MATERIAL_GLASS);
    appendTriangles(world_triangles, glowing_column, MATERIAL_GLOWING_GLASS);
    return world_triangles;
}

Acoustics::Reflectors makeGridReflectors()
{
    Acoustics::Reflectors reflectors{};
    reflectors.level_heights = gridTerraceLevels(GRID_FLOOR_CONFIG);

    /*
        The floor's own risers, from the very list the floor mesh emits its wall triangles
        from — see `gridRiserWalls`. These are what make monostatic echolocation real: a call
        emitted at a riser strikes a genuinely vertical mirror and comes straight back, so a
        creature can range the step in front of it by its own voice. The range cap prunes the
        distant ones before any validation ray is spent.
    */
    for (const GridWall& wall : gridRiserWalls(GRID_FLOOR_CONFIG)) {
        reflectors.faces.push_back(Acoustics::RectFace{.origin = wall.origin, .edge_u = wall.edge_u, .edge_v = wall.edge_v});
    }

    const auto add_faces = [&reflectors](const std::vector<PlacedBox>& placements) {
        for (const PlacedBox& box : placements) {
            for (const Acoustics::RectFace& face : Acoustics::outwardBoxFaces(box.centre, box.half_extents)) {
                reflectors.faces.push_back(face);
            }
        }
    };
    add_faces(pillarPlacements(GRID_FLOOR_CONFIG));
    add_faces(glassSlabPlacements(GRID_FLOOR_CONFIG));
    add_faces(glowingColumnPlacement(GRID_FLOOR_CONFIG));

    return reflectors;
}
