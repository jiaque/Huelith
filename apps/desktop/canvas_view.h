#pragma once
#include "huelith/document/canvas_spec.h"
#include "huelith/editing/project.h"
#include <QWidget>
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace huelith::desktop {
enum class CanvasTool {
    move,
    hand,
    rectangle,
    ellipse,
    lasso,
    polygon,
    pen,
    brush,
    eraser,
    wand,
    color_range
};
class CanvasView final : public QWidget {
  public:
    explicit CanvasView(CanvasSpec spec, QWidget* parent = nullptr);
    [[nodiscard]] const CanvasSpec& spec() const noexcept {
        return spec_;
    }
    [[nodiscard]] double zoom() const noexcept {
        return zoom_;
    }
    void set_zoom(double zoom);
    void fit_to_view();
    [[nodiscard]] editing::Project& project() {
        return *project_;
    }
    [[nodiscard]] const editing::Project& project() const {
        return *project_;
    }
    void load_state(editing::State state, bool saved);
    void edit(const QString& name, const std::function<void(editing::State&)>& operation);
    void refresh();
    void preview(const editing::State& state);
    void set_tool(CanvasTool tool);
    [[nodiscard]] CanvasTool tool() const noexcept {
        return tool_;
    }
    bool show_transform_controls{true};
    bool keep_aspect{true};
    bool auto_select{true};
    bool begin_transform();
    void commit_transform();
    void cancel_transform();
    [[nodiscard]] bool transforming() const noexcept {
        return transform_draft_.has_value();
    }
    [[nodiscard]] std::array<QPointF, 8> transform_handles() const;
    [[nodiscard]] QTransform current_layer_transform() const;
    void prepare_inserted_layer();
    std::function<void()> tool_changed;

    [[nodiscard]] QTransform view_transform() const;
    [[nodiscard]] QPointF image_point(QPointF widget_point) const;
    QUuid active_layer;
    bool mask_target{};
    bool mask_view{};
    bool show_grid{};
    bool show_rulers{true};
    bool snap{true};
    double rotation{};
    double brush_radius{20};
    QColor foreground{Qt::black};
    QColor background{Qt::white};
    editing::RangeOp range_operation{editing::RangeOp::replace};
    QPainterPath path;
    QString filename;
    QString recovery_file;
    QString comparison;
    std::function<void()> changed;

  protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;

  private:
    QRectF target_rect() const;
    QTransform parent_transform() const;
    const editing::State& visible_state() const;
    int transform_hit(QPointF point) const;
    void transform_motion(QPointF point, Qt::KeyboardModifiers modifiers);
    std::optional<editing::State> transform_draft_;
    QUuid transform_target_;
    bool transform_mask_{};
    quint64 transform_revision_{};
    int transform_handle_{-1};
    QTransform drag_transform_;
    QTransform drag_mask_transform_;
    QPointF transform_press_;
    bool transform_modified_{};
    bool move_gesture_{};
    void finish_path();
    void apply_stroke();
    CanvasSpec spec_;
    std::unique_ptr<editing::Project> project_;
    QImage preview_;
    QImage linear_preview_;
    QString monitor_name_;
    QColorSpace monitor_space_{QColorSpace::SRgb};
    CanvasTool tool_{CanvasTool::move};
    double zoom_{1.0};
    bool fit_mode_{true};
    QPointF pan_;
    QPointF press_;
    QPointF last_;
    QPainterPath stroke_;
    QPainterPath path_before_;
    int node_index_{-1};
    int guide_axis_{};
    bool dragging_{};
    bool temporary_hand_{};
    bool pan_gesture_{};
    editing::RangeOp gesture_range_{editing::RangeOp::replace};
};
} // namespace huelith::desktop
