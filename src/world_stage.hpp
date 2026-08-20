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

#pragma once

#include "components.hpp"
#include "world_client.hpp"

#include <bvh/bvh.hpp>

#include <cstdint>
#include <vector>

/*!
    The live view's scene: the Grid, plus one placeholder body per creature the world tells of.

    A spectator knows a creature only as a pose on the wire — its real shape arrives with REZ in a
    later etape — so every creature stands in the same placeholder body: a low neon dart the size
    the roster's constants say a body is, nose towards -Z because that is the way `forwardFor`
    faces. One geometry shared by every instance, which is exactly what the two-level hierarchy is
    for: what changes per telling is placement alone, and nothing is ever rebuilt.

    No Vulkan here, so the whole thing is testable under ctest. The device half is the tracer's
    dynamic instance path, which consumes what `records` returns.
*/
namespace WorldStageLib
{

    class WorldStage {
    public:
        /*!
            \param grid The Grid's own hierarchy. Taken by value and kept, as `Stage` keeps its.
            \param grid_materials The Grid's material table. The placeholder body's material is
                   appended, so the table this stage serves is one longer than what came in.
            \param creature_capacity Most creatures a telling may place. The protocol's own cap is
                   the honest value; `records` refuses more rather than truncating silently.
        */
        WorldStage(BvhLib::Bvh grid, std::vector<Material> grid_materials, std::uint32_t creature_capacity);

        WorldStage(const WorldStage&) = delete;
        WorldStage& operator=(const WorldStage&) = delete;
        WorldStage(WorldStage&&) = delete;
        WorldStage& operator=(WorldStage&&) = delete;

        //! The combined material table: the Grid's slots, then the placeholder body's one.
        [[nodiscard]] const std::vector<Material>& materials() const noexcept
        {
            return m_materials;
        }

        //! Placements the instance buffer must hold: the Grid, plus a body per possible creature.
        [[nodiscard]] std::uint32_t instanceCapacity() const noexcept
        {
            return 1u + m_creature_capacity;
        }

        /*!
            The whole scene in upload form: both geometries concatenated, the Grid's identity
            placement as the only instance. What a `World` is built from, once; every later frame
            owes the device only what `records` returns.
        */
        [[nodiscard]] BvhLib::FlatScene flatScene() const;

        /*!
            The Grid's placement followed by one placeholder body per creature, poses applied.

            The per-frame shape: 144-byte records ready for the tracer's dynamic instance path, in
            the order given so a creature keeps its row while it lives. More creatures than the
            capacity is refused loudly — the wire's contract already caps a telling, so arriving
            here over it is a defect and not a bigger world.
        */
        [[nodiscard]] std::vector<BvhLib::InstanceRecord> records(const std::vector<WorldClientLib::InterpolatedCreature>& creatures) const;

    private:
        std::uint32_t m_creature_capacity;
        BvhLib::Scene m_scene; //!< Geometry 0 the Grid, geometry 1 the placeholder body.
        std::vector<Material> m_materials;

        //! Where each geometry landed in the concatenated buffers, cached as `Stage` caches its.
        std::vector<std::uint32_t> m_node_offsets;
        std::vector<std::uint32_t> m_triangle_offsets;
    };

}
