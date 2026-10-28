# Changes to extern/aurora that upstream (encounter/aurora) doesn't have yet. The submodule stays at the
# upstream commit; each patch in cmake/patches/aurora is applied to its working tree at configure time
# and skipped when it is already in (applied before, or merged upstream after a submodule bump). A
# patch that does neither stops the configure: rebase it onto the new aurora or drop it.
#
# - 0001-rmlui-code-input-type (N37): InputType::Code, a soft keyboard without autocorrect or
#   auto-capitalization, for the Online menu's Code and Relay URL fields.
# - 0002-gfx-depth-copy-formats (N54): GXCopyTex conversion pipelines for Z24X8, Z8, Z4, Z8M, Z8L and Z16L
#   (only Z16 had one). A scaled Z24X8 copy (m419) went through the color blit with the depth view bound;
#   with skip_validation that faulted the GPU (radv GPUVM fault, device lost).
# - 0003-gfx-frame-capture (N60): aurora_capture_frame(path, delay_frames) saves the game frame (the EFB as
#   presented, before RmlUi/ImGui) to a PNG via a GPU readback, so tests get screenshots of hidden windows.
# - 0004-emscripten (N87): the browser build. Dawn is emdawnwebgpu (the browser's WebGPU), the surface SDL's
#   canvas, no render/pipeline threads and no pipeline cache, aurora_update() yields to the browser once per frame
#   (Asyncify), imgui's wgpu backend built in Dawn mode. Everything is under EMSCRIPTEN / __EMSCRIPTEN__ but two
#   size_t casts wasm32 needs.
find_package(Git REQUIRED)
file(GLOB PB_AURORA_PATCHES ${CMAKE_SOURCE_DIR}/cmake/patches/aurora/*.patch)
list(SORT PB_AURORA_PATCHES)
foreach (patch IN LISTS PB_AURORA_PATCHES)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${patch})
    execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${patch}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extern/aurora RESULT_VARIABLE reverse OUTPUT_QUIET ERROR_QUIET)
    if (reverse EQUAL 0)
        continue()
    endif ()
    execute_process(COMMAND ${GIT_EXECUTABLE} apply ${patch}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extern/aurora RESULT_VARIABLE result ERROR_VARIABLE error)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "extern/aurora: ${patch} doesn't apply:\n${error}")
    endif ()
    message(STATUS "extern/aurora: applied ${patch}")
endforeach ()
