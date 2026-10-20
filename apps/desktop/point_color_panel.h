#pragma once
#include "huelith/editing/tone.h"
#include <QWidget>
#include <functional>
#include <optional>
class QCheckBox;
class QDoubleSpinBox;
class QTabBar;
class QToolButton;
class QLabel;
namespace huelith::desktop {
class PointRange;
class PointField;
class PointColorPanel final : public QWidget {
  public:
    PointColorPanel(editing::ToneSettings& settings, QWidget* parent);
    void refresh();
    bool sample(QColor color);
    bool sampling() const;
    void stop_sampling();
    std::optional<editing::PointColor> visualized() const;
    std::function<void()> changed;
    std::function<void()> preview_changed;

  private:
    void notify();
    editing::PointColor* current();
    editing::ToneSettings& settings_;
    QTabBar* swatches_{};
    QToolButton* picker_{};
    QCheckBox* visualize_{};
    QWidget* controls_{};
    QLabel* status_{};
    std::array<QDoubleSpinBox*, 3> shifts_{};
    QDoubleSpinBox* amount_{};
    std::array<PointRange*, 3> ranges_{};
    PointField* field_{};
    PointField* light_{};
    QLabel* comparison_{};
};
} // namespace huelith::desktop
