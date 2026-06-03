/// @file
/// @brief Test binary asserting that `version.bzl` and `MODULE.bazel` agree on
///        the `rules_verible` version string.
///
/// @details
///   The Starlark side loads `VERSION` from `version.bzl` at analysis time and
///   plumbs it into the test process via the `VERSION` env var. The
///   `MODULE_BAZEL` env var holds the runfiles key for the project's
///   `MODULE.bazel` file. This binary resolves the runfile, parses the
///   `version = "..."` line out of the top-level `module(...)` block, and
///   exits non-zero if it does not match `VERSION`.

#include "rules_cc/cc/runfiles/runfiles.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using rules_cc::cc::runfiles::Runfiles;

/// @brief Bazel 7 does not define `BAZEL_CURRENT_REPOSITORY`; supply a default.
#ifndef BAZEL_CURRENT_REPOSITORY
#define BAZEL_CURRENT_REPOSITORY "_main"
#endif

namespace {

/// @brief Trim leading whitespace from @p s in place.
void ltrim(std::string& s) {
  s.erase(0, s.find_first_not_of(" \t"));
}

/// @brief Trim trailing whitespace and a single trailing comma from @p s
///        in place.
void rtrim_with_comma(std::string& s) {
  size_t last = s.find_last_not_of(" \t,");
  if (last == std::string::npos) {
    s.clear();
  } else {
    s.erase(last + 1);
  }
}

/// @brief Strip surrounding double quotes from @p s, if present.
void unquote(std::string& s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
  }
}

/// @brief Parse the `version = "..."` attribute out of the top-level
///        `module(...)` block in @p text.
/// @param text   Full contents of a `MODULE.bazel` file.
/// @param output Out: the parsed version string.
/// @return True on success; false (with an error logged to stderr) if no
///         version line was found before the closing `)`.
bool parse_module_bazel_version(const std::string& text, std::string& output) {
  std::istringstream stream(text);
  std::string line;
  bool inside_module = false;

  while (std::getline(stream, line)) {
    if (inside_module) {
      if (!line.empty() && line.front() == ')') {
        std::cerr << "version_test: reached end of module(...) without a "
                     "version= line"
                  << std::endl;
        return false;
      }
      std::size_t eq = line.rfind(" = ");
      if (eq == std::string::npos) continue;

      std::string param = line.substr(0, eq);
      std::string value = line.substr(eq + 3);
      ltrim(param);
      rtrim_with_comma(param);
      ltrim(value);
      rtrim_with_comma(value);
      unquote(value);

      if (param == "version") {
        output = value;
        return true;
      }
    } else if (line.rfind("module(", 0) == 0) {
      inside_module = true;
    }
  }

  std::cerr << "version_test: no module(...) block found in MODULE.bazel"
            << std::endl;
  return false;
}

/// @brief Read an environment variable, returning an empty string when unset.
std::string get_env(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

}  // namespace

/// @brief Program entry point.
int main() {
  std::string version = get_env("VERSION");
  std::string module_bazel_key = get_env("MODULE_BAZEL");
  if (version.empty() || module_bazel_key.empty()) {
    std::cerr << "version_test: VERSION and MODULE_BAZEL env vars must be set"
              << std::endl;
    return 1;
  }

  std::string err;
  std::unique_ptr<Runfiles> runfiles(
      Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &err));
  if (!runfiles) {
    std::cerr << "version_test: could not initialize runfiles: " << err
              << std::endl;
    return 1;
  }

  std::string module_bazel_path = runfiles->Rlocation(module_bazel_key);
  if (module_bazel_path.empty()) {
    std::cerr << "version_test: could not resolve MODULE_BAZEL='"
              << module_bazel_key << "'" << std::endl;
    return 1;
  }

  std::ifstream module_bazel(module_bazel_path);
  if (!module_bazel.is_open()) {
    std::cerr << "version_test: could not open " << module_bazel_path
              << std::endl;
    return 1;
  }

  std::ostringstream buf;
  buf << module_bazel.rdbuf();
  std::string parsed;
  if (!parse_module_bazel_version(buf.str(), parsed)) return 1;

  if (parsed != version) {
    std::cerr << "version_test: version.bzl=\"" << version
              << "\" != MODULE.bazel=\"" << parsed << "\"" << std::endl;
    return 1;
  }

  std::cout << "Versions match: " << version << std::endl;
  return 0;
}
