// Credits: TwilitRealm

#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <port/settings.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
// iOS, as opentennis: the disc is whatever image sits in the app's folder (Files > On My iPhone > Party Board); there
// is no picker.
// (-DPB_DISC_FROM_FOLDER=1 tries it on a desktop build)
#ifndef PB_DISC_FROM_FOLDER
#if defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_MACCATALYST
#define PB_DISC_FROM_FOLDER 1
#else
#define PB_DISC_FROM_FOLDER 0
#endif
#endif

namespace partyboard::iso {
struct KnownDisc;

enum class ValidationError : uint8_t {
    Unknown = 0,
    IOError,
    InvalidImage,
    WrongGame,
    WrongVersion,
    Canceled,
    HashMismatch,
    Success
};

struct VerificationStatus {
    std::atomic_size_t bytesRead = 0;
    std::atomic_size_t bytesTotal = 0;
    const KnownDisc* knownDisc = nullptr;
    std::atomic_bool shouldCancel = false;
};

struct DiscInfo {
    bool isPal = false;
};

ValidationError inspect(const char* path, DiscInfo& info);
ValidationError validate(const char* path, VerificationStatus& status, DiscInfo& info);
bool isPal(const char* path);
void log_verification_state(std::string_view path, DiscVerificationState state);
// The first disc image (by name) in dir that inspect() accepts, or "".
std::string find_disc_in(const std::filesystem::path& dir);

}  // namespace partyboard::iso
