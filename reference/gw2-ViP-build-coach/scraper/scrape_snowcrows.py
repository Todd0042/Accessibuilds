#!/usr/bin/env python3
"""
Snow Crows build scraper — GW2 Armory embed approach.
Produces sc_builds_full.json in the format expected by BuildCoach's ParseBuildJSON().

Usage:
    python3 scrape_snowcrows.py [--out path] [--delay 1.5]
    python3 scrape_snowcrows.py --prof guardian --limit 3   # quick test
    python3 scrape_snowcrows.py --force                     # clear cache and re-fetch

Requirements:
    pip install requests beautifulsoup4 lxml playwright
    python3 -m playwright install chromium

SC uses Laravel/Livewire v3 (not Next.js).  Build data is in the server-rendered
HTML as GW2 Armory widget attributes:

    data-armory-embed="specializations"  data-armory-ids="{spec_id}"
        data-armory-{spec_id}-traits="{t1},{t2},{t3}"

    data-armory-embed="skills"  data-armory-ids="{heal},{u1},{u2},{u3},{elite}"

    data-armory-embed="items"  data-armory-ids="{item_id}"
        data-armory-{item_id}-stat="{stat_id}"
        data-armory-{item_id}-upgrades="{upg_id}[,{upg_id2}]"

Gear slot names come from a sibling <td><span> in each table row.

Listing pages are JavaScript-rendered (Livewire lazy loads some builds).
We use Playwright to get the fully-rendered listing, then fetch individual
build pages with requests (they are server-rendered).
"""

import argparse
import base64
import json
import re
import struct
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

try:
    from playwright.sync_api import sync_playwright
    HAS_PLAYWRIGHT = True
except ImportError:
    HAS_PLAYWRIGHT = False
    print("WARN: playwright not available — listing pages may miss JS-rendered builds.")
    print("      Run: pip install playwright && python3 -m playwright install chromium")

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
BASE_URL    = "https://snowcrows.com"
GW2_API     = "https://api.guildwars2.com"
SCRIPT_DIR  = Path(__file__).parent
DEFAULT_OUT = SCRIPT_DIR / ".." / "sc_builds_full.json"
CACHE_DIR   = SCRIPT_DIR / ".scraper_cache"

# Use dated schema for skills_by_palette support
GW2_API_SCHEMA = "2019-12-19T00:00:00.000Z"

HEADERS = {
    "User-Agent":      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                       "AppleWebKit/537.36 (KHTML, like Gecko) "
                       "Chrome/124.0.0.0 Safari/537.36",
    "Accept":          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "Accept-Language": "en-US,en;q=0.9",
}

PROFESSION_SLUGS = [
    "guardian", "warrior", "engineer", "ranger", "thief",
    "elementalist", "mesmer", "necromancer", "revenant",
]

# Profession slug → GW2 API profession ID and chat-link byte
PROFESSION_META: dict[str, dict] = {
    "guardian":     {"api_id": "Guardian",     "chat_byte": 1},
    "warrior":      {"api_id": "Warrior",      "chat_byte": 2},
    "engineer":     {"api_id": "Engineer",     "chat_byte": 3},
    "ranger":       {"api_id": "Ranger",       "chat_byte": 4},
    "thief":        {"api_id": "Thief",        "chat_byte": 5},
    "elementalist": {"api_id": "Elementalist", "chat_byte": 6},
    "mesmer":       {"api_id": "Mesmer",       "chat_byte": 7},
    "necromancer":  {"api_id": "Necromancer",  "chat_byte": 8},
    "revenant":     {"api_id": "Revenant",     "chat_byte": 9},
}

# GW2 spec ID → display name (matches C++ SpecFromString names)
SPEC_ID_NAME: dict[int, str] = {
    # Guardian
    16: "Radiance",      42: "Virtues",     46: "Honor",
    27: "Dragonhunter",  62: "Firebrand",   65: "Willbender",
    # Warrior
    4:  "Strength",      51: "Arms",        22: "Defense",
    18: "Berserker",     61: "Spellbreaker", 68: "Bladesworn",
    # Engineer
    6:  "Firearms",      47: "Inventions",  29: "Explosives",
    43: "Scrapper",      57: "Holosmith",   70: "Mechanist",
    # Ranger
    8:  "Skirmishing",   25: "Marksmanship", 30: "Nature Magic",
    5:  "Druid",         55: "Soulbeast",   72: "Untamed",
    # Thief
    28: "Shadow Arts",   20: "Deadly Arts", 35: "Critical Strikes",
    7:  "Daredevil",     58: "Deadeye",     71: "Specter",
    # Elementalist
    31: "Fire",          26: "Water",       37: "Arcane",
    48: "Tempest",       56: "Weaver",      67: "Catalyst",
    # Mesmer
    10: "Domination",    23: "Illusions",   24: "Chaos",
    40: "Chronomancer",  59: "Mirage",      66: "Virtuoso",
    # Necromancer
    50: "Soul Reaping",  2:  "Spite",       39: "Death Magic",
    34: "Reaper",        60: "Scourge",     64: "Harbinger",
    # Revenant
    3:  "Salvation",     14: "Retribution", 9:  "Corruption",
    52: "Herald",        63: "Renegade",    69: "Vindicator",
    # Expansion (2025+)
    81: "Luminary",    # Guardian
    74: "Paragon",     # Warrior
    75: "Amalgam",     # Engineer
    78: "Galeshot",    # Ranger
    77: "Antiquary",   # Thief
    80: "Evoker",      # Elementalist
    73: "Troubadour",  # Mesmer
    76: "Ritualist",   # Necromancer
    79: "Conduit",     # Revenant
}

