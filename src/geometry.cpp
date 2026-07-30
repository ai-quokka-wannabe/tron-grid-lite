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

#include "geometry.hpp"
#include <array>
#include <cmath>

namespace
{
    //! Squared length below which a cross product is treated as degenerate.
    constexpr float DEGENERATE_CROSS_EPSILON{0.000001f};

    //! Builds a vertex from a position, a face normal and a pair of surface coordinates.
    [[nodiscard]] Vertex makeVertex(const MathLib::Vec3& position, const MathLib::Vec3& normal, float u, float v)
    {
        return Vertex{{position.x, position.y, position.z}, {normal.x, normal.y, normal.z}, {u, v}};
    }

    //! Appends one triangle to a mesh, giving all three vertices the same face normal.
    void emitTriangle(Mesh& mesh, const MathLib::Vec3& a, const MathLib::Vec3& b, const MathLib::Vec3& c, const MathLib::Vec3& normal, const std::array<float, 2>& uv_a,
        const std::array<float, 2>& uv_b, const std::array<float, 2>& uv_c)
    {
        const uint32_t base{static_cast<uint32_t>(mesh.vertices.size())};

        mesh.vertices.push_back(makeVertex(a, normal, uv_a[0], uv_a[1]));
        mesh.vertices.push_back(makeVertex(b, normal, uv_b[0], uv_b[1]));
        mesh.vertices.push_back(makeVertex(c, normal, uv_c[0], uv_c[1]));

        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1u);
        mesh.indices.push_back(base + 2u);
    }

    /*!
        Appends a thin quad running along the edge from a to b, lifted above the surface.

        The quad is widened perpendicular to the edge and to the world up vector, which for the
        near-horizontal grid edges involved keeps it lying flat on the surface. A fallback
        reference direction handles the degenerate case of an edge parallel to up.
    */
    void emitEdgeQuad(Mesh& mesh, const MathLib::Vec3& a, const MathLib::Vec3& b, const NeonTubeConfig& config)
    {
        // The edge direction is normalised before the cross product so that the degeneracy test
        // below measures the angle between edge and up rather than the length of the edge: an
        // unnormalised test would wrongly call every edge degenerate for a small enough cell size.
        const MathLib::Vec3 edge{(b - a).normalised()};

        MathLib::Vec3 up{0.0f, 1.0f, 0.0f};
        MathLib::Vec3 side{edge.cross(up)};

        if (side.lengthSquared() < DEGENERATE_CROSS_EPSILON) {
            up = MathLib::Vec3{1.0f, 0.0f, 0.0f};
            side = edge.cross(up);
        }

        side = side.normalised();

        const MathLib::Vec3 lift{0.0f, config.surface_offset, 0.0f};
        const MathLib::Vec3 half_width{side * config.half_width};

        // Quad corners: p0 and p1 straddle endpoint a, p2 and p3 straddle endpoint b.
        const MathLib::Vec3 p0{(a + lift) - half_width};
        const MathLib::Vec3 p1{(a + lift) + half_width};
        const MathLib::Vec3 p2{(b + lift) + half_width};
        const MathLib::Vec3 p3{(b + lift) - half_width};

        const MathLib::Vec3 normal{(p1 - p0).cross(p2 - p0).normalised()};

        // uv.x runs along the tube, uv.y across it.
        emitTriangle(mesh, p0, p1, p2, normal, {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f});
        emitTriangle(mesh, p0, p2, p3, normal, {0.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f});
    }

    //! Returns true when a grid line index falls on a major grid line.
    [[nodiscard]] bool isMajorGridLine(uint32_t line_index, uint32_t major_interval)
    {
        if (major_interval == 0u) {
            return false;
        }

        return (line_index % major_interval) == 0u;
    }

    /*!
        Emits the two triangles of one surface quad, per-face shaded.

        The corners are named by their (x, z) grid position: p00 is the low corner in both axes,
        p11 the high one. Winding is chosen so both face normals point upwards in a right-handed
        Y-up world. The uv of each vertex is its grid position measured in cells from the centre.

        Each triangle is emitted in the order (v0, v1, v2) for which the right-handed geometric
        normal (v1 - v0) x (v2 - v0) is the stored face normal, so the stored normal and the
        winding always agree and a front-face-anticlockwise pipeline never culls the surface.
    */
    void emitSurfaceQuad(Mesh& mesh, const MathLib::Vec3& p00, const MathLib::Vec3& p10, const MathLib::Vec3& p01, const MathLib::Vec3& p11, float u0, float u1, float v0,
        float v1)
    {
        const MathLib::Vec3 normal_a{(p01 - p00).cross(p10 - p00).normalised()};
        emitTriangle(mesh, p00, p01, p10, normal_a, {u0, v0}, {u0, v1}, {u1, v0});

        const MathLib::Vec3 normal_b{(p01 - p10).cross(p11 - p10).normalised()};
        emitTriangle(mesh, p10, p01, p11, normal_b, {u1, v0}, {u0, v1}, {u1, v1});
    }

}

