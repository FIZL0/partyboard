# N87: browser build (Emscripten), before aurora is added. The rest is cmake/web.cmake; tools/web.sh drives both.
#
# No dlopen in the browser: dol is a static library and every REL an object library, which cmake/web.cmake
# prelinks into one object per REL and links into partyboard.
set(PB_DOL_LIBRARY_TYPE STATIC)
set(PB_REL_LIBRARY_TYPE OBJECT)
# fmt 11.1's format.h calls malloc/free without <cstdlib>, which Emscripten's libc++ doesn't pull in by itself.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-includecstdlib>)
# The link uses wasm exceptions (Rust's std, cmake/web.cmake), and Emscripten's JS setjmp/longjmp can't be mixed
# with them: libpng's and freetype's setjmp use wasm's too. Nothing that calls setjmp yields to the browser.
add_compile_options(-sSUPPORT_LONGJMP=wasm)
add_link_options(-sSUPPORT_LONGJMP=wasm)
