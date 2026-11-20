#pragma once

#include "SPAObservation.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cv
{
class Mat;
}

namespace asst
{
struct Rect;

namespace spa
{
struct TextObservation;
std::optional<int> pact_layers(const cv::Mat& image, const std::string& pact);
// Value and actual OCR confidence, anchored to the top pact HUD. This never
// treats an unreadable inactive counter as zero or substitutes O/C for 0.
std::optional<std::pair<int, double>> pact_hud_number(const cv::Mat& image, int center_x);
// Signed quote next to a uniquely recognized sale button. Bare digits and
// low-confidence tokens are never inferred as the usual one-fund price.
std::optional<std::pair<int, double>> sale_credit_number(const cv::Mat& image, int button_x, int button_y);
std::optional<int> shop_number(const cv::Mat& image, const Rect& roi, double minimum_confidence = .40);
std::optional<int> equipment_shop_price(const cv::Mat& image, int slot);
std::optional<TextObservation> match_hidden_round_badge(const cv::Mat& image);
std::optional<int> preparation_round_number(const cv::Mat& image, int expected_round, double confidence = .40);
std::optional<int> defence_life_number(const cv::Mat& image);
// Read the complete title and training-level digits from one immutable frame.
std::optional<OperatorCardObservation> selected_operator_card(const cv::Mat& image);
std::optional<int> shop_book_level(const cv::Mat& image, int upgrade_center);
std::optional<int> remaining_deployment_number(const cv::Mat& image, const std::vector<TextObservation>& texts);
}
}
