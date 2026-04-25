#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Parse flags
CLEAN=0
INSTALL=0
GW2_ADDONS_DIR="${GW2_ADDONS_DIR:-}"

REGEN=0
for arg in "$@"; do
    case "$arg" in
        --clean)   CLEAN=1 ;;
        --regen)   REGEN=1 ;;
        --install) INSTALL=1 ;;
        --install=*) INSTALL=1; GW2_ADDONS_DIR="${arg#--install=}" ;;
        -h|--help)
            echo "Usage: $0 [--clean] [--regen] [--install[=<path>]]"
            echo "  --clean            Remove build/ before configuring"
            echo "  --regen            Regenerate src/sc_builds_embedded.h from sc_builds_full.json"
            echo "  --install          Deploy DLL + JSON to \$GW2_ADDONS_DIR/BuildCoach/"
            echo "  --install=<path>   Same, using an explicit path"
            exit 0
            ;;
    esac
done

if [[ $REGEN -eq 1 ]]; then
    # Merge per-profession files into sc_builds_full.json before anything reads it.
    # Per-prof files take priority over existing full data for their profession;
    # professions not covered by a per-prof file are kept from the existing full file.
    echo "Merging per-profession JSON files into sc_builds_full.json ..."
    python3 - << 'PYEOF'
import json, pathlib, sys

root = pathlib.Path(".")
per_prof_files = sorted(
    f for f in root.glob("sc_builds_*.json")
    if f.name != "sc_builds_full.json"
)

if not per_prof_files:
    print("  No per-profession files found — using existing sc_builds_full.json")
    sys.exit(0)

# Load per-profession files; track which professions they cover
by_prof: dict[str, list] = {}
for f in per_prof_files:
    builds = json.loads(f.read_text(encoding="utf-8"))
    for b in builds:
        p = b.get("profession", "").lower()
        by_prof.setdefault(p, []).append(b)
    print("  %s: %d builds" % (f.name, len(builds)))

covered = set(by_prof.keys())

# Supplement with existing full file for any professions not re-scraped
full_path = root / "sc_builds_full.json"
retained = 0
if full_path.exists():
    existing = json.loads(full_path.read_text(encoding="utf-8"))
    for b in existing:
        p = b.get("profession", "").lower()
        if p not in covered:
            by_prof.setdefault(p, []).append(b)
            retained += 1
    if retained:
        print("  sc_builds_full.json: kept %d builds from %d uncovered profession(s)" % (
            retained, len({b.get("profession","").lower() for b in existing if b.get("profession","").lower() not in covered})))

all_builds = [b for builds in by_prof.values() for b in builds]
full_path.write_text(json.dumps(all_builds, indent=2, ensure_ascii=False), encoding="utf-8")
print("  Total: %d builds → sc_builds_full.json" % len(all_builds))
PYEOF

    # Increment database version
    VERSION_FILE="$SCRIPT_DIR/sc_builds_version.txt"
    if [[ -f "$VERSION_FILE" ]]; then
        CURRENT_VER=$(cat "$VERSION_FILE")
    else
        CURRENT_VER=0
    fi
    NEW_VER=$((CURRENT_VER + 1))
    echo "$NEW_VER" > "$VERSION_FILE"
    echo "Database version: $NEW_VER (was $CURRENT_VER)"

    echo "Regenerating src/api/weapon_type_db.h ..."
    python3 scraper/build_weapon_db.py

    echo "Regenerating src/sc_builds_embedded.h ..."
    python3 - << PYEOF
import subprocess, re, sys
version = int(open("sc_builds_version.txt").read().strip())
raw = subprocess.check_output(["xxd", "-i", "sc_builds_full.json"]).decode()
raw = re.sub(r'unsigned char \S+\[\]', 'static const unsigned char sc_builds_json[]', raw)
raw = re.sub(r'unsigned int \S+',      'static const size_t sc_builds_json_len',       raw)
header = ("/* Auto-generated from sc_builds_full.json — do not edit. "
          "Regenerate with ./build.sh --regen */\n#pragma once\n#include <stddef.h>\n"
          "static const int sc_builds_version = %d;\n" % version + raw)
with open("src/sc_builds_embedded.h", "w") as f:
    f.write(header)
print("  %d bytes embedded" % len(raw))
PYEOF
fi

if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
    echo "Cleaning $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

echo "Configuring ..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/mingw-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \

echo "Building ..."
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

DLL="$BUILD_DIR/BuildCoach.dll"
if [[ -f "$DLL" ]]; then
    echo "Built: $DLL"
else
    echo "ERROR: DLL not found at $DLL" >&2
    exit 1
fi

if [[ $INSTALL -eq 1 ]]; then
    if [[ -z "$GW2_ADDONS_DIR" ]]; then
        echo "ERROR: --install requires \$GW2_ADDONS_DIR to be set or passed as --install=<path>" >&2
        exit 1
    fi
    DEST="$GW2_ADDONS_DIR/BuildCoach"
    CACHE="$DEST/cache"
    mkdir -p "$CACHE"

    cp "$DLL" "$DEST/BuildCoach.dll"
    echo "Installed: $DEST/BuildCoach.dll"

    if [[ -f "$SCRIPT_DIR/sc_builds_full.json" ]]; then
        cp "$SCRIPT_DIR/sc_builds_full.json" "$CACHE/sc_builds_full.json"
        echo "Installed: $CACHE/sc_builds_full.json"
    fi
fi
