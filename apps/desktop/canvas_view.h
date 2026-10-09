#pragma once

#include "huelith/document/canvas_spec.h"
#include <QWidget>

namespace huelith::desktop {
class CanvasView final : public QWidget {
public:
    explicit CanvasView(CanvasSpec spec, QWidget* parent = nullptr);
    [[nodiscard]] const CanvasSpec& spec() const noexcept;
    [[nodiscard]] double zoom() const noexcept;
    void set_zoom(double zoom);
    void fit_to_view();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    CanvasSpec spec_;
    double zoom_{1.0};
    bool fit_mode_{true};
};
} // namespace huelith::desktop
