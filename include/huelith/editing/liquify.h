#pragma once
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <atomic>

namespace huelith::editing {
enum class LiquifyTool { warp, reconstruct, smooth, twirl, pucker, bloat, push_left, freeze, thaw };
struct LiquifyBrush {
    double diameter{100}, density{.5}, pressure{.5};
    LiquifyTool tool{LiquifyTool::warp};
    bool reverse{};
};
// Normalized inverse displacement in RG, frozen coverage in B. No colour conversion.
[[nodiscard]] QImage create_liquify_mesh(QSize source_size);
void validate_liquify_mesh(const QImage& mesh);
double liquify_minimum_jacobian(const QImage& mesh);
[[nodiscard]] QJsonObject liquify_json(const QImage& mesh);
[[nodiscard]] QImage liquify_from_json(const QJsonObject& json);
void liquify_dab(QImage& mesh, QSize source_size, QPointF from, QPointF to,
                 const LiquifyBrush& brush, double amount = 1);
void reconstruct_liquify(QImage& mesh, double amount);
void freeze_liquify(QImage& mesh, int operation); // 0 clear, 1 all, 2 invert
[[nodiscard]] QImage apply_liquify(const QImage& source, const QImage& mesh,
                                   const std::atomic_bool* cancelled = nullptr);
} // namespace huelith::editing
