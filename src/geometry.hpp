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

    The Grid is right-handed and Y-up, distances are metres, and triangle winding is
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
        Appends another mesh, shifting its indices so they address the merged vertex array.

        This is how the several sub-meshes of the Grid are concatenated into the single flat
        triangle buffer that the `Geometry` component slices with a first-triangle/count pair.

        \param other Mesh to append. Left unchanged.
    */
    void append(const Mesh& other);
};

/*!
    Configuration for the grid floor — the Grid's default surface.

    The floor is a lattice of flat cells, each standing at a level the relief chooses, joined by
    genuinely vertical risers wherever neighbouring levels differ. The relief decides where the
    terraces are; the drawn floor holds every facet horizontal or vertical, with nothing tilted in
    between. The risers cost triangles — two per stepped cell boundary, on top of the two every
    cell always had — and they are the whole point.

    It earns its place twice over. Optically, the stepped mirror breaks the neon into terrace-edge
    lines with dark cliffs beneath them, which reads as something built rather than grown.
    Acoustically it matters far more: a flat plane sends every reflection away at the mirror angle
    and none of it ever returns, while a vertical riser throws a call straight back at whoever
    made it — the walls are what make echoes, and echolocation, possible on the Grid at all.
*/
struct GridFloorConfig {
    uint32_t cells{64u}; //!< Number of quads along each axis, so `cells` × `cells` quads in total.
    float cell_size{1.0f}; //!< Edge length of one quad, in metres.
    float height{0.0f}; //!< World-space Y of the lowest ground, in metres. The relief rises from it.
    float relief_amplitude{5.0f}; //!< How far the highest ground stands above `height`, in metres. Zero gives a flat plane.
    float relief_wavelength{46.0f}; //!< Approximate distance between one landform and the next, in metres.

    /*!
        Layers of noise. Each halves the amplitude and doubles the frequency of the one before.

        Three, not more: the fourth octave's features would be a quarter of `relief_wavelength`
        across, which at the default cell size is barely wider than a single quad. Detail finer
        than the mesh can carry does not appear — it aliases, and on a mirror that reads as noise.
    */
    uint32_t relief_octaves{3u};

    /*!
        Discrete height levels the relief snaps to. Zero leaves it smooth.

        Six over five metres gives steps a little under a metre — tall enough to read as terraces
        from the User's camera rather than as ripples, and tall enough to be worth reflecting
        sound off.
    */
    uint32_t relief_terraces{6u};
    uint32_t relief_seed{42u}; //!< Chooses which landscape. Fixed rather than random, so a recording is reproducible.
};

/*!
    Configuration for the neon tubes laid along grid edges.

    The tubes are thin quads rather than actual cylinders: at the widths involved a cylinder would
    cost dozens of triangles per edge and look identical, and thin quads keep the emissive triangle
    count low, which matters because every emissive triangle is a light source the ray tree may hit.
*/
struct NeonTubeConfig {
    float half_width{0.01f}; //!< Half the tube width, in metres. The default gives a 2 cm wide strip.
    /*!
        Vertical lift above the surface, in metres, so a tube does not z-fight with the floor.

        The drawn floor is flat cells joined by vertical risers, and every tube lies flat on a
        lip, so the lift only has to clear coplanarity with the terrace top it rests on — there is
        no gradient anywhere under a tube for its outer edge to sink below.
    */
    float surface_offset{0.005f};
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
    Returns the height of the floor surface at a world-space position.

    Deterministic, stateless and continuous: the same coordinates always give the same height, on
    every machine and every run, which is what a reproducible recording requires. Anything that
    must sit on the floor calls this rather than assuming zero — the floor mesh, the neon tubes
    laid along its grid lines, and every object planted on it.

    Sampling by world position rather than by grid index is what lets an object standing at an
    arbitrary place find its own ground; the floor and the tubes agree exactly because they pass
    the same coordinates, computed the same way, from the same integer grid.

