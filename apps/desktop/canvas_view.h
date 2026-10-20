#pragma once
#include "huelith/document/canvas_spec.h"
#include "huelith/editing/brush.h"
#include "huelith/editing/photo_operations.h"
#include "huelith/editing/project.h"
#include <QElapsedTimer>
#include <QTextCharFormat>
#include <QWidget>
#include <array>
#include <functional>
#include <memory>
#include <optional>
class QTextCharFormat;
class QTimer;
class QPainter;

namespace huelith::desktop {
class TextEditView;
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
    color_range,
    zoom,
    path_select,
    history_brush,
    bucket,
    eyedropper,
    clone,
    heal,
    spot,
    blur,
    sharpen,
    dodge,
    burn,
    sponge,
    crop,
    text,
    shape,
    patch,
    object_selection
};
class CanvasView final : public QWidget {
  public:
    explicit CanvasView(CanvasSpec spec, QWidget* parent = nullptr);
    ~CanvasView() override;
    [[nodiscard]] const CanvasSpec& spec() const noexcept {
        return spec_;
    }
    [[nodiscard]] double zoom() const noexcept {
        return zoom_;
    }
    void set_zoom(double zoom);
    void fit_to_view();
    void navigation_sync(bool zoom, bool pan);
    [[nodiscard]] editing::Project& project() {
        return *project_;
    }
    [[nodiscard]] const editing::Project& project() const {
        return *project_;
    }
    void load_state(editing::State state, bool saved);
    void edit(const QString& name, const std::function<void(editing::State&)>& operation);
    void refresh(bool update_pixels = true);
    void preview(const editing::State& state);
    [[nodiscard]] bool preview_pending() const;
    [[nodiscard]] const QImage& rendered_preview() const {
        return preview_;
    }
    void set_tool(CanvasTool tool);
    bool object_all_layers{true};
    bool object_show_all{};
    int object_mode{}; // Rectangle or lasso.
    void update_object_options();
    void begin_text(QRectF box, bool paragraph, bool existing = false);
    void finish_text(bool accept);
    void format_text(const QTextCharFormat& format);
    [[nodiscard]] QTextCharFormat current_text_format() const;
    void align_text(Qt::Alignment alignment);
    void text_line_height(double percent);
    [[nodiscard]] bool editing_text() const {
        return text_editor_ != nullptr;
    }
    void create_shape(QRectF box, bool reverse_line = false);
    editing::ShapeContent shape_options;
    QTextCharFormat text_defaults;
    int pen_mode{}; // Draw, insert anchor, delete anchor, convert anchor.
    void bind_shape_path();
    void apply_preferences();
    [[nodiscard]] CanvasTool tool() const noexcept {
        return tool_;
    }
    bool show_transform_controls{true};
    bool warp_mode{};
    [[nodiscard]] std::array<QPointF, 9> warp_handles() const;
    [[nodiscard]] const editing::State& display_state() const {
        return brush_draft_ ? *brush_draft_ : visible_state();
    }
    bool keep_aspect{true};
    bool auto_select{true};
    bool begin_transform(bool selection = false);
    void stage_layer(editing::Layer layer);
    std::vector<QUuid> selected_layers;
    bool zoom_out{};
    double hit_tolerance{3};
    int color_tolerance{32};
    bool contiguous{true};
    bool antialias{true};
    double brush_hardness{.8};
    double brush_opacity{1};
    double brush_flow{1};
    [[nodiscard]] editing::BrushSettings brush_settings() const;
    void set_brush_settings(const editing::BrushSettings& settings);
    bool pressure_enabled{true};
    QImage history_source;
    double grid_spacing{100};
    [[nodiscard]] int ants_phase() const noexcept {
        return ants_phase_;
    }

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
    bool show_guides{true};
    bool lock_guides{};
    bool show_rulers{true};
    bool snap{true};
    [[nodiscard]] QPointF snapping_offset(QRectF bounds, bool moving = false) const;
    double rotation{};
    double brush_radius{20};
    editing::RetouchOptions photo_brush;
    double retouch_strength{.5};
    bool clone_aligned{true};
    bool clone_overlay{true};
    double clone_overlay_opacity{.5};
    bool patch_destination{};
    double patch_color_adaptation{1};
    bool sample_all_layers{};
    bool quick_mask{};
    void toggle_quick_mask();
    [[nodiscard]] bool painting_tool() const;
    [[nodiscard]] bool retouch_tool() const;
    [[nodiscard]] bool cropping() const {
        return crop_bounds_.has_value();
    }
    void commit_crop();
    void cancel_crop();
    double crop_ratio{}, crop_angle{};
    bool crop_delete_pixels{true};
    bool crop_straighten{};
    void set_crop_ratio(double value);
    [[nodiscard]] std::array<QPointF, 8> crop_handles() const;
    [[nodiscard]] QRectF crop_rectangle() const {
        return crop_bounds_.value_or(QRectF{});
    }
    QColor foreground{Qt::black};
    QColor background{Qt::white};
    editing::RangeOp range_operation{editing::RangeOp::replace};
    QPainterPath path;
    void set_path(QPainterPath value);
    QString filename;
    QString recovery_file;
    QString comparison;
    std::function<void()> changed;

