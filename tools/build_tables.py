#!/usr/bin/env python3 -u
"""Build dictionary tables — Superior runes/sigils, exotic+ food, all lv80 utility."""

import json, urllib.request, sys, time
from urllib.request import urlopen, Request

API = "https://api.guildwars2.com/v2"

def fetch(url):
    req = Request(url, headers={"User-Agent": "Accessibuilds/1.0"})
    for attempt in range(3):
        try:
            with urlopen(req, timeout=20) as r:
                return json.load(r)
        except Exception as e:
            if attempt == 2:
                return None
            time.sleep(1)

print("Fetching stats + item IDs...", flush=True)
stat_ids = fetch(f"{API}/itemstats")
stat_table = sorted(stat_ids)
all_ids = fetch(f"{API}/items")
print(f"  stats={len(stat_table)} items={len(all_ids)}", flush=True)

# Scan
relic_ids, rune_list, sigil_list = set(), [], []
food_ids, util_list = set(), []
BATCH = 200

for start in range(0, len(all_ids), BATCH):
    batch = all_ids[start:start + BATCH]
    items = fetch(f"{API}/items?ids=" + ",".join(str(i) for i in batch))
    if items is None: continue
    for item in items:
        t, name = item.get('type'), item.get('name', '')
        if t == 'Relic': relic_ids.add(item['id'])
        elif t == 'UpgradeComponent':
            dt = item.get('details', {}).get('type')
            if dt == 'Rune' and name.startswith('Superior'):
                rune_list.append((item['id'], name))
            elif dt == 'Sigil' and name.startswith('Superior'):
                sigil_list.append((item['id'], name))
        elif t == 'Consumable':
            dt = item.get('details', {}).get('type')
            lvl, rarity = item.get('level', 0), item.get('rarity', '')
            if dt == 'Food' and lvl == 80 and rarity in ('Exotic', 'Ascended'):
                food_ids.add(item['id'])
            if dt == 'Utility' and lvl == 80:
                util_list.append((item['id'], name))

rune_ids = sorted(rid for rid, _ in rune_list)
sigil_ids = sorted(rid for rid, _ in sigil_list)
util_ids = sorted(uid for uid, _ in util_list)

print(f"\nResults:", flush=True)
print(f"  Stats:    {len(stat_table)} ({len(stat_table).bit_length()}b)", flush=True)
print(f"  Relics:   {len(relic_ids)} ({max(1,len(relic_ids)).bit_length()}b)", flush=True)
print(f"  Runes:    {len(rune_ids)} ({max(1,len(rune_ids)).bit_length()}b)", flush=True)
print(f"  Sigils:   {len(sigil_ids)} ({max(1,len(sigil_ids)).bit_length()}b)", flush=True)
print(f"  Food:     {len(food_ids)} ({max(1,len(food_ids)).bit_length()}b)", flush=True)
print(f"  Utility:  {len(util_ids)} ({max(1,len(util_ids)).bit_length()}b)", flush=True)

print(f"\nSample runes:", flush=True)
for rid, name in rune_list[:5]: print(f"  {rid}: {name}", flush=True)
print(f"\nSample sigils:", flush=True)
for sid, name in sigil_list[:5]: print(f"  {sid}: {name}", flush=True)
print(f"\nSample utility:", flush=True)
for uid, name in util_list[:5]: print(f"  {uid}: {name}", flush=True)

tables = {
    "stats":   stat_table,
    "relics":  sorted(relic_ids),
    "runes":   rune_ids,
    "sigils":  sigil_ids,
    "food":    sorted(food_ids),
    "utility": util_ids,
}
with open("/home/todd/gw2-build-coaches/Accessibuilds/tools/dict_tables.json", "w") as f:
    json.dump(tables, f, indent=1)
print(f"\nSaved tools/dict_tables.json", flush=True)
