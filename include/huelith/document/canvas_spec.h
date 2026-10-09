#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace huelith {

enum class CanvasBackground { transparent, white };
// Explicit prototype working space. Storage precision is independent of primaries.
enum class WorkingSpace { linear_srgb };

struct CanvasSpec {
    std::string name{"Untitled"}; // UTF-8; UI handles translated default names.
    std::int32_t width{1920};
    std::int32_t height{1080};
    double resolution_ppi{300.0};
    CanvasBackground background{CanvasBackground::transparent};
    WorkingSpace working_space{WorkingSpace::linear_srgb};
};

enum class CanvasSpecError { empty_name, invalid_dimensions, invalid_resolution, invalid_enum };
inline constexpr std::int32_t max_canvas_dimension{65536};

[[nodiscard]] std::optional<CanvasSpecError> validate(const CanvasSpec& spec) noexcept;

} // namespace huelith
