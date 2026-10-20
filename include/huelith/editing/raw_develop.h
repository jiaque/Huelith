#pragma once
#include "huelith/editing/project.h"
#include <QJsonObject>
#include <atomic>

namespace huelith::editing {
[[nodiscard]] bool is_raw_file(const QString& path);
[[nodiscard]] bool is_raw_suffix(const QString& suffix);
[[nodiscard]] QJsonObject raw_capabilities(const QByteArray& source);
struct RawOptions {
    bool denoise{};
    bool directml{true};
    bool custom_model{};     // Explicit opt-in, constrained to the RawNIND Bayer tensor contract.
    bool high_quality{true}; // MHC for new sources; old recipes retain their bilinear version.
    QString model_path;
    double exposure{};
};
struct RawResult {
    QImage image;
    QJsonObject metadata;
    QImage reference; // Unenhanced demosaic in the same linear working space.
};
[[nodiscard]] QImage blend_raw_denoise(const QImage& enhanced, const QImage& reference,
                                       double amount, const std::atomic_bool* cancelled = nullptr);
using RawProgress = std::function<void(int completed, int total)>;
[[nodiscard]] bool raw_high_quality(const QJsonObject& recipe);
[[nodiscard]] RawResult develop_raw(const QString& path, const RawOptions& options,
                                    const std::atomic_bool& cancelled,
                                    const RawProgress& progress = {});
} // namespace huelith::editing
