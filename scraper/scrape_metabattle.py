#!/usr/bin/env python3
"""
MetaBattle build scraper for BuildCoach.

MetaBattle uses the same GW2 Armory embed format as Snow Crows, so the HTML
parsing mirrors what the C++ MetaBattle::ParseBuildPage() already does in the
addon.  Key structural landmarks (sourced from metabattle.cpp):

  id="firstHeading"           → build name
  build-template-code div     → [&chat_code]
  specialization-embed-row    → traits (data-armory-embed="specializations")
  id="skill_bar" + >utility<  → skill bar (data-armory-embed="skills", 5 IDs)
  id="eq-box-1"               → armor + trinkets  (data-armory-embed="items")
  id="eq-box-2/3"             → weapon sets
  id="consumables"            → food / utility items
  <small>Rune…                → rune (assigned to all armor upgrade slots)
  gw2skills.net links         → stored verbatim as gw2skills_url

Pages are fetched with plain requests (no JS rendering needed — MediaWiki
renders armory embeds server-side).  Category discovery uses the MediaWiki API.

Extra fields vs Snow Crows schema:
  game_mode     — "Raid" | "Fractal" | "WvW" | "PvP" | "OpenWorld" | "Strike"
  tier          — "Meta" | "Great" | "Good" | "Viable" | ""
  source        — "metabattle"
  gw2skills_url — first gw2skills.net editor link on the page, or ""

Output (default: ../mb-builds/):
  mb_builds_<profession>.json   per-profession, upserted on re-run
  mb_builds_full.json           combined

Usage:
  python3 scrape_metabattle.py                          # all game modes
  python3 scrape_metabattle.py --mode Raid              # one game mode
  python3 scrape_metabattle.py --mode Fractal --limit 3 # quick test
  python3 scrape_metabattle.py --url "https://metabattle.com/wiki/Build:Guardian_-_Power_Dragonhunter"
  python3 scrape_metabattle.py --force                  # clear cache, re-fetch

Requirements:
  pip install requests beautifulsoup4 lxml
"""

import argparse
import json
import re
import shutil
import sys
import time
from pathlib import Path
from typing import Optional

try:
    import requests
    from bs4 import BeautifulSoup
except ImportError:
    print("ERROR: pip install requests beautifulsoup4 lxml")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
BASE_URL   = "https://metabattle.com/wiki"
API_URL    = "https://metabattle.com/wiki/api.php"
SCRIPT_DIR = Path(__file__).parent
DEFAULT_OUT_DIR = SCRIPT_DIR / ".." / "mb-builds"
CACHE_DIR  = SCRIPT_DIR / ".mb_cache"

HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/124.0.0.0 Safari/537.36"
    ),
    "Accept":          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "Accept-Language": "en-US,en;q=0.9",
}

# Game mode → MetaBattle category name candidates (tried in order until one returns builds)
GAME_MODE_CATEGORIES: dict[str, list[str]] = {
    "Raid":      ["Raid_builds",           "Raid builds"],
    "Fractal":   ["Fractal_builds",        "Fractal builds"],
    "WvW":       ["WvW_builds",            "World_vs_World_builds",     "WvW builds"],
    "PvP":       ["PvP_builds",            "Player_vs_Player_builds",   "PvP builds"],
    "OpenWorld": ["Open_World_builds",     "Open World builds",         "Openworld_builds"],
    "Strike":    ["Strike_Mission_builds", "Strike_builds"],
}

PROFESSIONS = [
    "Guardian", "Warrior", "Engineer", "Ranger", "Thief",
    "Elementalist", "Mesmer", "Necromancer", "Revenant",
]
PROFESSION_SLUG_MAP: dict[str, str] = {p.lower(): p for p in PROFESSIONS}

