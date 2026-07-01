# dev/area_seeds/gen_area_seed.py (python)

"""
Assign area types in the database, then dump one seed file per type.

Usage:
    python dev/gen_area_seed.py

Two phases:
  1. UPDATE areas SET type = ... WHERE type IS NULL
     Rules run in order; earlier rules win. Existing types are never overwritten,
     so manual DB edits and types loaded by load_seed_to_db survive.
  2. Dump every typed area to data/areas/<slug>.sql (one file per distinct type),
     then call combine_seed to rebuild seed.sql for local inspection.
"""

import re
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "build"))
from combine_seed import combine

ROOT = Path(__file__).parent.parent.parent
DB   = ROOT / "l2p-poe1.db"
OUT  = ROOT / "poe-info-service" / "internal" / "schema" / "sql" / "areas"


# ── type assignment rules ────────────────────────────────────────────────────
#
# Applied in order via UPDATE ... WHERE type IS NULL.
# List explicit codes before broad globs so exceptions are claimed first.

def _glob(db: sqlite3.Connection, area_type: str, pattern: str) -> None:
    db.execute(
        "UPDATE areas SET type=? WHERE code GLOB ? AND type IS NULL",
        (area_type, pattern),
    )


def _exact(db: sqlite3.Connection, area_type: str, codes: list[str]) -> None:
    ph = ",".join("?" * len(codes))
    db.execute(
        f"UPDATE areas SET type=? WHERE code IN ({ph}) AND type IS NULL",
        [area_type, *codes],
    )


def apply_type_rules(db: sqlite3.Connection) -> None:
    before = db.total_changes

    # Explicit one-off codes — claim these before any glob can grab them.
    _exact(db, "Mechanic", [
        "HeistHub", "Labyrinth_Airlock", "KalguuranSettlersLeague", "Menagerie_Hub",
        "Delve_Main", "SanctumFoyer_Fellshrine", "ChayulaLeague", "ClassicTreasury_Cosmic",
        "Metamorphosis_Hub", "MavenHub", "LakePrototype", "CrucibleLeagueArea",
        "HarvestLeagueBoss", "BetrayalMastermindFight", "ChayulaLeagueTowerBoss",
    ])
    _exact(db, "Boss Arena", [
        "MavenBoss", "Synthesis_MapBoss", "FaridunLeagueBoss",
        "AtlasExilesBoss5",
    ])

    # Broad globs.
    _glob(db, "Hideout",              "Hideout*")
    _glob(db, "Map",                  "MapWorlds*")
    _glob(db, "Map — Vaal side area", "MapSideArea*")
    _glob(db, "Heist",                "Heist*")
    _glob(db, "PvP",                  "ctf*")

    for pat in ("1_Labyrinth*", "2_Labyrinth*", "3_Labyrinth*", "EndGame_Labyrinth*"):
        _glob(db, "Lab", pat)

    for pat in ("BreachBoss*", "BetrayalSafeHouse*", "BestiaryLeague_*Boss",
                "SettlersBoss*", "MapAtziri*"):
        _glob(db, "Boss Arena", pat)

    for pat in ("AbyssLeague*", "LegionLeague*", "Incursion_Temple*", "Sanctum[CNV]*",
                "Synthesis_MapGuardian*", "Expedition*", "2_11_*"):
        _glob(db, "Mechanic", pat)

    # Campaign acts 1–5: main areas 1_N_*, vaal areas 1_SideAreaN_*
    for n in range(1, 6):
        _glob(db, f"Act {n}",                  f"1_{n}_*")
        _glob(db, f"Act {n} — Vaal side area", f"1_SideArea{n}_*")

    # Campaign acts 6–10: main areas 2_N_*, vaal areas 1_SideAreaN_*
    for n in range(6, 11):
        _glob(db, f"Act {n}",                  f"2_{n}_*")
        _glob(db, f"Act {n} — Vaal side area", f"1_SideArea{n}_*")

    db.commit()
    print(f"Type rules applied ({db.total_changes - before} rows updated).")


# ── dump ─────────────────────────────────────────────────────────────────────

def sql_str(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def type_slug(area_type: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", area_type.lower()).strip("_")


def dump_to_files(db: sqlite3.Connection) -> None:
    # Clear stale files so renamed/removed types don't linger.
    for f in OUT.glob("*.sql"):
        f.unlink()

    types = [
        r[0] for r in db.execute(
            "SELECT DISTINCT type FROM areas WHERE type IS NOT NULL ORDER BY type"
        )
    ]

    for area_type in types:
        rows = db.execute(
            "SELECT code, display_name FROM areas WHERE type=? ORDER BY display_name, code",
            (area_type,),
        ).fetchall()

        lines = ["INSERT OR IGNORE INTO areas (code, type, display_name) VALUES"]
        for i, (code, display_name) in enumerate(rows):
            sep = "," if i < len(rows) - 1 else ";"
            lines.append(
                f"    ({sql_str(code)}, {sql_str(area_type)}, {sql_str(display_name)}){sep}"
            )

        path = OUT / (type_slug(area_type) + ".sql")
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"  {path.name} ({len(rows)} rows)")

    print(f"Dumped {len(types)} type files.")


# ─────────────────────────────────────────────────────────────────────────────

def main() -> None:
    if not DB.exists():
        print(f"Database not found: {DB}", file=sys.stderr)
        sys.exit(1)

    db = sqlite3.connect(DB)

    try:
        db.execute("ALTER TABLE areas ADD COLUMN type TEXT")
        db.commit()
        print("Added type column to areas table.")
    except sqlite3.OperationalError:
        pass

    apply_type_rules(db)
    dump_to_files(db)
    combine()


if __name__ == "__main__":
    main()
