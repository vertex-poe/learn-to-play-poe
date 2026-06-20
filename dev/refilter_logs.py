#!/usr/bin/env python3
# dev/refilter_logs.py (python)

"""
Concurrent log splitter using findstr. Extracts all categories in parallel
(one findstr process per category, up to os.cpu_count() at a time), then
creates the remainder with a single combined inverse pass.

Usage: python dev/refilter_logs.py [path/to/Client.txt]
"""

import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT       = Path(__file__).parent.parent
OUTDIR     = Path(__file__).parent / "filtered_client"
OUTDIR_SQL = Path(__file__).parent / "filtered_sql"
OUT        = Path(__file__).parent / "filtered_client.txt"
WORKERS    = os.cpu_count() or 4

# Lines that go into filtered_client/ — noise / engine chatter
CATEGORIES = [
    ("log_file",       ["LOG FILE"]),
    ("render",         ["[RENDER"]),
    ("shader",         ["[SHADER"]),
    ("engine",         ["[ENGINE"]),
    ("sound",          ["[SOUND"]),
    ("startup",        ["[STARTUP"]),
    ("d3d12",          ["[D3D12"]),
    ("d3d11",          ["[D3D11]"]),
    ("window",         ["[WINDOW"]),
    ("gamepadmanager", ["GamepadManager"]),
    ("job",            ["[JOB]"]),
    ("bundle2",        ["[BUNDLE2]"]),
    ("bundle",         ["[BUNDLE] "]),
    ("download",       ["[DOWNLOAD]"]),
    ("storage",        ["[STORAGE]"]),
    ("streamline",     ["[STREAMLINE]"]),
    ("gamepad",        ["[GAMEPAD]"]),
    ("ingame_audio",   ["[InGameAudioManager]"]),
    ("vulkan",         ["[VULKAN]"]),
    ("gpu_trail",      ["[GPU TRAIL]"]),
    ("dxc",            ["[DXC]"]),
    ("enumerated",     ["Enumerated"]),
    ("item_filter",    ["[Item Filter]"]),
    ("blueprint_ui",   ["[Blueprint UI]"]),
    ("faridun",        ["[Faridun]"]),
    ("entity2",        ["[ENTITY2]"]),
    ("replace_object", ["Replacing awake object with id ", "Replacing asleep object with id "]),
    ("trigger_event",  ["Trigger Event '"]),
]

# Lines silently dropped from the remainder (refiltered_client.txt) — we don't care about these
IGNORE_PREFIXES = [
    "Rebuilding UI::",
    'Changing to device "',
    "Async connecting to ",
    "Connected to ",
    "Got Instance Details ",
    "Precalc",
    "Connecting to instance server ",
    "Connect time to instance server ",
    "Tile hash: ",
    "Doodad hash: ",
    ": Your Stash Tab with the Unique Affinity does not have enough space for this item.",
    ": Not enough room for item.",
    " monsters remain.",
    "Re-ordering tabs to match stash data",
    "Re-ordering sub tabs to match stash data",
    "FindClosestObject found no nearby object",
    "Matching object found for InstanceClientActionUpdate, but no matching action was found on object to update.",
    "InstanceClientFinishChannellingAction id did not match current channelled action",
    "Unable to connect to login server.",
    "Attempted to trigger GEAL event on destructing object ",
    "Marking steam content as corrupt ",
    "Remove Timeline Component ",
    "Failed to create effect graph node ",
    " animation cannot be a looping animation! ",
    "Effect builder being destroyed without ",
    " tried to create an object out of bounds!",
    "The operation timed out.",
    "Selected XInput.dll is ",
    "Attempting to get depth for location in unconnected area",
    " after level generation was complete",
    "Tried to add Steam stat ",
    "Client-Safe Instance ID = ",
    "Resyncing ",
    "Core::OpenVirtualKeyboard called with mode '",
    "Building Uncached Shader ",
    "Attempted to remove an effect pack, \"",
    "packet for a container this skill doesn't have - packet is for wrong skill",
    "Invalid for Metadata/",
    "GEAL Triggered action with animation event - could not find specified event",
    "GEAL Triggered action with animation event was serialized to the client, but the client did not have the action.",
    "Mod did not exist",
    " with a self targetted cast.",
    "SetOrientation failed as the orientation value wasn't a location or float",
    "Received an update for a blight portal that we didn't know about",
    "Could not find row for ",
    "Steam stats stored",
    ": Item on cursor destroyed.",
    ": Failed to apply item: Item has no space for more Mods.",
    "Client couldn't execute",
    "Instant/Triggered action",
]

