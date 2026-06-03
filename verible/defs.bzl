"""Public entry point for `rules_verible`."""

load(
    ":verible_diff_test.bzl",
    _verible_diff_test = "verible_diff_test",
)
load(
    ":verible_format_aspect.bzl",
    _verible_format_aspect = "verible_format_aspect",
)
load(
    ":verible_format_test.bzl",
    _verible_format_test = "verible_format_test",
)
load(
    ":verible_lint_aspect.bzl",
    _verible_lint_aspect = "verible_lint_aspect",
)
load(
    ":verible_lint_test.bzl",
    _verible_lint_test = "verible_lint_test",
)
load(
    ":verible_toolchain.bzl",
    _verible_toolchain = "verible_toolchain",
)

verible_toolchain = _verible_toolchain
verible_format_aspect = _verible_format_aspect
verible_format_test = _verible_format_test
verible_lint_aspect = _verible_lint_aspect
verible_lint_test = _verible_lint_test
verible_diff_test = _verible_diff_test
