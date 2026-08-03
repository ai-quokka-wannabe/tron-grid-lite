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
#include <testing/testing.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace
{

    //! Deterministic pseudo-random generator, so a failing test always fails the same way.
    class Rng {
    public:
        explicit Rng(uint32_t seed) :
            m_state(seed | 1u)
        {
        }

        //! Returns the next 32 bits using the xorshift32 sequence.
        [[nodiscard]] uint32_t next()
        {
            m_state ^= m_state << 13u;
            m_state ^= m_state >> 17u;
            m_state ^= m_state << 5u;
            return m_state;
        }

        //! Returns a float in [low, high).
        [[nodiscard]] float range(float low, float high)
        {
            const float unit{static_cast<float>(next() & 0xFFFFFFu) / static_cast<float>(0x1000000u)};
            return low + (unit * (high - low));
        }

        //! Returns a vector with each component in [low, high).
        [[nodiscard]] MathLib::Vec3 vector(float low, float high)
        {
            return MathLib::Vec3{range(low, high), range(low, high), range(low, high)};
        }

    private:
        uint32_t m_state; //!< Xorshift state; never zero, which the constructor guarantees.
    };

    //! Builds a triangle from three vertices.
    [[nodiscard]] BvhLib::Triangle makeTriangle(const MathLib::Vec3& a, const MathLib::Vec3& b, const MathLib::Vec3& c, uint32_t material = 0u)
    {
        return BvhLib::Triangle{.v0 = a, .material = material, .edge1 = b - a, .padding0 = 0u, .edge2 = c - a, .padding1 = 0u};
    }

    //! Scatters small triangles through a box, which is the case the hierarchy is built for.
    [[nodiscard]] std::vector<BvhLib::Triangle> randomCloud(uint32_t count, uint32_t seed)
    {
        Rng rng{seed};
        std::vector<BvhLib::Triangle> triangles;
        triangles.reserve(count);
        for (uint32_t index{0u}; index < count; ++index) {
            const MathLib::Vec3 origin{rng.vector(-50.0f, 50.0f)};
            triangles.push_back(makeTriangle(origin, origin + rng.vector(0.1f, 3.0f), origin + rng.vector(0.1f, 3.0f), index % 7u));
        }
        return triangles;
    }

    //! Checks that `outer` contains `inner`, allowing for floating-point slop.
    [[nodiscard]] bool contains(const MathLib::Vec3& outer_min, const MathLib::Vec3& outer_max, const MathLib::Vec3& point)
    {
        constexpr float TOLERANCE{1e-4f};
        return (point.x >= (outer_min.x - TOLERANCE)) && (point.x <= (outer_max.x + TOLERANCE)) && (point.y >= (outer_min.y - TOLERANCE))
            && (point.y <= (outer_max.y + TOLERANCE)) && (point.z >= (outer_min.z - TOLERANCE)) && (point.z <= (outer_max.z + TOLERANCE));
    }

    //! Walks the tree checking structural invariants, and counts how many triangles the leaves own.
    void checkNode(const BvhLib::Bvh& bvh, uint32_t node_index, std::vector<uint32_t>& triangle_visits)
    {
        const BvhLib::Node& node{bvh.nodes[node_index]};

        if (node.isLeaf()) {
            for (uint32_t offset{0u}; offset < node.triangle_count; ++offset) {
                const uint32_t index{node.left_or_first + offset};
                TEST_CHECK(index < bvh.triangles.size());
                ++triangle_visits[index];

                const BvhLib::Triangle& triangle{bvh.triangles[index]};
                TEST_CHECK(contains(node.bounds_min, node.bounds_max, triangle.v0));
                TEST_CHECK(contains(node.bounds_min, node.bounds_max, triangle.v1()));
                TEST_CHECK(contains(node.bounds_min, node.bounds_max, triangle.v2()));
            }
            return;
        }

        const uint32_t left{node.left_or_first};
        const uint32_t right{left + 1u};
        TEST_CHECK(right < bvh.nodes.size());

        for (const uint32_t child : {left, right}) {
            TEST_CHECK(contains(node.bounds_min, node.bounds_max, bvh.nodes[child].bounds_min));
            TEST_CHECK(contains(node.bounds_min, node.bounds_max, bvh.nodes[child].bounds_max));
            checkNode(bvh, child, triangle_visits);
        }
    }

} // namespace

TEST_CASE(bvh_empty_input_produces_empty_hierarchy)
{
    const BvhLib::Bvh bvh{BvhLib::build({})};

    TEST_CHECK(bvh.empty());
    TEST_CHECK(bvh.nodes.empty());
    TEST_CHECK(bvh.triangles.empty());
    TEST_CHECK_EQUAL(bvh.depth(), 0u);

    const BvhLib::Hit hit{BvhLib::intersect(bvh, MathLib::Vec3{0.0f, 0.0f, 0.0f}, MathLib::Vec3{0.0f, 0.0f, -1.0f}, 1000.0f)};
    TEST_CHECK(!hit.valid);
}

