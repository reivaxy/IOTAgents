Import("env")
import subprocess
import re

# Get current Git branch
branch = subprocess.check_output(
    ["git", "rev-parse", "--abbrev-ref", "HEAD"],
    cwd=env.subst("$PROJECT_DIR")
).decode().strip()

# Sanitize branch name for filesystem compatibility
branch = re.sub(r'[^a-zA-Z0-9_-]', '_', branch)

# Include the chip type from the selected board, independently of the environment name.
chip = env.BoardConfig().get("build.mcu")
env.Replace(PROGNAME="firmware_" + branch + "_" + chip)

print("Firmware branch:", branch)
print("Firmware chip:", chip)