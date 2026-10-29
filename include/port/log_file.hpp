#pragma once

// N109: the log file (<user dir>/logs/partyboard.log, the last 3 runs kept as partyboard.1.log...) and the crash
// handler that writes a backtrace and the loaded image list to it before the process dies.

#include <filesystem>
#include <string>

namespace partyboard::logfile {

// Windows (N145): the release exe is a GUI-subsystem program, which starts without stdout/stderr unless its parent
// passed some (a redirect). Then they go to the console it was started from, if any, else to NUL, so the log file's
// tee still has streams to take over. Call first thing in main. Nothing elsewhere.
void attach_std_streams();
// Rotates the old logs, notes whether the last run crashed, and (when the setting is on) sends everything written to
// stdout/stderr to the log file as well. Installs the crash handler either way. Call once, after the settings load.
void start(const std::filesystem::path& userDir, bool writeLogFile);
// Version, git hash, platform, device model, OS version and user dir, into the log.
void log_boot_info(const std::filesystem::path& userDir);
// The disc's game id (call after aurora_dvd_open).
void log_disc_id();
// PB_CRASH_TEST=segv|abort|bus|ill (Android: the debug.partyboard.crash property): crash on purpose, to check the
// crash report.
void maybe_test_crash(const char* where);

bool supported();
// Where the logs go; empty when this platform has no log file.
std::filesystem::path logs_dir();
std::filesystem::path log_path();
// The previous run's log when that run ended in a crash, else empty.
const std::filesystem::path& last_crash_log();
bool can_open_logs_dir();
void open_logs_dir();

} // namespace partyboard::logfile
