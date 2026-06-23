"""
Combine seed_base.sql and data/areas/*.sql into data/seed.sql.

Usage:
    python dev/build/combine_seed.py

This is the only seed step the build system runs — it has no database
dependency. Run gen_area_seed.py (which calls this at the end) when you
want to regenerate the area fixtures from the live database.
"""

from pathlib import Path

DATA = Path(__file__).parent.parent.parent / "data"


def combine() -> None:
    sources = [DATA / "seed_base.sql", *sorted((DATA / "areas").glob("*.sql"))]
    parts = [p.read_text(encoding="utf-8").rstrip() for p in sources]
    out = DATA / "seed.sql"
    out.write_text("\n\n".join(parts) + "\n", encoding="utf-8")
    print(f"Combined {len(sources)} files -> {out}")


if __name__ == "__main__":
    combine()
