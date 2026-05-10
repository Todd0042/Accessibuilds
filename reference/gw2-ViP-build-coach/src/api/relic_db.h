#pragma once
#include <cstdint>
#include <cstring>

/* Static name→ID table for all GW2 Relic items.
 * Dual-ID relics (PvE/WvW vs PvP variants) use the PvE/WvW ID.
 * Source: https://wiki.guildwars2.com/wiki/Relic */

struct RelicEntry { const char* name; uint32_t id; };

static const RelicEntry RELIC_DB[] = {
    { "Legendary Relic",                    101582 },
    { "Relic of Agony",                     104849 },
    { "Relic of Akeem",                     100432 },
    { "Relic of Altruism",                  104256 },
    { "Relic of Antitoxin",                 100390 },
    { "Relic of Atrocity",                  102245 },
    { "Relic of Bava Nisos",                104848 },
    { "Relic of Bloodstone",                104800 },
    { "Relic of Castora",                   105652 },
    { "Relic of Cerus",                     100074 },
    { "Relic of Dagda",                     100942 },
    { "Relic of Durability",                100455 },
    { "Relic of Dwayna",                    100442 },
    { "Relic of Evasion",                   100614 },
    { "Relic of Febe",                      101116 },
    { "Relic of Fire",                      104501 },
    { "Relic of Fireworks",                 100947 },
    { "Relic of Fog",                       107030 },
    { "Relic of Geysers",                   103763 },
    { "Relic of Isgarren",                   99997 },
    { "Relic of Karakosa",                  101268 },
    { "Relic of Leadership",                100625 },
    { "Relic of Lyhr",                      100461 },
    { "Relic of Mabon",                     100115 },
    { "Relic of Mercy",                     100429 },
    { "Relic of Mistburn",                  104994 },
    { "Relic of Mosyn",                     101801 },
    { "Relic of Mount Balrior",             103872 },
    { "Relic of Nayos",                     101198 },
    { "Relic of Nourys",                    101191 },
    { "Relic of Peitha",                    100177 },
    { "Relic of Resistance",                100794 },
    { "Relic of Reunification",             103984 },
    { "Relic of Rivers",                    103015 },
    { "Relic of Shackles",                  106916 },
    { "Relic of Sorrow",                    103424 },
    { "Relic of Speed",                     100148 },
    { "Relic of Surging",                   100063 },
    { "Relic of the Adventurer",            100561 },
    { "Relic of the Afflicted",             100693 },
    { "Relic of the Alliance",              107192 },
    { "Relic of the Aristocracy",           100849 },
    { "Relic of the Astral Ward",           100388 },
    { "Relic of the Beehive",               103977 },
    { "Relic of the Biomancer",             106364 },
    { "Relic of the Blightbringer",         102199 },
    { "Relic of the Brawler",               100527 },
    { "Relic of the Cavalier",              100542 },
    { "Relic of the Centaur",               100385 },
    { "Relic of the Chronomancer",          100450 },
    { "Relic of the Citadel",               100448 },
    { "Relic of the Claw",                  103574 },
    { "Relic of the Coral Heart",           107061 },
    { "Relic of the Daredevil",             100345 },
    { "Relic of the Deadeye",               100924 },
    { "Relic of the Defender",              100934 },
    { "Relic of the Demon Queen",           101166 },
    { "Relic of the Dragonhunter",          100090 },
    { "Relic of the Eagle",                 104241 },
    { "Relic of the Earth",                 100435 },
    { "Relic of the Firebrand",             100453 },
    { "Relic of the First Revenant",        105585 },
    { "Relic of the Flock",                  99965 },
    { "Relic of the Forest Dweller",        107124 },
    { "Relic of the Founding",              101737 },
    { "Relic of the Fractal",               100153 },
    { "Relic of the Golemancer",            100403 },
    { "Relic of the Herald",                100219 },
    { "Relic of the Holosmith",             100908 },
    { "Relic of the Ice",                   100048 },
    { "Relic of the Krait",                 100230 },
    { "Relic of the Lich",                  100238 },
    { "Relic of the Living City",           104928 },
    { "Relic of the Midnight King",         101139 },
    { "Relic of the Mirage",                100158 },
    { "Relic of the Mist Stranger",         106206 },
    { "Relic of the Mists Tide",            103901 },
    { "Relic of the Monk",                  100031 },
    { "Relic of the Nautical Beast",        106920 },
    { "Relic of the Necromancer",           100580 },
    { "Relic of the Nightmare",             100579 },
    { "Relic of the Ogre",                  100311 },
    { "Relic of the Pack",                  100752 },
    { "Relic of the Phenom",                104733 },
    { "Relic of the Pirate Queen",          106221 },
    { "Relic of the Privateer",             100479 },
    { "Relic of the Reaper",                100739 },
    { "Relic of the Scoundrel",             106355 },
    { "Relic of the Scourge",               100368 },
    { "Relic of the Sorcerer",              101863 },
    { "Relic of the Steamshrieker",         104022 },
    { "Relic of the Stormsinger",           102595 },
    { "Relic of the Sunless",               100400 },
    { "Relic of the Thief",                 100916 },
    { "Relic of the Trooper",               100411 },
    { "Relic of the Twin Generals",         101767 },
    { "Relic of the Unseen Invasion",       100694 },
    { "Relic of the Warrior",               100144 },
    { "Relic of the Water",                 100659 },
    { "Relic of the Wayfinder",             101943 },
    { "Relic of the Weaver",                100194 },
    { "Relic of the Wizard's Tower",        100557 },
    { "Relic of the Zephyrite",             100893 },
    { "Relic of Thorns",                    104424 },
    { "Relic of Vampirism",                 100676 },
    { "Relic of Vass",                      100775 },
    { "Relic of Zakiros",                   101955 },
};

static constexpr int RELIC_DB_COUNT = (int)(sizeof(RELIC_DB) / sizeof(RELIC_DB[0]));

inline uint32_t FindRelicID(const char* name)
{
    if (!name || !name[0]) return 0;
    for (int i = 0; i < RELIC_DB_COUNT; i++)
        if (_stricmp(RELIC_DB[i].name, name) == 0)
            return RELIC_DB[i].id;
    return 0;
}

inline const char* FindRelicName(uint32_t id)
{
    if (!id) return nullptr;
    for (int i = 0; i < RELIC_DB_COUNT; i++)
        if (RELIC_DB[i].id == id) return RELIC_DB[i].name;
    return nullptr;
}
