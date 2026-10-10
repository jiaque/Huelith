#pragma once
#include "huelith/editing/project.h"
#include <QJsonObject>
#include <atomic>

namespace huelith::editing {
struct RawOptions {
    bool denoise{};
    bool directml{true};
    bool custom_model{}; // Explicit opt-in, constrained to the RawNIND Bayer tensor contract.
    QString model_path;
    double exposure{};
};
struct RawResult {
    QImage image;
    QJsonObject metadata;
};
using RawProgress = std::function<void(int completed, int total)>;
[[nodiscard]] RawResult develop_raw(const QString& path, const RawOptions& options,
                                    const std::atomic_bool& cancelled,
                                    const RawProgress& progress = {});
} // namespace huelith::editing