# Spec name in URL slug → (profession, elite_spec display name)
SPEC_SLUG_MAP: dict[str, tuple[str, str]] = {
    "dragonhunter":  ("Guardian",     "Dragonhunter"),
    "firebrand":     ("Guardian",     "Firebrand"),
    "willbender":    ("Guardian",     "Willbender"),
    "luminary":      ("Guardian",     "Luminary"),
    "berserker":     ("Warrior",      "Berserker"),
    "spellbreaker":  ("Warrior",      "Spellbreaker"),
    "bladesworn":    ("Warrior",      "Bladesworn"),
    "paragon":       ("Warrior",      "Paragon"),
    "scrapper":      ("Engineer",     "Scrapper"),
    "holosmith":     ("Engineer",     "Holosmith"),
    "mechanist":     ("Engineer",     "Mechanist"),
    "amalgam":       ("Engineer",     "Amalgam"),
    "druid":         ("Ranger",       "Druid"),
    "soulbeast":     ("Ranger",       "Soulbeast"),
    "untamed":       ("Ranger",       "Untamed"),
    "galeshot":      ("Ranger",       "Galeshot"),
    "daredevil":     ("Thief",        "Daredevil"),
    "deadeye":       ("Thief",        "Deadeye"),
    "specter":       ("Thief",        "Specter"),
    "antiquary":     ("Thief",        "Antiquary"),
    "tempest":       ("Elementalist", "Tempest"),
    "weaver":        ("Elementalist", "Weaver"),
    "catalyst":      ("Elementalist", "Catalyst"),
    "evoker":        ("Elementalist", "Evoker"),
    "chronomancer":  ("Mesmer",       "Chronomancer"),
    "mirage":        ("Mesmer",       "Mirage"),
    "virtuoso":      ("Mesmer",       "Virtuoso"),
    "troubadour":    ("Mesmer",       "Troubadour"),
    "reaper":        ("Necromancer",  "Reaper"),
    "scourge":       ("Necromancer",  "Scourge"),
    "harbinger":     ("Necromancer",  "Harbinger"),
    "ritualist":     ("Necromancer",  "Ritualist"),
    "herald":        ("Revenant",     "Herald"),
    "renegade":      ("Revenant",     "Renegade"),
    "vindicator":    ("Revenant",     "Vindicator"),
    "conduit":       ("Revenant",     "Conduit"),
}

SPEC_ID_NAME: dict[int, str] = {
    16: "Radiance",      42: "Virtues",      46: "Honor",
    27: "Dragonhunter",  62: "Firebrand",    65: "Willbender",
    4:  "Strength",      51: "Arms",         22: "Defense",
    18: "Berserker",     61: "Spellbreaker", 68: "Bladesworn",
    6:  "Firearms",      47: "Inventions",   29: "Explosives",
    43: "Scrapper",      57: "Holosmith",    70: "Mechanist",
    8:  "Skirmishing",   25: "Marksmanship", 30: "Nature Magic",
    5:  "Druid",         55: "Soulbeast",    72: "Untamed",
    28: "Shadow Arts",   20: "Deadly Arts",  35: "Critical Strikes",
    7:  "Daredevil",     58: "Deadeye",      71: "Specter",
    31: "Fire",          26: "Water",        37: "Arcane",
    48: "Tempest",       56: "Weaver",       67: "Catalyst",
    10: "Domination",    23: "Illusions",    24: "Chaos",
    40: "Chronomancer",  59: "Mirage",       66: "Virtuoso",
    50: "Soul Reaping",  2:  "Spite",        39: "Death Magic",
    34: "Reaper",        60: "Scourge",      64: "Harbinger",
    3:  "Salvation",     14: "Retribution",  9:  "Corruption",
    52: "Herald",        63: "Renegade",     69: "Vindicator",
    81: "Luminary",  74: "Paragon",    75: "Amalgam",
    78: "Galeshot",  77: "Antiquary",  80: "Evoker",
    73: "Troubadour", 76: "Ritualist", 79: "Conduit",
}

ELITE_SPEC_IDS: set[int] = {
    27, 62, 65,  18, 61, 68,  43, 57, 70,  5,  55, 72,
    7,  58, 71,  48, 56, 67,  40, 59, 66,  34, 60, 64,
    52, 63, 69,  81, 74, 75,  78, 77, 80,  73, 76, 79,
}

TIER_KEYWORDS = ["meta", "great", "good", "viable"]

BUILD_TYPE_KEYWORDS: dict[str, str] = {
    "quick": "Quickness", "alac": "Alacrity",
    "heal":  "Heal",      "healer": "Heal",
    "condi": "Condi",     "condition": "Condi",
    "power": "Power",     "support": "Support",
}

# Gear slot label → canonical slot name (matches Snow Crows scraper)
SLOT_MAP: dict[str, str] = {
    "head":       "Helm",
    "shoulders":  "Shoulders",
    "chest":      "Chest",
    "hands":      "Gloves",
    "legs":       "Leggings",
    "feet":       "Boots",
    "backpiece":  "BackItem",
    "back":       "BackItem",
    "amulet":     "Amulet",
}
REPEATING_SLOTS = {
    "accessory": ["Accessory1", "Accessory2"],
    "ring":      ["Ring1",      "Ring2"],
}

