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
//     The renderer. Traces the Grid by walking a hand-built bounding volume hierarchy in an
//     ordinary compute shader, with no ray-tracing hardware of any kind. Every surface is
//     perfectly smooth, so each intersection spawns exactly one reflected ray: the ray tree is
//     deterministic and shallow, there is no Monte Carlo integration, no variance and therefore
//     no denoiser anywhere in this project.
//
//     The same kernel will serve creature sensors, at a far smaller resolution, once bodies
//     exist. Nothing here is specific to the User's window except the size of the image.
//
// Entry points:
//     traceMain — [shader("compute")], [numthreads(8, 8, 1)] — one thread per output pixel.
//
// Descriptor bindings (set 0):
//     binding 0 : RWTexture2D<float4>       output_image — format rgba16f, linear radiance, written once per thread.
//     binding 1 : StructuredBuffer<Node>     nodes       — 32 bytes each, element 0 is the root.
//     binding 2 : StructuredBuffer<Triangle> triangles   — 48 bytes each, in leaf order.
//     binding 3 : StructuredBuffer<Material> materials   — 32 bytes each, indexed per triangle.
//     binding 4 : StructuredBuffer<Instance> instances   — 144 bytes each, the top level.
//
// Push constants (80 bytes):
//     offset  0, size 16 : camera_position   — xyz world space, w unused.
//     offset 16, size 16 : top_left          — xyz direction through the centre of pixel (0, 0).
//     offset 32, size 16 : pixel_delta_u     — xyz direction change per pixel step in x.
//     offset 48, size 16 : pixel_delta_v     — xyz direction change per pixel step in y.
//     offset 64, size  8 : resolution        — output image size in pixels.
//     offset 72, size  4 : max_bounces       — total ray segments per pixel, at least 1.
//     offset 76, size  4 : instance_count    — placements in the top level; zero means every ray misses.
//
//     The tracer writes linear radiance and nothing else: exposure, tone mapping and sRGB encoding
//     all belong to postprocess.slang, which sees the bloom chain as well.
//
// Shared code:
//     `grid_bvh` holds the hierarchy traversal — Node, Triangle, Hit, the slab test, Möller-
//     Trumbore and the stack walk. None of it knows what a colour is, which is the point: the
//     acoustic pass imports the same module and binds the same two buffers. What stays here is
//     everything optical, which is Material and the whole of `radiance` below.

import grid_bvh;

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
    uint instance_count;
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

[[vk::binding(4, 0)]]
StructuredBuffer<Instance> instances;

/*!
    Schlick's approximation to the Fresnel term.

    This is the whole lighting model. A perfectly smooth surface reflects a share of the light that
    depends only on the viewing angle and the index of refraction: about four per cent head-on for
    ordinary glass, rising to everything at all at a grazing angle. That rise is what produces the
    long bright streaks along a mirrored floor, and it is why the Grid's floor reads as almost
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

    Worth keeping in proportion: the User's window is by far the most expensive consumer this
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

    Every surface in the Grid is perfectly smooth, so an intersection produces at most two
    outgoing rays with no sampling whatsoever: one reflected, one refracted, weighted by Fresnel.
    That is Whitted's 1980 algorithm, and it is deterministic — the same pixel traced twice gives
    bit-identical results, which is why this renderer has no denoiser and needs none.

    Recursion is not available in a compute shader, so the tree is walked with an explicit stack:
    the reflected branch is followed immediately and the refracted one is set aside for later.
*/
float3 radiance(float3 primary_origin, float3 primary_direction)
{
    float3 accumulated = float3(0.0, 0.0, 0.0);

    /*
        Deliberately uninitialised, and it must stay that way. This is a bounded stack: stack_size
        only increments after a store at that index and only decrements before a read of it, so
        every read is of an element already written. Slang's flow analysis cannot see that and
        reports E41035, which the build disables by id for this reason alone.

        Initialising it is not free. Zeroing the array measured +6% of trace time on an RTX 4090
        and +29% on an Intel integrated part, which is thirty stores every call, per instance, per
        ray - and the weakest device is the one this project is meant to run on.
    */
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

        const Hit hit = traceScene(instances, push.instance_count, nodes, triangles, current.origin, current.direction, 10000.0);

        if (!hit.valid)
        {
            // Infinite black. The Grid has no sky, which is why emissive geometry reads as neon.
            have_ray = false;
            continue;
        }

        const Triangle triangle = triangles[hit.triangle];
        const Material material = materials[triangle.material];

        /*
            Flat shading throughout: the geometry is emitted per face, so the geometric normal is the
            only normal there is, and the faceted look is the intended one.

            The edges are in the instance's own frame, so the normal has to come back out to world
            space before anything reflects about it. The rotation part of `to_world` does that for a
            rigid placement, which is what a creature body is: for a rotation the inverse-transpose is
            the rotation itself, so this is exact rather than approximate.

            **Under a non-uniform scale it would not be**, and the correct transform there is the
            inverse-transpose, which this record does not carry. Nothing places a scaled instance
            today. The moment something does, this is the line that is wrong, and `BvhLib::Hit` says
            the same thing on the host side.

            The Grid sits at the identity, where every one of these products is exact — so the
            picture is bit-for-bit what a single-level traversal would produce.
        */
        const Instance instance = instances[hit.instance];
        const float3 local_normal = cross(triangle.edge1, triangle.edge2);
        const float3 face_normal = normalize(transformDirection(instance.to_world_row0, instance.to_world_row1, instance.to_world_row2, local_normal));

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
        const float3 hit_position = current.origin + (current.direction * hit.distance);

        // Air outside, the material's own index inside. Leaving a surface inverts the ratio.
        const float eta = entering ? (1.0 / material.index_of_refraction) : material.index_of_refraction;
        const float3 refracted = refract(current.direction, normal, eta);

        // refract() returns the zero vector when the angle exceeds the critical angle. There is
        // no transmitted ray at all in that case, and every last photon reflects instead — which
        // is why a glass edge seen at a shallow angle turns into a mirror.
        const bool total_internal_reflection = dot(refracted, refracted) < 1e-8;

        /*
            Schlick's approximation is defined for the angle measured on the *air* side of the
            interface. On the way in that is the angle of incidence, but on the way out it is the
            angle of the ray that leaves, not the shallower one inside the glass.

            Feeding it the internal angle understates reflectance badly exactly where it matters:
            a ray leaving 1.52 glass at 40 degrees returns 0.04 when the true value has already
            climbed past 0.3, and then a degree later total internal reflection snaps it to 1. That
            draws a hard ring around every glass edge — the opposite of the gradual turn to mirror
            the comment above describes.
        */
        const float fresnel_cosine = entering ? cosine : saturate(dot(refracted, -normal));
        const float reflectance = total_internal_reflection ? 1.0 : fresnelSchlick(fresnel_cosine, material.index_of_refraction);

        // No separate total-internal-reflection case: reflectance is already 1 there, which leaves
        // nothing to transmit and sends the whole throughput down the reflected branch.
        const float transmitted_weight = material.transmission * (1.0 - reflectance);
        const float reflected_weight = reflectance;

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
