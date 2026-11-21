#include "SPABoardGeometry.h"

#include "SPABoardData.h"
#include "SPAProjection.h"
#include <algorithm>
#include <array>
#include <limits>
#include <opencv2/core/optim.hpp>
#include <opencv2/imgproc.hpp>
#include <queue>

namespace asst::spa
{
cv::Mat board_adjustment(const double* p)
{
    return (
        cv::Mat_<double>(3, 3) << 1 + p[2],
        p[4],
        p[0] - p[2] * 160 - p[4] * 90,
        p[5],
        1 + p[3],
        p[1] - p[5] * 160 - p[3] * 90,
        0,
        0,
        1);
}

class BoardOverlayFit final : public cv::MinProblemSolver::Function
{
public:
    BoardOverlayFit(cv::Mat model, cv::Mat observed, cv::Mat occluded) :
        m_model(std::move(model)),
        m_observed(std::move(observed)),
        m_occluded(std::move(occluded))
    {
        m_observed = m_observed.clone();
        m_observed.setTo(0, m_occluded);
        m_sum = cv::sum(m_observed)[0];
        m_variance = m_observed.dot(m_observed) - m_sum * m_sum / m_observed.total();
    }

    int getDims() const override { return 6; }

    double calc(const double* p) const override
    {
        if (std::abs(p[0]) > 11 || std::abs(p[1]) > 11 || std::abs(p[2]) > .18 || std::abs(p[3]) > .18 ||
            std::abs(p[4]) > .12 || std::abs(p[5]) > .12) {
            return 1;
        }
        const auto homography = board_adjustment(p);
        cv::Mat aligned;
        cv::warpAffine(m_model, aligned, homography(cv::Rect(0, 0, 3, 2)), m_observed.size());
        aligned.setTo(0, m_occluded);
        const double sum = cv::sum(aligned)[0];
        const double variance = aligned.dot(aligned) - sum * sum / aligned.total();
        if (variance <= 0 || m_variance <= 0) {
            return 1;
        }
        return -(aligned.dot(m_observed) - sum * m_sum / aligned.total()) / std::sqrt(variance * m_variance);
    }

private:
    cv::Mat m_model, m_observed, m_occluded;
    double m_sum = 0, m_variance = 0;
};

cv::Point2f board_project(const cv::Mat& matrix, float col, float row)
{
    std::vector<cv::Point2f> out;
    cv::perspectiveTransform(std::vector<cv::Point2f> { { col, row } }, out, matrix);
    return out.front();
}

bool board_grid_consensus(const cv::Mat& selected, const std::vector<cv::Mat>& alternatives)
{
    if (selected.rows != 3 || selected.cols != 3 || !cv::checkRange(selected) || alternatives.empty()) {
        return false;
    }
    for (const auto& alternative : alternatives) {
        if (alternative.rows != 3 || alternative.cols != 3 || !cv::checkRange(alternative)) {
            return false;
        }
        for (int row = 1; row <= 4; ++row) {
            for (int col = 3; col <= 9; ++col) {
                const auto a = board_project(selected, static_cast<float>(col), static_cast<float>(row));
                const auto b = board_project(alternative, static_cast<float>(col), static_cast<float>(row));
                if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
                    cv::norm(a - b) > 10) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool terrain_walkable(char tile)
{
    return tile == 'G' || tile == 'M' || tile == '.' || tile == 'S' || tile == 'E';
}

std::array<int, 55> board_distances(const BoardTerrain& terrain, int start)
{
    std::array<int, 55> distances;
    distances.fill(1000);
    std::queue<int> pending;
    pending.push(start);
    distances[start] = 0;
    while (!pending.empty()) {
        const int index = pending.front();
        pending.pop();
        const int row = index / 11, col = index % 11;
        for (const auto offset : { cv::Point(1, 0), cv::Point(-1, 0), cv::Point(0, 1), cv::Point(0, -1) }) {
            const int r = row + offset.y, c = col + offset.x;
            if (r < 0 || r >= 5 || c < 0 || c >= 11 || !terrain_walkable(terrain.rows[r][c])) {
                continue;
            }
            const int next = r * 11 + c;
            if (distances[next] > distances[index] + 1) {
                distances[next] = distances[index] + 1;
                pending.push(next);
            }
        }
    }
    return distances;
}

static BoardRegistration
    populate_board_cells(BoardRegistration result, const BoardTerrain& terrain, const cv::Mat& image, bool melee_only)
{
    int home = -1;
    std::vector<int> spawns;
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 11; ++col) {
            if (terrain.rows[row][col] == 'E') {
                home = row * 11 + col;
            }
            if (terrain.rows[row][col] == 'S') {
                spawns.push_back(row * 11 + col);
            }
        }
    }
    if (result.boss_phase_visible && spawns.empty()) {
        const auto normal = std::find_if(BoardTerrains.begin(), BoardTerrains.end(), [&](const auto& row) {
            return row.id == terrain.id;
        });
        if (normal != BoardTerrains.end()) {
            for (int index = 0; index < 55; ++index) {
                if (normal->rows[index / 11][index % 11] == 'S') {
                    spawns.push_back(index);
                }
            }
        }
    }
    if (home < 0 || spawns.empty()) {
        return result;
    }
    const auto from_home = board_distances(terrain, home);
    std::array<bool, 55> routes {};
    for (const auto spawn : spawns) {
        if (from_home[spawn] >= 1000) {
            continue;
        }
        const auto from_spawn = board_distances(terrain, spawn);
        for (int index = 0; index < 55; ++index) {
            routes[index] = routes[index] || from_home[index] + from_spawn[index] == from_home[spawn];
        }
    }
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 11; ++col) {
            const char tile = terrain.rows[row][col];
            if (tile != 'G' && tile != 'M' && tile != 'F' && !(tile == 'R' && !melee_only)) {
                continue;
            }
            const auto point = board_project(result.grid_to_screen, static_cast<float>(col), static_cast<float>(row));
            const cv::Point center(cvRound(point.x), cvRound(point.y));
            if (center.y < 160 || center.y >= 495 || !green_deployment_hint(image, center)) {
                continue;
            }
            result.legal_cells.push_back({ row, col, center, terrain_walkable(tile), routes[row * 11 + col] });
        }
    }
    result.verified = !result.legal_cells.empty();
    result.grid_verified = result.verified;
    return result;
}

