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

const std::vector<CastEvent>&  GetCastHistory();
const std::vector<BoonUptime>& GetBoonUptimes();

std::vector<CoachingNote> AnalyseRotation(const GW2::SCBuild& reference);

void OnLocalCombatEvent(void* aEventArgs);

} /* namespace ArcDPS */
