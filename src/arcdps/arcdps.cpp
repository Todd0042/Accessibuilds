#include "arcdps.h"
#include "../shared.h"
#include "../build/cache.h"
#include <mutex>
#include <chrono>
#include <algorithm>
#include <map>
#include <deque>

namespace ArcDPS {

/* ── State ──────────────────────────────────────────────────────────────── */
static bool                 s_in_combat       = false;
static uint64_t             s_combat_enter_ms = 0;
static double               s_current_dps     = 0.0;
static double               s_peak_dps        = 0.0;
static int64_t              s_total_damage    = 0;
static uint64_t             s_last_gw2_build  = 0;
static std::vector<CastEvent>  s_casts;
static std::vector<BoonUptime> s_boons;
static std::mutex              s_mutex;

struct DmgSample { uint64_t time_ms; int64_t dmg; };
static std::deque<DmgSample> s_window10;

/* Per-second DPS history — one float per completed second of fight time */
static std::vector<float> s_dps_history;
static uint64_t           s_history_sec  = 0;   /* last completed second index */
static int64_t            s_sec_damage   = 0;   /* damage in the current second */

/* Heal / barrier tracking (ArcDPS Healing Stats addon + standard barrier stream) */
static int64_t            s_total_heal    = 0;
static int64_t            s_total_barrier = 0;
static double             s_current_hps   = 0.0;
static double             s_current_bps   = 0.0;
static double             s_peak_hps      = 0.0;
static std::vector<float> s_heal_history;
static std::vector<float> s_barrier_history;
static uint64_t           s_hs_history_sec = 0;
static int64_t            s_sec_heal       = 0;
static int64_t            s_sec_barrier    = 0;

static GW2BuildChangedCb    s_build_changed_cb;

static uint16_t s_player_instid = 0;   /* set from CBTS_ENTERCOMBAT for pet detection */

/* Diagnostic counters (reset on combat enter, logged on combat exit) */
static int  s_ev_total          = 0;
static int  s_ev_damage         = 0;

/* Ring buffer: stores the last EV_RING_SIZE per-event debug strings in memory,
 * written to disk only at fight end — no disk I/O during combat. */
static constexpr int EV_RING_SIZE = 30;
static char s_ev_ring[EV_RING_SIZE][256] = {};
static int  s_ev_ring_head  = 0;   /* next write slot */
static int  s_ev_ring_count = 0;   /* valid entries (saturates at EV_RING_SIZE) */

static const std::map<uint32_t, const char*> BOON_NAMES = {
    {743,   "Aegis"},    {725,   "Fury"},       {718, "Regeneration"},
    {1187,  "Quickness"},{30328, "Alacrity"},   {740, "Might"},
    {1122,  "Stability"},{873,   "Resolution"}, {726, "Vigor"},
    {717,   "Swiftness"},
};

static std::map<uint32_t, uint64_t> s_boon_uptime_ms;
static std::map<uint32_t, uint64_t> s_boon_start_ms;
static std::map<uint32_t, int>      s_boon_stacks;

static uint64_t Now()
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(
               steady_clock::now().time_since_epoch()).count();
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void SetGW2BuildChangedCallback(GW2BuildChangedCb cb)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    s_build_changed_cb = std::move(cb);
}

void Init()
{
    if (!APIDefs) return;
    APIDefs->Events_Subscribe(EV_ARCDPS_COMBATEVENT_LOCAL_RAW, OnLocalCombatEvent);
    s_last_gw2_build = BuildCache::LoadGW2Build();
    Log(LOGL_INFO, "ArcDPS event subscription registered");
}

void Shutdown()
{
    if (!APIDefs) return;
    APIDefs->Events_Unsubscribe(EV_ARCDPS_COMBATEVENT_LOCAL_RAW, OnLocalCombatEvent);
}

void ResetFight()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    s_casts.clear();
    s_boons.clear();
    s_boon_uptime_ms.clear();
    s_boon_start_ms.clear();
    s_boon_stacks.clear();
    s_window10.clear();
    s_dps_history.clear();
    s_history_sec     = 0;
    s_sec_damage      = 0;
    s_total_damage    = 0;
    s_current_dps     = 0.0;
    s_peak_dps        = 0.0;
    s_total_heal      = 0;
    s_total_barrier   = 0;
    s_current_hps     = 0.0;
    s_current_bps     = 0.0;
    s_peak_hps        = 0.0;
    s_heal_history.clear();
    s_barrier_history.clear();
    s_hs_history_sec  = 0;
    s_sec_heal        = 0;
    s_sec_barrier     = 0;
    s_ev_ring_head    = 0;
    s_ev_ring_count   = 0;
    s_combat_enter_ms = Now();
    s_in_combat       = true;
}

