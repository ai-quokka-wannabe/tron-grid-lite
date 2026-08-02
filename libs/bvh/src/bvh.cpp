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

#include <bvh/bvh.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>
#include <limits>

namespace BvhLib
{

    static_assert(sizeof(Triangle) == 48u, "Triangle must be exactly 48 bytes to match its std430 declaration.");
    static_assert(offsetof(Triangle, material) == 12u, "Triangle::material must sit at offset 12.");
    static_assert(offsetof(Triangle, edge1) == 16u, "Triangle::edge1 must sit at offset 16.");
    static_assert(offsetof(Triangle, edge2) == 32u, "Triangle::edge2 must sit at offset 32.");

    static_assert(sizeof(Node) == 32u, "Node must be exactly 32 bytes to match its std430 declaration.");
    static_assert(offsetof(Node, left_or_first) == 12u, "Node::left_or_first must sit at offset 12.");
    static_assert(offsetof(Node, bounds_max) == 16u, "Node::bounds_max must sit at offset 16.");
    static_assert(offsetof(Node, triangle_count) == 28u, "Node::triangle_count must sit at offset 28.");

    namespace
    {

        //! Number of buckets the surface-area heuristic samples along the split axis.
        constexpr uint32_t BIN_COUNT{12u};

        //! Relative cost of descending one interior node, expressed in units of one triangle test.
        constexpr float TRAVERSAL_COST{1.0f};

        //! An axis-aligned bounding box that starts empty and grows to fit whatever it is shown.
        struct Aabb {
            MathLib::Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            MathLib::Vec3 max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

            //! Grows the box to contain the given point.
            void grow(const MathLib::Vec3& point)
            {
                min.x = std::min(min.x, point.x);
                min.y = std::min(min.y, point.y);
                min.z = std::min(min.z, point.z);
                max.x = std::max(max.x, point.x);
                max.y = std::max(max.y, point.y);
                max.z = std::max(max.z, point.z);
            }

            //! Grows the box to contain another box. A no-op when `other` is empty.
            void grow(const Aabb& other)
            {
                if (other.empty()) {
                    return;
                }
                grow(other.min);
                grow(other.max);
            }

            //! Grows the box to contain all three vertices of a triangle.
            void grow(const Triangle& triangle)
            {
                grow(triangle.v0);
                grow(triangle.v1());
                grow(triangle.v2());
            }

            //! Returns true when nothing has been added yet.
            [[nodiscard]] bool empty() const
            {
                return (min.x > max.x) || (min.y > max.y) || (min.z > max.z);
            }

            //! Returns the extent along each axis, or zero when empty.
            [[nodiscard]] MathLib::Vec3 extent() const
            {
                if (empty()) {
                    return MathLib::Vec3{0.0f, 0.0f, 0.0f};
                }
                return max - min;
            }

            /*!
                Returns the total surface area, which is what the heuristic is a heuristic about:
                the chance a random ray enters a box is proportional to the area of its surface.
            */
            [[nodiscard]] float surfaceArea() const
            {
                const MathLib::Vec3 size{extent()};
                return 2.0f * ((size.x * size.y) + (size.y * size.z) + (size.z * size.x));
            }
        };

        //! Returns the component of `vector` along `axis`, where 0 is x, 1 is y and 2 is z.
        [[nodiscard]] float component(const MathLib::Vec3& vector, uint32_t axis)
        {
            if (axis == 0u) {
                return vector.x;
            }
            if (axis == 1u) {
                return vector.y;
            }
            return vector.z;
        }

        //! One bucket of the binned build: the triangles that fall in it, and the box they occupy.
        struct Bin {
            Aabb bounds{};
            uint32_t count{0u};
        };

        //! Everything the recursive builder needs to carry around.
        struct Builder {
            std::vector<Node>* nodes{nullptr};
            std::vector<Triangle>* triangles{nullptr};

            /*!
                Builds the subtree rooted at `node_index`, which already owns the triangle range
                [first, first + count).

                \param node_index Index of the node to fill in.
                \param first First triangle of the range this node owns.
                \param count Number of triangles in the range.
                \param depth Depth of this node, with the root at zero.
            */
            void subdivide(uint32_t node_index, uint32_t first, uint32_t count, uint32_t depth);

            //! Turns a node into a leaf owning the whole range.
            void makeLeaf(uint32_t node_index, uint32_t first, uint32_t count)
            {
                Node& node{(*nodes)[node_index]};
                node.left_or_first = first;
                node.triangle_count = count;
            }
        };