BoardRegistration
    register_preparation_board(const cv::Mat& image, bool melee_only, const std::vector<cv::Point>& occupied)
{
    BoardRegistration result;
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return result;
    }
    cv::Mat hsv, observed;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    // Industrial shadows put the active green overlay near V=50--55.
    // Retain that silhouette using its hue/saturation, without lowering map
    // confidence or changing the separate per-cell placement-preview gate.
    cv::inRange(hsv, cv::Scalar(35, 80, 40), cv::Scalar(85, 255, 255), observed);
    const auto restrict_board = [](cv::Mat& mask) {
        mask(cv::Rect(0, 0, 1280, 160)).setTo(0);
        mask(cv::Rect(0, 495, 1280, 225)).setTo(0);
        mask(cv::Rect(0, 0, 300, 720)).setTo(0);
        mask(cv::Rect(1000, 0, 280, 720)).setTo(0);
    };
    restrict_board(observed);
    if (cv::countNonZero(observed) < 8000) {
        return result;
    }
    cv::Mat reduced;
    cv::resize(observed, reduced, cv::Size(320, 180));
    reduced.convertTo(reduced, CV_32F, 1.0 / 255);
    cv::Mat occluded = cv::Mat::zeros(image.size(), CV_8UC1);
    for (const auto& point : occupied) {
        const auto body = cv::Rect(point.x - 30, point.y - 78, 60, 95) & cv::Rect(0, 0, image.cols, image.rows);
        occluded(body).setTo(255);
    }
    cv::resize(occluded, occluded, reduced.size(), 0, 0, cv::INTER_NEAREST);
    // This is an initialization for image registration, not an authorized
    // placement. The actual overlay must fit and every output cell is gated
    // against the live green preview after the fit.
    const std::array<cv::Point2f, 4> grid { { { 3, 1 }, { 8, 1 }, { 3, 4 }, { 9, 4 } } };
    const std::array<cv::Point2f, 4> seed_pixels { { { 480, 238 }, { 878, 238 }, { 470, 440 }, { 978, 440 } } };
    const auto initial = cv::getPerspectiveTransform(grid.data(), seed_pixels.data());
    const BoardTerrain* winner = nullptr;
    std::vector<std::pair<double, cv::Mat>> fitted_boards;
    result.boss_phase_visible = preparation_boss_health_bar(image).has_value();
    const auto& catalogue = result.boss_phase_visible ? BossBoardTerrains : BoardTerrains;
    for (const auto& terrain : catalogue) {
        cv::Mat model = cv::Mat::zeros(image.size(), CV_8UC1);
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 11; ++col) {
                const char tile = terrain.rows[row][col];
                // Mire remains walkable/deployable, but its dark textured
                // surface does not render the same solid green overlay.
                // Do not require its colour to look like an ordinary floor.
                if (tile != 'G' && tile != 'F' && !(tile == 'R' && !melee_only)) {
                    continue;
                }
                std::vector<cv::Point> corners;
                for (const auto offset : { cv::Point2f(-.45F, -.45F),
                                           cv::Point2f(.45F, -.45F),
                                           cv::Point2f(.45F, .45F),
                                           cv::Point2f(-.45F, .45F) }) {
                    const auto point = board_project(initial, col + offset.x, row + offset.y);
                    corners.emplace_back(cvRound(point.x), cvRound(point.y));
                }
                cv::fillConvexPoly(model, corners, cv::Scalar(255));
            }
        }
        restrict_board(model);
        cv::resize(model, model, reduced.size());
        model.convertTo(model, CV_32F, 1.0 / 255);
        cv::Mat parameters = cv::Mat::zeros(1, 6, CV_64F);
        double score = 0;
        try {
            const cv::Mat steps = (cv::Mat_<double>(1, 6) << 2, 2, .015, .015, .01, .01);
            const auto solver = cv::DownhillSolver::create(
                cv::makePtr<BoardOverlayFit>(model, reduced, occluded),
                steps,
                cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 140, 1e-3));
            score = -solver->minimize(parameters);
        }
        catch (const cv::Exception&) {
            continue;
        }
        const auto adjustment64 = board_adjustment(parameters.ptr<double>());
        const cv::Mat enlarge = (cv::Mat_<double>(3, 3) << 4, 0, 0, 0, 4, 0, 0, 0, 1);
        const cv::Mat shrink = (cv::Mat_<double>(3, 3) << .25, 0, 0, 0, .25, 0, 0, 0, 1);
        cv::Mat fitted = enlarge * adjustment64 * shrink * initial;
        bool bounded = cv::checkRange(fitted);
        for (size_t index = 0; bounded && index < grid.size(); ++index) {
            bounded = cv::norm(board_project(fitted, grid[index].x, grid[index].y) - seed_pixels[index]) < 45;
        }
        if (!bounded) {
            continue;
        }
        fitted_boards.emplace_back(score, fitted);
        if (score > result.score) {
            result.alternative_score = result.score;
            result.score = score;
            result.map_id = terrain.id;
            result.grid_to_screen = fitted;
            winner = &terrain;
        }
        else {
            result.alternative_score = std::max(result.alternative_score, score);
        }
    }
    if (!winner || result.score < .80) {
        return result;
    }
    std::vector<cv::Mat> competing_grids;
    for (const auto& [score, fitted] : fitted_boards) {
        if (score >= result.score - .04) {
            competing_grids.push_back(fitted);
        }
    }
    result.grid_verified = board_grid_consensus(result.grid_to_screen, competing_grids);
    if (result.score - result.alternative_score < .04) {
        if (result.grid_verified) {
            for (int row = 1; row <= 4; ++row) {
                for (int col = 3; col <= 9; ++col) {
                    const auto point =
                        board_project(result.grid_to_screen, static_cast<float>(col), static_cast<float>(row));
                    const cv::Point center(cvRound(point.x), cvRound(point.y));
                    if (center.x >= 300 && center.x < 1000 && center.y >= 160 && center.y < 495 &&
                        green_deployment_hint(image, center)) {
                        // Geometry and the live preview authorize this cell;
                        // neither walkability nor a route has been established.
                        result.legal_cells.push_back({ row, col, center, false, false });
                    }
                }
            }
            result.grid_verified = !result.legal_cells.empty();
        }
        return result;
    }
    return populate_board_cells(std::move(result), *winner, image, melee_only);
}

BoardRegistration reproject_preparation_board(
    const cv::Mat& reference,
    const cv::Mat& image,
    const cv::Mat& grid_to_reference,
    const std::string& verified_map_id,
    bool melee_only)
{
    const bool boss_phase = preparation_boss_health_bar(image).has_value();
    const auto& catalogue = boss_phase ? BossBoardTerrains : BoardTerrains;
    const auto terrain =
        std::find_if(catalogue.begin(), catalogue.end(), [&](const auto& item) { return item.id == verified_map_id; });
    if (terrain == catalogue.end()) {
        return {};
    }
    const auto projection = registered_grid_projection(reference, image, grid_to_reference);
    if (!projection) {
        return {};
    }
    BoardRegistration result;
    result.map_id = verified_map_id;
    result.grid_to_screen = *projection;
    result.identity_from_reference = true;
    result.boss_phase_visible = boss_phase;
    return populate_board_cells(std::move(result), *terrain, image, melee_only);
}
}
