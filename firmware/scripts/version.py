# PlatformIO pre-build script: embeds the version from ../VERSION into the firmware.
Import("env")  # noqa: F821 (provided by PlatformIO)
import os
import subprocess

root = os.path.abspath(os.path.join(env["PROJECT_DIR"], ".."))  # noqa: F821
with open(os.path.join(root, "VERSION")) as f:
    version = f.read().strip()

# CI sets KVM_BUILD to the run number; local builds are "dev".
build = os.environ.get("KVM_BUILD", "dev")
if build != "dev":
    build = "b" + build

try:
    sha = subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"], cwd=root, stderr=subprocess.DEVNULL
    ).decode().strip()
except Exception:
    sha = "unknown"

env.Append(  # noqa: F821
    CPPDEFINES=[
        ("KVM_VERSION", env.StringifyMacro(version)),  # noqa: F821
        ("KVM_BUILD", env.StringifyMacro(build)),  # noqa: F821
        ("KVM_SHA", env.StringifyMacro(sha)),  # noqa: F821
    ]
)
