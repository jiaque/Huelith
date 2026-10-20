#pragma once
#include <QColor>
class QWidget;
namespace huelith::desktop {
struct CanvasPreferences {
    bool wheel_zoom{};
    bool center_clicked{};
    bool scrubby_zoom{};
    bool animated_zoom{};
    bool flick_panning{};
    bool overscroll{true};
    bool zoom_all{};
    bool pan_all{};
    bool shift_tool_switch{true};
    bool spring_tools{true};
    int spring_delay{200};
    bool show_tooltips{true};
    int history_states{100};
    bool auto_recovery{true};
    int recovery_minutes{1};
    int recent_files{12};
    int interface_scale{1}; // 75%, 100%, 125%, 150%, 200%; applied at startup.
    int interface_font{2}; // Tiny, small, medium, large; Qt logical pixel sizes.
    bool restore_workspace{true};
    bool lock_workspace{};
    bool movable_tabs{true};
    bool close_tabs{true};
    bool large_tabs{};
    int raw_backend{}; // DirectML, CPU; defaults apply only to newly opened RAW sources.
    bool raw_denoise{};
    bool raw_high_quality{true};
    int raw_denoise_amount{50};
    int raw_preview_edge{1280};
    int raw_drag_edge{640};
    int raw_refine_delay{250};
    int raw_memory_mb{512};
    int raw_disk_mb{4096};
    int painting_cursor{2}; // Standard, precise, 50% coverage, full tip.
    bool cursor_crosshair{};
    bool precise_other{};
    int ruler_unit{}; // Pixels, inches, cm, mm, points, percent.
    double grid_spacing{100};
    int grid_subdivisions{1};
    int grid_style{1};
    int guide_style{};
    bool snap_guides{true};
    bool snap_grid{true};
    bool snap_layers{true};
    bool snap_document{true};
    int snap_distance{8}; // Screen pixels, independent of document zoom.
    QColor grid_color{"#566473"};
    QColor guide_color{"#43c9f7"};
    int checker_size{8};
    QColor checker_light{120, 120, 120};
    QColor checker_dark{98, 98, 98};
    QColor canvas_color{"#202428"};
    bool operator==(const CanvasPreferences&) const = default;
};
void configure_interface_scale();
[[nodiscard]] CanvasPreferences read_canvas_preferences();
[[nodiscard]] double ruler_units_per_pixel(int unit, double ppi, double extent);
// Dialog works on a draft; only OK writes settings and refreshes open canvases.
bool show_canvas_preferences(QWidget* parent, int initial_page = 0);
} // namespace huelith::desktop
