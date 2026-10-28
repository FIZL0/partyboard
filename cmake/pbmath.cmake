# N91: one libm for the game on every platform. Two peers in lockstep compute the same frames only if every float
# operation gives the same bits; glibc's, musl's (web), bionic's, Apple's and Microsoft's sin/cos/atan2/... differ
# in the last bit (the web build left the desktop's game at m415, N87). So:
#  - src/port/math is musl's math (the web build's libm) under pb_ names, compiled into dol;
#  - the game's C (dol, every REL, aurora's mtx) finds src/port/math/include/math.h first, which includes the
#    system's and then pb_math.h: sin, cosf, atan2, ... go to the copy (sqrt, fmod, floor, ... are exact everywhere
#    and stay the platform's). MSVC (no #include_next) gets pb_math.h forced in instead;
#  - no FMA contraction (-ffp-contract=off): clang fuses a*b+c on arm64 (Android, iOS) by default, x86_64 and wasm32
#    have no FMA to fuse with. MSVC doesn't contract unless asked (/fp:contract).
set(PB_MATH_FILES
        src/port/math/__cos.c src/port/math/__cosdf.c src/port/math/__math_divzero.c src/port/math/__math_invalid.c
        src/port/math/__math_oflow.c src/port/math/__math_uflow.c src/port/math/__math_xflow.c
        src/port/math/__rem_pio2.c src/port/math/__rem_pio2_large.c src/port/math/__rem_pio2f.c src/port/math/__sin.c
        src/port/math/__sindf.c src/port/math/__tan.c src/port/math/__tandf.c src/port/math/acos.c
        src/port/math/acosf.c src/port/math/asin.c src/port/math/asinf.c src/port/math/atan.c src/port/math/atan2.c
        src/port/math/atan2f.c src/port/math/atanf.c src/port/math/cos.c src/port/math/cosf.c src/port/math/exp.c
        src/port/math/exp_data.c src/port/math/log.c src/port/math/log_data.c src/port/math/pow.c
        src/port/math/pow_data.c src/port/math/sin.c src/port/math/sinf.c src/port/math/tan.c src/port/math/tanf.c)

function(pb_game_math target)
    if (MSVC)
        target_compile_options(${target} PRIVATE
                "$<$<COMPILE_LANGUAGE:C>:/FI${CMAKE_SOURCE_DIR}/src/port/math/include/pb_math.h>")
    else ()
        target_include_directories(${target} BEFORE PRIVATE "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/src/port/math/include>")
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=off>")
    endif ()
endfunction()

target_sources(dol PRIVATE ${PB_MATH_FILES})
set_source_files_properties(${PB_MATH_FILES} TARGET_DIRECTORY dol PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/src/port/math")
pb_game_math(dol)
pb_game_math(aurora_mtx)
foreach (rel ${REL_TARGETS})
    if (TARGET ${rel})
        pb_game_math(${rel})
    endif ()
endforeach ()
