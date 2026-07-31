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

#pragma once

#include <math/vector.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

/*!
    Procedural geometry generators.

    Every generator here returns plain standard-library containers and this header deliberately
    pulls in no Vulkan header at all — geometry is produced on the CPU, and only later becomes the
    triangle soup that the hand-built bounding volume hierarchy indexes for the compute ray tracer.

    All generators emit per-face (flat-shaded) vertices: three distinct vertices per triangle
    sharing one face normal, with no shared-vertex indexing. That is what the angular look wants
    anyway, and it means a triangle list for the BVH builder is obtained by reading the vertex
    array three entries at a time with no indirection.

    The world is right-handed and Y-up, distances are metres, and triangle winding is
    anticlockwise when seen from the front face.
*/

/*!
    A single vertex of a generated mesh.

    | Offset | Size | Member     |
    |-------:|-----:|------------|
    |      0 |   12 | `position` |
    |     12 |   12 | `normal`   |
    |     24 |    8 | `uv`       |

    Thirty-two bytes with no padding anywhere. Plain `std::array<float, N>` members are used rather
    than `MathLib::Vec3` so that the struct stays trivially copyable, aggregate-initialisable, and
    obviously free of any hidden alignment.

    Only `position` reaches the GPU today: the tracer derives its shading normal from the triangle's
    own edges, and nothing samples a texture. The other two members are what the generators
    naturally produce while building faces, and they are kept because a mesh without them is not a
    mesh anybody else can reuse.
*/
struct Vertex {
    std::array<float, 3> position{}; //!< Object-space position, in metres.
    std::array<float, 3> normal{}; //!< Face normal, unit length. Flat shaded: identical for all three vertices of a triangle.
    std::array<float, 2> uv{}; //!< Parametric surface coordinates. See each generator for what it puts here.
};

static_assert(sizeof(Vertex) == 32u, "Vertex must be exactly 32 bytes with no padding.");
static_assert(alignof(Vertex) == 4u, "Vertex must be 4-byte aligned so the array is tightly packed.");
static_assert(offsetof(Vertex, position) == 0u, "Vertex::position must sit at offset 0.");
static_assert(offsetof(Vertex, normal) == 12u, "Vertex::normal must sit at offset 12.");
static_assert(offsetof(Vertex, uv) == 24u, "Vertex::uv must sit at offset 24.");
static_assert(std::is_trivially_copyable_v<Vertex>, "Vertex must be trivially copyable so a whole array can be memcpy'd into a mapped buffer.");

/*!
    A flat-shaded mesh: per-face vertices plus the triangle indices into them.

    Because the vertices are per-face, `indices` is always the trivial sequence 0, 1, 2, 3, … and
    `vertices.size()` is always a multiple of three. It is kept anyway because an index buffer is
    what both the rasteriser and the eventual BVH triangle table want, and because a later
    welding or deduplication pass would only have to change the generators, not their consumers.
*/
struct Mesh {
    std::vector<Vertex> vertices; //!< Per-face vertices — three per triangle, no shared-vertex indexing.
    std::vector<uint32_t> indices; //!< Triangle indices into `vertices`.

    //! Returns the number of triangles in the mesh.
    [[nodiscard]] std::size_t triangleCount() const
    {
        return indices.size() / 3u;
    }

    //! Returns true when the mesh holds no geometry.
    [[nodiscard]] bool empty() const
    {
        return indices.empty();
    }

    /*!
        Returns the radius of a bounding sphere centred on the object-space origin.

        This is a linear scan over the vertices, so it is cheap enough to call once after
        generation. It is the coarsest possible spatial bound and exists to seed the `Bounds`
        component and to give the BVH builder a first cut before it computes proper axis-aligned
        boxes per node.
    */
    [[nodiscard]] float boundingRadius() const;

