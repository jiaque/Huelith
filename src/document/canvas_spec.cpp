#include "huelith/document/canvas_spec.h"
#include <cmath>

namespace huelith {
std::optional<CanvasSpecError> validate(const CanvasSpec& spec) noexcept {
    if (spec.name.find_first_not_of(" \t\r\n\f\v") == std::string::npos) {
        return CanvasSpecError::empty_name;
    }
    if (spec.width < 1 || spec.height < 1 || spec.width > max_canvas_dimension ||
        spec.height > max_canvas_dimension || std::int64_t(spec.width) * spec.height > 512'000'000) {
        return CanvasSpecError::invalid_dimensions;
    }
    if (!std::isfinite(spec.resolution_ppi) || spec.resolution_ppi <= 0.0) {
        return CanvasSpecError::invalid_resolution;
    }
    if ((spec.background != CanvasBackground::transparent &&
         spec.background != CanvasBackground::white &&
         spec.background != CanvasBackground::custom) ||
        spec.working_space != WorkingSpace::linear_rec2020 || spec.color_mode < 0 || spec.color_mode > 3 ||
        (spec.color_bits != 8 && spec.color_bits != 16 && spec.color_bits != 32) ||
        spec.color_profile < 0 || spec.color_profile > 3 || spec.icc_profile.size() > 4*1024*1024 ||
        (spec.color_mode == 3 && (spec.icc_profile.empty() || spec.color_bits != 8))) {
        return CanvasSpecError::invalid_enum;
    }
    return std::nullopt;
}
} // namespace huelith