        void Builder::subdivide(uint32_t node_index, uint32_t first, uint32_t count, uint32_t depth)
        {
            {
                Aabb bounds{};
                for (uint32_t index{first}; index < (first + count); ++index) {
                    bounds.grow((*triangles)[index]);
                }
                Node& node{(*nodes)[node_index]};
                node.bounds_min = bounds.min;
                node.bounds_max = bounds.max;
            }

            // A node this small, or this deep, is not worth splitting. The depth cap is what keeps
            // the tree inside the shader's fixed traversal stack.
            if ((count <= MAX_LEAF_TRIANGLES) || (depth >= (MAX_DEPTH - 1u))) {
                makeLeaf(node_index, first, count);
                return;
            }

            // Split along whichever axis the centroids are most spread out over. Bounding the
            // centroids rather than the triangles is what stops a few large triangles from
            // dictating the split plane.
            Aabb centroid_bounds{};
            for (uint32_t index{first}; index < (first + count); ++index) {
                centroid_bounds.grow((*triangles)[index].centroid());
            }

            const MathLib::Vec3 centroid_extent{centroid_bounds.extent()};
            uint32_t axis{0u};
            if (centroid_extent.y > component(centroid_extent, axis)) {
                axis = 1u;
            }
            if (centroid_extent.z > component(centroid_extent, axis)) {
                axis = 2u;
            }

            const float axis_extent{component(centroid_extent, axis)};
            if (axis_extent <= 0.0f) {
                // Every centroid coincides, so no split plane can separate them.
                makeLeaf(node_index, first, count);
                return;
            }

            const float axis_min{component(centroid_bounds.min, axis)};
            const float bin_scale{static_cast<float>(BIN_COUNT) / axis_extent};

            /*
                The extent test above rejects zero but not the denormal range just above it, where
                the division overflows to infinity. A centroid sitting exactly at axis_min then
                computes 0 * inf, which is NaN, and casting NaN to int32_t is undefined behaviour
                that the clamp below cannot repair. Worse, the same lambda is the predicate of a
                std::partition, and a predicate that answers inconsistently is undefined behaviour
                in its own right.

                Guarding the scale rather than picking a threshold on the extent is what makes this
                correct: comparing the extent against the smallest normal float does not help,
                because twelve divided by that is still infinity.
            */
            if (!std::isfinite(bin_scale)) {
                makeLeaf(node_index, first, count);
                return;
            }

            const auto binOf = [&](const Triangle& triangle) -> uint32_t {
                const float offset{component(triangle.centroid(), axis) - axis_min};
                const int32_t raw{static_cast<int32_t>(offset * bin_scale)};
                return static_cast<uint32_t>(std::clamp(raw, 0, static_cast<int32_t>(BIN_COUNT) - 1));
            };

            std::array<Bin, BIN_COUNT> bins{};
            for (uint32_t index{first}; index < (first + count); ++index) {
                const Triangle& triangle{(*triangles)[index]};
                Bin& bin{bins[binOf(triangle)]};
                bin.bounds.grow(triangle);
                ++bin.count;
            }

            // Sweep from both ends so that each candidate plane knows the box and population on
            // either side of it without rescanning the triangles.
            std::array<float, BIN_COUNT - 1u> left_area{};
            std::array<uint32_t, BIN_COUNT - 1u> left_count{};
            std::array<float, BIN_COUNT - 1u> right_area{};
            std::array<uint32_t, BIN_COUNT - 1u> right_count{};

            {
                Aabb sweep{};
                uint32_t population{0u};
                for (uint32_t bin{0u}; bin < (BIN_COUNT - 1u); ++bin) {
                    sweep.grow(bins[bin].bounds);
                    population += bins[bin].count;
                    left_area[bin] = sweep.surfaceArea();
                    left_count[bin] = population;
                }
            }
            {
                Aabb sweep{};
                uint32_t population{0u};
                for (uint32_t bin{BIN_COUNT - 1u}; bin > 0u; --bin) {
                    sweep.grow(bins[bin].bounds);
                    population += bins[bin].count;
                    right_area[bin - 1u] = sweep.surfaceArea();
                    right_count[bin - 1u] = population;
                }
            }

            float best_cost{std::numeric_limits<float>::max()};
            uint32_t best_bin{0u};
            for (uint32_t bin{0u}; bin < (BIN_COUNT - 1u); ++bin) {
                if ((left_count[bin] == 0u) || (right_count[bin] == 0u)) {
                    continue;
                }
                const float cost{(left_area[bin] * static_cast<float>(left_count[bin])) + (right_area[bin] * static_cast<float>(right_count[bin]))};
                if (cost < best_cost) {
                    best_cost = cost;
                    best_bin = bin;
                }
            }

            const Node& node{(*nodes)[node_index]};
            Aabb node_bounds{};
            node_bounds.grow(node.bounds_min);
            node_bounds.grow(node.bounds_max);
            const float leaf_cost{node_bounds.surfaceArea() * static_cast<float>(count)};

            /*
                Splitting only pays for itself when the triangle tests it saves outweigh the extra
                interior node a ray must descend through. The traversal term therefore belongs on
                the split's side of the comparison: written with a plus, as it was, the test could
                never fire, because a split's cost is bounded above by the leaf's cost whenever the
                bin bounds are subsets of the node's own.
            */
            if ((best_cost + (TRAVERSAL_COST * node_bounds.surfaceArea())) >= leaf_cost) {
                makeLeaf(node_index, first, count);
                return;
            }

            const auto middle = std::partition((*triangles).begin() + first, (*triangles).begin() + first + count, [&](const Triangle& triangle) {
                return binOf(triangle) <= best_bin;
            });

            uint32_t left_size{static_cast<uint32_t>(std::distance((*triangles).begin() + first, middle))};

            if ((left_size == 0u) || (left_size == count)) {
                // The chosen plane failed to separate anything, which the sweep above should have
                // ruled out. Fall back to a median split rather than recursing forever.
                const uint32_t median{count / 2u};
                std::nth_element((*triangles).begin() + first, (*triangles).begin() + first + median, (*triangles).begin() + first + count,
                    [&](const Triangle& a, const Triangle& b) {
                        return component(a.centroid(), axis) < component(b.centroid(), axis);
                    });
                left_size = median;
            }

            const uint32_t left_child{static_cast<uint32_t>(nodes->size())};
            nodes->emplace_back();
            nodes->emplace_back();

            {
                Node& parent{(*nodes)[node_index]};
                parent.left_or_first = left_child;
                parent.triangle_count = 0u;
            }

            subdivide(left_child, first, left_size, depth + 1u);
            subdivide(left_child + 1u, first + left_size, count - left_size, depth + 1u);
        }

