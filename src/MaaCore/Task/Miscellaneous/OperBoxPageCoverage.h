#pragma once

#include <cstddef>
#include <set>
#include <string>

namespace asst
{
class OperBoxTraversalBudget
{
public:
    OperBoxTraversalBudget(std::size_t analyses, std::size_t backtracks) :
        m_analysis_limit(analyses),
        m_backtrack_limit(backtracks)
    {
    }

    [[nodiscard]] bool can_analyze() const noexcept { return m_analyses < m_analysis_limit; }

    bool take_analysis() noexcept
    {
        if (!can_analyze()) {
            return false;
        }
        ++m_analyses;
        return true;
    }

    bool request_backtrack() noexcept
    {
        if (m_backtracks >= m_backtrack_limit) {
            return false;
        }
        ++m_backtracks;
        return true;
    }

    [[nodiscard]] std::size_t analyses() const noexcept { return m_analyses; }

    [[nodiscard]] std::size_t backtracks() const noexcept { return m_backtracks; }

private:
    std::size_t m_analysis_limit = 0, m_backtrack_limit = 0;
    std::size_t m_analyses = 0, m_backtracks = 0;
};

inline bool oper_box_pages_overlap(const std::set<std::string>& previous, const std::set<std::string>& current)
{
    if (previous.empty() || current.empty()) {
        return false;
    }
    for (const auto& id : current) {
        if (!id.empty() && previous.contains(id)) {
            return true;
        }
    }
    return false;
}
}
