#include "build_editor.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../build/cache.h"
#include "../api/item_lookup.h"
#include "../api/gw2names.h"
#include <imgui.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include <chrono>

namespace BuildEditor {

static bool s_visible = false;
static std::vector<GW2::SCBuild> s_builds;
static int  s_list_idx = -1;

/* ── Resolution state for gear fields ── */
struct ResolveStatus {
    bool        ok   = false;
    uint32_t    id   = 0;
    std::string hint;
};

/* ── Metadata form ── */
static char   s_name_buf [128] = {};
static char   s_notes_buf[512] = {};
static int    s_prof_idx  = 0; /* 0=None … 9=Revenant, matches GW2::Profession */
static int    s_spec_idx  = 0; /* index into SPECS[] for the elite spec field */
static int    s_type_idx  = 0;
static int    s_prev_spec_idx = 0; /* detect elite-spec combo changes */
static double s_benchmark_dps = 0.0;

/* ── Traits ── */
static uint32_t s_spec_id_val [3]      = {};  /* raw GW2 API spec IDs per line */
static uint32_t s_raw_trait_id[3][3]   = {};  /* raw trait IDs (for save/load) */
static int      s_trait_sel   [3][3]   = {{-1,-1,-1},{-1,-1,-1},{-1,-1,-1}};
/* s_trait_sel[line][tier] = 0/1/2 choice within that tier's 3 major traits, -1=none */

/* ── Skills ── */
static char s_skill_heal_name   [128]    = {};
static char s_skill_util_name [3][128]   = {};
static char s_skill_elite_name  [128]    = {};

/* ── Gear ── */
struct SlotRow { GW2::GearSlot slot; const char* label; bool is_weapon; };
static const SlotRow SLOT_ROWS[] = {
    {GW2::GearSlot::Helm,       "Helm",       false},
    {GW2::GearSlot::Shoulders,  "Shoulders",  false},
    {GW2::GearSlot::Chest,      "Chest",      false},
    {GW2::GearSlot::Gloves,     "Gloves",     false},
    {GW2::GearSlot::Leggings,   "Leggings",   false},
    {GW2::GearSlot::Boots,      "Boots",      false},
    {GW2::GearSlot::BackItem,   "Back",       false},
    {GW2::GearSlot::Accessory1, "Accessory 1",false},
    {GW2::GearSlot::Accessory2, "Accessory 2",false},
    {GW2::GearSlot::Amulet,     "Amulet",     false},
    {GW2::GearSlot::Ring1,      "Ring 1",     false},
    {GW2::GearSlot::Ring2,      "Ring 2",     false},
    {GW2::GearSlot::WeaponA1,   "Weapon A1",  true},
    {GW2::GearSlot::WeaponA2,   "Weapon A2",  true},
    {GW2::GearSlot::WeaponB1,   "Weapon B1",  true},
    {GW2::GearSlot::WeaponB2,   "Weapon B2",  true},
};
static constexpr int NUM_SLOTS = 16;

static char         s_stat_name   [NUM_SLOTS][64]  = {};
static char         s_upgrade_name[NUM_SLOTS][128] = {};
static ResolveStatus s_stat_resolve   [NUM_SLOTS]  = {};
static ResolveStatus s_upgrade_resolve[NUM_SLOTS]  = {};
static int          s_weapon_type[NUM_SLOTS]       = {};
static char         s_upgrade_id_override[NUM_SLOTS][12] = {};
static char         s_relic_name  [128] = {};
static char         s_food_name   [128] = {};
static char         s_utility_name[128] = {};
static char         s_relic_id_override  [12] = {};
static char         s_food_id_override   [12] = {};
static char         s_utility_id_override[12] = {};

/* Save flash */
static std::chrono::steady_clock::time_point s_save_time{};
static bool s_save_ok = false;

/* ── Static tables ── */
static const char* PROF_NAMES[] = {
    "None","Guardian","Warrior","Engineer","Ranger",
    "Thief","Elementalist","Mesmer","Necromancer","Revenant"
};

struct SpecEntry { const char* name; GW2::EliteSpec spec; };
static const SpecEntry SPECS[] = {
    {"None",         GW2::EliteSpec::None},
    {"Dragonhunter", GW2::EliteSpec::Dragonhunter},
    {"Firebrand",    GW2::EliteSpec::Firebrand},
    {"Willbender",   GW2::EliteSpec::Willbender},
    {"Luminary",     GW2::EliteSpec::Luminary},
    {"Berserker",    GW2::EliteSpec::Berserker},
    {"Spellbreaker", GW2::EliteSpec::Spellbreaker},
    {"Bladesworn",   GW2::EliteSpec::Bladesworn},
    {"Paragon",      GW2::EliteSpec::Paragon},
    {"Scrapper",     GW2::EliteSpec::Scrapper},
    {"Holosmith",    GW2::EliteSpec::Holosmith},
    {"Mechanist",    GW2::EliteSpec::Mechanist},
    {"Amalgam",      GW2::EliteSpec::Amalgam},
    {"Druid",        GW2::EliteSpec::Druid},
    {"Soulbeast",    GW2::EliteSpec::Soulbeast},
    {"Untamed",      GW2::EliteSpec::Untamed},
    {"Galeshot",     GW2::EliteSpec::Galeshot},
    {"Daredevil",    GW2::EliteSpec::Daredevil},
    {"Deadeye",      GW2::EliteSpec::Deadeye},
    {"Specter",      GW2::EliteSpec::Specter},
    {"Antiquary",    GW2::EliteSpec::Antiquary},
    {"Tempest",      GW2::EliteSpec::Tempest},
    {"Weaver",       GW2::EliteSpec::Weaver},
    {"Catalyst",     GW2::EliteSpec::Catalyst},
    {"Evoker",       GW2::EliteSpec::Evoker},
    {"Chronomancer", GW2::EliteSpec::Chronomancer},
    {"Mirage",       GW2::EliteSpec::Mirage},
    {"Virtuoso",     GW2::EliteSpec::Virtuoso},
    {"Troubadour",   GW2::EliteSpec::Troubadour},
    {"Reaper",       GW2::EliteSpec::Reaper},
    {"Scourge",      GW2::EliteSpec::Scourge},
    {"Harbinger",    GW2::EliteSpec::Harbinger},
    {"Ritualist",    GW2::EliteSpec::Ritualist},
    {"Herald",       GW2::EliteSpec::Herald},
    {"Renegade",     GW2::EliteSpec::Renegade},
    {"Vindicator",   GW2::EliteSpec::Vindicator},
    {"Conduit",      GW2::EliteSpec::Conduit},
};
static constexpr int NUM_SPECS = 37;

static const char* TYPE_NAMES[] = {
    "Unknown","Power","Condi","Support","Heal","Quickness","Alacrity"
};

static const char* WEAPON_TYPE_NAMES[] = {
    "—","Sword","Greatsword","Hammer","Mace","Axe","Dagger",
    "Scepter","Staff","Torch","Focus","Shield","Warhorn",
    "Shortbow","Longbow","Rifle","Pistol","Spear","Speargun","Trident",
};
static constexpr int NUM_WEAPON_TYPES = 20;

/* ── Helpers ── */

static bool IsAllDigits(const char* s)
{
    if (!s || !s[0]) return false;
    for (const char* p = s; *p; p++) if (*p<'0'||*p>'9') return false;
    return true;
}

static ResolveStatus ResolveStat(const char* f)
{
    ResolveStatus r;
    if (!f||!f[0]) return r;
    if (IsAllDigits(f)) { r.id=(uint32_t)atoi(f); r.ok=(r.id>0); r.hint=r.ok?"(ID)":""; return r; }
    r.id=ItemLookup::FindStatID(f);
    if (r.id) { r.ok=true; r.hint="resolved"; }
    else if (!ItemLookup::StatNamesLoaded()) r.hint="loading...";
    else r.hint="not found";
    return r;
}

static ResolveStatus ResolveUpgrade(const char* f, const char* ov)
{
    ResolveStatus r;
    if (!f||!f[0]) return r;
    if (IsAllDigits(f)) { r.id=(uint32_t)atoi(f); r.ok=(r.id>0); r.hint=r.ok?"(ID)":""; return r; }
    r.id=ItemLookup::FindItemID(f);
    if (r.id) { r.ok=true; r.hint="resolved"; return r; }
    if (ov&&ov[0]&&IsAllDigits(ov)) {
        r.id=(uint32_t)atoi(ov);
        if (r.id) { ItemLookup::CacheItemName(f,r.id); r.ok=true; r.hint="cached"; }
    } else { r.hint="enter ID to cache"; }
    return r;
}

/* Find SpecData pointer in loaded spec list for a given ID. */
static const ItemLookup::SpecData* FindSpec(uint32_t id)
{
    if (!id || !ItemLookup::SpecDataLoaded()) return nullptr;
    for (const auto& sd : ItemLookup::GetSpecData())
        if (sd.id == id) return &sd;
    return nullptr;
}

static void ClearForm()
{
    memset(s_name_buf,           0, sizeof(s_name_buf));
    memset(s_notes_buf,          0, sizeof(s_notes_buf));
    s_prof_idx=0; s_spec_idx=0; s_type_idx=0; s_prev_spec_idx=0; s_benchmark_dps=0.0;

    memset(s_spec_id_val,        0, sizeof(s_spec_id_val));
    memset(s_raw_trait_id,       0, sizeof(s_raw_trait_id));
    for (int i=0;i<3;i++) s_trait_sel[i][0]=s_trait_sel[i][1]=s_trait_sel[i][2]=-1;

    memset(s_skill_heal_name,    0, sizeof(s_skill_heal_name));
    memset(s_skill_util_name,    0, sizeof(s_skill_util_name));
    memset(s_skill_elite_name,   0, sizeof(s_skill_elite_name));

    memset(s_stat_name,          0, sizeof(s_stat_name));
    memset(s_upgrade_name,       0, sizeof(s_upgrade_name));
    for (auto& r : s_stat_resolve)    r={};
    for (auto& r : s_upgrade_resolve) r={};
    memset(s_weapon_type,        0, sizeof(s_weapon_type));
    memset(s_upgrade_id_override,0, sizeof(s_upgrade_id_override));
    memset(s_relic_name,         0, sizeof(s_relic_name));
    memset(s_food_name,          0, sizeof(s_food_name));
    memset(s_utility_name,       0, sizeof(s_utility_name));
    memset(s_relic_id_override,  0, sizeof(s_relic_id_override));
    memset(s_food_id_override,   0, sizeof(s_food_id_override));
    memset(s_utility_id_override,0, sizeof(s_utility_id_override));
}

/* Resolve skill name → ID, accepting bare numeric IDs too. */
static uint32_t ResolveSkillName(const char* name)
{
    if (!name||!name[0]) return 0;
    if (IsAllDigits(name)) return (uint32_t)atoi(name);
    return ItemLookup::FindSkillID(name, (uint8_t)s_prof_idx);
}

/* Reverse-lookup skill ID → name from the loaded skill cache. */
static std::string SkillIDToName(uint32_t id, uint8_t prof)
{
    if (!id) return {};
    auto skills = ItemLookup::GetSkillsForProfession(prof);
    for (const auto& se : skills) if (se.id==id) return se.name;
    /* Fall back to GW2Names ID→name (lazy async). */
    const std::string& n = GW2Names::GetSkill(id);
    if (!n.empty() && n!="...") return n;
    return std::to_string(id);
}

static void BuildToForm(const GW2::SCBuild& b)
{
    ClearForm();
    strncpy(s_name_buf,  b.name.c_str(),  sizeof(s_name_buf)-1);
    strncpy(s_notes_buf, b.notes.c_str(), sizeof(s_notes_buf)-1);

    s_prof_idx=(int)b.profession;
    s_spec_idx=0;
    for (int i=0;i<NUM_SPECS;i++) if (SPECS[i].spec==b.elite_spec) { s_spec_idx=i; break; }
    s_prev_spec_idx=s_spec_idx;
    s_type_idx=(int)b.build_type;
    s_benchmark_dps=b.benchmark_dps;

    /* Traits — store raw IDs, dropdowns populated lazily in Render when spec data loads */
    for (int i=0;i<3;i++) {
        s_spec_id_val[i]=b.traits.lines[i].spec_id;
        for (int t=0;t<3;t++) s_raw_trait_id[i][t]=b.traits.lines[i].traits[t].trait_id;
        s_trait_sel[i][0]=s_trait_sel[i][1]=s_trait_sel[i][2]=-1;
    }

    /* Skills */
    uint8_t prof=(uint8_t)b.profession;
    if (b.skills.heal) { std::string n=SkillIDToName(b.skills.heal,prof); strncpy(s_skill_heal_name, n.c_str(),127); }
    for (int i=0;i<3;i++) if (b.skills.utilities[i]) {
        std::string n=SkillIDToName(b.skills.utilities[i],prof);
        strncpy(s_skill_util_name[i],n.c_str(),127);
    }
    if (b.skills.elite) { std::string n=SkillIDToName(b.skills.elite,prof); strncpy(s_skill_elite_name, n.c_str(),127); }

    /* Gear */
    for (const auto& gi : b.gear.items) {
        for (int r=0;r<NUM_SLOTS;r++) {
            if (SLOT_ROWS[r].slot!=gi.slot) continue;
            if (gi.stat_id) snprintf(s_stat_name[r],sizeof(s_stat_name[r]),"%u",gi.stat_id);
            if (gi.upgrade_id) {
                std::string cached;
                for (const auto& e : ItemLookup::GetItemNames())
                    if (ItemLookup::FindItemID(e)==gi.upgrade_id) { cached=e; break; }
                if (!cached.empty()) strncpy(s_upgrade_name[r],cached.c_str(),sizeof(s_upgrade_name[r])-1);
                else snprintf(s_upgrade_name[r],sizeof(s_upgrade_name[r]),"%u",gi.upgrade_id);
            } else if (!gi.upgrade_name.empty()) {
                strncpy(s_upgrade_name[r],gi.upgrade_name.c_str(),sizeof(s_upgrade_name[r])-1);
            }
            s_weapon_type[r]=(int)gi.weapon_type;
            break;
        }
    }
    auto fmtC=[](char* d,size_t sz,uint32_t id){
        if (!id) return;
        std::string cached;
        for (const auto& e : ItemLookup::GetItemNames())
            if (ItemLookup::FindItemID(e)==id) { cached=e; break; }
        if (!cached.empty()) strncpy(d,cached.c_str(),sz-1);
        else snprintf(d,sz,"%u",id);
    };
    fmtC(s_relic_name,   sizeof(s_relic_name),   b.gear.relic_id);
    fmtC(s_food_name,    sizeof(s_food_name),     b.gear.food_id);
    fmtC(s_utility_name, sizeof(s_utility_name),  b.gear.utility_id);
}

static void ResolveAll()
{
    for (int r=0;r<NUM_SLOTS;r++) {
        s_stat_resolve[r]    = ResolveStat(s_stat_name[r]);
        s_upgrade_resolve[r] = ResolveUpgrade(s_upgrade_name[r], s_upgrade_id_override[r]);
    }
}

static GW2::SCBuild FormToBuild()
{
    GW2::SCBuild b;
    b.name=s_name_buf; b.notes=s_notes_buf;
    b.profession=(GW2::Profession)s_prof_idx;
    b.elite_spec=SPECS[s_spec_idx].spec;
    b.build_type=(GW2::BuildType)s_type_idx;
    b.benchmark_dps=s_benchmark_dps;
    b.id=b.name;
    for (char& c:b.id) c=(c==' ')?'-':(char)tolower((unsigned char)c);

    /* Traits */
    for (int i=0;i<3;i++) {
        b.traits.lines[i].spec_id=s_spec_id_val[i];
        if (!s_spec_id_val[i]) continue;
        const auto* sd=FindSpec(s_spec_id_val[i]);
        for (int t=0;t<3;t++) {
            int ch=s_trait_sel[i][t];
            if (sd && ch>=0 && ch<3)
                b.traits.lines[i].traits[t].trait_id=sd->major_traits[t*3+ch];
            else if (s_raw_trait_id[i][t])
                b.traits.lines[i].traits[t].trait_id=s_raw_trait_id[i][t];
        }
    }

    /* Skills */
    b.skills.heal=ResolveSkillName(s_skill_heal_name);
    for (int i=0;i<3;i++) b.skills.utilities[i]=ResolveSkillName(s_skill_util_name[i]);
    b.skills.elite=ResolveSkillName(s_skill_elite_name);

    /* Gear */
    for (int r=0;r<NUM_SLOTS;r++) {
        auto& sr=s_stat_resolve[r]; auto& ur=s_upgrade_resolve[r]; int wt=s_weapon_type[r];
        if (!sr.id && !ur.id && !wt && !s_upgrade_name[r][0]) continue;
        GW2::GearItem gi;
        gi.slot=SLOT_ROWS[r].slot; gi.stat_id=sr.id;
        gi.upgrade_id=ur.id; gi.weapon_type=(GW2::WeaponType)wt;
        if (s_upgrade_name[r][0]) gi.upgrade_name = s_upgrade_name[r];
        b.gear.items.push_back(gi);
    }

    auto resC=[](const char* name, const char* ov)->uint32_t{
        if (!name||!name[0]) return 0;
        if (IsAllDigits(name)) return (uint32_t)atoi(name);
        uint32_t id=ItemLookup::FindItemID(name);
        if (!id&&ov&&IsAllDigits(ov)) { id=(uint32_t)atoi(ov); if (id) ItemLookup::CacheItemName(name,id); }
        return id;
    };
    b.gear.relic_id  =resC(s_relic_name,   s_relic_id_override);
    b.gear.food_id   =resC(s_food_name,    s_food_id_override);
    b.gear.utility_id=resC(s_utility_name, s_utility_id_override);
    return b;
}

static void RenderHints(const char* partial, bool is_stat)
{
    if (!partial||!partial[0]) return;
    std::vector<std::string> names=is_stat?ItemLookup::GetStatNames():ItemLookup::GetItemNames();
    std::string lc=partial; for (char& c:lc) c=(char)tolower((unsigned char)c);
    int shown=0;
    for (const auto& n:names) {
        std::string nlc=n; for (char& c:nlc) c=(char)tolower((unsigned char)c);
        if (nlc.find(lc)!=std::string::npos) { ImGui::TextDisabled("  %s",n.c_str()); if (++shown>=5) break; }
    }
}

/* Render a skill name field with inline resolution feedback. */
static void SkillField(const char* label, char* buf, size_t sz, const char* wid)
{
    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(180));
    ImGui::InputText(wid, buf, sz);
    ImGui::SameLine(0,S(6));
    uint32_t id=ResolveSkillName(buf);
    if (id) {
        const std::string& nm=GW2Names::GetSkill(id);
        ImGui::TextColored(ImVec4(0.3f,1.f,0.3f,1.f), "✓ %s", (nm.empty()||nm=="...")?"...":nm.c_str());
    } else if (buf[0]) {
        if (s_prof_idx && !ItemLookup::SkillNamesLoaded((uint8_t)s_prof_idx))
            ImGui::TextDisabled("loading...");
        else
            ImGui::TextColored(ImVec4(1.f,0.45f,0.45f,1.f),"?");
    }
}

