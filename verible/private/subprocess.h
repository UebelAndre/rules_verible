/// @file
/// @brief Cross-platform process-spawn helpers used by the runner / fixer
///        cc_binary targets.
///
/// @details
///   The runners and fixers all need to spawn a child (the verible binary, or
///   `bazel query`) and capture its output. The implementations differ between
///   POSIX (`posix_spawn` + pipes + waitpid) and Windows (`CreateProcessW` +
///   anonymous pipes + `WaitForSingleObject`). The split is contained in
///   `subprocess_posix.cc` / `subprocess_win32.cc`; consumers see only this
///   header.
///
///   No function in this API performs any file-system layout assumption beyond
///   what the OS exposes — runfiles lookup happens in the callers via
///   `@rules_cc//cc/runfiles`. That keeps the rules working in both symlink-
///   based and manifest-only runfiles modes.

#ifndef VERIBLE_PRIVATE_SUBPROCESS_H_
#define VERIBLE_PRIVATE_SUBPROCESS_H_

#include <string>
#include <vector>

namespace rules_verible {
namespace subprocess {

/// @brief Spawn @p argv with both stdout and stderr piped into one buffer.
/// @param argv             Command and arguments (argv[0] is the executable).
/// @param combined_output  Out: captured combined stream. May be `nullptr`.
/// @return Child exit status (0..127 normal exit, 128+signo on POSIX signal,
///         non-zero on Windows abnormal termination, 1 otherwise).
int run_capture_combined(const std::vector<std::string>& argv,
                         std::string* combined_output);

/// @brief Spawn @p argv in @p cwd capturing stdout; stderr inherits the
///        parent's stderr.
/// @param argv           Command and arguments.
/// @param cwd            Working directory for the child. Empty inherits.
/// @param stdout_output  Out: captured stdout. May be `nullptr`.
/// @return Child exit status (see `run_capture_combined`).
int run_capture_stdout(const std::vector<std::string>& argv,
                       const std::string& cwd,
                       std::string* stdout_output);

/// @brief Spawn @p argv in @p cwd inheriting parent's stdin/stdout/stderr.
/// @param argv  Command and arguments.
/// @param cwd   Working directory for the child. Empty inherits.
/// @return Child exit status (see `run_capture_combined`).
int run_inherit(const std::vector<std::string>& argv,
                const std::string& cwd);

/// @brief Check whether the filesystem entry at @p path exists.
bool file_exists(const std::string& path);

/// @brief Search PATH for the first existing executable in @p candidates.
/// @details On Windows the search also tries `.exe`, `.bat`, and `.cmd`
///          suffixes; on POSIX the candidate names are tried verbatim and the
///          executable bit is checked.
/// @return Absolute path on success, empty string if nothing found.
std::string which(const std::vector<std::string>& candidates);

/// @brief Read an environment variable.
/// @return The value, or empty string if the variable is unset.
std::string get_env(const char* name);

}  // namespace subprocess
}  // namespace rules_verible

#endif  // VERIBLE_PRIVATE_SUBPROCESS_H_
