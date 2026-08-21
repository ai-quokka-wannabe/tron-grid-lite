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

#include "acoustics.hpp"
#include "components.hpp"
#include "geometry.hpp"

#include <bvh/bvh.hpp>

#include <vector>

/*!
    The Grid the whole world agrees on: its floor, its geometry, its materials, its mirrors.

    One assembly for every consumer, which is the entire point of this header. The renderer
    uploads what `buildGridTriangles` returns, the headless Program run gathers hearing from it,
    and Master Control will one day step physics against the very same floor — so a creature
    necessarily hears the Grid the User would see, rather than a second copy maintained in step.
    That is why this lives outside `main.cpp`: a truth with more than one consumer cannot live in
    the anonymous namespace of one of them (`docs/TOPOLOGY.md` § The four repositories).

    Everything here is deviceless and deterministic: plain containers in, plain containers out,
    the same values every call.
*/

//! The Grid: a mirror floor under a low terraced relief, with neon tubes along its grid lines.
inline constexpr GridFloorConfig GRID_FLOOR_CONFIG{.cells = 64u, .cell_size = 2.0f, .height = 0.0f};

/*!
    The Grid's material table.

    The floor carries no emission at all: every photon in the Grid starts inside a neon tube, and
    the floor is only ever as bright as what it reflects. Fresnel does the rest — barely anything
    head-on, everything at a grazing angle, which is what draws the long streaks towards the
    horizon.
*/
[[nodiscard]] std::vector<Material> makeMaterials();

/*!
    Every triangle of the Grid, materials assigned, ready for `BvhLib::build`.

    One assembly for every consumer: the renderer uploads what this returns, and the headless
    Program run gathers hearing from it — so a creature necessarily hears the very Grid the User
    would see, rather than a second copy maintained in step.
*/
[[nodiscard]] std::vector<BvhLib::Triangle> buildGridTriangles();

/*!
    The Grid's acoustic mirror planes: every terrace level the floor can quantise to, and the
    outward faces of everything standing on it.

    Derived from the very config the floor is generated from and the very placements the box
    meshes are built from, so the candidate list cannot drift from the geometry the validation
    rays then test it against. The list proposes; the Grid disposes. Ten boxes of five faces
    and seven candidate levels — the top one exists only where the noise reaches exactly one,
    which the shipped landscape never does, so it is a candidate that validation rejects
    everywhere rather than an echo that arrives from nowhere.
*/
[[nodiscard]] Acoustics::Reflectors makeGridReflectors();