bool     IsInCombat()          { return s_in_combat; }
double   GetCurrentDPS()       { return s_current_dps; }
double   GetPeakDPS()          { return s_peak_dps; }
uint64_t GetLastGW2Build()     { return s_last_gw2_build; }
int      GetEventCount()       { return s_ev_total; }
int      GetDamageEventCount() { return s_ev_damage; }

double GetLast10SecDPS()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    if (!s_in_combat) return 0.0;
    uint64_t cutoff = Now() - 10000;
    while (!s_window10.empty() && s_window10.front().time_ms < cutoff)
        s_window10.pop_front();
    int64_t sum = 0;
    for (const auto& s : s_window10) sum += s.dmg;
    return sum / 10.0;
}

uint64_t GetFightDurationMs()
{
    if (!s_in_combat || s_combat_enter_ms == 0) return 0;
    return Now() - s_combat_enter_ms;
}

const std::vector<CastEvent>&  GetCastHistory()  { return s_casts; }
const std::vector<BoonUptime>& GetBoonUptimes()  { return s_boons; }

std::vector<CastEvent> GetCastHistoryCopy()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_casts;
}

std::vector<float> GetDpsHistory()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_dps_history;
}

double GetCurrentHPS()     { return s_current_hps; }
double GetCurrentBPS()     { return s_current_bps; }
double GetPeakHPS()        { return s_peak_hps; }

std::vector<float> GetHealHistory()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_heal_history;
}

std::vector<float> GetBarrierHistory()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_barrier_history;
}

