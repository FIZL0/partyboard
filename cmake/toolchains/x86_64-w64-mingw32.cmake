# N138: Windows x86_64 cross build with MinGW-w64 GCC (the windows-mingw preset; tools/build.sh windows).
# The runtime (libgcc, libstdc++, winpthread) is linked statically into every binary, so no MinGW DLL ships
# beside partyboard.exe. CMAKE_SYSTEM_PROCESSOR is AMD64, as on a Windows host, so aurora picks its prebuilt
# windows-amd64 Dawn, SDL3 and nod packages: their DLLs export C APIs only, which MinGW links like MSVC does.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(PB_MINGW_TRIPLE x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${PB_MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${PB_MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER ${PB_MINGW_TRIPLE}-windres)
set(CMAKE_AR ${PB_MINGW_TRIPLE}-gcc-ar)
set(CMAKE_RANLIB ${PB_MINGW_TRIPLE}-gcc-ranlib)

set(CMAKE_FIND_ROOT_PATH /usr/${PB_MINGW_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# mingw-lib: linker scripts standing in for the MSVC library names aurora links (comsuppw, DXGI).
set(PB_MINGW_STATIC_RUNTIME "-L${CMAKE_CURRENT_LIST_DIR}/mingw-lib -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${PB_MINGW_STATIC_RUNTIME}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${PB_MINGW_STATIC_RUNTIME}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${PB_MINGW_STATIC_RUNTIME}")

# pbnet (cmake/pbnet.cmake): the Rust crate for the GNU ABI, linked by the same GCC
set(PBNET_CARGO_TARGET x86_64-pc-windows-gnu CACHE STRING "Rust target triple for pbnet (empty: host)")

# mingw-include: MSVC spellings of SDK headers aurora includes (Windows.h, Wbemidl.h, shlobj_core.h), searched last.
# Windows 10 API level, without the 10.0.22621 (NI) WinRT headers: with them, abseil 20240722's time zone lookup
# includes MinGW's windows.globalization.h, which does not compile (redefinition of IReference<unsigned char>).
set(PB_MINGW_CFLAGS "-idirafter ${CMAKE_CURRENT_LIST_DIR}/mingw-include -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000000")
set(CMAKE_C_FLAGS_INIT "${PB_MINGW_CFLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${PB_MINGW_CFLAGS}")
