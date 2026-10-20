#include "huelith/editing/photo_document.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace huelith {
bool valid_exposure(double stops) noexcept {
    return std::isfinite(stops) && stops >= -5.0 && stops <= 5.0;
}

PhotoDocument::PhotoDocument(std::shared_ptr<const FloatImage> source) : source_(std::move(source)) {
    if (!source_) { throw std::invalid_argument("Photo document requires a source"); }
}

const std::shared_ptr<const FloatImage>& PhotoDocument::source() const noexcept { return source_; }
double PhotoDocument::exposure() const noexcept { return history_[position_]; }
bool PhotoDocument::can_undo() const noexcept { return position_ > 0; }
bool PhotoDocument::can_redo() const noexcept { return position_ + 1 < history_.size(); }
double PhotoDocument::undo_value() const noexcept { return can_undo() ? history_[position_ - 1] : exposure(); }
double PhotoDocument::redo_value() const noexcept { return can_redo() ? history_[position_ + 1] : exposure(); }

bool PhotoDocument::set_exposure(double stops) {
    if (!valid_exposure(stops)) { throw std::invalid_argument("Exposure must be between -5 and +5 EV"); }
    if (stops == exposure()) { return false; }
    // Allocate before changing history so failure leaves both undo and redo intact.
    auto next = std::vector<double>(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(position_ + 1));
    next.push_back(stops);
    history_.swap(next);
    ++position_;
    return true;
}

bool PhotoDocument::undo() noexcept {
    if (!can_undo()) { return false; }
    --position_;
    return true;
}
bool PhotoDocument::redo() noexcept {
    if (!can_redo()) { return false; }
    ++position_;
    return true;
}
} // namespace huelith
