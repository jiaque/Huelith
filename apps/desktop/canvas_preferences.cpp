#include "canvas_preferences.h"
#include "canvas_view.h"
#include "main_window.h"
#include "raw_dialog.h"
#include "window_header.h"
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace huelith::desktop {
namespace {
class PreferenceEventFilter final : public QObject {
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject*, QEvent* event) override {
        return event->type() == QEvent::ToolTip &&
               !QSettings().value("tools/showTooltips", true).toBool();
    }
};
} // namespace
void configure_interface_scale() {
    const double scales[]{.75,1.,1.25,1.5,2.};
    const int index=std::clamp(QSettings().value("interface/scale",1).toInt(),0,4);
    bool valid=false;const double external=qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&valid);
    qputenv("QT_SCALE_FACTOR",QByteArray::number(scales[index]*(valid && external>0 ? external : 1.)));
}
CanvasPreferences read_canvas_preferences() {
    static QPointer<PreferenceEventFilter> filter;
    if (!filter && qApp) {
        filter = new PreferenceEventFilter(qApp);
        qApp->installEventFilter(filter);
    }
    CanvasPreferences p;
    const QSettings settings;
    p.wheel_zoom = settings.value("navigation/wheelZoom", false).toBool();
    p.center_clicked = settings.value("navigation/centerClicked", false).toBool();
    p.scrubby_zoom = settings.value("navigation/scrubbyZoom", false).toBool();
    p.animated_zoom = settings.value("navigation/animatedZoom", false).toBool();
    p.flick_panning = settings.value("navigation/flickPanning", false).toBool();
    p.overscroll = settings.value("navigation/overscroll", true).toBool();
    p.zoom_all = settings.value("navigation/zoomAll", false).toBool();
    p.pan_all = settings.value("navigation/panAll", false).toBool();
    p.shift_tool_switch = settings.value("tools/shiftSwitch", true).toBool();
    p.spring_tools = settings.value("tools/springLoaded", true).toBool();
    p.spring_delay = std::clamp(settings.value("tools/springDelay", 200).toInt(), 100, 2000);
    p.show_tooltips = settings.value("tools/showTooltips", true).toBool();
    p.history_states = std::clamp(settings.value("history/states", 100).toInt(), 2, 1000);
    p.auto_recovery = settings.value("files/autoRecovery", true).toBool();
    p.recovery_minutes = std::clamp(settings.value("files/recoveryMinutes", 1).toInt(), 1, 60);
    p.recent_files = std::clamp(settings.value("files/recentCount", 12).toInt(), 0, 100);
    p.interface_scale = std::clamp(settings.value("interface/scale",1).toInt(),0,4);
    p.interface_font = std::clamp(settings.value("interface/fontSize", 2).toInt(), 0, 3);
    p.restore_workspace = settings.value("workspace/restoreLast", true).toBool();
    p.lock_workspace = settings.value("workspace/locked", false).toBool();
    p.movable_tabs = settings.value("workspace/movableTabs", true).toBool();
    p.close_tabs = settings.value("workspace/closeTabs", true).toBool();
    p.large_tabs = settings.value("workspace/largeTabs", false).toBool();
    p.raw_backend = std::clamp(settings.value("raw/defaultBackend", 0).toInt(), 0, 1);
    p.raw_denoise = settings.value("raw/defaultDenoise", false).toBool();
    p.raw_high_quality = settings.value("raw/defaultHighQuality", true).toBool();
    p.raw_denoise_amount =
        std::clamp(settings.value("raw/defaultDenoiseAmount", 50).toInt(), 0, 100);
    p.raw_preview_edge = std::clamp(settings.value("raw/previewEdge", 1280).toInt(), 640, 2560);
    p.raw_drag_edge =
        std::clamp(settings.value("raw/dragEdge", 640).toInt(), 320, p.raw_preview_edge);
    p.raw_refine_delay = std::clamp(settings.value("raw/refineDelay", 250).toInt(), 100, 2000);
    p.raw_memory_mb = std::clamp(settings.value("raw/memoryMB", 512).toInt(), 128, 16384);
    p.raw_disk_mb = std::clamp(settings.value("raw/diskMB", 4096).toInt(), 1024, 131072);
    p.painting_cursor = std::clamp(settings.value("cursors/painting", 2).toInt(), 0, 3);
    p.cursor_crosshair = settings.value("cursors/crosshair", false).toBool();
    p.precise_other = settings.value("cursors/preciseOther", false).toBool();
    p.ruler_unit = std::clamp(settings.value("units/rulers", 0).toInt(), 0, 5);
    const double spacing = settings.value("grid/spacing", 100.).toDouble();
    p.grid_spacing = std::isfinite(spacing) ? std::clamp(spacing, 1., 10000.) : 100.;
    p.grid_subdivisions = std::clamp(settings.value("grid/subdivisions", 1).toInt(), 1, 100);
    p.grid_style = std::clamp(settings.value("grid/style", 1).toInt(), 0, 2);
    p.guide_style = std::clamp(settings.value("guides/style", 0).toInt(), 0, 2);
    p.snap_guides = settings.value("snapping/guides", true).toBool();
    p.snap_grid = settings.value("snapping/grid", true).toBool();
    p.snap_layers = settings.value("snapping/layers", true).toBool();
    p.snap_document = settings.value("snapping/document", true).toBool();
    p.snap_distance = std::clamp(settings.value("snapping/distance", 8).toInt(), 1, 32);
    p.checker_size = std::clamp(settings.value("transparency/size", 8).toInt(), 0, 32);
    const auto color = [&](const char* key, QColor fallback) {
        const QColor value(settings.value(QLatin1String(key), fallback.name()).toString());
        return value.isValid() ? value : fallback;
    };
    p.grid_color = color("grid/color", p.grid_color);
    p.guide_color = color("guides/color", p.guide_color);
    p.checker_light = color("transparency/light", p.checker_light);
    p.checker_dark = color("transparency/dark", p.checker_dark);
    p.canvas_color = color("canvas/background", p.canvas_color);
    return p;
}
double ruler_units_per_pixel(int unit, double ppi, double extent) {
    if (!std::isfinite(ppi) || ppi <= 0 || !std::isfinite(extent) || extent <= 0)
        return 1;
    switch (unit) {
    case 1:
        return 1 / ppi;
    case 2:
        return 2.54 / ppi;
    case 3:
        return 25.4 / ppi;
    case 4:
        return 72 / ppi;
    case 5:
        return 100 / extent;
    default:
        return 1;
    }
}
bool show_canvas_preferences(QWidget* parent, int initial_page) {
    auto draft = read_canvas_preferences();
    QDialog dialog(parent);
    dialog.setObjectName("navigationPreferences");
    dialog.setWindowTitle(QObject::tr("首选项"));
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.resize(650, 470);
    auto* layout = new QVBoxLayout(&dialog);
    auto* menu = new QMenuBar(&dialog);
    menu->addAction(QObject::tr("首选项"))->setEnabled(false);
    layout->addWidget(new WindowHeader(menu, &dialog));
    auto* body = new QHBoxLayout;
    auto* categories = new QListWidget(&dialog);
    categories->setObjectName("preferenceCategories");
    categories->addItems(
        {QObject::tr("工具与画面操控"), QObject::tr("光标"), QObject::tr("单位与标尺"),
         QObject::tr("参考线与网格"), QObject::tr("透明度与画布"), QObject::tr("历史记录"),
         QObject::tr("文件处理"), QObject::tr("界面与工作区"), QObject::tr("RAW与性能")});
    categories->setFixedWidth(155);
    auto* pages = new QStackedWidget(&dialog);
    body->addWidget(categories);
    body->addWidget(pages, 1);
    layout->addLayout(body, 1);
    std::vector<std::function<void()>> refresh;
    const auto page = [&] {
        auto* scroll = new QScrollArea(pages);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* widget = new QWidget(scroll);
        scroll->setWidget(widget);
        pages->addWidget(scroll);
        auto* form = new QFormLayout(widget);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        return form;
    };
    const auto check = [&](QFormLayout* form, const char* name, const QString& label, bool& value) {
        auto* widget = new QCheckBox(label, &dialog);
        widget->setObjectName(QLatin1String(name));
        widget->setChecked(value);
        form->addRow(widget);
        QObject::connect(widget, &QCheckBox::toggled, &dialog, [&value](bool v) { value = v; });
        refresh.emplace_back([widget, &value] { widget->setChecked(value); });
    };
    const auto combo = [&](QFormLayout* form, const char* name, const QString& label,
                           const QStringList& options, int& value) {
        auto* widget = new QComboBox(&dialog);
        widget->setObjectName(QLatin1String(name));
        widget->addItems(options);
        widget->setCurrentIndex(value);
        form->addRow(label, widget);
        QObject::connect(widget, &QComboBox::currentIndexChanged, &dialog,
                         [&value](int v) { value = v; });
        refresh.emplace_back([widget, &value] { widget->setCurrentIndex(value); });
    };
    const auto color = [&](QFormLayout* form, const char* name, const QString& label,
                           QColor& value) {
        auto* button = new QPushButton(&dialog);
        button->setObjectName(QLatin1String(name));
        const auto render = [button, &value] {
            button->setText(value.name());
            button->setProperty("selectedColor", value);
            button->setStyleSheet(
                QString("QPushButton {border: 2px solid %1; padding: 6px;}").arg(value.name()));
        };
        render();
        refresh.emplace_back(render);
        form->addRow(label, button);
        QObject::connect(button, &QPushButton::clicked, &dialog, [&, render] {
            const auto chosen = QColorDialog::getColor(value, &dialog, QObject::tr("选择颜色"));
            if (chosen.isValid()) {
                value = chosen;
                render();
            }
        });
    };
    auto* navigation = page();
    check(navigation, "wheelZoomPreference", QObject::tr("使用滚轮缩放"), draft.wheel_zoom);
    check(navigation, "centerZoomPreference", QObject::tr("将缩放点击位置置于中心"),
          draft.center_clicked);
    auto* help =
        new QLabel(QObject::tr("关闭滚轮缩放时，Alt+滚轮缩放；开启时，Alt+"
                               "滚轮平移。\n空格拖动画面；Ctrl+0适合窗口；Ctrl+1显示100%。"),
                   &dialog);
    help->setWordWrap(true);
    navigation->addRow(help);
    check(navigation, "scrubbyZoomPreference", QObject::tr("拖动缩放"), draft.scrubby_zoom);
    check(navigation, "animatedZoomPreference", QObject::tr("动画缩放"), draft.animated_zoom);
    check(navigation, "flickPanningPreference", QObject::tr("启用惯性平移"), draft.flick_panning);
    check(navigation, "overscrollPreference", QObject::tr("允许超出画布边缘平移"),
          draft.overscroll);
    check(navigation, "zoomAllPreference", QObject::tr("缩放所有文档"), draft.zoom_all);
    check(navigation, "panAllPreference", QObject::tr("滚动所有文档"), draft.pan_all);
    check(navigation, "shiftToolSwitchPreference", QObject::tr("使用 Shift 键切换同组工具"),
          draft.shift_tool_switch);
    check(navigation, "springToolsPreference", QObject::tr("按住快捷键临时切换工具"),
          draft.spring_tools);
    auto* spring_delay = new QSpinBox(&dialog);
    spring_delay->setObjectName("springDelayPreference");
    spring_delay->setRange(100, 2000);
    spring_delay->setSuffix(QObject::tr(" 毫秒"));
    spring_delay->setValue(draft.spring_delay);
    navigation->addRow(QObject::tr("临时切换判定时间"), spring_delay);
    QObject::connect(spring_delay, &QSpinBox::valueChanged, &dialog,
                     [&](int value) { draft.spring_delay = value; });
    refresh.emplace_back([&] { spring_delay->setValue(draft.spring_delay); });
    check(navigation, "showTooltipsPreference", QObject::tr("显示工具提示"), draft.show_tooltips);
    auto* cursors = page();
    combo(cursors, "paintingCursorPreference", QObject::tr("绘画光标"),
          {QObject::tr("标准"), QObject::tr("精确"), QObject::tr("正常画笔笔尖"),
           QObject::tr("完整大小画笔笔尖")},
          draft.painting_cursor);
    check(cursors, "cursorCrosshairPreference", QObject::tr("在画笔笔尖中显示十字线"),
          draft.cursor_crosshair);
    check(cursors, "preciseOtherPreference", QObject::tr("其他工具使用精确光标"),
          draft.precise_other);
    cursors->addRow(new QLabel(
        QObject::tr("Caps Lock 切换精确光标。笔尖轮廓随大小、硬度和缩放变化。"), &dialog));
    auto* units = page();
    combo(units, "rulerUnitPreference", QObject::tr("标尺"),
          {QObject::tr("像素"), QObject::tr("英寸"), QObject::tr("厘米"), QObject::tr("毫米"),
           QObject::tr("点"), QObject::tr("百分比")},
          draft.ruler_unit);
    units->addRow(
        new QLabel(QObject::tr("物理单位使用当前文档分辨率换算；右键标尺也可切换单位。"), &dialog));
    auto* grid = page();
    color(grid, "guideColorPreference", QObject::tr("参考线颜色"), draft.guide_color);
    combo(grid, "guideStylePreference", QObject::tr("参考线样式"),
          {QObject::tr("实线"), QObject::tr("虚线"), QObject::tr("点线")}, draft.guide_style);
    color(grid, "gridColorPreference", QObject::tr("网格颜色"), draft.grid_color);
    combo(grid, "gridStylePreference", QObject::tr("网格样式"),
          {QObject::tr("实线"), QObject::tr("虚线"), QObject::tr("点线")}, draft.grid_style);
    auto* spacing = new QDoubleSpinBox(&dialog);
    spacing->setObjectName("gridSpacingPreference");
    spacing->setRange(1, 10000);
    spacing->setSuffix(QObject::tr(" 像素"));
    spacing->setValue(draft.grid_spacing);
    grid->addRow(QObject::tr("网格线间隔"), spacing);
    QObject::connect(spacing, &QDoubleSpinBox::valueChanged, &dialog,
                     [&](double value) { draft.grid_spacing = value; });
    refresh.emplace_back([&] { spacing->setValue(draft.grid_spacing); });
    auto* divisions = new QSpinBox(&dialog);
    divisions->setObjectName("gridSubdivisionsPreference");
    divisions->setRange(1, 100);
    divisions->setValue(draft.grid_subdivisions);
    grid->addRow(QObject::tr("子网格"), divisions);
    QObject::connect(divisions, &QSpinBox::valueChanged, &dialog,
                     [&](int value) { draft.grid_subdivisions = value; });
    refresh.emplace_back([&] { divisions->setValue(draft.grid_subdivisions); });
    check(grid, "snapGuidesPreference", QObject::tr("吸附到参考线"), draft.snap_guides);
    check(grid, "snapGridPreference", QObject::tr("吸附到网格"), draft.snap_grid);
    check(grid, "snapLayersPreference", QObject::tr("吸附到图层"), draft.snap_layers);
    check(grid, "snapDocumentPreference", QObject::tr("吸附到文档边界"), draft.snap_document);
    auto* snap_distance = new QSpinBox(&dialog);
    snap_distance->setObjectName("snapDistancePreference");
    snap_distance->setRange(1, 32);
    snap_distance->setValue(draft.snap_distance);
    snap_distance->setSuffix(QObject::tr(" 屏幕像素"));
    grid->addRow(QObject::tr("吸附距离"), snap_distance);
    QObject::connect(snap_distance, &QSpinBox::valueChanged, &dialog,
                     [&](int value) { draft.snap_distance = value; });
    refresh.emplace_back([&] { snap_distance->setValue(draft.snap_distance); });
    auto* snap_help = new QLabel(
        QObject::tr("移动时按住 Ctrl 临时取消吸附。隐藏的网格、参考线和图层不参与吸附。"), &dialog);
    snap_help->setWordWrap(true);
    grid->addRow(snap_help);
    auto* transparency = page();
    auto* size = new QComboBox(&dialog);
    size->setObjectName("checkerSizePreference");
    const QStringList sizes{QObject::tr("无"), QObject::tr("小"), QObject::tr("中"),
                            QObject::tr("大")};
    const int pixels[]{0, 4, 8, 16};
    for (int i = 0; i < 4; ++i)
        size->addItem(sizes[i], pixels[i]);
    size->setCurrentIndex(std::max(0, size->findData(draft.checker_size)));
    transparency->addRow(QObject::tr("网格大小"), size);
    QObject::connect(size, &QComboBox::currentIndexChanged, &dialog,
                     [&] { draft.checker_size = size->currentData().toInt(); });
    refresh.emplace_back([&] { size->setCurrentIndex(size->findData(draft.checker_size)); });
    color(transparency, "checkerLightPreference", QObject::tr("浅色方格"), draft.checker_light);
    color(transparency, "checkerDarkPreference", QObject::tr("深色方格"), draft.checker_dark);
    color(transparency, "canvasColorPreference", QObject::tr("画布周围"), draft.canvas_color);
    auto* history = page();
    auto* states = new QSpinBox(&dialog);
    states->setObjectName("historyStatesPreference");
    states->setRange(2, 1000);
    states->setValue(draft.history_states);
    history->addRow(QObject::tr("保留的历史状态"), states);
    QObject::connect(states, &QSpinBox::valueChanged, &dialog,
                     [&](int value) { draft.history_states = value; });
    refresh.emplace_back([&] { states->setValue(draft.history_states); });
    auto* history_help = new QLabel(
        QObject::tr("包括当前状态，适用于主编辑器和 "
                    "RAW。减少数量会释放超出限额的撤销记录；不会改变当前图片或保存状态。"),
        &dialog);
    history_help->setWordWrap(true);
    history->addRow(history_help);
    auto* files = page();
    check(files, "autoRecoveryPreference", QObject::tr("自动保存恢复信息"), draft.auto_recovery);
    const auto integer = [&](const char* name, const QString& label, int& value, int minimum,
                             int maximum) {
        auto* spin = new QSpinBox(&dialog);
        spin->setObjectName(QLatin1String(name));
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        files->addRow(label, spin);
        QObject::connect(spin, &QSpinBox::valueChanged, &dialog, [&value](int v) { value = v; });
        refresh.emplace_back([spin, &value] { spin->setValue(value); });
        return spin;
    };
    auto* recovery_interval = integer("recoveryMinutesPreference", QObject::tr("恢复间隔（分钟）"),
                                      draft.recovery_minutes, 1, 60);
    auto* recovery_check = dialog.findChild<QCheckBox*>("autoRecoveryPreference");
    recovery_interval->setEnabled(draft.auto_recovery);
    QObject::connect(recovery_check, &QCheckBox::toggled, recovery_interval, &QWidget::setEnabled);
    integer("recentFilesPreference", QObject::tr("最近文件数量"), draft.recent_files, 0, 100);
    auto* recovery_help = new QLabel(QObject::tr("恢复信息在后台保存，不覆盖原文件，也不改变文档的"
                                                 "未保存状态。关闭文档仍会询问是否保存。"),
                                     &dialog);
    recovery_help->setWordWrap(true);
    files->addRow(recovery_help);
    auto* interface_page = page();
    combo(interface_page, "interfaceScalePreference", QObject::tr("界面缩放（重启生效）"),
          {"75%", "100%", "125%", "150%", "200%"}, draft.interface_scale);
    combo(interface_page, "interfaceFontPreference", QObject::tr("用户界面字体大小"),
          {QObject::tr("极小"), QObject::tr("小"), QObject::tr("中"), QObject::tr("大")},
          draft.interface_font);
    check(interface_page, "restoreWorkspacePreference", QObject::tr("启动时恢复上次工作区"),
          draft.restore_workspace);
    check(interface_page, "lockWorkspacePreference", QObject::tr("锁定工作区面板位置"),
          draft.lock_workspace);
    check(interface_page, "movableTabsPreference", QObject::tr("允许拖动文档标签排序"),
          draft.movable_tabs);
    check(interface_page, "closeTabsPreference", QObject::tr("在文档标签上显示关闭按钮"),
          draft.close_tabs);
    check(interface_page, "largeTabsPreference", QObject::tr("使用大文档标签"), draft.large_tabs);
    auto* interface_help =
        new QLabel(QObject::tr("字体大小立即生效；界面缩放重启生效，并叠加 Windows 每屏 DPI "
                               "设置决定。工作区保存面板布局，不改变图片内容。"),
                   &dialog);
    interface_help->setWordWrap(true);
    interface_page->addRow(interface_help);
    auto* performance = page();
    combo(performance, "rawDefaultBackendPreference", QObject::tr("默认计算设备"),
          {"DirectML", "CPU"}, draft.raw_backend);
    check(performance, "rawDefaultDenoisePreference", QObject::tr("新RAW默认启用AI降噪"),
          draft.raw_denoise);
    check(performance, "rawHighQualityPreference", QObject::tr("高质量去马赛克"),
          draft.raw_high_quality);
    const auto performance_number = [&](const QString& text, const char* name, int low, int high,
                                        int& value, const QString& suffix) {
        auto* control = new QSpinBox(&dialog);
        control->setObjectName(name);
        control->setRange(low, high);
        control->setSuffix(suffix);
        control->setValue(value);
        performance->addRow(text, control);
        QObject::connect(control, &QSpinBox::valueChanged, &dialog, [&value](int v) { value = v; });
        refresh.emplace_back([control, &value] { control->setValue(value); });
        return control;
    };
    performance_number(QObject::tr("默认降噪数量"), "rawDenoiseAmountPreference", 0, 100,
                       draft.raw_denoise_amount, " %");
    auto* preview = performance_number(QObject::tr("常规预览长边"), "rawPreviewEdgePreference", 640,
                                       2560, draft.raw_preview_edge, QObject::tr(" 像素"));
    auto* drag =
        performance_number(QObject::tr("重负载拖动长边"), "rawDragEdgePreference", 320,
                           draft.raw_preview_edge, draft.raw_drag_edge, QObject::tr(" 像素"));
    QObject::connect(preview, &QSpinBox::valueChanged, drag,
                     [drag](int v) { drag->setMaximum(v); });
    performance_number(QObject::tr("100%细化等待"), "rawRefineDelayPreference", 100, 2000,
                       draft.raw_refine_delay, " ms");
    performance_number(QObject::tr("RAW内存缓存"), "rawMemoryPreference", 128, 16384,
                       draft.raw_memory_mb, " MiB");
    performance_number(QObject::tr("RAW磁盘缓存"), "rawDiskPreference", 1024, 131072,
                       draft.raw_disk_mb, " MiB");
    bool clear_raw_cache = false;
    auto* clear = new QPushButton(QObject::tr("清除当前RAW会话缓存"), &dialog);
    clear->setObjectName("rawClearCachePreference");
    performance->addRow(clear);
    QObject::connect(clear, &QPushButton::clicked, &dialog, [&] {
        clear_raw_cache = true;
        clear->setText(QObject::tr("确认首选项后清除"));
    });
    refresh.emplace_back([&] {
        clear_raw_cache = false;
        clear->setText(QObject::tr("清除当前RAW会话缓存"));
    });
    auto* explanation = new QLabel(
        QObject::tr("默认处理只影响新打开的RAW，已有配方保持原值。缓存预算、预览大小在确认后生效；1"
                    "00%及最终输出仍使用完整分辨率。磁盘缓存随会话关闭清除。"),
        &dialog);
    explanation->setWordWrap(true);
    performance->addRow(explanation);
    QObject::connect(categories, &QListWidget::currentRowChanged, pages,
                     &QStackedWidget::setCurrentIndex);
    categories->setCurrentRow(std::clamp(initial_page, 0, 8));
    auto* footer = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                            QDialogButtonBox::RestoreDefaults,
                                        &dialog);
    layout->addWidget(footer);
    QObject::connect(footer, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(footer, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(footer->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
                     &dialog, [&] {
                         draft = {};
                         for (const auto& update : refresh)
                             update();
                     });
    if (dialog.exec() != QDialog::Accepted)
        return false;
    QSettings settings;
    settings.setValue("navigation/wheelZoom", draft.wheel_zoom);
    settings.setValue("navigation/centerClicked", draft.center_clicked);
    settings.setValue("navigation/scrubbyZoom", draft.scrubby_zoom);
    settings.setValue("navigation/animatedZoom", draft.animated_zoom);
    settings.setValue("navigation/flickPanning", draft.flick_panning);
    settings.setValue("navigation/overscroll", draft.overscroll);
    settings.setValue("navigation/zoomAll", draft.zoom_all);
    settings.setValue("navigation/panAll", draft.pan_all);
    settings.setValue("tools/shiftSwitch", draft.shift_tool_switch);
    settings.setValue("tools/springLoaded", draft.spring_tools);
    settings.setValue("tools/springDelay", draft.spring_delay);
    settings.setValue("tools/showTooltips", draft.show_tooltips);
    settings.setValue("history/states", draft.history_states);
    settings.setValue("files/autoRecovery", draft.auto_recovery);
    settings.setValue("files/recoveryMinutes", draft.recovery_minutes);
    settings.setValue("files/recentCount", draft.recent_files);
    settings.setValue("interface/fontSize", draft.interface_font);
    settings.setValue("interface/scale", draft.interface_scale);
    settings.setValue("workspace/restoreLast", draft.restore_workspace);
    settings.setValue("workspace/locked", draft.lock_workspace);
    settings.setValue("workspace/movableTabs", draft.movable_tabs);
    settings.setValue("workspace/closeTabs", draft.close_tabs);
    settings.setValue("workspace/largeTabs", draft.large_tabs);
    settings.setValue("cursors/painting", draft.painting_cursor);
    settings.setValue("cursors/crosshair", draft.cursor_crosshair);
    settings.setValue("cursors/preciseOther", draft.precise_other);
    settings.setValue("units/rulers", draft.ruler_unit);
    settings.setValue("grid/spacing", draft.grid_spacing);
    settings.setValue("grid/subdivisions", draft.grid_subdivisions);
    settings.setValue("grid/style", draft.grid_style);
    settings.setValue("guides/style", draft.guide_style);
    settings.setValue("snapping/guides", draft.snap_guides);
    settings.setValue("snapping/grid", draft.snap_grid);
    settings.setValue("snapping/layers", draft.snap_layers);
    settings.setValue("snapping/document", draft.snap_document);
    settings.setValue("snapping/distance", draft.snap_distance);
    settings.setValue("grid/color", draft.grid_color.name());
    settings.setValue("guides/color", draft.guide_color.name());
    settings.setValue("transparency/size", draft.checker_size);
    settings.setValue("transparency/light", draft.checker_light.name());
    settings.setValue("transparency/dark", draft.checker_dark.name());
    settings.setValue("canvas/background", draft.canvas_color.name());
    settings.setValue("raw/defaultBackend", draft.raw_backend);
    settings.setValue("raw/defaultDenoise", draft.raw_denoise);
    settings.setValue("raw/defaultHighQuality", draft.raw_high_quality);
    settings.setValue("raw/defaultDenoiseAmount", draft.raw_denoise_amount);
    settings.setValue("raw/previewEdge", draft.raw_preview_edge);
    settings.setValue("raw/dragEdge", draft.raw_drag_edge);
    settings.setValue("raw/refineDelay", draft.raw_refine_delay);
    settings.setValue("raw/memoryMB", draft.raw_memory_mb);
    settings.setValue("raw/diskMB", draft.raw_disk_mb);
    settings.sync();
    if (qApp->property("interfaceFontSize").toInt() != draft.interface_font)
        apply_dark_theme();
    for (auto* widget : QApplication::topLevelWidgets())
        if (auto* window = dynamic_cast<MainWindow*>(widget))
            window->apply_workspace_preferences();
    for (auto* widget : QApplication::topLevelWidgets())
        if (auto* raw = dynamic_cast<RawDialog*>(widget))
            raw->apply_performance_preferences(clear_raw_cache);
    for (auto* widget : parent->findChildren<QWidget*>())
        if (auto* canvas = dynamic_cast<CanvasView*>(widget))
            canvas->apply_preferences();
    return true;
}
} // namespace huelith::desktop
