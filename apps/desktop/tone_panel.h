#pragma once
#include "huelith/editing/tone.h"
#include <QWidget>
#include <functional>
class QStackedWidget;
class QListWidget;
namespace huelith::desktop {
class TonePanel final : public QWidget {
  public:
    explicit TonePanel(editing::ToneSettings settings, QWidget* parent = nullptr);
    [[nodiscard]] const editing::ToneSettings& settings() const {
        return settings_;
    }
    void set_category(int index);
    void set_settings(editing::ToneSettings settings);
    void use_accordion();
    std::function<void()> changed;

  private:
    editing::ToneSettings settings_;
    std::function<void()> refresh_extra_;
    QListWidget* categories_{};
    QStackedWidget* pages_{};
};
} // namespace huelith::desktop
