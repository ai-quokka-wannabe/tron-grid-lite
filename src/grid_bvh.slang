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

// Purpose:
//     Walking the Grid's bounding volume hierarchy, shared by every sense that traces it.
//
//     `libs/bvh/include/bvh/bvh.hpp` claims that "the same hierarchy is intended to serve acoustic
//     rays later, which is why nothing here is specific to light", and this module is where that
//     claim is actually kept on the GPU side. Nothing below knows what a colour is. `Node` and
//     `Triangle` carry no optical field, the slab test is a slab test, and the stack walk is a
//     stack walk — an acoustic pass binds the very same two buffers with no rebuild, no second
//     structure and no layout change.
//
//     Two details here were written defensively for light and are load-bearing for sound:
//
//     - **Back faces are accepted.** A creature inside a closed shape must see its inner surface
//       rather than nothing at all — and a creature standing in a terrace hollow must hear its
//       walls.
//     - **Zero direction components are replaced with 1e-30.** The terraced floor is made of
//       horizontal patches, so rays fired straight up or straight down at a grid coordinate are
//       *more* common acoustically than optically, not less.
//
//     The buffers arrive as parameters rather than as module-level bindings, so that each pipeline
//     keeps its own descriptor set layout and this module imposes no binding numbers on anybody.
//
// Host twin:
//     `libs/bvh/src/bvh.cpp` implements the same traversal on the CPU, and
//     `libs/bvh/tests/bvh_tests.cpp` holds the two to each other against a brute-force sweep. A
//     change here that is not mirrored there is a bug in this file, whichever one is "right".

module grid_bvh;

/*!
    Depth of the traversal stack.

    Must equal `BvhLib::MAX_DEPTH` on the host. The builder forces a leaf at that depth precisely
    so that this fixed-size stack can never overflow.
*/
public static const uint MAX_DEPTH = 30;

//! Matches BvhLib::Node exactly: 32 bytes, std430.
public struct Node
{
    public float3 bounds_min;
    public uint left_or_first;
    public float3 bounds_max;
    public uint triangle_count; // Zero marks an interior node.
};

//! Matches BvhLib::Triangle exactly: 48 bytes, std430.
public struct Triangle
{
    public float3 v0;
    public uint material;
    public float3 edge1;
    public uint padding0;
    public float3 edge2;
    public uint padding1;
};

//! What a ray struck, if anything.
public struct Hit
{
    public float distance;
    public uint triangle;
    public bool valid;
};

/*!
    Slab test against an axis-aligned box.

    Returns the distance at which the ray enters the box, or +infinity on a miss. The sentinel is
    deliberately positive: the traversal orders two sibling boxes by comparing these distances, and
    a negative sentinel would sort a missed box in front of a hit one and discard the hit.
*/
public float intersectAabb(float3 origin, float3 inverse_direction, float3 bounds_min, float3 bounds_max, float max_distance)
{
    const float3 t1 = (bounds_min - origin) * inverse_direction;
    const float3 t2 = (bounds_max - origin) * inverse_direction;

    const float3 lower = min(t1, t2);
    const float3 upper = max(t1, t2);

    const float tmin = max(max(lower.x, lower.y), lower.z);
    const float tmax = min(min(upper.x, upper.y), upper.z);

    if ((tmax >= max(tmin, 0.0)) && (tmin < max_distance))
    {
        return max(tmin, 0.0);
    }
    return 1.0 / 0.0;
}

