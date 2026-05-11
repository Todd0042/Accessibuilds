#!/usr/bin/env python3
"""Fix Ranger pet IDs in sc_builds_full.json by fetching each ranger build page
with Playwright, extracting pet names from sc-tile data-armory-id elements,
and mapping them to correct /v2/pets API IDs."""

import json, re, sys, os, time
from playwright.sync_api import sync_playwright

# Name → ID lookup from /v2/pets (without "Juvenile " prefix, lowercase)
PET_NAME_TO_ID = {
    "jungle stalker": 1, "boar": 2, "lynx": 3, "krytan drakehound": 4,
    "brown bear": 5, "carrion devourer": 6, "salamander drake": 7,
    "alpine wolf": 8, "snow leopard": 9, "raven": 10, "jaguar": 11,
    "marsh drake": 12, "blue moa": 13, "white moa": 14, "pink moa": 15,
    "black moa": 16, "red moa": 17, "ice drake": 18, "river drake": 19,
    "murellow": 20, "shark": 21, "fern hound": 22, "black bear": 23,
    "polar bear": 24, "arctodus": 25, "whiptail devourer": 26,
    "lashtail devourer": 27, "hyena": 28, "wolf": 29, "owl": 30,
    "eagle": 31, "white raven": 32, "forest spider": 33, "jungle spider": 34,
    "cave spider": 35, "black widow spider": 36, "warthog": 37,
    "siamoth": 38, "pig": 39, "armor fish": 40, "blue jellyfish": 41,
    "red jellyfish": 42, "rainbow jellyfish": 43, "hawk": 44,
    "reef drake": 45, "smokescale": 46, "tiger": 47, "electric wyvern": 48,
    "fire wyvern": 51, "bristleback": 52, "cheetah": 54, "sand lion": 55,
    "jacaranda": 57, "rock gazelle": 59, "fanged iboga": 61,
    "white tiger": 63, "wallow": 64, "phoenix": 65, "siege turtle": 66,
    "aether hunter": 67, "sky-chak striker": 68, "spinegazer": 69,
    "warclaw": 70, "janthiri bee": 71, "bee": 71, "raptor swiftwing": 72,
}

def resolve_pet_id(name: str) -> int:
    key = name.lower().strip()
    if key.startswith("juvenile "):
        key = key[9:]
    # Direct lookup
    pid = PET_NAME_TO_ID.get(key, 0)
    if pid: return pid
    # Substring match: some SC names are truncated (e.g. "Bee" for "Janthiri Bee")
    for k, v in PET_NAME_TO_ID.items():
        if key in k or k in key:
            return v
    return 0

def extract_pets_from_html(html: str):
    """Extract pet IDs from sc-tile spans in the build header."""
    pets = []
    # Pattern 1: standard sc-tile with data-tippy-content
    for m in re.finditer(
        r'<span[^>]*data-armory-id="(\d+)"[^>]*class="[^"]*sc-tile[^"]*"[^>]*data-tippy-content="([^"]*)"',
        html
    ):
        name = m.group(2)
        pid = resolve_pet_id(name)
        if pid > 0:
            pets.append(pid)
    # Pattern 2: sc-tile inside a table cell (sometimes rendered differently)
    if not pets:
        for m in re.finditer(
            r'<span[^>]*data-armory-id="(\d+)"[^>]*data-armory-title="([^"]*)"[^>]*class="[^"]*sc-tile[^"]*"',
            html
        ):
            name = m.group(2)
            pid = resolve_pet_id(name)
            if pid > 0:
                pets.append(pid)
    return pets[:2]

def fixup_json(path: str, max_builds: int = 0):
    with open(path) as f:
        builds = json.load(f)

    ranger_urls = [(i, b) for i, b in enumerate(builds)
                   if b.get("profession") == "Ranger" and b.get("pets")]

    if max_builds > 0:
        ranger_urls = ranger_urls[:max_builds]

    print(f"Found {len(ranger_urls)} Ranger builds with pets to fix")

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        context = browser.new_context(
            user_agent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        )

        fixed = 0
        errors = 0
        for idx, b in ranger_urls:
            url = b.get("source_url", "")
            slug = b.get("id", url)

            print(f"  [{fixed+1}/{len(ranger_urls)}] {slug}...", end=" ", flush=True)
            try:
                page = context.new_page()
                page.goto(url, wait_until="domcontentloaded", timeout=30000)
                time.sleep(2.5)
                html = page.content()
                page.close()

                new_pets = extract_pets_from_html(html)
                old_pets = b["pets"]

                if len(new_pets) >= 1 and new_pets[0]:
                    # Pad to 2 elements
                    while len(new_pets) < 2:
                        new_pets.append(0)
                    b["pets"] = [new_pets[0], new_pets[1]]
                    print(f"OK {old_pets} -> {[new_pets[0], new_pets[1]]}")
                    fixed += 1
                else:
                    print(f"NO PETS FOUND from {old_pets}")
                    errors += 1

            except Exception as e:
                print(f"ERROR: {e}")
                errors += 1

        browser.close()

    print(f"\nFixed {fixed}/{len(ranger_urls)} builds ({errors} errors)")

    # Save updated JSON
    with open(path, "w") as f:
        json.dump(builds, f, indent=2)
    print(f"Saved {path}")

if __name__ == "__main__":
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    json_path = os.path.join(repo_root, "sc_builds_full.json")
    max_b = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    fixup_json(json_path, max_b)