ELITE_SPEC_IDS: set[int] = {
    27, 62, 65,        # Guardian
    18, 61, 68,        # Warrior
    43, 57, 70,        # Engineer
    5,  55, 72,        # Ranger
    7,  58, 71,        # Thief
    48, 56, 67,        # Elementalist
    40, 59, 66,        # Mesmer
    34, 60, 64,        # Necromancer
    52, 63, 69,        # Revenant
    81, 74, 75, 78, 77, 80, 73, 76, 79,  # Expansion (2025+)
}

# Slug keyword → canonical build_type string
BUILD_TYPE_KEYWORDS: dict[str, str] = {
    "power": "Power",     "condi": "Condi",     "condition": "Condi",
    "support": "Support", "heal": "Heal",        "healer": "Heal",
    "quickness": "Quickness",                     "alacrity": "Alacrity",
    "dps": "Power",       "boon": "Support",     "celestial": "Celestial",
    "inferno": "Condi",
}

# SC slot type label → canonical GW2 slot name
_SLOT_MAP: dict[str, str] = {
    "Helm":       "Helm",      "Head":       "Helm",
    "Shoulders":  "Shoulders", "Shoulder":   "Shoulders",
    "Coat":       "Chest",     "Chest":      "Chest",     "Torso": "Chest",
    "Gloves":     "Gloves",    "Hands":      "Gloves",
    "Leggings":   "Leggings",  "Legs":       "Leggings",  "Pants": "Leggings",
    "Boots":      "Boots",     "Feet":       "Boots",
    "Backpiece":  "BackItem",  "Back":       "BackItem",  "Backpack": "BackItem",
    "Amulet":     "Amulet",
}
_REPEATING_SLOTS: dict[str, list[str]] = {
    "Accessory": ["Accessory1", "Accessory2"],
    "Ring":      ["Ring1",      "Ring2"],
    "Main Hand": ["WeaponA1",   "WeaponB1"],
    "Off Hand":  ["WeaponA2",   "WeaponB2"],
}

# Revenant legend swap-skill-ID → chat link legend code (1-indexed)
LEGEND_SWAP_TO_CODE: dict[int, int] = {
    28134: 1,  # Glint / Dragon
    28085: 2,  # Shiro / Assassin
    28419: 3,  # Jalis / Dwarf
    28494: 4,  # Mallyx / Demon
    41858: 5,  # Kalla / Renegade
    28195: 6,  # Ventari / Centaur
    62749: 7,  # Alliance / Vindicator
    76610: 8,  # Conduit
}

# Words to strip when building the variant suffix from a slug
_SLUG_STRIP_WORDS = frozenset([
    "power", "condi", "condition", "heal", "healer", "support",
    "quickness", "alacrity", "celestial", "inferno", "boon", "hand", "kite",
    "beginner",
    # spec names (lowercase)
    "guardian", "warrior", "engineer", "ranger", "thief",
    "elementalist", "mesmer", "necromancer", "revenant",
    "dragonhunter", "firebrand", "willbender", "luminary",
    "berserker", "spellbreaker", "bladesworn", "paragon",
    "scrapper", "holosmith", "mechanist", "amalgam",
    "druid", "soulbeast", "untamed", "galeshot",
    "daredevil", "deadeye", "specter", "antiquary",
    "tempest", "weaver", "catalyst", "evoker",
    "chronomancer", "mirage", "virtuoso", "troubadour",
    "reaper", "scourge", "harbinger", "ritualist",
    "herald", "renegade", "vindicator", "conduit",
])

# ---------------------------------------------------------------------------
# HTTP session with disk cache
# ---------------------------------------------------------------------------
CACHE_DIR.mkdir(exist_ok=True)
_session = requests.Session()
_session.headers.update(HEADERS)


def fetch(url: str, delay: float = 1.0, force: bool = False) -> Optional[str]:
    safe = re.sub(r"[^a-zA-Z0-9_-]", "_", url.replace(BASE_URL, ""))
    cp = CACHE_DIR / (safe[:200] + ".html")
    if not force and cp.exists():
        return cp.read_text(encoding="utf-8")
    time.sleep(delay)
    try:
        r = _session.get(url, timeout=30)
        r.raise_for_status()
        cp.write_text(r.text, encoding="utf-8")
        return r.text
    except requests.RequestException as exc:
        print("    WARN fetch: %s" % exc)
        return None


