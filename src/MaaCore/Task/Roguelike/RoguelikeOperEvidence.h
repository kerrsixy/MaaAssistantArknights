#pragma once

#include "RoguelikeConfig.h"

namespace asst
{
// Only callers with a confirmed current-run identity may merge an observation.
// Missing fields preserve compatible current-run evidence, never account strength.
inline bool merge_current_run_operator(
    std::unordered_map<std::string, RoguelikeOper>& operators,
    const std::string& name,
    int elite,
    int level)
{
    if (name.empty() || elite < -1 || elite > 2 || level < -1) {
        return false;
    }
    const auto found = operators.try_emplace(name, RoguelikeOper { .elite = -1, .level = 0 }).first;
    if (elite >= 0) {
        if (found->second.elite != elite) {
            found->second.preparation = {};
            // Levels belong to an elite phase; promotion can reset the level.
            found->second.level = 0;
        }
        found->second.elite = elite;
    }
    if (level > 0) {
        found->second.level = level;
    }
    return true;
}
}