float Mesh::boundingRadius() const
{
    float max_squared{0.0f};

    for (const Vertex& vertex : vertices) {
        const float squared{(vertex.position[0] * vertex.position[0]) + (vertex.position[1] * vertex.position[1]) + (vertex.position[2] * vertex.position[2])};

        if (squared > max_squared) {
            max_squared = squared;
        }
    }

    return std::sqrt(max_squared);
}

void Mesh::append(const Mesh& other)
{
    const uint32_t offset{static_cast<uint32_t>(vertices.size())};

    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());

    indices.reserve(indices.size() + other.indices.size());
    for (uint32_t index : other.indices) {
        indices.push_back(index + offset);
    }
}

Mesh generateGridFloor(const GridFloorConfig& config)
{
    Mesh floor{};

    const float half_size{(static_cast<float>(config.cells) * config.cell_size) * 0.5f};
    const float half_cells{static_cast<float>(config.cells) * 0.5f};

    const std::size_t triangle_count{static_cast<std::size_t>(config.cells) * config.cells * 2u};
    floor.vertices.reserve(triangle_count * 3u);
    floor.indices.reserve(triangle_count * 3u);

    for (uint32_t z{0u}; z < config.cells; ++z) {
        for (uint32_t x{0u}; x < config.cells; ++x) {
            const float x0{(static_cast<float>(x) * config.cell_size) - half_size};
            const float x1{(static_cast<float>(x + 1u) * config.cell_size) - half_size};
            const float z0{(static_cast<float>(z) * config.cell_size) - half_size};
            const float z1{(static_cast<float>(z + 1u) * config.cell_size) - half_size};

            const MathLib::Vec3 p00{x0, config.height, z0};
            const MathLib::Vec3 p10{x1, config.height, z0};
            const MathLib::Vec3 p01{x0, config.height, z1};
            const MathLib::Vec3 p11{x1, config.height, z1};

            // Surface coordinates in grid cells from the centre of the floor.
            const float u0{static_cast<float>(x) - half_cells};
            const float u1{static_cast<float>(x + 1u) - half_cells};
            const float v0{static_cast<float>(z) - half_cells};
            const float v1{static_cast<float>(z + 1u) - half_cells};

            emitSurfaceQuad(floor, p00, p10, p01, p11, u0, u1, v0, v1);
        }
    }

    return floor;
}

