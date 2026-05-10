# BuildCoach — Scraping & Building Guide

## Requirements

```bash
pip install requests beautifulsoup4 lxml playwright
python3 -m playwright install chromium
```

---

## Scraping Snow Crows Builds

The scraper fetches all build data from snowcrows.com and produces one JSON file per profession plus a combined file.

### Scrape all 9 professions (full rebuild)

```bash
cd /home/todd/gw2-browser
python3 scraper/scrape_snowcrows.py
```

Output in the project root:
```
sc_builds_guardian.json
sc_builds_warrior.json
sc_builds_engineer.json
sc_builds_ranger.json
sc_builds_thief.json
sc_builds_elementalist.json
sc_builds_mesmer.json
sc_builds_necromancer.json
sc_builds_revenant.json
sc_builds_full.json        ← combined (written only on a full run)
```

The scraper prints a count per profession and a per-build status line as it runs:
```
── Warrior ──
  12 build page(s) found
  [ 1/12] power-bladesworn  OK [Bladesworn] DPS=38200 specs=3 skills=yes gear=19 ...
  [ 2/12] condi-berserker   OK [Berserker] ...
  ...
```

### Re-scrape a single profession

Use this when one profession's builds are missing or outdated — it only touches that profession's file and leaves the others alone.

```bash
python3 scraper/scrape_snowcrows.py --prof warrior
```

This writes `sc_builds_warrior.json` only. The combined `sc_builds_full.json` is **not** modified — `./build.sh --regen` handles the merge (see below).

### Force re-fetch (ignore disk cache)

The scraper caches downloaded pages in `scraper/.scraper_cache/`. To bypass the cache entirely:

```bash
python3 scraper/scrape_snowcrows.py --force
# or for one profession:
python3 scraper/scrape_snowcrows.py --prof warrior --force
```

### Quick test (limit builds per profession)

```bash
python3 scraper/scrape_snowcrows.py --prof guardian --limit 3
```

---

## Building the DLL

### Normal build (no data changes)

```bash
./build.sh
```

### Build + regenerate embedded data

Run this after any scraping. It:
1. **Merges** all `sc_builds_*.json` per-profession files into `sc_builds_full.json`
   (per-profession data wins; professions not re-scraped are kept from the existing combined file)
2. **Regenerates** `src/api/weapon_type_db.h` — the compiled-in weapon type lookup table
   (reads from the freshly-merged `sc_builds_full.json` so all 9 professions are covered)
3. **Re-embeds** `sc_builds_full.json` into `src/sc_builds_embedded.h` as a C++ byte array
4. **Compiles** the DLL

```bash
./build.sh --regen
```

### Clean build

```bash
./build.sh --clean
```

### Install directly to GW2

```bash
./build.sh --install=/path/to/Guild Wars 2/addons
# or set the env var:
export GW2_ADDONS_DIR="/path/to/Guild Wars 2/addons"
./build.sh --install
```

---

## Deploying Updated Build Data Without Rebuilding the DLL

If you just want to update the in-game data without a full recompile, copy the per-profession JSON files directly into the addon's cache folder on the Windows machine:

```
<GW2 install>\addons\BuildCoach\cache\sc_builds_warrior.json
<GW2 install>\addons\BuildCoach\cache\sc_builds_guardian.json
... etc.
```

The addon checks for per-profession files on startup. Any profession file found in the cache takes priority over the embedded data. Professions without a cache file continue to use the data bundled into the DLL.

---

## How the Addon Loads Build Data

On startup, the addon loads builds in this priority order:

1. **Per-profession cache files** (`sc_builds_guardian.json`, `sc_builds_warrior.json`, etc.) — if present, these override the combined file for their profession
2. **Combined cache file** (`sc_builds_full.json`) — used for any professions not covered by a per-profession file
3. **Embedded data** — on first install (or if no cache files exist), the DLL deploys its bundled `sc_builds_full.json` to the cache folder automatically

This means you can update just warrior builds by dropping `sc_builds_warrior.json` in the cache folder — no DLL recompile needed.

---

## Typical Workflows

### "Warrior builds are missing — fix without re-scraping everything"

```bash
python3 scraper/scrape_snowcrows.py --prof warrior
./build.sh --regen
# then install the new DLL, or copy sc_builds_warrior.json to the cache folder
```

### "SC updated their builds — refresh everything"

```bash
python3 scraper/scrape_snowcrows.py --force
./build.sh --regen --install
```

### "Just rebuild the DLL, data is unchanged"

```bash
./build.sh
```

---

## All build.sh Flags

| Flag | Effect |
|------|--------|
| `--regen` | Merge per-prof JSONs, regenerate weapon type DB and embedded header |
| `--clean` | Delete `build/` before configuring (full recompile) |
| `--install` | Copy DLL + JSON to `$GW2_ADDONS_DIR/BuildCoach/` |
| `--install=<path>` | Same, with an explicit path |

## All scraper flags

| Flag | Effect |
|------|--------|
| `--prof <name>` | Only scrape this profession (e.g. `warrior`) |
| `--force` | Clear disk cache and re-fetch all pages from the web |
| `--limit <n>` | Max builds per profession (useful for testing) |
| `--delay <s>` | Seconds between HTTP requests (default 1.5) |
| `--out <path>` | Override output path for combined file (default: `sc_builds_full.json`) |
