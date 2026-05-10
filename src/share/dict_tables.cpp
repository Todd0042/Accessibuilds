#include "dict_tables.h"

namespace DictTable {

std::unordered_map<uint32_t,int> g_StatToIdx;
std::unordered_map<uint32_t,int> g_RelicToIdx;
std::unordered_map<uint32_t,int> g_UpgradeToIdx;
std::unordered_map<uint32_t,int> g_FoodToIdx;
std::unordered_map<uint32_t,int> g_UtilityToIdx;

void Init()
{
    if (!g_StatToIdx.empty()) return;
    for (int i = 0; i < DICT_RELIC_COUNT;   i++) g_RelicToIdx  [DICT_RELIC[i]]   = i;
    for (int i = 0; i < DICT_STAT_COUNT;    i++) g_StatToIdx   [DICT_STAT[i]]    = i;
    for (int i = 0; i < DICT_UPGRADE_COUNT; i++) g_UpgradeToIdx[DICT_UPGRADE[i]]  = i;
    for (int i = 0; i < DICT_FOOD_COUNT;    i++) g_FoodToIdx   [DICT_FOOD[i]]    = i;
    for (int i = 0; i < DICT_UTILITY_COUNT; i++) g_UtilityToIdx[DICT_UTILITY[i]] = i;
}

} /* namespace DictTable */
