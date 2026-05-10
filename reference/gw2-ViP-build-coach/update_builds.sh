#!/usr/bin/env bash
# update_builds.sh — Re-scrape Snow Crows and rebuild the DLL after a patch.
#
# Usage:
#   ./update_builds.sh                      # scrape + regen + build
#   ./update_builds.sh --install=<path>     # also copy DLL + JSON to GW2 addons dir
#   ./update_builds.sh --prof guardian      # one profession only (quick test)
#   ./update_builds.sh --force              # clear scraper cache (full re-fetch)
#   ./update_builds.sh --delay 2.0          # slower scraping (be kind to SC servers)
#
# Requirements (first time only):
#   pip install requests beautifulsoup4 lxml playwright
#   python3 -m playwright install chromium

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- Parse args -------------------------------------------------------------
SCRAPER_ARGS=()
BUILD_ARGS=("--regen")
INSTALL_PATH=""

for arg in "$@"; do
    case "$arg" in
        --install=*) INSTALL_PATH="${arg#--install=}"; BUILD_ARGS+=("$arg") ;;
        --install)   BUILD_ARGS+=("--install") ;;
        --force)     SCRAPER_ARGS+=("--force") ;;
        --delay=*)   SCRAPER_ARGS+=("--delay" "${arg#--delay=}") ;;
        --delay)     shift; SCRAPER_ARGS+=("--delay" "$1") ;;
        --prof=*)    SCRAPER_ARGS+=("--prof" "${arg#--prof=}") ;;
        --prof)      shift; SCRAPER_ARGS+=("--prof" "$1") ;;
        --limit=*)   SCRAPER_ARGS+=("--limit" "${arg#--limit=}") ;;
        -h|--help)
            sed -n '2,14p' "$0" | sed 's/^# //'
            exit 0 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

echo "=============================="
echo " BuildCoach — Update SC Builds"
echo "=============================="
echo

# --- Step 1: Scrape Snow Crows ----------------------------------------------
echo "Step 1/3  Scraping Snow Crows..."
python3 "$SCRIPT_DIR/scraper/scrape_snowcrows.py" "${SCRAPER_ARGS[@]+"${SCRAPER_ARGS[@]}"}"

echo

# --- Step 2: Regenerate embedded header + rebuild DLL -----------------------
echo "Step 2/3  Regenerating embedded header..."
echo "Step 3/3  Building DLL..."
bash "$SCRIPT_DIR/build.sh" "${BUILD_ARGS[@]}"

echo
echo "Done!  Builds in: $SCRIPT_DIR/sc_builds_full.json"
echo "       DLL at:    $SCRIPT_DIR/build/BuildCoach.dll"

if [[ -n "$INSTALL_PATH" ]]; then
    echo "       Installed to: $INSTALL_PATH"
fi
echo
echo "If GW2 is running, reload the addon in Nexus to pick up the new builds."
