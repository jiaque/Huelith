#pragma once
#include "huelith/editing/tone.h"
#include <QString>
#include <QUuid>
#include <memory>

namespace huelith::editing {
struct CubeLut {
    int size{};
    std::array<double, 3> minimum{0, 0, 0};
    std::array<double, 3> maximum{1, 1, 1};
    std::vector<std::array<float, 3>> samples;
    QString source;
};
struct Effect {
    QUuid id{QUuid::createUuid()};
    QString name{QStringLiteral("调整")};
    bool enabled{true};
    double amount{1};
    ToneSettings tone;
    std::shared_ptr<const CubeLut> lut;
    QImage mask; // Source-local coverage; null means the whole layer.
};
[[nodiscard]] std::shared_ptr<const CubeLut> parse_cube(const QString& text);
void validate_effects(const std::vector<Effect>& effects);
[[nodiscard]] QImage apply_effects(const QImage& source, const std::vector<Effect>& effects,
                                   const std::atomic_bool* cancelled = nullptr);
} // namespace huelith::editing
