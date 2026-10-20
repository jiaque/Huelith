#pragma once
#include "huelith/editing/ai_worker.h"
#include <QStringList>
#include <vector>
namespace huelith::editing {
enum class PhotoMergeMode { hdr, panorama, focus };
struct PhotoMergeOptions {
    PhotoMergeMode mode{PhotoMergeMode::hdr};
    QStringList paths;
    std::vector<double> exposure_ev;
    int reference{};
    bool align{true};
    bool crop{true};
    int deghost{1};
    int projection{}; // Spherical, cylindrical, perspective.
    int preview_edge{}; // Zero means full-resolution output.
};
AiResult merge_photos(const PhotoMergeOptions& options, const std::atomic_bool* cancelled = nullptr,
                      const AiProgress& progress = {});
AiResult infer_photo_merge(const QJsonObject& request, const AiProgress& progress);
}
