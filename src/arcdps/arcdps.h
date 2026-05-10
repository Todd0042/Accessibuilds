#pragma once
#include "arcdps_structs.h"
#include "../build/types.h"
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>

namespace ArcDPS {

struct CastEvent {
    uint64_t    timestamp_ms;
    uint32_t    skill_id;
    std::string skill_name;
    bool        cancelled;
    bool        fired;
};

struct BoonUptime {
    uint32_t boon_id;
    std::string name;
    float    uptime_pct;
};

struct CoachingNote {
    float       fight_time_s;
    std::string message;
    bool        is_error;
};

/*
 * Called by arcdps.cpp when a new GW2 client build number is detected via
 * CBTS_GWBUILD.  MainWindow wires this up to trigger a Snow Crows re-fetch.
 */
using GW2BuildChangedCb = std::function<void(uint64_t new_build)>;
void SetGW2BuildChangedCallback(GW2BuildChangedCb cb);

void Init();
void Shutdown();
void ResetFight();

bool     IsInCombat();
double   GetCurrentDPS();
double   GetLast10SecDPS();
double   GetPeakDPS();
uint64_t GetFightDurationMs();
uint64_t GetLastGW2Build();
int      GetEventCount();
int      GetDamageEventCount();

/* Returns a copy of per-second DPS history for the current/last fight.
 * Index 0 = first second, each value is total damage dealt in that second. */
std::vector<float> GetDpsHistory();

/* Healing Stats — requires the ArcDPS Healing Stats addon.
 * IsHealAddonActive() returns true once any event from that addon has been received.
 * Barrier is also tracked from the standard ArcDPS stream as a fallback. */
double GetCurrentHPS();
double GetCurrentBPS();   /* barrier per second */
double GetPeakHPS();
std::vector<float> GetHealHistory();
std::vector<float> GetBarrierHistory();

const std::vector<CastEvent>&  GetCastHistory();
std::vector<CastEvent>         GetCastHistoryCopy();
const std::vector<BoonUptime>& GetBoonUptimes();

std::vector<CoachingNote> AnalyseRotation(const GW2::SCBuild& reference);

void OnLocalCombatEvent(void* aEventArgs);

} /* namespace ArcDPS */
