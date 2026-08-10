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

#pragma once

#ifdef _WIN32
#include <Volk/volk.h>
#else
#include <volk/volk.h>
#endif
#include <vulkan/vulkan_raii.hpp>
#include "memory_arena.hpp"
#include <logging/logger.hpp>
#include <cstdint>
#include <string>
#include <vector>

class Device; // forward declaration

/*!
    Everything that happens to the traced image before the User sees it.

    The tracer writes linear radiance, which is not a picture: emissive surfaces are far brighter
    than one, and nothing about those numbers is ready for a display. This stage turns radiance
    into a picture in three steps — a bloom chain that gives the emissive geometry its halo, the
    ACES fitted tone curve that compresses the range, and sRGB encoding.

    The bloom chain is a mip pyramid. The extract pass thresholds the full-resolution image into
    mip 0 at half resolution, successive downsamples box-filter their way down the pyramid, and the
    upsamples tent-filter back up while adding into the mip below. What arrives back at mip 0 is a
    wide, soft glow that no single blur pass of a sensible radius could produce.

    Creature sensors will skip all of this. A 64x64 sensor gains nothing from a mip pyramid, and a
    vignette on a sensor would be a synthetic gradient a Program could learn from rather than a
    property of the Grid — which is why both are parameters rather than fixtures.
*/
class PostProcess {
public:
    /*!
        Builds the pipelines and descriptor machinery. Call resize() before the first record().

        \param device Logical device.
        \param frames_in_flight Number of independent image sets to allocate.
        \param bloom_shader_path Compiled `bloom_downsample.spv`.
        \param postprocess_shader_path Compiled `postprocess.spv`.
        \param logger Logger for the allocation summary.
    */
    PostProcess(const Device& device, uint32_t frames_in_flight, const std::string& bloom_shader_path, const std::string& postprocess_shader_path,
        LoggingLib::Logger& logger);

    // Non-copyable, non-movable: it owns Vulkan objects and is referenced by the frame loop.
    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;
    PostProcess(PostProcess&&) = delete;
    PostProcess& operator=(PostProcess&&) = delete;

    /*!
        Reallocates every image for a new output size and rewrites the descriptor sets.

        The caller must have waited for the device to be idle, because the sets being rewritten may
        still be referenced by work in flight.

        \param extent Size of the final picture.
        \param hdr_views One HDR view per frame in flight, owned by the tracer and read here.
    */
    void resize(vk::Extent2D extent, const std::vector<vk::ImageView>& hdr_views);

    /*!
        Records the bloom chain and the tone mapping pass.

        Expects the tracer's HDR image for this slot to be in `eGeneral` and already made visible.
        On return this stage's own output image is in `eTransferSrcOptimal`, ready to be blitted.

        \param command_buffer Command buffer to record into.
        \param frame_slot Frame in flight, which selects the image set and descriptor sets.
        \param bloom_threshold Luminance below which a texel contributes no bloom.
        \param bloom_strength Multiplier on the bloom contribution; zero skips the chain entirely.
        \param vignette_strength Radial darkening; zero disables it.
        \param exposure Linear scale applied to radiance before the tone curve.
    */
    void record(const vk::raii::CommandBuffer& command_buffer, uint32_t frame_slot, float bloom_threshold, float bloom_strength, float vignette_strength,
        float exposure) const;

    //! Returns the final low-dynamic-range image for a frame slot, valid until the next resize().
    [[nodiscard]] vk::Image outputImage(uint32_t frame_slot) const
    {
        return *m_output_images[frame_slot].image;
    }

    //! Returns the size the output images were last allocated at.
    [[nodiscard]] vk::Extent2D extent() const
    {
        return m_extent;
    }

private:
    /*!
        An image and its view, owned together because they always live and die together.

        There is deliberately no memory member. The backing store belongs to `m_image_arena`, which
        holds one block behind the whole pyramid rather than one allocation per mip, and reclaims all
        of it at once on resize.
    */
    struct OwnedImage {
        vk::raii::Image image{nullptr};
        vk::raii::ImageView view{nullptr};
    };

    //! Creates one storage image of the given size and format, bound to the arena.
    [[nodiscard]] OwnedImage createImage(vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage);

    //! Chooses the pyramid's mip sizes for the current output extent.
    void computeBloomExtents();

    //! Points every descriptor set at the current images.
    void writeDescriptorSets(const std::vector<vk::ImageView>& hdr_views);

    //! Places a barrier making one bloom mip's writes visible to the next dispatch's reads.
    static void barrierBetweenMips(const vk::raii::CommandBuffer& command_buffer, vk::Image image);

    const Device* m_device{nullptr}; //!< Logical device (non-owning).
    LoggingLib::Logger* m_logger{nullptr}; //!< Logger (non-owning).

    uint32_t m_frames_in_flight{0u}; //!< Number of independent image sets.
    vk::Extent2D m_extent{}; //!< Size of the final picture.
    /*!
        One block of memory behind every image this stage owns, rather than one allocation each.

        The bloom pyramid alone is ten images at the default two frames in flight, every one of them
        far below the megabyte at which the validation layer stops objecting. They are built together
        and thrown away together on resize, which is exactly the shape a bump allocator wants.
    */
    MemoryArena m_image_arena;

    std::vector<vk::Extent2D> m_bloom_extents; //!< Size of each mip, index 0 being half the output.

    //! Per frame in flight, one mip pyramid. Indexed [frame][mip].
    std::vector<std::vector<OwnedImage>> m_bloom_mips;

    std::vector<OwnedImage> m_output_images; //!< Final low-dynamic-range image, one per frame.

    vk::raii::DescriptorSetLayout m_bloom_layout{nullptr}; //!< hdr_input, bloom_src, bloom_dst.
    vk::raii::DescriptorSetLayout m_postprocess_layout{nullptr}; //!< hdr_input, output, bloom mip 0.
    vk::raii::DescriptorPool m_descriptor_pool{nullptr}; //!< Pool every set below comes from.

    /*!
        One descriptor set per bloom dispatch, per frame: the extract, then one per downsample step,
        then one per upsample step. A set binds a fixed pair of mip views, so binding the right set
        is all a dispatch needs to do. Indexed [frame][dispatch].
    */
    std::vector<std::vector<vk::DescriptorSet>> m_bloom_sets;

    std::vector<vk::DescriptorSet> m_postprocess_sets; //!< One per frame in flight.
    vk::raii::DescriptorSets m_all_sets{nullptr}; //!< Owns every set above.

    vk::raii::PipelineLayout m_bloom_pipeline_layout{nullptr}; //!< Shared by all three bloom entry points.
    vk::raii::Pipeline m_extract_pipeline{nullptr}; //!< bloomExtractMain.
    vk::raii::Pipeline m_downsample_pipeline{nullptr}; //!< bloomDownsampleMain.
    vk::raii::Pipeline m_upsample_pipeline{nullptr}; //!< bloomUpsampleMain.

    vk::raii::PipelineLayout m_postprocess_pipeline_layout{nullptr}; //!< Tone mapping push constants.
    vk::raii::Pipeline m_postprocess_pipeline{nullptr}; //!< postprocessMain.
};
