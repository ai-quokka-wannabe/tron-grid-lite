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
//     Bloom mip chain for the emissive surfaces. Emissive materials are the only light sources in the Grid, so the soft halo the bloom chain
//     produces is what makes a neon edge read as a light rather than as a bright stripe. Three compute entry points share one descriptor set
//     layout, one pipeline layout and one push-constant block; the host dispatches them in the order extract, downsample (per mip, descending),
//     upsample (per mip, ascending), then hands mip 0 to the post-process pass.
//
//     The chain runs on the HDR image of the User's window. Creature sensors are tiny (typically 64x64 to 256x256), so a mip chain buys them
//     nothing; the host is expected to skip bloom for sensor renders and pass bloom_strength 0 to the post-process shader instead.
//
// Entry points:
//     bloomExtractMain    — [shader("compute")], [numthreads(8, 8, 1)] — thresholds the HDR image and writes it at half resolution with a Karis
//                                                                        average, which suppresses single-pixel fireflies.
//     bloomDownsampleMain — [shader("compute")], [numthreads(8, 8, 1)] — 2x2 box-filter downsample of one mip into the next smaller mip.
//     bloomUpsampleMain   — [shader("compute")], [numthreads(8, 8, 1)] — 3x3 tent-filter upsample of one mip, additively blended into the next
//                                                                        larger mip, sampled bilinearly so the glow does not step.
//
// Expected descriptor bindings (set 0, identical for all three entry points):
//     binding 0 : RWTexture2D<float4> hdr_input — format rgba16f, full resolution. Read by bloomExtractMain only; the other two entry points
//                                                 never touch it, but it stays in the layout so all three share one descriptor set layout.
//     binding 1 : RWTexture2D<float4> bloom_src — format rgba16f, the source mip. Unused by bloomExtractMain.
//     binding 2 : RWTexture2D<float4> bloom_dst — format rgba16f, the destination mip. Written by all three entry points; also read back by
//                                                 bloomUpsampleMain for the additive blend. The format is stated explicitly rather than left
//                                                 unknown because the read-back would otherwise need the shaderStorageImageReadWithoutFormat
//                                                 feature; every mip in the chain is rgba16f, so there is nothing to generalise over.
//
//     Every entry point derives its extents with GetDimensions, so the host only needs to bind the right pair of mip views per dispatch and
//     dispatch ceil(dst_extent / 8) groups.
//
// Push-constant layout (stage compute, total 4 bytes):
//     offset 0, size 4 : float threshold — luminance below which a texel contributes nothing to the bloom. Read by bloomExtractMain only.

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> hdr_input;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> bloom_src;

