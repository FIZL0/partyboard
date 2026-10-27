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

// N47: whole-file writes for the local party autosave (netplay_autosave.c). Temporary file + rename, so a crash
// mid-write keeps the previous file; std::filesystem::rename replaces the target on Windows too.
#include <cstdint>
#include <cstdio>
#include <system_error>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
std::filesystem::path u8path(const char *s)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s)));
}

FILE *open(const std::filesystem::path &p, bool write)
{
#ifdef _WIN32
    return _wfopen(p.c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(p.c_str(), write ? "wb" : "rb");
#endif
}
} // namespace

extern "C" int netplay_file_replace(const char *dir, const char *name, const void *data, uint32_t size)
{
    std::error_code ec;
    const auto d = u8path(dir);
    std::filesystem::create_directories(d, ec);
    const auto path = d / u8path(name);
    auto tmp = path;
    tmp += ".tmp";
    FILE *f = open(tmp, true);
    if (f == nullptr) {
        return 0;
    }
    bool ok = std::fwrite(data, 1, size, f) == size && std::fflush(f) == 0;
#ifdef _WIN32
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;
#endif
    ok = std::fclose(f) == 0 && ok;
    if (ok) {
        std::filesystem::rename(tmp, path, ec);
        ok = !ec;
    }
    if (!ok) {
        std::filesystem::remove(tmp, ec);
    }
    return ok ? 1 : 0;
}

extern "C" int netplay_file_read(const char *path, void *buf, uint32_t cap)
{
    FILE *f = open(u8path(path), false);
    if (f == nullptr) {
        return -1;
    }
    size_t n = std::fread(buf, 1, cap, f);
    bool more = std::fgetc(f) != EOF;
    std::fclose(f);
    return more ? -1 : static_cast<int>(n);
}

extern "C" int netplay_file_remove(const char *path)
{
    std::error_code ec;
    return std::filesystem::remove(u8path(path), ec) ? 1 : 0;
}
