// N109: log file + crash handler. See include/port/log_file.hpp.
//
// Everything the game logs ends up on stdout/stderr (aurora's log callback, OSReport, printf, pbnet's eprintln), so
// the log file is a tee at the fd level: fds 1 and 2 become pipes, and a thread per pipe copies what it reads to the
// original fd (the console, unchanged) and to the log file with write(2), so nothing waits in a buffer when the
// process dies. The crash handler waits for the pipes to drain, then writes its report straight to both.

#include "port/log_file.hpp"

#include "partyboard_version.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_system.h>
#include <aurora/lib/logging.hpp>
#include <dolphin/dvd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32)
#define PB_LOG_WIN32 1
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#include <tlhelp32.h>
#elif !defined(__EMSCRIPTEN__)
#define PB_LOG_POSIX 1
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <execinfo.h>
#include <sys/ucontext.h>
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#include <link.h>
#include <sys/system_properties.h>
#include <ucontext.h>
#include <unwind.h>
#else
#include <execinfo.h>
#include <link.h>
#include <ucontext.h>
#endif
#endif

namespace partyboard::logfile {
namespace {

aurora::Module Log("partyboard::log");

constexpr const char* kCrashMarker = "*** PartyBoard crashed";
constexpr int kKeptRuns = 3; // partyboard.1.log .. partyboard.3.log, besides this run's

std::filesystem::path g_logsDir;
std::filesystem::path g_lastCrash;
bool g_writing = false;

std::filesystem::path calc_logs_dir(const std::filesystem::path& userDir)
{
#if defined(__ANDROID__)
    // The app's external files dir (Android/data/<package>/files): readable over USB and by file managers without
    // root, unlike the internal user dir.
    if (const char* ext = SDL_GetAndroidExternalStoragePath(); ext != nullptr && *ext) {
        return std::filesystem::path(ext) / "logs";
    }
#endif
    if (userDir.empty()) {
        return {};
    }
    return userDir / "logs";
}

std::filesystem::path run_log(int n)
{
    return g_logsDir / (n == 0 ? std::string("partyboard.log") : "partyboard." + std::to_string(n) + ".log");
}

bool log_ended_in_crash(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0) {
        return false;
    }
    const std::streamoff tail = std::min<std::streamoff>(size, 256 * 1024);
    in.seekg(size - tail);
    std::string buf(static_cast<size_t>(tail), '\0');
    in.read(buf.data(), tail);
    return buf.find(kCrashMarker) != std::string::npos;
}

void rotate_logs()
{
    std::error_code ec;
    std::filesystem::remove(run_log(kKeptRuns), ec);
    for (int n = kKeptRuns - 1; n >= 0; n--) {
        if (std::filesystem::exists(run_log(n), ec)) {
            std::filesystem::rename(run_log(n), run_log(n + 1), ec);
        }
    }
}

#if PB_LOG_POSIX || PB_LOG_WIN32

#if PB_LOG_WIN32
int sys_write(int fd, const void* buf, size_t len) { return _write(fd, buf, static_cast<unsigned>(len)); }
int sys_read(int fd, void* buf, size_t len) { return _read(fd, buf, static_cast<unsigned>(len)); }
int sys_dup(int fd) { return _dup(fd); }
int sys_dup2(int from, int to) { return _dup2(from, to); }
int sys_close(int fd) { return _close(fd); }
#else
ssize_t sys_write(int fd, const void* buf, size_t len) { return write(fd, buf, len); }
ssize_t sys_read(int fd, void* buf, size_t len) { return read(fd, buf, len); }
int sys_dup(int fd) { return dup(fd); }
int sys_dup2(int from, int to) { return dup2(from, to); }
int sys_close(int fd) { return close(fd); }
#endif

int g_logFd = -1;
// The console fds as they were before the tee (stderr is where the crash report goes besides the log).
int g_consoleErr = 2;

struct Tee {
    int readFd = -1;
    int consoleFd = -1;
    std::atomic<bool> busy { false };
};
Tee g_tees[2];
int g_teeCount = 0;
std::mutex g_logMutex;

void write_all(int fd, const char* buf, size_t len)
{
    while (len > 0) {
        const auto n = sys_write(fd, buf, len);
        if (n < 0) {
#if PB_LOG_POSIX
            if (errno == EINTR) {
                continue;
            }
#endif
            return;
        }
        buf += n;
        len -= static_cast<size_t>(n);
    }
}

void tee_thread(Tee* tee)
{
    char buf[4096];
    for (;;) {
        const auto n = sys_read(tee->readFd, buf, sizeof buf);
        if (n < 0) {
#if PB_LOG_POSIX
            if (errno == EINTR) {
                continue;
            }
#endif
            return;
        }
        if (n == 0) {
            return;
        }
        tee->busy = true;
        if (tee->consoleFd >= 0) {
            write_all(tee->consoleFd, buf, static_cast<size_t>(n));
        }
        {
            std::lock_guard lock(g_logMutex);
            write_all(g_logFd, buf, static_cast<size_t>(n));
        }
        tee->busy = false;
    }
}

bool make_pipe(int fds[2])
{
#if PB_LOG_WIN32
    return _pipe(fds, 64 * 1024, _O_BINARY | _O_NOINHERIT) == 0;
#else
    if (pipe(fds) != 0) {
        return false;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
#endif
}

bool same_file(int a, int b)
{
#if PB_LOG_POSIX
    struct stat sa {}, sb {};
    return fstat(a, &sa) == 0 && fstat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#else
    (void)a;
    (void)b;
    return false;
#endif
}

// Sends fds 1 and 2 through the tee. When both are the same file (2>&1, a terminal), one pipe keeps their order.
void start_tee()
{
    fflush(stdout);
    fflush(stderr);
    const int fds[2] = { 1, 2 };
    const bool shared = same_file(1, 2);
    g_teeCount = shared ? 1 : 2;
    for (int i = 0; i < g_teeCount; i++) {
        int p[2];
        if (!make_pipe(p)) {
            g_teeCount = i;
            break;
        }
        g_tees[i].readFd = p[0];
        g_tees[i].consoleFd = sys_dup(fds[i]);
        if (shared) {
            sys_dup2(p[1], 1);
            sys_dup2(p[1], 2);
        } else {
            sys_dup2(p[1], fds[i]);
        }
        sys_close(p[1]);
#if PB_LOG_POSIX
        if (g_tees[i].consoleFd >= 0) {
            fcntl(g_tees[i].consoleFd, F_SETFD, FD_CLOEXEC);
        }
#endif
        std::thread(tee_thread, &g_tees[i]).detach();
    }
    if (g_teeCount > 0) {
        g_consoleErr = g_tees[g_teeCount - 1].consoleFd;
    }
#if PB_LOG_WIN32
    // Rust's eprintln and other non-CRT writers use the Win32 std handles, not fds 1 and 2.
    SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(1)));
    SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(2)));
