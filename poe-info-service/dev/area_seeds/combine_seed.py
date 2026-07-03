"""
Combine seed_base.sql and areas/*.sql into seed.sql, for local inspection of
what poe-info-service's embedded schema.EnsureSchema will insert on a fresh
database. poe-info-service combines these files itself at runtime (see
poe-info-service/internal/schema/embed.go) — this script's output isn't
consumed by any build step.

Usage:
    python poe-info-service/dev/area_seeds/combine_seed.py

Run gen_area_seed.py (which calls this at the end) when you want to
regenerate the area fixtures from the live database.
"""

from pathlib import Path

DATA = Path(__file__).parent.parent.parent / "internal" / "schema" / "sql"


def combine() -> None:
    sources = [DATA / "seed_base.sql", *sorted((DATA / "areas").glob("*.sql"))]
    parts = [p.read_text(encoding="utf-8").rstrip() for p in sources]
    out = DATA / "seed.sql"
    out.write_text("\n\n".join(parts) + "\n", encoding="utf-8")
    print(f"Combined {len(sources)} files -> {out}")


if __name__ == "__main__":
    combine()
