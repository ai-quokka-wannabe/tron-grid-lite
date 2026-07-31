add_executable(${PROJECT_NAME}
    main.cpp
    volk.cpp
    vma.cpp
    instance.cpp
    device.cpp
    surface.cpp
    swapchain.cpp
    allocator.cpp
    tracer.cpp
    postprocess.cpp
    geometry.cpp
    profiler.cpp
    spectator.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

# Vulkan-specific definitions — scoped to this target only.
# VULKAN_HPP_NO_STRUCT_CONSTRUCTORS is defined here rather than per-header so that every
# translation unit sees the same vulkan-hpp struct definitions (defining it in only some
# headers is an ODR hazard that links silently and misbehaves at runtime).
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VK_NO_PROTOTYPES
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
    $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_XCB_KHR>
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    window
    signals
    logging
    math
    bvh
    ${CMAKE_DL_LIBS}
)

# Slang shader compilation — slangc and spirv-val come from the Vulkan SDK.
find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
find_program(SPIRV_VAL_EXECUTABLE spirv-val HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

#[[
    Compiles one Slang source into a single SPIR-V module and validates it.

    name    — output module name (produces <name>.spv)
    source  — Slang source file, relative to this directory
    ARGN    — one or more entry point names
]]
function(add_slang_shader name source)
    set(entry_args "")
    foreach(entry IN LISTS ARGN)
        list(APPEND entry_args -entry ${entry})
    endforeach()

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT_DIR}/${name}.spv
        COMMAND ${SLANGC_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/${source}
            -target spirv
            -profile spirv_1_4
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -fvk-invert-y
            -warnings-as-errors all
            ${entry_args}
            -o ${SHADER_OUTPUT_DIR}/${name}.spv
        COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/${name}.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        COMMENT "Compiling ${source} -> ${name}.spv (with validation)"
        VERBATIM
    )
endfunction()

# Phase 2 compute ray tracer — the renderer proper.
add_slang_shader(trace trace.slang traceMain)

# Phase 4 post-processing.
add_slang_shader(postprocess postprocess.slang postprocessMain)

# All three bloom entry points go into one module. They share a descriptor set layout and a
# pipeline layout, so the host creates three pipelines from a single shader module rather than
# loading three files that would each contain a third of the same code.
add_slang_shader(bloom bloom_downsample.slang bloomExtractMain bloomDownsampleMain bloomUpsampleMain)

set(SHADER_MODULES
    trace
    postprocess
    bloom
)

set(SHADER_SPV_FILES "")
foreach(module IN LISTS SHADER_MODULES)
    list(APPEND SHADER_SPV_FILES ${SHADER_OUTPUT_DIR}/${module}.spv)
endforeach()

# ALL ensures shaders build even when the executable itself is up to date.
add_custom_target(shaders ALL DEPENDS ${SHADER_SPV_FILES})
add_dependencies(${PROJECT_NAME} shaders)

# Copy compiled shaders next to the executable. An ALL target with an explicit dependency on
# `shaders` always runs after compilation, unlike POST_BUILD which only fires when the
# executable is relinked. copy_if_different makes this a no-op when nothing changed.
set(COPY_COMMANDS "")
foreach(module IN LISTS SHADER_MODULES)
    list(APPEND COPY_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SHADER_OUTPUT_DIR}/${module}.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/${module}.spv
    )
endforeach()

add_custom_target(copy_shaders ALL
    ${COPY_COMMANDS}
    COMMENT "Copying shaders to output directory"
    VERBATIM
)
add_dependencies(copy_shaders shaders)
