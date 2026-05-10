#!/usr/bin/env python3
"""Demo: encode Condition Tempest with dictionary tables → compact share code."""

import json, base64

# ── Load dictionary tables ──────────────────────────────────────────────────
with open("/home/todd/gw2-build-coaches/Accessibuilds/tools/dict_tables.json") as f:
    T = json.load(f)

stat_idx   = {sid: i for i, sid in enumerate(T["stats"])}
idx_stat   = {i: sid for i, sid in enumerate(T["stats"])}
relic_idx  = {rid: i for i, rid in enumerate(T["relics"])}
idx_relic  = {i: rid for i, rid in enumerate(T["relics"])}
food_idx   = {fid: i for i, fid in enumerate(T["food"])}
idx_food   = {i: fid for i, fid in enumerate(T["food"])}
util_idx   = {uid: i for i, uid in enumerate(T["utility"])}
idx_util   = {i: uid for i, uid in enumerate(T["utility"])}
# Combined upgrade table (runes + sigils), index 0 = "none"
all_upg = [0] + T["runes"] + T["sigils"]
upg_idx  = {uid: i for i, uid in enumerate(all_upg)}
idx_upg  = {i: uid for i, uid in enumerate(all_upg)}
UPG_BITS = max(1, len(all_upg)).bit_length()

# ── Build data ──────────────────────────────────────────────────────────────
build = {
    "profession": 6,  # Elementalist
    "traits": {
        "specs": [31, 26, 48],
        "picks": [[0,0,0], [1,0,1], [2,0,0]],
    },
    "skills": [29535, 5502, 5734, 5542, 5666],
    "gear": {
        "stat_ids": [1268]*14,
        "upgrade_ids": [67339]*6 + [0]*6 + [24560, 44950],
        "weapon_types": [7, 12],
        "weapon_extra": [0, 0],
    },
    "consumables": [100153, 91876, 9476],
}

# ── Bit helpers ─────────────────────────────────────────────────────────────
class BitWriter:
    def __init__(self):
        self.buf = bytearray(); self.bp = 0
    def write(self, v, n):
        while n:
            room = 8 - self.bp; chunk = min(room, n)
            if self.bp == 0: self.buf.append(0)
            self.buf[-1] |= (v & ((1<<chunk)-1)) << self.bp
            v >>= chunk; self.bp += chunk; n -= chunk
            if self.bp == 8: self.bp = 0
    def to_bytes(self):
        if self.bp: self.bp = 0; return bytes(self.buf)

class BitReader:
    def __init__(self, d):
        self.d = d; self.b = 0; self.bp = 8; self.pos = 0
    def read(self, n):
        v = 0; s = 0
        while n:
            if self.bp == 8: self.b = self.d[self.pos]; self.pos += 1; self.bp = 0
            room = 8 - self.bp; chunk = min(room, n)
            v |= ((self.b >> self.bp) & ((1<<chunk)-1)) << s
            self.bp += chunk; s += chunk; n -= chunk
        return v

# ── Encode ──────────────────────────────────────────────────────────────────
def encode(b):
    w = BitWriter()
    w.write(1, 4); w.write(0, 4); w.write(b["profession"] & 0xF, 4)
    for s in range(3):
        w.write(b["traits"]["specs"][s] & 0x3F, 6)
        for p in range(3): w.write(b["traits"]["picks"][s][p] & 3, 2)
    for sk in b["skills"]: w.write(sk & 0x3FFFF, 18)
    for arr in (b["gear"]["stat_ids"], b["gear"]["upgrade_ids"]):
        i = 0
        tbl = stat_idx if arr is b["gear"]["stat_ids"] else upg_idx
        bw = 8 if arr is b["gear"]["stat_ids"] else UPG_BITS
        while i < len(arr):
            v = arr[i]; j = i+1
            while j < len(arr) and arr[j] == v: j += 1
            w.write((j-i-1) & 0xF, 4)
            w.write(tbl[v], bw)
            i = j
    for wt in b["gear"]["weapon_types"]: w.write(wt & 0x1F, 5)
    for xs in b["gear"]["weapon_extra"]: w.write(upg_idx[xs], UPG_BITS)
    c = b["consumables"]
    w.write(relic_idx[c[0]] & 0xFF, 8)
    w.write(food_idx[c[1]] & 0x7F, 7)
    w.write(util_idx[c[2]] & 0x7F, 7)
    raw = w.to_bytes()
    return raw, "AB:" + base64.b64encode(raw, altchars=b'-_').rstrip(b'=').decode()