NeonGrid generateGridFloorNeon(const GridFloorConfig& floor_config, const NeonTubeConfig& tube_config)
{
    NeonGrid neon{};

    const uint32_t verts_per_side{floor_config.cells + 1u};
    const float half_size{(static_cast<float>(floor_config.cells) * floor_config.cell_size) * 0.5f};

    const auto gridVertex = [&](uint32_t gx, uint32_t gz) -> MathLib::Vec3 {
        const float world_x{(static_cast<float>(gx) * floor_config.cell_size) - half_size};
        const float world_z{(static_cast<float>(gz) * floor_config.cell_size) - half_size};
        return MathLib::Vec3{world_x, floor_config.height, world_z};
    };

    // Lines running along X, one per Z row.
    for (uint32_t z{0u}; z < verts_per_side; ++z) {
        const bool major_row{isMajorGridLine(z, tube_config.major_interval)};

        for (uint32_t x{0u}; x < floor_config.cells; ++x) {
            // An edge is an accent edge when either of its axes lies on a major line, so that the
            // crossings of a major row and a major column stay a single unbroken accent lattice.
            const bool major_edge{major_row || isMajorGridLine(x, tube_config.major_interval)};
            Mesh& target{major_edge ? neon.accent : neon.primary};
            emitEdgeQuad(target, gridVertex(x, z), gridVertex(x + 1u, z), tube_config);
        }
    }

    // Lines running along Z, one per X column.
    for (uint32_t x{0u}; x < verts_per_side; ++x) {
        const bool major_column{isMajorGridLine(x, tube_config.major_interval)};

        for (uint32_t z{0u}; z < floor_config.cells; ++z) {
            const bool major_edge{major_column || isMajorGridLine(z, tube_config.major_interval)};
            Mesh& target{major_edge ? neon.accent : neon.primary};
            emitEdgeQuad(target, gridVertex(x, z), gridVertex(x, z + 1u), tube_config);
        }
    }

    return neon;
}

Mesh generateBox(const MathLib::Vec3& centre, const MathLib::Vec3& half_extents)
{
    Mesh box{};

    // The eight corners, named by the sign taken along each of x, y and z.
    const MathLib::Vec3 c000{centre.x - half_extents.x, centre.y - half_extents.y, centre.z - half_extents.z};
    const MathLib::Vec3 c100{centre.x + half_extents.x, centre.y - half_extents.y, centre.z - half_extents.z};
    const MathLib::Vec3 c010{centre.x - half_extents.x, centre.y + half_extents.y, centre.z - half_extents.z};
    const MathLib::Vec3 c110{centre.x + half_extents.x, centre.y + half_extents.y, centre.z - half_extents.z};
    const MathLib::Vec3 c001{centre.x - half_extents.x, centre.y - half_extents.y, centre.z + half_extents.z};
    const MathLib::Vec3 c101{centre.x + half_extents.x, centre.y - half_extents.y, centre.z + half_extents.z};
    const MathLib::Vec3 c011{centre.x - half_extents.x, centre.y + half_extents.y, centre.z + half_extents.z};
    const MathLib::Vec3 c111{centre.x + half_extents.x, centre.y + half_extents.y, centre.z + half_extents.z};

    struct FaceQuad {
        MathLib::Vec3 a;
        MathLib::Vec3 b;
        MathLib::Vec3 c;
        MathLib::Vec3 d;
        MathLib::Vec3 normal;
    };

    // Six faces, each wound anticlockwise as seen from outside the box.
    const std::array<FaceQuad, 6u> faces{{
        {c001, c101, c111, c011, {0.0f, 0.0f, 1.0f}}, // +Z face.
        {c100, c000, c010, c110, {0.0f, 0.0f, -1.0f}}, // -Z face.
        {c101, c100, c110, c111, {1.0f, 0.0f, 0.0f}}, // +X face.
        {c000, c001, c011, c010, {-1.0f, 0.0f, 0.0f}}, // -X face.
        {c011, c111, c110, c010, {0.0f, 1.0f, 0.0f}}, // +Y face.
        {c000, c100, c101, c001, {0.0f, -1.0f, 0.0f}}, // -Y face.
    }};

    box.vertices.reserve(36u);
    box.indices.reserve(36u);

    for (const FaceQuad& face : faces) {
        // Each face is one unit square in uv, split along the a-c diagonal.
        emitTriangle(box, face.a, face.b, face.c, face.normal, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f});
        emitTriangle(box, face.a, face.c, face.d, face.normal, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f});
    }

    return box;
}
