"""Implementation of `verible_diff_test`."""

load("//verible:verible_toolchain.bzl", "TOOLCHAIN_TYPE")

_VERILOG_EXTENSIONS = [".sv", ".svh", ".v", ".vh"]

def _rlocationpath(file, workspace_name):
    if file.short_path.startswith("../"):
        return file.short_path[len("../"):]

    return "{}/{}".format(workspace_name, file.short_path)

def _verible_diff_test_impl(ctx):
    file1 = ctx.file.file1
    file2 = ctx.file.file2

    tc = ctx.toolchains[TOOLCHAIN_TYPE]
    ws = ctx.workspace_name

    args = ctx.actions.args()
    args.set_param_file_format("multiline")
    args.add(_rlocationpath(tc.verible_diff, ws), format = "--verible-diff=%s")
    args.add(_rlocationpath(file1, ws), format = "--file1=%s")
    args.add(_rlocationpath(file2, ws), format = "--file2=%s")
    args.add(ctx.attr.mode, format = "--mode=%s")

    args_file = ctx.actions.declare_file("{}.args.txt".format(ctx.label.name))
    ctx.actions.write(
        output = args_file,
        content = args,
    )

    # Bazel requires a test rule's executable be created by the rule itself, so
    # symlink the runner cc_binary into this package and surface that. Preserve
    # any Windows `.exe` / `.bat` extension so the OS still treats it as an
    # executable after the symlink.
    extension = ""
    if ctx.executable._runner.basename.endswith((".exe", ".bat")):
        extension = ".{}".format(ctx.executable._runner.extension)

    test_bin = ctx.actions.declare_file("{}{}".format(ctx.label.name, extension))
    ctx.actions.symlink(
        output = test_bin,
        target_file = ctx.executable._runner,
        is_executable = True,
    )

    runner_default_runfiles = ctx.attr._runner[DefaultInfo].default_runfiles
    runfiles = ctx.runfiles(
        files = [
            file1,
            file2,
            tc.verible_diff,
            ctx.executable._runner,
            args_file,
        ],
        transitive_files = tc.all_files,
    ).merge(runner_default_runfiles)

    return [
        DefaultInfo(
            executable = test_bin,
            runfiles = runfiles,
        ),
        RunEnvironmentInfo(
            environment = {
                "VERIBLE_DIFF_TEST_ARGS_FILE": _rlocationpath(args_file, ws),
            },
        ),
    ]

verible_diff_test = rule(
    implementation = _verible_diff_test_impl,
    doc = """Test rule that runs `verible-verilog-diff` between two Verilog sources.

By default the comparison uses `format` mode, which ignores whitespace and
compares token texts — useful for verifying that some transformed source still
parses to the same tokens as a golden reference. Set `mode = "obfuscate"` to
compare token-text *lengths* only, the standard recipe for verifying
`verible-verilog-obfuscate` output.

The test exits 0 when the sources are equivalent under the chosen mode, and
with the diff binary's non-zero status (1) otherwise; the per-token mismatch
report is emitted to the test log.
""",
    attrs = {
        "file1": attr.label(
            mandatory = True,
            allow_single_file = _VERILOG_EXTENSIONS,
            doc = "The first Verilog/SystemVerilog source file.",
        ),
        "file2": attr.label(
            mandatory = True,
            allow_single_file = _VERILOG_EXTENSIONS,
            doc = "The second Verilog/SystemVerilog source file.",
        ),
        "mode": attr.string(
            default = "format",
            values = ["format", "obfuscate"],
            doc = "Diff mode passed to `verible-verilog-diff --mode=...`.",
        ),
        "_runner": attr.label(
            cfg = "exec",
            executable = True,
            default = Label("//verible/private:verible_diff_runner"),
        ),
    },
    test = True,
    toolchains = [TOOLCHAIN_TYPE],
)
