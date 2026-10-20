#pragma once
#include "huelith/editing/white_balance.h"
#include <QWidget>
#include <functional>
class QComboBox;
class QDoubleSpinBox;
class QToolButton;
namespace huelith::desktop {
class RawWhiteBalancePanel final : public QWidget {
  public:
    explicit RawWhiteBalancePanel(QWidget* parent);
    void set_settings(editing::WhiteBalance value, editing::WhiteBalance shot, bool available);
    editing::WhiteBalance settings() const {
        return settings_;
    }
    bool sampling() const;
    void stop_sampling();
    std::function<void()> changed, view_changed, automatic, gesture_started, gesture_finished;

  private:
    void refresh();
    editing::WhiteBalance settings_, shot_;
    QComboBox* mode_{};
    QDoubleSpinBox *temperature_{}, *tint_{};
    QToolButton* picker_{};
};
} // namespace huelith::desktop
