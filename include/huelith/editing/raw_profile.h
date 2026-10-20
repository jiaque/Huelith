#pragma once
#include "huelith/editing/tone.h"
#include <QJsonArray>
namespace huelith::editing {
QJsonArray builtin_raw_profiles();
QJsonObject validate_raw_profile(const QJsonObject& profile);
QJsonObject read_raw_profile(const QByteArray& bytes);
QImage apply_raw_profile(const QImage& source, const QJsonObject& recipe,
                         const std::atomic_bool* cancelled = nullptr);
ToneSettings automatic_raw_tone(const QImage& source, ToneSettings existing);
} // namespace huelith::editing
