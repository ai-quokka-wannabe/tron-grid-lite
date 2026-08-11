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
//     What a creature's light senses read: one radiance answer per sample ray. Eye samples and
//     irradiance directions arrive together in one flat ray buffer — the shader neither knows nor
//     cares which is which, because both questions are the same question.
//
//     This is the promise trace.slang carried from its first line, kept: the User's window and a
//     creature eye ask the identical `radiance` in `grid_optics`, so what a creature sees cannot
//     drift from what the User would see standing in the same place.
//
// Entry points:
//     senseMain — [shader("compute")], [numthreads(64, 1, 1)] — one thread per sample ray.
//
// Descriptor bindings (set 0):
//     binding 0 : StructuredBuffer<Node>       nodes     — the Grid's hierarchy.
//     binding 1 : StructuredBuffer<Triangle>   triangles — in leaf order.
//     binding 2 : StructuredBuffer<Material>   materials — indexed per triangle.
//     binding 3 : StructuredBuffer<Instance>   instances — the top level.
//     binding 4 : StructuredBuffer<float4>     rays      — origin, direction interleaved: ray i is
//                                                          elements 2i (origin, w unused) and
//                                                          2i + 1 (direction, w unused).
//     binding 5 : RWStructuredBuffer<float4>   results   — linear radiance per ray, w written zero.
//
// Push constants (12 bytes):
//     offset 0, size 4 : ray_count      — rays in the buffer; the dispatch is rounded up.
//     offset 4, size 4 : max_bounces    — total ray segments per sample, at least 1.
//     offset 8, size 4 : instance_count — placements in the top level.

import grid_bvh;
import grid_optics;

struct SensesPushConstants
{
    uint ray_count;
    uint max_bounces;
    uint instance_count;
};

[[vk::push_constant]]
SensesPushConstants push;

[[vk::binding(0, 0)]]
StructuredBuffer<Node> nodes;

[[vk::binding(1, 0)]]
StructuredBuffer<Triangle> triangles;

[[vk::binding(2, 0)]]
StructuredBuffer<Material> materials;

[[vk::binding(3, 0)]]
StructuredBuffer<Instance> instances;

[[vk::binding(4, 0)]]
StructuredBuffer<float4> rays;

[[vk::binding(5, 0)]]
RWStructuredBuffer<float4> results;

[shader("compute")]
[numthreads(64, 1, 1)]
void senseMain(uint3 thread_id: SV_DispatchThreadID)
{
    const uint index = thread_id.x;

    if (index >= push.ray_count)
    {
        return; // The dispatch is rounded up to whole workgroups.
    }

    const float3 origin = rays[index * 2u].xyz;
    const float3 direction = rays[(index * 2u) + 1u].xyz;

    const float3 colour = radiance(instances, push.instance_count, nodes, triangles, materials, push.max_bounces, origin, direction);

    // Linear radiance, unencoded and unclamped, exactly as the ABI documents TglEyeView::samples.
    results[index] = float4(colour, 0.0);
}
