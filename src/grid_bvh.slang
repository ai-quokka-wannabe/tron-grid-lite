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

/*!
    One placement of a geometry, exactly as `BvhLib::InstanceRecord` lays it out: 144 bytes, std430.

    The transforms arrive as three rows rather than as a `float4x4`, and the reason is narrower than
    it first looks — **a `float4x4` was tried, and it works.** Declaring the record with two of them
    and reading them with `mul(matrix, vector)` agrees with `MathLib::Mat4`'s column-major storage
    exactly, giving numbers identical to these to every digit `--verify-scene` prints, at the identity
    and at an angle alike. So this is not a workaround for a broken toolchain.

    What it avoids is a dependency on a compiler *option*. Slang's matrix layout is a flag, the
    agreement above holds for its default, and the mistake it would produce is a transpose — which
    for a rotation is the inverse, so the geometry would still look like geometry while being wrong.
    Three rows have exactly one interpretation and cannot express that mistake.

    That would be a thin argument if a check caught the flip. One does — `--verify-scene` — but it
    needs a GPU, so it never runs in CI and fires only when somebody remembers it on a machine with a
    device attached. Given a choice between a layout that cannot go wrong and a layout whose check is
    manual, this repository takes the first, the same way it prefers a `static_assert` to a comment.

    `transformPoint` multiplies the rows out by hand, and the bottom row of an affine transform is
    always (0, 0, 0, 1), so omitting it loses nothing and saves 32 bytes an instance.
*/
public struct Instance
{
    public float4 to_instance_row0;
    public float4 to_instance_row1;
    public float4 to_instance_row2;
    public float4 to_world_row0;
    public float4 to_world_row1;
    public float4 to_world_row2;

    public float3 bounds_min;
    public uint node_offset;
    public float3 bounds_max;
    public uint triangle_offset;

    public uint node_count; // Zero means this instance has no geometry and is skipped.
    public uint padding0;
    public uint padding1;
    public uint padding2;
};

//! What a ray struck, if anything.
public struct Hit
{
    public float distance;
    public uint triangle;
    public uint instance; //!< Which placement was struck. Always zero from `trace`, which has only one.
    public bool valid;
};

//! Applies an affine transform stored as three rows to a position.
public float3 transformPoint(float4 row0, float4 row1, float4 row2, float3 position)
{
    return float3(dot(row0.xyz, position) + row0.w, dot(row1.xyz, position) + row1.w, dot(row2.xyz, position) + row2.w);
}

/*!
    Applies an affine transform stored as three rows to a direction, ignoring the translation.

    **The result is deliberately not normalised**, and every caller must leave it that way. Scaling
    the direction by whatever the transform does to lengths is what keeps the ray parameter identical
    in both frames, so a distance found in instance space is already a world distance and a limit can
    be passed straight through. Normalising it here would look tidy and would silently mis-scale
    every hit from a scaled instance. The host says the same thing in `BvhLib::intersectScene`.
*/
public float3 transformDirection(float4 row0, float4 row1, float4 row2, float3 direction)
{
    return float3(dot(row0.xyz, direction), dot(row1.xyz, direction), dot(row2.xyz, direction));
}

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
    Walks one geometry's hierarchy inside a shared buffer and returns the nearest hit.

    The offsets exist because a shader cannot hold a buffer of buffers. Every geometry's nodes and
    triangles are concatenated into one pair of buffers by `BvhLib::flatten`, and an instance names
    its own range by where it starts. A single-level trace is the case where both offsets are zero,
    which is what `trace` passes, so the two paths are the same code rather than two copies of it.

    \param nodes Node buffer holding every geometry; this one's root is at `node_offset`.
    \param triangles Triangle buffer holding every geometry, in the same order.
    \param node_offset Where this geometry's nodes begin.
    \param triangle_offset Where this geometry's triangles begin.
    \param node_count Zero means no geometry at all, and every ray misses.
    \param origin Ray origin, in this geometry's own frame.
    \param direction Ray direction, in this geometry's own frame. Need not be normalised, and
           `Hit::distance` is measured in units of its length.
    \param max_distance Distance beyond which a hit does not count.
    \return The nearest hit with `triangle` indexing the shared buffer, or a `Hit` with `valid` false.
