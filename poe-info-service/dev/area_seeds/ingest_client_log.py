"""
Run poe-info-service headlessly to catch the database up on Client.txt, then
exit — used ahead of area-seed generation so newly-visited areas are in the
DB without needing the full app running.

Usage:
    python poe-info-service/dev/area_seeds/ingest_client_log.py [preset]

Finds the first install directory in l2p-poe.toml's install_dirs that has a
Client.txt, then runs `poe-info-service log ingest --install-dir ...`
(poe-info-service/cli.go), which tails that Client.txt to EOF once and exits
— poe-info-service otherwise only tails continuously while running as a
service, with no "run once and exit" mode of its own.

Runs the binary straight from the build tree (build/<preset>/src/), never
bin/ — bin/ is reserved for the user's own manual testing and may have a live
instance running with its own config (see CLAUDE.md).
"""

import platform
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent.parent.parent
TOML = ROOT / "l2p-poe.toml"

DEFAULT_PRESET = "windows-msvc" if platform.system() == "Windows" else "debug"
EXE_NAME = "poe-info-service.exe" if platform.system() == "Windows" else "poe-info-service"


def find_install_dir() -> Path:
    if not TOML.exists():
        print(f"error: {TOML} not found", file=sys.stderr)
        sys.exit(1)

    text = TOML.read_text(encoding="utf-8")
    block = re.search(r'install_dirs\s*=\s*\[([^\]]*)\]', text, re.DOTALL)
    if block:
        for d in re.findall(r'["\']([^"\']+)["\']', block.group(1)):
            if (Path(d) / "logs" / "Client.txt").exists():
                return Path(d)

    print(f"error: no Client.txt found under any install_dirs entry in {TOML}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    preset = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PRESET
    exe = ROOT / "build" / preset / "src" / EXE_NAME
    if not exe.exists():
        print(f"Executable not found: {exe}", file=sys.stderr)
        print("Build the project first (just service-build).", file=sys.stderr)
        sys.exit(1)

    install_dir = find_install_dir()

    result = subprocess.run([
        str(exe), "log", "ingest",
        "--install-dir", str(install_dir),
        "--config-path", str(TOML),
        "--data-dir", str(ROOT),
    ])
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