/*!
    Möller-Trumbore triangle intersection.

    Back faces are accepted as well as front faces, so that a creature inside a closed shape sees
    its inner surface rather than nothing at all.
*/
public void intersectTriangle(Triangle triangle, uint index, float3 origin, float3 direction, float max_distance, inout Hit nearest)
{
    const float EPSILON = 1e-8;

    const float3 pvec = cross(direction, triangle.edge2);
    const float determinant = dot(triangle.edge1, pvec);

    if (abs(determinant) < EPSILON)
    {
        return; // The ray runs parallel to the triangle's plane.
    }

    const float inverse_determinant = 1.0 / determinant;
    const float3 tvec = origin - triangle.v0;
    const float u = dot(tvec, pvec) * inverse_determinant;

    if ((u < 0.0) || (u > 1.0))
    {
        return;
    }

    const float3 qvec = cross(tvec, triangle.edge1);
    const float v = dot(direction, qvec) * inverse_determinant;

    if ((v < 0.0) || ((u + v) > 1.0))
    {
        return;
    }

    const float distance = dot(triangle.edge2, qvec) * inverse_determinant;

    if ((distance <= EPSILON) || (distance >= max_distance))
    {
        return;
    }

    if (nearest.valid && (distance >= nearest.distance))
    {
        return;
    }

    nearest.distance = distance;
    nearest.triangle = index;
    nearest.valid = true;
}

/*!
    Walks the hierarchy and returns the nearest hit.

    \param nodes Hierarchy nodes; element 0 is the root.
    \param triangles Triangles in leaf order.
    \param node_count Zero means an empty Grid, and every ray misses.
    \param origin Ray origin, world space.
    \param direction Ray direction. Need not be normalised, but `Hit::distance` is measured in
           units of its length, so every caller in this repository passes a unit vector.
    \param max_distance Distance beyond which a hit does not count.
    \return The nearest hit, or a `Hit` with `valid` false.
*/
public Hit trace(StructuredBuffer<Node> nodes, StructuredBuffer<Triangle> triangles, uint node_count, float3 origin, float3 direction, float max_distance)
{
    Hit nearest;
    nearest.distance = max_distance;
    nearest.triangle = 0;
    nearest.valid = false;

    if (node_count == 0)
    {
        return nearest;
    }

    // A zero direction component would make its reciprocal infinite, and an origin lying exactly on
    // that slab's plane then computes 0 * inf, which is NaN — and a NaN propagating through the min
    // and max reductions turns a box the ray passes through into a miss. The Grid's floor is
    // axis-aligned on a two-metre grid, so this is an everyday case rather than a corner one.
    // Substituting a tiny magnitude keeps every reciprocal finite. The host does the same.
    const float3 safe_direction = select(direction == 0.0, float3(1e-30, 1e-30, 1e-30), direction);
    const float3 inverse_direction = 1.0 / safe_direction;

    uint stack[MAX_DEPTH];
    uint stack_size = 0;
    uint current = 0;

    for (;;)
    {
        const Node node = nodes[current];

        if (node.triangle_count != 0)
        {
            for (uint offset = 0; offset < node.triangle_count; ++offset)
            {
                const uint index = node.left_or_first + offset;
                intersectTriangle(triangles[index], index, origin, direction, max_distance, nearest);
            }
        }
        else
        {
            const uint left = node.left_or_first;
            const uint right = left + 1;

            const float limit = nearest.valid ? nearest.distance : max_distance;
            float left_distance = intersectAabb(origin, inverse_direction, nodes[left].bounds_min, nodes[left].bounds_max, limit);
            float right_distance = intersectAabb(origin, inverse_direction, nodes[right].bounds_min, nodes[right].bounds_max, limit);

            uint near_child = left;
            uint far_child = right;
            if (right_distance < left_distance)
            {
                const float swap_distance = left_distance;
                left_distance = right_distance;
                right_distance = swap_distance;

                near_child = right;
                far_child = left;
            }

            // After the swap the nearer box is in left_distance, so a finite value there means at
            // least one child was hit and the nearer one is where to go next.
            if (!isinf(left_distance))
            {
                if (!isinf(right_distance) && (stack_size < MAX_DEPTH))
                {
                    stack[stack_size] = far_child;
                    ++stack_size;
                }
                current = near_child;
                continue;
            }
        }

        if (stack_size == 0)
        {
            break;
        }
        --stack_size;
        current = stack[stack_size];
    }

    return nearest;
}
