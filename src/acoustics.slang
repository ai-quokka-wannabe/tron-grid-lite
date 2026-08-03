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
//     Hearing on the Grid. Gathers sound at an ear by walking the very same hierarchy the renderer
//     traces, through the very same `grid_bvh` module, and accumulating what arrives into a
//     histogram of energy against delay.
//
//     `src/acoustics.hpp` is the specification and this file mirrors it, exactly as `trace.slang`
//     mirrors `libs/bvh`. A change to one that is not mirrored in the other is a bug in this file,
//     whichever one is "right", and `--verify-acoustics` exists to catch precisely that.
//
// What differs from the visual pass, which is everything downstream of the intersection:
//     - A ray carries **accumulated path length**, and where that lands in time is as much of the
//       answer as how loud it is. Light is instantaneous for a renderer; sound is not.
//     - **Path length, not depth, is the termination rule.** Reflection order is a second, looser
//       bound.
//     - The output is a **histogram**, not a pixel. Many rays land in the same bin, so the write is
//       a scattered atomic add rather than one unconditional store.
//     - Surfaces are **perfect mirrors**: nothing is subtracted on contact, so a ray carries no
//       throughput at all. See `acoustics.hpp` for why, and for the open-half-space assumption that
//       makes it safe.
//
// Entry points:
//     acousticMain — [shader("compute")], [numthreads(64, 1, 1)] — one workgroup per ear.
//
// Descriptor bindings (set 0):
//     binding 0 : StructuredBuffer<Node>      nodes            — 32 bytes each, element 0 is the root.
//     binding 1 : StructuredBuffer<Triangle>  triangles        — 48 bytes each, in leaf order.
//     binding 2 : StructuredBuffer<float>     source_strengths — one per material slot.
//     binding 3 : StructuredBuffer<float4>    ears             — xyz world position, w unused.
//     binding 4 : RWStructuredBuffer<uint>    histogram        — ear_count * BAND_COUNT * BIN_COUNT.
//     binding 5 : StructuredBuffer<Instance>  instances        — 144 bytes each, the top level.
//
// Push constants (52 bytes):
//     offset  0, size 16 : hum_spectrum       — source strength per band, at full amplitude.
//     offset 16, size 16 : air_absorption     — dB per kilometre per band.
//     offset 32, size  4 : direction_count    — rays cast from each ear.
//     offset 36, size  4 : max_order          — reflections a ray may make. Zero is direct only.
//     offset 40, size  4 : instance_count     — placements in the top level; zero means every ray misses.
//     offset 44, size  4 : material_count     — length of source_strengths, for the bounds guard.
//     offset 48, size  4 : range_metres       — total accumulated path cap.

import grid_bvh;

//! Must equal Acoustics::BAND_COUNT. The host asserts it.
static const uint BAND_COUNT = 4;

//! Must equal Acoustics::BIN_COUNT. The host asserts it.
static const uint BIN_COUNT = 64;

//! Must equal Acoustics::BIN_SECONDS.
static const float BIN_SECONDS = 0.001;

//! Must equal Acoustics::SPEED_OF_SOUND.
static const float SPEED_OF_SOUND = 343.0;

//! Must equal Acoustics::SURFACE_EPSILON.
static const float SURFACE_EPSILON = 1e-3;

/*!
    Threads per workgroup, and therefore per ear.

    Sixty-four is a whole wave on AMD and two on NVIDIA, and it divides the 256-entry histogram
    evenly so that the clear and the write-out need no bounds test beyond their loop condition.
*/
static const uint WORKGROUP_SIZE = 64;

