"""
Run the app headlessly to ingest the configured Client.txt into the database.

Usage:
    python poe-info-service/dev/ingest_client_log.py

BROKEN as of the poe-info-service migration (ADR-006): delegates to
`bin/l2p-poe.exe ingest`, a CLI verb that no longer exists — l2p-poe's own
CLI only has `dialog hash` now (src/core/Cli.cpp), and poe-info-service
tails Client.txt continuously while running rather than via a one-shot CLI
command. See ROADMAP.md ("poe-info-service" goal) for what fixing this
requires. Left as-is (moved, not fixed) pending that work.
"""

import subprocess
import sys
from pathlib import Path

EXE = Path(__file__).parent.parent.parent / "bin" / "l2p-poe.exe"


def main() -> None:
    if not EXE.exists():
        print(f"Executable not found: {EXE}", file=sys.stderr)
        print("Build the project first.", file=sys.stderr)
        sys.exit(1)

    result = subprocess.run([str(EXE), "ingest"])
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
