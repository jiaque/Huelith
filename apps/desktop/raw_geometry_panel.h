#pragma once
#include "huelith/editing/raw_geometry.h"
#include <QWidget>
#include <functional>
#include <vector>
class QDoubleSpinBox;
class QCheckBox;
class QAbstractButton;
namespace huelith::desktop {
class RawGeometryPanel final : public QWidget {
  public:
    explicit RawGeometryPanel(QWidget* parent);
    void set_settings(editing::RawGeometry settings);
    const editing::RawGeometry& settings() const {
        return settings_;
    }
    std::function<void()> changed, gesture_started, gesture_finished;

  private:
    editing::RawGeometry settings_;
    bool loading_{};
    std::vector<std::pair<QDoubleSpinBox*, double editing::RawGeometry::*>> numbers_;
    std::vector<std::pair<QAbstractButton*, bool editing::RawGeometry::*>> flags_;
};
} // namespace huelith::desktop
