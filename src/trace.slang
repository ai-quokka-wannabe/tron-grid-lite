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
//     binding 0 : RWTexture2D<float4>       output_image — format rgba8, written once per thread.
//     binding 1 : StructuredBuffer<Node>     nodes       — 32 bytes each, element 0 is the root.
//     binding 2 : StructuredBuffer<Triangle> triangles   — 48 bytes each, in leaf order.
//     binding 3 : StructuredBuffer<Material> materials   — 32 bytes each, indexed per triangle.
//
// Push constants (96 bytes):
//     offset  0, size 16 : camera_position   — xyz world space, w unused.
//     offset 16, size 16 : top_left          — xyz direction through the centre of pixel (0, 0).
//     offset 32, size 16 : pixel_delta_u     — xyz direction change per pixel step in x.
//     offset 48, size 16 : pixel_delta_v     — xyz direction change per pixel step in y.
//     offset 64, size  8 : resolution        — output image size in pixels.
//     offset 72, size  4 : max_bounces       — total ray segments per pixel, at least 1.
//     offset 76, size  4 : node_count        — zero means an empty world; the kernel writes black.
//     offset 80, size  4 : exposure          — linear scale applied before the tone curve.
//     offset 84, size 12 : padding.

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
    float exposure;

    // Three separate scalars rather than a float3: std430 would align a float3 to sixteen bytes
    // and silently grow the struct past what the host declares.
    float padding0;
    float padding1;
    float padding2;
};

[[vk::push_constant]]
TracePushConstants push;

[[vk::binding(0, 0)]]
[[vk::image_format("rgba8")]]
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

    const float3 inverse_direction = 1.0 / direction;

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

//! Follows one ray through the world, reflecting at every surface it meets.
float3 radiance(float3 origin, float3 direction)
{
    float3 accumulated = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);

    const uint bounces = max(push.max_bounces, 1u);

    for (uint bounce = 0; bounce < bounces; ++bounce)
    {
        const Hit hit = trace(origin, direction, 10000.0);

        if (!hit.valid)
        {
            // Infinite black. The world has no sky, which is why emissive geometry reads as neon.
            break;
        }

        const Triangle triangle = triangles[hit.triangle];
        const Material material = materials[triangle.material];

        // Flat shading throughout: the geometry is emitted per face, so the geometric normal is
        // the only normal there is, and the faceted look is the intended one.
        float3 normal = normalize(cross(triangle.edge1, triangle.edge2));
        if (dot(normal, direction) > 0.0)
        {
            normal = -normal; // Struck from behind; flip so the reflection leaves the same side.
        }

        accumulated += throughput * material.emission;

        const float cosine = saturate(dot(-direction, normal));
        const float reflectance = fresnelSchlick(cosine, material.index_of_refraction);

        throughput *= material.colour * reflectance;

        // Everything not reflected is absorbed for now. Transmission arrives in Phase 3, at which
        // point this becomes a two-way split rather than a dead end.
        if (dot(throughput, throughput) < 1e-8)
        {
            break;
        }

        const float3 hit_position = origin + (direction * hit.distance);
        direction = reflect(direction, normal);
        origin = hit_position + (normal * 1e-3); // Nudge off the surface so the next ray cannot re-hit it.
    }

    return accumulated;
}

/*!
    Reinhard tone curve and sRGB encoding.

    Deliberately minimal. Phase 4 moves tone mapping into postprocess.slang, where the fitted ACES
    curve and the bloom chain already live; until then this keeps the emissive values, which are
    well above one, inside a displayable range.
*/
float3 tonemap(float3 colour)
{
    const float3 exposed = colour * push.exposure;
    const float3 mapped = exposed / (exposed + 1.0);

    // Exact sRGB transfer function rather than a 2.2 power, so the dark end of a mostly black
    // image is right.
    const float3 low = mapped * 12.92;
    const float3 high = (1.055 * pow(max(mapped, 1e-5), 1.0 / 2.4)) - 0.055;
    return select(mapped <= 0.0031308, low, high);
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

    output_image[int2(pixel)] = float4(tonemap(colour), 1.0);
}