        //! Returned by the slab test when a ray misses the box entirely.
        constexpr float MISS{std::numeric_limits<float>::infinity()};

        /*!
            Slab test. Returns the distance at which the ray enters the box, or MISS.

            The sentinel is deliberately positive infinity rather than a negative number: the
            traversal below orders two sibling boxes by comparing these distances, and a negative
            sentinel would sort a missed box in front of a hit one and discard the hit.
        */
        [[nodiscard]] float intersectAabb(const MathLib::Vec3& origin, const MathLib::Vec3& inverse_direction, const MathLib::Vec3& bounds_min,
            const MathLib::Vec3& bounds_max, float max_distance)
        {
            const float tx1{(bounds_min.x - origin.x) * inverse_direction.x};
            const float tx2{(bounds_max.x - origin.x) * inverse_direction.x};
            float tmin{std::min(tx1, tx2)};
            float tmax{std::max(tx1, tx2)};

            const float ty1{(bounds_min.y - origin.y) * inverse_direction.y};
            const float ty2{(bounds_max.y - origin.y) * inverse_direction.y};
            tmin = std::max(tmin, std::min(ty1, ty2));
            tmax = std::min(tmax, std::max(ty1, ty2));

            const float tz1{(bounds_min.z - origin.z) * inverse_direction.z};
            const float tz2{(bounds_max.z - origin.z) * inverse_direction.z};
            tmin = std::max(tmin, std::min(tz1, tz2));
            tmax = std::min(tmax, std::max(tz1, tz2));

            if ((tmax >= std::max(tmin, 0.0f)) && (tmin < max_distance)) {
                return std::max(tmin, 0.0f);
            }
            return MISS;
        }

