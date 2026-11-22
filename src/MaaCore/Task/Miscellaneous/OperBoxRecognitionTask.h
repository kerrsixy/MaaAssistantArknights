#pragma once
#include "Common/AsstBattleDef.h"
#include "Task/AbstractTask.h"
#include "Vision/Oper/OperBoxImageAnalyzer.h"
#include <functional>

namespace asst
{
class OperBoxRecognitionTask : public AbstractTask
{
public:
    using AbstractTask::AbstractTask;
    virtual ~OperBoxRecognitionTask() override = default;

    // Optional page-local observer. It must restore and verify the same list page.
    using PageVisitor = std::function<bool(const std::vector<OperBoxInfo>&)>;

    void set_page_visitor(PageVisitor visitor) { m_page_visitor = std::move(visitor); }

    // Optional navigation guard, run before recognizing each list page.
    void set_page_guard(std::function<bool()> guard) { m_page_guard = std::move(guard); }

    [[nodiscard]] const auto& own_operators() const noexcept { return m_own_opers; }

    [[nodiscard]] bool stopped_at_repeated_page() const noexcept { return m_repeated_page_stop; }

    void set_stop_after_page(std::function<bool()> predicate) { m_stop_after_page = std::move(predicate); }

protected:
    virtual bool _run() override;
    void swipe_page();
    void callback_analyze_result(bool done);
    bool swipe_and_analyze();

    std::unordered_map<std::string, OperBoxInfo> m_own_opers;
    PageVisitor m_page_visitor;
    std::function<bool()> m_page_guard;
    bool m_repeated_page_stop = false;
    std::function<bool()> m_stop_after_page;
};
}
