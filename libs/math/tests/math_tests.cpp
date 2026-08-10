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

#include "testing/testing.hpp"
#include <math/matrix.hpp>
#include <math/quaternion.hpp>
#include <math/vector.hpp>
#include <cmath>

//! Approximate equality for floating-point comparisons.
static bool approx(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) < epsilon;
}

static bool approxVec3(const MathLib::Vec3& a, const MathLib::Vec3& b, float epsilon = 1e-5f)
{
    return approx(a.x, b.x, epsilon) && approx(a.y, b.y, epsilon) && approx(a.z, b.z, epsilon);
}

static bool approxVec4(const MathLib::Vec4& a, const MathLib::Vec4& b, float epsilon = 1e-5f)
{
    return approx(a.x, b.x, epsilon) && approx(a.y, b.y, epsilon) && approx(a.z, b.z, epsilon) && approx(a.w, b.w, epsilon);
}

static bool approxMat4(const MathLib::Mat4& a, const MathLib::Mat4& b, float epsilon = 1e-5f)
{
    for (uint32_t col{0}; col < 4; ++col) {
        for (uint32_t row{0}; row < 4; ++row) {
            if (!approx(a(col, row), b(col, row), epsilon)) {
                return false;
            }
        }
    }
    return true;
}

// ── Vec3 ──

TEST_CASE(vec3_default_zero)
{
    MathLib::Vec3 v;
    TEST_CHECK((v.x == 0.0f) && (v.y == 0.0f) && (v.z == 0.0f));
}

TEST_CASE(vec3_dot)
{
    MathLib::Vec3 a{1.0f, 2.0f, 3.0f};
    MathLib::Vec3 b{4.0f, 5.0f, 6.0f};
    TEST_CHECK_CLOSE(a.dot(b), 32.0f, 1e-5f);
}

TEST_CASE(vec3_cross)
{
    MathLib::Vec3 x{1.0f, 0.0f, 0.0f};
    MathLib::Vec3 y{0.0f, 1.0f, 0.0f};
    MathLib::Vec3 z{x.cross(y)};
    TEST_CHECK(approxVec3(z, {0.0f, 0.0f, 1.0f}));
}

TEST_CASE(vec3_cross_anticommutative)
{
    MathLib::Vec3 a{1.0f, 2.0f, 3.0f};
    MathLib::Vec3 b{4.0f, 5.0f, 6.0f};
    MathLib::Vec3 ab{a.cross(b)};
    MathLib::Vec3 ba{b.cross(a)};
    TEST_CHECK(approxVec3(ab, -ba));
}

TEST_CASE(vec3_length)
{
    MathLib::Vec3 v{1.0f, 2.0f, 2.0f};
    TEST_CHECK_CLOSE(v.length(), 3.0f, 1e-5f);
}

TEST_CASE(vec3_normalised)
{
    MathLib::Vec3 v{0.0f, 3.0f, 4.0f};
    MathLib::Vec3 n{v.normalised()};
    TEST_CHECK_CLOSE(n.length(), 1.0f, 1e-5f);
    TEST_CHECK_CLOSE(n.x, 0.0f, 1e-5f);
}

TEST_CASE(vec3_normalised_zero)
{
    MathLib::Vec3 v{0.0f, 0.0f, 0.0f};
    MathLib::Vec3 n{v.normalised()};
    TEST_CHECK(n.x == 0.0f && n.y == 0.0f && n.z == 0.0f);
}

TEST_CASE(vec3_arithmetic)
{
    MathLib::Vec3 a{1.0f, 2.0f, 3.0f};
    MathLib::Vec3 b{4.0f, 5.0f, 6.0f};
    TEST_CHECK(((a + b) == MathLib::Vec3{5.0f, 7.0f, 9.0f}));
    TEST_CHECK(((a - b) == MathLib::Vec3{-3.0f, -3.0f, -3.0f}));
    TEST_CHECK(((a * 2.0f) == MathLib::Vec3{2.0f, 4.0f, 6.0f}));
    TEST_CHECK(((-a) == MathLib::Vec3{-1.0f, -2.0f, -3.0f}));
}

