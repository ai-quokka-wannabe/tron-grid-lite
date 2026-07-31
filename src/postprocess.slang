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
//     Compute post-process pass. Reads the HDR image produced by the compute ray tracer, composites the bloom mip chain on top of it, applies the
//     ACES fitted RRT+ODT tone curve, encodes to sRGB and optionally darkens the corners with a subtle vignette. The result is written straight
//     into the destination storage image.
//
//     The pass is used for two different destinations. For the spectator window the destination is the swapchain image and the vignette is
//     usually enabled, because it reads as photographic to a human observer. For a creature sensor the destination is the small sensor image and
//     the vignette must be disabled (strength 0), because a synthetic radial brightness gradient would be an artefact the agent could learn from
//     rather than a property of the world.
//
//     Deliberately absent, in contrast to the renderer this code was adapted from: no chromatic aberration, no colour grade, no scan lines and no
//     volumetric fog composite. The output is meant to be an honest tone mapping of the radiance the tracer computed.
//
// Entry points:
//     postprocessMain — [shader("compute")], [numthreads(8, 8, 1)] — one thread per destination pixel.
//
// Expected descriptor bindings (set 0):
//     binding 0 : RWTexture2D<float4> hdr_input      — format rgba16f,  full resolution, read only.
//     binding 1 : RWTexture2D<float4> output_image   — format rgba8, full resolution, write only.
//     binding 2 : RWTexture2D<float4> bloom_texture  — format rgba16f,  half resolution mip 0 of the bloom chain, read only.
//
//     The output image must be a UNORM format, never an SRGB format: this shader performs the sRGB transfer function itself, and hardware
//     conversion on top of it would apply gamma twice. The format is declared rather than left unknown so that writing to it needs no optional
//     device feature — shaderStorageImageWriteWithoutFormat is not something a renderer aimed at modest hardware should require.
//
// Push-constant layout (stage compute, total 12 bytes):
//     offset 0, size 4 : float bloom_strength    — linear multiplier on the bloom contribution. 0 disables bloom.
//     offset 4, size 4 : float vignette_strength — radial darkening amount. 0 disables the vignette (use this for creature sensors).
//     offset 8, size 4 : float exposure          — linear scale applied to the radiance before the tone curve.

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> hdr_input;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba8")]]
RWTexture2D<float4> output_image;

[[vk::binding(2, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> bloom_texture;

struct PostProcessPushConstants
{
    float bloom_strength;
    float vignette_strength;

    // Exposure belongs here rather than in the tracer: the tracer's business is radiance, and how
    // much of it survives into a displayable range is a property of the tone mapping.
    float exposure;
};

[[vk::push_constant]]
PostProcessPushConstants push;

// ACES colour space matrices (Stephen Hill / MJP BakingLab).
// sRGB primaries to approximate AP1 (Academy Color Encoding System).
static const float3x3 ACES_INPUT_MAT = float3x3(
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777
);

// Approximate AP1 back to sRGB primaries.
static const float3x3 ACES_OUTPUT_MAT = float3x3(
     1.60475, -0.53108, -0.07367,
    -0.10208,  1.10813, -0.00605,
    -0.00327, -0.07276,  1.07602
);

// IEC 61966-2-1 sRGB transfer function constants.
static const float SRGB_LINEAR_THRESHOLD = 0.0031308;
static const float SRGB_LINEAR_SCALE = 12.92;
static const float SRGB_POWER_SCALE = 1.055;
static const float SRGB_POWER_EXPONENT = 1.0 / 2.4;
static const float SRGB_POWER_OFFSET = 0.055;

//! Fitted RRT+ODT curve applied per channel in AP1 space (Hill / MJP).
float3 rrtAndOdtFit(float3 v)
{
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

//! ACES fitted tonemapping with AP1 colour space transforms (Hill / MJP BakingLab).
float3 acesTonemap(float3 colour)
{
    colour = mul(ACES_INPUT_MAT, colour);
    colour = rrtAndOdtFit(colour);
    colour = mul(ACES_OUTPUT_MAT, colour);
    return saturate(colour);
}

//! Converts a single linear-light channel to sRGB using the exact IEC 61966-2-1 transfer function.
float linearToSrgb(float channel)
{
    channel = max(channel, 0.0);

    if (channel <= SRGB_LINEAR_THRESHOLD)
    {
        return channel * SRGB_LINEAR_SCALE;
    }

    return SRGB_POWER_SCALE * pow(channel, SRGB_POWER_EXPONENT) - SRGB_POWER_OFFSET;
}

//! Reads the HDR image at the given integer coordinate and adds the bloom contribution from the half-resolution bloom mip.
float3 sampleHdrWithBloom(uint2 coord, uint2 hdr_extent, uint2 bloom_extent)
{
    uint2 clamped = min(coord, hdr_extent - uint2(1, 1));
    float3 hdr = hdr_input[clamped].rgb;
    uint2 bloom_coord = min(clamped / 2, bloom_extent - uint2(1, 1));
    return hdr + push.bloom_strength * bloom_texture[bloom_coord].rgb;
}

//! Tonemaps one pixel of the HDR image into the destination storage image.
[shader("compute")]
[numthreads(8, 8, 1)]
void postprocessMain(uint3 thread_id : SV_DispatchThreadID)
{
    uint width;
    uint height;
    hdr_input.GetDimensions(width, height);

    if ((thread_id.x >= width) || (thread_id.y >= height))
    {
        return;
    }

    uint bloom_width;
    uint bloom_height;
    bloom_texture.GetDimensions(bloom_width, bloom_height);

    float3 colour = sampleHdrWithBloom(thread_id.xy, uint2(width, height), uint2(bloom_width, bloom_height));

    // Exposure scales the radiance into the range the tone curve is shaped for. Applied after the
    // bloom composite so that raising it brightens the glow along with everything else.
    colour *= push.exposure;

    // ACES fitted RRT+ODT: transforms into AP1 colour space, applies the tone curve, transforms back.
    colour = acesTonemap(colour);

    // sRGB encoding (IEC 61966-2-1). The destination image uses a UNORM format, so the hardware applies no gamma of its own.
    colour.r = linearToSrgb(colour.r);
    colour.g = linearToSrgb(colour.g);
    colour.b = linearToSrgb(colour.b);

    // Optional vignette: a radial falloff that darkens the corners. Enabled for the human spectator window, disabled for creature sensors so no
    // synthetic brightness gradient contaminates what an agent perceives.
    if (push.vignette_strength > 0.0)
    {
        float2 uv = (float2(thread_id.xy) + 0.5) / float2(width, height);
        float2 centre_offset = uv - 0.5;
        float vignette = saturate(1.0 - dot(centre_offset, centre_offset) * push.vignette_strength);
        colour *= vignette;
    }

    output_image[thread_id.xy] = float4(colour, 1.0);
}
