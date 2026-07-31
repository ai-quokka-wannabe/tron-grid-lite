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
#include <cmath>
#include <cstdint>
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
    // Creature eyes sit inside the world rather than looking at it from outside, so the case of an
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
        The regression that matters most for this world.

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

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
