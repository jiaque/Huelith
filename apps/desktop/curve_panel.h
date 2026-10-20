#pragma once
#include "huelith/editing/tone.h"
#include <QWidget>
#include <functional>
namespace huelith::desktop {
class CurvePanel final : public QWidget {
  public:
    CurvePanel(editing::ToneSettings& settings, std::function<void()> changed, QWidget* parent);
    void refresh();
    void histogram(const QImage& image);

  private:
    std::function<void()> refresh_;
    std::function<void(const QImage&)> histogram_;
};
} // namespace huelith::desktop
