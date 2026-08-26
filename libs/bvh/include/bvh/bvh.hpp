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

#include <math/matrix.hpp>
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

    //! What a ray struck, if anything. Field for field what the shader's own Hit carries.
    struct Hit {
        float distance{0.0f}; //!< Distance along the ray direction, in the direction's own units.
        uint32_t triangle{0u}; //!< Index into Bvh::triangles.
        bool valid{false}; //!< False when the ray struck nothing.

        /*!
            Which instance was struck, for a scene traversal. Zero for a single-hierarchy trace.

            The triangle index is into *that instance's* geometry, and its vertices are in that
            geometry's own frame rather than the world's. A caller that needs a world-space normal
            must transform it — see `intersectScene`.
        */
        uint32_t instance{0u};
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

    /*!
        One placement of a geometry in the world.

        The Grid is an instance like any other, sitting at the identity transform, and a creature body
        is an instance of a geometry built once when the body is rezzed. **That is the entire point:
        a rigid body's hierarchy is never rebuilt, only its transform changes**, which is what turns a
        31 ms per-tick rebuild into a 0.003 ms one. See `docs/ARCHITECTURE.md` § One hierarchy today,
        two when creatures move.

        The inverse is stored rather than computed, because it is needed once per instance per ray and
        inverting a matrix in that loop would undo the saving this structure exists for.
    */
    struct Instance {
        MathLib::Mat4 to_world{MathLib::Mat4::identity()}; //!< Geometry frame to world frame.
        MathLib::Mat4 to_instance{MathLib::Mat4::identity()}; //!< World frame to geometry frame. The inverse of `to_world`, cached.
        uint32_t geometry{0u}; //!< Index into `Scene::geometries`.

        MathLib::Vec3 bounds_min{}; //!< World-space bounds of the whole instance, for the cheap rejection test.
        MathLib::Vec3 bounds_max{};
    };

    /*!
        A world made of instanced geometries: the two-level structure.

        Geometries are built once and shared; instances place them. Nothing here rebuilds when things
        move, which is the whole reason it exists.
    */
    struct Scene {
        std::vector<Bvh> geometries; //!< Bottom level. Built once each.
        std::vector<Instance> instances; //!< Top level. One box per placement.
    };

    /*!
        Builds an instance of a geometry at a transform, computing its inverse and world bounds.

        \param geometry The hierarchy being placed. Its root bounds are transformed to get the
               instance's world bounds, so it must already be built.
        \param geometry_index Index of that hierarchy within `Scene::geometries`.
        \param to_world Placement. Any affine transform; see `intersectScene` on what scale costs.
        \return An instance ready to be added to a scene.
    */
    [[nodiscard]] Instance makeInstance(const Bvh& geometry, uint32_t geometry_index, const MathLib::Mat4& to_world);

    /*!
        The instance index that names no instance, for the skip parameters below.

        A sentinel rather than an optional, because it crosses into configuration structs that want
        aggregate initialisation, and because the shader-side instance index is a plain uint with
        exactly this "none" spelling available.
    */
    inline constexpr uint32_t NO_INSTANCE{0xFFFFFFFFu};

    /*!
        Traces one ray against a whole scene and returns the nearest hit across every instance.

        The host-side twin of the two-level traversal the compute shader will perform, and the
        specification it is checked against — the same relationship `intersect` has with the
        single-level shader path.

        **The two skip parameters make named instances transparent to this ray.** Two rather than
        one or many, because two is what the Grid's senses actually need: a creature's own senses
        skip its own body, and a call's paths skip the caller's body and the listener's — a source
        inside its own hull would otherwise gag itself, and an ear inside its own hull would be
        deaf. `NO_INSTANCE` skips nothing, which is every caller that predates creatures having
        bodies.

        **The ray is transformed into instance space without normalising it**, and that is not an
        oversight. Leaving the transformed direction unnormalised means the ray parameter is identical
        in both frames, so a distance found in instance space is already the distance in world space
        and no rescaling is needed. It is also what makes a scaled instance work at all.

        What a scale *does* cost is the normal: a triangle's geometric normal is computed from its
        edges in the geometry's own frame, and under a non-uniform scale the world normal is not the
        transformed normal but the inverse-transpose of it. `Hit` deliberately does not return a
        normal, so the caller decides — and for a rigid placement, which is what creature bodies are
        expected to use, the linear part of `to_world` transforms the normal correctly as it stands.

        \param scene Instances and the geometries they place.
        \param origin Ray origin in world space.
        \param direction Ray direction in world space; need not be normalised, and the returned
               distance is in its units.
        \param max_distance Furthest hit to accept.
        \return The nearest hit, with `instance` naming which placement it belongs to.
    */
    [[nodiscard]] Hit intersectScene(const Scene& scene, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance,
        uint32_t skip_instance_a = NO_INSTANCE, uint32_t skip_instance_b = NO_INSTANCE);

    /*!
        A run of consecutive instances, which is what one creature stands in since bodies became
        chains: its head and every trailing segment, placed one after another. `NO_INSTANCE` with a
        count of zero names nothing, which is every caller that predates creatures having bodies.
    */
    struct InstanceRange {
        uint32_t first{NO_INSTANCE};
        uint32_t count{0u};

        [[nodiscard]] bool contains(const uint32_t index) const noexcept
        {
            return (first != NO_INSTANCE) && (index >= first) && (index - first < count);
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return (first == NO_INSTANCE) || (count == 0u);
        }
    };

    //! `intersectScene` with the two skips as runs rather than single instances: a chain's whole
    //! body is transparent to its own senses, exactly as one rigid body was.
    [[nodiscard]] Hit intersectScene(const Scene& scene, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance, InstanceRange skip_a,
        InstanceRange skip_b);

    /*!
        One instance in the form the shader reads: 144 bytes, std430, ready to upload.

        `Scene` is the shape the host wants — a vector of hierarchies and a vector of placements that
        index them. A shader cannot have a vector of vectors, so the geometries are concatenated into
        one node buffer and one triangle buffer and each instance carries the offsets of its own
        range. `flatten` does that; this is what comes out.

        **The transforms are stored as three rows rather than as a matrix, deliberately.** A matrix in
        a buffer means agreeing with the shader compiler about row-major against column-major, and
        that agreement is exactly the kind of duplicated fact with nothing holding the two copies
        together that this repository has been bitten by before. Three `float4` rows of an affine
        transform have precisely one interpretation, the shader multiplies them out by hand, and the
        bottom row of an affine matrix is always (0, 0, 0, 1) so nothing is lost by not storing it.

        | Offset | Size | Member              | std430 equivalent |
        |-------:|-----:|---------------------|-------------------|
        |      0 |   48 | `to_instance_row*`  | `float4` x 3      |
        |     48 |   48 | `to_world_row*`     | `float4` x 3      |
        |     96 |   12 | `bounds_min`        | `float3`          |
        |    108 |    4 | `node_offset`       | `uint`            |
        |    112 |   12 | `bounds_max`        | `float3`          |
        |    124 |    4 | `triangle_offset`   | `uint`            |
        |    128 |    4 | `node_count`        | `uint`            |
        |    132 |   12 | padding             | `uint` x 3        |
    */
    struct InstanceRecord {
        MathLib::Vec4 to_instance_row0{}; //!< First row of the world-to-instance transform.
        MathLib::Vec4 to_instance_row1{};
        MathLib::Vec4 to_instance_row2{};
        MathLib::Vec4 to_world_row0{}; //!< First row of the instance-to-world transform, for normals.
        MathLib::Vec4 to_world_row1{};
        MathLib::Vec4 to_world_row2{};

        MathLib::Vec3 bounds_min{}; //!< World-space bounds, for the top level's rejection test.
        uint32_t node_offset{0u}; //!< Where this instance's geometry begins in the shared node buffer.
        MathLib::Vec3 bounds_max{};
        uint32_t triangle_offset{0u}; //!< Where this instance's geometry begins in the shared triangle buffer.

        uint32_t node_count{0u}; //!< Nodes in this instance's geometry. Zero means the shader skips it.
        uint32_t padding0{0u}; //!< Unused; present so the layout matches std430 exactly.
        uint32_t padding1{0u};
        uint32_t padding2{0u};
    };

    /*!
        A scene in the form three storage buffers want.

        One node buffer, one triangle buffer, one instance buffer — nothing else, and no indirection
        beyond the offsets each record carries.
    */
    struct FlatScene {
        std::vector<Node> nodes; //!< Every geometry's nodes, concatenated in geometry order.
        std::vector<Triangle> triangles; //!< Every geometry's triangles, concatenated in the same order.
        std::vector<InstanceRecord> instances; //!< One record per instance, in the scene's own order.
    };

    /*!
        Concatenates a scene's geometries and resolves each instance to offsets into the result.

        Instance order is preserved exactly, so a `Hit::instance` from the shader means the same
        placement it means on the host.

        **A triangle index from the flat form is global**, into `FlatScene::triangles`, where a
        `Hit::triangle` from `intersectScene` is local to the instance's own geometry. That is not an
        inconsistency to iron out: the host has the per-geometry array to index and the shader does
        not, so each returns an index into the array it actually has. The shader adds the offset as it
        goes, which costs nothing and saves every caller from having to.

        An instance naming a geometry that does not exist is emitted with a node count of zero, which
        is the flat spelling of the skip `intersectScene` performs — an empty geometry is silent
        rather than an error, matching how an empty `Bvh` behaves everywhere else here.

        \param scene Geometries and the instances placing them.
        \return The three arrays, ready for a `std::memcpy` into mapped buffers.
    */
    [[nodiscard]] FlatScene flatten(const Scene& scene);

    /*!
        Packs one instance into the record the shader reads, given where its geometry landed.

        The per-instance half of `flatten`, public because a moving instance is exactly this much
        work per tick: the geometry offsets never change once the buffers are uploaded, so a caller
        that cached them rebuilds one 144-byte record per moved body instead of re-concatenating
        every node and triangle on the Grid. `flatten` itself is written in terms of this function,
        which is what keeps the two from drifting.

        \param instance The placement, with its transforms and world bounds current.
        \param node_offset Where the instance's geometry begins in the shared node buffer.
        \param triangle_offset Where it begins in the shared triangle buffer.
        \param node_count Nodes in that geometry. Zero makes the shader skip the instance, which is
               also how a caller silences a body deliberately — a creature's own senses blank their
               own record rather than asking the shader to learn a skip.
        \return The record, ready to overwrite its slot in a mapped instance buffer.
    */
    [[nodiscard]] InstanceRecord flattenInstance(const Instance& instance, uint32_t node_offset, uint32_t triangle_offset, uint32_t node_count);

} // namespace BvhLib