    /*!
        Appends another mesh, shifting its indices so they address the merged vertex array.

        This is how the several sub-meshes of the world are concatenated into the single flat
        triangle buffer that the `Geometry` component slices with a first-triangle/count pair.

        \param other Mesh to append. Left unchanged.
    */
    void append(const Mesh& other);
};

/*!
    Configuration for the flat grid floor — the default world surface.

    The floor is a plain subdivided plane at a constant height with no noise at all. It is a
    perfect mirror in practice, so its flatness is the whole point: an unbroken horizontal mirror
    under an infinite black sky, with the neon grid lines the only thing to reflect.
*/
struct GridFloorConfig {
    uint32_t cells{64u}; //!< Number of quads along each axis, so `cells` × `cells` quads in total.
    float cell_size{1.0f}; //!< Edge length of one quad, in metres.
    float height{0.0f}; //!< Constant world-space Y of the whole floor, in metres.
};

/*!
    Configuration for the neon tubes laid along grid edges.

    The tubes are thin quads rather than actual cylinders: at the widths involved a cylinder would
    cost dozens of triangles per edge and look identical, and thin quads keep the emissive triangle
    count low, which matters because every emissive triangle is a light source the ray tree may hit.
*/
struct NeonTubeConfig {
    float half_width{0.01f}; //!< Half the tube width, in metres. The default gives a 2 cm wide strip.
    float surface_offset{0.005f}; //!< Vertical lift above the surface, in metres, so the tube never z-fights with the floor beneath it.
    uint32_t major_interval{8u}; //!< Every Nth grid line gets the accent colour. Zero puts every line in the primary sub-mesh.
};

/*!
    The neon grid, split by colour.

    The two sub-meshes exist purely so that each can be given its own emissive material; they are
    otherwise identical in kind. Which real colours the primary and accent materials carry is a
    scene decision and is deliberately not made here.
*/
struct NeonGrid {
    Mesh primary; //!< Tubes along ordinary grid lines.
    Mesh accent; //!< Tubes along major grid lines — every `NeonTubeConfig::major_interval` line.
};

/*!
    Generates the flat grid floor: a subdivided plane at a constant height, no noise.

    This is the default world surface and the first thing the BVH indexes. Vertices carry an
    upward face normal and a `uv` measured in grid cells from the centre of the floor, so a shader
    can derive grid-line coordinates from `uv` alone without knowing the floor's world extent.

    \param config Floor dimensions.
    \return Mesh with per-face vertices and trivial indices.
*/
[[nodiscard]] Mesh generateGridFloor(const GridFloorConfig& config);

/*!
    Generates neon tubes along every grid line of a flat grid floor.

    One thin quad is emitted per grid edge, lifted slightly above the floor. Every edge whose row
    or column index is a multiple of `NeonTubeConfig::major_interval` goes to the accent sub-mesh,
    the rest to the primary sub-mesh. Vertices carry `uv.x` running 0 to 1 along the tube and
    `uv.y` being 0 or 1 across its width, so a shader can fade a tube towards its ends if it wants.

    \param floor_config Floor the tubes are laid on — must match the one given to generateGridFloor.
    \param tube_config Tube dimensions and accent interval.
    \return NeonGrid with the primary and accent sub-meshes.
*/
[[nodiscard]] NeonGrid generateGridFloorNeon(const GridFloorConfig& floor_config, const NeonTubeConfig& tube_config = NeonTubeConfig{});

/*!
    Generates a flat-shaded axis-aligned box — six quads, twelve triangles, thirty-six vertices.

    Useful as scenery and as the simplest possible non-planar test case for the BVH: an axis-
    aligned box is the one shape whose correct traversal result can be verified by hand.

    \param centre World-space centre of the box, in metres.
    \param half_extents Half the size along each axis, so the box spans `centre - half_extents` to `centre + half_extents`.
    \return Mesh with per-face vertices and trivial indices.
*/
[[nodiscard]] Mesh generateBox(const MathLib::Vec3& centre, const MathLib::Vec3& half_extents);
