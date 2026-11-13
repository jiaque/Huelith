#pragma once
#include "huelith/editing/project.h"
namespace huelith::editing {
State read_photoshop(const QString& path, QStringList& warnings, bool mergedOnly = false);
void write_photoshop(const State& state, const QString& path);
} // namespace huelith::editing