TEST_CASE(bvh_single_triangle_is_one_leaf_and_is_hit)
{
    std::vector<BvhLib::Triangle> triangles{makeTriangle(MathLib::Vec3{-1.0f, -1.0f, -5.0f}, MathLib::Vec3{1.0f, -1.0f, -5.0f}, MathLib::Vec3{0.0f, 1.0f, -5.0f}, 3u)};

    const BvhLib::Bvh bvh{BvhLib::build(triangles)};

    TEST_CHECK(!bvh.empty());
    TEST_CHECK_EQUAL(bvh.nodes.size(), static_cast<size_t>(1u));
    TEST_CHECK(bvh.nodes[0].isLeaf());
    TEST_CHECK_EQUAL(bvh.nodes[0].triangle_count, 1u);

    const BvhLib::Hit hit{BvhLib::intersect(bvh, MathLib::Vec3{0.0f, 0.0f, 0.0f}, MathLib::Vec3{0.0f, 0.0f, -1.0f}, 1000.0f)};
    TEST_CHECK(hit.valid);
    TEST_CHECK(std::abs(hit.distance - 5.0f) < 1e-4f);
    TEST_CHECK_EQUAL(bvh.triangles[hit.triangle].material, 3u);
}

TEST_CASE(bvh_ray_pointing_away_misses)
{
    std::vector<BvhLib::Triangle> triangles{makeTriangle(MathLib::Vec3{-1.0f, -1.0f, -5.0f}, MathLib::Vec3{1.0f, -1.0f, -5.0f}, MathLib::Vec3{0.0f, 1.0f, -5.0f})};

    const BvhLib::Bvh bvh{BvhLib::build(triangles)};

    const BvhLib::Hit hit{BvhLib::intersect(bvh, MathLib::Vec3{0.0f, 0.0f, 0.0f}, MathLib::Vec3{0.0f, 0.0f, 1.0f}, 1000.0f)};
    TEST_CHECK(!hit.valid);
}

TEST_CASE(bvh_max_distance_is_respected)
{
    std::vector<BvhLib::Triangle> triangles{makeTriangle(MathLib::Vec3{-1.0f, -1.0f, -5.0f}, MathLib::Vec3{1.0f, -1.0f, -5.0f}, MathLib::Vec3{0.0f, 1.0f, -5.0f})};

    const BvhLib::Bvh bvh{BvhLib::build(triangles)};

    TEST_CHECK(BvhLib::intersect(bvh, MathLib::Vec3{0.0f, 0.0f, 0.0f}, MathLib::Vec3{0.0f, 0.0f, -1.0f}, 4.0f).valid == false);
    TEST_CHECK(BvhLib::intersect(bvh, MathLib::Vec3{0.0f, 0.0f, 0.0f}, MathLib::Vec3{0.0f, 0.0f, -1.0f}, 6.0f).valid == true);
}

TEST_CASE(bvh_back_faces_are_hit_too)
{
    // A creature inside a closed shape must see its inner surface, not nothing at all.
    std::vector<BvhLib::Triangle> triangles{makeTriangle(MathLib::Vec3{-1.0f, -1.0f, -5.0f}, MathLib::Vec3{0.0f, 1.0f, -5.0f}, MathLib::Vec3{1.0f, -1.0f, -5.0f})};

    const BvhLib::Bvh bvh{BvhLib::build(triangles)};

    const BvhLib::Hit hit{BvhLib::intersect(bvh, MathLib::Vec3{0.0f, 0.0f, 0.0f}, MathLib::Vec3{0.0f, 0.0f, -1.0f}, 1000.0f)};
    TEST_CHECK(hit.valid);
}

TEST_CASE(bvh_structure_is_sound_and_every_triangle_is_owned_exactly_once)
{
    const BvhLib::Bvh bvh{BvhLib::build(randomCloud(2000u, 12345u))};

    TEST_CHECK_EQUAL(bvh.triangles.size(), static_cast<size_t>(2000u));

    /*
        A hierarchy that never split would satisfy every other assertion in this file: a lone root
        leaf is structurally sound, owns every triangle exactly once, sits within the depth cap, and
        agrees with brute force perfectly — because it tests every triangle, which is what brute
        force does. Without this, a builder regression to "one big leaf" is invisible.

        2000 triangles at four per leaf force a depth of at least nine, so the bounds are generous.
    */
    TEST_CHECK(bvh.nodes.size() > 1u);
    TEST_CHECK(bvh.depth() > 4u);

    std::vector<uint32_t> visits(bvh.triangles.size(), 0u);
    checkNode(bvh, 0u, visits);

    const bool every_triangle_owned_once{std::all_of(visits.begin(), visits.end(), [](uint32_t count) {
        return count == 1u;
    })};
    TEST_CHECK(every_triangle_owned_once);
}

TEST_CASE(bvh_never_exceeds_the_depth_the_shader_stack_is_sized_for)
{
    // The compute shader traverses with a fixed-size stack of exactly MAX_DEPTH entries, so a
    // deeper tree would overflow it silently. These triangles all share a corner and grow steadily,
    // which packs their centroids into a short span along one diagonal — awkward for a binned split
    // without being degenerate. The identical-triangle case is covered separately below.
    std::vector<BvhLib::Triangle> triangles;
    triangles.reserve(4000u);
    for (uint32_t index{0u}; index < 4000u; ++index) {
        const float size{1.0f + (static_cast<float>(index) * 0.001f)};
        triangles.push_back(makeTriangle(MathLib::Vec3{0.0f, 0.0f, 0.0f}, MathLib::Vec3{size, 0.0f, 0.0f}, MathLib::Vec3{0.0f, size, 0.0f}));
    }

    const BvhLib::Bvh identical{BvhLib::build(triangles)};
    TEST_CHECK(identical.depth() <= BvhLib::MAX_DEPTH);

    const BvhLib::Bvh scattered{BvhLib::build(randomCloud(20000u, 777u))};
    TEST_CHECK(scattered.depth() <= BvhLib::MAX_DEPTH);
}