# Lines that go into filtered_sql/ — ingested into the database
CATEGORIES_SQL = [
    ("area_generating",    ["Generating level"]),
    ("area_entered",       ["You have entered"]),
    ("scene_set_source",   ["[SCENE] Set Source ["]),
    ("guild_joined",           ["Joined guild named"]),
    ("guild_details_changed",  ["Guild details changed "]),
    ("guild_member_updated",   ["Guild member updated "]),
    ("chat_channel_joined", ["You have joined global chat channel"]),
    ("char_level_up",       ["is now level"]),
    ("session_afk",         ["AFK mode is now"]),
    ("quest_monsters_cleared",   [": 0 monsters remain."]),
    ("passive_skill_allocated",   ["Successfully allocated passive skill",
                                   "Successfully unallocated passive skill",
                                   "Successfully allocated mastery effect",
                                   "Successfully unallocated mastery effect"]),
    ("passive_respec_received",   ["Passive Respec Points"]),
    ("passive_skill_point",       ["You have received a Passive Skill Point.",
                                   "Passive Skill Points."]),
    ("chats",                     ["] #", "] $", "] %", "] &"]),
    ("whispers",                  ["@From ", "@To "]),
    ("kitava_resistance_penalty", ["Kitava's merciless affliction"]),
    ("achievements",              ["Achivement stored:"]),
    ("played",                    ["You have played for"]),
    ("passives_command",          ["total Passive Skill Points",
                                   "total Ascendancy Skill Points",
                                   "Passive Skill Points from",
                                   ") from "]),
    ("hideout_discovered",        ["Spawning discoverable Hideout"]),
    ("pvp_queue",                 ["Queueing for PVP match", "Cancelled PVP queue"]),
    ("ruleset_failed",            ["Failed to create ruleset "]),
    ("talking_pet_audio",         ["TalkingPetAudioEvent '"]),
    ("labyrinth_craft_options",   ["InstanceClientLabyrinthCraftResultOptionsList recieved"]),
    ("steam_not_logged_in",       ["Not logged in to steam. Achievements will not work"]),
    ("patch_required",            ["There has been a patch that you need to update to."]),
]


def find_client_txt():
    if len(sys.argv) > 1:
        return Path(sys.argv[1])
    toml = ROOT / "l2p-poe1.toml"
    if toml.exists():
        text = toml.read_text(encoding="utf-8")
        block = re.search(r'install_dirs\s*=\s*\[([^\]]*)\]', text, re.DOTALL)
        if block:
            for d in re.findall(r'["\']([^"\']+)["\']', block.group(1)):
                p = Path(d) / "logs" / "Client.txt"
                if p.exists():
                    return p
    print("error: no Client.txt found — pass path as argument or set install_dirs in l2p-poe1.toml",
          file=sys.stderr)
    sys.exit(1)


# Strips the variable per-line prefix so identical messages dedup across sessions:
#   2026/06/03 22:30:25 12345678 abcdef01 [INFO Client 9012] → (removed)
# Uses [^\]]+ instead of a strict "WORD Client NNN" pattern so any bracket format matches.
STRIP_RE = re.compile(
    r'^\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} \d+ [0-9a-f]+ \[[^\]]+\] ?'
)

# Per-category substitutions applied after timestamp stripping, before dedup.
# Each entry is a list of (compiled_regex, replacement_string) pairs.
CATEGORY_NORMALIZERS: dict[str, list[tuple[re.Pattern, str]]] = {
    # log_file lines have no client-id bracket, just "YYYY/MM/DD HH:MM:SS TEXT"
    "log_file": [
        (re.compile(r'^\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} '), ""),
    ],
    "startup": [
        # elapsed times vary per run; normalise before dedup
        (re.compile(r'\b\d+(?:\.\d+)?(?:e[+-]?\d+)? seconds\b'), "<time> seconds"),
    ],
    "area_generating": [
        # seed is unique per instance; normalise so same area+level dedup across sessions
        (re.compile(r'\bwith seed \d+\b'), "with seed <seed>"),
    ],
    "char_level_up": [
        # replace char name and level so distinct chars/levels collapse to one line per class
        (re.compile(r'\S+ (\(\w+\)) is now level \d+'), r'<char> \1 is now level <level>'),
    ],
    "d3d12": [
        # LUID is a session-unique adapter identifier; normalise so identical
        # adapter lines dedup across sessions.
        (re.compile(r'\bLUID [0-9A-Fa-f]+\b'), "LUID <id>"),
    ],
    "trigger_event": [
        (re.compile(r'0x[0-9a-fA-F]+'), "<addr>"),
    ],
}


def extract_category(name, patterns, src, outdir):
    args = ["findstr"] + [f"/c:{p}" for p in patterns] + [str(src)]
    cat_file = outdir / f"{name}.txt"
    with open(cat_file, "wb") as f:
        subprocess.run(args, stdout=f, stderr=subprocess.DEVNULL)
    return name, cat_file.stat().st_size


