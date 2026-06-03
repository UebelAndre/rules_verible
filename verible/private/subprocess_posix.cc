/// @file
/// @brief POSIX implementation of the `subprocess` API.
///
/// @details
///   Uses `posix_spawn` + anonymous pipes + `waitpid`. The chdir-before-exec
///   functionality relies on `posix_spawn_file_actions_addchdir_np` (glibc
///   >= 2.29, macOS >= 10.15, POSIX 2024).

#include "verible/private/subprocess.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

extern char** environ;

namespace rules_verible {
namespace subprocess {

namespace {

/// @brief Print @p msg to stderr and exit with status 2.
[[noreturn]] void fatal(const std::string& msg) {
  std::fprintf(stderr, "subprocess: %s\n", msg.c_str());
  std::exit(2);
}

/// @brief Shared `posix_spawn` worker.
/// @param argv                 Command and arguments.
/// @param cwd                  Working directory; empty to inherit.
/// @param capture_stdout       Redirect child stdout into the capture pipe.
/// @param capture_stderr_combined  Redirect child stderr into the same pipe as
///                             stdout (only honored when capture_stdout=true).
/// @param out                  Out: captured pipe data. May be `nullptr`.
/// @return Exit status: 0..127 normal exit, 128+signo on signal, 1 otherwise.
int run_impl(const std::vector<std::string>& argv,
             const std::string& cwd,
             bool capture_stdout,
             bool capture_stderr_combined,
             std::string* out) {
  int pipefd[2] = {-1, -1};
  const bool need_pipe = capture_stdout;
  if (need_pipe && pipe(pipefd) != 0) {
    fatal(std::string("pipe: ") + std::strerror(errno));
  }

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (need_pipe) {
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    if (capture_stderr_combined) {
      posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
    }
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
  }
  if (!cwd.empty()) {
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
  }

  std::vector<char*> cargv;
  cargv.reserve(argv.size() + 1);
  for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
  cargv.push_back(nullptr);

  pid_t pid = 0;
  int rc = posix_spawnp(&pid, cargv[0], &actions, nullptr, cargv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (need_pipe) close(pipefd[1]);
  if (rc != 0) {
    if (need_pipe) close(pipefd[0]);
    fatal(std::string("posix_spawnp ") + cargv[0] + ": " + std::strerror(rc));
  }

  if (need_pipe) {
    std::ostringstream buf;
    char tmp[4096];
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) buf.write(tmp, n);
    close(pipefd[0]);
    if (out) *out = buf.str();
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) fatal(std::string("waitpid: ") + std::strerror(errno));
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 1;
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
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

std::string which(const std::vector<std::string>& candidates) {
  std::string path_env = get_env("PATH");
  std::stringstream ss(path_env);
  std::string dir;
  while (std::getline(ss, dir, ':')) {
    if (dir.empty()) continue;
    for (const auto& name : candidates) {
      std::string candidate = dir + "/" + name;
      if (file_exists(candidate) && access(candidate.c_str(), X_OK) == 0) {
        return candidate;
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
