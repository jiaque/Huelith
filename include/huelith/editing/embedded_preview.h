#pragma once
#include "huelith/editing/project.h"
namespace huelith::editing {
struct EmbeddedPreview {QImage image;int bits{};QString description;};
[[nodiscard]] EmbeddedPreview embedded_preview(const Layer& source);
[[nodiscard]] double preview_exposure_reference(const QImage& developed,const QImage& preview);
}
