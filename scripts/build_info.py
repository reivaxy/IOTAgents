"""PlatformIO pre-build script: generates build_info_generated.h with the git hash and build time."""
import datetime
import os
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)


def git(*args):
    try:
        return subprocess.check_output(
            ["git", *args], cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL  # noqa: F821
        ).decode().strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


git_hash = git("rev-parse", "--short=8", "HEAD") or "unknown"
if git_hash != "unknown" and git("status", "--porcelain"):
    git_hash += "-dirty"

build_time = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

gen_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")  # noqa: F821
os.makedirs(gen_dir, exist_ok=True)
with open(os.path.join(gen_dir, "build_info_generated.h"), "w") as f:
    f.write("#pragma once\n")
    f.write('#define BUILD_GIT_HASH "%s"\n' % git_hash)
    f.write('#define BUILD_TIME "%s"\n' % build_time)

env.Append(CPPPATH=[gen_dir])  # noqa: F821
