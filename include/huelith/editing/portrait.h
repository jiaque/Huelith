#pragma once
#include "huelith/editing/ai_worker.h"
#include <QJsonArray>
#include <QRectF>
#include <QUuid>
#include <vector>

namespace huelith::editing {
struct PortraitPerson {
    QUuid id{QUuid::createUuid()};
    QRectF face; // Normalized source coordinates, independent of preview size.
    QRectF body;
    QJsonArray landmarks; // 66 [x,y,confidence] points in normalized coordinates.
    QImage body_mask; // Analysis resolution; no fabricated face rectangle as body.
    QJsonObject settings;
    double confidence{};
    bool body_ambiguous{};
    bool match_ambiguous{};
    bool unmatched{}; // Retained edits without a confirmed current detection.
};
std::vector<PortraitPerson> analyze_portrait(const QImage& image, const QString& models,
    const std::atomic_bool& cancelled, const AiProgress& progress = {});
AiResult infer_portrait(const QImage& image, const QString& models, const AiProgress& progress);
QJsonObject portrait_person_json(const PortraitPerson& person);
PortraitPerson portrait_person_from_json(const QJsonObject& object);
// Only same-source spatial re-detection. Cross-photo identity is always explicit.
void match_portrait_people(std::vector<PortraitPerson>& detected,
                          const std::vector<PortraitPerson>& previous);
} // namespace huelith::editing
