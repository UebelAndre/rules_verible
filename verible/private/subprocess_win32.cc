/// @file
/// @brief Windows implementation of the `subprocess` API.
///
/// @details
///   Uses `CreateProcessW` + anonymous pipes + `WaitForSingleObject`. Arguments
///   are joined into a UTF-16 command line with the standard
///   `CommandLineToArgvW` quoting rules so that any path containing spaces or
///   backslashes survives the round-trip into the child process.

#include "verible/private/subprocess.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace rules_verible {
namespace subprocess {

namespace {

/// @brief Print @p msg to stderr and exit with status 2.
[[noreturn]] void fatal(const std::string& msg) {
  std::fprintf(stderr, "subprocess: %s\n", msg.c_str());
  std::exit(2);
}

/// @brief Convert UTF-8 @p s to UTF-16 for the Win32 W APIs.
std::wstring utf8_to_utf16(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                              nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  // Use `&w[0]` rather than `w.data()` — the latter returns `const wchar_t*`
  // before C++17 and MSVC defaults to an older standard, so it won't bind to
  // `LPWSTR`. `&w[0]` is guaranteed non-const since C++11.
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      &w[0], n);
  return w;
}

/// @brief Quote a single argument per `CommandLineToArgvW` rules.
/// @details Wraps the argument in `"..."` when it contains whitespace or
///          quote characters, doubling internal backslashes that precede a
///          quote (or the trailing quote).
std::wstring quote_arg(const std::wstring& arg) {
  bool needs_quote = arg.empty();
  for (wchar_t c : arg) {
    if (c == L' ' || c == L'\t' || c == L'"' || c == L'\n' || c == L'\v') {
      needs_quote = true;
      break;
    }
  }
  if (!needs_quote) return arg;

  std::wstring out;
  out.push_back(L'"');
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
    } else {
      out.append(backslashes, L'\\');
      out.push_back(c);
      backslashes = 0;
    }
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

/// @brief Join @p argv into a single command-line wide string.
std::wstring build_command_line(const std::vector<std::string>& argv) {
  std::wstring cmd;
  for (const auto& a : argv) {
    if (!cmd.empty()) cmd.push_back(L' ');
    cmd += quote_arg(utf8_to_utf16(a));
  }
  return cmd;
}

/// @brief Shared `CreateProcessW` worker.
/// @copydetails rules_verible::subprocess::run_impl (POSIX version semantics)
int run_impl(const std::vector<std::string>& argv,
             const std::string& cwd,
             bool capture_stdout,
             bool capture_stderr_combined,
             std::string* out) {
  HANDLE read_pipe = nullptr, write_pipe = nullptr;
  const bool need_pipe = capture_stdout;
  if (need_pipe) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
      fatal("CreatePipe failed");
    }
    // Don't let the child inherit the read end.
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = need_pipe ? write_pipe : GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = (need_pipe && capture_stderr_combined)
                     ? write_pipe
                     : GetStdHandle(STD_ERROR_HANDLE);

  std::wstring cmd_line = build_command_line(argv);
  // CreateProcessW requires a writable command-line buffer.
  std::vector<wchar_t> cmd_buf(cmd_line.begin(), cmd_line.end());
  cmd_buf.push_back(L'\0');

  std::wstring cwd_w;
  if (!cwd.empty()) cwd_w = utf8_to_utf16(cwd);

  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(
      /*lpApplicationName=*/nullptr,
      /*lpCommandLine=*/cmd_buf.data(),
      /*lpProcessAttributes=*/nullptr,
      /*lpThreadAttributes=*/nullptr,
      /*bInheritHandles=*/TRUE,
      /*dwCreationFlags=*/0,
      /*lpEnvironment=*/nullptr,
      /*lpCurrentDirectory=*/cwd_w.empty() ? nullptr : cwd_w.c_str(),
      &si, &pi);
  if (need_pipe) CloseHandle(write_pipe);
  if (!ok) {
    if (need_pipe) CloseHandle(read_pipe);
    fatal("CreateProcessW failed for " + argv[0]);
  }

  if (need_pipe) {
    std::string captured;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(read_pipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
      captured.append(buf, n);
    }
    CloseHandle(read_pipe);
    if (out) *out = captured;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return static_cast<int>(exit_code);
}

}  // namespace

int run_capture_combined(const std::vector<std::string>& argv,
                         std::string* combined_output) {
  return run_impl(argv, "", /*capture_stdout=*/true,
                  /*capture_stderr_combined=*/true, combined_output);
}

int run_capture_stdout(const std::vector<std::string>& argv,
                       const std::string& cwd,
                       std::string* stdout_output) {
  return run_impl(argv, cwd, /*capture_stdout=*/true,
                  /*capture_stderr_combined=*/false, stdout_output);
}

int run_inherit(const std::vector<std::string>& argv,
                const std::string& cwd) {
  return run_impl(argv, cwd, /*capture_stdout=*/false,
                  /*capture_stderr_combined=*/false, nullptr);
}

bool file_exists(const std::string& path) {
  DWORD attrs = GetFileAttributesW(utf8_to_utf16(path).c_str());
  return attrs != INVALID_FILE_ATTRIBUTES;
}

std::string which(const std::vector<std::string>& candidates) {
  std::string path_env = get_env("PATH");
  std::stringstream ss(path_env);
  std::string dir;
  static constexpr const char* kExtensions[] = {"", ".exe", ".bat", ".cmd"};
  while (std::getline(ss, dir, ';')) {
    if (dir.empty()) continue;
    for (const auto& name : candidates) {
      for (const char* ext : kExtensions) {
        std::string candidate = dir + "\\" + name + ext;
        if (file_exists(candidate)) return candidate;
      }
    }
  }
  return std::string();
}

std::string get_env(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

}  // namespace subprocess
}  // namespace rules_verible