/* ── Public API ── */

void Init() { BuildCache::LoadUserBuilds(s_builds); }
void Toggle()    { s_visible=!s_visible; }
bool IsVisible() { return s_visible; }

const std::vector<GW2::SCBuild>& GetBuilds() { return s_builds; }

void UseAsReference(int idx)
{
    if (idx<0||idx>=(int)s_builds.size()) return;
    std::lock_guard<std::mutex> lk(g_SCBuildMutex);
    g_SCBuild=s_builds[idx]; g_SCBuildLoaded=true;
}

void Render()
{
    if (!s_visible) return;

    /* Kick off skill loading whenever profession is set */
    if (s_prof_idx>0) ItemLookup::LoadSkillsForProfession((uint8_t)s_prof_idx);

    /* Sync elite spec combo → L3 spec line */
    if (s_spec_idx != s_prev_spec_idx) {
        s_prev_spec_idx = s_spec_idx;
        if (s_spec_idx>0 && ItemLookup::SpecDataLoaded()) {
            uint32_t eid=(uint32_t)SPECS[s_spec_idx].spec;
            /* Only apply if this elite spec belongs to the current profession */
            for (const auto& sd : ItemLookup::GetSpecData()) {
                if (sd.id==eid && sd.profession==(uint8_t)s_prof_idx && sd.elite) {
                    if (s_spec_id_val[2]!=eid) {
                        s_spec_id_val[2]=eid;
                        s_trait_sel[2][0]=s_trait_sel[2][1]=s_trait_sel[2][2]=-1;
                        s_raw_trait_id[2][0]=s_raw_trait_id[2][1]=s_raw_trait_id[2][2]=0;
                    }
                    break;
                }
            }
        }
    }

    ImGui::SetNextWindowSize(ImVec2(S(860),S(920)),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(600),S(500)),ImVec2(FLT_MAX,FLT_MAX));
    if (!ImGui::Begin("ViP WvW - Build Editor",&s_visible)) { ImGui::End(); return; }

    /* ── Build list ── */
    ImGui::Text("Saved builds:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(260));
    const char* preview=(s_list_idx>=0&&s_list_idx<(int)s_builds.size())
                       ?s_builds[s_list_idx].name.c_str():"-- New Build --";
    if (ImGui::BeginCombo("##build_list",preview)) {
        if (ImGui::Selectable("-- New Build --",s_list_idx==-1)) { s_list_idx=-1; ClearForm(); }
        for (int i=0;i<(int)s_builds.size();i++) {
            bool sel=(s_list_idx==i);
            if (ImGui::Selectable(s_builds[i].name.c_str(),sel)) { s_list_idx=i; BuildToForm(s_builds[i]); }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Use as Reference")&&s_list_idx>=0&&s_list_idx<(int)s_builds.size()) {
        std::lock_guard<std::mutex> lk(g_SCBuildMutex);
        g_SCBuild=s_builds[s_list_idx]; g_SCBuildLoaded=true;
    }
    ImGui::Separator();

    /* ── Metadata ── */
    ImGui::Text("Build Name:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(200)); ImGui::InputText("##name",s_name_buf,sizeof(s_name_buf));
    ImGui::SameLine(0,S(12)); ImGui::Text("Profession:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(110));
    if (ImGui::BeginCombo("##prof",PROF_NAMES[s_prof_idx])) {
        for (int i=0;i<10;i++) if (ImGui::Selectable(PROF_NAMES[i],s_prof_idx==i)) {
            if (s_prof_idx!=i) { /* clear trait lines on profession change */
                memset(s_spec_id_val,0,sizeof(s_spec_id_val));
                memset(s_raw_trait_id,0,sizeof(s_raw_trait_id));
                for (int l=0;l<3;l++) s_trait_sel[l][0]=s_trait_sel[l][1]=s_trait_sel[l][2]=-1;
                memset(s_skill_heal_name,0,sizeof(s_skill_heal_name));
                memset(s_skill_util_name,0,sizeof(s_skill_util_name));
                memset(s_skill_elite_name,0,sizeof(s_skill_elite_name));
            }
            s_prof_idx=i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(110));
    if (ImGui::BeginCombo("##spec",SPECS[s_spec_idx].name)) {
        for (int i=0;i<NUM_SPECS;i++) if (ImGui::Selectable(SPECS[i].name,s_spec_idx==i)) s_spec_idx=i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(95));
    if (ImGui::BeginCombo("##type",TYPE_NAMES[s_type_idx])) {
        for (int i=0;i<7;i++) if (ImGui::Selectable(TYPE_NAMES[i],s_type_idx==i)) s_type_idx=i;
        ImGui::EndCombo();
    }
    ImGui::Text("Notes:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(540)); ImGui::InputText("##notes",s_notes_buf,sizeof(s_notes_buf));
    ImGui::SameLine(0,S(16));
    ImGui::Text("DPS Benchmark:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(90));
    ImGui::InputDouble("##bench", &s_benchmark_dps, 0.0, 0.0, "%.0f");

    /* ── Traits ── */
    ImGui::Spacing();
    ImGui::TextDisabled("Traits");
    ImGui::Separator();

    bool spec_ok = ItemLookup::SpecDataLoaded();
    if (!spec_ok) {
        ImGui::TextColored(ImVec4(1.f,0.8f,0.2f,1.f),"Loading specialization data from GW2 API...");
    } else {
        const auto& all_specs = ItemLookup::GetSpecData();

        if (ImGui::BeginTable("##traits",5,ImGuiTableFlags_Borders|ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Line",  ImGuiTableColumnFlags_WidthFixed, S(36));
            ImGui::TableSetupColumn("Spec",  ImGuiTableColumnFlags_WidthFixed, S(140));
            ImGui::TableSetupColumn("Tier 1",ImGuiTableColumnFlags_WidthFixed, S(170));
            ImGui::TableSetupColumn("Tier 2",ImGuiTableColumnFlags_WidthFixed, S(170));
            ImGui::TableSetupColumn("Tier 3",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (int line=0;line<3;line++) {
                ImGui::TableNextRow();
                char lbl[32];

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("L%d",line+1);

                /* Spec dropdown */
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);
                snprintf(lbl,sizeof(lbl),"##sp%d",line);

                /* Find display name for current selection */
                std::string cur_spec_name="—";
                for (const auto& sd:all_specs)
                    if (sd.id==s_spec_id_val[line]&&sd.profession==(uint8_t)s_prof_idx)
                        { cur_spec_name=sd.name; break; }
                if (s_spec_id_val[line]&&cur_spec_name=="—") cur_spec_name="(other prof)";

                if (ImGui::BeginCombo(lbl,cur_spec_name.c_str())) {
                    if (ImGui::Selectable("—",s_spec_id_val[line]==0)) {
                        s_spec_id_val[line]=0;
                        s_trait_sel[line][0]=s_trait_sel[line][1]=s_trait_sel[line][2]=-1;
                        s_raw_trait_id[line][0]=s_raw_trait_id[line][1]=s_raw_trait_id[line][2]=0;
                    }
                    bool in_elite_section=false;
                    for (const auto& sd:all_specs) {
                        if (sd.profession!=(uint8_t)s_prof_idx) continue;
                        if (sd.elite && line<2) continue; /* no elite in L1/L2 */
                        if (sd.elite && !in_elite_section) {
                            ImGui::Separator();
                            ImGui::TextDisabled("  — Elite Specializations —");
                            in_elite_section=true;
                        }
                        bool is_sel=(s_spec_id_val[line]==sd.id);
                        if (ImGui::Selectable(sd.name.c_str(),is_sel)) {
                            if (s_spec_id_val[line]!=sd.id) {
                                s_trait_sel[line][0]=s_trait_sel[line][1]=s_trait_sel[line][2]=-1;
                                s_raw_trait_id[line][0]=s_raw_trait_id[line][1]=s_raw_trait_id[line][2]=0;
                            }
                            s_spec_id_val[line]=sd.id;
                            /* Sync elite spec → metadata dropdown */
                            if (line==2 && sd.elite) {
                                for (int i=0;i<NUM_SPECS;i++)
                                    if ((uint32_t)SPECS[i].spec==sd.id) { s_spec_idx=i; s_prev_spec_idx=i; break; }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                /* Trait tier dropdowns — only when spec is selected and has data */
                const ItemLookup::SpecData* sd=FindSpec(s_spec_id_val[line]);

                /* Lazily resolve s_trait_sel from s_raw_trait_id now that data is available */
                if (sd) {
                    for (int t=0;t<3;t++) {
                        if (s_raw_trait_id[line][t] && s_trait_sel[line][t]==-1) {
                            for (int c=0;c<3;c++) {
                                if (sd->major_traits[t*3+c]==s_raw_trait_id[line][t]) {
                                    s_trait_sel[line][t]=c; break;
                                }
                            }
                        }
                    }
                }

                for (int tier=0;tier<3;tier++) {
                    ImGui::TableSetColumnIndex(2+tier);
                    if (!sd) { ImGui::TextDisabled("—"); continue; }

                    ImGui::SetNextItemWidth(-1);
                    snprintf(lbl,sizeof(lbl),"##tr%d%d",line,tier);
                    int ch=s_trait_sel[line][tier];

                    /* Build preview string */
                    std::string cur_t="—";
                    if (ch>=0&&ch<3&&sd->major_traits[tier*3+ch]) {
                        const std::string& tn=GW2Names::GetTrait(sd->major_traits[tier*3+ch]);
                        cur_t=(tn.empty()||tn=="...")?std::to_string(sd->major_traits[tier*3+ch]):tn;
                    }

                    if (ImGui::BeginCombo(lbl,cur_t.c_str())) {
                        if (ImGui::Selectable("—",ch==-1)) {
                            s_trait_sel[line][tier]=-1; s_raw_trait_id[line][tier]=0;
                        }
                        for (int c=0;c<3;c++) {
                            uint32_t tid=sd->major_traits[tier*3+c];
                            if (!tid) continue;
                            const std::string& tn=GW2Names::GetTrait(tid);
                            std::string disp=(tn.empty()||tn=="...")?std::to_string(tid):tn;
                            bool sel=(ch==c);
                            if (ImGui::Selectable(disp.c_str(),sel)) {
                                s_trait_sel[line][tier]=c;
                                s_raw_trait_id[line][tier]=tid;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    /* ── Skills ── */
    ImGui::Spacing();
    if (s_prof_idx>0 && !ItemLookup::SkillNamesLoaded((uint8_t)s_prof_idx))
        ImGui::TextColored(ImVec4(1.f,0.8f,0.2f,1.f),"Loading skill names...");
    else
        ImGui::TextDisabled("Skills  (type name or numeric ID)");
    ImGui::Separator();

    if (ImGui::BeginTable("##skills",3,ImGuiTableFlags_Borders|ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Slot",    ImGuiTableColumnFlags_WidthFixed,   S(60));
        ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthFixed,   S(220));
        ImGui::TableSetupColumn("Resolved",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        auto SkillRow=[&](const char* slot, char* buf, size_t sz, const char* wid){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(slot);
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1); ImGui::InputText(wid,buf,sz);
            ImGui::TableSetColumnIndex(2);
            uint32_t id=ResolveSkillName(buf);
            if (id) {
                const std::string& nm=GW2Names::GetSkill(id);
                ImGui::TextColored(ImVec4(0.3f,1.f,0.3f,1.f),"✓ %u  %s",id,
                                   (nm.empty()||nm=="...")?"...":nm.c_str());
            } else if (buf[0]) {
                if (s_prof_idx&&!ItemLookup::SkillNamesLoaded((uint8_t)s_prof_idx))
                    ImGui::TextDisabled("loading...");
                else
                    ImGui::TextColored(ImVec4(1.f,0.45f,0.45f,1.f),"not found");
            }
        };
        SkillRow("Heal",   s_skill_heal_name,     sizeof(s_skill_heal_name),    "##sk_h");
        SkillRow("Util 1", s_skill_util_name[0],  sizeof(s_skill_util_name[0]), "##sk_u1");
        SkillRow("Util 2", s_skill_util_name[1],  sizeof(s_skill_util_name[1]), "##sk_u2");
        SkillRow("Util 3", s_skill_util_name[2],  sizeof(s_skill_util_name[2]), "##sk_u3");
        SkillRow("Elite",  s_skill_elite_name,    sizeof(s_skill_elite_name),   "##sk_el");
        ImGui::EndTable();
    }

    /* ── Gear ── */
    ImGui::Spacing();
    if (!ItemLookup::StatNamesLoaded())
        ImGui::TextColored(ImVec4(1.f,0.8f,0.2f,1.f),"Loading stat names...");
    else
        ImGui::TextDisabled("%d stat names.  Type e.g. \"Berserker's\" or a numeric ID.",
                            (int)ItemLookup::GetStatNames().size());
    ImGui::Separator();

    if (ImGui::BeginTable("##gear",6,
            ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,
            ImVec2(0,S(320)))) {
        ImGui::TableSetupColumn("Slot",        ImGuiTableColumnFlags_WidthFixed, S(90));
        ImGui::TableSetupColumn("Weapon Type", ImGuiTableColumnFlags_WidthFixed, S(100));
        ImGui::TableSetupColumn("Stat Name",   ImGuiTableColumnFlags_WidthFixed, S(140));
        ImGui::TableSetupColumn("",            ImGuiTableColumnFlags_WidthFixed, S(14));
        ImGui::TableSetupColumn("Upgrade",     ImGuiTableColumnFlags_WidthFixed, S(170));
        ImGui::TableSetupColumn("ID override", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int r=0;r<NUM_SLOTS;r++) {
            ImGui::TableNextRow();
            char lbl[32];

            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(SLOT_ROWS[r].label);

            ImGui::TableSetColumnIndex(1);
            if (SLOT_ROWS[r].is_weapon) {
                ImGui::SetNextItemWidth(-1);
                snprintf(lbl,sizeof(lbl),"##wt%d",r);
                int wt=s_weapon_type[r];
                if (ImGui::BeginCombo(lbl,WEAPON_TYPE_NAMES[wt])) {
                    for (int w=0;w<NUM_WEAPON_TYPES;w++)
                        if (ImGui::Selectable(WEAPON_TYPE_NAMES[w],wt==w)) s_weapon_type[r]=w;
                    ImGui::EndCombo();
                }
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1); snprintf(lbl,sizeof(lbl),"##st%d",r);
            if (ImGui::InputText(lbl,s_stat_name[r],sizeof(s_stat_name[r]))) s_stat_resolve[r]={};
            if (ImGui::IsItemHovered()&&s_stat_name[r][0]) {
                ImGui::BeginTooltip(); RenderHints(s_stat_name[r],true); ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(3);
            if (s_stat_name[r][0]) {
                if (s_stat_resolve[r].ok)            ImGui::TextColored(ImVec4(0.3f,1.f,0.3f,1.f),"●");
                else if (s_stat_resolve[r].hint.empty()) ImGui::TextDisabled("○");
                else                                 ImGui::TextColored(ImVec4(1.f,0.4f,0.4f,1.f),"●");
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-1); snprintf(lbl,sizeof(lbl),"##upg%d",r);
            if (ImGui::InputText(lbl,s_upgrade_name[r],sizeof(s_upgrade_name[r]))) s_upgrade_resolve[r]={};
            if (ImGui::IsItemHovered()&&s_upgrade_name[r][0]) {
                ImGui::BeginTooltip(); RenderHints(s_upgrade_name[r],false); ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(5);
            bool needs_id=s_upgrade_name[r][0]&&!s_upgrade_resolve[r].ok;
            if (needs_id) {
                ImGui::SetNextItemWidth(-1); snprintf(lbl,sizeof(lbl),"##uid%d",r);
                ImGui::InputText(lbl,s_upgrade_id_override[r],sizeof(s_upgrade_id_override[r]),ImGuiInputTextFlags_CharsDecimal);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enter GW2 item ID to cache this name");
            } else if (s_upgrade_resolve[r].ok) {
                ImGui::TextColored(ImVec4(0.3f,1.f,0.3f,1.f),"✓ %u",s_upgrade_resolve[r].id);
            }
        }
        ImGui::EndTable();
    }

    /* ── Consumables ── */
    ImGui::Spacing();
    auto ConsRow=[](const char* label,char* nb,size_t nsz,char* ib,size_t isz,const char* nl,const char* il){
        ImGui::Text("%s",label); ImGui::SameLine();
        ImGui::SetNextItemWidth(S(180)); ImGui::InputText(nl,nb,nsz);
        uint32_t res=0;
        if (nb[0]) { if (IsAllDigits(nb)) res=(uint32_t)atoi(nb); else res=ItemLookup::FindItemID(nb); }
        ImGui::SameLine();
        if (res) { ImGui::TextColored(ImVec4(0.3f,1.f,0.3f,1.f),"✓ %u",res); }
        else if (nb[0]) { ImGui::SetNextItemWidth(S(70)); ImGui::InputText(il,ib,isz,ImGuiInputTextFlags_CharsDecimal); ImGui::SameLine(); ImGui::TextDisabled("ID"); }
    };
    ConsRow("Relic:",   s_relic_name,   sizeof(s_relic_name),   s_relic_id_override,   sizeof(s_relic_id_override),   "##rn","##ri");
    ImGui::SameLine(0,S(16));
    ConsRow("Food:",    s_food_name,    sizeof(s_food_name),    s_food_id_override,    sizeof(s_food_id_override),    "##fn","##fi");
    ImGui::SameLine(0,S(16));
    ConsRow("Utility:", s_utility_name, sizeof(s_utility_name), s_utility_id_override, sizeof(s_utility_id_override), "##un","##ui");

    ImGui::Spacing(); ImGui::Separator();

    /* ── Buttons ── */
    bool name_empty=(s_name_buf[0]=='\0');
    if (name_empty) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,0.4f);
        ImGui::Button("Resolve & Save"); ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fill in a Build Name first");
    } else if (ImGui::Button("Resolve & Save")) {
        ResolveAll();
        GW2::SCBuild b=FormToBuild();
        if (s_list_idx>=0&&s_list_idx<(int)s_builds.size()) s_builds[s_list_idx]=b;
        else { s_builds.push_back(b); s_list_idx=(int)s_builds.size()-1; }
        s_save_ok=BuildCache::SaveUserBuilds(s_builds);
        s_save_time=std::chrono::steady_clock::now();
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) { s_list_idx=-1; ClearForm(); }
    ImGui::SameLine();
    if (ImGui::Button("Refresh Specs & Stats")) { ItemLookup::RefreshStatNames(); ItemLookup::RefreshSpecData(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-fetch spec and stat data from GW2 API");
    ImGui::SameLine();
    if (s_list_idx>=0&&s_list_idx<(int)s_builds.size())
        if (ImGui::Button("Delete")) {
            s_builds.erase(s_builds.begin()+s_list_idx);
            BuildCache::SaveUserBuilds(s_builds); s_list_idx=-1; ClearForm();
        }

    /* Save flash */
    auto elapsed=std::chrono::steady_clock::now()-s_save_time;
    if (elapsed<std::chrono::seconds(3)) {
        ImGui::SameLine();
        if (s_save_ok) ImGui::TextColored(ImVec4(0.3f,1.f,0.3f,1.f),"Saved!");
        else           ImGui::TextColored(ImVec4(1.f,0.4f,0.4f,1.f),"Save failed");
    }

    /* Resolve summary */
    int ok=0,fail=0;
    for (int r=0;r<NUM_SLOTS;r++) if (s_stat_name[r][0]) {
        if (s_stat_resolve[r].ok) ok++; else if (!s_stat_resolve[r].hint.empty()) fail++;
    }
    if (ok||fail)
        ImGui::TextColored(fail?ImVec4(1.f,0.5f,0.2f,1.f):ImVec4(0.4f,1.f,0.4f,1.f),
                           "Stats: %d resolved, %d unresolved",ok,fail);

    ImGui::End();
}

} /* namespace BuildEditor */