# ---------------------------------------------------------------------------
# HTTP session + disk cache
# ---------------------------------------------------------------------------
CACHE_DIR.mkdir(exist_ok=True)
_session = requests.Session()
_session.headers.update(HEADERS)


def _cache_path(key: str, ext: str = ".html") -> Path:
    safe = re.sub(r"[^a-zA-Z0-9_-]", "_", key)
    return CACHE_DIR / (safe[:200] + ext)


def fetch_html(url: str, *, force: bool = False, delay: float = 1.5) -> Optional[str]:
    cp = _cache_path("page_" + url)
    if not force and cp.exists():
        return cp.read_text(encoding="utf-8")
    time.sleep(delay)
    try:
        r = _session.get(url, timeout=30)
        r.raise_for_status()
        cp.write_text(r.text, encoding="utf-8")
        return r.text
    except Exception as exc:
        print("    WARN fetch %s: %s" % (url, exc))
        return None


def api_get(params: dict, cache_key: str, *, force: bool = False, delay: float = 1.0) -> Optional[dict]:
    cp = _cache_path("api_" + cache_key, ext=".json")
    if not force and cp.exists():
        try:
            return json.loads(cp.read_text(encoding="utf-8"))
        except Exception:
            pass
    time.sleep(delay)
    try:
        r = _session.get(API_URL, params=params, timeout=30)
        r.raise_for_status()
        data = r.json()
        cp.write_text(json.dumps(data), encoding="utf-8")
        return data
    except Exception as exc:
        print("    WARN api_get %s: %s" % (cache_key, exc))
        return None


# ---------------------------------------------------------------------------
# HTML parsing helpers (mirroring metabattle.cpp logic)
# ---------------------------------------------------------------------------

def _parse_int_list(s: str) -> list[int]:
    result = []
    for tok in (s or "").split(","):
        tok = tok.strip()
        if tok and tok.lstrip("-").isdigit():
            result.append(int(tok))
    return result


def extract_build_name(soup: BeautifulSoup) -> str:
    """id="firstHeading" → strip optional "Build:" prefix."""
    el = soup.find(id="firstHeading")
    if not el:
        el = soup.find(id="firstheading")  # case fallback
    if not el:
        return ""
    name = el.get_text(strip=True)
    name = re.sub(r"^Build:\s*", "", name).strip()
    return name


def extract_chat_code(soup: BeautifulSoup) -> str:
    """build-template-code div contains the [&…] chat link."""
    el = soup.find(class_="build-template-code")
    if not el:
        # try class containing the keyword
        for candidate in soup.find_all(class_=True):
            if "build-template-code" in " ".join(candidate.get("class", [])):
                el = candidate
                break
    if not el:
        return ""
    text = el.get_text(strip=True)
    m = re.search(r'\[&[A-Za-z0-9+/=]+\]', text)
    return m.group(0) if m else ""


def extract_traits(soup: BeautifulSoup) -> list[dict]:
    """
    Parse specialization-embed-row elements (up to 3).
    Each row carries data-armory-embed="specializations", data-armory-ids="{spec_id}",
    and data-armory-{spec_id}-traits="{t1},{t2},{t3}".
    """
    spec_lines: list[dict] = []

    for el in soup.find_all(class_="specialization-embed-row"):
        if el.get("data-armory-embed") != "specializations":
            continue
        ids_str = (el.get("data-armory-ids") or "").strip()
        if not ids_str.isdigit():
            continue
        spec_id = int(ids_str)
        traits_str = el.get("data-armory-%d-traits" % spec_id, "") or ""
        traits = _parse_int_list(traits_str)
        while len(traits) < 3:
            traits.append(0)
        spec_lines.append({"spec_id": spec_id, "traits": traits[:3]})
        if len(spec_lines) == 3:
            break

    return spec_lines