#endif
    // A pipe would make stdout fully buffered: keep it per line, so a crash keeps the tail.
    setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

// Waits (up to ~300 ms) until the tee threads have written out everything already in the pipes. Async-signal-safe.
void drain_tee()
{
    for (int tries = 0; tries < 30; tries++) {
        bool idle = true;
        for (int i = 0; i < g_teeCount; i++) {
            int pending = 0;
#if PB_LOG_POSIX
            if (ioctl(g_tees[i].readFd, FIONREAD, &pending) != 0) {
                pending = 0;
            }
#else
            DWORD avail = 0;
            if (PeekNamedPipe(reinterpret_cast<HANDLE>(_get_osfhandle(g_tees[i].readFd)), nullptr, 0, nullptr, &avail,
                    nullptr)) {
                pending = static_cast<int>(avail);
            }
#endif
            if (pending > 0 || g_tees[i].busy) {
                idle = false;
            }
        }
        if (idle) {
            return;
        }
#if PB_LOG_POSIX
        const timespec ts { 0, 10 * 1000 * 1000 };
        nanosleep(&ts, nullptr);
#else
        Sleep(10);
#endif
    }
}

void stop_tee()
{
    fflush(stdout);
    fflush(stderr);
    drain_tee();
}

// ---- crash report: fixed buffers only, no allocation ----

struct Line {
    char buf[1024];
    size_t n = 0;