TEST_CASE(bvh_traversal_agrees_with_brute_force_on_random_rays)
{
    // The test that actually proves the hierarchy correct: whatever clever pruning the traversal
    // does, it must return exactly what testing every triangle in turn would have returned.
    const std::vector<BvhLib::Triangle> triangles{randomCloud(1500u, 99u)};
    const BvhLib::Bvh bvh{BvhLib::build(triangles)};

    // Brute-force agreement is trivially satisfied by a hierarchy that never split, since it would
    // then be testing every triangle exactly as brute force does. Prove it actually built a tree.
    TEST_CHECK(bvh.nodes.size() > 1u);
    TEST_CHECK(bvh.depth() > 4u);

    Rng rng{424242u};
    uint32_t hits{0u};
    uint32_t agreements{0u};

    constexpr uint32_t RAY_COUNT{4000u};
    for (uint32_t index{0u}; index < RAY_COUNT; ++index) {
        // Aim each ray through the populated volume rather than in an arbitrary direction. A cloud
        // this sparse is mostly empty space, and rays that miss everything agree trivially.
        const MathLib::Vec3 origin{rng.vector(-120.0f, 120.0f)};
        const MathLib::Vec3 target{rng.vector(-50.0f, 50.0f)};
        const MathLib::Vec3 direction{(target - origin).normalised()};

        const BvhLib::Hit accelerated{BvhLib::intersect(bvh, origin, direction, 500.0f)};
        const BvhLib::Hit reference{BvhLib::intersectBruteForce(bvh.triangles, origin, direction, 500.0f)};

        bool agrees{accelerated.valid == reference.valid};
        if (agrees && accelerated.valid) {
            ++hits;
            // Compare distances rather than indices: two triangles may genuinely coincide, and it
            // is the geometry that matters, not which of a tie the traversal happened to keep.
            agrees = std::abs(accelerated.distance - reference.distance) < 1e-3f;
        }

        if (agrees) {
            ++agreements;
        }
    }

    TEST_CHECK_EQUAL(agreements, RAY_COUNT);

    // A run in which nothing was ever struck would pass the comparison while proving nothing.
    TEST_CHECK(hits > (RAY_COUNT / 20u));
}

TEST_CASE(bvh_traversal_agrees_with_brute_force_on_rays_that_start_inside)
{
    // Creature eyes sit inside the Grid rather than looking at it from outside, so the case of an
    // origin surrounded by geometry is the one that actually matters.
    const std::vector<BvhLib::Triangle> triangles{randomCloud(800u, 5150u)};
    const BvhLib::Bvh bvh{BvhLib::build(triangles)};

    Rng rng{31337u};
    uint32_t agreements{0u};
    uint32_t hits{0u};

    constexpr uint32_t RAY_COUNT{2000u};
    for (uint32_t index{0u}; index < RAY_COUNT; ++index) {
        const MathLib::Vec3 origin{rng.vector(-20.0f, 20.0f)};
        const MathLib::Vec3 target{rng.vector(-50.0f, 50.0f)};
        const MathLib::Vec3 direction{(target - origin).normalised()};

        const BvhLib::Hit accelerated{BvhLib::intersect(bvh, origin, direction, 1000.0f)};
        const BvhLib::Hit reference{BvhLib::intersectBruteForce(bvh.triangles, origin, direction, 1000.0f)};

        bool agrees{accelerated.valid == reference.valid};
        if (agrees && accelerated.valid) {
            ++hits;
            agrees = std::abs(accelerated.distance - reference.distance) < 1e-3f;
        }
        if (agrees) {
            ++agreements;
        }
    }

    TEST_CHECK_EQUAL(agreements, RAY_COUNT);
    TEST_CHECK(hits > (RAY_COUNT / 20u));
}

TEST_CASE(bvh_handles_a_flat_sheet_of_coplanar_triangles)
{
    // The grid floor is exactly this: thousands of triangles with zero extent on one axis, which
    // is the case a naive median split handles worst.
    std::vector<BvhLib::Triangle> triangles;
    triangles.reserve(64u * 64u * 2u);
    for (uint32_t z{0u}; z < 64u; ++z) {
        for (uint32_t x{0u}; x < 64u; ++x) {
            const float x0{static_cast<float>(x)};
            const float z0{static_cast<float>(z)};
            const MathLib::Vec3 a{x0, 0.0f, z0};
            const MathLib::Vec3 b{x0 + 1.0f, 0.0f, z0};
            const MathLib::Vec3 c{x0, 0.0f, z0 + 1.0f};
            const MathLib::Vec3 d{x0 + 1.0f, 0.0f, z0 + 1.0f};
            triangles.push_back(makeTriangle(a, c, b));
            triangles.push_back(makeTriangle(b, c, d));
        }
    }

    const BvhLib::Bvh bvh{BvhLib::build(triangles)};
    TEST_CHECK(bvh.depth() <= BvhLib::MAX_DEPTH);

    // The depth cap is satisfied trivially by a hierarchy of depth one.
    TEST_CHECK(bvh.nodes.size() > 1u);
    TEST_CHECK(bvh.depth() > 4u);

    // Straight down onto the middle of the sheet.
    const BvhLib::Hit hit{BvhLib::intersect(bvh, MathLib::Vec3{32.5f, 10.0f, 32.5f}, MathLib::Vec3{0.0f, -1.0f, 0.0f}, 100.0f)};
    TEST_CHECK(hit.valid);
    TEST_CHECK(std::abs(hit.distance - 10.0f) < 1e-3f);

    // Just past the edge of the sheet, which must miss.
    const BvhLib::Hit miss{BvhLib::intersect(bvh, MathLib::Vec3{-5.0f, 10.0f, 32.5f}, MathLib::Vec3{0.0f, -1.0f, 0.0f}, 100.0f)};
    TEST_CHECK(!miss.valid);
}