# ---------------------------------------------------------------------------
# GW2 API caches for chat link generation
# ---------------------------------------------------------------------------

# Per-profession: skill_id → palette_id
_skill_to_palette: dict[str, dict[int, int]] = {}

# spec_id → list of 9 major_trait IDs [adept_top, adept_mid, adept_bot, ...]
_spec_major_traits: dict[int, list[int]] = {}


def load_api_caches(force: bool = False) -> None:
    """Pre-fetch /v2/professions and /v2/specializations for chat link generation."""
    api_cache = CACHE_DIR / "_gw2api_professions.json"
    spec_cache = CACHE_DIR / "_gw2api_specializations.json"

    # Professions
    if not force and api_cache.exists():
        profs_data = json.loads(api_cache.read_text())
    else:
        print("  Fetching /v2/professions (skills_by_palette)...")
        ids_param = ",".join(m["api_id"] for m in PROFESSION_META.values())
        resp = requests.get(
            f"{GW2_API}/v2/professions?ids={ids_param}&v={GW2_API_SCHEMA}",
            timeout=30
        )
        resp.raise_for_status()
        profs_data = resp.json()
        api_cache.write_text(json.dumps(profs_data))

    for prof in profs_data:
        slug = prof["id"].lower()
        mapping: dict[int, int] = {}
        for pair in prof.get("skills_by_palette", []):
            if isinstance(pair, (list, tuple)) and len(pair) == 2:
                palette_id, skill_id = int(pair[0]), int(pair[1])
                if skill_id and palette_id:
                    mapping[skill_id] = palette_id
        _skill_to_palette[slug] = mapping

    # Specializations — fetch ALL of them so any core spec used in a build works
    if not force and spec_cache.exists():
        specs_data = json.loads(spec_cache.read_text())
    else:
        print("  Fetching /v2/specializations (all, for trait positions)...")
        # Step 1: get the full list of IDs
        all_ids_resp = requests.get(f"{GW2_API}/v2/specializations", timeout=30)
        all_ids_resp.raise_for_status()
        all_spec_ids = all_ids_resp.json()
        # Step 2: fetch all specs in one batched request
        ids_param = ",".join(str(i) for i in all_spec_ids)
        resp = requests.get(
            f"{GW2_API}/v2/specializations?ids={ids_param}",
            timeout=30
        )
        resp.raise_for_status()
        specs_data = resp.json()
        spec_cache.write_text(json.dumps(specs_data))

    for spec in specs_data:
        sid = spec.get("id", 0)
        major = spec.get("major_traits", [])
        if sid and len(major) == 9:
            _spec_major_traits[sid] = major


def _encode_trait_byte(spec_id: int, trait_ids: list[int]) -> int:
    """
    Pack 3 trait choices into one byte.
    Bits 0-1: adept choice (0=none, 1=top, 2=mid, 3=bot)
    Bits 2-3: master choice
    Bits 4-5: grandmaster choice
    """
    major = _spec_major_traits.get(spec_id, [])
    if len(major) < 9:
        return 0

    choices = [0, 0, 0]  # [adept, master, grandmaster]
    for tid in trait_ids:
        if not tid:
            continue
        try:
            idx = major.index(tid)
        except ValueError:
            continue
        tier = idx // 3
        pos  = (idx % 3) + 1  # 1=top, 2=mid, 3=bot
        if tier < 3:
            choices[tier] = pos

    return choices[0] | (choices[1] << 2) | (choices[2] << 4)


