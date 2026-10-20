#pragma once
#include "huelith/editing/project.h"
namespace huelith::editing {
QString raw_sidecar_path(const QString& source_path);
QJsonObject load_raw_sidecar(const Layer& source);
void save_raw_sidecars(const std::vector<Layer>& sources,
                       const std::atomic_bool* cancelled = nullptr);
} // namespace huelith::editing
