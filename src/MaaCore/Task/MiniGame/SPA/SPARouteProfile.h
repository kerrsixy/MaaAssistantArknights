#pragma once

#include <map>
#include <string>
#include <vector>

namespace asst::spa
{
// A route profile is planning knowledge, not a hard-coded graduation roster.
// It describes the earliest useful book window and the role signals that
// should raise a route's score when those operators actually appear.
struct RouteProfile
{
    std::string pact;
    int minimum_shop_level = 2;
    std::vector<std::string> growth_engines;
    std::vector<std::string> output_candidates;
    // Units whose value comes from a battle-time deploy/retreat/redeploy
    // loop.  This is deliberately separate from preparation-phase buying or
    // selling, so diagnostics cannot describe the two as the same action.
    std::vector<std::string> battle_redeploy_units;
    std::vector<std::string> preferred_support_pacts;
    // Harmony contributes one activation member to core pacts and inherits
    // their active effects; two distinct core members plus Harmony can
    // activate a three-member core. Literal six-member bonuses stay separate.
    std::vector<std::string> conditional_support_pacts = { "调和" };
};

const std::map<std::string, RouteProfile>& route_profiles();
}
