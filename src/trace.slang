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
//     The renderer. Traces the world by walking a hand-built bounding volume hierarchy in an
//     ordinary compute shader, with no ray-tracing hardware of any kind. Every surface is
//     perfectly smooth, so each intersection spawns exactly one reflected ray: the ray tree is
//     deterministic and shallow, there is no Monte Carlo integration, no variance and therefore
//     no denoiser anywhere in this project.
//
//     The same kernel will serve creature sensors, at a far smaller resolution, once bodies
//     exist. Nothing here is specific to the spectator window except the size of the image.
//
// Entry points:
//     traceMain — [shader("compute")], [numthreads(8, 8, 1)] — one thread per output pixel.
//
// Descriptor bindings (set 0):
//     binding 0 : RWTexture2D<float4>       output_image — format rgba16f, linear radiance, written once per thread.
//     binding 1 : StructuredBuffer<Node>     nodes       — 32 bytes each, element 0 is the root.
//     binding 2 : StructuredBuffer<Triangle> triangles   — 48 bytes each, in leaf order.
//     binding 3 : StructuredBuffer<Material> materials   — 32 bytes each, indexed per triangle.
//
// Push constants (80 bytes):
//     offset  0, size 16 : camera_position   — xyz world space, w unused.
//     offset 16, size 16 : top_left          — xyz direction through the centre of pixel (0, 0).
//     offset 32, size 16 : pixel_delta_u     — xyz direction change per pixel step in x.
//     offset 48, size 16 : pixel_delta_v     — xyz direction change per pixel step in y.
//     offset 64, size  8 : resolution        — output image size in pixels.
//     offset 72, size  4 : max_bounces       — total ray segments per pixel, at least 1.
//     offset 76, size  4 : node_count        — zero means an empty world; the kernel writes black.
//
//     The tracer writes linear radiance and nothing else: exposure, tone mapping and sRGB encoding
//     all belong to postprocess.slang, which sees the bloom chain as well.

// Must equal BvhLib::MAX_DEPTH on the host. The builder forces a leaf at that depth precisely so
// that this fixed-size stack can never overflow.
static const uint MAX_DEPTH = 30;

//! Matches BvhLib::Node exactly: 32 bytes, std430.
struct Node
{
    float3 bounds_min;
    uint left_or_first;
    float3 bounds_max;
    uint triangle_count; // Zero marks an interior node.
};

//! Matches BvhLib::Triangle exactly: 48 bytes, std430.
struct Triangle
{
    float3 v0;
    uint material;
    float3 edge1;
    uint padding0;
    float3 edge2;
    uint padding1;
};

//! Matches the host Material exactly: 32 bytes, std430. The host asserts the same offsets.
struct Material
{
    float3 colour;
    float index_of_refraction;
    float3 emission;
    float transmission;
};

struct TracePushConstants
{
    float4 camera_position;
    float4 top_left;
    float4 pixel_delta_u;
    float4 pixel_delta_v;
    uint2 resolution;
    uint max_bounces;
    uint node_count;
};

[[vk::push_constant]]
TracePushConstants push;

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> output_image;

[[vk::binding(1, 0)]]
StructuredBuffer<Node> nodes;

[[vk::binding(2, 0)]]
StructuredBuffer<Triangle> triangles;

[[vk::binding(3, 0)]]
StructuredBuffer<Material> materials;

//! What a ray struck, if anything.
struct Hit
{
    float distance;
    uint triangle;
    bool valid;
};

/*!
    Slab test against an axis-aligned box.

    Returns the distance at which the ray enters the box, or +infinity on a miss. The sentinel is
    deliberately positive: the traversal orders two sibling boxes by comparing these distances, and
    a negative sentinel would sort a missed box in front of a hit one and discard the hit.
*/
float intersectAabb(float3 origin, float3 inverse_direction, float3 bounds_min, float3 bounds_max, float max_distance)
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
void intersectTriangle(Triangle triangle, uint index, float3 origin, float3 direction, float max_distance, inout Hit nearest)
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

