#include "RoguelikePreparationAnalyzer.h"

#include <algorithm>
#include <charconv>
#include <optional>

#include "Config/TaskData.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Vision/Matcher.h"
#include "Vision/RegionOCRer.h"

namespace asst::preparation
{
struct LabeledMatch
{
    int value = -1;
    double score = -1.0;
};

bool inside(const cv::Mat& image, const Rect& roi)
{
    return !roi.empty() && roi.x >= 0 && roi.y >= 0 && roi.x + roi.width <= image.cols &&
           roi.y + roi.height <= image.rows;
}

std::optional<int> positive_integer(const std::string& text)
{
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc {} || result.ptr != text.data() + text.size() || value <= 0) {
        return std::nullopt;
    }
    return value;
}

LabeledMatch match_labels(const cv::Mat& image, const Rect& roi, const std::string& task_name)
{
    const auto task = Task.get<MatchTaskInfo>(task_name);
    if (!task || task->templ_names.size() != task->special_params.size() || task->templ_thresholds.empty() ||
        !inside(image, roi)) {
        return {};
    }
    LabeledMatch best;
    bool tied = false;
    for (std::size_t index = 0; index < task->templ_names.size(); ++index) {
        Matcher matcher(image);
        matcher.set_task_info(task);
        matcher.set_templ(task->templ_names[index]);
        matcher.set_threshold(task->templ_thresholds.at(std::min(index, task->templ_thresholds.size() - 1)));
        matcher.set_roi(roi);
        if (const auto result = matcher.analyze()) {
            if (result->score > best.score) {
                best = { task->special_params[index], result->score };
                tied = false;
            }
            else if (result->score == best.score && task->special_params[index] != best.value) {
                tied = true;
            }
        }
    }
    if (tied) {
        best.value = -1;
    }
    return best;
}

std::string read_number(const cv::Mat& image, const Rect& roi, const std::string& task_name, bool character)
{
    if (!inside(image, roi)) {
        return {};
    }
    RegionOCRer reader(image);
    reader.set_task_info(task_name);
    reader.set_use_char_model(character);
    reader.set_roi(roi);
    if (const auto result = reader.analyze()) {
        return result->text;
    }
    return {};
}

std::string availability_name(SkillAvailability value)
{
    switch (value) {
    case SkillAvailability::Available:
        return "available";
    case SkillAvailability::Locked:
        return "locked";
    case SkillAvailability::Absent:
        return "absent";
    default:
        return "unknown";
    }
}
}

bool asst::RoguelikePreparationAnalyzer::skill_page_visible(const cv::Mat& image)
{
    if (image.empty()) {
        return false;
    }
    Matcher header(image);
    header.set_task_info("BlackFlow@Roguelike@QuickFormation");
    return header.analyze().has_value();
}

json::value asst::PreparationOperatorObservation::to_json() const
{
    json::array skills;
    for (std::size_t index = 0; index < availability.size(); ++index) {
        skills.emplace_back(
            json::object {
                { "skill", static_cast<int>(index) + 1 },
                { "availability", preparation::availability_name(availability[index]) },
                { "displayed_training", displayed_skill_levels[index] },
                { "raw_training", raw_skill_levels[index] },
                { "selected_marker_score", selected_marker_scores[index] },
            });
    }
    return json::object {
        { "name", name },
        { "operator_id", operator_id },
        { "anchor", json::array { flag.x, flag.y, flag.width, flag.height } },
        { "anchor_verified", anchor_verified },
        { "training_observed", training_observed },
        { "elite", elite },
        { "elite_score", elite_score },
        { "level", level },
        { "raw_level", raw_level },
        { "selected_skill", selected_skill },
        { "skills", std::move(skills) },
        { "current_module_equipment_verified", false },
        { "account_strength_used", false },
    };
}

