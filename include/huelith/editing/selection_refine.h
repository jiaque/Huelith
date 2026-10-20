#pragma once
#include <QImage>
#include <atomic>
namespace huelith::editing {
struct SelectionRefineSettings {
    double radius{};
    bool smart_radius{};
    double smooth{};
    double feather{};
    double contrast{};
    double shift{};
    bool invert{};
    double decontaminate{};
};
struct RefinedSelection {
    QImage mask;
    QImage pixels; // Only allocated when decontaminating; source alpha is retained.
};
QImage selection_edge_band(const QImage& mask, int radius,
                           const std::atomic_bool* cancelled = nullptr);
RefinedSelection refine_selection(const QImage& image, const QImage& mask,
                                  const SelectionRefineSettings& settings,
                                  const QImage& refine_area = {},
                                  const std::atomic_bool* cancelled = nullptr,
                                  double pixel_scale = 1);
} // namespace huelith::editing
