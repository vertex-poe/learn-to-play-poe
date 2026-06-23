#!/usr/bin/env python3
# dev/parse_npc_dialog.py (python)

"""
Exploratory tool: parses NPC names and their dialog lines from
dev/filtered_client/dialog.txt, generating a stable SHA-256 hash for each
unique message.  Filters known player names using whispers and chat logs
(also derived from Client.txt).

Prerequisite: run dev/log_split/refilter_logs.py first to produce the filtered files.

Output: dev/log_split/filtered_client/npc_dialog_hashes.json  (gitignored — contains dialog text)
"""

import hashlib
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

# Windows consoles default to cp1252; names can contain non-BMP chars.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

DIALOG_FILE   = Path(__file__).parent / "filtered_client" / "dialog.txt"
WHISPERS_FILE = Path(__file__).parent / "filtered_sql" / "whispers.txt"
CHATS_FILE    = Path(__file__).parent / "filtered_sql" / "chats.txt"
OUTPUT_FILE   = Path(__file__).parent / "filtered_client" / "npc_dialog_hashes.json"

GAME_SPEAKER = "game"

SPEAKER_RE    = re.compile(r'^([^:]+):\s+(.+)$')
GUILD_TAG_RE  = re.compile(r'^<[^>]+>')
WHISPER_RE    = re.compile(r'^@(?:From|To) (?:<[^>]+> )?([^:]+):')
CHAT_RE       = re.compile(r'^[#$%&](?:<[^>]+> )?([^:]+):')


def load_player_names() -> set[str]:
    """Return character names confirmed as players via whispers and chat logs."""
    players: set[str] = set()
    for path, pattern in ((WHISPERS_FILE, WHISPER_RE), (CHATS_FILE, CHAT_RE)):
        if not path.exists():
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            m = pattern.match(line.strip())
            if m:
                players.add(m.group(1).strip())
    return players


def sha256_prefix(text: str, length: int = 16) -> str:
    return hashlib.sha256(text.encode()).hexdigest()[:length]


def main() -> None:
    if not DIALOG_FILE.exists():
        print(
            f"error: {DIALOG_FILE} does not exist\n"
            f"Run dev/log_split/refilter_logs.py first to generate the dialog file.",
            file=sys.stderr,
        )
        sys.exit(1)

    players = load_player_names()

    npc_lines: dict[str, list[dict]] = defaultdict(list)
    seen: set[tuple[str, str]] = set()

    for raw in DIALOG_FILE.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith(": "):
            npc, message = GAME_SPEAKER, line[2:].strip()
        else:
            m = SPEAKER_RE.match(line)
            if not m:
                continue
            npc, message = m.group(1).strip(), m.group(2).strip()
            if GUILD_TAG_RE.match(npc) or npc in players:
                continue
        key = (npc, message)
        if key in seen:
            continue
        seen.add(key)
        npc_lines[npc].append({"hash": sha256_prefix(message), "message": message})

    result = {npc: msgs for npc, msgs in sorted(npc_lines.items())}
    OUTPUT_FILE.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    total_msgs = sum(len(v) for v in result.values())
    print(f"speakers: {len(result)}  messages: {total_msgs}")
    for npc, msgs in result.items():
        print(f"  {npc} ({len(msgs)})")
    print(f"\nwrote {OUTPUT_FILE}")


if __name__ == "__main__":
    main()
