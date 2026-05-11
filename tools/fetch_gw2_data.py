#!/usr/bin/env python3
"""
Fetch GW2 API data for offline caching.
Run this BEFORE the API goes down for the patch.

Outputs:
- cache/gw2_api_data.json (consolidated cache)
- cache/specializations.json
- cache/skills.json
- cache/itemstats.json
- cache/items.json
- cache/traits.json
- cache/professions.json
"""

import json
import requests
import sys
from pathlib import Path
from datetime import datetime

API_BASE = "https://api.guildwars2.com/v2"
CACHE_DIR = Path("cache")

def fetch_json(endpoint, ids=None, batch_size=200):
    """Fetch data from GW2 API with batching."""
    if ids is None:
        # Get all IDs first
        id_url = f"{API_BASE}/{endpoint}?ids=all"
        print(f"  Fetching ID list: {id_url}")
        ids = requests.get(id_url).json()
        print(f"  Found {len(ids)} {endpoint}")
    
    # Batch requests (GW2 API limit: 200 IDs per request)
    results = []
    for i in range(0, len(ids), batch_size):
        batch = ids[i:i+batch_size]
        ids_str = ",".join(map(str, batch))
        url = f"{API_BASE}/{endpoint}?ids={ids_str}"
        
        try:
            resp = requests.get(url)
            if resp.status_code == 200:
                results.extend(resp.json())
                print(f"  Progress: {min(i+batch_size, len(ids))}/{len(ids)}")
            elif resp.status_code == 429:
                print(f"  Rate limited, waiting...")
                import time
                time.sleep(2)
                i -= batch_size  # Retry this batch
            else:
                print(f"  Error {resp.status_code} for batch {i}-{i+batch_size}")
        except Exception as e:
            print(f"  Exception: {e}")
    
    return results

def save_cache(data, filename):
    """Save data to cache file."""
    CACHE_DIR.mkdir(exist_ok=True)
    filepath = CACHE_DIR / filename
    with open(filepath, 'w') as f:
        json.dump(data, f, indent=2)
    print(f"✓ Saved {filepath} ({len(data)} entries)")

