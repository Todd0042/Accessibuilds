#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Parse flags
CLEAN=0
INSTALL=0
REGEN=0
GW2_ADDONS_DIR="${GW2_ADDONS_DIR:-}"

for arg in "$@"; do
    case "$arg" in
        --clean)   CLEAN=1 ;;
        --regen)   REGEN=1 ;;
        --install) INSTALL=1 ;;
        --install=*) INSTALL=1; GW2_ADDONS_DIR="${arg#--install=}" ;;
        -h|--help)
            echo "Usage: $0 [--clean] [--regen] [--install[=<path>]]"
            echo "  --clean            Remove build/ before configuring"
            echo "  --regen            Regenerate embedded headers from JSON files:"
            echo "                       - src/sc_builds_embedded.h (binary xxd format)"
            echo "                       - src/sc_builds_offline.h (C string format)"
            echo "  --install          Deploy DLL to \$GW2_ADDONS_DIR/Accessibuilds/"
            echo "  --install=<path>   Same, using an explicit path"
            exit 0
            ;;
    esac
done

if [[ $REGEN -eq 1 ]]; then
    echo "Regenerating src/sc_builds_embedded.h ..."
    python3 - << 'PYEOF'
import subprocess, re, sys, os, json, pathlib

root = pathlib.Path(".")
version_file = root / "sc_builds_version.txt"
if version_file.exists():
    version = int(version_file.read_text().strip())
else:
    version = 1

def embed_file(src: str, var_name: str) -> str:
    raw = subprocess.check_output(["xxd", "-i", src]).decode()
    raw = re.sub(r'unsigned char \S+\[\]', f'static const unsigned char {var_name}[]', raw)
    raw = re.sub(r'unsigned int \S+',      f'static const size_t {var_name}_len',       raw)
    return raw

lines = []
lines.append("/* Auto-generated — do not edit. Regenerate with ./build.sh --regen */")
lines.append("#pragma once")
lines.append("#include <stddef.h>")
lines.append("")
lines.append("static const int sc_builds_version = %d;" % version)
lines.append("")

# Main builds (may be empty)
if os.path.exists("sc_builds_full.json"):
    main_raw = embed_file("sc_builds_full.json", "sc_builds_json")
    lines.append(main_raw)
    print("  Main data: %d bytes" % len(main_raw))
else:
    lines.append("static const unsigned char sc_builds_json[] = {0x5b, 0x5d}; /* [] */")
    lines.append("static const size_t sc_builds_json_len = 2;")
    print("  Main data: no file, embedded empty array")

# Accessibility builds
if os.path.exists("sc_builds_accessibility.json"):
    acc_raw = embed_file("sc_builds_accessibility.json", "sc_builds_accessibility_json")
    lines.append(acc_raw)
    with open("sc_builds_accessibility.json") as f:
        count = len(json.load(f))
    print("  Accessibility: %d builds, %d bytes" % (count, len(acc_raw)))
else:
    lines.append("static const unsigned char sc_builds_accessibility_json[] = {0x5b, 0x5d}; /* [] */")
    lines.append("static const size_t sc_builds_accessibility_json_len = 2;")
    print("  Accessibility: no file, embedded empty array")

# Rotation data
if os.path.exists("sc_rotations_accessibility.json"):
    rot_raw = embed_file("sc_rotations_accessibility.json", "sc_rotations_accessibility_json")
    lines.append(rot_raw)
    with open("sc_rotations_accessibility.json") as f:
        count = len(json.load(f))
    print("  Rotations: %d builds, %d bytes" % (count, len(rot_raw)))
else:
    lines.append("static const unsigned char sc_rotations_accessibility_json[] = {0x7b, 0x7d}; /* {} */")
    lines.append("static const size_t sc_rotations_accessibility_json_len = 2;")
    print("  Rotations: no file, embedded empty object")

with open("src/sc_builds_embedded.h", "w") as f:
    f.write("\n".join(lines) + "\n")
print("  Done.")
PYEOF

    echo "Regenerating src/sc_builds_offline.h ..."
    python3 tools/embed_builds.py
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

DLL="$BUILD_DIR/Accessibuilds.dll"
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
    DEST="$GW2_ADDONS_DIR/Accessibuilds"
    mkdir -p "$DEST"
    cp "$DLL" "$DEST/Accessibuilds.dll"
    echo "Installed: $DEST/Accessibuilds.dll"
fi