TEST_CASE(vec3_compound_assignment)
{
    MathLib::Vec3 v{1.0f, 2.0f, 3.0f};
    v += MathLib::Vec3{4.0f, 5.0f, 6.0f};
    TEST_CHECK((v == MathLib::Vec3{5.0f, 7.0f, 9.0f}));
    v -= MathLib::Vec3{1.0f, 1.0f, 1.0f};
    TEST_CHECK((v == MathLib::Vec3{4.0f, 6.0f, 8.0f}));
    v *= 0.5f;
    TEST_CHECK((v == MathLib::Vec3{2.0f, 3.0f, 4.0f}));
}

// ── Vec4 ──

TEST_CASE(vec4_default_zero)
{
    MathLib::Vec4 v;
    TEST_CHECK((v.x == 0.0f) && (v.y == 0.0f) && (v.z == 0.0f) && (v.w == 0.0f));
}

TEST_CASE(vec4_from_vec3)
{
    MathLib::Vec3 v{1.0f, 2.0f, 3.0f};
    MathLib::Vec4 v4{MathLib::Vec4::fromVec3(v, 1.0f)};
    TEST_CHECK((v4 == MathLib::Vec4{1.0f, 2.0f, 3.0f, 1.0f}));
}

TEST_CASE(vec4_xyz)
{
    MathLib::Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    TEST_CHECK((v.xyz() == MathLib::Vec3{1.0f, 2.0f, 3.0f}));
}

TEST_CASE(vec4_dot)
{
    MathLib::Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    MathLib::Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
    TEST_CHECK_CLOSE(a.dot(b), 70.0f, 1e-5f);
}

TEST_CASE(vec4_arithmetic)
{
    MathLib::Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    MathLib::Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
    TEST_CHECK(((a + b) == MathLib::Vec4{6.0f, 8.0f, 10.0f, 12.0f}));
    TEST_CHECK(((a - b) == MathLib::Vec4{-4.0f, -4.0f, -4.0f, -4.0f}));
    TEST_CHECK(((a * 2.0f) == MathLib::Vec4{2.0f, 4.0f, 6.0f, 8.0f}));
    TEST_CHECK(((-a) == MathLib::Vec4{-1.0f, -2.0f, -3.0f, -4.0f}));
}

// ── Mat4 ──

TEST_CASE(mat4_identity)
{
    MathLib::Mat4 id{MathLib::Mat4::identity()};
    MathLib::Vec4 v{1.0f, 2.0f, 3.0f, 1.0f};
    TEST_CHECK(approxVec4(id * v, v));
}

TEST_CASE(mat4_identity_multiply)
{
    MathLib::Mat4 id{MathLib::Mat4::identity()};
    MathLib::Mat4 result{id * id};
    TEST_CHECK(approxMat4(result, id));
}

TEST_CASE(mat4_translate)
{
    MathLib::Mat4 t{MathLib::Mat4::translate({1.0f, 2.0f, 3.0f})};
    MathLib::Vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result{t * origin};
    TEST_CHECK(approxVec4(result, {1.0f, 2.0f, 3.0f, 1.0f}));
}

TEST_CASE(mat4_translate_direction_unaffected)
{
    MathLib::Mat4 t{MathLib::Mat4::translate({10.0f, 20.0f, 30.0f})};
    MathLib::Vec4 dir{1.0f, 0.0f, 0.0f, 0.0f}; // w=0 is a direction
    MathLib::Vec4 result{t * dir};
    TEST_CHECK(approxVec4(result, {1.0f, 0.0f, 0.0f, 0.0f}));
}

TEST_CASE(mat4_scale)
{
    MathLib::Mat4 s{MathLib::Mat4::scale({2.0f, 3.0f, 4.0f})};
    MathLib::Vec4 v{1.0f, 1.0f, 1.0f, 1.0f};
    MathLib::Vec4 result{s * v};
    TEST_CHECK(approxVec4(result, {2.0f, 3.0f, 4.0f, 1.0f}));
}

