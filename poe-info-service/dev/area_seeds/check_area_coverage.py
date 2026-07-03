"""
Report area codes in the database that have no type assigned.

Usage:
    python poe-info-service/dev/area_seeds/check_area_coverage.py

Run after gen_area_seed to see what still needs manual categorization.
A non-zero exit code signals there are uncategorized areas (useful in scripts).
"""

import sqlite3
import sys
from pathlib import Path

DB   = Path(__file__).parent.parent.parent.parent / "poe-info-service.db"
SKIP = {None, "(null)", "(unknown)"}


def main() -> None:
    if not DB.exists():
        print(f"Database not found: {DB}", file=sys.stderr)
        sys.exit(1)

    db = sqlite3.connect(DB)
    rows = db.execute(
        "SELECT code, display_name FROM areas"
        " WHERE type IS NULL AND code NOT IN ('(null)', '(unknown)')"
        " ORDER BY display_name, code"
    ).fetchall()

    if not rows:
        print("All areas are categorized.")
        sys.exit(0)

    print(f"{len(rows)} uncategorized area(s):\n")
    for code, display in rows:
        print(f"  {code}|{display}")

    sys.exit(1)


if __name__ == "__main__":
    main()