/* ── Nexus combat event callback ─────────────────────────────────────────── */
void OnLocalCombatEvent(void* aEventArgs)
{
    if (!aEventArgs) return;
    auto* args = static_cast<ArcDPSCombatEventArgs*>(aEventArgs);
    if (!args->ev) return;

    cbtevent* ev = args->ev;

    /* ── GW2 client build number ─────────────────────────────────────────
     * ArcDPS fires CBTS_GWBUILD at log start.  ev->src_agent = build number.
     * When the number changes we persist it and invoke the callback (which
     * triggers a Snow Crows re-fetch) OUTSIDE the mutex to avoid deadlock.
     */
    if (ev->is_statechange == CBTS_GWBUILD) {
        uint64_t new_build = ev->src_agent;
        GW2BuildChangedCb fire_cb;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lk(s_mutex);
            if (new_build != 0 && new_build != s_last_gw2_build) {
                s_last_gw2_build = new_build;
                fire_cb          = s_build_changed_cb;
                changed          = true;
            }
        }
        if (changed) {
            char buf[80];
            snprintf(buf, sizeof(buf),
                     "GW2 build changed to %llu — refreshing SC data",
                     (unsigned long long)new_build);
            Log(LOGL_INFO, buf);

            g_GW2BuildNumber.store(new_build);
            BuildCache::SaveGW2Build(new_build);  /* fast path, no full save */

            if (fire_cb) fire_cb(new_build);
        }
        return;
    }

    std::lock_guard<std::mutex> lk(s_mutex);

    if (ev->is_statechange == CBTS_ENTERCOMBAT) {
        s_total_damage    = 0;
        s_current_dps     = 0.0;
        s_combat_enter_ms = Now();   /* use our clock — ev->time epoch may differ */
        s_in_combat       = true;
        s_player_instid   = ev->src_instid;  /* used to detect pet/minion hits */
        s_ev_total        = 0;
        s_ev_damage       = 0;
        s_ev_ring_head    = 0;
        s_ev_ring_count   = 0;
        s_window10.clear();
        s_dps_history.clear();
        s_history_sec     = 0;
        s_sec_damage      = 0;
        s_total_heal      = 0;
        s_total_barrier   = 0;
        s_current_hps     = 0.0;
        s_current_bps     = 0.0;
        s_peak_hps        = 0.0;
        s_heal_history.clear();
        s_barrier_history.clear();
        s_hs_history_sec  = 0;
        s_sec_heal        = 0;
        s_sec_barrier     = 0;
        s_casts.clear();
        s_boon_uptime_ms.clear();
        s_boon_start_ms.clear();
        s_boon_stacks.clear();
        return;
    }
    if (ev->is_statechange == CBTS_EXITCOMBAT) {
        /* Flush the ring buffer (oldest → newest) then log the summary */
        {
            int start = (s_ev_ring_count < EV_RING_SIZE) ? 0 : s_ev_ring_head;
            for (int i = 0; i < s_ev_ring_count; i++)
                Log(LOGL_DEBUG, s_ev_ring[(start + i) % EV_RING_SIZE]);

            char buf[128];
            snprintf(buf, sizeof(buf),
                     "ArcDPS: fight end — events=%d damage_events=%d total_dmg=%lld peak_dps=%.0f",
                     s_ev_total, s_ev_damage,
                     (long long)s_total_damage, s_peak_dps);
            Log(LOGL_DEBUG, buf);
        }
        s_in_combat = false;
        /* Finalise any still-active boon uptimes */
        for (auto& [bid, stacks] : s_boon_stacks) {
            if (stacks > 0 && s_boon_start_ms.count(bid))
                s_boon_uptime_ms[bid] += ev->time - s_boon_start_ms[bid];
        }
        /* Build boon uptime vector for the UI */
        s_boons.clear();
        uint64_t fight_dur = (ev->time > s_combat_enter_ms)
                             ? ev->time - s_combat_enter_ms : 1;
        for (auto& [bid, uptime] : s_boon_uptime_ms) {
            BoonUptime bu;
            bu.boon_id    = bid;
            auto it = BOON_NAMES.find(bid);
            bu.name       = it != BOON_NAMES.end() ? it->second : "Boon";
            bu.uptime_pct = (float)uptime / fight_dur * 100.0f;
            if (bu.uptime_pct > 100.0f) bu.uptime_pct = 100.0f;
            s_boons.push_back(std::move(bu));
        }
        return;
    }

    if (!s_in_combat) return;

    uint64_t fight_ms = Now() - s_combat_enter_ms;   /* steady_clock, always valid */

    /* src_is_self: player's own direct hits.
     * src_is_pet: pet/minion whose master is the local player. */
    bool src_is_self = (args->src != nullptr && args->src->self != 0);
    bool src_is_pet  = (s_player_instid != 0 &&
                        ev->src_master_instid == s_player_instid);

    /* Verbose per-event trace — buffered in memory, flushed to disk at fight end */
    {
        bool direct_hit = (!ev->is_statechange && !ev->is_activation && !ev->buff &&
                           ev->value < 0 && (src_is_self || src_is_pet));
        bool condi_a    = (!ev->is_statechange && !ev->is_activation && ev->buff &&
                           ev->is_buffremove == CBTB_NONE && ev->buff_dmg > 0);
        bool condi_b    = (!ev->is_statechange && !ev->is_activation && ev->buff &&
                           ev->is_buffremove == CBTB_NONE && ev->buff_dmg < 0 && ev->iff == IFF_FOE);
        bool outgoing_heal    = (!ev->is_statechange && !ev->is_activation && !ev->buff &&
                                 !ev->is_shields && ev->value > 0 && ev->iff == IFF_FRIEND && src_is_self);
        bool outgoing_barrier = (!ev->is_statechange && !ev->is_activation && ev->is_shields &&
                                 ev->iff == IFF_FRIEND && src_is_self &&
                                 (ev->value > 0 || (ev->buff && ev->buff_dmg > 0)));
        const char* tag = direct_hit ? "DIRECT" : condi_a ? "CONDI-A" : condi_b ? "CONDI-B"
                        : outgoing_heal ? "HEAL" : outgoing_barrier ? "BARRIER" : "skip";
        snprintf(s_ev_ring[s_ev_ring_head], 256,
                 "ArcDPS ev#%02d: sc=%d act=%d buff=%d br=%d shld=%d val=%d bdmg=%d "
                 "iff=%d res=%d skill=%u self=%d pet=%d t_ms=%llu [%s]",
                 s_ev_ring_count,
                 (int)ev->is_statechange, (int)ev->is_activation, (int)ev->buff,
                 (int)ev->is_buffremove, (int)ev->is_shields, ev->value, ev->buff_dmg,
                 (int)ev->iff, (int)ev->result, ev->skillid,
                 src_is_self ? 1 : 0, src_is_pet ? 1 : 0,
                 (unsigned long long)fight_ms, tag);
        s_ev_ring_head = (s_ev_ring_head + 1) % EV_RING_SIZE;
        if (s_ev_ring_count < EV_RING_SIZE) s_ev_ring_count++;
    }

    /* Skill activation */
    if (ev->is_activation == ACTV_NORMAL || ev->is_activation == ACTV_QUICKNESS) {
        CastEvent ce;
        ce.timestamp_ms = fight_ms;
        ce.skill_id     = ev->skillid;
        ce.skill_name   = args->skill_name ? args->skill_name : "";
        ce.cancelled    = false;
        ce.fired        = false;
        s_casts.push_back(std::move(ce));
    }
    if (ev->is_activation == ACTV_CANCEL_CANCEL && !s_casts.empty())
        s_casts.back().cancelled = true;
    if (ev->is_activation == ACTV_CANCEL_FIRE  && !s_casts.empty())
        s_casts.back().fired = true;

    s_ev_total++;

    /* Direct strike damage (player or pet/minion) — value is negative health delta */
    if (!ev->is_statechange && !ev->is_activation && !ev->buff &&
        ev->value < 0 && (src_is_self || src_is_pet)) {
        int64_t dmg = (int64_t)(-ev->value);
        s_total_damage += dmg;
        s_sec_damage   += dmg;
        s_window10.push_back({Now(), dmg});
        s_ev_damage++;
    }
    /* Condition tick — variant A: buff_dmg > 0 (positive tick going to target) */
    if (!ev->is_statechange && !ev->is_activation && ev->buff &&
        ev->is_buffremove == CBTB_NONE && ev->buff_dmg > 0) {
        s_total_damage += ev->buff_dmg;
        s_sec_damage   += ev->buff_dmg;
        s_window10.push_back({Now(), ev->buff_dmg});
        s_ev_damage++;
    }
    /* Condition tick — variant B: buff_dmg < 0 going TO a foe (same negative health-delta
     * convention as direct hits; iff==IFF_FOE confirms the target is an enemy) */
    if (!ev->is_statechange && !ev->is_activation && ev->buff &&
        ev->is_buffremove == CBTB_NONE && ev->buff_dmg < 0 && ev->iff == IFF_FOE) {
        int64_t dmg = (int64_t)(-ev->buff_dmg);
        s_total_damage += dmg;
        s_sec_damage   += dmg;
        s_window10.push_back({Now(), dmg});
        s_ev_damage++;
    }

    if (s_total_damage > 0 && fight_ms > 0) {
        s_current_dps = (double)s_total_damage / (fight_ms / 1000.0);
        if (s_current_dps > s_peak_dps) s_peak_dps = s_current_dps;
    }

    /* Outgoing heals — direct (buff=0, non-barrier, positive value to a friendly from local player) */
    if (!ev->is_statechange && !ev->is_activation && !ev->buff &&
        !ev->is_shields && ev->value > 0 && ev->iff == IFF_FRIEND && src_is_self) {
        s_total_heal += ev->value;
        s_sec_heal   += ev->value;
    }
    /* Outgoing barrier — is_shields=1 to a friendly from local player.
     * Direct form: buff=0, value>0.  Buff-tick form: buff=1, buff_dmg>0, value=0. */
    if (!ev->is_statechange && !ev->is_activation && ev->is_shields &&
        ev->iff == IFF_FRIEND && (src_is_self || src_is_pet)) {
        int32_t amount = ev->value > 0 ? ev->value
                       : (ev->buff && ev->buff_dmg > 0 ? ev->buff_dmg : 0);
        if (amount > 0) {
            s_total_barrier += amount;
            s_sec_barrier   += amount;
        }
    }

    /* Update HPS / BPS */
    if (fight_ms > 0) {
        s_current_hps = (double)s_total_heal    / (fight_ms / 1000.0);
        s_current_bps = (double)s_total_barrier / (fight_ms / 1000.0);
        if (s_current_hps > s_peak_hps) s_peak_hps = s_current_hps;
    }

    /* Advance per-second histories — DPS, heal, and barrier */
    {
        uint64_t fight_sec = fight_ms / 1000;
        while (fight_sec > s_history_sec) {
            s_dps_history.push_back((float)s_sec_damage);
            s_sec_damage = 0;
            ++s_history_sec;
        }
        while (fight_sec > s_hs_history_sec) {
            s_heal_history.push_back((float)s_sec_heal);
            s_barrier_history.push_back((float)s_sec_barrier);
            s_sec_heal    = 0;
            s_sec_barrier = 0;
            ++s_hs_history_sec;
        }
    }

    /* Boon apply */
    if (ev->buff && ev->is_buffremove == CBTB_NONE && ev->value > 0) {
        uint32_t bid = ev->skillid;
        if (BOON_NAMES.count(bid)) {
            if (s_boon_stacks[bid] == 0)
                s_boon_start_ms[bid] = ev->time;
            s_boon_stacks[bid] += ev->value;
        }
    }

    /* Boon remove */
    if (ev->buff && ev->is_buffremove != CBTB_NONE) {
        uint32_t bid = ev->skillid;
        if (BOON_NAMES.count(bid) && s_boon_stacks[bid] > 0) {
            s_boon_uptime_ms[bid] += ev->time - s_boon_start_ms[bid];
            s_boon_stacks[bid]     = 0;
        }
    }
}