//! Walks the hierarchy and returns the nearest hit. The host-side twin of this lives in libs/bvh.
Hit trace(float3 origin, float3 direction, float max_distance)
{
    Hit nearest;
    nearest.distance = max_distance;
    nearest.triangle = 0;
    nearest.valid = false;

    if (push.node_count == 0)
    {
        return nearest;
    }

    // A zero direction component would make its reciprocal infinite, and an origin lying exactly on
    // that slab's plane then computes 0 * inf, which is NaN — and a NaN propagating through the min
    // and max reductions turns a box the ray passes through into a miss. The world's floor is
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

/*!
    Schlick's approximation to the Fresnel term.

    This is the whole lighting model. A perfectly smooth surface reflects a share of the light that
    depends only on the viewing angle and the index of refraction: about four per cent head-on for
    ordinary glass, rising to everything at all at a grazing angle. That rise is what produces the
    long bright streaks along a mirrored floor, and it is why this world's floor reads as almost
    black underfoot and blazing towards the horizon.
*/
float fresnelSchlick(float cosine, float index_of_refraction)
{
    const float f0_root = (index_of_refraction - 1.0) / (index_of_refraction + 1.0);
    const float f0 = f0_root * f0_root;
    const float one_minus_cosine = saturate(1.0 - cosine);
    const float squared = one_minus_cosine * one_minus_cosine;
    return f0 + ((1.0 - f0) * squared * squared * one_minus_cosine);
}

//! One ray still owed a trace, along with what its contribution is worth.
struct RayTask
{
    float3 origin;
    float3 direction;
    float3 throughput;
    uint depth;
};

/*!
    Depth of the pending-ray stack.

    A surface that both reflects and transmits splits the ray in two. One branch is followed
    immediately and the other waits here, so this bounds how many splits may be outstanding at
    once rather than how deep the tree goes. A full stack drops the ray it cannot hold, which
    loses a little energy deterministically rather than corrupting anything.

    Three is a measured choice rather than a guess. Every thread allocates this array whether or
    not its pixel ever meets glass, so its size costs occupancy across the whole image, and that
    cost compounds with the tree depth. Traced on the reference GTX 1650 Ti at 1280x720 with
    validation enabled:

    | Stack | Depth 2 | Depth 6 |
    |------:|--------:|--------:|
    |     1 | 5.41 ms | 9.86 ms |
    |     3 | 7.17 ms | 15.24 ms |
    |     6 | 9.86 ms | 19.00 ms |

    Both axes cost roughly a doubling across the range, and they multiply rather than add. Three
    slots and a depth of six sit just inside a sixty-frame budget while rendering glass correctly.

    Worth keeping in proportion: the spectator window is by far the most expensive consumer this
    renderer will ever have. A creature sensor is 64x64 to 256x256 pixels, between a fourteenth
    and a two-hundredth of this frame, so the figures above are the ceiling and not the workload.
*/
static const uint MAX_TASKS = 3;

/*!
    Hard ceiling on ray segments traced for one pixel.

    A splitting tree grows exponentially with depth, so the depth limit alone does not bound the
    work. This does, and it is what keeps one awkward pixel from stalling an entire workgroup.
*/
static const uint MAX_SEGMENTS = 48;

//! Contributions below this are not worth tracing further.
static const float THROUGHPUT_CUTOFF = 1.0 / 512.0;

//! Distance to nudge a new ray off the surface it started on, in metres.
static const float SURFACE_EPSILON = 1e-3;

//! Returns the largest component, used to decide whether a contribution still matters.
float maxComponent(float3 value)
{
    return max(value.x, max(value.y, value.z));
}

/*!
    Traces the ray tree rooted at one primary ray.

    Every surface in this world is perfectly smooth, so an intersection produces at most two
    outgoing rays with no sampling whatsoever: one reflected, one refracted, weighted by Fresnel.
    That is Whitted's 1980 algorithm, and it is deterministic — the same pixel traced twice gives
    bit-identical results, which is why this renderer has no denoiser and needs none.

    Recursion is not available in a compute shader, so the tree is walked with an explicit stack:
    the reflected branch is followed immediately and the refracted one is set aside for later.
*/
float3 radiance(float3 primary_origin, float3 primary_direction)
{
    float3 accumulated = float3(0.0, 0.0, 0.0);

    RayTask stack[MAX_TASKS];
    uint stack_size = 0;

    RayTask current;
    current.origin = primary_origin;
    current.direction = primary_direction;
    current.throughput = float3(1.0, 1.0, 1.0);
    current.depth = 0;

    bool have_ray = true;
    const uint max_depth = max(push.max_bounces, 1u);

    for (uint segment = 0; segment < MAX_SEGMENTS; ++segment)
    {
        if (!have_ray)
        {
            if (stack_size == 0)
            {
                break;
            }
            --stack_size;
            current = stack[stack_size];
        }

        const Hit hit = trace(current.origin, current.direction, 10000.0);

        if (!hit.valid)
        {
            // Infinite black. The world has no sky, which is why emissive geometry reads as neon.
            have_ray = false;
            continue;
        }

        const Triangle triangle = triangles[hit.triangle];
        const Material material = materials[triangle.material];

        // Flat shading throughout: the geometry is emitted per face, so the geometric normal is
        // the only normal there is, and the faceted look is the intended one.
        const float3 face_normal = normalize(cross(triangle.edge1, triangle.edge2));

        // Which side the ray arrives from decides both the normal to reflect about and whether
        // the refracted ray is entering the material or leaving it.
        const bool entering = dot(current.direction, face_normal) < 0.0;
        const float3 normal = entering ? face_normal : -face_normal;

        accumulated += current.throughput * material.emission;

        if ((current.depth + 1u) >= max_depth)
        {
            have_ray = false;
            continue;
        }

        const float cosine = saturate(dot(-current.direction, normal));
        const float reflectance = fresnelSchlick(cosine, material.index_of_refraction);
        const float3 hit_position = current.origin + (current.direction * hit.distance);

        // Air outside, the material's own index inside. Leaving a surface inverts the ratio.
        const float eta = entering ? (1.0 / material.index_of_refraction) : material.index_of_refraction;
        const float3 refracted = refract(current.direction, normal, eta);

        // refract() returns the zero vector when the angle exceeds the critical angle. There is
        // no transmitted ray at all in that case, and every last photon reflects instead — which
        // is why a glass edge seen at a shallow angle turns into a mirror.
        const bool total_internal_reflection = dot(refracted, refracted) < 1e-8;

        float transmitted_weight = material.transmission * (1.0 - reflectance);
        float reflected_weight = reflectance;

        if (total_internal_reflection)
        {
            reflected_weight += transmitted_weight;
            transmitted_weight = 0.0;
        }

        if (transmitted_weight > 0.0)
        {
            const float3 transmitted_throughput = current.throughput * material.colour * transmitted_weight;
            if ((maxComponent(transmitted_throughput) > THROUGHPUT_CUTOFF) && (stack_size < MAX_TASKS))
            {
                RayTask task;
                // Offset along the far side of the surface, since this ray leaves through it.
                task.origin = hit_position - (normal * SURFACE_EPSILON);
                task.direction = refracted;
                task.throughput = transmitted_throughput;
                task.depth = current.depth + 1u;

                stack[stack_size] = task;
                ++stack_size;
            }
        }

        const float3 reflected_throughput = current.throughput * material.colour * reflected_weight;

        if (maxComponent(reflected_throughput) > THROUGHPUT_CUTOFF)
        {
            current.origin = hit_position + (normal * SURFACE_EPSILON);
            current.direction = reflect(current.direction, normal);
            current.throughput = reflected_throughput;
            current.depth += 1u;
            have_ray = true;
        }
        else
        {
            have_ray = false;
        }
    }

    return accumulated;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void traceMain(uint3 thread_id: SV_DispatchThreadID)
{
    const uint2 pixel = thread_id.xy;

    if ((pixel.x >= push.resolution.x) || (pixel.y >= push.resolution.y))
    {
        return; // The dispatch is rounded up to whole workgroups.
    }

    const float3 direction = normalize(push.top_left.xyz + (push.pixel_delta_u.xyz * float(pixel.x)) + (push.pixel_delta_v.xyz * float(pixel.y)));

    const float3 colour = radiance(push.camera_position.xyz, direction);

    // Linear radiance, unencoded. postprocess.slang applies exposure, the ACES curve and sRGB.
    output_image[int2(pixel)] = float4(colour, 1.0);
}
