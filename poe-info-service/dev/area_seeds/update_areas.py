"""
Orchestrate the full area seed update workflow.

Usage:
    python poe-info-service/dev/area_seeds/update_areas.py           # skip client ingest
    python poe-info-service/dev/area_seeds/update_areas.py --ingest  # run client log ingest first

Steps:
    1. [--ingest] ingest_client_log.py   pull new areas from the running app
                  (currently broken — see that script's own docstring and ROADMAP.md)
    2.            load_seed_to_db.py     fold committed seed types back into DB
    3.            gen_area_seed.py       assign types, dump files, combine seed
    4.            check_area_coverage.py report anything still uncategorized
"""

import subprocess
import sys
from pathlib import Path

DEV = Path(__file__).parent
PY  = sys.executable


def run(script: Path) -> None:
    print(f"\n=== {script.name} ===")
    result = subprocess.run([PY, str(script)])
    if result.returncode not in (0, 1):  # 1 = coverage warnings, not fatal
        print(f"Error in {script.name} (exit {result.returncode})", file=sys.stderr)
        sys.exit(result.returncode)


def main() -> None:
    if "--ingest" in sys.argv:
        run(DEV / "ingest_client_log.py")

    run(DEV / "load_seed_to_db.py")
    run(DEV / "gen_area_seed.py")
    run(DEV / "check_area_coverage.py")


if __name__ == "__main__":
    main()