/*!
    Fixed point for the histogram: what one unit of relative energy is worth.

    Float atomics are not available — `VK_EXT_shader_atomic_float` is not requested and requesting it
    would break the device policy of Vulkan 1.3 core plus the swapchain extension — so accumulation
    is in `uint`. That turns out to be a feature rather than a concession: **integer addition is
    associative, so the histogram is bit-identical regardless of the order the threads happen to
    finish in.** A float atomic would have made the result depend on scheduling, which
    `PROGRAM_INTERFACE.md`'s replay guarantee could not survive.

    The scale is answered by the range cap rather than dodged by it. With total path capped at 20 m,
    spreading costs at most 26 dB from the one-metre reference and air at 8 kHz adds about 1.5 dB
    over that distance; surfaces contribute nothing, being lossless. The whole delivered response
    therefore spans under about 28 dB — a factor of a thousand, not many orders of magnitude — so one
    scale suffices.

    `2^18` puts a unit arrival at 262,144, which leaves roughly eight bits of resolution on the
    quietest arrival the cap permits. Against a worst case of every ray depositing at full strength
    into one bin, the product is what must not overflow: `2^18 x 2^13 = 2^31` allows 8,192
    full-strength deposits with a bit to spare, and the host asserts the product at dispatch time
    rather than leaving a wrap to be discovered in a histogram.
*/
static const float FIXED_POINT_SCALE = 262144.0;

/*!
    The golden angle as a fraction of a full turn: (3 - sqrt(5)) / 2.

    Written as a literal rather than computed, and the host uses the same one, because the two must
    agree on the direction set before anything else can be compared.
*/
static const float GOLDEN_TURN_FRACTION = 0.38196601125010515;

//! One full turn, in radians.
static const float TWO_PI = 6.28318530717958648;

struct AcousticPushConstants
{
    float4 hum_spectrum;
    float4 air_absorption_db_per_km;
    uint direction_count;
    uint max_order;
    uint instance_count;
    uint material_count;
    float range_metres;
};

[[vk::push_constant]]
AcousticPushConstants push;

[[vk::binding(0, 0)]]
StructuredBuffer<Node> nodes;

[[vk::binding(1, 0)]]
StructuredBuffer<Triangle> triangles;

[[vk::binding(2, 0)]]
StructuredBuffer<float> source_strengths;

[[vk::binding(3, 0)]]
StructuredBuffer<float4> ears;

[[vk::binding(4, 0)]]
RWStructuredBuffer<uint> histogram;

[[vk::binding(5, 0)]]
StructuredBuffer<Instance> instances;

/*!
    One ear's histogram, in shared memory.

    Four bands by sixty-four bins is 1 KiB, comfortably inside the 16 KiB Vulkan guarantees a
    workgroup. Giving one workgroup the whole of one ear is what removes cross-workgroup contention
    entirely: every atomic below is a *shared* memory atomic against at most sixty-three other
    threads, and the only global write is the single flat copy at the end.
*/
groupshared uint shared_histogram[BAND_COUNT * BIN_COUNT];

