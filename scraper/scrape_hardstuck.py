#!/usr/bin/env python3
"""
Hardstuck build scraper for BuildCoach.

Discovery:
  Per-profession listing pages  → h3 section headers identify game mode
  WordPress sitemap             → completeness + lastmod for change detection

HTML parsing mirrors hardstuck.cpp:
  <gw2object type="specialization">  in .gw2-build-traits     → traits
  <gw2object type="skill">           in .skills-utility        → 5-slot skill bar
  .gw2-build-equipment-container.armors    → armor (6 positional slots)
  .gw2-build-equipment-container.weapons  → weapon sets
  .gw2-build-equipment-container.trinkets → trinkets + relic (no stats= → relic)
  .gw2-build-equipment-container.food     → food + utility
  <gw2object type="pet">               → ranger pets
  <gw2object type="legend">            → revenant legends
  First [& in HTML                     → chat code

Extra fields vs Snow Crows schema:
  game_mode  — "PvP"|"GroupPvE"|"WvWZerg"|"WvWRoaming"|"OpenWorld"|"Fractal"|"Raid"|"Strike"
  source     — "hardstuck"

Output (default: ../hs-builds/):
  hs_builds_<profession>.json   per-profession, upserted on re-run
  hs_builds_full.json           combined

Usage:
  python3 scrape_hardstuck.py                           # all professions
  python3 scrape_hardstuck.py --prof ranger             # one profession
  python3 scrape_hardstuck.py --prof warrior --limit 3  # quick test
  python3 scrape_hardstuck.py --url "https://hardstuck.gg/gw2/builds/guardian/power-dragonhunter/"
  python3 scrape_hardstuck.py --sitemap-only            # list sitemap URLs and exit
  python3 scrape_hardstuck.py --force                   # clear cache, re-fetch

Requirements:
  pip install requests beautifulsoup4 lxml

Note: Hardstuck has given explicit permission to use their builds in BuildCoach.
"""

import argparse
import json
import re
import shutil
import sys
import time
import xml.etree.ElementTree as ET
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
BASE_URL        = "https://hardstuck.gg"
SITEMAP_PATTERN = BASE_URL + "/wp-sitemap-posts-gw2_builds-%d.xml"
SCRIPT_DIR      = Path(__file__).parent
DEFAULT_OUT_DIR = SCRIPT_DIR / ".." / "hs-builds"
CACHE_DIR       = SCRIPT_DIR / ".hs_cache"

HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/124.0.0.0 Safari/537.36"
    ),
    "Accept":          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "Accept-Language": "en-US,en;q=0.9",
    "Referer":         BASE_URL + "/",
}

PROFESSIONS = [
    "guardian", "warrior", "engineer", "ranger", "thief",
    "elementalist", "mesmer", "necromancer", "revenant",
]
PROFESSION_DISPLAY = {p: p.title() for p in PROFESSIONS}

# h3 header text (lowercased) → canonical game_mode string
GAME_MODE_MAP: dict[str, str] = {
    "pvp":             "PvP",
    "group pve":       "GroupPvE",
    "pve":             "GroupPvE",
    "wvw - zerg":      "WvWZerg",
    "wvw zerg":        "WvWZerg",
    "wvw - roaming":   "WvWRoaming",
    "wvw roaming":     "WvWRoaming",
    "wvw":             "WvW",
    "open world":      "OpenWorld",
    "fractals":        "Fractal",
    "fractal":         "Fractal",
    "raids":           "Raid",
    "raid":            "Raid",
    "strike missions": "Strike",
    "strike":          "Strike",
    "dungeons":        "Dungeon",
    "dungeon":         "Dungeon",
}

# Positional slot names for equipment containers (order matches Hardstuck HTML order)
ARMOR_SLOTS   = ["Helm", "Shoulders", "Chest", "Gloves", "Leggings", "Boots"]
TRINKET_SLOTS = ["BackItem", "Amulet", "Ring1", "Ring2", "Accessory1", "Accessory2"]

