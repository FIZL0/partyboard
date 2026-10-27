// The user dir for netplay's C code (src/port/netplay_save.c: online saves under <user dir>/online).
#include <filesystem>
#include <string>

#include "port/main.h"

extern "C" const char *netplay_user_dir(void)
{
    static std::string dir;
    const auto u8 = PartyBoard_ConfigPath.u8string();
    dir.assign(reinterpret_cast<const char *>(u8.c_str()), u8.size());
    return dir.c_str();
}