def generate_chat_code(profession_slug: str,
                       spec_lines: list[dict],
                       skill_bar: Optional[dict],
                       legends: Optional[list[int]] = None,
                       pets: Optional[list[int]] = None) -> str:
    """
    Generate a GW2 build template chat link [&BQUA...].
    44-byte layout:
      Byte  0:    0x0D  (build template type)
      Byte  1:    profession code 1-9
      Bytes 2-7:  3 × (spec_id byte + trait-choice byte)
      Bytes 8-27: 10 × uint16 LE palette IDs, terrestrial/aquatic interleaved:
                    [terr_heal, aqua_heal,
                     terr_u1,   aqua_u1,
                     terr_u2,   aqua_u2,
                     terr_u3,   aqua_u3,
                     terr_elite,aqua_elite]
      Bytes 28-43: 16 profession-specific bytes
                    Revenant: [terr_active, terr_inactive, aqua_active, aqua_inactive, zeros...]
                    Ranger:   [terr_pet1, terr_pet2, aqua_pet1, aqua_pet2, zeros...]
                    Others:   all zeros
    """
    meta = PROFESSION_META.get(profession_slug)
    if not meta:
        return ""

    prof_byte   = meta["chat_byte"]
    palette_map = _skill_to_palette.get(meta["api_id"].lower(), {})

    data = bytearray()
    data.append(0x0D)       # type: build template
    data.append(prof_byte)  # profession

    # 3 specialization lines (6 bytes)
    lines = (spec_lines + [{}, {}, {}])[:3]
    for line in lines:
        spec_id = line.get("spec_id", 0) or 0
        traits  = line.get("traits",  []) or []
        tb = _encode_trait_byte(spec_id, traits) if spec_id else 0
        data.append(spec_id & 0xFF)
        data.append(tb)

    # 10 skill palette IDs interleaved terrestrial/aquatic (20 bytes)
    heal  = 0
    utils = [0, 0, 0]
    elite = 0
    if skill_bar:
        heal  = skill_bar.get("heal",      0) or 0
        utils = list(skill_bar.get("utilities", [0, 0, 0]) or [0, 0, 0])
        while len(utils) < 3:
            utils.append(0)
        elite = skill_bar.get("elite", 0) or 0

    skill_slots = [
        heal,     0,         # terrestrial heal,   aquatic heal
        utils[0], 0,         # terrestrial util 1, aquatic util 1
        utils[1], 0,         # terrestrial util 2, aquatic util 2
        utils[2], 0,         # terrestrial util 3, aquatic util 3
        elite,    0,         # terrestrial elite,  aquatic elite
    ]
    for sk_id in skill_slots:
        pid = palette_map.get(sk_id, 0) if sk_id else 0
        data += struct.pack("<H", pid)

    # 16 profession-specific bytes (bytes 28-43)
    prof_data = bytearray(16)
    if profession_slug == "revenant" and legends:
        lgs = list(legends)
        while len(lgs) < 4:
            lgs.append(0)
        prof_data[0] = LEGEND_SWAP_TO_CODE.get(lgs[0], 0)  # terr active
        prof_data[1] = LEGEND_SWAP_TO_CODE.get(lgs[1], 0)  # terr inactive
        # aqua slots (2, 3) stay 0 — SC doesn't specify aquatic legends
    elif profession_slug == "ranger" and pets:
        for i, pet_id in enumerate(pets[:4]):
            prof_data[i] = pet_id & 0xFF
    data += prof_data

    encoded = base64.b64encode(bytes(data)).decode("ascii")
    return "[&%s]" % encoded


# ---------------------------------------------------------------------------
# Build-list discovery (Playwright for JS-rendered listing pages)
# ---------------------------------------------------------------------------
def discover_build_urls(prof_slug: str, delay: float, force: bool = False) -> list[dict]:
    slug_lower = prof_slug.lower()
    url = "%s/builds/raids/%s" % (BASE_URL, slug_lower)

    if HAS_PLAYWRIGHT:
        html = _fetch_with_playwright(url, force=force)
    else:
        html = fetch(url, delay=delay, force=force)

    if not html:
        return []

    # SC always uses lowercase slugs in href paths regardless of URL case
    pattern = re.compile(r"/builds/raids/%s/([a-z0-9-]+)" % re.escape(slug_lower))
    seen    = set()
    builds  = []
    for m in pattern.finditer(html):
        slug = m.group(1)
        if slug in seen:
            continue
        seen.add(slug)
        builds.append({"url": BASE_URL + "/builds/raids/%s/%s" % (slug_lower, slug),
                       "profession": slug_lower, "slug": slug})
    return builds


def _fetch_with_playwright(url: str, force: bool = False) -> Optional[str]:
    """Fetch a page with full JS execution. Caches result to disk."""
    safe = re.sub(r"[^a-zA-Z0-9_-]", "_", url.replace(BASE_URL, ""))
    cp = CACHE_DIR / ("_pw_" + safe[:200] + ".html")
    if not force and cp.exists():
        return cp.read_text(encoding="utf-8")

    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.goto(url, wait_until="domcontentloaded", timeout=30000)
            # Wait for Livewire lazy-loaded content
            try:
                page.wait_for_load_state("networkidle", timeout=8000)
            except Exception:
                pass  # networkidle may timeout on slow pages; use what we have
            html = page.content()
            browser.close()

        # Refuse to cache Cloudflare challenge pages — they would poison the cache
        if "Just a moment" in html and "challenges.cloudflare.com" in html:
            print("    WARN: Cloudflare challenge received — not caching. "
                  "Try again in a moment or increase --delay.")
            return None

        cp.write_text(html, encoding="utf-8")
        return html
    except Exception as exc:
        print("    WARN playwright: %s" % exc)
        return None


# ---------------------------------------------------------------------------
# Armory embed + gear parsing
# ---------------------------------------------------------------------------
def _parse_int_list(s: str) -> list[int]:
    result = []
    for tok in (s or "").split(","):
        tok = tok.strip()
        if tok and tok.isdigit():
            result.append(int(tok))
    return result