    Line& str(const char* s)
    {
        while (s != nullptr && *s && n < sizeof buf - 1) {
            buf[n++] = *s++;
        }
        return *this;
    }
    Line& hex(uintptr_t v)
    {
        char tmp[2 + sizeof(uintptr_t) * 2];
        size_t len = 0;
        do {
            tmp[len++] = "0123456789abcdef"[v & 0xF];
            v >>= 4;
        } while (v != 0);
        str("0x");
        while (len > 0 && n < sizeof buf - 1) {
            buf[n++] = tmp[--len];
        }
        return *this;
    }
    Line& dec(long long v)
    {
        char tmp[24];
        size_t len = 0;
        const bool neg = v < 0;
        unsigned long long u = neg ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
        do {
            tmp[len++] = static_cast<char>('0' + u % 10);
            u /= 10;
        } while (u != 0);
        if (neg) {
            str("-");
        }
        while (len > 0 && n < sizeof buf - 1) {
            buf[n++] = tmp[--len];
        }
        return *this;
    }
    void end()
    {
        buf[n++] = '\n';
        if (g_logFd >= 0) {
            write_all(g_logFd, buf, n);
        }
        if (g_consoleErr >= 0) {
            write_all(g_consoleErr, buf, n);
        }
#if defined(__ANDROID__)
        buf[n - 1] = '\0';
        __android_log_write(ANDROID_LOG_FATAL, "PartyBoard", buf);
#endif
        n = 0;
    }
};

#endif // PB_LOG_POSIX || PB_LOG_WIN32

#if PB_LOG_POSIX

constexpr int kSignals[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE, SIGTRAP };
struct sigaction g_oldActions[std::size(kSignals)];
std::atomic<int> g_inHandler { 0 };
alignas(16) char g_altStack[128 * 1024];

const char* signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGABRT:
        return "SIGABRT";
    case SIGILL:
        return "SIGILL";
    case SIGFPE:
        return "SIGFPE";
    case SIGTRAP:
        return "SIGTRAP";
    default:
        return "?";
    }
}

struct Registers {
    uintptr_t pc = 0, sp = 0, lr = 0;
};

Registers registers_of(void* uctx)
{
    Registers r;
    if (uctx == nullptr) {
        return r;
    }
    auto* uc = static_cast<ucontext_t*>(uctx);
#if defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
#if defined(__darwin_arm_thread_state64_get_pc)
    r.pc = __darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
    r.lr = __darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
    r.sp = __darwin_arm_thread_state64_get_sp(uc->uc_mcontext->__ss);
#else
    r.pc = uc->uc_mcontext->__ss.__pc;
    r.lr = uc->uc_mcontext->__ss.__lr;
    r.sp = uc->uc_mcontext->__ss.__sp;
#endif
#elif defined(__x86_64__)
    r.pc = uc->uc_mcontext->__ss.__rip;
    r.sp = uc->uc_mcontext->__ss.__rsp;
#endif
#elif defined(__aarch64__)
    r.pc = uc->uc_mcontext.pc;
    r.sp = uc->uc_mcontext.sp;
    r.lr = uc->uc_mcontext.regs[30];
#elif defined(__x86_64__)
    r.pc = uc->uc_mcontext.gregs[REG_RIP];
    r.sp = uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__i386__)
    r.pc = uc->uc_mcontext.gregs[REG_EIP];
    r.sp = uc->uc_mcontext.gregs[REG_ESP];
#elif defined(__arm__)
    r.pc = uc->uc_mcontext.arm_pc;
    r.sp = uc->uc_mcontext.arm_sp;
    r.lr = uc->uc_mcontext.arm_lr;
#else
    (void)uc;
#endif
    return r;
}

#if defined(__ANDROID__)
// Bionic has no backtrace() before API 33.
struct UnwindState {
    void** frames;
    int count;
    int max;
};

_Unwind_Reason_Code unwind_frame(_Unwind_Context* ctx, void* arg)
{
    auto* st = static_cast<UnwindState*>(arg);
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc != 0) {
        if (st->count >= st->max) {
            return _URC_END_OF_STACK;
        }
        st->frames[st->count++] = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

int capture_backtrace(void** frames, int max)
{
    UnwindState st { frames, 0, max };
    _Unwind_Backtrace(unwind_frame, &st);
    return st.count;
}
#else
int capture_backtrace(void** frames, int max) { return backtrace(frames, max); }
#endif

void write_frame(int i, void* addr)
{
    Line line;
    line.str("  #").dec(i).str(" ").hex(reinterpret_cast<uintptr_t>(addr));
    Dl_info info {};
    if (dladdr(addr, &info) != 0 && info.dli_fname != nullptr) {
        const char* base = std::strrchr(info.dli_fname, '/');
        line.str(" ").str(base != nullptr ? base + 1 : info.dli_fname).str("+");
        line.hex(reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(info.dli_fbase));
        if (info.dli_sname != nullptr) {
            line.str(" (").str(info.dli_sname).str("+");
            line.hex(reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(info.dli_saddr)).str(")");
        }
    }
    line.end();
}

#if defined(__APPLE__)
void write_images()
{
    Line line;
    line.str("*** loaded images (header address, slide, path):").end();
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        line.str("  ").hex(reinterpret_cast<uintptr_t>(_dyld_get_image_header(i))).str(" ");
        line.hex(static_cast<uintptr_t>(_dyld_get_image_vmaddr_slide(i))).str(" ").str(_dyld_get_image_name(i)).end();
    }
}
#else
int write_image(dl_phdr_info* info, size_t, void*)
{
    Line line;
    line.str("  ").hex(static_cast<uintptr_t>(info->dlpi_addr)).str(" ");
    if (info->dlpi_name != nullptr && *info->dlpi_name) {
        line.str(info->dlpi_name);
    } else {
        char exe[512];
        const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        exe[n > 0 ? n : 0] = '\0';
        line.str(n > 0 ? exe : "(main program)");
    }
    line.end();
    return 0;
}

