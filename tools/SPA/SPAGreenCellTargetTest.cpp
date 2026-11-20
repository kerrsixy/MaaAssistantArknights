#include "Task/MiniGame/SPA/SPAGreenCellTarget.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <meojson/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

int main()
{
    using asst::spa::green_deployment_hint;
    using asst::spa::registered_green_cell_target;
    const auto fixtures = json::open("green_cell_interiors.json");
    assert(fixtures);
    int cases = 0;
    for (const auto& entry : fixtures->at("cases").as_array()) {
        const auto held = cv::imread(entry.at("image").as_string());
        assert(held.size() == cv::Size(1280, 720));
        cv::Mat projection(3, 3, CV_64F);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                projection.at<double>(row, col) = entry.at("projection").at(row).at(col).as_double();
            }
        }
        const cv::Point cell(entry.at("cell").at(0).as_integer(), entry.at("cell").at(1).as_integer());
        const cv::Point original(
            entry.at("original_point").at(0).as_integer(),
            entry.at("original_point").at(1).as_integer());
        // Both real quick3 failures have an edge anchor that fails the same
        // unmodified native threshold, while the cell interior is usable.
        assert(!green_deployment_hint(held, original));
        const auto target = registered_green_cell_target(held, projection, cell, true, true, true);
        assert(target && *target != original);
        assert(green_deployment_hint(held, *target));
        std::vector<cv::Point2f> target_grid;
        cv::perspectiveTransform(std::vector<cv::Point2f> { cv::Point2f(*target) }, target_grid, projection.inv());
        assert(target_grid.size() == 1);
        assert(std::abs(target_grid[0].x - cell.x) < .49 && std::abs(target_grid[0].y - cell.y) < .49);
        assert(!registered_green_cell_target(held, projection, cell, false, true, true));
        assert(!registered_green_cell_target(held, projection, cell, true, false, true));
        assert(!registered_green_cell_target(held, projection, cell, true, true, false));

        // A green adjacent cell cannot authorize the empty target. These
        // synthetic negative masks use the real frame's registered geometry.
        for (const auto offset : { cv::Point(-1, 0), cv::Point(1, 0), cv::Point(0, -1), cv::Point(0, 1) }) {
            cv::Mat neighboring = cv::Mat::zeros(held.size(), held.type());
            std::vector<cv::Point2f> corners;
            cv::perspectiveTransform(
                std::vector<cv::Point2f> { { cell.x + offset.x - .49F, cell.y + offset.y - .49F },
                                           { cell.x + offset.x + .49F, cell.y + offset.y - .49F },
                                           { cell.x + offset.x + .49F, cell.y + offset.y + .49F },
                                           { cell.x + offset.x - .49F, cell.y + offset.y + .49F } },
                corners,
                projection);
            std::vector<cv::Point> polygon;
            for (const auto point : corners) {
                polygon.emplace_back(cvRound(point.x), cvRound(point.y));
            }
            cv::fillConvexPoly(neighboring, polygon, cv::Scalar(0, 255, 0));
            assert(!registered_green_cell_target(neighboring, projection, cell, true, true, true));
        }
        assert(!registered_green_cell_target(
            cv::Mat::zeros(held.size(), held.type()),
            projection,
            cell,
            true,
            true,
            true));
        assert(!registered_green_cell_target(held, cv::Mat(), cell, true, true, true));
        assert(!registered_green_cell_target(held, cv::Mat::zeros(3, 3, CV_64F), cell, true, true, true));
        assert(!registered_green_cell_target(held, projection, { -1, 2 }, true, true, true));
        assert(!registered_green_cell_target(held, projection, { 11, 2 }, true, true, true));
        assert(!registered_green_cell_target(held, projection, { 6, 5 }, true, true, true));
        assert(!registered_green_cell_target(cv::Mat(720, 1280, CV_8UC1), projection, cell, true, true, true));
        assert(!registered_green_cell_target(cv::Mat(360, 640, CV_8UC3), projection, cell, true, true, true));
        auto invalid = projection.clone();
        invalid.at<double>(0, 0) = std::numeric_limits<double>::quiet_NaN();
        assert(!registered_green_cell_target(held, invalid, cell, true, true, true));
        std::cout << "R" << entry.at("round").as_integer() << " edge " << original << " -> same-cell green " << *target
                  << '\n';
        ++cases;
    }
    assert(cases == 2);
    std::cout << "Green-cell target: 2 archived frames and adjacent/occupied/unknown/invalid negative checks passed.\n";
}
