#pragma once
#include "huelith/editing/tone.h"
#include <QWidget>
#include <functional>
#include <optional>
class QStackedWidget;
class QListWidget;
namespace huelith::desktop {
class PointColorPanel;
class CurvePanel;
class TonePanel final : public QWidget {
  public:
    explicit TonePanel(editing::ToneSettings settings, QWidget* parent = nullptr);
    [[nodiscard]] const editing::ToneSettings& settings() const {
        return settings_;
    }
    void set_category(int index);
    void set_settings(editing::ToneSettings settings);
    void use_accordion();
    void set_curve_histogram(const QImage& image);
    int target_mode() const {
        return target_mode_;
    }
    void set_target_mode(int mode);
    bool begin_target(QColor color);
    void move_target(double amount);
    void finish_target(bool cancel = false);
    bool target_dragging() const {
        return target_base_.has_value();
    }
    bool point_color_sampling() const;
    void sample_point_color(QColor color);
    void stop_point_sampling();
    std::optional<editing::PointColor> point_color_visualized() const;
    std::function<void()> changed;
    std::function<void()> point_view_changed;
    std::function<void()> detail_view_changed;
    int detail_preview() const {
        return detail_preview_;
    }

  private:
    editing::ToneSettings settings_;
    std::function<void()> refresh_extra_;
    std::function<void()> refresh_hsl_;
    std::function<void()> refresh_details_;
    std::function<void()> refresh_film_;
    std::function<void()> refresh_grading_;
    QListWidget* categories_{};
    QStackedWidget* pages_{};
    PointColorPanel* point_colors_{};
    CurvePanel* curves_{};
    int detail_preview_{};
    int target_mode_{-1};
    std::optional<editing::ToneSettings> target_base_;
    std::array<double, 8> target_weights_{};
};
} // namespace huelith::desktop
