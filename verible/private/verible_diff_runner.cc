/// @file
/// @brief Process wrapper invoked by the `verible_diff_test` rule.
///
/// @details
///   Runs `verible-verilog-diff [--mode=MODE] source against`. Exits 0 if the
///   two sources are equivalent under the chosen mode (default `format` — token
///   comparison that ignores whitespace), or with the diff binary's non-zero
///   exit code (1 for mismatches). The captured combined stdout/stderr from
///   the child is forwarded to stderr on any non-zero exit so the test log
///   shows the mismatched-token report.
///
///   Like the format/lint runners, the wrapper operates in one of two modes:
///   - **Test mode**: `VERIBLE_DIFF_TEST_ARGS_FILE` env holds the runfiles key
///     of an args file. Every PATH-valued flag inside is itself a runfiles key
///     resolved via `@rules_cc//cc/runfiles`. Works in both symlink-tree and
///     manifest-only runfiles modes.
///   - **CLI mode**: argv (or `@argfile`) holds literal paths.
///
/// @par CLI
///   | Flag                   | Description                                            |
///   |------------------------|--------------------------------------------------------|
///   | `--verible-diff=PATH`  | (required) Path to the verible-verilog-diff binary.    |
///   | `--file1=PATH`         | (required) First source file.                          |
///   | `--file2=PATH`         | (required) Second source file.                         |
///   | `--mode=MODE`          | (optional) `format` (default) or `obfuscate`.          |

#include "rules_cc/cc/runfiles/runfiles.h"
#include "verible/private/subprocess.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using rules_cc::cc::runfiles::Runfiles;
namespace sp = rules_verible::subprocess;

namespace {

/// @brief Print @p msg to stderr and exit with status 2.
[[noreturn]] void die(const std::string& msg) {
  std::fprintf(stderr, "verible_diff_runner: %s\n", msg.c_str());
  std::exit(2);
}

/// @brief Load one argument per line from a Bazel parameter file.
std::vector<std::string> read_param_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) die("could not open param file: " + path);
  std::vector<std::string> out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(std::move(line));
  }
  return out;
}

/// @brief Test whether @p s begins with @p prefix.
bool starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

/// @brief Extract the value from a `--flag=value` argument.
std::string flag_value(const std::string& arg, const std::string& flag) {
  return arg.substr(flag.size() + 1);
}

/// @brief Resolve @p key via @p runfiles if non-null, otherwise return @p key.
std::string maybe_resolve(Runfiles* runfiles, const std::string& key) {
  if (!runfiles) return key;
  std::string resolved = runfiles->Rlocation(key);
  if (resolved.empty()) die("could not resolve runfiles key: " + key);
  return resolved;
}

}  // namespace

/// @brief Program entry point. See file-level documentation for the CLI.
int main(int argc, char** argv) {
  std::unique_ptr<Runfiles> runfiles;
  std::vector<std::string> args;

  std::string test_args_key = sp::get_env("VERIBLE_DIFF_TEST_ARGS_FILE");
  if (!test_args_key.empty()) {
    std::string err;
    runfiles.reset(Runfiles::Create(argv[0], BAZEL_CURRENT_REPOSITORY, &err));
    if (!runfiles) die("could not initialize runfiles: " + err);

    std::string args_file_path = runfiles->Rlocation(test_args_key);
    if (args_file_path.empty()) {
      die("could not resolve VERIBLE_DIFF_TEST_ARGS_FILE='" + test_args_key + "'");
    }
    args = read_param_file(args_file_path);
  } else if (argc == 2 && argv[1][0] == '@') {
    args = read_param_file(argv[1] + 1);
  } else {
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  }

  std::string verible_bin;
  std::string file1;
  std::string file2;
  std::string mode = "format";

  for (const auto& a : args) {
    if (starts_with(a, "--verible-diff=")) {
      verible_bin = maybe_resolve(runfiles.get(), flag_value(a, "--verible-diff"));
    } else if (starts_with(a, "--file1=")) {
      file1 = maybe_resolve(runfiles.get(), flag_value(a, "--file1"));
    } else if (starts_with(a, "--file2=")) {
      file2 = maybe_resolve(runfiles.get(), flag_value(a, "--file2"));
    } else if (starts_with(a, "--mode=")) {
      mode = flag_value(a, "--mode");
    } else {
      die("unknown argument: " + a);
    }
  }

  if (verible_bin.empty()) die("--verible-diff=PATH is required");
  if (file1.empty()) die("--file1=PATH is required");
  if (file2.empty()) die("--file2=PATH is required");
  if (mode != "format" && mode != "obfuscate") {
    die("--mode must be 'format' or 'obfuscate' (got '" + mode + "')");
  }

  std::vector<std::string> cmd = {
      verible_bin,
      "--mode=" + mode,
      file1,
      file2,
  };

  // Let the child's stdout/stderr stream straight through. Bazel buffers
  // test output itself and only surfaces it on failure.
  return sp::run_inherit(cmd, "");
}