    \param world_x World-space X, in metres.
    \param world_z World-space Z, in metres.
    \param config Floor dimensions and relief.
    \return World-space Y of the surface at that point, in metres.
*/
[[nodiscard]] float gridSurfaceHeight(float world_x, float world_z, const GridFloorConfig& config);

/*!
    Returns every height the terraced relief can quantise to, ascending.

    The flat planes of the floor, as data: `gridSurfaceHeight` snaps its relief to
    `floor(relief * terraces) / terraces`, so the surface is piecewise flat at exactly these
    heights, and anything that enumerates horizontal reflectors — the acoustic image-source
    delivery is the consumer this exists for — must agree with that arithmetic rather than
    restate it. The list is the *possible* levels: the top one requires the noise to reach
    exactly one and the shipped landscape never does, which costs its consumer one candidate
    that never validates rather than an echo that never arrives.

    A smooth relief (`relief_terraces` of zero) has no flat level anywhere and returns an empty
    list. A flat floor (zero amplitude, zero wavelength or zero octaves) is one level at
    `config.height`.

    \param config Floor dimensions and relief.
    \return World-space Y of each possible terrace level, ascending.
*/
[[nodiscard]] std::vector<float> gridTerraceLevels(const GridFloorConfig& config);

/*!
    Returns the height of the floor **as it is actually drawn**, at a world-space position.

    This is not the same thing as `gridSurfaceHeight`, and the difference is the whole reason this
    function exists. `gridSurfaceHeight` is the analytic relief, whose terrace boundaries curve
    wherever the noise puts them; the drawn floor stands every cell flat at the level of its own
    centre and joins neighbouring cells with genuinely vertical risers, so the drawn boundary runs
    along the cell lattice. Anything that must sit *on* the floor has to ask where the floor is,
    not where the function that generated it would have been.

    Piecewise constant, therefore: the level of the cell under the point, with a step exactly at
    every cell boundary whose neighbours disagree. Positions beyond the floor's extent are clamped
    to its edge.

    \param world_x World-space X, in metres.
    \param world_z World-space Z, in metres.
    \param config Floor dimensions and relief.
    \return World-space Y of the drawn surface at that point, in metres.
*/
[[nodiscard]] float gridMeshHeight(float world_x, float world_z, const GridFloorConfig& config);

/*!
    One vertical riser of the drawn floor: a rectangle standing on a cell boundary.

    `edge_u` runs along the boundary, `edge_v` straight up, and the winding is the statement of
    which side reflects: `cross(edge_u, edge_v)` normalised points **towards the lower cell** —
    into the air a creature stands in, because the other side is inside the hill.
*/
struct GridWall {
    MathLib::Vec3 origin{}; //!< A bottom corner of the wall, in world space.
    MathLib::Vec3 edge_u{}; //!< Along the cell boundary, full length.
    MathLib::Vec3 edge_v{}; //!< Straight up, the full level difference.
};

/*!
    Returns every vertical riser the drawn floor has, in generation order.

    **This is the single source the walls have.** `generateGridFloor` emits its riser triangles
    from exactly this list, and the acoustic image-source delivery enumerates its riser mirrors
    from exactly this list, so the wall an echo comes back off is the wall the picture shows — by
    construction rather than by two generators kept in step.

    These walls are what make monostatic echolocation real on the Grid: a call emitted towards a
    riser strikes a genuinely vertical face and comes straight back, where the tilted facets of a
    corner-sampled heightfield deflected it skyward.

    \param config Floor dimensions and relief.
    \return One wall per cell boundary whose neighbouring levels differ. Empty for a flat floor.
*/
[[nodiscard]] std::vector<GridWall> gridRiserWalls(const GridFloorConfig& config);

/*!
    Generates the grid floor: a subdivided plane displaced by `gridSurfaceHeight`.

    This is the Grid's default surface and the first thing the BVH indexes. Face normals are
    derived from the displaced triangles rather than assumed upward, so the relief shades and
    reflects correctly. Vertices carry a `uv` measured in grid cells from the centre of the floor,
    so a shader can derive grid-line coordinates from `uv` alone without knowing the world extent.

    \param config Floor dimensions and relief.
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
