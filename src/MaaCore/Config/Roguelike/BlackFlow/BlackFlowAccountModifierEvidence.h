#pragma once

#include <string>
#include <vector>

namespace asst::blackflow
{
// Outside-exploration account evidence; independent of a fresh run ID and of
// temporary/recruited operators. A source-cohort change requires revalidation.
struct LifeModifierReceipt
{
    std::string client;
    std::string uid;
    std::string source;
    std::string source_hash;
    std::string observed_at;
    std::string evidence_file;
    std::vector<std::string> active_ids;
    std::vector<std::string> inactive_ids;
};
}
