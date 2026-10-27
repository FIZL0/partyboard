# Netplay determinism (src/port/netplay.c): while a session is active, OSGetTime/OSGetTick must return
# frame-derived time and PADRead the scripted/lockstep pads. aurora defines these in its static libs, so
# instead of patching aurora every call from dol (game and port objects) and from every REL is sent to
# netplay.c's __wrap_* functions, which pass through to aurora offline. Two routes:
#
# - wrap: the linker's --wrap. ELF only (GNU ld, lld): Linux and Android. Also catches calls from aurora
#   objects, though aurora calls none of the three outside OSTime.cpp itself.
# - define: compile definitions OSGetTime=__wrap_OSGetTime etc. on the dol and REL sources, so the calls
#   (and the declarations in aurora's headers) are renamed by the preprocessor. Works with any compiler
#   and linker: ld64 (macOS/iOS/tvOS) and MSVC/MinGW. aurora is not compiled with these definitions, so
#   its own OSGetTime stays the real one, and netplay.c #undefs them to call it.
#
# PB_NETPLAY_HOOK picks the route; the default is wrap on ELF platforms and define elsewhere. Building
# with -DPB_NETPLAY_HOOK=define on Linux checks the Apple/Windows route.
if (APPLE OR WIN32)
    set(PB_NETPLAY_HOOK_DEFAULT define)
else ()
    set(PB_NETPLAY_HOOK_DEFAULT wrap)
endif ()
set(PB_NETPLAY_HOOK ${PB_NETPLAY_HOOK_DEFAULT} CACHE STRING "How netplay redirects OSGetTime/OSGetTick/PADRead: wrap (ELF ld --wrap) or define (preprocessor)")
set_property(CACHE PB_NETPLAY_HOOK PROPERTY STRINGS wrap define)
if (PB_NETPLAY_HOOK STREQUAL wrap AND (APPLE OR WIN32))
    message(FATAL_ERROR "netplay: PB_NETPLAY_HOOK=wrap needs an ELF linker, use define on this platform")
endif ()

if (PB_NETPLAY_HOOK STREQUAL wrap)
    target_compile_definitions(dol PRIVATE PB_NETPLAY_WRAP)
    target_link_options(dol PRIVATE "LINKER:--wrap=OSGetTime,--wrap=OSGetTick,--wrap=PADRead")
    foreach (rel ${REL_TARGETS})
        if (TARGET ${rel})
            target_link_options(${rel} PRIVATE "LINKER:--wrap=OSGetTime,--wrap=OSGetTick")
        endif ()
    endforeach ()
elseif (PB_NETPLAY_HOOK STREQUAL define)
    target_compile_definitions(dol PRIVATE PB_NETPLAY_WRAP PB_NETPLAY_DEFINE
        OSGetTime=__wrap_OSGetTime OSGetTick=__wrap_OSGetTick PADRead=__wrap_PADRead)
    foreach (rel ${REL_TARGETS})
        if (TARGET ${rel})
            target_compile_definitions(${rel} PRIVATE OSGetTime=__wrap_OSGetTime OSGetTick=__wrap_OSGetTick)
        endif ()
    endforeach ()
else ()
    message(FATAL_ERROR "netplay: PB_NETPLAY_HOOK must be wrap or define, not '${PB_NETPLAY_HOOK}'")
endif ()
message(STATUS "netplay: OSGetTime/OSGetTick/PADRead hook: ${PB_NETPLAY_HOOK}")