def strip_and_dedup(path, normalizers=None):
    """Strip timestamp prefix and remove duplicate lines in-place."""
    if not path.exists() or path.stat().st_size == 0:
        return 0, 0
    # read_text normalises \r\n → \n so write_text doesn't double-convert them.
    # splitlines() (no keepends) gives clean strings with no trailing newline chars.
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    seen = set()
    out  = []
    for line in lines:
        clean = STRIP_RE.sub("", line)
        if normalizers:
            for pattern, replacement in normalizers:
                clean = pattern.sub(replacement, clean)
        if clean.strip() and clean not in seen:
            seen.add(clean)
            out.append(clean)
    path.write_text("\n".join(out) + ("\n" if out else ""), encoding="utf-8")
    return len(lines), len(out)


# ── main ────────────────────────────────────────────────────────────────────

client_txt = find_client_txt()  # resolve first so a missing file doesn't wipe output

for d in (OUTDIR, OUTDIR_SQL):
    if d.exists():
        shutil.rmtree(d)
    d.mkdir()
OUT.unlink(missing_ok=True)
size_mb = client_txt.stat().st_size / 1_000_000
print(f"source  {client_txt}  ({size_mb:.1f} MB)  workers={WORKERS}")

t0 = time.perf_counter()

# Phase 1: all category extractions in parallel
with ThreadPoolExecutor(max_workers=WORKERS) as pool:
    futures = {
        **{pool.submit(extract_category, name, patterns, client_txt, OUTDIR):     name for name, patterns in CATEGORIES},
        **{pool.submit(extract_category, name, patterns, client_txt, OUTDIR_SQL): name for name, patterns in CATEGORIES_SQL},
    }
    for f in as_completed(futures):
        name, size = f.result()
        print(f"  {name:<20}  {size // 1024:>6} KB")

t1 = time.perf_counter()
print(f"  extraction: {t1 - t0:.2f}s")

# Phase 2: chained findstr /v passes, 60 patterns per pass, to stay under findstr's
# internal pattern-count limit (~85).  Each pass writes a temp file; the last writes OUT.
_all_filter = [p for _, patterns in (*CATEGORIES, *CATEGORIES_SQL) for p in patterns] + list(IGNORE_PREFIXES)
_chunks     = [_all_filter[i:i+60] for i in range(0, len(_all_filter), 60)]
_src = client_txt
for _i, _chunk in enumerate(_chunks):
    _dst = OUT if _i == len(_chunks) - 1 else OUT.with_suffix(f".tmp{_i}")
    with open(_dst, "wb") as _f:
        subprocess.run(["findstr", "/v"] + [f"/c:{p}" for p in _chunk] + [str(_src)],
                       stdout=_f, stderr=subprocess.DEVNULL)
    if _i > 0:
        _src.unlink(missing_ok=True)
    _src = _dst

t2 = time.perf_counter()
print(f"  remainder:  {t2 - t1:.2f}s  ({OUT.stat().st_size // 1024} KB)")

# Phase 2.5: split dialog lines out of the remainder into filtered_client/dialog.txt.
# Matches lines whose message body starts with ": " (game/NPC prefix) or looks like
# "Speaker: text" (.+: .+).  Broad on purpose — will be refined later.
DIALOG_RE = re.compile(r'^: |\S[^:]*: .')
dialog_out = OUTDIR / "dialog.txt"
if OUT.exists() and OUT.stat().st_size > 0:
    raw_lines = OUT.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
    dialog, keep = [], []
    for raw in raw_lines:
        stripped = raw.rstrip("\r\n")
        body = STRIP_RE.sub("", stripped)
        if DIALOG_RE.match(body):
            dialog.append(raw)
        elif not STRIP_RE.match(stripped):
            pass  # non-timestamped continuation line — drop
        else:
            keep.append(raw)
    dialog_out.write_text("".join(dialog), encoding="utf-8")
    OUT.write_text("".join(keep), encoding="utf-8")

t25 = time.perf_counter()
dialog_kb = dialog_out.stat().st_size // 1024 if dialog_out.exists() else 0
print(f"  dialog:     {t25 - t2:.2f}s  ({dialog_kb} KB)")

# Phase 3: strip timestamp prefix and dedup every output file
targets = (
    [(OUT, None)] +
    [(dialog_out, None)] +
    [(OUTDIR     / f"{name}.txt", CATEGORY_NORMALIZERS.get(name)) for name, _ in CATEGORIES] +
    [(OUTDIR_SQL / f"{name}.txt", CATEGORY_NORMALIZERS.get(name)) for name, _ in CATEGORIES_SQL]
)
total_in = total_out = 0
for p, normalizers in targets:
    n_in, n_out = strip_and_dedup(p, normalizers)
    total_in  += n_in
    total_out += n_out

elapsed = time.perf_counter() - t0
print(f"  dedup:      {total_in:,} -> {total_out:,} lines  ({100*total_out//total_in if total_in else 0}% kept)")
print(f"done in {elapsed:.2f}s")
