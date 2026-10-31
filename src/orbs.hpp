/*
    Copyright (C) 2026 The AiQuokkaWannabe Authors https://github.com/ai-quokka-wannabe/tron-grid-lite

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

#include <bvh/bvh.hpp>
#include <lnk/lnk_protocol.h>
#include <math/matrix.hpp>

#include <cstdint>
#include <vector>

/*!
    Food as the eyes meet it: glowing white orbs (docs/CHEMORECEPTION.md § Food).

    An orb is a sphere the world places and the mouths deplete; on the Grid it is geometry like
    any other, an emissive surface in the scene the senses trace - nothing special-cased, as the
    neon tubes are not. Its size is the row's radius, a uniform scale on one unit icosphere; its
    glow is the row's quantity, so a half-eaten orb is a dimmer one.

    The glow is quantised into a few levels rather than written per orb, and that is the material
    model's shape rather than a shortcut: a triangle's material slot is the only key the tracers
    have, the material table is uploaded once, and an instance carries a transform and nothing
    else. So each level is its own small geometry wearing its own emissive slot, built once, and
    a telling only chooses which level an orb stands in - a placement, never a re-upload.

    Shared by the spectator's stage and the creature host's, so the picture a User watches and
    the scene a creature's eyes trace hold the same orbs.
*/
namespace OrbLib
{

    //! How many steps the glow takes from a crumb to a whole orb. Finer than an eye tells one
    //! bite from the next at a glance, and it costs eight small geometries.
    constexpr std::uint32_t GLOW_LEVELS{8u};

    //! A whole orb's linear radiance, equal in every channel: white, and so blue enough for the
    //! worm's photoreceptors. Its brightest channel level with the floor's neon's, below the pillars'.
    constexpr float FULL_EMISSION{4.0f};

    //! The level an orb with this much food left glows at: 1 for a crumb, GLOW_LEVELS for a
    //! whole orb, never 0 - an orb still in the telling is still there to be seen.
    [[nodiscard]] std::uint32_t glowLevel(float quantity) noexcept;

    //! The emissive white material of one glow level.
    [[nodiscard]] Material material(std::uint32_t level) noexcept;

    //! A unit icosphere, centred on the origin, every triangle wearing `material_slot`.
    [[nodiscard]] std::vector<BvhLib::Triangle> unitOrb(std::uint32_t material_slot);

    //! Where an orb stands: scaled to its radius, carried to its centre. Uniform, so the normals
    //! need no correction.
    [[nodiscard]] MathLib::Mat4 transform(const LnkFoodState& food) noexcept;

    /*!
        Appends the orbs' shapes to a scene's geometries and their materials to its table: one
        geometry per glow level, level 1 first. Returns the index of level 1's geometry; level
        `n` stands in `first + n - 1`.
    */
    [[nodiscard]] std::uint32_t appendShapes(std::vector<BvhLib::Bvh>& geometries, std::vector<Material>& materials);

} // namespace OrbLib
