# GW2 Data Requirements for Accessibuilds

## Current Cached Data (Offline Available)

### 1. Share Code Dictionaries (`dict_tables.h`)
- ✅ **Stat IDs** (191 entries) - Berserker's, Celestial, etc.
- ✅ **Relic IDs** (206 entries) - All relics including legendary
- ✅ **Upgrade IDs** (361 entries) - Runes and sigils
- ✅ **Food IDs** (83 entries) - All food buffs
- ✅ **Utility IDs** (77 entries) - All utility buffs

### 2. Build Cache (`cache/`)
- ✅ Snow Crows reference builds (sc_builds_*.json)
- ✅ Accessibility rotation data (sc_rotations_accessibility.json)
- ✅ User-created builds (user_builds.json)

## Data Currently Fetched from GW2 API (Needs Caching)

### 1. Specializations (`/v2/specializations`)
- **What**: Spec names, icons, major_traits[9], minor_traits[3]
- **Usage**: Trait line dropdowns, trait resolution, build comparison
- **Size**: ~40 specializations across all professions
- **Priority**: 🔴 CRITICAL - needed for trait UI and comparison

### 2. Skills (`/v2/skills`)
- **What**: Skill names, icons, descriptions, professions
- **Usage**: Skill bar display, skill name resolution, comparison
- **Size**: ~300-400 skills (varies by profession)
- **Priority**: 🔴 CRITICAL - needed for skill UI and comparison

### 3. Item Stats (`/v2/itemstats`)
- **What**: Stat set names (e.g. "Berserker's") and attribute mappings
- **Usage**: Stat name display, stat ID resolution
- **Size**: ~200 stat combinations
- **Priority**: 🟡 HIGH - already has partial cache via GW2Names

### 4. Items (`/v2/items`)
- **What**: Item names, icons, types, default stat IDs
- **Usage**: Gear icon display, weapon type detection, fixed-stat item resolution
- **Size**: ~2000 items (armor, weapons, upgrades, trinkets)
- **Priority**: 🟡 HIGH - needed for gear icons and names

### 5. Professions (`/v2/professions`)
- **What**: Skill palettes, training panels, weapon/masteries
- **Usage**: Skill dropdown filtering, weapon availability
- **Size**: 9 professions
- **Priority**: 🟠 MEDIUM - needed for skill palette mapping

### 6. Traits (`/v2/traits`)
- **What**: Trait names, icons, descriptions
- **Usage**: Trait name display in comparison panels
- **Size**: ~300 traits
- **Priority**: 🟠 MEDIUM - used for trait name display

## Recommended Cache Strategy

### Phase 1: Essential for Build Editor/Comparator (Before API Down)
```bash
# Fetch these immediately - core functionality depends on them
/v2/specializations?ids=all
/v2/skills?ids=<all skill IDs from palettes>
/v2/itemstats?ids=all
/v2/items?ids=<armor, weapons, upgrades, trinkets>
```

### Phase 2: Enhanced UI/UX
```bash
# Fetch these for better name resolution and tooltips
/v2/traits?ids=<all trait IDs from specs>
/v2/professions?ids=all
```

### Phase 3: Optional/Nice-to-Have
```bash
# Can be fetched on-demand or skipped
/v2/materials (not used)
/v2/minis (not used)
/v2/skins (not used)
```

## Cache File Format Recommendation

```json
{
  "gw2_build": 158823,  // Game build number for cache invalidation
  "fetched_at": "2025-05-10T12:00:00Z",
  "specializations": [...],
  "skills": [...],
  "itemstats": [...],
  "items": [...],
  "traits": [...],
  "professions": [...]
}
```

## Storage Location
`cache/gw2_api_data.json` - Single consolidated cache file
- keyed by game build number (auto-invalidates on patch)
- ~2-5 MB total when complete
- Load once at addon startup

## Implementation Priority
1. ✅ Dict tables (already done - share code dictionaries)
2. 🔴 Specializations + Skills (most critical for editor)
3. 🟡 Items + Itemstats (gear display and resolution)
4. 🟠 Traits + Professions (enhanced tooltips/filtering)

