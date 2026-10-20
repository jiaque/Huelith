#pragma once
#include <QImage>
#include <QJsonArray>
#include <QPointF>
#include <atomic>
#include <vector>

namespace huelith::editing {
struct HealingStroke {
    bool clone{};
    bool enabled{true};
    std::vector<QPointF> points; // Normalized optical-image coordinates, before geometry.
    std::vector<QPointF> source_offsets; // Optional offset per path vertex, interpolated along segments.
    QPointF offset;              // Normalized source minus destination.
    double radius{.02};          // Relative to the shorter image side.
    double feather{.5};
    double opacity{1};
    bool operator==(const HealingStroke&) const = default;
};
[[nodiscard]] QJsonObject healing_json(const HealingStroke& stroke);
[[nodiscard]] HealingStroke healing_from_json(const QJsonObject& object);
[[nodiscard]] QPointF automatic_healing_offset(const QImage& image, const HealingStroke& stroke,
                                               int alternative = 0, const QImage& source_mask = {},
                                               const QPointF* preferred_offset = nullptr, bool match_texture = false,
                                               bool nearby_only = false);
[[nodiscard]] HealingStroke automatic_healing_path(const QImage& image, const HealingStroke& stroke,
                                                    const QImage& source_mask = {}, bool nearby_only = true);
[[nodiscard]] QImage apply_healing(const QImage& source, const QJsonArray& strokes,
                                   const std::atomic_bool* cancelled = nullptr,
                                   QImage* coverage = nullptr);
// Fully corrected replacement pixels. Callers apply the soft coverage once when compositing.
[[nodiscard]] QImage patch_replacement(const QImage& source, const QImage& coverage, QPointF offset,
                                       double color_adaptation = 1,
                                       const std::atomic_bool* cancelled = nullptr);
[[nodiscard]] QPointF automatic_patch_offset(const QImage& source, const QImage& coverage,
                                             const std::atomic_bool* cancelled = nullptr);
} // namespace huelith::editing