void write_images()
{
    Line line;
    line.str("*** loaded images (load base, path):").end();
    dl_iterate_phdr(write_image, nullptr);
}
#endif

void crash_handler(int sig, siginfo_t* info, void* uctx)
{
    const int index = static_cast<int>(std::find(std::begin(kSignals), std::end(kSignals), sig) - std::begin(kSignals));
    if (g_inHandler.fetch_add(1) == 0) {
        drain_tee();
        const Registers regs = registers_of(uctx);
        Line line;
        line.str(kCrashMarker).str(": signal ").dec(sig).str(" (").str(signal_name(sig)).str("), code ");
        line.dec(info != nullptr ? info->si_code : 0);
        if (info != nullptr && info->si_code <= 0) {
            line.str(", sent by pid ").dec(info->si_pid).end(); // abort(), raise(), kill
        } else {
            line.str(", address ").hex(info != nullptr ? reinterpret_cast<uintptr_t>(info->si_addr) : 0).end();
        }
        line.str("*** ").str(PARTY_BOARD_WC_DESCRIBE).str(" ").str(PARTY_BOARD_WC_REVISION).str(" ");
        line.str(PARTY_BOARD_BUILD_TYPE).end();
        line.str("*** pc ").hex(regs.pc).str(" lr ").hex(regs.lr).str(" sp ").hex(regs.sp).end();
        if (regs.pc != 0) {
            line.str("*** pc frame:").end();
            write_frame(0, reinterpret_cast<void*>(regs.pc));
        }
        void* frames[128];
        const int count = capture_backtrace(frames, static_cast<int>(std::size(frames)));
        line.str("*** backtrace (").dec(count).str(" frames, the first are the crash handler's):").end();
        for (int i = 0; i < count; i++) {
            write_frame(i, frames[i]);
        }
        write_images();
        line.str("*** end of crash report").end();
        if (g_logFd >= 0) {
            fsync(g_logFd);
        }
    }
    // Hand the signal on: back to what was there before (usually the default action, which ends the process and
    // leaves a core). A fault re-runs the instruction and faults again; a sent signal (abort, raise) is raised again.
    if (index < static_cast<int>(std::size(kSignals))) {
        sigaction(sig, &g_oldActions[index], nullptr);
    } else {
        signal(sig, SIG_DFL);
    }
    if (info == nullptr || info->si_code <= 0 || sig == SIGABRT) {
        raise(sig);
    }
}

void install_crash_handler()
{
    // backtrace() loads libgcc on first use: do that now, not in the handler.
    void* warm[4];
    capture_backtrace(warm, 4);

    stack_t ss {};
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof g_altStack;
    sigaltstack(&ss, nullptr);

    for (size_t i = 0; i < std::size(kSignals); i++) {
        struct sigaction sa {};
        sa.sa_sigaction = crash_handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(kSignals[i], &sa, &g_oldActions[i]);
    }
}

#elif PB_LOG_WIN32

LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep)
{
    static std::atomic<int> entered { 0 };
    if (entered.fetch_add(1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    drain_tee();
    Line line;
    const EXCEPTION_RECORD* rec = ep != nullptr ? ep->ExceptionRecord : nullptr;
    line.str(kCrashMarker).str(": exception ").hex(rec != nullptr ? rec->ExceptionCode : 0).str(" at ");
    line.hex(rec != nullptr ? reinterpret_cast<uintptr_t>(rec->ExceptionAddress) : 0).end();
    line.str("*** ").str(PARTY_BOARD_WC_DESCRIBE).str(" ").str(PARTY_BOARD_WC_REVISION).str(" ");
    line.str(PARTY_BOARD_BUILD_TYPE).end();
    void* frames[62];
    const USHORT count = CaptureStackBackTrace(0, 62, frames, nullptr);
    line.str("*** backtrace (").dec(count).str(" frames, the first are the crash handler's):").end();
    for (USHORT i = 0; i < count; i++) {
        line.str("  #").dec(i).str(" ").hex(reinterpret_cast<uintptr_t>(frames[i])).end();
    }
    line.str("*** loaded images (base, size, path):").end();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me {};
        me.dwSize = sizeof me;
        for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
            line.str("  ").hex(reinterpret_cast<uintptr_t>(me.modBaseAddr)).str(" ").hex(me.modBaseSize).str(" ");
            line.str(me.szExePath).end();
        }
        CloseHandle(snap);
    }
    line.str("*** end of crash report").end();
    if (g_logFd >= 0) {
        _commit(g_logFd);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void install_crash_handler() { SetUnhandledExceptionFilter(crash_filter); }

#endif

std::string read_first_line(const char* path, const char* key = nullptr)
{
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (key == nullptr) {
            return line;
        }
        if (line.rfind(key, 0) == 0) {
            line = line.substr(std::strlen(key));
            if (line.size() >= 2 && line.front() == '"' && line.back() == '"') {
                line = line.substr(1, line.size() - 2);
            }
            return line;
        }
    }
    return {};
}

#if defined(__ANDROID__)
std::string android_prop(const char* name)
{
    char value[PROP_VALUE_MAX] = {};
    __system_property_get(name, value);
    return value;
}
#endif

#if defined(__APPLE__)
std::string apple_sysctl(const char* name)
{
    char value[256] = {};
    size_t size = sizeof value - 1;
    if (sysctlbyname(name, value, &size, nullptr, 0) != 0) {
        return {};
    }
    return value;
}
#endif

std::string device_model()
{
#if defined(__ANDROID__)
    return android_prop("ro.product.manufacturer") + " " + android_prop("ro.product.model") + " (" +
           android_prop("ro.product.device") + ")";
#elif defined(__APPLE__)
    return apple_sysctl("hw.machine");
#elif defined(__linux__)
    std::string vendor = read_first_line("/sys/devices/virtual/dmi/id/sys_vendor");
    std::string product = read_first_line("/sys/devices/virtual/dmi/id/product_name");
    return vendor.empty() && product.empty() ? std::string("unknown") : vendor + " " + product;
#else
    return "unknown";
#endif
}

std::string os_version()
{
    std::string v;
#if defined(__ANDROID__)
    v = "Android " + android_prop("ro.build.version.release") + " (API " + android_prop("ro.build.version.sdk") +
        ", " + android_prop("ro.build.fingerprint") + ")";
#elif defined(__APPLE__)
    v = "iOS " + apple_sysctl("kern.osproductversion") + " (" + apple_sysctl("kern.osversion") + ")";
#elif defined(__linux__)
    v = read_first_line("/etc/os-release", "PRETTY_NAME=");
#elif defined(_WIN32)
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            OSVERSIONINFOW info {};
            info.dwOSVersionInfoSize = sizeof info;
            if (fn(&info) == 0) {
                v = "Windows " + std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) +
                    "." + std::to_string(info.dwBuildNumber);
            }
        }
    }
#endif
#if PB_LOG_POSIX
    utsname u {};
    if (uname(&u) == 0) {
        v += std::string(v.empty() ? "" : ", ") + u.sysname + " " + u.release + " " + u.machine;
    }
#endif
    return v.empty() ? std::string("unknown") : v;
}

} // namespace

bool supported()
{
#if PB_LOG_POSIX || PB_LOG_WIN32
    return true;
#else
    return false;
#endif
}