def main():
    print("=" * 60)
    print("GW2 API Data Fetcher for Accessibuilds")
    print("=" * 60)
    print()
    
    # Check API status
    print("Checking API status...")
    try:
        resp = requests.get(f"{API_BASE}/build")
        build_id = resp.json()
        print(f"Current GW2 build: {build_id}")
    except Exception as e:
        print(f"⚠ API may be down: {e}")
        retry = input("Continue anyway? (y/n): ")
        if retry.lower() != 'y':
            sys.exit(1)
    
    print()
    
    # Phase 1: Critical data
    print("Phase 1: Critical Data (Specializations + Skills)")
    print("-" * 60)
    
    # Specializations
    print("\n[1/6] Fetching specializations...")
    specs = fetch_json("specializations")
    save_cache(specs, "specializations.json")
    
    # Extract all trait IDs from specializations for later fetching
    trait_ids = set()
    for spec in specs:
        trait_ids.update(spec.get("minor_traits", []))
        trait_ids.update(spec.get("major_traits", []))
    print(f"  Found {len(trait_ids)} unique trait IDs")
    
    # Skills - need to get from professions first
    print("\n[2/6] Fetching professions (for skill IDs)...")
    prof_ids = ["elementalist", "warrior", "engineer", "ranger", 
                "thief", "mesmer", "necromancer", "revenant"]
    professions = []
    for prof in prof_ids:
        print(f"  Fetching {prof}...")
        try:
            resp = requests.get(f"{API_BASE}/professions/{prof}")
            if resp.status_code == 200:
                professions.append(resp.json())
        except Exception as e:
            print(f"  Error fetching {prof}: {e}")
    
    save_cache(professions, "professions.json")
    
    # Extract all skill IDs from profession skill palettes
    skill_ids = set()
    for prof in professions:
        skills_by_palette = prof.get("skills_by_palette", [])
        for entry in skills_by_palette:
            if isinstance(entry, list) and len(entry) >= 2:
                skill_ids.add(entry[1])  # skill_id is second element
    
    # Also add special skills from training panel
    for prof in professions:
        training = prof.get("training", [])
        for train in training:
            if "skills" in train:
                skill_ids.update(train["skills"])
    
    print(f"  Found {len(skill_ids)} unique skill IDs")
    
    print("\n[3/6] Fetching skills...")
    skills = fetch_json("skills", list(skill_ids))
    save_cache(skills, "skills.json")
    
    print("\nPhase 1 complete! Core data cached.")
    print()
    
    # Phase 2: Gear and stats
    print("Phase 2: Gear Data (Items + Itemstats)")
    print("-" * 60)
    
    # Itemstats
    print("\n[4/6] Fetching itemstats...")
    itemstats = fetch_json("itemstats")
    save_cache(itemstats, "itemstats.json")
    
    # Items - need to be selective to avoid fetching everything
    # Focus on: armor, weapons, runes, sigils, relics, trinkets
    print("\n[5/6] Fetching items (this may take a while)...")
    
    # Get item IDs by categories
    item_categories = [
        # Armor
        ("Heavy", range(48000, 49000)),
        ("Medium", range(49000, 50000)),
        ("Light", range(50000, 51000)),
        # Weapons  
        ("Weapons", range(30000, 31000)),
        # Runes
        ("Runes", range(24000, 25000)),
        ("Runes2", range(91000, 92000)),
        # Sigils
        ("Sigils", range(24000, 25000)),
        # Relics
        ("Relics", range(99000, 108000)),
        # Accessories/Amulets/Rings
        ("Trinkets", range(74000, 75000)),
        ("Trinkets2", range(81000, 82000)),
        ("Trinkets3", range(91000, 92000)),
    ]
    
    all_item_ids = []
    for name, range_ids in item_categories:
        all_item_ids.extend(range_ids)
    
    # Filter to only existing items (quick check)
    print(f"  Checking {len(all_item_ids)} potential item IDs...")
    valid_item_ids = []
    for i in range(0, len(all_item_ids), 200):
        batch = all_item_ids[i:i+200]
        ids_str = ",".join(map(str, batch))
        try:
            resp = requests.get(f"{API_BASE}/items?ids={ids_str}")
            if resp.status_code == 200:
                items = resp.json()
                valid_item_ids.extend([item["id"] for item in items if isinstance(item, dict)])
                print(f"  Progress: {i+200}/{len(all_item_ids)} (found {len(valid_item_ids)} so far)")
        except:
            pass
    
    print(f"  Found {len(valid_item_ids)} valid item IDs")
    print(f"  Fetching full item data...")
    items = fetch_json("items", valid_item_ids)
    save_cache(items, "items.json")
    
    print("\nPhase 2 complete! Gear data cached.")
    print()
    
    # Phase 3: Traits
    print("Phase 3: Traits")
    print("-" * 60)
    
    print("\n[6/6] Fetching traits...")
    traits = fetch_json("traits", list(trait_ids))
    save_cache(traits, "traits.json")
    
    print("\nPhase 3 complete! All data cached.")
    print()
    
    # Create consolidated cache
    print("Creating consolidated cache...")
    consolidated = {
        "gw2_build": build_id,
        "fetched_at": datetime.utcnow().isoformat() + "Z",
        "specializations": specs,
        "skills": skills,
        "itemstats": itemstats,
        "items": items,
        "traits": traits,
        "professions": professions,
        "stats": {
            "specializations": len(specs),
            "skills": len(skills),
            "itemstats": len(itemstats),
            "items": len(items),
            "traits": len(traits),
            "professions": len(professions),
        }
    }
    save_cache(consolidated, "gw2_api_data.json")
    
    print()
    print("=" * 60)
    print("✓ All data fetched successfully!")
    print("=" * 60)
    print()
    print("Cache files created in cache/ directory:")
    for f in CACHE_DIR.glob("*.json"):
        size = f.stat().st_size / 1024  # KB
        print(f"  - {f.name} ({size:.1f} KB)")
    print()
    print("Next steps:")
    print("1. Copy cache/ folder to Accessibuilds addon directory")
    print("2. Modify gw2names.cpp to load from cache on startup")
    print("3. Test offline functionality")

if __name__ == "__main__":
    main()
