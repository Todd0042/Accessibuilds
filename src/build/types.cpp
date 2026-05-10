#include "types.h"

namespace GW2 {

const char* ProfessionName(Profession p)
{
    switch (p) {
    case Profession::Guardian:      return "Guardian";
    case Profession::Warrior:       return "Warrior";
    case Profession::Engineer:      return "Engineer";
    case Profession::Ranger:        return "Ranger";
    case Profession::Thief:         return "Thief";
    case Profession::Elementalist:  return "Elementalist";
    case Profession::Mesmer:        return "Mesmer";
    case Profession::Necromancer:   return "Necromancer";
    case Profession::Revenant:      return "Revenant";
    default:                        return "Unknown";
    }
}

const char* EliteSpecName(EliteSpec s)
{
    switch (s) {
    case EliteSpec::Dragonhunter:   return "Dragonhunter";
    case EliteSpec::Firebrand:      return "Firebrand";
    case EliteSpec::Willbender:     return "Willbender";
    case EliteSpec::Berserker:      return "Berserker";
    case EliteSpec::Spellbreaker:   return "Spellbreaker";
    case EliteSpec::Bladesworn:     return "Bladesworn";
    case EliteSpec::Scrapper:       return "Scrapper";
    case EliteSpec::Holosmith:      return "Holosmith";
    case EliteSpec::Mechanist:      return "Mechanist";
    case EliteSpec::Druid:          return "Druid";
    case EliteSpec::Soulbeast:      return "Soulbeast";
    case EliteSpec::Untamed:        return "Untamed";
    case EliteSpec::Daredevil:      return "Daredevil";
    case EliteSpec::Deadeye:        return "Deadeye";
    case EliteSpec::Specter:        return "Specter";
    case EliteSpec::Tempest:        return "Tempest";
    case EliteSpec::Weaver:         return "Weaver";
    case EliteSpec::Catalyst:       return "Catalyst";
    case EliteSpec::Chronomancer:   return "Chronomancer";
    case EliteSpec::Mirage:         return "Mirage";
    case EliteSpec::Virtuoso:       return "Virtuoso";
    case EliteSpec::Reaper:         return "Reaper";
    case EliteSpec::Scourge:        return "Scourge";
    case EliteSpec::Harbinger:      return "Harbinger";
    case EliteSpec::Herald:         return "Herald";
    case EliteSpec::Renegade:       return "Renegade";
    case EliteSpec::Vindicator:     return "Vindicator";
    case EliteSpec::Luminary:       return "Luminary";
    case EliteSpec::Paragon:        return "Paragon";
    case EliteSpec::Amalgam:        return "Amalgam";
    case EliteSpec::Galeshot:       return "Galeshot";
    case EliteSpec::Antiquary:      return "Antiquary";
    case EliteSpec::Evoker:         return "Evoker";
    case EliteSpec::Troubadour:     return "Troubadour";
    case EliteSpec::Ritualist:      return "Ritualist";
    case EliteSpec::Conduit:        return "Conduit";
    default:                        return "None";
    }
}

const char* BuildTypeName(BuildType t)
{
    switch (t) {
    case BuildType::Power:      return "Power";
    case BuildType::Condi:      return "Condi";
    case BuildType::Support:    return "Support";
    case BuildType::Heal:       return "Heal";
    case BuildType::Quickness:  return "Quickness";
    case BuildType::Alacrity:   return "Alacrity";
    default:                    return "Unknown";
    }
}

} /* namespace GW2 */
