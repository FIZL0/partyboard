# Builds the `pbnet` Rust crate (net/) with cargo and exposes it as the imported static library
# `pbnet::pbnet`. Link it into a target with target_link_libraries(<target> PRIVATE pbnet::pbnet).

find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin" REQUIRED)

# Rust target triple. Empty means the host (desktop builds). Cross builds pick it from the toolchain.
set(PBNET_CARGO_TARGET "" CACHE STRING "Rust target triple for pbnet (empty: host)")
if (NOT PBNET_CARGO_TARGET)
    if (Rust_CARGO_TARGET)
        # Set by the iOS/tvOS/Android CI presets for corrosion (aurora builds nod from source there).
        set(PBNET_CARGO_TARGET ${Rust_CARGO_TARGET})
    elseif (ANDROID)
        if (CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
            set(PBNET_CARGO_TARGET aarch64-linux-android)
        elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
            set(PBNET_CARGO_TARGET armv7-linux-androideabi)
        elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
            set(PBNET_CARGO_TARGET x86_64-linux-android)
        elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
            set(PBNET_CARGO_TARGET i686-linux-android)
        endif ()
    elseif (IOS)
        if (CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator" AND CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
            set(PBNET_CARGO_TARGET x86_64-apple-ios)
        elseif (CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            set(PBNET_CARGO_TARGET aarch64-apple-ios-sim)
        else ()
            set(PBNET_CARGO_TARGET aarch64-apple-ios)
        endif ()
    elseif (APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "^(x86_64|arm64)$")
        if (CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
            set(PBNET_CARGO_TARGET x86_64-apple-darwin)
        else ()
            set(PBNET_CARGO_TARGET aarch64-apple-darwin)
        endif ()
    elseif (APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES ";")
        # A universal build would need one cargo build per arch and a lipo step; not done.
        message(FATAL_ERROR "pbnet: multi-arch CMAKE_OSX_ARCHITECTURES (${CMAKE_OSX_ARCHITECTURES}) is not supported, "
                "build one arch per configure")
    elseif (MSVC)
        # Explicit so a cross build (e.g. an x64 host with an ARM64 vcvars) does not get the host's lib.
        if (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
            set(PBNET_CARGO_TARGET aarch64-pc-windows-msvc)
        elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
            set(PBNET_CARGO_TARGET i686-pc-windows-msvc)
        elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
            set(PBNET_CARGO_TARGET x86_64-pc-windows-msvc)
        endif ()
    endif ()
endif ()

# C stand-in for targets the crate cannot be built for (src/port/pbnet_stub.c: no session, ever).
# tvOS: the std is there on stable, but iroh -> netwatch -> netdev has no tvOS code (netdev 0.46.3,
# `interfaces()` and `libc::in6_ifreq`), so the crate fails to compile; nightly + -Zbuild-std does not
# help (research/journal/2026-10-27-n17-pbnet-tvos). Built everywhere so the desktop build checks it.
add_library(pbnet_stub STATIC ${CMAKE_SOURCE_DIR}/src/port/pbnet_stub.c)
target_include_directories(pbnet_stub PRIVATE ${CMAKE_SOURCE_DIR}/include)
if (NOT APPLE AND NOT WIN32)
    target_link_libraries(pbnet_stub PRIVATE m)
endif ()
if (PBNET_CARGO_TARGET MATCHES "-tvos(-sim)?$")
    set(PBNET_STUB_DEFAULT ON)
else ()
    set(PBNET_STUB_DEFAULT OFF)
endif ()
option(PBNET_STUB "Link the C pbnet stub (no netplay) instead of building the Rust crate" ${PBNET_STUB_DEFAULT})
# Every pbnet_* the header declares must be in the stub, or the stub build fails to link later.
file(STRINGS ${CMAKE_SOURCE_DIR}/include/port/pbnet.h PBNET_HEADER_FNS REGEX "[ *]pbnet_[a-z_]+\\(")
foreach (decl ${PBNET_HEADER_FNS})
    string(REGEX MATCH "pbnet_[a-z_]+\\(" fn "${decl}")
    string(REPLACE "(" "" fn "${fn}")
    file(STRINGS ${CMAKE_SOURCE_DIR}/src/port/pbnet_stub.c defined REGEX "[ *]${fn}\\(")
    if (NOT defined)
        if (PBNET_STUB)
            message(FATAL_ERROR "pbnet: ${fn}() is declared in include/port/pbnet.h but not in src/port/pbnet_stub.c")
        endif ()
        message(WARNING "pbnet: ${fn}() is declared in include/port/pbnet.h but not in src/port/pbnet_stub.c "
                "(the tvOS build needs it there)")
    endif ()
endforeach ()
if (PBNET_STUB)
    message(STATUS "pbnet: PBNET_STUB is on (target: ${PBNET_CARGO_TARGET}), linking the C stub; netplay is not available")
    add_library(pbnet::pbnet ALIAS pbnet_stub)
    return()
endif ()

set(PBNET_DIR ${CMAKE_SOURCE_DIR}/net)
set(PBNET_TARGET_DIR ${CMAKE_BINARY_DIR}/pbnet-cargo)
set(PBNET_CARGO_ARGS build --release --locked --manifest-path ${PBNET_DIR}/Cargo.toml --target-dir ${PBNET_TARGET_DIR})
if (PBNET_CARGO_TARGET)
    list(APPEND PBNET_CARGO_ARGS --target ${PBNET_CARGO_TARGET})
    set(PBNET_OUT_DIR ${PBNET_TARGET_DIR}/${PBNET_CARGO_TARGET}/release)
else ()
    set(PBNET_OUT_DIR ${PBNET_TARGET_DIR}/release)
endif ()
if (MSVC)
    set(PBNET_CARGO_LIB ${PBNET_OUT_DIR}/pbnet.lib)
else ()
    set(PBNET_CARGO_LIB ${PBNET_OUT_DIR}/libpbnet.a)
endif ()

# Android: hand the NDK toolchain to cargo, or cc-rs (ring, blake3 build scripts) looks for an
# `aarch64-linux-android-clang` that the NDK doesn't ship, and rustc links with the host `cc`.
# CMAKE_C_COMPILER is the NDK's plain `clang` (the target is in CMAKE_C_COMPILER_TARGET), which neither
# cc-rs nor rustc's linker call would pass a target to, so use the NDK's per-API wrapper next to it
# (`<triple><api>-clang`, armv7 spelled `armv7a`). Env vars already set at configure time win.
set(PBNET_CARGO_ENV CARGO_TERM_COLOR=never)
if (ANDROID AND PBNET_CARGO_TARGET)
    string(REPLACE "-" "_" pbnet_triple_lower ${PBNET_CARGO_TARGET})
    string(TOUPPER ${pbnet_triple_lower} pbnet_triple_upper)
    string(REGEX REPLACE "^armv7-" "armv7a-" pbnet_clang_triple ${PBNET_CARGO_TARGET})
    get_filename_component(pbnet_ndk_bin ${CMAKE_C_COMPILER} DIRECTORY)
    # The NDK's toolchain file sets ANDROID_PLATFORM_LEVEL (its legacy default leaves CMAKE_SYSTEM_VERSION at 1);
    # CMake's own Android support only sets CMAKE_SYSTEM_VERSION.
    if (ANDROID_PLATFORM_LEVEL)
        set(pbnet_api ${ANDROID_PLATFORM_LEVEL})
    else ()
        set(pbnet_api ${CMAKE_SYSTEM_VERSION})
    endif ()
    if (CMAKE_HOST_WIN32)
        set(pbnet_ndk_cc ${pbnet_ndk_bin}/${pbnet_clang_triple}${pbnet_api}-clang.cmd)
    else ()
        set(pbnet_ndk_cc ${pbnet_ndk_bin}/${pbnet_clang_triple}${pbnet_api}-clang)
    endif ()
    if (NOT EXISTS ${pbnet_ndk_cc})
        message(WARNING "pbnet: NDK compiler wrapper ${pbnet_ndk_cc} not found; set CC_${pbnet_triple_lower}, "
                "AR_${pbnet_triple_lower} and CARGO_TARGET_${pbnet_triple_upper}_LINKER yourself")
    else ()
        foreach (pbnet_pair "CC_${pbnet_triple_lower}=${pbnet_ndk_cc}" "AR_${pbnet_triple_lower}=${CMAKE_AR}"
                "CARGO_TARGET_${pbnet_triple_upper}_LINKER=${pbnet_ndk_cc}")
            string(REGEX MATCH "^[^=]+" pbnet_var "${pbnet_pair}")
            if (NOT DEFINED ENV{${pbnet_var}})
                list(APPEND PBNET_CARGO_ENV "${pbnet_pair}")
            endif ()
        endforeach ()
    endif ()
endif ()

# Always ask cargo (it does its own change tracking); the byproduct is only rewritten when the crate
# changed, so Ninja's restat keeps `dol` from relinking needlessly.
add_custom_target(pbnet_cargo
        COMMAND ${CMAKE_COMMAND} -E env ${PBNET_CARGO_ENV} -- ${CARGO_EXECUTABLE} ${PBNET_CARGO_ARGS}
        BYPRODUCTS ${PBNET_CARGO_LIB}
        WORKING_DIRECTORY ${PBNET_DIR}
        COMMENT "Building pbnet (cargo)"
        USES_TERMINAL
        VERBATIM)

# `dol` already links another Rust staticlib (aurora's nod, libnod.a), and two Rust staticlibs each
# carry their own copy of std: the link fails on duplicate `rust_eh_personality` etc. So pre-link
# the archive into one relocatable object and make every symbol but `pbnet_*` local.
# ELF: also drop the COMDAT groups (only `DW.ref.rust_eh_personality`): left in, the linker keeps our
# copy of the group and nod's reference lands on a symbol we just made local.
if (MSVC)
    # COFF has no `ld -r` + localize, so the cargo .lib is linked as is. That is fine with aurora's
    # Windows default, nod as a DLL: its import lib defines only nod_* stubs. A static nod_static.lib
    # carries its own std and the link fails on `rust_eh_personality` (checked with lld-link; see
    # research/journal/2026-10-27-n13-pbnet-non-linux), so refuse that up front.
    if (AURORA_NOD_LINKAGE STREQUAL "static")
        message(FATAL_ERROR "pbnet: AURORA_NOD_LINKAGE=static is not supported with MSVC (two Rust std copies "
                "in one link, which COFF cannot hide); use the default AURORA_NOD_LINKAGE=shared")
    endif ()
    set(PBNET_LIB ${PBNET_CARGO_LIB})
    add_custom_target(pbnet_lib DEPENDS pbnet_cargo)
else ()
    set(PBNET_OBJ ${PBNET_TARGET_DIR}/pbnet.o)
    set(PBNET_LIB ${PBNET_TARGET_DIR}/libpbnet_hidden.a)
    if (APPLE)
        set(PBNET_PRELINK
                COMMAND xcrun ld -r -all_load -exported_symbol "_pbnet_*" ${PBNET_CARGO_LIB} -o ${PBNET_OBJ})
    else ()
        set(PBNET_PRELINK
                COMMAND ${CMAKE_LINKER} -r --whole-archive ${PBNET_CARGO_LIB} -o ${PBNET_OBJ}
                COMMAND ${CMAKE_OBJCOPY} --wildcard --keep-global-symbol=pbnet_* --remove-section=.group ${PBNET_OBJ})
    endif ()
    add_custom_command(OUTPUT ${PBNET_LIB}
            ${PBNET_PRELINK}
            COMMAND ${CMAKE_COMMAND} -E rm -f ${PBNET_LIB}
            COMMAND ${CMAKE_AR} rcs ${PBNET_LIB} ${PBNET_OBJ}
            DEPENDS pbnet_cargo ${PBNET_CARGO_LIB}
            COMMENT "Hiding pbnet's Rust std symbols"
            VERBATIM)
    add_custom_target(pbnet_lib DEPENDS ${PBNET_LIB})
endif ()

add_library(pbnet STATIC IMPORTED GLOBAL)
set_target_properties(pbnet PROPERTIES IMPORTED_LOCATION ${PBNET_LIB})
add_dependencies(pbnet pbnet_lib)
# System libraries the Rust std and the transport (iroh, rustls-platform-verifier) need
# (`rustc --print native-static-libs`).
if (WIN32)
    set_property(TARGET pbnet PROPERTY INTERFACE_LINK_LIBRARIES ws2_32 userenv ntdll dbghelp bcrypt advapi32 kernel32 iphlpapi crypt32 secur32 ncrypt)
elseif (APPLE)
    set_property(TARGET pbnet PROPERTY INTERFACE_LINK_LIBRARIES "-framework CoreFoundation" "-framework Security" "-framework SystemConfiguration" iconv)
elseif (ANDROID)
    set_property(TARGET pbnet PROPERTY INTERFACE_LINK_LIBRARIES dl log m)
else ()
    set_property(TARGET pbnet PROPERTY INTERFACE_LINK_LIBRARIES pthread dl m util rt)
endif ()
add_library(pbnet::pbnet ALIAS pbnet)