def _slot_type_from_tr(el) -> str:
    """Find the SC slot type label from the sibling <td> in the item's table row."""
    parent_td = el.find_parent("td")
    if parent_td is None:
        return ""
    tr = parent_td.find_parent("tr")
    if tr is None:
        return ""
    for td in tr.find_all("td"):
        if td is parent_td:
            continue
        for span in td.find_all("span"):
            txt = span.get_text(strip=True)
            if txt and not txt.startswith("x") and not txt[0].isdigit():
                return txt
    return ""


def parse_armory_embeds(soup: BeautifulSoup) -> dict:
    """
    Extract spec lines, skill bar, and gear items from GW2 Armory embed attrs.
    Returns:
        spec_lines: [{spec_id, traits}]
        skill_bar:  {heal, utilities, elite} or None
        gear:       {relic_id, food_id, utility_id, items[...]}
    """
    spec_lines: list[dict] = []
    skill_bar:  Optional[dict] = None
    legends:    list[int]     = []
    gear = {"relic_id": 0, "food_id": 0, "utility_id": 0, "items": []}

    # Pre-scan main-hand weapon rows to detect 2H weapons.  A 2H weapon has two
    # upgrade IDs in its single upgrades attribute and has no off-hand page row.
    # We build a per-call off-hand slot sequence that skips the slot for any
    # weapon set whose main-hand is 2H.
    _mh_2h: list[bool] = []
    for _el in soup.find_all(attrs={"data-armory-embed": "items"}):
        if _el.get("data-armory-size") or _el.get("data-armory-inline-text"):
            continue
        _ids = (_el.get("data-armory-ids") or "").strip()
        if not _ids.isdigit():
            continue
        if _slot_type_from_tr(_el) != "Main Hand":
            continue
        _iid = int(_ids)
        _upg = _parse_int_list(_el.get("data-armory-%d-upgrades" % _iid) or "")
        _mh_2h.append(len(_upg) >= 2)

    _off_hand_seq = [s for i, s in enumerate(["WeaponA2", "WeaponB2"])
                     if i >= len(_mh_2h) or not _mh_2h[i]]
    repeating_slots = dict(_REPEATING_SLOTS)
    repeating_slots["Off Hand"] = _off_hand_seq

    repeat_counters: dict[str, int] = {k: 0 for k in repeating_slots}

    for el in soup.find_all(attrs={"data-armory-embed": True}):
        etype   = el.get("data-armory-embed", "")
        ids_str = (el.get("data-armory-ids") or "").strip()
        if not ids_str:
            continue

        # ── Specializations ─────────────────────────────────────────────────
        if etype == "specializations":
            if not ids_str.isdigit():
                continue
            spec_id    = int(ids_str)
            traits_str = el.get("data-armory-%d-traits" % spec_id, "") or ""
            traits     = _parse_int_list(traits_str)
            while len(traits) < 3:
                traits.append(0)
            spec_lines.append({"spec_id": spec_id, "traits": traits[:3]})

        # ── Skills ──────────────────────────────────────────────────────────
        elif etype == "skills":
            if el.get("data-armory-size"):
                continue  # single-icon tooltip
            ids = _parse_int_list(ids_str)
            if len(ids) == 5 and skill_bar is None:
                skill_bar = {
                    "heal":      ids[0],
                    "utilities": ids[1:4],
                    "elite":     ids[4],
                }
            elif len(ids) == 2 and not legends:
                legends = ids

        # ── Items ────────────────────────────────────────────────────────────
        elif etype == "items":
            if el.get("data-armory-size") or el.get("data-armory-inline-text"):
                continue
            if not ids_str.isdigit():
                continue
            item_id = int(ids_str)

            stat_id = 0
            stat_s  = (el.get("data-armory-%d-stat" % item_id) or "").strip()
            if stat_s.isdigit():
                stat_id = int(stat_s)

            upgrade_id  = 0
            upgrade2_id = 0
            upg_s = (el.get("data-armory-%d-upgrades" % item_id) or "").strip()
            upg_ids = _parse_int_list(upg_s)
            if upg_ids:
                upgrade_id = upg_ids[0]

            slot_type = _slot_type_from_tr(el)

            if slot_type == "Relic":
                gear["relic_id"] = item_id
                continue

            # Check for food/utility via <p> label in row
            parent_td = el.find_parent("td")
            if parent_td:
                tr = parent_td.find_parent("tr")
                if tr:
                    for td in tr.find_all("td"):
                        p = td.find("p")
                        if p:
                            lbl = p.get_text(strip=True).lower()
                            if "food" in lbl or "nourishment" in lbl:
                                if gear["food_id"] == 0:
                                    gear["food_id"] = item_id
                                slot_type = "__skip__"
                                break
                            if "utility" in lbl or "enhancement" in lbl:
                                if gear["utility_id"] == 0:
                                    gear["utility_id"] = item_id
                                slot_type = "__skip__"
                                break

            if slot_type == "__skip__":
                continue

            if slot_type in repeating_slots:
                options = repeating_slots[slot_type]
                cnt     = repeat_counters[slot_type]
                slot    = options[cnt] if cnt < len(options) else options[-1]
                repeat_counters[slot_type] = cnt + 1
            else:
                slot = _SLOT_MAP.get(slot_type, slot_type or "Unknown")

            if slot in ("Unknown", "") or slot.startswith("__"):
                continue

            # For 2H main-hand weapons (WeaponA1, WeaponB1) SC puts both sigil IDs
            # in the single upgrades attribute — second ID is the second sigil, not
            # an infusion.  Store it as upgrade2_id so the C++ side can display it.
            _MAINHAND_SLOTS = {"WeaponA1", "WeaponB1"}
            if slot in _MAINHAND_SLOTS and len(upg_ids) >= 2:
                upgrade2_id = upg_ids[1]
                infusions   = upg_ids[2:] if len(upg_ids) > 2 else []
            else:
                infusions   = upg_ids[1:] if len(upg_ids) > 1 else []

            item_dict: dict = {
                "slot":       slot,
                "item_id":    item_id,
                "stat_id":    stat_id,
                "upgrade_id": upgrade_id,
                "infusions":  infusions,
            }
            if upgrade2_id:
                item_dict["upgrade2_id"] = upgrade2_id
            gear["items"].append(item_dict)

    return {"spec_lines": spec_lines, "skill_bar": skill_bar,
            "legends": legends, "gear": gear}


