#pragma once
#include "huelith/imaging/float_image.h"
#include <memory>
#include <vector>

namespace huelith {
[[nodiscard]] bool valid_exposure(double stops) noexcept;

// UI-thread-owned parameter history; pixel data is immutable and shared by jobs.
class PhotoDocument final {
public:
    explicit PhotoDocument(std::shared_ptr<const FloatImage> source);
    [[nodiscard]] const std::shared_ptr<const FloatImage>& source() const noexcept;
    [[nodiscard]] double exposure() const noexcept;
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] double undo_value() const noexcept;
    [[nodiscard]] double redo_value() const noexcept;
    bool set_exposure(double stops);
    bool undo() noexcept;
    bool redo() noexcept;
private:
    std::shared_ptr<const FloatImage> source_;
    std::vector<double> history_{0.0};
    std::size_t position_{};
};
} // namespace huelith
