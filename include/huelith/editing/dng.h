#pragma once
#include "huelith/editing/project.h"
namespace huelith::editing {
void save_raw_dng(const Layer& source, const QString& path, const std::atomic_bool* cancelled = nullptr);
QJsonObject dng_recipe(const QByteArray& bytes);
QJsonObject raw_sensor_info(const QByteArray& bytes);
}