        /*!
            Möller-Trumbore triangle intersection.

            Back faces are accepted as well as front faces: a creature inside a closed shape should
            see its inner surface rather than nothing at all.
        */
        void intersectTriangle(const Triangle& triangle, uint32_t index, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance, Hit& nearest)
        {
            constexpr float EPSILON{1e-8f};

            const MathLib::Vec3 pvec{direction.cross(triangle.edge2)};
            const float determinant{triangle.edge1.dot(pvec)};

            if ((determinant > -EPSILON) && (determinant < EPSILON)) {
                return; // The ray runs parallel to the triangle's plane.
            }

            const float inverse_determinant{1.0f / determinant};
            const MathLib::Vec3 tvec{origin - triangle.v0};
            const float u{tvec.dot(pvec) * inverse_determinant};

            if ((u < 0.0f) || (u > 1.0f)) {
                return;
            }

            const MathLib::Vec3 qvec{tvec.cross(triangle.edge1)};
            const float v{direction.dot(qvec) * inverse_determinant};

            if ((v < 0.0f) || ((u + v) > 1.0f)) {
                return;
            }

            const float distance{triangle.edge2.dot(qvec) * inverse_determinant};

            if ((distance <= EPSILON) || (distance >= max_distance)) {
                return;
            }

            if (nearest.valid && (distance >= nearest.distance)) {
                return;
            }

            nearest.distance = distance;
            nearest.triangle = index;
            nearest.barycentric_u = u;
            nearest.barycentric_v = v;
            nearest.valid = true;
        }

        //! Recursive depth measurement, used only by tests and diagnostics.
        [[nodiscard]] uint32_t depthOf(const std::vector<Node>& nodes, uint32_t node_index, uint32_t depth)
        {
            const Node& node{nodes[node_index]};
            if (node.isLeaf()) {
                return depth;
            }
            const uint32_t left{depthOf(nodes, node.left_or_first, depth + 1u)};
            const uint32_t right{depthOf(nodes, node.left_or_first + 1u, depth + 1u)};
            return std::max(left, right);
        }

    } // namespace

    uint32_t Bvh::depth() const
    {
        if (nodes.empty()) {
            return 0u;
        }
        return depthOf(nodes, 0u, 1u);
    }

    Bvh build(std::vector<Triangle> triangles)
    {
        Bvh bvh{};
        if (triangles.empty()) {
            return bvh;
        }

        bvh.triangles = std::move(triangles);

        // An interior node always adds exactly two children, so a tree over n triangles can never
        // exceed 2n - 1 nodes. Reserving up front keeps the node references inside subdivide()
        // from being invalidated mid-build.
        bvh.nodes.reserve((2u * bvh.triangles.size()) - 1u);
        bvh.nodes.emplace_back();

        Builder builder{.nodes = &bvh.nodes, .triangles = &bvh.triangles};
        builder.subdivide(0u, 0u, static_cast<uint32_t>(bvh.triangles.size()), 0u);

        return bvh;
    }

    Hit intersect(const Bvh& bvh, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance)
    {
        Hit nearest{};
        if (bvh.nodes.empty()) {
            return nearest;
        }

        /*
            A direction component of exactly zero would make its reciprocal infinite, and an origin
            lying exactly on that slab's plane then computes 0 * inf, which is NaN. std::min and
            std::max propagate a NaN in their first argument through every later reduction, so both
            tmin and tmax become NaN, the entry test fails, and a box the ray demonstrably passes
            through reports a miss.

            That is not a theoretical case here: the floor is axis-aligned on a two-metre grid, so a
            ray travelling straight down from an exact grid coordinate hits it precisely. Substituting
            a tiny magnitude keeps the reciprocal finite and leaves the comparisons unchanged.
        */
        constexpr float TINY{1e-30f};
        const auto safeInverse = [](float value) {
            return 1.0f / ((value == 0.0f) ? TINY : value);
        };
        const MathLib::Vec3 inverse_direction{safeInverse(direction.x), safeInverse(direction.y), safeInverse(direction.z)};

        std::array<uint32_t, MAX_DEPTH> stack{};
        uint32_t stack_size{0u};
        uint32_t current{0u};

        for (;;) {
            const Node& node{bvh.nodes[current]};

            if (node.isLeaf()) {
                for (uint32_t offset{0u}; offset < node.triangle_count; ++offset) {
                    const uint32_t index{node.left_or_first + offset};
                    intersectTriangle(bvh.triangles[index], index, origin, direction, max_distance, nearest);
                }
            } else {
                const uint32_t left{node.left_or_first};
                const uint32_t right{left + 1u};

                const float limit{nearest.valid ? nearest.distance : max_distance};
                float left_distance{intersectAabb(origin, inverse_direction, bvh.nodes[left].bounds_min, bvh.nodes[left].bounds_max, limit)};
                float right_distance{intersectAabb(origin, inverse_direction, bvh.nodes[right].bounds_min, bvh.nodes[right].bounds_max, limit)};

                uint32_t near_child{left};
                uint32_t far_child{right};
                if (right_distance < left_distance) {
                    std::swap(left_distance, right_distance);
                    std::swap(near_child, far_child);
                }

                // After the swap the nearer box is in left_distance, so a finite value there means
                // at least one child was hit and the nearer one is the right place to go next.
                if (left_distance < MISS) {
                    if (right_distance < MISS) {
                        stack[stack_size] = far_child;
                        ++stack_size;
                    }
                    current = near_child;
                    continue;
                }
            }

            if (stack_size == 0u) {
                break;
            }
            --stack_size;
            current = stack[stack_size];
        }

        return nearest;
    }

