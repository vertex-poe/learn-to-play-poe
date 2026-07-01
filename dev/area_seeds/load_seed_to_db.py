"""
Load all committed seed files into the live database.

Usage:
    python dev/load_seed_to_db.py

Runs INSERT OR IGNORE so existing rows are never overwritten. The main
purpose is to backfill type assignments that exist in seed files but not
yet in the DB, so a subsequent gen_area_seed run doesn't lose them.
"""

import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent.parent
DB   = ROOT / "l2p-poe.db"
DATA = ROOT / "data"


def main() -> None:
    if not DB.exists():
        print(f"Database not found: {DB}", file=sys.stderr)
        sys.exit(1)

    db = sqlite3.connect(DB)

    # Live DB may predate the type column — add it if missing.
    try:
        db.execute("ALTER TABLE areas ADD COLUMN type TEXT")
        db.commit()
        print("Added type column to areas table.")
    except sqlite3.OperationalError:
        pass

    sources = [DATA / "seed_base.sql", *sorted((DATA / "areas").glob("*.sql"))]
    for src in sources:
        sql = src.read_text(encoding="utf-8")
        if "INSERT OR IGNORE INTO areas" in sql or "INSERT OR IGNORE INTO classes" in sql:
            db.executescript(sql)
            print(f"  loaded {src.name}")

    print(f"Done — {len(sources)} files applied.")


if __name__ == "__main__":
    main()
