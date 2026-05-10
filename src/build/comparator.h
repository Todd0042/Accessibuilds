#pragma once
#include "types.h"
#include <string>
#include <vector>

namespace BuildComparator {

enum class Severity { OK, Warning, Error };

struct Diff {
    Severity    severity;
    std::string category;   /* "Trait", "Skill", "Gear", "Food" */
    std::string field;      /* specific field name */
    std::string player_val;
    std::string expected_val;
    std::string message;
};

struct ComparisonResult {
    std::vector<Diff> diffs;
    int error_count   = 0;
    int warning_count = 0;
    bool perfect_match = false;

    bool HasDiffs() const { return !diffs.empty(); }
};

/* Full build comparison (traits + skills + gear) */
ComparisonResult Compare(const GW2::PlayerBuild& player,
                         const GW2::SCBuild&     reference);

/* Trait-only comparison */
ComparisonResult CompareTraits(const GW2::TraitBuild& player,
                               const GW2::TraitBuild& reference);

/* Skill-only comparison */
ComparisonResult CompareSkills(const GW2::SkillBar& player,
                               const GW2::SkillBar& reference);

/* Gear-only comparison (stats, runes, sigils, infusions, relic, food) */
ComparisonResult CompareGear(const GW2::GearBuild& player,
                             const GW2::GearBuild& reference);

/* Helper: slot name for display */
std::string SlotName(GW2::GearSlot slot);

} /* namespace BuildComparator */