def extract_skills_and_legends(soup: BeautifulSoup, is_revenant: bool) -> tuple[dict, list[int]]:
    """
    Extract the 5-slot skill bar and (for Revenant) legend IDs.

    MetaBattle's id="Skill_Bar" anchor is on a <span> inside an <h2>, not on
    a container, so searching from it doesn't work.  Instead we scan all skill
    embeds soup-wide:
      - skip inline icons (data-armory-size or data-armory-inline-text present)
      - collect single-ID embeds before the 5-ID bar as Revenant legends
      - take the first 5-ID embed as the main skill bar
    """
    skills = {"heal": 0, "utilities": [0, 0, 0], "elite": 0}
    legends: list[int] = []

    for embed in soup.find_all(attrs={"data-armory-embed": "skills"}):
        if embed.get("data-armory-inline-text") or embed.get("data-armory-size"):
            continue
        ids = _parse_int_list(embed.get("data-armory-ids", ""))
        if len(ids) == 1 and is_revenant and len(legends) < 2:
            legends.append(ids[0])
        elif len(ids) >= 5 and not skills["heal"]:
            skills = {
                "heal":      ids[0],
                "utilities": ids[1:4],
                "elite":     ids[4],
            }

    return skills, legends


def extract_gear(soup: BeautifulSoup) -> dict:
    """
    Parse eq-box-1 (armor/trinkets), eq-box-2 and eq-box-3 (weapon sets),
    the consumables section (food/utility), and rune assignment.
    Returns a gear dict matching the Snow Crows schema.
    """
    gear: dict = {
        "relic_id":   0,
        "food_id":    0,
        "utility_id": 0,
        "items":      [],
    }

    # ── eq-box-1: armor + trinkets ──────────────────────────────────────────
    acc_idx  = 0
    ring_idx = 0
    box1 = soup.find(id="eq-box-1")
    if box1:
        for slot_div in box1.find_all(class_="equipment-slot"):
            # Find the item embed within this slot
            item_embed = slot_div.find(attrs={"data-armory-embed": "items"})
            if not item_embed:
                continue
            if item_embed.get("data-armory-inline-text"):
                continue

            ids_str = (item_embed.get("data-armory-ids") or "").strip()
            if not ids_str.isdigit() or int(ids_str) <= 0:
                continue
            item_id = int(ids_str)

            stat_id = 0
            stat_key = "data-armory-%d-stat" % item_id
            stat_s = (item_embed.get(stat_key) or "").strip()
            if stat_s.isdigit():
                stat_id = int(stat_s)

            upgrade_id = 0
            upg_s = (item_embed.get("data-armory-%d-upgrades" % item_id) or "").strip()
            upg_ids = _parse_int_list(upg_s)
            if upg_ids:
                upgrade_id = upg_ids[0]

            # Slot label and stat name come from <small>Label<br/>StatName</small>
            small = slot_div.find("small")
            lbl = ""
            stat_name = ""
            if small:
                br = small.find("br")
                if br:
                    lbl = small.get_text(separator="|", strip=True).split("|")[0].strip()
                    stat_name = small.get_text(separator="|", strip=True).split("|")[-1].strip()
                else:
                    lbl = small.get_text(strip=True)
            lbl_low = lbl.lower()

            if lbl_low.startswith("rune") or lbl_low.startswith("infusion"):
                continue
            if lbl_low == "relic":
                gear["relic_id"] = item_id
                continue

            # Map label → slot name
            slot: Optional[str] = None
            if lbl_low in SLOT_MAP:
                slot = SLOT_MAP[lbl_low]
            elif lbl_low == "accessory":
                slot = REPEATING_SLOTS["accessory"][min(acc_idx, 1)]
                acc_idx += 1
            elif lbl_low == "ring":
                slot = REPEATING_SLOTS["ring"][min(ring_idx, 1)]
                ring_idx += 1

            if slot:
                gear["items"].append({
                    "slot":       slot,
                    "item_id":    item_id,
                    "stat_id":    stat_id,
                    "stat_name":  stat_name,
                    "upgrade_id": upgrade_id,
                    "infusions":  [],
                })

    # ── eq-box-2 / eq-box-3: weapon sets ───────────────────────────────────
    weapon_slots = [
        ("eq-box-2", "WeaponA1", "WeaponA2"),
        ("eq-box-3", "WeaponB1", "WeaponB2"),
    ]
    for box_id, slot_mh, slot_oh in weapon_slots:
        box = soup.find(id=box_id)
        if not box:
            continue

        weapons: list[dict] = []
        sigil_ids: list[int] = []

        for slot_div in box.find_all(class_="equipment-slot"):
            item_embed = slot_div.find(attrs={"data-armory-embed": "items"})
            if not item_embed:
                continue
            if item_embed.get("data-armory-inline-text"):
                continue

            ids_str = (item_embed.get("data-armory-ids") or "").strip()
            if not ids_str.isdigit() or int(ids_str) <= 0:
                continue
            item_id = int(ids_str)

            stat_id = 0
            stat_key = "data-armory-%d-stat" % item_id
            stat_s = (item_embed.get(stat_key) or "").strip()
            if stat_s.isdigit():
                stat_id = int(stat_s)

            upgrade_id = 0
            upg_s = (item_embed.get("data-armory-%d-upgrades" % item_id) or "").strip()
            upg_ids = _parse_int_list(upg_s)
            if upg_ids:
                upgrade_id = upg_ids[0]

            small = slot_div.find("small")
            lbl = ""
            if small:
                lbl = small.get_text(separator="|", strip=True).split("|")[0].strip()
            lbl_low = lbl.lower()

            if "sigil" in lbl_low:
                sigil_ids.append(item_id)
            elif len(weapons) < 2:
                weapons.append({
                    "slot":       slot_mh if len(weapons) == 0 else slot_oh,
                    "item_id":    item_id,
                    "stat_id":    stat_id,
                    "upgrade_id": upgrade_id,
                    "infusions":  [],
                })

        # Assign sigils to weapons
        for i, sigil_id in enumerate(sigil_ids[:2]):
            if i < len(weapons):
                weapons[i]["upgrade_id"] = sigil_id
            elif weapons and _is_2h_slot(weapons[0]["slot"]):
                weapons[0]["upgrade2_id"] = sigil_id

        gear["items"].extend(weapons)

    # ── Consumables ─────────────────────────────────────────────────────────
    cons_section = soup.find(id=re.compile(r"consumables?", re.I))
    if cons_section:
        for bold in cons_section.find_all_next("b"):
            label = bold.get_text(strip=True).lower()
            if label not in ("food", "utility"):
                continue
            # Find the first inline-text items embed after this label
            for embed in bold.find_all_next(attrs={"data-armory-embed": "items"}, limit=5):
                if not embed.get("data-armory-inline-text"):
                    continue
                ids_str = (embed.get("data-armory-ids") or "").strip()
                if ids_str.isdigit() and int(ids_str) > 0:
                    iid = int(ids_str)
                    if label == "food"    and not gear["food_id"]:
                        gear["food_id"] = iid
                    elif label == "utility" and not gear["utility_id"]:
                        gear["utility_id"] = iid
                    break

    # ── Global pass: relic + rune live outside eq-boxes on MetaBattle ──────────
    # Collect the ids of equipment-slot divs already accounted for inside eq-boxes
    # so we don't re-process them.
    boxed_slots: set[int] = set()
    for box_id in ("eq-box-1", "eq-box-2", "eq-box-3"):
        box = soup.find(id=box_id)
        if box:
            for sd in box.find_all(class_="equipment-slot"):
                boxed_slots.add(id(sd))

    armor_slots = {"Helm", "Shoulders", "Chest", "Gloves", "Leggings", "Boots"}
    rune_id = 0

    for slot_div in soup.find_all(class_="equipment-slot"):
        if id(slot_div) in boxed_slots:
            continue
        item_embed = slot_div.find(attrs={"data-armory-embed": "items"})
        if not item_embed or item_embed.get("data-armory-inline-text"):
            continue
        ids_str = (item_embed.get("data-armory-ids") or "").strip()
        if not ids_str.isdigit() or int(ids_str) <= 0:
            continue
        item_id = int(ids_str)

        small = slot_div.find("small")
        if not small:
            continue
        lbl = small.get_text(separator="|", strip=True).split("|")[0].strip().lower()

        if lbl == "relic":
            gear["relic_id"] = item_id
        elif lbl.startswith("rune") and not rune_id:
            rune_id = item_id

    if rune_id:
        for item in gear["items"]:
            if item["slot"] in armor_slots:
                item["upgrade_id"] = rune_id

    return gear


