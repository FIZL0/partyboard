# Builds the `pbnet` Rust crate (net/) with cargo and exposes it as the imported static library
# `pbnet::pbnet`. Link it into a target with target_link_libraries(<target> PRIVATE pbnet::pbnet).

find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin" REQUIRED)

# Rust target triple. Empty means the host (desktop builds). Cross builds pick it from the toolchain.
set(PBNET_CARGO_TARGET "" CACHE STRING "Rust target triple for pbnet (empty: host)")
if (NOT PBNET_CARGO_TARGET)
    if (ANDROID)
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
        if (CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
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
    endif ()
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

# Always ask cargo (it does its own change tracking); the byproduct is only rewritten when the crate
# changed, so Ninja's restat keeps `dol` from relinking needlessly.
add_custom_target(pbnet_cargo
        COMMAND ${CMAKE_COMMAND} -E env CARGO_TERM_COLOR=never -- ${CARGO_EXECUTABLE} ${PBNET_CARGO_ARGS}
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
    # Not handled yet (see research/journal/2026-10-27-n1-pbnet-crate): link the cargo output as is.
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
# System libraries the Rust std needs (`rustc --print native-static-libs`).
if (WIN32)
    set_property(TARGET pbnet PROPERTY INTERFACE_LINK_LIBRARIES ws2_32 userenv ntdll bcrypt advapi32 kernel32)
elseif (APPLE)
    set_property(TARGET pbnet PROPERTY INTERFACE_LINK_LIBRARIES "-framework CoreFoundation" iconv)
elseif (ANDROID)
    set_property(TARGET pbnet PROPERTY INTERFACE_LINK_LIBRARIES dl log m)
else ()
    set_property(TARGET pbnet PROPERTY INTERFACE_LINK_LIBRARIES pthread dl m)
endif ()
add_library(pbnet::pbnet ALIAS pbnet)
