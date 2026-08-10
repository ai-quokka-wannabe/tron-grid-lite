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
//     `grid_bvh` holds the hierarchy traversal, and `grid_optics` holds everything light does with
//     it — Material, Fresnel and the whole Whitted walk. This file is what remains: the camera, the
//     pixel grid, and one `radiance` call per pixel. A creature eye is the same call per sample,
//     from its own pipeline, which is exactly why the walk lives in a module rather than here.

import grid_bvh;
import grid_optics;

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

    const float3 colour = radiance(instances, push.instance_count, nodes, triangles, materials, push.max_bounces, push.camera_position.xyz, direction);

    // Linear radiance, unencoded. postprocess.slang applies exposure, the ACES curve and sRGB.
    output_image[int2(pixel)] = float4(colour, 1.0);
}