*/
public Hit traceGeometry(StructuredBuffer<Node> nodes, StructuredBuffer<Triangle> triangles, uint node_offset, uint triangle_offset, uint node_count,
    float3 origin, float3 direction, float max_distance)
{
    Hit nearest;
    nearest.distance = max_distance;
    nearest.triangle = 0;
    nearest.instance = 0;
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
        // Node and triangle indices inside a hierarchy are relative to the geometry that owns it, so
        // every one of them is offset on the way to the shared buffer and nowhere else. Getting that
        // wrong reads another geometry's nodes as if they were this one's, which is a plausible
        // picture rather than a crash — see the CPU test that walks the flat form for the same reason.
        const Node node = nodes[node_offset + current];

        if (node.triangle_count != 0)
        {
            for (uint offset = 0; offset < node.triangle_count; ++offset)
            {
                const uint index = triangle_offset + node.left_or_first + offset;
                intersectTriangle(triangles[index], index, origin, direction, max_distance, nearest);
            }
        }
        else
        {
            const uint left = node.left_or_first;
            const uint right = left + 1;

            const float limit = nearest.valid ? nearest.distance : max_distance;
            float left_distance = intersectAabb(origin, inverse_direction, nodes[node_offset + left].bounds_min, nodes[node_offset + left].bounds_max, limit);
            float right_distance = intersectAabb(origin, inverse_direction, nodes[node_offset + right].bounds_min, nodes[node_offset + right].bounds_max, limit);

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

/*!
    Walks a single hierarchy occupying the whole of both buffers.

    The one-level path, kept because most rays in this repository still take it: one Grid, no
    instances, no transform. It is a call to `traceGeometry` at offset zero rather than a second
    traversal, so there is exactly one stack walk in this module and no way for the two to drift.

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
    return traceGeometry(nodes, triangles, 0, 0, node_count, origin, direction, max_distance);
}

/*!
    Walks the whole scene and returns the nearest hit across every instance.

    The device twin of `BvhLib::intersectScene`, and held to it: the host is the specification, this
    is the mirror, and a difference between them is a bug here whichever one looks more reasonable.

    The top level is a linear sweep over instance boxes rather than a hierarchy over them, for the
    same reason as on the host — a handful of creatures plus the Grid is twenty-odd boxes, and a slab
    test is a few instructions. The measurement that says when that stops being true is in
    `docs/ARCHITECTURE.md` rather than in a guess here.

    The ray goes into each instance's frame **without being normalised**, so the distance that comes
    back is already a world distance and the running limit can be passed straight down. See
    `transformDirection`.

    \param instances Placements, in the order `BvhLib::flatten` emitted them.
    \param instance_count Number of placements. Zero means every ray misses.
    \param nodes Node buffer holding every geometry.
    \param triangles Triangle buffer holding every geometry.
    \param origin Ray origin, world space.
    \param direction Ray direction, world space.
    \param max_distance Distance beyond which a hit does not count.
    \return The nearest hit, with `instance` naming the placement and `triangle` indexing the shared
            triangle buffer. Its vertices are in that instance's own frame, not the world's.
*/
public Hit traceScene(StructuredBuffer<Instance> instances, uint instance_count, StructuredBuffer<Node> nodes, StructuredBuffer<Triangle> triangles,
    float3 origin, float3 direction, float max_distance)
{
    Hit nearest;
    nearest.distance = max_distance;
    nearest.triangle = 0;
    nearest.instance = 0;
    nearest.valid = false;

    // Same substitution as the inner traversal, and needed for the same reason: the Grid's instance
    // box is axis-aligned, so an axis-aligned ray is an everyday case rather than a corner one.
    const float3 safe_direction = select(direction == 0.0, float3(1e-30, 1e-30, 1e-30), direction);
    const float3 inverse_direction = 1.0 / safe_direction;

    for (uint index = 0; index < instance_count; ++index)
    {
        const Instance instance = instances[index];

        if (instance.node_count == 0)
        {
            continue; // No geometry behind this placement. Silent rather than an error, as on the host.
        }

        // Shrinking as hits are found, so a box entirely behind the nearest hit so far is rejected
        // without descending into it. The instances are in scene order rather than sorted, which
        // makes this weaker than it could be and is the first thing to change if it ever matters.
        const float limit = nearest.valid ? nearest.distance : max_distance;

        if (isinf(intersectAabb(origin, inverse_direction, instance.bounds_min, instance.bounds_max, limit)))
        {
            continue;
        }

        const float3 local_origin = transformPoint(instance.to_instance_row0, instance.to_instance_row1, instance.to_instance_row2, origin);
        const float3 local_direction = transformDirection(instance.to_instance_row0, instance.to_instance_row1, instance.to_instance_row2, direction);

        const Hit hit = traceGeometry(nodes, triangles, instance.node_offset, instance.triangle_offset, instance.node_count, local_origin, local_direction, limit);

        if (hit.valid && ((!nearest.valid) || (hit.distance < nearest.distance)))
        {
            nearest = hit;
            nearest.instance = index;
        }
    }

    return nearest;
}
