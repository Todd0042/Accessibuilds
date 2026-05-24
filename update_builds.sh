#!/usr/bin/env bash
# update_builds.sh — Re-scrape Snow Crows + Hardstuck and rebuild the DLL.
#
# Usage:
#   ./update_builds.sh                      # scrape all + regen + build
#   ./update_builds.sh --install=<path>     # also copy DLL to GW2 addons dir
#   ./update_builds.sh --sc-only            # Snow Crows only
#   ./update_builds.sh --hs-only            # Hardstuck only
#   ./update_builds.sh --prof guardian      # one profession only (both scrapers)
#   ./update_builds.sh --force              # clear scraper cache (full re-fetch)
#   ./update_builds.sh --delay 2.0          # slower scraping (be kind to servers)
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
RUN_SC=1
RUN_HS=1
RUN_MB=1

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
        --sc-only)   RUN_HS=0; RUN_MB=0 ;;
        --hs-only)   RUN_SC=0; RUN_MB=0 ;;
        --mb-only)   RUN_SC=0; RUN_HS=0 ;;
        -h|--help)
            sed -n '2,17p' "$0" | sed 's/^# //'
            exit 0 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

TOTAL_STEPS=$(( 2 + RUN_SC + RUN_HS + RUN_MB ))
STEP=0

echo "================================="
echo " BuildCoach — Update All Builds"
echo "================================="
echo

# --- Scrape Snow Crows -------------------------------------------------------
if [[ $RUN_SC -eq 1 ]]; then
    STEP=$(( STEP + 1 ))
    echo "Step $STEP/$TOTAL_STEPS  Scraping Snow Crows..."
    python3 "$SCRIPT_DIR/scraper/scrape_snowcrows.py" "${SCRAPER_ARGS[@]+"${SCRAPER_ARGS[@]}"}"
    echo
fi

# --- Scrape Hardstuck --------------------------------------------------------
if [[ $RUN_HS -eq 1 ]]; then
    STEP=$(( STEP + 1 ))
    echo "Step $STEP/$TOTAL_STEPS  Scraping Hardstuck..."
    python3 "$SCRIPT_DIR/scraper/scrape_hardstuck.py" "${SCRAPER_ARGS[@]+"${SCRAPER_ARGS[@]}"}"
    echo
fi

# --- Scrape MetaBattle -------------------------------------------------------
if [[ $RUN_MB -eq 1 ]]; then
    STEP=$(( STEP + 1 ))
    echo "Step $STEP/$TOTAL_STEPS  Scraping MetaBattle..."
    python3 "$SCRIPT_DIR/scraper/scrape_metabattle.py" "${SCRAPER_ARGS[@]+"${SCRAPER_ARGS[@]}"}"
    echo
fi

# --- Regenerate embedded headers + rebuild DLL -------------------------------
STEP=$(( STEP + 1 ))
echo "Step $STEP/$TOTAL_STEPS  Regenerating embedded headers..."
STEP=$(( STEP + 1 ))
echo "Step $STEP/$TOTAL_STEPS  Building DLL..."
bash "$SCRIPT_DIR/build.sh" "${BUILD_ARGS[@]}"

echo
echo "Done!  Build data in: $SCRIPT_DIR/full-builds/"
echo "       DLL at:        $SCRIPT_DIR/build/BuildCoach.dll"

if [[ -n "$INSTALL_PATH" ]]; then
    echo "       Installed to: $INSTALL_PATH"
fi
echo
echo "If GW2 is running, reload the addon in Nexus to pick up the new builds."