TEST_CASE(bvh_axis_parallel_rays_from_exact_grid_coordinates_still_hit)
{
    /*
        The regression that matters most for the Grid.

        A ray straight down has two direction components of exactly zero, so their reciprocals are
        infinite. Launch it from a coordinate that lies exactly on a node boundary — which every
        integer coordinate does, on an axis-aligned grid — and the slab test computes 0 * inf,
        which is NaN. Both std::min and std::max propagate a NaN from their first argument, so the
        box reports a miss and the floor develops invisible holes at precisely the tidiest
        coordinates in the scene.
    */
    std::vector<BvhLib::Triangle> triangles;
    triangles.reserve(64u * 64u * 2u);
    for (uint32_t z{0u}; z < 64u; ++z) {
        for (uint32_t x{0u}; x < 64u; ++x) {
            const float x0{static_cast<float>(x)};
            const float z0{static_cast<float>(z)};
            const MathLib::Vec3 a{x0, 0.0f, z0};
            const MathLib::Vec3 b{x0 + 1.0f, 0.0f, z0};
            const MathLib::Vec3 c{x0, 0.0f, z0 + 1.0f};
            const MathLib::Vec3 d{x0 + 1.0f, 0.0f, z0 + 1.0f};
            triangles.push_back(makeTriangle(a, c, b));
            triangles.push_back(makeTriangle(b, c, d));
        }
    }

    const BvhLib::Bvh bvh{BvhLib::build(triangles)};

    // Sweep across every whole-metre line rather than testing one, since which coordinates land on
    // a node boundary depends on how the builder happened to partition.
    uint32_t hits{0u};
    uint32_t tested{0u};
    for (uint32_t x{1u}; x < 64u; ++x) {
        for (const float z : {8.5f, 32.5f, 55.5f}) {
            const MathLib::Vec3 origin{static_cast<float>(x), 10.0f, z};
            const BvhLib::Hit hit{BvhLib::intersect(bvh, origin, MathLib::Vec3{0.0f, -1.0f, 0.0f}, 100.0f)};
            ++tested;
            if (hit.valid && (std::abs(hit.distance - 10.0f) < 1e-3f)) {
                ++hits;
            }
        }
    }

    TEST_CHECK_EQUAL(hits, tested);
}

TEST_CASE(bvh_handles_triangles_whose_centroids_all_coincide)
{
    // No split plane can separate identical centroids, so the builder must give up and make one
    // large leaf rather than recursing until it hits the depth cap.
    std::vector<BvhLib::Triangle> triangles;
    triangles.reserve(4000u);
    for (uint32_t index{0u}; index < 4000u; ++index) {
        triangles.push_back(makeTriangle(MathLib::Vec3{0.0f, 0.0f, 0.0f}, MathLib::Vec3{1.0f, 0.0f, 0.0f}, MathLib::Vec3{0.0f, 1.0f, 0.0f}));
    }

    const BvhLib::Bvh bvh{BvhLib::build(triangles)};

    TEST_CHECK_EQUAL(bvh.nodes.size(), static_cast<size_t>(1u));
    TEST_CHECK(bvh.nodes[0].isLeaf());
    TEST_CHECK_EQUAL(bvh.depth(), 1u);

    const BvhLib::Hit hit{BvhLib::intersect(bvh, MathLib::Vec3{0.2f, 0.2f, 5.0f}, MathLib::Vec3{0.0f, 0.0f, -1.0f}, 100.0f)};
    TEST_CHECK(hit.valid);
    TEST_CHECK(std::abs(hit.distance - 5.0f) < 1e-3f);
}

TEST_CASE(bvh_material_indices_survive_the_reordering)
{
    // The builder permutes the triangle array, so a triangle's material must travel with it.
    std::vector<BvhLib::Triangle> triangles{randomCloud(500u, 8080u)};

    std::vector<uint32_t> expected_material_counts(7u, 0u);
    for (const BvhLib::Triangle& triangle : triangles) {
        ++expected_material_counts[triangle.material];
    }

    const BvhLib::Bvh bvh{BvhLib::build(triangles)};

    std::vector<uint32_t> actual_material_counts(7u, 0u);
    for (const BvhLib::Triangle& triangle : bvh.triangles) {
        TEST_CHECK(triangle.material < 7u);
        ++actual_material_counts[triangle.material];
    }

    TEST_CHECK(expected_material_counts == actual_material_counts);
}

