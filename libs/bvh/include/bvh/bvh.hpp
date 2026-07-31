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
#include <cstdint>
#include <vector>

/*!
    A bounding volume hierarchy, built on the host and handed to the GPU as two storage buffers.

    TronGrid Lite has no hardware ray tracing available to it, so it builds its own acceleration
    structure and walks it by hand in a compute shader. Both structures below are laid out so that
    a `std::memcpy` of the vector's data into a mapped buffer produces exactly what the shader's
    std430 declaration expects — the offsets are asserted in the implementation file rather than
    left to trust.

    The same hierarchy is intended to serve acoustic rays later, which is why nothing here is
    specific to light.
*/
namespace BvhLib
{

    /*!
        One triangle, in the form the intersection test actually wants.

        Storing the two edges rather than the second and third vertices saves the shader two
        subtractions per test, and the material index rides along in padding that std430 would
        have inserted anyway.

        | Offset | Size | Member      | std430 equivalent |
        |-------:|-----:|-------------|-------------------|
        |      0 |   12 | `v0`        | `float3`          |
        |     12 |    4 | `material`  | `uint`            |
        |     16 |   12 | `edge1`     | `float3`          |
        |     28 |    4 | `padding0`  | `uint`            |
        |     32 |   12 | `edge2`     | `float3`          |
        |     44 |    4 | `padding1`  | `uint`            |
    */
    struct Triangle {
        MathLib::Vec3 v0{}; //!< First vertex, in world space.
        uint32_t material{0u}; //!< Index into the scene's material table.
        MathLib::Vec3 edge1{}; //!< Second vertex minus the first.
        uint32_t padding0{0u}; //!< Unused; present so the layout matches std430 exactly.
        MathLib::Vec3 edge2{}; //!< Third vertex minus the first.
        uint32_t padding1{0u}; //!< Unused; present so the layout matches std430 exactly.

        //! Returns the second vertex.
        [[nodiscard]] constexpr MathLib::Vec3 v1() const
        {
            return v0 + edge1;
        }

        //! Returns the third vertex.
        [[nodiscard]] constexpr MathLib::Vec3 v2() const
        {
            return v0 + edge2;
        }

        //! Returns the unnormalised geometric normal, anticlockwise about the winding order.
        [[nodiscard]] constexpr MathLib::Vec3 geometricNormal() const
        {
            return edge1.cross(edge2);
        }

        //! Returns the centroid, used to bin the triangle during the build.
        [[nodiscard]] constexpr MathLib::Vec3 centroid() const
        {
            return v0 + (edge1 + edge2) * (1.0f / 3.0f);
        }
    };

    /*!
        One node of the hierarchy, 32 bytes.

        A node is a leaf when `triangle_count` is non-zero, in which case `left_or_first` is the
        index of its first triangle in `Bvh::triangles`. Otherwise it is an interior node and
        `left_or_first` is the index of its left child; the right child always follows immediately
        after, so only one index need be stored.

        | Offset | Size | Member           | std430 equivalent |
        |-------:|-----:|------------------|-------------------|
        |      0 |   12 | `bounds_min`     | `float3`          |
        |     12 |    4 | `left_or_first`  | `uint`            |
        |     16 |   12 | `bounds_max`     | `float3`          |
        |     28 |    4 | `triangle_count` | `uint`            |
    */
    struct Node {
        MathLib::Vec3 bounds_min{}; //!< Lower corner of the axis-aligned bounding box.
        uint32_t left_or_first{0u}; //!< Left child index for an interior node, first triangle index for a leaf.
        MathLib::Vec3 bounds_max{}; //!< Upper corner of the axis-aligned bounding box.
        uint32_t triangle_count{0u}; //!< Triangle count for a leaf; zero marks an interior node.

        //! Returns true when this node holds triangles rather than children.
        [[nodiscard]] constexpr bool isLeaf() const
        {
            return triangle_count != 0u;
        }
    };

    /*!
        Deepest the builder will ever nest, and therefore the smallest traversal stack that is
        guaranteed to suffice.

        The shader allocates a fixed-size stack in registers, so this is not a soft preference: a
        deeper tree would silently overflow it. The builder forces a leaf on reaching this depth,
        which costs a little traversal time on pathological input and cannot corrupt anything. The
        shader's stack must be declared with exactly this size.
    */
    inline constexpr uint32_t MAX_DEPTH{30u};

    //! Largest number of triangles the builder will leave in a leaf when a split is still worthwhile.
    inline constexpr uint32_t MAX_LEAF_TRIANGLES{4u};

    //! A built hierarchy, ready to be uploaded.
    struct Bvh {
        std::vector<Node> nodes; //!< Node array; element 0 is the root when the hierarchy is non-empty.
        std::vector<Triangle> triangles; //!< Triangles reordered so that each leaf owns a contiguous range.

        //! Returns true when the hierarchy holds no geometry at all.
        [[nodiscard]] bool empty() const
        {
            return nodes.empty();
        }

        //! Returns the depth of the deepest leaf, which must never exceed MAX_DEPTH.
        [[nodiscard]] uint32_t depth() const;
    };

    /*!
        Builds a hierarchy over the given triangles using a binned surface-area-heuristic split.

        The input is taken by value and reordered rather than indexed indirectly, so the shader
        needs one buffer fewer and a leaf's triangles share cache lines.

        \param triangles Triangles to index. May be empty, in which case the result is empty.
        \return The built hierarchy.
    */
    [[nodiscard]] Bvh build(std::vector<Triangle> triangles);

    //! What a ray struck, if anything.
    struct Hit {
        float distance{0.0f}; //!< Distance along the ray direction, in the direction's own units.
        uint32_t triangle{0u}; //!< Index into Bvh::triangles.
        float barycentric_u{0.0f}; //!< Barycentric coordinate along edge1.
        float barycentric_v{0.0f}; //!< Barycentric coordinate along edge2.
        bool valid{false}; //!< False when the ray struck nothing.
    };

    /*!
        Traces one ray against the hierarchy and returns the nearest hit.

        This is the host-side twin of the traversal the compute shader performs, and exists so that
        the shader's logic can be checked against a brute-force sweep in the test suite. It is not
        intended to be fast.

        \param bvh Hierarchy to traverse.
        \param origin Ray origin in world space.
        \param direction Ray direction; need not be normalised, and the returned distance is in its units.
        \param max_distance Furthest hit to accept.
    */
    [[nodiscard]] Hit intersect(const Bvh& bvh, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance);

    //! Tests every triangle in turn. The reference the hierarchy is checked against.
    [[nodiscard]] Hit intersectBruteForce(const std::vector<Triangle>& triangles, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance);

} // namespace BvhLib
