#pragma once
#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCVMat.hpp"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace asst::blackflow
{
enum class SoulEvidenceKind
{
    Action,
    Diagnostic,
    Callback
};

struct SoulEvidenceRecord
{
    SoulEvidenceKind kind = SoulEvidenceKind::Diagnostic;
    json::value payload = json::object {};
    std::filesystem::path json_path, png_path;
    cv::Mat frame;
};

struct SoulEvidenceFlushResult
{
    std::size_t delivered = 0, dropped = 0;
};

// Main-thread, bounded storage only. It performs no IO and grants no input.
class BlackFlowSoulEvidenceBuffer
{
public:
    static constexpr std::size_t MaxRecords = 16, MaxFrames = 8, MaxFrameBytes = 8 * 1280 * 720 * 3,
                                 MaxMetadataBytes = 256 * 1024;

    bool try_push(SoulEvidenceRecord record, const cv::Mat& frame = {})
    {
        const auto bytes = frame.empty() ? 0 : frame.total() * frame.elemSize();
        const auto metadata = record.payload.to_string().size();
        if (m_records.size() >= MaxRecords || m_frames + !frame.empty() > MaxFrames ||
            m_frame_bytes + bytes > MaxFrameBytes || m_metadata_bytes + metadata > MaxMetadataBytes) {
            return false;
        }
        record.frame = frame.clone();
        m_frames += !frame.empty();
        m_frame_bytes += bytes;
        m_metadata_bytes += metadata;
        m_records.emplace_back(std::move(record));
        return true;
    }

    template <class Sink>
    SoulEvidenceFlushResult flush(Sink&& sink) noexcept
    {
        SoulEvidenceFlushResult result;
        for (auto& record : m_records) {
            try {
                if (sink(record)) {
                    ++result.delivered;
                }
                else {
                    ++result.dropped;
                }
            }
            catch (...) {
                ++result.dropped;
            }
        }
        m_records.clear();
        m_frames = 0;
        m_frame_bytes = 0;
        m_metadata_bytes = 0;
        return result;
    }

    std::size_t size() const noexcept { return m_records.size(); }

    std::size_t frame_bytes() const noexcept { return m_frame_bytes; }

private:
    std::vector<SoulEvidenceRecord> m_records;
    std::size_t m_frames = 0, m_frame_bytes = 0, m_metadata_bytes = 0;
};
}