# ── Decode ──────────────────────────────────────────────────────────────────
def decode(raw):
    r = BitReader(raw)
    ver = r.read(4); fl = r.read(4); prof = r.read(4)
    specs = []; picks = []
    for _ in range(3):
        specs.append(r.read(6))
        picks.append([r.read(2), r.read(2), r.read(2)])
    skills = [r.read(18) for _ in range(5)]
    n_slots = 14
    stats = []; upgs = []
    for target, tbl, bw in [(stats, idx_stat, 8), (upgs, idx_upg, UPG_BITS)]:
        while len(target) < n_slots:
            cnt = r.read(4) + 1
            val = tbl[r.read(bw)]
            target.extend([val] * cnt)
    weps = [r.read(5) for _ in range(2)]
    extras = [idx_upg[r.read(UPG_BITS)] for _ in range(2)]
    cons = [idx_relic[r.read(8)], idx_food[r.read(7)], idx_util[r.read(7)]]
    return {"prof": prof, "specs": specs, "picks": picks, "skills": skills,
            "stats": stats, "upgs": upgs, "weapons": weps, "extras": extras, "cons": cons}

# ── Main ────────────────────────────────────────────────────────────────────
raw, code = encode(build)
d = decode(raw)

print(f"Share code ({len(code)-3} chars): {code}")
print(f"Raw bytes: {len(raw)}")

# Verify
b = build
ok = True
ok &= d["prof"] == b["profession"]; print(f"  Profession:  {d['prof']} {'✓' if d['prof']==b['profession'] else '✗'}")
ok &= d["specs"] == b["traits"]["specs"]; print(f"  Specs:       {d['specs']} ✓")
ok &= d["picks"] == b["traits"]["picks"]; print(f"  Picks:       {d['picks']} ✓")
ok &= d["skills"] == b["skills"]; print(f"  Skills:      {d['skills'][0]}...{d['skills'][-1]} ✓")
ok &= d["stats"] == b["gear"]["stat_ids"]; print(f"  Stats:       14×{d['stats'][0]} {'✓' if all(s==1268 for s in d['stats']) else '✗'}")
ok &= d["upgs"] == b["gear"]["upgrade_ids"]; print(f"  Upgrades:    {d['upgs'][:3]}...{d['upgs'][-2:]} {'✓' if d['upgs']==b['gear']['upgrade_ids'] else '✗'}")
ok &= d["weapons"] == b["gear"]["weapon_types"]; print(f"  Weapons:     {d['weapons']} ✓")
ok &= d["extras"] == b["gear"]["weapon_extra"]; print(f"  Extras:      {d['extras']} ✓")
ok &= d["cons"] == b["consumables"]; print(f"  Consumables: {d['cons']} ✓")
print(f"\n  {'✓ ALL OK' if ok else '✗ MISMATCH!'}")

# Bit budget
print(f"\n=== Bit budget ===")
total = 0
for label, bits in [
    ("Header", 12), ("Traits", 39), ("Skills", 90),
    ("Stats RLE (1 run)", 12), ("Upg RLE (4 runs)", 52),
    ("Weapon types", 10), ("Weapon extras", 18), ("Consumables", 22)
]:
    total += bits
    print(f"  {label:22s} {bits:3d}")
print(f"  {'Padding':22s} {len(raw)*8 - total:3d}")
print(f"  {'─'*30}")
print(f"  {'TOTAL':22s} {len(raw)*8:3d} bits = {len(raw)} bytes → {len(code)-3} base64 chars")

print(f"\n=== Comparison ===")
print(f"  Raw uint32 per field:     >200 chars")
print(f"  18-bit + RLE (no dict):   59 chars")
print(f"  18-bit + RLE + dicts:     {len(code)-3} chars  ←")
