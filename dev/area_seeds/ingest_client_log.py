"""
Run the app headlessly to ingest the configured Client.txt into the database.

Usage:
    python dev/ingest_client_log.py

Delegates entirely to `bin/l2p-poe1.exe ingest`, which reads install paths
from the app's TOML config and tails the log from the last known byte offset.
This script adds no logic — it exists so the orchestrator has a named step.
"""

import subprocess
import sys
from pathlib import Path

EXE = Path(__file__).parent.parent.parent / "bin" / "l2p-poe1.exe"


def main() -> None:
    if not EXE.exists():
        print(f"Executable not found: {EXE}", file=sys.stderr)
        print("Build the project first.", file=sys.stderr)
        sys.exit(1)

    result = subprocess.run([str(EXE), "ingest"])
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
