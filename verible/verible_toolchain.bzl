"""# `verible_toolchain` rule"""

TOOLCHAIN_TYPE = str(Label("//verible:toolchain_type"))

def _rlocationpath(file, workspace_name):
    if file.short_path.startswith("../"):
        return file.short_path[len("../"):]
    return (workspace_name or "_main") + "/" + file.short_path

def _verible_toolchain_impl(ctx):
    format_file = ctx.file.verible_format
    lint_file = ctx.file.verible_lint
    diff_file = ctx.file.verible_diff

    all_files = depset(
        [format_file, lint_file, diff_file],
        transitive = [
            ctx.attr.verible_format[DefaultInfo].files,
            ctx.attr.verible_lint[DefaultInfo].files,
            ctx.attr.verible_diff[DefaultInfo].files,
        ],
    )

    template_vars = platform_common.TemplateVariableInfo({
        "VERIBLE_DIFF_RLOCATIONPATH": _rlocationpath(diff_file, ctx.workspace_name),
        "VERIBLE_FORMAT_RLOCATIONPATH": _rlocationpath(format_file, ctx.workspace_name),
        "VERIBLE_LINT_RLOCATIONPATH": _rlocationpath(lint_file, ctx.workspace_name),
    })

    toolchain_info = platform_common.ToolchainInfo(
        verible_format = format_file,
        verible_lint = lint_file,
        verible_diff = diff_file,
        all_files = all_files,
        template_variable_info = template_vars,
    )

    return [
        toolchain_info,
        DefaultInfo(files = all_files),
        template_vars,
    ]

verible_toolchain = rule(
    implementation = _verible_toolchain_impl,
    doc = """A toolchain that exposes the [Verible](https://github.com/chipsalliance/verible) tools.

A single instance holds every Verible binary `rules_verible` knows about
(`verible-verilog-format`, `verible-verilog-lint`, `verible-verilog-diff`).
The format, lint, and diff pipelines all consume the same
`//verible:toolchain_type` and pull whichever binary they need from the
resolved `ToolchainInfo`.

The rule also provides `platform_common.TemplateVariableInfo` exposing
`VERIBLE_FORMAT_RLOCATIONPATH`, `VERIBLE_LINT_RLOCATIONPATH`, and
`VERIBLE_DIFF_RLOCATIONPATH`, so a downstream `cc_binary` can reference each
binary's runfiles key via `env = {"VERIBLE_..._RLOCATIONPATH": "$(VERIBLE_..._RLOCATIONPATH)"}`.

The default toolchain registered by `//verible/toolchain:all` points at the
prebuilt Verible releases for linux x86_64, linux aarch64, macOS, and
windows x86_64. To override (e.g. point at a vendored or system-installed
Verible), instantiate this rule yourself and `register_toolchains(...)` it
ahead of the defaults in your `MODULE.bazel`.
""",
    attrs = {
        "verible_diff": attr.label(
            doc = "The `verible-verilog-diff` binary.",
            allow_single_file = True,
            cfg = "exec",
            executable = True,
            mandatory = True,
        ),
        "verible_format": attr.label(
            doc = "The `verible-verilog-format` binary.",
            allow_single_file = True,
            cfg = "exec",
            executable = True,
            mandatory = True,
        ),
        "verible_lint": attr.label(
            doc = "The `verible-verilog-lint` binary.",
            allow_single_file = True,
            cfg = "exec",
            executable = True,
            mandatory = True,
        ),
    },
)