# ---------------------------------------------------------------------------
# Page-level text helpers
# ---------------------------------------------------------------------------
def extract_build_name(soup: BeautifulSoup, slug: str) -> str:
    title_el = soup.find("title")
    if title_el and title_el.string:
        t = title_el.string.strip()
        t = re.sub(r"\s*[|–—-]\s*Snow\s*Crows.*$", "", t, flags=re.I)
        t = re.sub(r"\s*[|–—-]\s*Guild\s*Wars\s*2.*$", "", t, flags=re.I)
        t = re.sub(r"\s*[|–—-]\s*GW2.*$", "", t, flags=re.I)
        t = re.sub(r"\s+Build$", "", t, flags=re.I).strip()
        if t and len(t) < 120:
            base = t
        else:
            base = ""
    else:
        base = ""

    if not base:
        h1 = soup.find("h1")
        if h1:
            base = h1.get_text(strip=True)

    if not base:
        base = " ".join(w.title() for w in slug.split("-"))
        return base

    # Append weapon-variant suffix derived from slug words not in the base name
    variant = _variant_suffix_from_slug(slug, base)
    if variant:
        return "%s (%s)" % (base, variant)
    return base


def _variant_suffix_from_slug(slug: str, base_name: str) -> str:
    """
    Return a weapon/variant suffix by removing base-name words and known
    build-type/spec words from the slug.  E.g.:
      slug = "power-dragonhunter-radiance-longbow-greatsword"
      base = "Power Dragonhunter"
      → "Radiance/Longbow/Greatsword"
    """
    # Words already in the base name (lowercase)
    base_words = frozenset(base_name.lower().split())
    parts = slug.split("-")
    remainder = []
    for word in parts:
        if word in _SLUG_STRIP_WORDS:
            continue
        if word in base_words:
            continue
        remainder.append(word.title())
    if not remainder:
        return ""
    return "/".join(remainder)


def extract_benchmark_dps(soup: BeautifulSoup) -> float:
    text = soup.get_text(separator=" ", strip=True)
    for pat in [
        r'(\d{1,3}(?:,\d{3})+)\s*(?:DPS|dps)',
        r'(\d{5,6})\s*(?:DPS|dps)',
        r'[Bb]enchmark\D{0,8}(\d[\d,.]+)',
    ]:
        m = re.search(pat, text)
        if m:
            raw = m.group(1).replace(",", "")
            try:
                val = float(raw)
                if 5000 < val < 200000:
                    return val
            except ValueError:
                pass
    return 0.0


def extract_patch_version(soup: BeautifulSoup) -> str:
    text = soup.get_text(separator=" ", strip=True)
    for pat in [
        r'[Pp]atch[:\s]+(\d{4}-\d{2}-\d{2})',
        r'[Uu]pdated?[:\s]+(\d{4}-\d{2}-\d{2})',
        r'[Vv]ersion[:\s]+(\d{4}-\d{2}-\d{2})',
    ]:
        m = re.search(pat, text)
        if m:
            return m.group(1)
    return ""


def extract_notes(soup: BeautifulSoup) -> str:
    meta = soup.find("meta", attrs={"name": "description"})
    if meta:
        desc = (meta.get("content") or "").strip()
        if len(desc) > 20:
            return desc[:500]
    return ""


def build_type_from_slug(slug: str) -> str:
    for part in slug.split("-"):
        if part in BUILD_TYPE_KEYWORDS:
            return BUILD_TYPE_KEYWORDS[part]
    return "Power"


