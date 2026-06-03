"""verible_toolchain_alias"""

load("//verible:verible_toolchain.bzl", "TOOLCHAIN_TYPE")

def _verible_toolchain_alias_impl(ctx):
    tc = ctx.toolchains[TOOLCHAIN_TYPE]
    runfiles = ctx.runfiles(files = [tc.verible_format, tc.verible_lint])
    return [
        DefaultInfo(files = tc.all_files, runfiles = runfiles),
        tc.template_variable_info,
    ]

verible_toolchain_alias = rule(
    implementation = _verible_toolchain_alias_impl,
    doc = "Forwards the registered `verible_toolchain`'s files and template variables.",
    toolchains = [TOOLCHAIN_TYPE],
)