SPEC_ID_NAME: dict[int, str] = {
    16: "Radiance",    42: "Virtues",       46: "Honor",
    27: "Dragonhunter",62: "Firebrand",     65: "Willbender",
    4:  "Strength",    51: "Arms",          22: "Defense",
    18: "Berserker",   61: "Spellbreaker",  68: "Bladesworn",
    6:  "Firearms",    47: "Inventions",    29: "Explosives",
    43: "Scrapper",    57: "Holosmith",     70: "Mechanist",
    8:  "Skirmishing", 25: "Marksmanship",  30: "Nature Magic",
    5:  "Druid",       55: "Soulbeast",     72: "Untamed",
    28: "Shadow Arts", 20: "Deadly Arts",   35: "Critical Strikes",
    7:  "Daredevil",   58: "Deadeye",       71: "Specter",
    31: "Fire",        26: "Water",         37: "Arcane",
    48: "Tempest",     56: "Weaver",        67: "Catalyst",
    10: "Domination",  23: "Illusions",     24: "Chaos",
    40: "Chronomancer",59: "Mirage",        66: "Virtuoso",
    50: "Soul Reaping",2:  "Spite",         39: "Death Magic",
    34: "Reaper",      60: "Scourge",       64: "Harbinger",
    3:  "Salvation",   14: "Retribution",   9:  "Corruption",
    52: "Herald",      63: "Renegade",      69: "Vindicator",
    81: "Luminary",    74: "Paragon",       75: "Amalgam",
    78: "Galeshot",    77: "Antiquary",     80: "Evoker",
    73: "Troubadour",  76: "Ritualist",     79: "Conduit",
}

ELITE_SPEC_IDS: set[int] = {
    27, 62, 65, 18, 61, 68, 43, 57, 70, 5,  55, 72,
    7,  58, 71, 48, 56, 67, 40, 59, 66, 34, 60, 64,
    52, 63, 69, 81, 74, 75, 78, 77, 80, 73, 76, 79,
}

# URL slug keywords → (profession, elite_spec)
SLUG_SPEC_MAP: dict[str, tuple[str, str]] = {
    "dragonhunter":  ("Guardian",     "Dragonhunter"),
    "firebrand":     ("Guardian",     "Firebrand"),
    "willbender":    ("Guardian",     "Willbender"),
    "berserker":     ("Warrior",      "Berserker"),
    "spellbreaker":  ("Warrior",      "Spellbreaker"),
    "bladesworn":    ("Warrior",      "Bladesworn"),
    "scrapper":      ("Engineer",     "Scrapper"),
    "holosmith":     ("Engineer",     "Holosmith"),
    "mechanist":     ("Engineer",     "Mechanist"),
    "druid":         ("Ranger",       "Druid"),
    "soulbeast":     ("Ranger",       "Soulbeast"),
    "untamed":       ("Ranger",       "Untamed"),
    "daredevil":     ("Thief",        "Daredevil"),
    "deadeye":       ("Thief",        "Deadeye"),
    "specter":       ("Thief",        "Specter"),
    "tempest":       ("Elementalist", "Tempest"),
    "weaver":        ("Elementalist", "Weaver"),
    "catalyst":      ("Elementalist", "Catalyst"),
    "chronomancer":  ("Mesmer",       "Chronomancer"),
    "mirage":        ("Mesmer",       "Mirage"),
    "virtuoso":      ("Mesmer",       "Virtuoso"),
    "reaper":        ("Necromancer",  "Reaper"),
    "scourge":       ("Necromancer",  "Scourge"),
    "harbinger":     ("Necromancer",  "Harbinger"),
    "herald":        ("Revenant",     "Herald"),
    "renegade":      ("Revenant",     "Renegade"),
    "vindicator":    ("Revenant",     "Vindicator"),
}