def elite_spec_from_lines(spec_lines: list[dict]) -> str:
    for line in spec_lines:
        if line["spec_id"] in ELITE_SPEC_IDS:
            return SPEC_ID_NAME.get(line["spec_id"], "")
    return ""


# ---------------------------------------------------------------------------
# Parse a single build page
# ---------------------------------------------------------------------------
def parse_build_page(meta: dict, delay: float) -> Optional[dict]:
    url  = meta["url"]
    html = fetch(url, delay=delay)
    if not html:
        return None

    soup   = BeautifulSoup(html, "lxml")
    parsed = parse_armory_embeds(soup)

    spec_lines = parsed["spec_lines"]
    skill_bar  = parsed["skill_bar"]
    legends    = parsed["legends"]
    gear       = parsed["gear"]

    if not spec_lines:
        print("    WARN: no armory embeds — %s" % url)
        return None

    traits: dict = {}
    for i, line in enumerate(spec_lines[:3]):
        traits["line%d" % (i + 1)] = {"spec_id": line["spec_id"], "traits": line["traits"]}
    for key in ("line1", "line2", "line3"):
        if key not in traits:
            traits[key] = {}

    if skill_bar:
        skills = {"heal": skill_bar["heal"],
                  "utilities": skill_bar["utilities"],
                  "elite": skill_bar["elite"]}
    else:
        skills = {"heal": 0, "utilities": [0, 0, 0], "elite": 0}

    chat_code = generate_chat_code(meta["profession"], spec_lines, skill_bar,
                                   legends=legends if legends else None)

    return {
        "id":            "%s-%s" % (meta["profession"], meta["slug"]),
        "name":          extract_build_name(soup, meta["slug"]),
        "profession":    meta["profession"].title(),
        "elite_spec":    elite_spec_from_lines(spec_lines),
        "build_type":    build_type_from_slug(meta["slug"]),
        "patch_version": extract_patch_version(soup),
        "benchmark_dps": extract_benchmark_dps(soup),
        "notes":         extract_notes(soup),
        "source_url":    url,
        "chat_code":     chat_code,
        "traits":        traits,
        "skills":        skills,
        "legends":       legends if legends else [0, 0],
        "gear":          gear,
        "rotation":      {"opener": [], "loop": []},
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def _print_result(result: dict) -> None:
    spec  = result["elite_spec"] or "?"
    dps   = ("%.0f" % result["benchmark_dps"]) if result["benchmark_dps"] else "?"
    nspec = sum(1 for k in ("line1", "line2", "line3") if result["traits"].get(k))
    nsk   = "yes" if result["skills"]["heal"] else "no"
    ngear = len(result["gear"]["items"])
    relic = result["gear"]["relic_id"]
    chat  = "yes" if result.get("chat_code") else "no"
    print("OK [%s] DPS=%s specs=%d skills=%s gear=%d relic=%d chat=%s name=%r" %
          (spec, dps, nspec, nsk, ngear, relic, chat, result["name"]))


def _merge_into_prof_file(out_dir: Path, result: dict) -> None:
    """Upsert a single build into the per-profession JSON file."""
    prof_slug  = result["profession"].lower()
    build_id   = result["id"]
    prof_path  = out_dir / ("sc_builds_" + prof_slug + ".json")

    existing: list[dict] = []
    if prof_path.exists():
        try:
            existing = json.loads(prof_path.read_text(encoding="utf-8"))
        except Exception:
            pass

    # Replace matching id in-place, or append if not found
    replaced = False
    for i, b in enumerate(existing):
        if b.get("id") == build_id:
            existing[i] = result
            replaced = True
            break
    if not replaced:
        existing.append(result)

    with open(prof_path, "w", encoding="utf-8") as fh:
        json.dump(existing, fh, indent=2, ensure_ascii=False)
    action = "updated" if replaced else "added"
    print("  %s  →  %s  (%s, %d total)" %
          (build_id, prof_path.name, action, len(existing)))


def main() -> None:
    ap = argparse.ArgumentParser(description="Scrape Snow Crows → sc_builds_full.json")
    ap.add_argument("--out",   default=str(DEFAULT_OUT))
    ap.add_argument("--delay", type=float, default=1.5)
    ap.add_argument("--force", action="store_true", help="Clear disk cache and re-fetch everything")
    ap.add_argument("--limit", type=int,   default=0, help="Max builds per prof, 0=all")
    ap.add_argument("--prof",  default="",            help="Only this profession slug")
    ap.add_argument("--url",   default="",
                    help="Scrape a single build URL, e.g. "
                         "https://snowcrows.com/builds/raids/warrior/condition-paragon-longbow-sword-sword")
    args = ap.parse_args()

    out_dir = Path(args.out).parent.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    # ── Single-build mode ────────────────────────────────────────────────────
    if args.url:
        url = args.url.rstrip("/")
        # Expect path: /builds/raids/<prof>/<slug>
        m = re.search(r"/builds/raids/([a-z]+)/([a-z0-9-]+)$", url)
        if not m:
            print("ERROR: URL does not look like a Snow Crows build page.")
            print("       Expected: https://snowcrows.com/builds/raids/<prof>/<slug>")
            sys.exit(1)
        prof_slug  = m.group(1)
        build_slug = m.group(2)
        if prof_slug not in PROFESSION_SLUGS:
            print("ERROR: Unknown profession %r in URL." % prof_slug)
            sys.exit(1)

        if args.force:
            # Evict only the cache entry for this specific URL
            safe = re.sub(r"[^a-zA-Z0-9_-]", "_", url.replace(BASE_URL, ""))
            for cp in [CACHE_DIR / (safe[:200] + ".html"),
                       CACHE_DIR / ("_pw_" + safe[:200] + ".html")]:
                if cp.exists():
                    cp.unlink()

        print("=== Snow Crows Scraper (single build) ===")
        print("URL    : %s" % url)
        print("Output : %s" % out_dir)
        print()

        print("Loading GW2 API caches...")
        try:
            load_api_caches(force=False)
        except Exception as exc:
            print("  WARN: API cache load failed: %s" % exc)
        print()

        meta   = {"url": url, "profession": prof_slug, "slug": build_slug}
        print("Scraping %s/%s ... " % (prof_slug, build_slug), end="", flush=True)
        result = parse_build_page(meta, delay=args.delay)
        if not result:
            print("FAILED")
            sys.exit(1)
        _print_result(result)
        print()

        _merge_into_prof_file(out_dir, result)
        print()
        print("Done.")
        print()
        print("Next steps:")
        print("  ./build.sh --regen          re-embed updated data into the DLL")
        print("  cp sc_builds_*.json <GW2>/addons/BuildCoach/cache/")
        return

    # ── Profession / full-site mode ──────────────────────────────────────────
    if args.prof:
        args.prof = args.prof.lower()
        if args.prof not in PROFESSION_SLUGS:
            print("ERROR: Unknown profession %r. Valid options: %s"
                  % (args.prof, ", ".join(PROFESSION_SLUGS)))
            sys.exit(1)

    if args.force:
        import shutil
        if CACHE_DIR.exists():
            shutil.rmtree(CACHE_DIR)
        CACHE_DIR.mkdir()

    profs = [args.prof] if args.prof else PROFESSION_SLUGS

    print("=== Snow Crows Scraper ===")
    print("Output   : %s" % args.out)
    print("Delay    : %.1fs" % args.delay)
    print("Profs    : %s" % ", ".join(profs))
    print("Playwright: %s" % ("yes" if HAS_PLAYWRIGHT else "no (install for full coverage)"))
    print()

    print("Loading GW2 API caches (professions + specializations)...")
    try:
        load_api_caches(force=args.force)
        print("  Loaded palette maps for: %s" % ", ".join(sorted(_skill_to_palette.keys())))
        print("  Loaded %d spec trait layouts" % len(_spec_major_traits))
    except Exception as exc:
        print("  WARN: API cache load failed: %s" % exc)
        print("  Chat codes will be empty.")
    print()

    all_builds: list[dict] = []

    for prof in profs:
        print("── %s ──" % prof.title())
        metas = discover_build_urls(prof, args.delay, force=args.force)
        print("  %d build page(s) found" % len(metas))
        if args.limit > 0:
            metas = metas[: args.limit]

        for i, meta in enumerate(metas, 1):
            print("  [%2d/%d] %s  " % (i, len(metas), meta["slug"]), end="", flush=True)
            result = parse_build_page(meta, delay=args.delay)
            if result:
                all_builds.append(result)
                _print_result(result)
            else:
                print("SKIP")
        print()

    # Always write per-profession files (enables individual re-scrapes)
    by_prof: dict[str, list] = {}
    for build in all_builds:
        slug = build["profession"].lower()
        if slug not in by_prof:
            by_prof[slug] = []
        by_prof[slug].append(build)

    print("Writing per-profession files:")
    for slug in sorted(by_prof.keys()):
        prof_path = out_dir / ("sc_builds_" + slug + ".json")
        with open(prof_path, "w", encoding="utf-8") as fh:
            json.dump(by_prof[slug], fh, indent=2, ensure_ascii=False)
        print("  %3d builds  →  %s" % (len(by_prof[slug]), prof_path.name))

    # Write combined file only when all professions were scraped in this run
    if not args.prof:
        out_path = Path(args.out).resolve()
        with open(out_path, "w", encoding="utf-8") as fh:
            json.dump(all_builds, fh, indent=2, ensure_ascii=False)
        print("  %3d builds  →  %s  (combined)" % (len(all_builds), out_path.name))

    print()
    print("Done. %d build(s) total." % len(all_builds))
    print()
    print("Next steps:")
    print("  ./build.sh --regen          re-embed all data into the DLL")
    print("  cp sc_builds_*.json <GW2>/addons/BuildCoach/cache/")


if __name__ == "__main__":
    main()