TEST_CASE(mat4_rotate_z_90)
{
    MathLib::Mat4 r{MathLib::Mat4::rotate({0.0f, 0.0f, 1.0f}, MathLib::PI / 2.0f)};
    MathLib::Vec4 v{1.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result{r * v};
    TEST_CHECK(approxVec4(result, {0.0f, 1.0f, 0.0f, 1.0f}, 1e-4f));
}

TEST_CASE(mat4_rotate_x_90)
{
    MathLib::Mat4 r{MathLib::Mat4::rotate({1.0f, 0.0f, 0.0f}, MathLib::PI / 2.0f)};
    MathLib::Vec4 v{0.0f, 1.0f, 0.0f, 1.0f};
    MathLib::Vec4 result{r * v};
    TEST_CHECK(approxVec4(result, {0.0f, 0.0f, 1.0f, 1.0f}, 1e-4f));
}

TEST_CASE(mat4_rotate_y_90)
{
    MathLib::Mat4 r{MathLib::Mat4::rotate({0.0f, 1.0f, 0.0f}, MathLib::PI / 2.0f)};
    MathLib::Vec4 v{1.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result{r * v};
    TEST_CHECK(approxVec4(result, {0.0f, 0.0f, -1.0f, 1.0f}, 1e-4f));
}

TEST_CASE(mat4_rotate_360_identity)
{
    MathLib::Mat4 r{MathLib::Mat4::rotate({0.0f, 1.0f, 0.0f}, 2.0f * MathLib::PI)};
    TEST_CHECK(approxMat4(r, MathLib::Mat4::identity(), 1e-4f));
}

TEST_CASE(mat4_multiply_associative)
{
    MathLib::Mat4 a{MathLib::Mat4::translate({1.0f, 0.0f, 0.0f})};
    MathLib::Mat4 b{MathLib::Mat4::scale({2.0f, 2.0f, 2.0f})};
    MathLib::Mat4 c{MathLib::Mat4::translate({0.0f, 0.0f, 3.0f})};
    MathLib::Mat4 ab_c{(a * b) * c};
    MathLib::Mat4 a_bc{a * (b * c)};
    TEST_CHECK(approxMat4(ab_c, a_bc));
}

/*
    Mat4::inversed() is sixteen hand-transcribed cofactor expressions called by production code
    (libs/bvh/src/bvh.cpp caches it as every instance's world-to-geometry transform), and the cases
    below are the whole of its coverage. A sign or index error in any one of them produces a
    silently wrong inverse.
*/
TEST_CASE(mat4_inversed_round_trips)
{
    const MathLib::Mat4 composite{
        MathLib::Mat4::translate({3.0f, -4.0f, 5.0f}) * MathLib::Mat4::rotate({0.3f, 0.9f, 0.2f}, 0.7f) * MathLib::Mat4::scale({2.0f, 0.5f, 3.0f})};

    const MathLib::Mat4 identity{composite * composite.inversed()};

    for (uint32_t row{0}; row < 4; ++row) {
        for (uint32_t column{0}; column < 4; ++column) {
            const float expected{(row == column) ? 1.0f : 0.0f};
            TEST_CHECK_CLOSE(identity(row, column), expected, 1e-4f);
        }
    }
}

//! A point taken through a transform and back must arrive where it started.
TEST_CASE(mat4_inversed_undoes_a_transform)
{
    const MathLib::Mat4 composite{
        MathLib::Mat4::translate({-7.0f, 2.5f, 11.0f}) * MathLib::Mat4::rotate({0.0f, 1.0f, 0.0f}, MathLib::PI / 3.0f) * MathLib::Mat4::scale({1.5f, 1.5f, 1.5f})};
    const MathLib::Mat4 inverse{composite.inversed()};

    for (const MathLib::Vec3& point : {MathLib::Vec3{1.0f, 2.0f, 3.0f}, MathLib::Vec3{-4.0f, 0.5f, 6.0f}, MathLib::Vec3{0.0f, 0.0f, 0.0f}}) {
        const MathLib::Vec4 transformed{composite * MathLib::Vec4{point.x, point.y, point.z, 1.0f}};
        const MathLib::Vec4 restored{inverse * transformed};
        TEST_CHECK(approxVec3({restored.x, restored.y, restored.z}, point, 1e-3f));
    }
}

//! A singular matrix has no inverse, and pretending otherwise would place geometry wrongly with no
//! diagnostic — the failure must be loud.
TEST_CASE(mat4_inversed_throws_on_singular)
{
    const MathLib::Mat4 flattened{MathLib::Mat4::scale({1.0f, 0.0f, 1.0f})};
    TEST_CHECK_THROWS(flattened.inversed());
}

// ── Quat ──

TEST_CASE(quat_identity)
{
    MathLib::Quat q{MathLib::Quat::identity()};
    TEST_CHECK((q.w == 1.0f) && (q.x == 0.0f) && (q.y == 0.0f) && (q.z == 0.0f));
}

TEST_CASE(quat_from_axis_angle_identity)
{
    MathLib::Quat q{MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.0f)};
    TEST_CHECK_CLOSE(q.w, 1.0f, 1e-5f);
    TEST_CHECK_CLOSE(q.x, 0.0f, 1e-5f);
    TEST_CHECK_CLOSE(q.y, 0.0f, 1e-5f);
    TEST_CHECK_CLOSE(q.z, 0.0f, 1e-5f);
}

TEST_CASE(quat_rotate_vec3)
{
    MathLib::Quat q{MathLib::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, MathLib::PI / 2.0f)};
    MathLib::Vec3 v{1.0f, 0.0f, 0.0f};
    MathLib::Vec3 result{q.rotate(v)};
    TEST_CHECK(approxVec3(result, {0.0f, 1.0f, 0.0f}, 1e-4f));
}

