#pragma once
#include <QJsonObject>
#include <QStringList>
#include <array>
namespace huelith::editing {
using RawGroups = std::array<bool, 14>;
QStringList raw_group_names();
QJsonObject transfer_raw_settings(const QJsonObject& source, QJsonObject target,
                                  const RawGroups& groups);
QJsonObject raw_preset(const QString& name, const QJsonObject& recipe, const RawGroups& groups);
QJsonObject read_raw_preset(const QByteArray& data);
RawGroups raw_preset_groups(const QJsonObject& preset);
} // namespace huelith::editing