/*!
    Returns direction `index` of a spherical Fibonacci set of `count` directions.

    Quasi-uniform over the whole sphere, deterministic, and free of any random number generator —
    there is none anywhere in this repository. Must match `Acoustics::fibonacciDirection` exactly.

    The half-offset is not decorative: without it the first point sits exactly at the north pole and
    the last exactly at the south, wasting two directions on a pair no uniform set would place there
    and thinning the equator to pay for them.
*/
float3 fibonacciDirection(uint index, uint count)
{
    const uint safe_count = max(count, 1u);
    const float height = 1.0 - ((2.0 * (float(index) + 0.5)) / float(safe_count));

    // Clamped because a height a hair outside [-1, 1] from rounding would take the square root of a
    // negative number, and the resulting NaN poisons a whole ray rather than one bin.
    const float radius = sqrt(max(0.0, 1.0 - (height * height)));

    const float turns = float(index) * GOLDEN_TURN_FRACTION;
    const float theta = (turns - floor(turns)) * TWO_PI;

    return float3(radius * cos(theta), height, radius * sin(theta));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void acousticMain(uint3 group_id: SV_GroupID, uint3 local_id: SV_GroupThreadID)
{
    const uint ear_index = group_id.x;
    const uint thread = local_id.x;

    for (uint clear = thread; clear < (BAND_COUNT * BIN_COUNT); clear += WORKGROUP_SIZE)
    {
        shared_histogram[clear] = 0u;
    }

    // Every thread must see a cleared histogram before any thread adds to it.
    GroupMemoryBarrierWithGroupSync();

    const float3 ear = ears[ear_index].xyz;
    const float range = max(push.range_metres, 0.0);

    /*
        Strided rather than blocked, so that neighbouring directions land on neighbouring threads.
        Adjacent Fibonacci directions point to nearly the same place, so they walk nearly the same
        path through the hierarchy — which is the closest thing an incoherent gather has to coherence,
        and it is free.
    */
    for (uint direction_index = thread; direction_index < push.direction_count; direction_index += WORKGROUP_SIZE)
    {
        float3 origin = ear;
        float3 direction = fibonacciDirection(direction_index, push.direction_count);
        float path = 0.0;

        for (uint order = 0; order <= push.max_order; ++order)
        {
            // The remaining budget is the ray's own limit, so no ray outruns the cap even within a
            // single segment.
            const float remaining = range - path;
            if (remaining <= 0.0)
            {
                break;
            }

            const Hit hit = traceScene(instances, push.instance_count, nodes, triangles, origin, direction, remaining);
            if (!hit.valid)
            {
                break; // Escaped. The Grid is an open plane, so most rays end here.
            }

            path += hit.distance;

            const Triangle triangle = triangles[hit.triangle];

            // A material index past the end of the table would be an out-of-bounds read. An
            // undescribed surface is silent and still reflects, matching the host exactly.
            const float source_strength = (triangle.material < push.material_count) ? source_strengths[triangle.material] : 0.0;

            if (source_strength > 0.0)
            {
                /*
                    Spreading is explicit and measured from a one-metre reference — the Grid's own
                    unit — never a detection sphere. The floor matters: without it a ray grazing a
                    tube it is almost touching divides by an arbitrarily small number and deposits an
                    arbitrarily large amount into one bin.
                */
                const float spreading = 1.0 / max(path * path, 1.0);
                const uint bin = uint(path / (SPEED_OF_SOUND * BIN_SECONDS));

                if (bin < BIN_COUNT)
                {
                    for (uint band = 0; band < BAND_COUNT; ++band)
                    {
                        // Energy rather than pressure, so the decibels divide by ten, not twenty.
                        const float air_db = push.air_absorption_db_per_km[band] * (path / 1000.0);
                        const float air = pow(10.0, -air_db / 10.0);

                        const float energy = source_strength * push.hum_spectrum[band] * spreading * air;
                        /*
                            Rounded, not truncated. Truncation loses on average half a quantum per
                            deposit, and a solve makes tens of thousands of them, so the loss is a
                            systematic deficit rather than noise — it showed up as the device
                            reading consistently below the host by a few thousandths of a per cent.
                            Rounding is unbiased, and the half it adds is far below the overflow
                            headroom the scale was chosen for.
                        */
                        InterlockedAdd(shared_histogram[(band * BIN_COUNT) + bin], uint((energy * FIXED_POINT_SCALE) + 0.5));
                    }
                }
            }

            /*
                Reflect losslessly. Nothing is subtracted here and nothing needs to be — what bounds
                the response is the range cap above, this loop's order cap, and the spreading and air
                terms applied at the deposit.
            */
            // Out to world space first: the edges are in the instance's own frame, and the ray this
            // reflects is a world-space ray. Exact for a rigid placement, which is what a body is,
            // and wrong for a non-uniformly scaled one — the same caveat trace.slang carries, for the
            // same reason, and nothing places a scaled instance today.
            const Instance instance = instances[hit.instance];
            const float3 local_normal = cross(triangle.edge1, triangle.edge2);
            const float3 face_normal = normalize(transformDirection(instance.to_world_row0, instance.to_world_row1, instance.to_world_row2, local_normal));

            // Reflect about the face the ray actually arrived at, whichever side that is: a creature
            // standing in a terrace hollow hears its walls from the inside.
            const float3 normal = (dot(direction, face_normal) < 0.0) ? face_normal : -face_normal;

            const float3 hit_position = origin + (direction * hit.distance);
            origin = hit_position + (normal * SURFACE_EPSILON);
            direction = direction - (normal * (2.0 * dot(direction, normal)));
        }
    }

    // Every deposit must have landed before any of it is copied out.
    GroupMemoryBarrierWithGroupSync();

    const uint base = ear_index * BAND_COUNT * BIN_COUNT;
    for (uint write = thread; write < (BAND_COUNT * BIN_COUNT); write += WORKGROUP_SIZE)
    {
        histogram[base + write] = shared_histogram[write];
    }
}