TEST_CASE(scene_with_one_identity_instance_matches_the_single_level_trace)
{
    /*
        The property the renderer depends on while nothing moves: wrapping the Grid in a scene as a
        single instance at the identity must not change one answer. If this drifts, every recorded
        frame drifts with it.
    */
    Rng rng{20260803u};
    BvhLib::Bvh geometry{BvhLib::build(randomCloud(600u, 20260803u))};

    BvhLib::Scene scene{};
    scene.instances.push_back(BvhLib::makeInstance(geometry, 0u, MathLib::Mat4::identity()));
    scene.geometries.push_back(std::move(geometry));

    for (uint32_t index{0u}; index < 2000u; ++index) {
        const MathLib::Vec3 origin{rng.vector(-80.0f, 80.0f)};
        const MathLib::Vec3 direction{rng.vector(-1.0f, 1.0f).normalised()};

        const BvhLib::Hit single{BvhLib::intersect(scene.geometries[0], origin, direction, 400.0f)};
        const BvhLib::Hit scened{BvhLib::intersectScene(scene, origin, direction, 400.0f)};

        TEST_CHECK(single.valid == scened.valid);
        if (single.valid) {
            TEST_CHECK_EQUAL(single.triangle, scened.triangle);
            TEST_CHECK(single.distance == scened.distance);
            TEST_CHECK_EQUAL(scened.instance, 0u);
        }
    }
}

TEST_CASE(an_instance_transform_moves_the_geometry_and_not_the_ray)
{
    /*
        Translating an instance by T must answer exactly as translating the ray by -T against the
        untransformed geometry. Exactly, not approximately: a translation is representable and the
        ray parameter is preserved because the transformed direction is deliberately not normalised.
    */
    Rng rng{7u};
    BvhLib::Bvh geometry{BvhLib::build(randomCloud(400u, 7u))};

    const MathLib::Vec3 offset{12.0f, -3.5f, 7.25f};

    BvhLib::Scene scene{};
    scene.instances.push_back(BvhLib::makeInstance(geometry, 0u, MathLib::Mat4::translate(offset)));
    scene.geometries.push_back(std::move(geometry));

    for (uint32_t index{0u}; index < 1500u; ++index) {
        const MathLib::Vec3 origin{rng.vector(-80.0f, 80.0f)};
        const MathLib::Vec3 direction{rng.vector(-1.0f, 1.0f).normalised()};

        const BvhLib::Hit moved_ray{BvhLib::intersect(scene.geometries[0], origin - offset, direction, 400.0f)};
        const BvhLib::Hit moved_instance{BvhLib::intersectScene(scene, origin, direction, 400.0f)};

        TEST_CHECK(moved_ray.valid == moved_instance.valid);
        if (moved_ray.valid) {
            TEST_CHECK_EQUAL(moved_ray.triangle, moved_instance.triangle);
            TEST_CHECK(moved_ray.distance == moved_instance.distance);
        }
    }
}

TEST_CASE(the_nearest_instance_wins_and_names_itself)
{
    // Two copies of one geometry, one behind the other along the ray. The near one must win, and the
    // hit must say which it was — a traversal that returned the right distance from the wrong
    // instance would transform the normal by the wrong matrix and shade nonsense.
    std::vector<BvhLib::Triangle> quad{makeTriangle(MathLib::Vec3{-1.0f, -1.0f, 0.0f}, MathLib::Vec3{1.0f, -1.0f, 0.0f}, MathLib::Vec3{0.0f, 1.0f, 0.0f})};
    BvhLib::Bvh geometry{BvhLib::build(std::move(quad))};

    BvhLib::Scene scene{};
    scene.instances.push_back(BvhLib::makeInstance(geometry, 0u, MathLib::Mat4::translate(MathLib::Vec3{0.0f, 0.0f, -20.0f})));
    scene.instances.push_back(BvhLib::makeInstance(geometry, 0u, MathLib::Mat4::translate(MathLib::Vec3{0.0f, 0.0f, -5.0f})));
    scene.geometries.push_back(std::move(geometry));

    const BvhLib::Hit hit{BvhLib::intersectScene(scene, MathLib::Vec3{0.0f, 0.0f, 0.0f}, MathLib::Vec3{0.0f, 0.0f, -1.0f}, 100.0f)};

    TEST_CHECK(hit.valid);
    TEST_CHECK_EQUAL(hit.instance, 1u);
    TEST_CHECK(std::fabs(hit.distance - 5.0f) < 1e-4f);

    // And from the other side the far one becomes the near one, so the answer must swap.
    const BvhLib::Hit behind{BvhLib::intersectScene(scene, MathLib::Vec3{0.0f, 0.0f, -40.0f}, MathLib::Vec3{0.0f, 0.0f, 1.0f}, 100.0f)};
    TEST_CHECK(behind.valid);
    TEST_CHECK_EQUAL(behind.instance, 0u);
    TEST_CHECK(std::fabs(behind.distance - 20.0f) < 1e-4f);
}