BUILD_TYPE_KEYWORDS: dict[str, str] = {
    "quicksupport": "Quickness",
    "quickness":    "Quickness",
    "quick":        "Quickness",
    "alacrity":     "Alacrity",
    "alac":         "Alacrity",
    "healer":       "Heal",
    "heal":         "Heal",
    "condition":    "Condi",
    "condi":        "Condi",
    "power":        "Power",
    "support":      "Support",
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


def fetch_xml(url: str, *, force: bool = False, delay: float = 1.0) -> Optional[str]:
    cp = _cache_path("xml_" + url, ext=".xml")
    if not force and cp.exists():
        return cp.read_text(encoding="utf-8")
    time.sleep(delay)
    try:
        r = _session.get(url, timeout=30)
        r.raise_for_status()
        cp.write_text(r.text, encoding="utf-8")
        return r.text
    except Exception as exc:
        print("    WARN fetch_xml %s: %s" % (url, exc))
        return None


# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------
def _parse_int_list(s: str) -> list[int]:
    result = []
    for tok in (s or "").split(","):
        tok = tok.strip()
        if tok and tok.lstrip("-").isdigit():
            result.append(int(tok))
    return result


def _normalize_game_mode(text: str) -> str:
    return GAME_MODE_MAP.get(text.strip().lower(), "")


def _is_build_url(href: str, prof: str) -> bool:
    """True if href is a build detail page for this profession (not the listing page itself)."""
    pattern = r"/gw2/builds/" + re.escape(prof) + r"/[^/?#]+"
    if not re.search(pattern, href):
        return False
    # Exclude the listing page itself
    if re.fullmatch(r".*gw2/builds/" + re.escape(prof) + r"/?", href.rstrip("/")):
        return False
    return True


def _eq_container(soup: BeautifulSoup, type_class: str):
    """Find <div class="gw2-build-equipment-container {type_class}">."""
    for div in soup.find_all(class_="gw2-build-equipment-container"):
        if type_class in (div.get("class") or []):
            return div
    return None


def _eq_pieces(container, type_class: str) -> list:
    """Find <div class="gw2-build-equipment-piece {type_class}"> inside container."""
    return [
        p for p in container.find_all(class_="gw2-build-equipment-piece")
        if type_class in (p.get("class") or [])
    ]


# ---------------------------------------------------------------------------
# Build discovery
# ---------------------------------------------------------------------------
def discover_from_listing(prof: str, *, force: bool, delay: float) -> list[dict]:
    """Fetch the per-profession listing page and extract (url, game_mode) pairs."""
    url  = "%s/gw2/builds/%s/" % (BASE_URL, prof)
    html = fetch_html(url, force=force, delay=delay)
    if not html:
        print("  WARN: could not fetch listing page for %s" % prof)
        return []

    soup  = BeautifulSoup(html, "lxml")
    builds: list[dict] = []
    seen:   set[str]   = set()

    for h3 in soup.find_all("h3"):
        mode = _normalize_game_mode(h3.get_text(strip=True))
        if not mode or mode == "PvP":
            continue  # PvP uses a different gear system; skip
        for sib in h3.find_next_siblings():
            if sib.name in ("h2", "h3"):
                break
            # On Hardstuck listing pages, build links are direct <a> siblings —
            # not nested inside wrapper divs — so check the sibling itself first.
            hrefs = []
            if sib.name == "a" and sib.get("href"):
                hrefs.append(sib["href"])
            else:
                hrefs = [a["href"] for a in sib.find_all("a", href=True)]
            for href in hrefs:
                if not _is_build_url(href, prof):
                    continue
                full = (BASE_URL + href if href.startswith("/") else href).rstrip("/") + "/"
                if full not in seen:
                    seen.add(full)
                    builds.append({"url": full, "game_mode": mode,
                                   "profession": PROFESSION_DISPLAY[prof]})
    return builds


def discover_from_sitemap(*, force: bool, delay: float) -> dict[str, str]:
    """
    Parse all WordPress build sitemap pages and return {url: lastmod}.
    Stops when a page returns 0 entries or a fetch fails.
    """
    entries: dict[str, str] = {}
    NS = "http://www.sitemaps.org/schemas/sitemap/0.9"

    for page in range(1, 30):
        sitemap_url = SITEMAP_PATTERN % page
        xml_text    = fetch_xml(sitemap_url, force=force, delay=delay)
        if not xml_text:
            break
        try:
            root = ET.fromstring(xml_text)
        except ET.ParseError as exc:
            print("  WARN sitemap page %d parse error: %s" % (page, exc))
            break

        count = 0
        for url_el in root.findall("{%s}url" % NS):
            loc_el = url_el.find("{%s}loc" % NS)
            mod_el = url_el.find("{%s}lastmod" % NS)
            if loc_el is not None and loc_el.text:
                loc     = loc_el.text.strip().rstrip("/") + "/"
                lastmod = (mod_el.text or "").strip() if mod_el is not None else ""
                entries[loc] = lastmod
                count += 1

        print("  Sitemap page %d: %d entries" % (page, count))
        if count == 0:
            break

    return entries


def discover_all(prof_filter: str, *, force: bool, delay: float) -> list[dict]:
    """
    Combine listing-page discovery (gives game_mode) with sitemap (catches any missed URLs).
    """
    profs   = [prof_filter] if prof_filter else PROFESSIONS
    by_url: dict[str, dict] = {}

    for prof in profs:
        print("  Listing: %s ..." % prof)
        found = discover_from_listing(prof, force=force, delay=delay)
        print("    %d build(s) (game mode assigned)" % len(found))
        for b in found:
            by_url[b["url"]] = b

    print("  Sitemap ...")
    sitemap = discover_from_sitemap(force=force, delay=delay)
    added = 0
    for url, _lastmod in sitemap.items():
        if url in by_url:
            continue
        m = re.search(r"/gw2/builds/([^/]+)/[^/]+/", url)
        if not m:
            continue
        slug = m.group(1).lower()
        if slug not in PROFESSIONS:
            continue
        if prof_filter and slug != prof_filter:
            continue
        by_url[url] = {
            "url":        url,
            "game_mode":  "Unknown",
            "profession": PROFESSION_DISPLAY.get(slug, slug.title()),
        }
        added += 1

    if added:
        print("    +%d from sitemap only (game_mode=Unknown)" % added)

    return list(by_url.values())


# ---------------------------------------------------------------------------
# HTML parsing (mirrors hardstuck.cpp)
# ---------------------------------------------------------------------------
def extract_build_name(soup: BeautifulSoup) -> str:
    h1 = soup.find("h1")
    if h1:
        # The h1 contains the name as a text node and a <div> child with the date.
        # Get only direct text nodes (skip child elements like the date div).
        name = "".join(t for t in h1.strings
                       if t.parent == h1 or t.parent.name not in ("div", "span", "time")).strip()
        if name:
            return name
        # Fallback: strip trailing month+year pattern ("October 2024")
        raw = h1.get_text(strip=True)
        return re.sub(r"\s+(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\w*\s+\d{4}.*$",
                      "", raw, flags=re.I).strip()
    title_tag = soup.find("title")
    if title_tag:
        t = title_tag.get_text(strip=True)
        return re.sub(r"\s*[-|]\s*(Hardstuck|GW2).*$", "", t, flags=re.I).strip()
    return ""


def extract_chat_code(raw_html: str) -> str:
    """First [& chat code found in the raw HTML (includes attribute values)."""
    m = re.search(r'\[&[A-Za-z0-9+/=]{20,}\]', raw_html)
    return m.group(0) if m else ""


def extract_traits(soup: BeautifulSoup) -> list[dict]:
    """
    Parse trait lines from .gw2-build-traits (.gw2-biuld-traits is a known typo on some pages).
    <gw2object type="specialization" objid="{spec_id}">
      <gw2object type="trait" class="skeleton trait_major [trait_unselected]" objid="{id}">
    Selected = has trait_major but NOT trait_unselected.
    """
    container = (
        soup.find(class_="gw2-build-traits") or
        soup.find(class_="gw2-biuld-traits")
    )
    if not container:
        return []

    spec_lines: list[dict] = []
    for spec_obj in container.find_all("gw2object", attrs={"type": "specialization"}):
        sid = spec_obj.get("objid", "").strip()
        if not sid.isdigit():
            continue
        spec_id = int(sid)

        # Primary: children without trait_unselected
        selected: list[int] = []
        for trait_obj in spec_obj.find_all("gw2object", attrs={"type": "trait"}):
            cls = trait_obj.get("class") or []
            if isinstance(cls, str):
                cls = cls.split()
            if "trait_major" in cls and "trait_unselected" not in cls:
                tid = trait_obj.get("objid", "").strip()
                if tid.isdigit():
                    selected.append(int(tid))

        # Fallback: use selected_traits="0,2,0" attribute + all major trait children
        if not selected:
            choices = _parse_int_list(spec_obj.get("selected_traits", ""))
            all_major = []
            for trait_obj in spec_obj.find_all("gw2object", attrs={"type": "trait"}):
                cls = trait_obj.get("class") or []
                if isinstance(cls, str):
                    cls = cls.split()
                if "trait_major" in cls:
                    all_major.append(trait_obj)
            for grp, choice in enumerate(choices[:3]):
                idx = grp * 3 + min(choice, 2)
                if idx < len(all_major):
                    tid = all_major[idx].get("objid", "").strip()
                    if tid.isdigit():
                        selected.append(int(tid))

        while len(selected) < 3:
            selected.append(0)
        spec_lines.append({"spec_id": spec_id, "traits": selected[:3]})
        if len(spec_lines) == 3:
            break

    return spec_lines


def extract_skills(soup: BeautifulSoup, is_revenant: bool) -> tuple[dict, list[int]]:
    """
    5 <gw2object type="skill"> in .skills-utility → heal / util1 / util2 / util3 / elite.
    <gw2object type="legend"> → revenant legend IDs (up to 2).
    """
    skills  = {"heal": 0, "utilities": [0, 0, 0], "elite": 0}
    legends: list[int] = []

    if is_revenant:
        for leg in soup.find_all("gw2object", attrs={"type": "legend"}):
            lid = leg.get("objid", "").strip()
            if lid.isdigit():
                legends.append(int(lid))
            if len(legends) == 2:
                break

    container = soup.find(class_="skills-utility")
    if not container:
        # Fallback: find any container with 5+ skill objects
        for div in soup.find_all(class_=True):
            if len(div.find_all("gw2object", attrs={"type": "skill"})) >= 5:
                container = div
                break

    if container:
        ids = []
        for sk in container.find_all("gw2object", attrs={"type": "skill"}):
            sid = sk.get("objid", "").strip()
            if sid.isdigit():
                ids.append(int(sid))
        if len(ids) >= 5:
            skills = {"heal": ids[0], "utilities": ids[1:4], "elite": ids[4]}
        elif ids:
            skills["heal"] = ids[0]
            skills["utilities"] = (ids[1:4] + [0, 0, 0])[:3]
            skills["elite"] = ids[4] if len(ids) > 4 else 0

    return skills, legends


def extract_gear(soup: BeautifulSoup) -> dict:
    gear: dict = {"relic_id": 0, "food_id": 0, "utility_id": 0, "items": []}

    # ── Armor ───────────────────────────────────────────────────────────────
    armor_cont = _eq_container(soup, "armors")
    if armor_cont:
        pieces = _eq_pieces(armor_cont, "armor")
        for idx, piece in enumerate(pieces[:6]):
            gwo = piece.find("gw2object", attrs={"type": "item"})
            if not gwo:
                continue
            oid = gwo.get("objid", "").strip()
            if not oid.isdigit():
                continue
            slotted    = _parse_int_list(gwo.get("slotted", ""))
            stat_s     = gwo.get("stats", "").strip()
            gear["items"].append({
                "slot":       ARMOR_SLOTS[idx] if idx < len(ARMOR_SLOTS) else "Helm",
                "item_id":    int(oid),
                "stat_id":    int(stat_s) if stat_s.isdigit() else 0,
                "stat_name":  "",
                "upgrade_id": slotted[0] if slotted else 0,
                "infusions":  slotted[1:] if len(slotted) > 1 else [],
            })

    # ── Weapons ─────────────────────────────────────────────────────────────
    weapon_cont = _eq_container(soup, "weapons")
    if weapon_cont:
        ws_divs    = weapon_cont.find_all(class_="weapon-set")
        slot_pairs = [("WeaponA1", "WeaponA2"), ("WeaponB1", "WeaponB2")]
        for set_idx, ws in enumerate(ws_divs[:2]):
            slot_mh, slot_oh = slot_pairs[set_idx]
            weapon_objs = [
                g for g in ws.find_all("gw2object", attrs={"type": "item"})
                if g.get("stats", "")
            ]
            for w_idx, gwo in enumerate(weapon_objs[:2]):
                oid = gwo.get("objid", "").strip()
                if not oid.isdigit():
                    continue
                slotted = _parse_int_list(gwo.get("slotted", ""))
                stat_s  = gwo.get("stats", "").strip()
                w = {
                    "slot":       slot_mh if w_idx == 0 else slot_oh,
                    "item_id":    int(oid),
                    "stat_id":    int(stat_s) if stat_s.isdigit() else 0,
                    "upgrade_id": slotted[0] if slotted else 0,
                    "infusions":  [],
                }
                if len(slotted) > 1:
                    w["upgrade2_id"] = slotted[1]
                gear["items"].append(w)

    # ── Trinkets + Relic ─────────────────────────────────────────────────────
    trinket_cont = _eq_container(soup, "trinkets")
    if trinket_cont:
        t_idx = 0
        for piece in trinket_cont.find_all(class_="gw2-build-equipment-piece"):
            gwo = piece.find("gw2object", attrs={"type": "item"})
            if not gwo:
                continue
            oid = gwo.get("objid", "").strip()
            if not oid.isdigit():
                continue
            stat_s  = gwo.get("stats", "").strip()
            slotted = _parse_int_list(gwo.get("slotted", ""))
            if not stat_s:
                # No stats= → relic
                gear["relic_id"] = int(oid)
            else:
                gear["items"].append({
                    "slot":       TRINKET_SLOTS[t_idx] if t_idx < len(TRINKET_SLOTS) else "Amulet",
                    "item_id":    int(oid),
                    "stat_id":    int(stat_s) if stat_s.isdigit() else 0,
                    "stat_name":  "",
                    "upgrade_id": slotted[0] if slotted else 0,
                    "infusions":  slotted[1:] if len(slotted) > 1 else [],
                })
                t_idx += 1

    # ── Food + Utility ────────────────────────────────────────────────────────
    food_cont = _eq_container(soup, "food")
    if food_cont:
        cons = []
        for gwo in food_cont.find_all("gw2object", attrs={"type": "item"}):
            if gwo.get("stats", ""):
                continue
            oid = gwo.get("objid", "").strip()
            if oid.isdigit() and int(oid) > 0:
                cons.append(int(oid))
        if cons:
            gear["food_id"] = cons[0]
        if len(cons) > 1:
            gear["utility_id"] = cons[1]

    return gear


def extract_pets(soup: BeautifulSoup) -> list[int]:
    """Ranger pets from <gw2object type="pet">."""
    pets: list[int] = []
    # Prefer the dedicated pets equipment piece
    pets_cont = _eq_container(soup, "pets")
    root = pets_cont if pets_cont else soup
    for gwo in root.find_all("gw2object", attrs={"type": "pet"}):
        pid = gwo.get("objid", "").strip()
        if pid.isdigit():
            pets.append(int(pid))
        if len(pets) == 2:
            break
    while len(pets) < 2:
        pets.append(0)
    return pets


# ---------------------------------------------------------------------------
# Profession / spec helpers
# ---------------------------------------------------------------------------
def prof_spec_from_url(url: str) -> tuple[str, str]:
    slug = url.rstrip("/").split("/")[-1].lower().replace("-", "")
    for kw, (prof, spec) in SLUG_SPEC_MAP.items():
        if kw in slug:
            return prof, spec
    return "", ""


def elite_spec_from_spec_lines(spec_lines: list[dict]) -> str:
    for line in spec_lines:
        if line["spec_id"] in ELITE_SPEC_IDS:
            return SPEC_ID_NAME.get(line["spec_id"], "")
    return ""


def build_type_from_name(name: str) -> str:
    name_low = name.lower()
    for kw in sorted(BUILD_TYPE_KEYWORDS, key=len, reverse=True):
        if kw in name_low:
            return BUILD_TYPE_KEYWORDS[kw]
    return "Power"


# ---------------------------------------------------------------------------
# Parse a single build page
# ---------------------------------------------------------------------------
def parse_build_page(meta: dict, *, force: bool, delay: float) -> Optional[dict]:
    url       = meta["url"]
    game_mode = meta.get("game_mode", "Unknown")
    prof      = meta.get("profession", "")

    html = fetch_html(url, force=force, delay=delay)
    if not html:
        return None

    soup = BeautifulSoup(html, "lxml")

    build_name = extract_build_name(soup)
    if not build_name:
        return None

    # Profession from URL if not already set
    if not prof:
        m = re.search(r"/gw2/builds/([^/]+)/", url)
        if m:
            slug = m.group(1).lower()
            prof = PROFESSION_DISPLAY.get(slug, slug.title())

    is_rev    = prof.lower() == "revenant"
    is_ranger = prof.lower() == "ranger"

    spec_lines       = extract_traits(soup)
    skills, legends  = extract_skills(soup, is_revenant=is_rev)
    gear             = extract_gear(soup)
    chat_code        = extract_chat_code(html)
    pets             = extract_pets(soup) if is_ranger else [0, 0]

    # Elite spec: URL slug → overridden by parsed trait lines
    _, elite_spec = prof_spec_from_url(url)
    if spec_lines:
        parsed = elite_spec_from_spec_lines(spec_lines)
        if parsed:
            elite_spec = parsed

    traits: dict = {}
    for i, line in enumerate(spec_lines[:3]):
        t = line.get("traits", [0, 0, 0])
        while len(t) < 3:
            t.append(0)
        traits["line%d" % (i + 1)] = {"spec_id": line["spec_id"], "traits": t[:3]}
    for key in ("line1", "line2", "line3"):
        if key not in traits:
            traits[key] = {}

    while len(legends) < 2:
        legends.append(0)

    build_id = "hs-%s-%s" % (
        prof.lower(),
        re.sub(r"[^a-z0-9]+", "-", build_name.lower()).strip("-"),
    )

    return {
        "id":            build_id,
        "name":          build_name,
        "profession":    prof,
        "elite_spec":    elite_spec,
        "build_type":    build_type_from_name(build_name),
        "game_mode":     game_mode,
        "patch_version": "",
        "benchmark_dps": 0.0,
        "notes":         "",
        "source":        "hardstuck",
        "source_url":    url,
        "chat_code":     chat_code,
        "is_legacy":     False,
        "accessibility": False,
        "traits":        traits,
        "skills":        skills,
        "legends":       legends[:2],
        "pets":          pets,
        "gear":          gear,
        "rotation":      {"opener": [], "loop": []},
    }


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
def _print_result(r: dict) -> None:
    spec     = r["elite_spec"] or "?"
    mode     = r["game_mode"]
    has_sk   = "yes" if r["skills"]["heal"] or any(r["skills"]["utilities"]) else "no"
    has_sp   = "yes" if any(r["traits"].get("line%d" % i) for i in (1, 2, 3)) else "no"
    gear_cnt = len(r["gear"]["items"])
    chat     = "yes" if r["chat_code"] else "no"
    print("OK  [%-14s] %-14s sk=%-3s sp=%-3s gear=%-2d chat=%-3s  %r" % (
        spec, mode, has_sk, has_sp, gear_cnt, chat, r["name"]))


def write_outputs(all_builds: list[dict], out_dir: Path, mode_filter: str = "") -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    by_prof: dict[str, list] = {}
    for b in all_builds:
        by_prof.setdefault(b["profession"].lower(), []).append(b)

    print("\nWriting output files:")
    for prof_slug in sorted(by_prof.keys()):
        prof_path = out_dir / ("hs_builds_%s.json" % prof_slug)
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

    suffix   = ("_%s" % mode_filter.lower()) if mode_filter else ""
    combined = out_dir / ("hs_builds%s_full.json" % suffix)
    combined.write_text(json.dumps(all_builds, indent=2, ensure_ascii=False), encoding="utf-8")
    print("  %3d builds  →  %s  (combined)" % (len(all_builds), combined.name))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    ap = argparse.ArgumentParser(
        description="Hardstuck build scraper for BuildCoach",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
game modes: PvP  GroupPvE  WvWZerg  WvWRoaming  OpenWorld  Fractal  Raid  Strike

examples:
  python3 scrape_hardstuck.py                           # all professions
  python3 scrape_hardstuck.py --prof ranger             # one profession
  python3 scrape_hardstuck.py --prof warrior --limit 3  # quick test
  python3 scrape_hardstuck.py --url "https://hardstuck.gg/gw2/builds/guardian/power-dragonhunter/"
  python3 scrape_hardstuck.py --sitemap-only            # list sitemap URLs and exit
  python3 scrape_hardstuck.py --force                   # ignore cache, re-fetch everything

Hardstuck has given explicit permission to scrape their builds for BuildCoach.
""")
    ap.add_argument("--prof",         default="",    help="Single profession slug (e.g. ranger)")
    ap.add_argument("--mode",         default="",    help="Filter output by game_mode string")
    ap.add_argument("--delay",        type=float, default=1.5, help="Seconds between requests (default 1.5)")
    ap.add_argument("--force",        action="store_true",     help="Clear cache and re-fetch everything")
    ap.add_argument("--limit",        type=int,   default=0,   help="Max builds per profession, 0=all")
    ap.add_argument("--url",          default="",              help="Scrape a single Hardstuck build URL")
    ap.add_argument("--sitemap-only", action="store_true",     help="Discover sitemap URLs and exit")
    ap.add_argument("--out",          default=str(DEFAULT_OUT_DIR), help="Output directory")
    args = ap.parse_args()

    out_dir = Path(args.out).resolve()

    if args.force and CACHE_DIR.exists():
        shutil.rmtree(CACHE_DIR)
    CACHE_DIR.mkdir(exist_ok=True)

    print("=== Hardstuck Scraper ===")
    print("Output : %s" % out_dir)
    print("Delay  : %.1fs" % args.delay)
    print()

    # ── Single URL mode ───────────────────────────────────────────────────────
    if args.url:
        url = args.url.rstrip("/") + "/"
        m   = re.search(r"/gw2/builds/([^/]+)/", url)
        prof_display = ""
        if m:
            prof_display = PROFESSION_DISPLAY.get(m.group(1).lower(), m.group(1).title())
        meta = {"url": url, "game_mode": args.mode or "Unknown", "profession": prof_display}
        print("Scraping: %s" % url)
        result = parse_build_page(meta, force=args.force, delay=args.delay)
        if not result:
            print("FAILED")
            sys.exit(1)
        _print_result(result)
        write_outputs([result], out_dir)
        return

    # ── Sitemap-only mode ─────────────────────────────────────────────────────
    if args.sitemap_only:
        print("Discovering sitemap ...")
        sitemap = discover_from_sitemap(force=args.force, delay=args.delay)
        print("\n%d total sitemap entries:" % len(sitemap))
        for url, lastmod in sorted(sitemap.items()):
            print("  %-70s  %s" % (url, lastmod))
        return

    # ── Validate profession arg ───────────────────────────────────────────────
    if args.prof and args.prof not in PROFESSIONS:
        print("ERROR: unknown profession %r" % args.prof)
        print("Valid: %s" % ", ".join(PROFESSIONS))
        sys.exit(1)

    # ── Full scrape ───────────────────────────────────────────────────────────
    print("Discovering builds ...")
    all_meta = discover_all(args.prof, force=args.force, delay=args.delay)

    if args.mode:
        all_meta = [b for b in all_meta if b["game_mode"] == args.mode]
        print("  Filtered to mode %r: %d build(s)" % (args.mode, len(all_meta)))

    if args.limit > 0:
        by_prof: dict[str, list] = {}
        for b in all_meta:
            by_prof.setdefault(b["profession"], []).append(b)
        all_meta = []
        for builds in by_prof.values():
            all_meta.extend(builds[:args.limit])

    print("\nScraping %d build(s) ..." % len(all_meta))
    results: list[dict] = []
    for i, meta in enumerate(all_meta, 1):
        short = meta["url"].replace(BASE_URL, "")
        print("  [%2d/%d] %-55s  " % (i, len(all_meta), short), end="", flush=True)
        result = parse_build_page(meta, force=args.force, delay=args.delay)
        if result:
            results.append(result)
            _print_result(result)
        else:
            print("SKIP")

    if not results:
        print("No builds scraped.")
        sys.exit(0)

    write_outputs(results, out_dir, mode_filter=args.mode)
    print()
    print("Done.  %d build(s) total." % len(results))
    print()
    print("Note: Hardstuck has given explicit permission to use their builds in BuildCoach.")


if __name__ == "__main__":
    main()