def _is_2h_slot(slot: str) -> bool:
    return slot in ("WeaponA1", "WeaponB1")


def extract_tier(soup: BeautifulSoup) -> str:
    """Search infobox / intro area for a tier keyword."""
    # MetaBattle typically renders tier as a badge or cell in the infobox table
    infobox = soup.find(class_=re.compile(r"infobox", re.I))
    search_root = infobox if infobox else soup

    for el in search_root.find_all(True):
        txt = el.get_text(strip=True).lower()
        if txt in TIER_KEYWORDS:
            return txt.title()

    # Broader scan limited to the first 3000 chars of page text
    full_text = soup.get_text(separator=" ", strip=True)
    for tier in TIER_KEYWORDS:
        if re.search(r'\b' + tier + r'\b', full_text[:3000], re.I):
            return tier.title()
    return ""


def extract_gw2skills_url(soup: BeautifulSoup) -> str:
    for a in soup.find_all("a", href=True):
        if "gw2skills.net" in a["href"]:
            return a["href"]
    return ""


def extract_notes(soup: BeautifulSoup) -> str:
    meta = soup.find("meta", attrs={"name": "description"})
    if meta:
        desc = (meta.get("content") or "").strip()
        if len(desc) > 20:
            return desc[:500]
    content = soup.find(id="mw-content-text") or soup
    for p in content.find_all("p"):
        txt = p.get_text(strip=True)
        if len(txt) > 30:
            return txt[:500]
    return ""


