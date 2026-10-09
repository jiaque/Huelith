#include "huelith/document/canvas_spec.h"
#include <cmath>

namespace huelith {
std::optional<CanvasSpecError> validate(const CanvasSpec& spec) noexcept {
    if (spec.name.find_first_not_of(" \t\r\n\f\v") == std::string::npos) {
        return CanvasSpecError::empty_name;
    }
    if (spec.width < 1 || spec.height < 1 || spec.width > max_canvas_dimension ||
        spec.height > max_canvas_dimension) {
        return CanvasSpecError::invalid_dimensions;
    }
    if (!std::isfinite(spec.resolution_ppi) || spec.resolution_ppi <= 0.0) {
        return CanvasSpecError::invalid_resolution;
    }
    if ((spec.background != CanvasBackground::transparent &&
         spec.background != CanvasBackground::white) ||
        spec.working_space != WorkingSpace::linear_srgb) {
        return CanvasSpecError::invalid_enum;
    }
    return std::nullopt;
}
} // namespace huelith
