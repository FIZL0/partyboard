# Netplay determinism (src/port/netplay.c): while a session is active, OSGetTime/OSGetTick must return
# frame-derived time and PADRead the scripted/lockstep pads. aurora defines these in its static libs, so
# instead of patching aurora the calls are redirected with the linker's --wrap: in dol (game, port and
# aurora objects) and in every REL, which import the __wrap_ symbols from dol.
#
# --wrap is an ELF linker feature (GNU ld, lld): Linux and Android. Elsewhere netplay time stays the
# wall clock, which is not deterministic.
if (NOT APPLE AND NOT WIN32)
    target_compile_definitions(dol PRIVATE PB_NETPLAY_WRAP)
    target_link_options(dol PRIVATE "LINKER:--wrap=OSGetTime,--wrap=OSGetTick,--wrap=PADRead")
    foreach (rel ${REL_TARGETS})
        if (TARGET ${rel})
            target_link_options(${rel} PRIVATE "LINKER:--wrap=OSGetTime,--wrap=OSGetTick")
        endif ()
    endforeach ()
else ()
    message(STATUS "netplay: no linker --wrap on this platform, netplay time is not deterministic")
endif ()
