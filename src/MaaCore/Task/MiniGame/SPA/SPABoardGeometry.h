#pragma once

#include "MaaUtils/NoWarningCV.hpp"
#include <string>
#include <vector>

namespace asst::spa
{
struct RegisteredBoardCell
{
    int row = 0;
    int col = 0;
    cv::Point center;
    bool ground_passable = false;
    bool on_ground_route = false;
};

struct BoardRegistration
{
    bool verified = false;
    bool grid_verified = false;
    std::string map_id;
    double score = 0;
    double alternative_score = 0;
    cv::Mat grid_to_screen;
    std::vector<RegisteredBoardCell> legal_cells;
    bool identity_from_reference = false;
    bool boss_phase_visible = false;
};

// Ambiguous terrain can still share a reliable logical grid. All competing
// fits must agree; this does not verify terrain identity or enemy routes.
bool board_grid_consensus(const cv::Mat& selected, const std::vector<cv::Mat>& alternatives);

// Register the current held-card overlay against current-season terrain.
// Buildability, ground passability and route membership stay separate.
BoardRegistration
    register_preparation_board(const cv::Mat& image, bool melee_only, const std::vector<cv::Point>& occupied = {});

// The caller supplies a previously verified same-map reference. This uses
// a verified camera transform; it does not identify a new or changed map.
BoardRegistration reproject_preparation_board(
    const cv::Mat& reference,
    const cv::Mat& image,
    const cv::Mat& grid_to_reference,
    const std::string& verified_map_id,
    bool melee_only);
}