# ---------------------------------------------------------------------------
# Profession / spec / build-type from URL (mirrors SpecFromSlug in C++)
# ---------------------------------------------------------------------------
def prof_spec_from_url(url: str) -> tuple[str, str]:
    """Return (profession, elite_spec) by matching spec slug keywords in the URL."""
    url_low = url.lower()
    for slug, (prof, spec) in SPEC_SLUG_MAP.items():
        if re.search(r'(?<![a-z])' + slug + r'(?![a-z])', url_low):
            return prof, spec
    # Fall back to bare profession name in the URL path
    for p in PROFESSIONS:
        if p.lower() in url_low:
            return p, ""
    return "", ""


def prof_from_title(title: str) -> str:
    """'Guardian - Power Dragonhunter' → 'Guardian'"""
    m = re.match(r"([A-Za-z]+)\s*[-–—]", title)
    if m:
        cand = m.group(1).title()
        if cand in PROFESSIONS:
            return cand
    for p in PROFESSIONS:
        if p.lower() in title.lower():
            return p
    return ""


def build_type_from_name(name: str) -> str:
    name_low = name.lower()
    for kw, bt in BUILD_TYPE_KEYWORDS.items():
        if kw in name_low:
            return bt
    return "Power"


def elite_spec_from_spec_lines(spec_lines: list[dict]) -> str:
    for line in spec_lines:
        if line["spec_id"] in ELITE_SPEC_IDS:
            return SPEC_ID_NAME.get(line["spec_id"], "")
    return ""


# ---------------------------------------------------------------------------
# MediaWiki API category discovery
# ---------------------------------------------------------------------------
def discover_category(game_mode: str, *, force: bool = False, delay: float = 1.0) -> list[dict]:
    """
    Return a list of build metadata dicts for the given game mode.
    Handles MediaWiki API pagination via cmcontinue for large categories.
    """
    candidates = GAME_MODE_CATEGORIES.get(game_mode, [game_mode + "_builds"])

    for cat_name in candidates:
        cat = cat_name.replace(" ", "_")
        all_members: list[dict] = []
        continue_token: Optional[str] = None
        page = 0

        while True:
            params = {
                "action":  "query",
                "list":    "categorymembers",
                "cmtitle": "Category:" + cat,
                "cmlimit": "500",
                "format":  "json",
            }
            if continue_token:
                params["cmcontinue"] = continue_token

            cache_key = "cat_%s_%s_p%d" % (game_mode, cat, page)
            data = api_get(params, cache_key, force=force, delay=delay)
            if not data:
                break

            members = data.get("query", {}).get("categorymembers", [])
            all_members.extend(members)

            # Check for continuation
            cont = data.get("continue", {}).get("cmcontinue")
            if cont:
                continue_token = cont
                page += 1
            else:
                break

        builds = [
            {
                "page_title": m["title"],
                "game_mode":  game_mode,
                "url": "%s/%s" % (BASE_URL, m["title"].replace(" ", "_")),
            }
            for m in all_members
            if m.get("title", "").startswith("Build:")
        ]

        if builds:
            pages = page + 1
            print("  Category:%-32s → %d build(s)%s" % (
                cat, len(builds),
                " (%d API pages)" % pages if pages > 1 else ""))
            return builds

    print("  WARN: no builds found for %r (tried: %s)" % (game_mode, ", ".join(candidates)))
    return []


