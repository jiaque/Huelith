#pragma once
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <array>
#include <atomic>
namespace huelith::editing {
struct WhiteBalance {
    QString mode{"as-shot"};
    double temperature{6500}, tint{};
};
QPointF white_balance_xy(double temperature, double tint);
WhiteBalance white_balance_temperature(QPointF xy);
WhiteBalance white_balance_from_json(const QJsonObject& object);
QJsonObject white_balance_json(const WhiteBalance& settings);
WhiteBalance white_balance_as_shot(const QJsonObject& metadata);
WhiteBalance white_balance_sample(const QImage& image, const QJsonObject& metadata, QRect region);
QImage apply_white_balance(const QImage& source, const QJsonObject& recipe,
                           const std::atomic_bool* cancelled = nullptr);
} // namespace huelith::editing