    Hit intersectBruteForce(const std::vector<Triangle>& triangles, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance)
    {
        Hit nearest{};
        for (uint32_t index{0u}; index < static_cast<uint32_t>(triangles.size()); ++index) {
            intersectTriangle(triangles[index], index, origin, direction, max_distance, nearest);
        }
        return nearest;
    }

    Instance makeInstance(const Bvh& geometry, uint32_t geometry_index, const MathLib::Mat4& to_world)
    {
        Instance instance{};
        instance.to_world = to_world;
        instance.to_instance = to_world.inversed();
        instance.geometry = geometry_index;

        if (geometry.nodes.empty()) {
            // An empty geometry has no bounds to transform. Leaving them equal makes the rejection
            // test below reject everything, which is the right answer for geometry that is not there.
            return instance;
        }

        /*
            All eight corners are transformed, not just the two extremes. Transforming a box by its
            min and max alone is only correct for an axis-aligned scale-and-translate; under any
            rotation it produces a box that does not contain the rotated geometry, and rays that
            should have hit are rejected before they ever reach the hierarchy.
        */
        const MathLib::Vec3& low{geometry.nodes[0].bounds_min};
        const MathLib::Vec3& high{geometry.nodes[0].bounds_max};

        Aabb world{};
        for (uint32_t corner{0u}; corner < 8u; ++corner) {
            const MathLib::Vec3 local{((corner & 1u) != 0u) ? high.x : low.x, ((corner & 2u) != 0u) ? high.y : low.y, ((corner & 4u) != 0u) ? high.z : low.z};
            const MathLib::Vec4 transformed{to_world * MathLib::Vec4::fromVec3(local, 1.0f)};
            world.grow(MathLib::Vec3{transformed.x, transformed.y, transformed.z});
        }

        instance.bounds_min = world.min;
        instance.bounds_max = world.max;
        return instance;
    }

    Hit intersectScene(const Scene& scene, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance)
    {
        Hit nearest{};

        constexpr float TINY{1e-30f};
        const auto safeInverse = [](float value) {
            return 1.0f / ((value == 0.0f) ? TINY : value);
        };
        const MathLib::Vec3 inverse_direction{safeInverse(direction.x), safeInverse(direction.y), safeInverse(direction.z)};

        for (uint32_t index{0u}; index < static_cast<uint32_t>(scene.instances.size()); ++index) {
            const Instance& instance{scene.instances[index]};

            if (instance.geometry >= scene.geometries.size()) {
                continue; // An instance naming a geometry that is not there contributes nothing.
            }

            /*
                The top level is a linear sweep over instance boxes rather than a hierarchy over them.

                That is a deliberate choice for the counts involved: a handful of creatures plus the
                Grid is twenty-odd boxes, and a slab test is a few instructions. Building a hierarchy
                over twenty objects would cost more to maintain than it saves to traverse. It becomes
                the wrong choice somewhere in the hundreds, and the measurement that decides when is
                in docs/ARCHITECTURE.md rather than in a guess here.
            */
            const float limit{nearest.valid ? nearest.distance : max_distance};
            if (intersectAabb(origin, inverse_direction, instance.bounds_min, instance.bounds_max, limit) >= MISS) {
                continue;
            }

            /*
                Into the instance's own frame, and deliberately without normalising the direction: the
                ray parameter is then the same number in both frames, so the distance that comes back
                is already a world distance and max_distance can be passed straight through.
            */
            const MathLib::Vec4 local_origin{instance.to_instance * MathLib::Vec4::fromVec3(origin, 1.0f)};
            const MathLib::Vec4 local_direction{instance.to_instance * MathLib::Vec4::fromVec3(direction, 0.0f)};

            const Hit hit{intersect(scene.geometries[instance.geometry], MathLib::Vec3{local_origin.x, local_origin.y, local_origin.z},
                MathLib::Vec3{local_direction.x, local_direction.y, local_direction.z}, limit)};

            if (hit.valid && ((!nearest.valid) || (hit.distance < nearest.distance))) {
                nearest = hit;
                nearest.instance = index;
            }
        }

        return nearest;
    }

} // namespace BvhLib