/* ── Rotation analysis ───────────────────────────────────────────────────── */
std::vector<CoachingNote> AnalyseRotation(const GW2::SCBuild& ref)
{
    std::vector<CoachingNote> notes;
    if (ref.opener.empty() && ref.loop.empty()) return notes;

    std::lock_guard<std::mutex> lk(s_mutex);

    std::vector<GW2::RotationStep> expected = ref.opener;
    if (!ref.loop.empty()) {
        for (int rep = 0; rep < 20 && expected.size() < s_casts.size() + 10; rep++)
            for (const auto& step : ref.loop)
                expected.push_back(step);
    }

    size_t cast_idx = 0;
    for (size_t ei = 0; ei < expected.size() && cast_idx < s_casts.size(); ei++) {
        const auto& exp = expected[ei];
        if (exp.is_optional) continue;

        bool   found        = false;
        size_t search_limit = std::min(cast_idx + 5, s_casts.size());
        for (size_t ci = cast_idx; ci < search_limit; ci++) {
            if (s_casts[ci].skill_id != exp.skill_id) continue;
            cast_idx = ci + 1;
            found    = true;

            if (exp.expected_cast_time_ms > 0) {
                float drift = (float)s_casts[ci].timestamp_ms - exp.expected_cast_time_ms;
                if (drift > 800.0f) {
                    CoachingNote n;
                    n.fight_time_s = s_casts[ci].timestamp_ms / 1000.0f;
                    n.message      = "\"" + exp.label + "\" was " +
                                     std::to_string((int)(drift / 100.0f) / 10) + "s late";
                    n.is_error     = drift > 2000.0f;
                    notes.push_back(std::move(n));
                }
            }

            if (exp.is_burst && s_casts[ci].cancelled) {
                CoachingNote n;
                n.fight_time_s = s_casts[ci].timestamp_ms / 1000.0f;
                n.message      = "\"" + exp.label + "\" cancelled — burst skill!";
                n.is_error     = true;
                notes.push_back(std::move(n));
            }
            break;
        }

        if (!found && ei < expected.size() / 4) {
            CoachingNote n;
            n.fight_time_s = cast_idx < s_casts.size()
                             ? s_casts[cast_idx].timestamp_ms / 1000.0f : 0.0f;
            n.message  = "Skipped \"" + exp.label + "\" in opener";
            n.is_error = true;
            notes.push_back(std::move(n));
        }
    }

    /* Low boon uptime */
    uint64_t fight_dur = GetFightDurationMs();
    if (fight_dur > 5000) {
        for (const auto& boon : s_boons) {
            if (boon.uptime_pct < 90.0f) {
                CoachingNote n;
                n.fight_time_s = fight_dur / 1000.0f;
                char buf[128];
                snprintf(buf, sizeof(buf), "%s uptime: %.0f%% (target 100%%)",
                         boon.name.c_str(), boon.uptime_pct);
                n.message  = buf;
                n.is_error = boon.uptime_pct < 70.0f;
                notes.push_back(std::move(n));
            }
        }
    }

    return notes;
}

} /* namespace ArcDPS */