TEST_CASE(quat_rotate_around_x)
{
    MathLib::Quat q{MathLib::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, MathLib::PI / 2.0f)};
    MathLib::Vec3 v{0.0f, 1.0f, 0.0f};
    MathLib::Vec3 result{q.rotate(v)};
    TEST_CHECK(approxVec3(result, {0.0f, 0.0f, 1.0f}, 1e-4f));
}

TEST_CASE(quat_rotate_around_y)
{
    MathLib::Quat q{MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, MathLib::PI / 2.0f)};
    MathLib::Vec3 v{1.0f, 0.0f, 0.0f};
    MathLib::Vec3 result{q.rotate(v)};
    TEST_CHECK(approxVec3(result, {0.0f, 0.0f, -1.0f}, 1e-4f));
}

TEST_CASE(quat_multiply_combines_rotations)
{
    MathLib::Quat q1{MathLib::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, MathLib::PI / 2.0f)};
    MathLib::Quat q2{MathLib::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, MathLib::PI / 2.0f)};
    MathLib::Quat combined{q1 * q2}; // 180 degrees around Z
    MathLib::Vec3 v{1.0f, 0.0f, 0.0f};
    MathLib::Vec3 result{combined.rotate(v)};
    TEST_CHECK(approxVec3(result, {-1.0f, 0.0f, 0.0f}, 1e-4f));
}

/*
    The test above cannot detect a swapped composition order, because it multiplies two identical
    rotations about the same axis and those commute. It is the only other use of the quaternion
    product in this file, and `Quat::rotate` is implemented directly rather than through
    `operator*`, so without the case below a conjugated Hamilton product would pass the whole suite.

    These two rotations do not commute. Taken in the order written, the result is (0, 1, 0); reversed
    it is (-1, 0, 0), so the assertion distinguishes them.
*/
TEST_CASE(quat_multiply_does_not_commute)
{
    const MathLib::Quat yaw{MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, MathLib::PI / 2.0f)};
    const MathLib::Quat pitch{MathLib::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, MathLib::PI / 2.0f)};

    const MathLib::Vec3 forward{0.0f, 0.0f, -1.0f};
    TEST_CHECK(approxVec3((yaw * pitch).rotate(forward), {0.0f, 1.0f, 0.0f}, 1e-4f));
    TEST_CHECK(approxVec3((pitch * yaw).rotate(forward), {-1.0f, 0.0f, 0.0f}, 1e-4f));
}

TEST_CASE(quat_length)
{
    MathLib::Quat q{1.0f, 2.0f, 3.0f, 4.0f};
    float expected{std::sqrt(1.0f + 4.0f + 9.0f + 16.0f)};
    TEST_CHECK_CLOSE(q.length(), expected, 1e-5f);
}

TEST_CASE(quat_normalised)
{
    MathLib::Quat q{2.0f, 0.0f, 0.0f, 0.0f};
    MathLib::Quat n{q.normalised()};
    TEST_CHECK_CLOSE(n.length(), 1.0f, 1e-5f);
    TEST_CHECK_CLOSE(n.w, 1.0f, 1e-5f);
}

TEST_CASE(quat_normalised_zero)
{
    MathLib::Quat q{0.0f, 0.0f, 0.0f, 0.0f};
    MathLib::Quat n{q.normalised()};
    TEST_CHECK(n == MathLib::Quat::identity());
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
