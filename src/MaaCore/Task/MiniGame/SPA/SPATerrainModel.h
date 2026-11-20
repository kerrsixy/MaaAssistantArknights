#pragma once

#include "SPABoardData.h"
#include "SPADeploymentReview.h"
#include <algorithm>
#include <array>
#include <optional>
#include <queue>

namespace asst::spa
{
struct TerrainLayoutModel
{
    std::vector<BoardCell> buildable;
    std::vector<BoardCell> ground_route;
    std::vector<DeploymentLane> lanes;
};

inline bool terrain_allows_deployment(const std::string& id, BoardCell cell, bool melee, bool boss_phase = false)
{
    if (cell.first < 0 || cell.first > 10 || cell.second < 0 || cell.second > 4) {
        return false;
    }
    const auto& catalogue = boss_phase ? BossBoardTerrains : BoardTerrains;
    const auto board = std::find_if(catalogue.begin(), catalogue.end(), [&](const auto& row) { return row.id == id; });
    if (board == catalogue.end()) {
        return false;
    }
    const char tile = board->rows[cell.second][cell.first];
    return tile == 'G' || tile == 'F' || tile == 'M' || (!melee && tile == 'R');
}

// Current sequence opening schedule supplied by the player: rounds 1-3
// activate only lower entries. Preserve full geometry for future junction
// planning; an unknown round must not silently suppress an enemy entry.
inline std::vector<DeploymentLane> active_preparation_lanes(const TerrainLayoutModel& model, int round)
{
    if (round < 1 || round > 3 || model.lanes.empty()) {
        return model.lanes;
    }
    int lower_row = -1;
    for (const auto& lane : model.lanes) {
        if (lane.path.empty()) {
            return model.lanes;
        }
        lower_row = std::max(lower_row, lane.path.front().second);
    }
    std::vector<DeploymentLane> result;
    for (const auto& lane : model.lanes) {
        if (lane.path.front().second == lower_row) {
            result.push_back(lane);
        }
    }
    return result;
}

// Current-season terrain predicts routes; it does not observe an enemy's
// individual path or validate a screen target. Held-input validation remains
// responsible for the actual map identity, green tile and occupied target.
inline std::optional<TerrainLayoutModel> terrain_layout_model(const std::string& id, bool boss_phase = false)
{
    const auto& catalogue = boss_phase ? BossBoardTerrains : BoardTerrains;
    const auto terrain =
        std::find_if(catalogue.begin(), catalogue.end(), [&](const auto& item) { return item.id == id; });
    if (terrain == catalogue.end()) {
        return std::nullopt;
    }
    const auto walkable = [&](int index) {
        const char tile = terrain->rows[index / 11][index % 11];
        return tile == 'G' || tile == 'M' || tile == '.' || tile == 'S' || tile == 'E';
    };
    int home = -1;
    std::vector<int> spawns;
    for (int index = 0; index < 55; ++index) {
        const char tile = terrain->rows[index / 11][index % 11];
        if (tile == 'E') {
            home = index;
        }
        if (tile == 'S') {
            spawns.push_back(index);
        }
    }
    if (boss_phase && spawns.empty()) {
        // Until the actual preview ingress is available, preserve normal
        // ingress anchors as a conservative route prediction. Buildability
        // below always comes from the separately anchored boss field.
        const auto normal =
            std::find_if(BoardTerrains.begin(), BoardTerrains.end(), [&](const auto& row) { return row.id == id; });
        if (normal != BoardTerrains.end()) {
            for (int index = 0; index < 55; ++index) {
                if (normal->rows[index / 11][index % 11] == 'S') {
                    spawns.push_back(index);
                }
            }
        }
    }
    if (home < 0 || spawns.empty()) {
        return std::nullopt;
    }
    const auto neighbors = [](int index) {
        std::vector<int> result;
        for (const auto& [dx, dy] : std::array<BoardCell, 4> { { { -1, 0 }, { 0, -1 }, { 0, 1 }, { 1, 0 } } }) {
            const int col = index % 11 + dx, row = index / 11 + dy;
            if (col >= 0 && col < 11 && row >= 0 && row < 5) {
                result.push_back(row * 11 + col);
            }
        }
        return result;
    };
    const auto distances = [&](int start) {
        std::array<int, 55> result;
        result.fill(1000);
        std::queue<int> pending;
        pending.push(start);
        result[start] = 0;
        while (!pending.empty()) {
            const int index = pending.front();
            pending.pop();
            for (const int next : neighbors(index)) {
                if (walkable(next) && result[next] > result[index] + 1) {
                    result[next] = result[index] + 1;
                    pending.push(next);
                }
            }
        }
        return result;
    };
    const auto from_home = distances(home);
    std::array<bool, 55> on_route {};
    TerrainLayoutModel result;
    for (const int spawn : spawns) {
        if (from_home[spawn] >= 1000) {
            return std::nullopt;
        }
        const auto from_spawn = distances(spawn);
        for (int index = 0; index < 55; ++index) {
            on_route[index] = on_route[index] || from_home[index] + from_spawn[index] == from_home[spawn];
        }
        DeploymentLane lane;
        lane.id = "terrain-entry-" + std::to_string(spawn);
        lane.minimum_engagement_column = 3;
        lane.precontact_cells = 2;
        const int lowest_spawn_row = *std::max_element(spawns.begin(), spawns.end()) / 11;
        lane.blocking_weight = spawn / 11 == lowest_spawn_row ? 2 : 1;
        int current = spawn;
        lane.path.emplace_back(current % 11, current / 11);
        while (current != home) {
            const auto adjacent = neighbors(current);
            const auto next = std::find_if(adjacent.begin(), adjacent.end(), [&](int candidate) {
                return walkable(candidate) && from_home[candidate] + 1 == from_home[current];
            });
            if (next == adjacent.end()) {
                return std::nullopt;
            }
            current = *next;
            lane.path.emplace_back(current % 11, current / 11);
        }
        result.lanes.push_back(std::move(lane));
    }
    for (int index = 0; index < 55; ++index) {
        const char tile = terrain->rows[index / 11][index % 11];
        if (tile == 'G' || tile == 'M' || tile == 'F' || tile == 'R') {
            result.buildable.emplace_back(index % 11, index / 11);
            if (walkable(index) && on_route[index]) {
                result.ground_route.emplace_back(index % 11, index / 11);
            }
        }
    }
    return result;
}
}
