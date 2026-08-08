add_executable(${PROJECT_NAME}
    main.cpp
    volk.cpp
    instance.cpp
    device.cpp
    surface.cpp
    swapchain.cpp
    tracer.cpp
    world.cpp
    acoustics.cpp
    acoustic_tracer.cpp
    memory_arena.cpp
    postprocess.cpp
    geometry.cpp
    profiler.cpp
    spectator.cpp
    program_library.cpp
    roster.cpp
    cinematic.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# SYSTEM so that warnings from the Vulkan SDK's own headers cannot fail this build under /WX and
# -Werror. A future SDK release is not something this repository can fix.
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${Vulkan_INCLUDE_DIRS})

# Vulkan-specific definitions — scoped to this target only.
# VULKAN_HPP_NO_STRUCT_CONSTRUCTORS is defined here rather than per-header so that every
# translation unit sees the same vulkan-hpp struct definitions (defining it in only some
# headers is an ODR hazard that links silently and misbehaves at runtime).
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VK_NO_PROTOTYPES
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_XCB_KHR>
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    window
    signals
    logging
    math
    bvh
    program_abi
    ${CMAKE_DL_LIBS}
)

# Slang shader compilation — slangc and spirv-val come from the Vulkan SDK.
find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
find_program(SPIRV_VAL_EXECUTABLE spirv-val HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

# Slang modules that entry-point shaders `import` rather than compile on their own. They produce no
# .spv of their own and are listed here only so that editing one rebuilds the shaders built from it.
# A module missing from this list compiles fine and then goes stale silently, which is the whole
# reason the list exists.
#
# Every shader depends on every module here rather than on the ones it actually imports, so editing
# one module rebuilds all three. That is deliberate: the alternative is a per-shader import list,
# which is one more thing to forget to update, and the failure mode of forgetting is a stale binary
# rather than a slow build. Three shaders take about a second between them.
set(SLANG_SHARED_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/grid_bvh.slang
)

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
            -warnings-as-errors all
            # E41035, possible use of uninitialized variable, is disabled and only this one is.
            # Slang 2026 reports it against both traversal stacks, and in both the array is a
            # bounded stack whose invariant the analysis cannot see: the size only ever
            # increments after a store at that index and only decrements before a read of it, so
            # every read is of an element already written. Satisfying it means initialising the
            # whole array, which measured +6% on an RTX 4090 and +29% on an Intel integrated
            # part - thirty stores per traceGeometry call, per instance, per ray. Paying that on
            # the weakest device the project targets, to silence a false positive, is the wrong
            # trade. Every other warning stays fatal.
            -warnings-disable 41035
            -I ${CMAKE_CURRENT_SOURCE_DIR}
            ${entry_args}
            -o ${SHADER_OUTPUT_DIR}/${name}.spv
        COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/${name}.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source} ${SLANG_SHARED_MODULES}
        COMMENT "Compiling ${source} -> ${name}.spv (with validation)"
        VERBATIM
    )
endfunction()

# Phase 2 compute ray tracer — the renderer proper.
add_slang_shader(trace trace.slang traceMain)

# Phase 5 acoustic gather — the same hierarchy, a different sense.
add_slang_shader(acoustics acoustics.slang acousticMain)

# Phase 4 post-processing.
add_slang_shader(postprocess postprocess.slang postprocessMain)

# All three bloom entry points go into one module. They share a descriptor set layout and a
# pipeline layout, so the host creates three pipelines from a single shader module rather than
# loading three files that would each contain a third of the same code.
add_slang_shader(bloom bloom_downsample.slang bloomExtractMain bloomDownsampleMain bloomUpsampleMain)

set(SHADER_MODULES
    trace
    acoustics
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

# Install rules. The release workflow stages the archive with `cmake --install`, so that the
# knowledge of where the binary and its shaders actually land lives here rather than being
# duplicated as a hand-written path in a YAML file — which is how the release archive came to be
# assembled from a directory that Ninja Multi-Config never writes.
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
install(FILES ${SHADER_SPV_FILES} DESTINATION .)

add_subdirectory(tests)