# ---------------------------------------------------------------------------
# Parse a single build page
# ---------------------------------------------------------------------------
def parse_build_page(meta: dict, *, delay: float, force: bool) -> Optional[dict]:
    url        = meta["url"]
    game_mode  = meta["game_mode"]
    page_title = meta.get("page_title", "")

    html = fetch_html(url, force=force, delay=delay)
    if not html:
        return None

    soup = BeautifulSoup(html, "lxml")

    # --- Identifiers ---
    build_name = extract_build_name(soup) or re.sub(r"^Build:\s*", "", page_title).strip()
    if not build_name:
        return None

    prof, elite_spec = prof_spec_from_url(url)
    if not prof:
        prof = prof_from_title(build_name)

    # --- Mechanics ---
    is_rev   = prof.lower() == "revenant"
    spec_lines         = extract_traits(soup)
    skills, legends    = extract_skills_and_legends(soup, is_revenant=is_rev)
    gear               = extract_gear(soup)
    chat_code          = extract_chat_code(soup)
    tier               = extract_tier(soup)
    gw2skills_url      = extract_gw2skills_url(soup)
    notes              = extract_notes(soup)

    # Elite spec from parsed trait lines overrides URL-derived when available
    if spec_lines:
        parsed_spec = elite_spec_from_spec_lines(spec_lines)
        if parsed_spec:
            elite_spec = parsed_spec

    # --- Build trait dict ---
    traits: dict = {}
    for i, line in enumerate(spec_lines[:3]):
        t = line.get("traits", [0, 0, 0])
        while len(t) < 3:
            t.append(0)
        traits["line%d" % (i + 1)] = {"spec_id": line["spec_id"], "traits": t[:3]}
    for key in ("line1", "line2", "line3"):
        if key not in traits:
            traits[key] = {}

    build_id = "mb-%s-%s" % (
        prof.lower(),
        re.sub(r"[^a-z0-9]+", "-", build_name.lower()).strip("-"),
    )

    while len(legends) < 2:
        legends.append(0)

    return {
        "id":            build_id,
        "name":          build_name,
        "profession":    prof,
        "elite_spec":    elite_spec,
        "build_type":    build_type_from_name(build_name),
        "game_mode":     game_mode,
        "tier":          tier,
        "patch_version": "",
        "benchmark_dps": 0.0,
        "notes":         notes,
        "source":        "metabattle",
        "source_url":    url,
        "gw2skills_url": gw2skills_url,
        "chat_code":     chat_code,
        "is_legacy":     False,
        "accessibility": False,
        "traits":        traits,
        "skills":        skills,
        "legends":       legends[:2],
        "pets":          [0, 0],
        "gear":          gear,
        "rotation":      {"opener": [], "loop": []},
    }


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
def _print_result(r: dict) -> None:
    spec   = r["elite_spec"] or "?"
    mode   = r["game_mode"]
    tier   = r["tier"] or "?"
    skills = "yes" if r["skills"]["heal"] or any(r["skills"]["utilities"]) else "no"
    specs  = "yes" if any(r["traits"].get("line%d" % i) for i in (1, 2, 3)) else "no"
    gear   = len(r["gear"]["items"])
    gw2s   = "yes" if r["gw2skills_url"] else "no"
    chat   = "yes" if r["chat_code"] else "no"
    print("OK  [%-12s] %-10s %-7s sk=%-3s sp=%-3s gear=%-2d chat=%-3s gw2s=%-3s  %r" % (
        spec, mode, tier, skills, specs, gear, chat, gw2s, r["name"]))