void start(const std::filesystem::path& userDir, bool writeLogFile)
{
    static bool started = false;
    if (started || !supported()) {
        return;
    }
    started = true;
    g_logsDir = calc_logs_dir(userDir);
#if PB_LOG_POSIX || PB_LOG_WIN32
    if (writeLogFile && !g_logsDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(g_logsDir, ec);
        const bool crashed = log_ended_in_crash(run_log(0));
        rotate_logs();
        if (crashed) {
            g_lastCrash = run_log(1);
        }
#if PB_LOG_WIN32
        g_logFd = _wopen(run_log(0).c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY | _O_NOINHERIT,
            _S_IREAD | _S_IWRITE);
#else
        g_logFd = open(run_log(0).c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
#endif
        if (g_logFd >= 0) {
            g_writing = true;
            start_tee();
            std::atexit(stop_tee);
        } else {
            Log.warn("Unable to open log file {}: {}", run_log(0).string(), std::strerror(errno));
        }
    }
    if (!g_writing) {
        // Per line here too, so stdout's tail before a crash isn't lost in a buffer.
        fflush(stdout);
        setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
    }
    install_crash_handler();
#else
    (void)writeLogFile;
#endif
}

void log_boot_info(const std::filesystem::path& userDir)
{
#ifdef PARTY_BOARD_ARCH
    const char* arch = PARTY_BOARD_ARCH;
#else
    const char* arch = "?";
#endif
    Log.info("Party Board {} ({} {}, {} build of {})", PARTY_BOARD_WC_DESCRIBE, PARTY_BOARD_WC_BRANCH,
        PARTY_BOARD_WC_REVISION, PARTY_BOARD_BUILD_TYPE, PARTY_BOARD_WC_DATE);
    Log.info("Platform: {} {}", SDL_GetPlatform(), arch);
    Log.info("Device: {}", device_model());
    Log.info("OS: {}", os_version());
    Log.info("User dir: {}", userDir.string());
    if (g_writing) {
        Log.info("Log file: {}", log_path().string());
    } else {
        Log.info("Log file: off");
    }
    if (!g_lastCrash.empty()) {
        Log.warn("The last run crashed, its log: {}", g_lastCrash.string());
    }
}

void log_disc_id()
{
    const DVDDiskID* id = DVDGetCurrentDiskID();
    if (id == nullptr || id->gameName[0] == '\0') {
        Log.info("Disc id: unknown");
        return;
    }
    Log.info("Disc id: {}{} (disc {}, version {})", std::string_view(id->gameName, sizeof id->gameName),
        std::string_view(id->company, sizeof id->company), id->diskNumber, id->gameVersion);
}

void maybe_test_crash(const char* where)
{
    const char* what = getenv("PB_CRASH_TEST");
#if defined(__ANDROID__)
    // An app gets no env vars: `adb shell setprop debug.partyboard.crash segv` (and setprop ... "" to stop).
    static std::string prop;
    if (what == nullptr || *what == '\0') {
        prop = android_prop("debug.partyboard.crash");
        what = prop.c_str();
    }
#endif
    if (what == nullptr || *what == '\0') {
        return;
    }
    Log.warn("PB_CRASH_TEST={}: crashing on purpose ({})", what, where);
    if (std::strcmp(what, "abort") == 0) {
        std::abort();
    }
#if PB_LOG_POSIX
    if (std::strcmp(what, "bus") == 0) {
        raise(SIGBUS);
    }
    if (std::strcmp(what, "ill") == 0) {
        raise(SIGILL);
    }
#endif
    if (std::strcmp(what, "segv") == 0) {
        *static_cast<volatile int*>(nullptr) = 1;
    }
}

std::filesystem::path logs_dir() { return g_logsDir; }

std::filesystem::path log_path() { return g_logsDir.empty() ? std::filesystem::path {} : run_log(0); }

const std::filesystem::path& last_crash_log() { return g_lastCrash; }

bool can_open_logs_dir()
{
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    // Android has no file: URL handler for app dirs (FileUriExposedException); the path is shown instead.
    return false;
#else
    return !g_logsDir.empty();
#endif
}

void open_logs_dir()
{
    std::error_code ec;
    std::filesystem::create_directories(g_logsDir, ec);
#if defined(__APPLE__) && TARGET_OS_IOS
    // Opens the Files app at the folder (On My iPhone > Party Board > logs).
    const std::string url = "shareddocuments://" + g_logsDir.string();
#else
    const auto u8 = g_logsDir.generic_u8string();
    std::string path(u8.begin(), u8.end());
    if (!path.empty() && path.front() != '/') {
        path.insert(path.begin(), '/'); // C:/... -> file:///C:/...
    }
    const std::string url = "file://" + path;
#endif
    if (!SDL_OpenURL(url.c_str())) {
        Log.warn("Unable to open {}: {}", url, SDL_GetError());
    }
}

} // namespace partyboard::logfile