  protected:
    bool event(QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void tabletEvent(QTabletEvent*) override;

  private:
    TextEditView* text_editor_{};
    quint64 text_revision_{};
    bool text_existing_{};
    bool design_drag_{};
    QRectF design_rect_;
    QUuid path_layer_;
    void update_shape_path(bool commit);
    bool insert_pen_node(QPointF point);
    std::optional<CanvasTool> shortcut_tool(QKeyEvent*) const;
    std::optional<CanvasTool> spring_original_;
    QElapsedTimer spring_timer_;
    quint64 spring_timestamp_{};
    int spring_key_{};
    std::array<CanvasTool, 5> group_tools_{CanvasTool::rectangle, CanvasTool::lasso,
                                           CanvasTool::wand, CanvasTool::spot, CanvasTool::dodge};
    void update_tool_cursor();
    bool precise_cursor_{};
    bool photo_press(QPointF point, Qt::KeyboardModifiers modifiers);
    void patch_move(QPointF point);
    void patch_finish(QPointF point);
    void patch_cancel();
    std::optional<editing::State> patch_base_, patch_draft_;
    QPointF patch_offset_;
    bool patch_selecting_{};
    void photo_preview();
    std::optional<QPointF> clone_source_;
    std::optional<QPointF> clone_offset_;
    QImage clone_preview_;
    QSize clone_preview_size_;
    QImage photo_sample_;
    quint64 photo_sample_revision_{};
    QUuid photo_sample_layer_;
    editing::SampleLayers photo_sample_scope_{};
    std::optional<QRectF> crop_bounds_;
    QRectF crop_before_;
    int crop_handle_{-1};
    double crop_angle_before_{};
    void crop_motion(QPointF point, Qt::KeyboardModifiers modifiers);
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
    bool selection_transform_{};
    bool placement_{};
    QRectF transform_rect_;
    QTransform transform_frame_, drag_frame_;
    editing::State drag_state_;
    std::vector<QUuid> transform_ids_;
    void start_transform_drag(int handle, QPointF position);
    void apply_transform_delta(const QTransform& delta);
    void update_selection_outline();
    QPainterPath selection_outline_;
    qint64 selection_cache_key_{};
    qint64 quick_mask_cache_key_{};
    QImage quick_mask_cache_;
    int ants_phase_{};
    int guide_index_{-1};
    QPointF cursor_position_;
    double pressure_{1};
    QImage stroke_coverage_;
    editing::BrushSettings advanced_brush_;
    editing::BrushStroke brush_stroke_;
    QElapsedTimer brush_clock_;
    std::uint32_t brush_seed_{1};
    std::optional<editing::State> stroke_base_;
    void stamp_brush(QPointF from, QPointF to);
    void update_brush_preview();
    struct PenNode {
        QPointF anchor, incoming, outgoing;
    };
    std::vector<PenNode> pen_nodes_;
    bool pen_closed_{};
    int pen_drag_node_{-1};
    int pen_drag_control_{};
    void rebuild_pen();
    std::optional<editing::State> brush_draft_;
    quint64 brush_revision_{};

    void finish_path();
    void apply_stroke();
    CanvasSpec spec_;
    std::unique_ptr<editing::Project> project_;
    QImage preview_;
    QImage linear_preview_;
    struct RenderWork;
    std::shared_ptr<RenderWork> render_work_;
    void request_preview(const editing::State& state);
    void reset_preview(const QImage& linear);
    void poll_preview();
    bool require_current_preview();
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
    void initialize_navigation();
    void constrain_pan();
    void navigate_zoom(double scale, QPointF image_anchor, QPointF screen_anchor, bool animate);
    void navigation_tick();
    void pan_motion(QPointF position);
    void finish_pan();
    QTimer* navigation_timer_{};
    QElapsedTimer navigation_clock_, pan_clock_;
    QPointF pan_velocity_;
    QPointF zoom_anchor_, zoom_screen_;
    double zoom_start_{1}, zoom_target_{1};
    bool zoom_animating_{};
    bool scrub_gesture_{};
    bool scrub_moved_{};
    bool scrub_out_{};
    double scrub_scale_{1};
    Qt::KeyboardModifiers selection_start_modifiers_{};
    editing::RangeOp gesture_range_{editing::RangeOp::replace};
    struct ObjectSelectionState;
    std::shared_ptr<ObjectSelectionState> objects_;
    void object_poll();
    void object_press(QPointF point, Qt::KeyboardModifiers modifiers);
    void object_move(QPointF point);
    void object_finish(QPointF point);
    void object_cancel();
    void object_paint(QPainter& painter);
};
} // namespace huelith::desktop