TEST_CASE(scene_traversal_agrees_with_brute_force_over_transformed_geometry)
{
    /*
        The load-bearing one, and the same shape as the single-level test above it: build a scene of
        rotated and translated instances, then check every ray against a brute-force sweep of the
        triangles transformed into world space by hand. Any disagreement is a bug in the transform,
        the rejection test, or the nearest-hit bookkeeping — and brute force has none of those.
    */
    Rng rng{99u};
    BvhLib::Bvh geometry{BvhLib::build(randomCloud(1500u, 99u))};

    const std::array<MathLib::Mat4, 3> placements{MathLib::Mat4::identity(),
        MathLib::Mat4::translate(MathLib::Vec3{60.0f, 0.0f, 0.0f}) * MathLib::Mat4::rotate(MathLib::Vec3{0.0f, 1.0f, 0.0f}, 0.9f),
        MathLib::Mat4::translate(MathLib::Vec3{-60.0f, 30.0f, 20.0f}) * MathLib::Mat4::rotate(MathLib::Vec3{1.0f, 0.0f, 0.0f}, -1.3f)};

    BvhLib::Scene scene{};
    for (const MathLib::Mat4& placement : placements) {
        scene.instances.push_back(BvhLib::makeInstance(geometry, 0u, placement));
    }
    scene.geometries.push_back(geometry);

    // The same geometry, flattened into world space, for the reference sweep.
    std::vector<BvhLib::Triangle> flattened;
    for (const MathLib::Mat4& placement : placements) {
        for (const BvhLib::Triangle& triangle : scene.geometries[0].triangles) {
            const auto point = [&placement](const MathLib::Vec3& v) {
                const MathLib::Vec4 t{placement * MathLib::Vec4::fromVec3(v, 1.0f)};
                return MathLib::Vec3{t.x, t.y, t.z};
            };
            flattened.push_back(makeTriangle(point(triangle.v0), point(triangle.v1()), point(triangle.v2()), triangle.material));
        }
    }

    uint32_t agreements{0u};
    uint32_t hits{0u};
    constexpr uint32_t RAY_COUNT{3000u};

    for (uint32_t index{0u}; index < RAY_COUNT; ++index) {
        /*
            Aimed at the scene rather than fired at random. Three small clouds scattered across a
            wide span are mostly missed by random directions, and a sweep that misses everything
            agrees with brute force perfectly while proving nothing — which the hit floor below
            caught on the first run of this very test.
        */
        const MathLib::Vec3 origin{rng.vector(-160.0f, 160.0f)};
        const MathLib::Vec3 target{rng.vector(-60.0f, 60.0f)};
        const MathLib::Vec3 direction{(target - origin).normalised()};

        const BvhLib::Hit accelerated{BvhLib::intersectScene(scene, origin, direction, 600.0f)};
        const BvhLib::Hit reference{BvhLib::intersectBruteForce(flattened, origin, direction, 600.0f)};

        if (accelerated.valid == reference.valid) {
            // Distances rather than triangle indices: the flattened array numbers them differently,
            // and two coincident surfaces would make an index comparison fail for no real reason.
            if ((!accelerated.valid) || (std::fabs(accelerated.distance - reference.distance) < 1e-3f)) {
                ++agreements;
            }
        }
        if (reference.valid) {
            ++hits;
        }
    }

    TEST_CHECK_EQUAL(agreements, RAY_COUNT);

    // Prove the comparison had something to compare. A scene the rays all miss agrees perfectly and
    // says nothing, which is a trap this suite has fallen into once already.
    TEST_CHECK(hits > (RAY_COUNT / 20u));
}

/*
    A second traversal, written against the flat form the shader reads.

    This is not production code and deliberately does not live in the library: nothing on the host
    needs it, because the host has `Scene` with its vector of hierarchies and does not have to resolve
    an offset to find anything. `grid_bvh.slang` does, and this is the only way to exercise that
    arithmetic without a GPU.

    It is written from the shader rather than from `intersectScene`, down to the slab test and the
    `1e-30` substitution, so that agreement between the two means the flat layout and the offsetting
    are right — and not merely that the same code was called twice.
*/
namespace
{

    [[nodiscard]] float flatIntersectAabb(const MathLib::Vec3& origin, const MathLib::Vec3& inverse_direction, const MathLib::Vec3& low, const MathLib::Vec3& high,
        float max_distance)
    {
        const auto slab = [](float lo, float hi, float o, float inverse) {
            return std::pair<float, float>{(lo - o) * inverse, (hi - o) * inverse};
        };

        const auto [x1, x2] = slab(low.x, high.x, origin.x, inverse_direction.x);
        const auto [y1, y2] = slab(low.y, high.y, origin.y, inverse_direction.y);
        const auto [z1, z2] = slab(low.z, high.z, origin.z, inverse_direction.z);

        const float tmin{std::max(std::max(std::min(x1, x2), std::min(y1, y2)), std::min(z1, z2))};
        const float tmax{std::min(std::min(std::max(x1, x2), std::max(y1, y2)), std::max(z1, z2))};

        if ((tmax >= std::max(tmin, 0.0f)) && (tmin < max_distance)) {
            return std::max(tmin, 0.0f);
        }
        return std::numeric_limits<float>::infinity();
    }

