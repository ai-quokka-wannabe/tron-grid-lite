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
//     Minimal rasterised vertex + fragment shader used for the Phase 0 milestone: prove that instance, device, swapchain, dynamic rendering and
//     the Slang compilation step all work end to end by drawing a single triangle into the spectator window. The compute ray tracer replaces this
//     shader entirely once the BVH traversal path is alive; it is kept afterwards purely as a smoke test for the graphics queue.
//
// Entry points:
//     vertMain — [shader("vertex")]   — transforms an object-space vertex into clip space.
//     fragMain — [shader("fragment")] — visualises the interpolated normal as a colour.
//
// Expected descriptor bindings:
//     None. The shader is deliberately descriptor-free so the Phase 0 pipeline needs no descriptor set layout at all.
//
// Push-constant layout (set on stages vertex | fragment, total 64 bytes):
//     offset  0, size 64 : float4x4 model_view_projection — combined object-to-clip transform, column-major as produced by MathLib::Mat4, which
//                                                            matches Slang's default SPIR-V matrix layout. Upload MathLib::Mat4::data() verbatim;
//                                                            do NOT transpose it on the host side.
//
// Vertex input layout (matches the C++ Vertex struct, 32 bytes, stride 32):
//     offset  0, size 12 : float3 position
//     offset 12, size 12 : float3 normal
//     offset 24, size  8 : float2 uv

struct TrianglePushConstants
{
    float4x4 model_view_projection;
    float4 tint; //!< Flat surface colour. Phase 1 has no lighting model: the host draws each sub-mesh with its own tint.
};

[[vk::push_constant]]
TrianglePushConstants push;

//! Per-vertex input attributes.
struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

//! Interpolated vertex-to-fragment data.
struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

//! Transforms the vertex into clip space and passes the normal and texture coordinate through unchanged.
[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    VSOutput output;
    output.position = mul(push.model_view_projection, float4(input.position, 1.0));
    output.normal = input.normal;
    output.uv = input.uv;
    return output;
}

//! Emits the flat tint the host pushed for this sub-mesh, with a slight facing-based falloff so surface orientation stays readable.
[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    // Not a lighting model — just enough shading that a flat floor does not read as a solid block of colour.
    const float facing = abs(normalize(input.normal).y) * 0.35 + 0.65;
    return float4(push.tint.rgb * facing, push.tint.a);
}