[[vk::binding(2, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> bloom_dst;

struct BloomPushConstants
{
    float threshold;
};

[[vk::push_constant]]
BloomPushConstants push;

// Rec. 709 luminance weights.
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);

//! Computes the luminance of a linear-light colour.
float luminance(float3 colour)
{
    return dot(colour, LUMINANCE_WEIGHTS);
}

/*!
    Reads the source mip with bilinear filtering at a continuous texel coordinate.

    A storage image has no sampler attached, so the four neighbouring texels are fetched and
    weighted by hand. Coordinates are clamped rather than wrapped, which keeps the glow from
    bleeding around the edges of the image.
*/
float3 sampleBilinear(float2 position, int2 max_coord)
{
    float2 corner = position - 0.5;
    int2 base = int2(floor(corner));
    float2 weight = corner - float2(base);

    float3 c00 = bloom_src[clamp(base + int2(0, 0), int2(0, 0), max_coord)].rgb;
    float3 c10 = bloom_src[clamp(base + int2(1, 0), int2(0, 0), max_coord)].rgb;
    float3 c01 = bloom_src[clamp(base + int2(0, 1), int2(0, 0), max_coord)].rgb;
    float3 c11 = bloom_src[clamp(base + int2(1, 1), int2(0, 0), max_coord)].rgb;

    return lerp(lerp(c00, c10, weight.x), lerp(c01, c11, weight.x), weight.y);
}

//! Extracts the bright pixels of the HDR image at half resolution using a Karis average.
[shader("compute")]
[numthreads(8, 8, 1)]
void bloomExtractMain(uint3 thread_id : SV_DispatchThreadID)
{
    uint dst_width;
    uint dst_height;
    bloom_dst.GetDimensions(dst_width, dst_height);

    if ((thread_id.x >= dst_width) || (thread_id.y >= dst_height))
    {
        return;
    }

    // Read a 2x2 block from the full-resolution HDR image, clamped to valid bounds.
    uint src_width;
    uint src_height;
    hdr_input.GetDimensions(src_width, src_height);

    uint2 max_coord = uint2(src_width - 1, src_height - 1);
    uint2 base = thread_id.xy * 2;
    uint2 c00 = min(base + uint2(0, 0), max_coord);
    uint2 c10 = min(base + uint2(1, 0), max_coord);
    uint2 c01 = min(base + uint2(0, 1), max_coord);
    uint2 c11 = min(base + uint2(1, 1), max_coord);

    float3 s00 = max(hdr_input[c00].rgb, float3(0.0, 0.0, 0.0));
    float3 s10 = max(hdr_input[c10].rgb, float3(0.0, 0.0, 0.0));
    float3 s01 = max(hdr_input[c01].rgb, float3(0.0, 0.0, 0.0));
    float3 s11 = max(hdr_input[c11].rgb, float3(0.0, 0.0, 0.0));

    // Threshold: zero out texels below the brightness threshold.
    s00 *= step(push.threshold, luminance(s00));
    s10 *= step(push.threshold, luminance(s10));
    s01 *= step(push.threshold, luminance(s01));
    s11 *= step(push.threshold, luminance(s11));

    // Karis average: weight each sample by 1/(1 + luminance) so sub-pixel bright spots cannot dominate the downsample.
    float w00 = 1.0 / (1.0 + luminance(s00));
    float w10 = 1.0 / (1.0 + luminance(s10));
    float w01 = 1.0 / (1.0 + luminance(s01));
    float w11 = 1.0 / (1.0 + luminance(s11));
    float weight_sum = w00 + w10 + w01 + w11;

    float3 result = (s00 * w00 + s10 * w10 + s01 * w01 + s11 * w11) / max(weight_sum, 1e-7);

    bloom_dst[thread_id.xy] = float4(result, 1.0);
}

//! Downsamples the bloom source mip by a factor of two using a box filter.
[shader("compute")]
[numthreads(8, 8, 1)]
void bloomDownsampleMain(uint3 thread_id : SV_DispatchThreadID)
{
    uint dst_width;
    uint dst_height;
    bloom_dst.GetDimensions(dst_width, dst_height);

    if ((thread_id.x >= dst_width) || (thread_id.y >= dst_height))
    {
        return;
    }

    // Read a 2x2 block from the source mip, clamped to valid bounds.
    uint src_width;
    uint src_height;
    bloom_src.GetDimensions(src_width, src_height);

    uint2 max_coord = uint2(src_width - 1, src_height - 1);
    uint2 base = thread_id.xy * 2;
    uint2 c00 = min(base + uint2(0, 0), max_coord);
    uint2 c10 = min(base + uint2(1, 0), max_coord);
    uint2 c01 = min(base + uint2(0, 1), max_coord);
    uint2 c11 = min(base + uint2(1, 1), max_coord);

    float3 s00 = bloom_src[c00].rgb;
    float3 s10 = bloom_src[c10].rgb;
    float3 s01 = bloom_src[c01].rgb;
    float3 s11 = bloom_src[c11].rgb;

    // Simple box filter: the average of four texels.
    float3 result = (s00 + s10 + s01 + s11) * 0.25;

    bloom_dst[thread_id.xy] = float4(result, 1.0);
}

//! Upsamples a smaller bloom mip and additively blends it into the next larger mip.
[shader("compute")]
[numthreads(8, 8, 1)]
void bloomUpsampleMain(uint3 thread_id : SV_DispatchThreadID)
{
    uint dst_width;
    uint dst_height;
    bloom_dst.GetDimensions(dst_width, dst_height);

    if ((thread_id.x >= dst_width) || (thread_id.y >= dst_height))
    {
        return;
    }

    // Map the destination texel to source coordinates: the source mip is half the size.
    uint src_width;
    uint src_height;
    bloom_src.GetDimensions(src_width, src_height);

    int2 max_coord = int2(int(src_width) - 1, int(src_height) - 1);

    /*
        Where this destination texel's centre falls in the source mip, in continuous texel
        coordinates. Integer division would send all four texels of a 2x2 destination block to the
        same source position, which upsamples the tent-filtered result by nearest neighbour and
        makes the glow visibly blocky. Half the point of a mip pyramid is that the result is smooth.
    */
    float2 centre = (float2(thread_id.xy) + 0.5) * 0.5;

    // 3x3 tent filter, corners 1/16, edges 2/16, centre 4/16, each tap read bilinearly.
    float3 sum = float3(0.0, 0.0, 0.0);
    sum += (1.0 / 16.0) * sampleBilinear(centre + float2(-1.0, -1.0), max_coord);
    sum += (2.0 / 16.0) * sampleBilinear(centre + float2( 0.0, -1.0), max_coord);
    sum += (1.0 / 16.0) * sampleBilinear(centre + float2( 1.0, -1.0), max_coord);
    sum += (2.0 / 16.0) * sampleBilinear(centre + float2(-1.0,  0.0), max_coord);
    sum += (4.0 / 16.0) * sampleBilinear(centre + float2( 0.0,  0.0), max_coord);
    sum += (2.0 / 16.0) * sampleBilinear(centre + float2( 1.0,  0.0), max_coord);
    sum += (1.0 / 16.0) * sampleBilinear(centre + float2(-1.0,  1.0), max_coord);
    sum += (2.0 / 16.0) * sampleBilinear(centre + float2( 0.0,  1.0), max_coord);
    sum += (1.0 / 16.0) * sampleBilinear(centre + float2( 1.0,  1.0), max_coord);

    // Additive blend: accumulate the upsampled mip onto the current destination value.
    float3 existing = bloom_dst[thread_id.xy].rgb;
    bloom_dst[thread_id.xy] = float4(existing + sum, 1.0);
}