    void flatIntersectTriangle(const BvhLib::Triangle& triangle, uint32_t index, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance,
        BvhLib::Hit& nearest)
    {
        constexpr float EPSILON{1e-8f};

        const MathLib::Vec3 pvec{direction.cross(triangle.edge2)};
        const float determinant{triangle.edge1.dot(pvec)};
        if (std::fabs(determinant) < EPSILON) {
            return;
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

    //! Walks one geometry inside the shared arrays, as `traceGeometry` does.
    [[nodiscard]] BvhLib::Hit flatTraceGeometry(const BvhLib::FlatScene& flat, uint32_t node_offset, uint32_t triangle_offset, uint32_t node_count,
        const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance)
    {
        BvhLib::Hit nearest{};
        nearest.distance = max_distance;
        if (node_count == 0u) {
            return nearest;
        }

        constexpr float TINY{1e-30f};
        const auto safeInverse = [](float value) {
            return 1.0f / ((value == 0.0f) ? TINY : value);
        };
        const MathLib::Vec3 inverse_direction{safeInverse(direction.x), safeInverse(direction.y), safeInverse(direction.z)};

        std::array<uint32_t, BvhLib::MAX_DEPTH> stack{};
        uint32_t stack_size{0u};
        uint32_t current{0u};

        for (;;) {
            const BvhLib::Node& node{flat.nodes[node_offset + current]};

            if (node.isLeaf()) {
                for (uint32_t offset{0u}; offset < node.triangle_count; ++offset) {
                    const uint32_t index{triangle_offset + node.left_or_first + offset};
                    flatIntersectTriangle(flat.triangles[index], index, origin, direction, max_distance, nearest);
                }
            } else {
                const uint32_t left{node.left_or_first};
                const uint32_t right{left + 1u};

                const float limit{nearest.valid ? nearest.distance : max_distance};
                float left_distance{
                    flatIntersectAabb(origin, inverse_direction, flat.nodes[node_offset + left].bounds_min, flat.nodes[node_offset + left].bounds_max, limit)};
                float right_distance{
                    flatIntersectAabb(origin, inverse_direction, flat.nodes[node_offset + right].bounds_min, flat.nodes[node_offset + right].bounds_max, limit)};

                uint32_t near_child{left};
                uint32_t far_child{right};
                if (right_distance < left_distance) {
                    std::swap(left_distance, right_distance);
                    near_child = right;
                    far_child = left;
                }

                if (!std::isinf(left_distance)) {
                    if ((!std::isinf(right_distance)) && (stack_size < BvhLib::MAX_DEPTH)) {
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

    //! Sweeps the top level, as `traceScene` does.
    [[nodiscard]] BvhLib::Hit flatTraceScene(const BvhLib::FlatScene& flat, const MathLib::Vec3& origin, const MathLib::Vec3& direction, float max_distance)
    {
        BvhLib::Hit nearest{};
        nearest.distance = max_distance;

        constexpr float TINY{1e-30f};
        const auto safeInverse = [](float value) {
            return 1.0f / ((value == 0.0f) ? TINY : value);
        };
        const MathLib::Vec3 inverse_direction{safeInverse(direction.x), safeInverse(direction.y), safeInverse(direction.z)};

        const auto transform = [](const MathLib::Vec4& r0, const MathLib::Vec4& r1, const MathLib::Vec4& r2, const MathLib::Vec3& v, float w) {
            return MathLib::Vec3{(r0.x * v.x) + (r0.y * v.y) + (r0.z * v.z) + (r0.w * w), (r1.x * v.x) + (r1.y * v.y) + (r1.z * v.z) + (r1.w * w),
                (r2.x * v.x) + (r2.y * v.y) + (r2.z * v.z) + (r2.w * w)};
        };

        for (uint32_t index{0u}; index < static_cast<uint32_t>(flat.instances.size()); ++index) {
            const BvhLib::InstanceRecord& record{flat.instances[index]};
            if (record.node_count == 0u) {
                continue;
            }

            const float limit{nearest.valid ? nearest.distance : max_distance};
            if (std::isinf(flatIntersectAabb(origin, inverse_direction, record.bounds_min, record.bounds_max, limit))) {
                continue;
            }

            const MathLib::Vec3 local_origin{transform(record.to_instance_row0, record.to_instance_row1, record.to_instance_row2, origin, 1.0f)};
            const MathLib::Vec3 local_direction{transform(record.to_instance_row0, record.to_instance_row1, record.to_instance_row2, direction, 0.0f)};

            const BvhLib::Hit hit{flatTraceGeometry(flat, record.node_offset, record.triangle_offset, record.node_count, local_origin, local_direction, limit)};

            if (hit.valid && ((!nearest.valid) || (hit.distance < nearest.distance))) {
                nearest = hit;
                nearest.instance = index;
            }
        }

        return nearest;
    }

} // namespace

TEST_CASE(flatten_names_each_geometry_range_and_keeps_instance_order)
{
    const BvhLib::Bvh first{BvhLib::build(randomCloud(300u, 11u))};
    const BvhLib::Bvh second{BvhLib::build(randomCloud(700u, 22u))};

    BvhLib::Scene scene{};
    scene.geometries.push_back(first);
    scene.geometries.push_back(second);
    scene.instances.push_back(BvhLib::makeInstance(second, 1u, MathLib::Mat4::identity()));
    scene.instances.push_back(BvhLib::makeInstance(first, 0u, MathLib::Mat4::translate(MathLib::Vec3{10.0f, 0.0f, 0.0f})));
    scene.instances.push_back(BvhLib::makeInstance(second, 1u, MathLib::Mat4::translate(MathLib::Vec3{0.0f, 10.0f, 0.0f})));

    // An instance naming a geometry that is not there. The shader has no way to skip it other than a
    // node count of zero, so that is what the flat form must produce.
    scene.instances.push_back(BvhLib::makeInstance(first, 7u, MathLib::Mat4::identity()));

    const BvhLib::FlatScene flat{BvhLib::flatten(scene)};

    TEST_CHECK_EQUAL(flat.nodes.size(), first.nodes.size() + second.nodes.size());
    TEST_CHECK_EQUAL(flat.triangles.size(), first.triangles.size() + second.triangles.size());
    TEST_CHECK_EQUAL(flat.instances.size(), scene.instances.size());

    // Geometries are concatenated in their own order, so the first starts at zero and the second
    // starts where the first ended. Three instances share two geometries, which is the sharing the
    // whole two-level arrangement exists for.
    TEST_CHECK_EQUAL(flat.instances[1].node_offset, 0u);
    TEST_CHECK_EQUAL(flat.instances[1].triangle_offset, 0u);
    TEST_CHECK_EQUAL(flat.instances[1].node_count, static_cast<uint32_t>(first.nodes.size()));

    TEST_CHECK_EQUAL(flat.instances[0].node_offset, static_cast<uint32_t>(first.nodes.size()));
    TEST_CHECK_EQUAL(flat.instances[0].triangle_offset, static_cast<uint32_t>(first.triangles.size()));
    TEST_CHECK_EQUAL(flat.instances[0].node_count, static_cast<uint32_t>(second.nodes.size()));

    // Two instances of one geometry point at the same range rather than at two copies of it.
    TEST_CHECK_EQUAL(flat.instances[2].node_offset, flat.instances[0].node_offset);
    TEST_CHECK_EQUAL(flat.instances[2].triangle_offset, flat.instances[0].triangle_offset);

    TEST_CHECK_EQUAL(flat.instances[3].node_count, 0u);

    // The rows are the matrix, read the way the shader reads it. A transposed row here is the exact
    // failure the row form exists to make impossible, so it is worth one comparison.
    const MathLib::Mat4& to_instance{scene.instances[1].to_instance};
    TEST_CHECK(std::fabs(flat.instances[1].to_instance_row0.w - to_instance(3u, 0u)) < 1e-6f);
    TEST_CHECK(std::fabs(flat.instances[1].to_instance_row0.w + 10.0f) < 1e-4f);
}

TEST_CASE(flat_form_traversal_agrees_with_the_scene_it_came_from)
{
    /*
        The one that matters, and the closest a CPU test can come to running the shader: two
        geometries, four placements, and every range but the first at a non-zero offset. Walking the
        flat form must find exactly what walking the Scene finds.

        An offset dropped anywhere in this arithmetic reads one geometry's nodes as if they were
        another's, which produces a plausible picture rather than a crash — the failure this test
        exists to catch before a GPU ever renders it.
    */
    Rng rng{4242u};
    const BvhLib::Bvh first{BvhLib::build(randomCloud(1200u, 31u))};
    const BvhLib::Bvh second{BvhLib::build(randomCloud(1200u, 57u))};

    BvhLib::Scene scene{};
    scene.geometries.push_back(first);
    scene.geometries.push_back(second);
    scene.instances.push_back(BvhLib::makeInstance(first, 0u, MathLib::Mat4::identity()));
    scene.instances.push_back(BvhLib::makeInstance(second, 1u,
        MathLib::Mat4::translate(MathLib::Vec3{25.0f, 0.0f, 0.0f}) * MathLib::Mat4::rotate(MathLib::Vec3{0.0f, 1.0f, 0.0f}, 0.7f)));
    scene.instances.push_back(BvhLib::makeInstance(first, 0u,
        MathLib::Mat4::translate(MathLib::Vec3{-25.0f, 12.0f, 8.0f}) * MathLib::Mat4::rotate(MathLib::Vec3{1.0f, 0.0f, 0.0f}, -1.1f)));
    scene.instances.push_back(BvhLib::makeInstance(second, 1u, MathLib::Mat4::translate(MathLib::Vec3{0.0f, -18.0f, 0.0f})));

    const BvhLib::FlatScene flat{BvhLib::flatten(scene)};

    uint32_t agreements{0u};
    uint32_t hits{0u};
    constexpr uint32_t RAY_COUNT{3000u};

    for (uint32_t index{0u}; index < RAY_COUNT; ++index) {
        // Aimed at the placements rather than fired at random, for the reason the test above it
        // records: a sweep that misses everything agrees perfectly and proves nothing.
        const MathLib::Vec3 origin{rng.vector(-70.0f, 70.0f)};
        const MathLib::Vec3 target{rng.vector(-28.0f, 28.0f)};
        const MathLib::Vec3 direction{(target - origin).normalised()};

        const BvhLib::Hit expected{BvhLib::intersectScene(scene, origin, direction, 400.0f)};
        const BvhLib::Hit actual{flatTraceScene(flat, origin, direction, 400.0f)};

        if (expected.valid == actual.valid) {
            const bool same_place{(!expected.valid) || ((std::fabs(expected.distance - actual.distance) < 1e-4f) && (expected.instance == actual.instance))};
            if (same_place) {
                ++agreements;
            }
        }
        if (expected.valid) {
            ++hits;
        }
    }

    TEST_CHECK_EQUAL(agreements, RAY_COUNT);
    TEST_CHECK(hits > (RAY_COUNT / 20u));
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
