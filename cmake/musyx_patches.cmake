# Changes to extern/musyx that upstream (AxioDL/musyx) doesn't have. As cmake/aurora_patches.cmake: the submodule stays
# at the upstream commit, each patch in cmake/patches/musyx is applied to its working tree at configure time and
# skipped when it is already in. A patch that does neither stops the configure.
#
# - 0001-snd3d-no-listener-pan (N125): CalcEmitter with no listener (msm removes it between overlays) left the pans
#   unwritten, and s3dHandle passed its uninitialized locals to SetFXParameters' clip127((1 +- pan) * 64.f), a float
#   -> u8 conversion of garbage (inf, NaN, 1e24: undefined). They are 0 now. Audio thread only, the voice's volume
#   is 0 there, and the game never reads it back (netplay's msm answers are stubbed, src/port/msm.c).
find_package(Git REQUIRED)
file(GLOB PB_MUSYX_PATCHES ${CMAKE_SOURCE_DIR}/cmake/patches/musyx/*.patch)
list(SORT PB_MUSYX_PATCHES)
foreach (patch IN LISTS PB_MUSYX_PATCHES)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${patch})
    execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${patch}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extern/musyx RESULT_VARIABLE reverse OUTPUT_QUIET ERROR_QUIET)
    if (reverse EQUAL 0)
        continue()
    endif ()
    execute_process(COMMAND ${GIT_EXECUTABLE} apply ${patch}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extern/musyx RESULT_VARIABLE result ERROR_VARIABLE error)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "extern/musyx: ${patch} doesn't apply:\n${error}")
    endif ()
    message(STATUS "extern/musyx: applied ${patch}")
endforeach ()
