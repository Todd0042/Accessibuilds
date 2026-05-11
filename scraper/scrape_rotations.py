#!/usr/bin/env python3
"""
Scrape rotation/guide sections from Snow Crows accessibility build pages.
Reads sc_builds_accessibility.json, fetches each build page, extracts
collapsible guide sections (h3 headings + prose content), and writes
sc_rotations_accessibility.json.

Output format (keyed by build id):
{
  "guardian-power-dragonhunter": {
    "sections": [
      {"title": "Key Concepts",  "items": [{"text": "..."}, ...]},
      {"title": "Step One",     "items": [{"text": "..."}, ...]},
      ...
    ]
  }
}
"""

import json
import re
import sys
import time
from pathlib import Path

try:
    import requests
    from bs4 import BeautifulSoup
except ImportError:
    print("ERROR: pip install requests beautifulsoup4 lxml")
    sys.exit(1)

BASE_URL = "https://snowcrows.com"
SCRIPT_DIR = Path(__file__).parent
DEFAULT_INPUT = SCRIPT_DIR / ".." / "sc_builds_accessibility.json"
DEFAULT_OUTPUT = SCRIPT_DIR / ".." / "sc_rotations_accessibility.json"

HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                  "AppleWebKit/537.36 (KHTML, like Gecko) "
                  "Chrome/124.0.0.0 Safari/537.36",
}

# h3 headings that form guide sections
SECTION_HEADINGS = [
    "key concepts", "step one", "step two", "step three",
    "step four", "step five", "step six", "defense",
    "crowd control", "opener", "priority", "skill usage",
]


def fetch(url: str, delay: float) -> str | None:
    time.sleep(delay)
    try:
        r = requests.get(url, headers=HEADERS, timeout=30)
        r.raise_for_status()
        return r.text
    except requests.RequestException as exc:
        print("    WARN: fetch %s" % exc)
        return None


def _extract_item(el):
    """Recursively walk an element, replacing skill embeds with {skill:N}.
    Returns (text, skill_ids) tuple.
    """
    parts = []
    skill_ids = []

    for child in el.children:
        if isinstance(child, str):
            parts.append(child)
            continue

        tag = child.name
        if tag is None:
            if child.strip():
                parts.append(child)
            continue

        # ── Skill embed: <span data-armory-embed="skills" data-armory-ids="N,..."> ──
        if tag == "span" and child.get("data-armory-embed") == "skills":
            ids_str = child.get("data-armory-ids", "")
            for sid in ids_str.split(","):
                sid = sid.strip()
                if not sid:
                    continue
                try:
                    sid_int = int(sid)
                    skill_ids.append(sid_int)
                    parts.append("{skill:%d}" % sid_int)
                except ValueError:
                    pass
            continue

        # ── Armory custom term: <span class="sc-armory-custom" ...>Text</span> ──
        # Keep the visible text (e.g. "Damage", "Cooldown", "Auto Attack")
        if tag == "span" and child.get("class") and "sc-armory-custom" in child.get("class"):
            inner = child.get_text(strip=True)
            if inner:
                parts.append(inner)
            continue

        # ── Inline code (F1, 2, 3, etc.) ──
        if tag == "code":
            inner = child.get_text(strip=True)
            if inner:
                parts.append(inner)
            continue

        # ── Arrow icon <i class="fas fa-arrow-right"> ──
        if tag == "i":
            cls = child.get("class", [])
            if any("arrow-right" in c for c in cls):
                parts.append(" -> ")
            continue

        # ── Line break ──
        if tag == "br":
            parts.append(" ")
            continue

        # ── Nested list — skip (inner items are picked up by find_all) ──
        if tag in ("ul", "ol"):
            continue

        # ── Any other tag — recurse ──
        sub_text, sub_ids = _extract_item(child)
        if sub_text:
            parts.append(sub_text)
        skill_ids.extend(sub_ids)

    text = "".join(parts)
    text = re.sub(r"\s+", " ", text).strip()
    return text, skill_ids


def extract_rotation_sections(html: str) -> list[dict]:
    """Parse <h3> guide sections + their content divs from the page HTML."""
    soup = BeautifulSoup(html, "lxml")
    sections = []

    for h3 in soup.find_all("h3"):
        title = h3.get_text(strip=True)
        title_lower = title.lower()
        matched = [t for t in SECTION_HEADINGS if title_lower.startswith(t)]
        if not matched:
            continue

        # Content is in the NEXT sibling div of h3's parent
        parent_div = h3.parent
        content_div = parent_div.find_next_sibling("div")
        if not content_div:
            continue

        # Text lives inside <span class="md"> within the content div
        md_span = content_div.find("span", class_="md")
        if not md_span:
            md_span = content_div

        items = []
        for el in md_span.find_all(["p", "li"], recursive=True):
            text, sids = _extract_item(el)
            text = text.strip()
            if not text and not sids:
                continue
            items.append({"text": text})

        sections.append({"title": title, "items": items})

    return sections


def main() -> None:
    import argparse
    ap = argparse.ArgumentParser(
        description="Scrape rotation sections for accessibility builds")
    ap.add_argument("--input", default=str(DEFAULT_INPUT))
    ap.add_argument("--output", default=str(DEFAULT_OUTPUT))
    ap.add_argument("--delay", type=float, default=2.0)
    args = ap.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print("ERROR: input file not found: %s" % input_path)
        print("Run scrape_snowcrows.py --listing-url first.")
        sys.exit(1)

    builds = json.loads(input_path.read_text(encoding="utf-8"))
    print("=== Rotation Scraper ===")
    print("Input  : %s (%d builds)" % (input_path, len(builds)))
    print("Output : %s" % args.output)
    print()

    rotations = {}
    for i, build in enumerate(builds, 1):
        bid = build.get("id", "")
        url = build.get("source_url", "")
        name = build.get("name", bid)
        if not url:
            print("  [%2d/%d] %s  SKIP (no URL)" % (i, len(builds), bid))
            continue

        print("  [%2d/%d] %s  " % (i, len(builds), bid), end="", flush=True)
        html = fetch(url, delay=args.delay)
        if not html:
            print("FETCH FAIL")
            continue

        sections = extract_rotation_sections(html)
        if sections:
            rotations[bid] = {"sections": sections}
            n = sum(len(s["items"]) for s in sections)
            print("OK  (%d sections, %d items)" % (len(sections), n))
        else:
            print("OK  (no sections)")
            rotations[bid] = {"sections": []}

    out_path = Path(args.output)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(rotations, f, indent=2, ensure_ascii=False)

    total_sections = sum(len(v["sections"]) for v in rotations.values())
    print()
    print("Done. %d build(s), %d total sections written to %s" %
          (len(rotations), total_sections, out_path))


if __name__ == "__main__":
    main()