def write_outputs(all_builds: list[dict], out_dir: Path, mode_filter: str = "") -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    by_prof: dict[str, list] = {}
    for b in all_builds:
        by_prof.setdefault(b["profession"].lower(), []).append(b)

    print("\nWriting output files:")
    for prof_slug in sorted(by_prof.keys()):
        prof_path = out_dir / ("mb_builds_%s.json" % prof_slug)
        existing: dict[str, dict] = {}
        if prof_path.exists():
            try:
                for b in json.loads(prof_path.read_text(encoding="utf-8")):
                    existing[b["id"]] = b
            except Exception:
                pass
        for b in by_prof[prof_slug]:
            existing[b["id"]] = b
        merged = list(existing.values())
        prof_path.write_text(json.dumps(merged, indent=2, ensure_ascii=False), encoding="utf-8")
        print("  %3d builds  →  %s" % (len(merged), prof_path.name))

    suffix = ("_%s" % mode_filter.lower()) if mode_filter else ""
    combined = out_dir / ("mb_builds%s_full.json" % suffix)
    combined.write_text(json.dumps(all_builds, indent=2, ensure_ascii=False), encoding="utf-8")
    print("  %3d builds  →  %s  (combined)" % (len(all_builds), combined.name))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    ap = argparse.ArgumentParser(
        description="MetaBattle build scraper for BuildCoach",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
game modes:  Raid  Fractal  WvW  PvP  OpenWorld  Strike

examples:
  python3 scrape_metabattle.py                          # all game modes
  python3 scrape_metabattle.py --mode Raid              # one mode
  python3 scrape_metabattle.py --mode WvW --limit 5    # test run
  python3 scrape_metabattle.py --url "https://metabattle.com/wiki/Build:Guardian_-_Power_Dragonhunter"
  python3 scrape_metabattle.py --force                  # ignore cache

Do not distribute the scraped data in the addon before getting MetaBattle's
written permission (their content is CC BY-SA licensed).
""")
    ap.add_argument("--mode",  default="",   help="Single game mode to scrape")
    ap.add_argument("--delay", type=float, default=1.5, help="Seconds between requests (default 1.5)")
    ap.add_argument("--force", action="store_true",     help="Clear cache and re-fetch everything")
    ap.add_argument("--limit", type=int,   default=0,   help="Max builds per game mode, 0=all")
    ap.add_argument("--url",   default="",              help="Scrape a single MetaBattle build URL")
    ap.add_argument("--out",   default=str(DEFAULT_OUT_DIR), help="Output directory")
    args = ap.parse_args()

    out_dir = Path(args.out).resolve()

    if args.force and CACHE_DIR.exists():
        shutil.rmtree(CACHE_DIR)
    CACHE_DIR.mkdir(exist_ok=True)

    print("=== MetaBattle Scraper ===")
    print("Output : %s" % out_dir)
    print("Delay  : %.1fs" % args.delay)
    print()

    # ── Single URL mode ───────────────────────────────────────────────────────
    if args.url:
        url = args.url.rstrip("/")
        m = re.search(r"/wiki/(Build:[^/?#]+)", url)
        if not m:
            print("ERROR: URL must look like https://metabattle.com/wiki/Build:Profession_-_Name")
            sys.exit(1)
        page_title = m.group(1).replace("_", " ")
        meta = {"page_title": page_title, "game_mode": args.mode or "Unknown", "url": url}

        print("Scraping: %s" % page_title)
        result = parse_build_page(meta, delay=args.delay, force=args.force)
        if not result:
            print("FAILED")
            sys.exit(1)
        _print_result(result)
        write_outputs([result], out_dir)
        return

    # ── Game mode(s) mode ─────────────────────────────────────────────────────
    modes = [args.mode] if args.mode else list(GAME_MODE_CATEGORIES.keys())
    print("Game modes: %s" % ", ".join(modes))
    print()

    all_builds: list[dict] = []

    for mode in modes:
        print("── %s ──" % mode)
        metas = discover_category(mode, force=args.force, delay=args.delay)
        if not metas:
            print()
            continue
        if args.limit > 0:
            metas = metas[:args.limit]

        print("  Scraping %d build(s)..." % len(metas))
        for i, meta in enumerate(metas, 1):
            label = meta["page_title"]
            print("  [%2d/%d] %-55s  " % (i, len(metas), label), end="", flush=True)
            result = parse_build_page(meta, delay=args.delay, force=args.force)
            if result:
                all_builds.append(result)
                _print_result(result)
            else:
                print("SKIP")
        print()

    if not all_builds:
        print("No builds scraped.")
        sys.exit(0)

    write_outputs(all_builds, out_dir, mode_filter=args.mode)
    print()
    print("Done.  %d build(s) total." % len(all_builds))
    print()
    print("Notes:")
    print("  - Builds with empty specs/skills may need inspect if MB restructured the page.")
    print("  - chat_code is extracted directly from MetaBattle's build-template-code div.")
    print("  - game_mode and tier are MetaBattle-specific — addon needs new filter UI.")
    print("  - Do not distribute without MetaBattle's written permission (CC BY-SA).")


if __name__ == "__main__":
    main()
