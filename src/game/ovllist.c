#include "game/dvd.h"
#include "game/object.h"

#ifdef TARGET_PC

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#ifdef _WIN32
#define DLL(name) { #name ".dll", 0 },
#elif defined(__APPLE__) && TARGET_OS_IPHONE
/* N86: dlopen does not search the app's Frameworks for a bare name (tools/ios-ipa.sh puts the RELs there) */
#define DLL(name) { "@executable_path/Frameworks/" #name ".dylib", 0 },
#elif defined(__APPLE__)
#define DLL(name) { #name ".dylib", 0 },
#else
#define DLL(name) { #name ".so", 0 },
#endif

#else

#define DLL(name) { "dll/" #name ".rel", 0 },
#endif

FileListEntry _ovltbl[] = {
    #include "ovl_table.h"
    { NULL, -1 }
};

#undef DLL