std::vector<asst::PreparationOperatorObservation> asst::RoguelikePreparationAnalyzer::analyze(
    const std::vector<PreparationCardAnchor>& cards,
    PreparationPage page,
    PreparationReadMode mode) const
{
    std::vector<PreparationOperatorObservation> observations;
    if (m_image.empty() || m_image.type() != CV_8UC3) {
        return observations;
    }
    cv::Mat blue, numbers;
    if (mode == PreparationReadMode::Full) {
        cv::extractChannel(m_image, blue, 0);
        cv::merge(std::array { blue, blue, blue }, numbers);
    }
    const bool selected_marker_observable = page == PreparationPage::Skills && skill_page_visible(m_image);
    const auto elite_task = Task.get("BlackFlowPreparationElite");
    const auto level_task = Task.get("BlackFlowPreparationLevel");
    const auto training_task = Task.get("BlackFlowPreparationTraining");
    const auto basic_task = Task.get("BlackFlowPreparationBasicSkill");
    const auto selected_task = Task.get("BlackFlowPreparationSelected");
    if (!elite_task || !level_task || !training_task || !basic_task || !selected_task ||
        training_task->special_params.empty() || selected_task->special_params.size() != 3) {
        return observations;
    }
    for (const auto& card : cards) {
        PreparationOperatorObservation observation;
        observation.name = card.name;
        observation.flag = card.flag;
        observation.training_observed = mode == PreparationReadMode::Full;
        if (card.name.empty() || !preparation::inside(m_image, card.flag)) {
            continue;
        }
        Matcher flag(m_image);
        flag.set_task_info("RoguelikeSkillSelectionFlag");
        flag.set_roi(card.flag);
        observation.anchor_verified = flag.analyze().has_value();
        if (!observation.anchor_verified) {
            observations.push_back(std::move(observation));
            continue;
        }
        observation.operator_id = RoguelikeTraining.unique_identity(card.name);
        if (mode == PreparationReadMode::Full) {
            const auto elite =
                preparation::match_labels(m_image, card.flag.move(elite_task->rect_move), "BlackFlowPreparationElite");
            observation.elite = elite.value;
            observation.elite_score = elite.score;
            observation.raw_level = preparation::read_number(
                numbers,
                card.flag.move(level_task->rect_move),
                "BlackFlowPreparationLevel",
                false);
            const auto level = preparation::positive_integer(observation.raw_level);
            const auto cap = RoguelikeTraining.maximum_level(card.name, observation.elite);
            if (level && cap && *level <= *cap) {
                observation.level = *level;
            }
        }
        for (std::size_t skill = 0; skill < observation.availability.size(); ++skill) {
            observation.availability[skill] = RoguelikeTraining.skill_availability(
                card.name,
                static_cast<int>(skill) + 1,
                observation.elite,
                observation.level);
            if (observation.availability[skill] == SkillAvailability::Locked ||
                observation.availability[skill] == SkillAvailability::Absent) {
                continue;
            }
            if (selected_marker_observable) {
                auto marker = selected_task->rect_move;
                marker.x += static_cast<int>(skill) * selected_task->special_params[0];
                marker = card.flag.move(marker);
                if (preparation::inside(m_image, marker)) {
                    cv::Mat white;
                    cv::inRange(
                        m_image(make_rect<cv::Rect>(marker)),
                        cv::Scalar::all(selected_task->special_params[1]),
                        cv::Scalar::all(255),
                        white);
                    observation.selected_marker_scores[skill] =
                        static_cast<double>(cv::countNonZero(white)) / white.total();
                }
            }
        }
        if (selected_marker_observable) {
            int selected = -1, matches = 0;
            for (std::size_t skill = 0; skill < observation.availability.size(); ++skill) {
                if (observation.selected_marker_scores[skill] >= selected_task->special_params[2] / 100.0) {
                    selected = static_cast<int>(skill) + 1;
                    ++matches;
                }
            }
            if (matches == 1) {
                observation.selected_skill = selected;
                if (observation.availability[selected - 1] == SkillAvailability::Unknown) {
                    observation.availability[selected - 1] = SkillAvailability::Available;
                }
            }
        }
        for (std::size_t skill = 0; skill < observation.availability.size(); ++skill) {
            if (mode != PreparationReadMode::Full) {
                break;
            }
            if (observation.availability[skill] != SkillAvailability::Available) {
                continue;
            }
            auto badge = training_task->rect_move;
            badge.x += static_cast<int>(skill) * selected_task->special_params[0];
            const auto matched =
                preparation::match_labels(m_image, card.flag.move(badge), "BlackFlowPreparationTraining");
            const auto maximum = RoguelikeTraining.maximum_training(card.name, static_cast<int>(skill) + 1);
            if (matched.value > 0 && maximum && matched.value <= *maximum) {
                observation.displayed_skill_levels[skill] = matched.value;
                continue;
            }
            auto digit = basic_task->rect_move;
            digit.x += static_cast<int>(skill) * selected_task->special_params[0];
            observation.raw_skill_levels[skill] =
                preparation::read_number(numbers, card.flag.move(digit), "BlackFlowPreparationBasicSkill", true);
            const auto rank = preparation::positive_integer(observation.raw_skill_levels[skill]);
            const auto basic_cap = RoguelikeTraining.maximum_base_skill(card.name);
            if (rank && basic_cap && *rank <= *basic_cap) {
                observation.displayed_skill_levels[skill] = *rank;
            }
        }
        observations.push_back(std::move(observation));
    }
    return observations;
}
