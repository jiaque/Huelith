#include "work_window.h"
#include "raw_dialog.h"
#include "huelith/editing/embedded_preview.h"
#include <QMessageBox>
#include "export_dialog.h"
#include "huelith/editing/raw_sidecar.h"
#include "huelith/editing/dng.h"
#include "canvas_preferences.h"
#include "huelith/editing/raw_adjustments.h"
#include "huelith/editing/raw_recipe.h"
#include "raw_geometry_panel.h"
#include "raw_optics_panel.h"
#include "raw_preview_view.h"
#include "raw_white_balance_panel.h"
#include "tone_panel.h"
#include "window_header.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QApplication>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSvgRenderer>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace huelith::desktop {
struct RawDialog::Job {
    std::atomic_bool cancelled{};
    std::atomic_int percentage{};
    int index{};
    QString key;
    std::future<editing::RawResult> result;
};
struct RawDialog::PreviewJob {
    std::atomic_bool cancelled{};
    quint64 revision{};
    QString key;
    bool comparison{};
    bool full_resolution{};
    int index{};
    int target{-1};
    std::future<std::array<QImage, 7>> result;
};
struct RawDialog::CommitJob {
    std::atomic_bool cancelled{};
    std::future<std::vector<editing::Layer>> result;
};
namespace {
QString bundled_model() {
    const auto path = QCoreApplication::applicationDirPath() + "/models/model_bayer.onnx";
    if (QFileInfo::exists(path))
        return path;
    // The Python validation host uses the same workspace package, not its own interpreter
    // directory.
    const auto development =
        QDir::current().absoluteFilePath("build/windows-release/models/model_bayer.onnx");
    return QFileInfo::exists(development) ? development : path;
}
} // namespace
RawDialog::RawDialog(std::vector<editing::Layer> sources, QWidget* parent)
    : QDialog(parent), sources_(std::move(sources)), cache_keys_(sources_.size()) {
    if (sources_.empty())
        throw editing::EditError(QStringLiteral("RAW 会话没有源文件"));
    for(auto& source:sources_)
        if(source.raw_recipe.isEmpty() && source.pixels.isNull())
            source.raw_recipe=editing::load_raw_sidecar(source);
    const auto preferences = read_canvas_preferences();
    cache_.configure(qint64(preferences.raw_memory_mb) * 1024 * 1024,
                     qint64(preferences.raw_disk_mb) * 1024 * 1024);
    for (auto& source : sources_)
        if (source.raw_recipe.isEmpty()) {
            source.raw_recipe.insert("denoise", preferences.raw_denoise);
            source.raw_recipe.insert("denoiseAmount", preferences.raw_denoise_amount);
            source.raw_recipe.insert("directml", preferences.raw_backend == 0);
            source.raw_recipe.insert("demosaic",
                                     preferences.raw_high_quality ? "malvar-2004" : "bilinear");
        }
    for (auto& source : sources_)
        source.raw_recipe.insert(
            "demosaic", editing::raw_high_quality(source.raw_recipe) ? "malvar-2004" : "bilinear");
    setObjectName("rawDialog");
    setWindowTitle(tr("RAW 显影"));
    setStyleSheet("QDialog#rawDialog { background: #292d31; } "
                  "QToolButton { border: 0; border-bottom: 1px solid #24272b; padding: 3px; } "
                  "QToolButton:checked { background: #363c42; } "
                  "QDoubleSpinBox { min-height: 18px; max-height: 22px; padding: 0px 3px; } "
                  "QSlider { min-height: 14px; max-height: 18px; } "
                  "QScrollArea { border: 0; }");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    refine_timer_ = new QTimer(this);
    refine_timer_->setSingleShot(true);
    refine_timer_->setInterval(preferences.raw_refine_delay);
    connect(refine_timer_, &QTimer::timeout, this, [this] {
        if (!detail_preview_ || closing_ || commit_job_)
            return;
        full_preview_requested_ = true;
        ++preview_revision_;
        setProperty("previewRequested", QVariant::fromValue(preview_revision_));
        preview_pending_ = true;
        preview_timer_->start();
    });
    history_timer_ = new QTimer(this);
    history_timer_->setSingleShot(true);
    history_timer_->setInterval(250);
    connect(history_timer_, &QTimer::timeout, this, &RawDialog::record_history);
    preview_timer_ = new QTimer(this);
    preview_timer_->setInterval(16);
    connect(preview_timer_, &QTimer::timeout, this, &RawDialog::finish_preview);
    commit_timer_ = new QTimer(this);
    commit_timer_->setInterval(30);
    connect(commit_timer_, &QTimer::timeout, this, &RawDialog::finish_commit);
    setMinimumSize(1040, 680);
    resize(1440, 900);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    auto* menu = new QMenuBar;
    menu->addAction(tr("RAW 显影"))->setEnabled(false);
    setup_settings_menu(menu);
    root->addWidget(new WindowHeader(menu, this));
    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(12, 8, 12, 8);
    const auto button = [&](const QString& text, const auto& function) {
        auto* result = new QPushButton(text, this);
        result->setAutoDefault(false);
        toolbar->addWidget(result);
        connect(result, &QPushButton::clicked, this, function);
        return result;
    };
    button(tr("适应窗口"), [this] {
        detail_preview_ = false;
        view_->setSceneRect(scene_->sceneRect());
        view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
        display();
    })->setObjectName("rawZoomFit");
    button(tr("100%"), [this] {
        fit_next_ = false;
        view_->setTransform(QTransform().rotate(sources_[current_].raw_recipe.value("quarterTurns").toInt()*90));
        detail_preview_ = true;
        display();
    })->setObjectName("rawZoom100");
    for (int tool = 0; tool < 3; ++tool) {
        auto* control = button(tool == 0   ? tr("手形")
                               : tool == 1 ? tr("放大 +")
                                           : tr("缩小 −"),
                               [this, tool] {
                                   tone_->set_target_mode(-1);
                                   tone_->stop_point_sampling();
                                   white_balance_->stop_sampling();
                                   optics_->stop_sampling();
                                   edit_mode_->setCurrentIndex(0);
                                   static_cast<RawPreviewView*>(view_)->set_tool(tool);
                                   view_->setFocus();
                               });
        control->setObjectName(tool == 0   ? "rawHandTool"
                               : tool == 1 ? "rawZoomTool"
                                           : "rawZoomOutTool");
    }
    compare_ = new QCheckBox(tr("原图 / 调整后"), this);
    toolbar->addWidget(compare_);
    connect(compare_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && compare_layout_) {
            const QSignalBlocker blocker(compare_layout_);
            compare_layout_->setCurrentIndex(0);
            if (before_view_)
                before_view_->hide();
            fit_next_ = true;
        }
        display();
    });
    toolbar->addStretch();
    button(tr("首选项…"), [this] {
        show_canvas_preferences(this, 8);
    })->setObjectName("rawPreferences");
    button(tr("同步设置…"), [this] { synchronize(); })->setObjectName("rawSynchronize");
    button(tr("更新全部预览"), [this] { compute_all(); })->setObjectName("rawComputeAll");
    button(tr("撤销"), [this] { restore_history(-1); })->setObjectName("rawUndo");
    button(tr("重做"), [this] { restore_history(1); })->setObjectName("rawRedo");
    auto* undo = new QShortcut(QKeySequence::Undo, this);
    connect(undo, &QShortcut::activated, this, [this] { restore_history(-1); });
    auto* redo = new QShortcut(QKeySequence("Ctrl+Shift+Z"), this);
    connect(redo, &QShortcut::activated, this, [this] { restore_history(1); });
    root->addLayout(toolbar);
    auto* body = new QHBoxLayout;
    files_ = create_raw_filmstrip(this);
    files_->setObjectName("rawFiles");
    files_->setViewMode(QListView::IconMode);
    files_->setFlow(QListView::LeftToRight);
    files_->setWrapping(false);
    files_->setResizeMode(QListView::Adjust);
    files_->setIconSize(QSize(112, 72));
    files_->setGridSize(QSize(144, 100));
    files_->setFixedHeight(108);
    for (const auto& source : sources_) {
        auto* item = new QListWidgetItem(source.name, files_);
        item->setCheckState(Qt::Checked);
        item->setToolTip(source.linked_path);
    }

    scene_ = new QGraphicsScene(this);
    auto* navigation_view = new RawPreviewView(scene_, this);
    view_ = navigation_view;
    navigation_view->navigated = [this] {
        fit_next_ = false;
        if (before_view_ && before_view_->isVisible()) {
            before_view_->setSceneRect(view_->sceneRect());
            before_view_->setTransform(view_->transform());
            before_view_->centerOn(view_->mapToScene(view_->viewport()->rect().center()));
        }
        if (!ui_ready_)
            return;
        const bool detail = std::hypot(view_->transform().m11(),view_->transform().m12()) >= .999;
        if (detail_preview_ != detail) {
            detail_preview_ = detail;
            display();
        }
    };
    navigation_view->fit_requested = [this] { findChild<QPushButton*>("rawZoomFit")->click(); };
    view_->setObjectName("rawPreview");
    view_->setDragMode(QGraphicsView::NoDrag);
    view_->setBackgroundBrush(QColor("#1c2024"));
    view_->viewport()->installEventFilter(this);
    view_->viewport()->setMouseTracking(true);
    auto* image_column = new QVBoxLayout;
    image_column->setSpacing(0);
    comparison_views_ = new QBoxLayout(QBoxLayout::LeftToRight);
    before_scene_ = new QGraphicsScene(this);
    auto* before_navigation = new RawPreviewView(before_scene_, this);
    before_view_ = before_navigation;
    before_view_->setObjectName("rawBeforePreview");
    before_view_->setBackgroundBrush(QColor("#1c2024"));
    before_view_->hide();
    before_navigation->fit_requested = navigation_view->fit_requested;
    before_navigation->navigated = [this, navigation_view] {
        view_->setSceneRect(before_view_->sceneRect());
        view_->setTransform(before_view_->transform());
        view_->centerOn(before_view_->mapToScene(before_view_->viewport()->rect().center()));
        if (navigation_view->navigated)
            navigation_view->navigated();
    };
    comparison_views_->addWidget(before_view_, 1);
    comparison_views_->addWidget(view_, 1);
    image_column->addLayout(comparison_views_, 1);
    image_column->addWidget(setup_filmstrip(this));
    body->addLayout(image_column, 1);
    auto* panel = new QWidget(this);
    panel->setObjectName("rawParamPanel");
    panel->setMinimumWidth(320);
    auto* form = new QFormLayout(panel);
    form->setSizeConstraint(QLayout::SetMinimumSize);
    form->setContentsMargins(12, 8, 12, 8);
    histogram_ = new QLabel(panel);
    histogram_->setObjectName("rawHistogram");
    histogram_->setFixedHeight(100);
    form->addRow(new QLabel(tr("直方图（显示 RGB）"), panel));
    histogram_->setAlignment(Qt::AlignCenter);
    histogram_->setScaledContents(true);
    form->addRow(histogram_);
    setup_inspection(panel);
    form->addRow(color_readout_);
    image_info_ = new QLabel(panel);
    image_info_->setObjectName("rawImageInfo");
    form->addRow(image_info_);
    edit_mode_ = new QComboBox(panel);
    edit_mode_->setObjectName("rawEditMode");
    edit_mode_->addItems({tr("平移"), tr("局部笔刷"), tr("擦除范围"), tr("线性渐变"),
                          tr("径向渐变"), tr("裁剪"), tr("修复")});
    auto* healing_panel = setup_healing(panel);
    form->addRow(healing_panel);
    healing_panel->hide();
    auto* mask_panel = new QWidget(panel);
    mask_panel->setObjectName("rawMaskPanel");
    auto* mask_form = new QFormLayout(mask_panel);
    mask_form->setContentsMargins(0, 0, 0, 0);
    edit_mode_->hide(); // Shared state binding; tools are presented directly rather than in a menu.
    auto* mask_tools = new QWidget(mask_panel);
    mask_tools->setObjectName("rawMaskTools");
    auto* mask_tools_layout = new QHBoxLayout(mask_tools);
    mask_tools_layout->setContentsMargins(0, 0, 0, 0);
    mask_tools_layout->setSpacing(4);
    auto* mask_group = new QButtonGroup(mask_tools);
    const QStringList mask_names{tr("画笔（K）"), tr("擦除"), tr("线性渐变（G）"),
                                 tr("径向渐变（J）")};
    const QStringList mask_paths{
        "<path d='m8 14 9-11 4 4-11 9M8 14q-5-1-5 7 7 0 7-5Z'/>",
        "<path d='m3 14 10-11 8 8-10 10H9Z M8 9l8 8M11 21h10'/>",
        "<path d='M3 4h18M3 20h18M12 4v16'/><circle cx='12' cy='12' r='2'/>",
        "<ellipse cx='12' cy='12' rx='8' ry='6'/><path d='M10 12h4M12 10v4'/>"};
    for (int i = 0; i < 4; ++i) {
        auto* tool = new QToolButton(mask_tools);
        tool->setObjectName(QString("rawMaskTool%1").arg(i + 1));
        tool->setToolTip(mask_names[i]);
        tool->setAccessibleName(mask_names[i]);
        tool->setCheckable(true);
        tool->setFixedSize(36, 32);
        tool->setIconSize(QSize(22, 22));
        QPixmap icon(44, 44);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        QSvgRenderer renderer(
            ("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><g fill='none' "
             "stroke='#c8cdd2' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'>" +
             mask_paths[i] + "</g></svg>")
                .toUtf8());
        renderer.render(&painter);
        painter.end();
        icon.setDevicePixelRatio(2);
        tool->setIcon(QIcon(icon));
        mask_group->addButton(tool, i + 1);
        mask_tools_layout->addWidget(tool);
        connect(tool, &QToolButton::clicked, this,
                [this, i] { edit_mode_->setCurrentIndex(i + 1); });
    }
    mask_tools_layout->addStretch();
    mask_form->addRow(tr("蒙版工具"), mask_tools);
    connect(edit_mode_, &QComboBox::currentIndexChanged, this, [mask_group](int index) {
        mask_group->setExclusive(false);
        for (auto* button : mask_group->buttons())
            button->setChecked(mask_group->id(button) == index);
        mask_group->setExclusive(true);
    });
    form->addRow(mask_panel);
    mask_panel->hide();
    adjustment_ = new QComboBox(panel);
    adjustment_->setObjectName("rawAdjustmentTarget");
    mask_form->addRow(tr("调整目标"), adjustment_);
    auto* local_buttons = new QHBoxLayout;
    const auto local_button = [&](const QString& text, const char* name, const auto& action) {
        auto* result = new QPushButton(text, panel);
        result->setObjectName(name);
        result->setAutoDefault(false);
        local_buttons->addWidget(result);
        connect(result, &QPushButton::clicked, this, action);
    };
    local_button(tr("新建局部"), "rawAddLocal", [this] {
        auto& recipe = sources_[current_].raw_recipe;
        auto locals = recipe.value("locals").toArray();
        if (locals.size() >= 64) {
            message_->setText(tr("当前最多支持 64 个局部调整"));
            return;
        }
        locals.append(QJsonObject{{"tone", editing::tone_json({})}, {"components", QJsonArray{}}});
        recipe.insert("locals", locals);
        refresh_local_controls();
        adjustment_->setCurrentIndex(int(locals.size()));
        edit_mode_->setCurrentIndex(1);
        overlay_->setChecked(true);
        record_history();
    });
    local_button(tr("删除局部"), "rawDeleteLocal", [this] {
        auto locals = sources_[current_].raw_recipe.value("locals").toArray();
        const int index = adjustment_->currentIndex() - 1;
        if (index < 0 || index >= locals.size())
            return;
        locals.removeAt(index);
        sources_[current_].raw_recipe.insert("locals", locals);
        refresh_local_controls();
        select_adjustment();
        record_history();
        display();
    });
    auto* crop_panel = setup_crop(panel);
    form->addRow(crop_panel);
    crop_panel->hide();
    mask_form->addRow(local_buttons);
    auto* semantic_buttons = new QHBoxLayout;
    for (const auto& [name, target] :
         {std::pair{tr("天空"), 0}, {tr("草地"), 1}, {tr("植被"), 2}, {tr("人物…"), 3}}) {
        auto* semantic_button = new QPushButton(name, panel);
        semantic_button->setObjectName(QString("rawSemantic%1").arg(target));
        semantic_button->setAutoDefault(false);
        semantic_buttons->addWidget(semantic_button);
        connect(semantic_button, &QPushButton::clicked, this,
                [this, target] { semantic_local(target); });
    }
    mask_form->addRow(semantic_buttons);
    auto* object_buttons = new QHBoxLayout;
    for (const auto& [name, target] :
         {std::pair{tr("主体"), 4}, {tr("背景"), 5}, {tr("对象…"), 6}}) {
        auto* object_button = new QPushButton(name, panel);
        object_button->setObjectName(QString("rawSemantic%1").arg(target));
        object_button->setAutoDefault(false);
        object_buttons->addWidget(object_button);
        connect(object_button, &QPushButton::clicked, this, [this, target] { semantic_local(target); });
    }
    mask_form->addRow(object_buttons);
    local_radius_ = new QDoubleSpinBox(panel);
    local_radius_->setObjectName("rawLocalDiameter");
    local_radius_->setRange(2, 2000);
    local_radius_->setValue(200);
    local_radius_->setSuffix(tr(" 像素"));
    mask_form->addRow(tr("大小（直径）"), local_radius_);
    local_feather_ = new QDoubleSpinBox(panel);
    local_feather_->setObjectName("rawLocalFeather");
    local_feather_->setRange(0, 100);
    local_feather_->setValue(50);
    local_feather_->setSuffix(" %");
    mask_form->addRow(tr("局部羽化"), local_feather_);
    overlay_ = new QCheckBox(tr("显示当前局部范围（红色）"), panel);
    overlay_->setObjectName("rawLocalOverlay");
    mask_form->addRow(overlay_);
    mask_form->insertRow(1, setup_mask_components(panel));
    mask_form->setRowVisible(adjustment_, false);
    connect(overlay_, &QCheckBox::toggled, this, &RawDialog::display);
    connect(adjustment_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_) {
            local_stroke_ = {};
            mask_component_ = -1;
            select_adjustment();
            display();
        }
    });
    connect(edit_mode_, &QComboBox::currentIndexChanged, this, [this](int mode) {
        findChild<QPushButton*>("rawMaskColorPicker")->setChecked(false);
        if (mode != 0 && compare_layout_)
            compare_layout_->setCurrentIndex(0);
        if (tone_ && mode != 0)
            tone_->set_target_mode(-1);
        healing_drawing_ = healing_drag_source_ = healing_delete_area_ = healing_drag_size_ =
            healing_new_source_ = false;
        static_cast<RawPreviewView*>(view_)->set_tool(mode == 0 ? 0 : -1);
        local_stroke_ = {};
        display();
    });
    tone_ = new TonePanel({}, panel);
    tone_->use_accordion();
    setup_white_balance();
    tone_->detail_view_changed = [this] {
        setProperty("detailPreview", tone_->detail_preview());
        display();
    };
    tone_->point_view_changed = [this] {
        if (tone_->point_color_sampling() || tone_->target_mode() >= 0)
            findChild<QPushButton*>("rawMaskColorPicker")->setChecked(false);
        if (compare_layout_ && (tone_->point_color_sampling() || tone_->target_mode() >= 0))
            compare_layout_->setCurrentIndex(0);
        if (tone_->point_color_sampling() || tone_->target_mode() >= 0)
            white_balance_->stop_sampling();
        if ((tone_->point_color_sampling() || tone_->target_mode() >= 0) && optics_)
            optics_->stop_sampling();
        view_->viewport()->setCursor((tone_->point_color_sampling() || tone_->target_mode() >= 0)
                                         ? Qt::CrossCursor
                                         : Qt::ArrowCursor);
        display();
    };
    exposure_ = tone_->findChild<QDoubleSpinBox*>("toneExposure");
    exposure_->setObjectName("rawExposure");
    form->addRow(tone_);
    optics_ = new RawOpticsPanel(panel);
    form->addRow(optics_);
    optics_->changed = [this] {
        if (loading_)
            return;
        sources_[current_].raw_recipe.insert("optics", editing::optics_json(optics_->settings()));
        if (!parameter_gesture_)
            history_timer_->start();
        display();
    };
    optics_->view_changed = [this] {
        if (optics_->sampling())
            findChild<QPushButton*>("rawMaskColorPicker")->setChecked(false);
        if (compare_layout_ && optics_->sampling())
            compare_layout_->setCurrentIndex(0);
        if (optics_->sampling())
            white_balance_->stop_sampling();
        if (optics_->sampling()) {
            tone_->set_target_mode(-1);
            tone_->stop_point_sampling();
        }
        view_->viewport()->setCursor(optics_->sampling() ? Qt::CrossCursor : Qt::ArrowCursor);
        display();
    };
    optics_->gesture_started = [this] {
        history_timer_->stop();
        record_history();
        parameter_gesture_ = true;
    };
    optics_->gesture_finished = [this] {
        parameter_gesture_ = false;
        history_timer_->stop();
        record_history();
    };
    geometry_ = new RawGeometryPanel(panel);
    form->addRow(geometry_);
    geometry_->hide();
    geometry_->gesture_started = optics_->gesture_started;
    geometry_->gesture_finished = optics_->gesture_finished;
    geometry_->changed = [this] {
        if (loading_)
            return;
        {
            const QSignalBlocker blocker(crop_angle_);
            crop_angle_->setValue(geometry_->settings().rotation);
            const QSignalBlocker slider_block(findChild<QSlider*>("rawCropAngleSlider"));
            findChild<QSlider*>("rawCropAngleSlider")
                ->setValue(qRound(geometry_->settings().rotation * 100));
        }
        sources_[current_].raw_recipe.insert("geometry",
                                             editing::geometry_json(geometry_->settings()));
        if (!parameter_gesture_)
            history_timer_->start();
        display();
    };
    auto* detail_form =
        qobject_cast<QFormLayout*>(tone_->findChild<QWidget*>("rawSectionBody7")->layout());
    denoise_ = new QCheckBox(tr("AI RAW 降噪"), panel);
    denoise_->setObjectName("rawDenoise");
    detail_form->addRow(denoise_);
    auto* capability_note = new QLabel(panel);
    capability_note->setObjectName("rawCapabilityNote");
    capability_note->setWordWrap(true);
    detail_form->addRow(capability_note);
    auto* amount_row = new QWidget(panel);
    auto* amount_layout = new QHBoxLayout(amount_row);
    amount_layout->setContentsMargins(0, 0, 0, 0);
    auto* amount_slider = new QSlider(Qt::Horizontal, amount_row);
    amount_slider->setObjectName("rawDenoiseAmountSlider");
    amount_slider->setRange(0, 100);
    denoise_amount_ = new QDoubleSpinBox(amount_row);
    denoise_amount_->setObjectName("rawDenoiseAmount");
    denoise_amount_->setRange(0, 100);
    denoise_amount_->setDecimals(0);
    denoise_amount_->setValue(50);
    amount_slider->setValue(50);
    amount_layout->addWidget(amount_slider, 1);
    amount_layout->addWidget(denoise_amount_);
    detail_form->addRow(tr("数量"), amount_row);
    amount_row->setEnabled(false);
    connect(denoise_, &QCheckBox::toggled, amount_row, &QWidget::setEnabled);
    connect(amount_slider, &QSlider::valueChanged, denoise_amount_, &QDoubleSpinBox::setValue);
    connect(denoise_amount_, &QDoubleSpinBox::valueChanged, this,
            [this, amount_slider](double value) {
                const QSignalBlocker blocker(amount_slider);
                amount_slider->setValue(qRound(value));
                if (!loading_) {
                    sources_[current_].raw_recipe.insert("denoiseAmount", value);
                    if (!amount_slider->isSliderDown())
                        history_timer_->start();
                    display();
                }
            });
    denoise_compare_ = new QPushButton(tr("按住查看降噪前"), panel);
    denoise_compare_->setObjectName("rawDenoiseCompare");
    denoise_compare_->setAutoDefault(false);
    detail_form->addRow(denoise_compare_);
    connect(denoise_compare_, &QPushButton::pressed, this, &RawDialog::display);
    connect(denoise_compare_, &QPushButton::released, this, &RawDialog::display);
    auto* advanced_button = new QPushButton(tr("降噪设置…"), panel);
    advanced_button->setAutoDefault(false);
    advanced_button->setObjectName("rawAdvanced");
    advanced_button->setCheckable(true);
    detail_form->addRow(advanced_button);
    auto* advanced = new QWidget(panel);
    auto* advanced_form = new QFormLayout(advanced);
    advanced_form->setContentsMargins(0, 0, 0, 0);
    detail_form->addRow(advanced);
    advanced->hide();
    connect(advanced_button, &QPushButton::toggled, advanced, &QWidget::setVisible);
    backend_ = new QComboBox(panel);
    backend_->setObjectName("rawBackend");
    backend_->addItems({"DirectML", "CPU"});
    advanced_form->addRow(tr("计算设备"), backend_);
    model_choice_ = new QComboBox(panel);
    model_choice_->setObjectName("rawModelChoice");
    model_choice_->addItems({tr("内置 · RawNIND / UtNet2 Bayer"), tr("自定义兼容模型…")});
    advanced_form->addRow(tr("降噪模型"), model_choice_);
    model_ = new QLineEdit(bundled_model(), panel);
    model_->setObjectName("rawModel");
    advanced_form->addRow(tr("模型"), model_);
    auto* browse = new QPushButton(tr("选择模型…"), panel);
    browse->setAutoDefault(false);
    browse->setObjectName("rawBrowse");
    advanced_form->addRow(browse);
    model_->setReadOnly(true);
    browse->setEnabled(false);
    connect(model_choice_, &QComboBox::currentIndexChanged, this, [this, browse](int index) {
        model_->setReadOnly(index == 0);
        browse->setEnabled(index == 1 && !job_);
        if (!loading_) {
            const QSignalBlocker blocker(model_);
            model_->setText(index == 0 ? bundled_model()
                                       : QSettings().value("raw/customModel").toString());
            auto& recipe = sources_[current_].raw_recipe;
            recipe.insert("customModel", index == 1);
            recipe.insert("modelPath", model_->text());
            cache_keys_[current_].clear();
            record_history();
            display();
            if (denoise_->isChecked())
                message_->setText(tr("模型已修改，点击“开始降噪”应用"));
            else
                start();
        }
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto file =
            QFileDialog::getOpenFileName(this, tr("选择 Bayer ONNX 模型"), {}, "ONNX (*.onnx)");
        if (!file.isEmpty()) {
            model_->setText(file);
            QSettings().setValue("raw/customModel", file);
        }
    });
    compute_ = new QPushButton(tr("开始降噪"), panel);
    compute_->setObjectName("rawCompute");
    compute_->setAutoDefault(false);
    detail_form->insertRow(detail_form->rowCount() - 2, compute_);
    compute_->setEnabled(false);
    connect(compute_, &QPushButton::clicked, this, &RawDialog::start);
    auto* stop = new QPushButton(QString::fromUtf8("×"), this);
    stop->setObjectName("rawCancelCompute");
    stop->setToolTip(tr("取消当前计算，保留显影窗口"));
    stop->setAccessibleName(tr("取消计算"));
    stop->setFixedSize(26, 26);
    stop->setStyleSheet("padding: 0px; font-size: 18px;");
    stop->hide();
    stop->setEnabled(false);
    stop->setAutoDefault(false);
    connect(stop, &QPushButton::clicked, this, [this] {
        batch_ = false;
        if (job_)
            job_->cancelled.store(true);
        if (commit_job_)
            commit_job_->cancelled.store(true);
        message_->setText(tr("正在取消计算…"));
    });
    progress_ = new QProgressBar(this);
    progress_->setObjectName("rawProgress");
    progress_->setFixedWidth(180);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->hide();
    message_ = new QLabel(this);
    message_->setObjectName("rawStatus");
    message_->setWordWrap(true);
    message_->setMinimumWidth(0);
    auto* sidebar = new QVBoxLayout;
    const auto detach_row = [form](QWidget* widget) {
        const auto row = form->takeRow(widget);
        delete row.labelItem;
        delete row.fieldItem;
    };
    detach_row(histogram_);
    detach_row(image_info_);
    detach_row(color_readout_);
    const auto title = form->takeRow(0);
    if (title.fieldItem) {
        delete title.fieldItem->widget();
        delete title.fieldItem;
    }
    delete title.labelItem;
    sidebar->addWidget(histogram_);
    sidebar->addWidget(color_readout_);
    auto* capture_info = new QLabel(this);
    capture_info->setObjectName("rawCaptureInfo");
    capture_info->setAlignment(Qt::AlignCenter);
    sidebar->addWidget(capture_info);
    sidebar->addWidget(setup_profiles(this));
    sidebar->addWidget(profile_browser_, 1);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("rawEditingScroll");
    scroll->setWidgetResizable(true);
    scroll->setFixedWidth(360);
    scroll->setWidget(panel);
    sidebar->addWidget(scroll, 1);
    body->addLayout(sidebar);
    auto* rail = new QVBoxLayout;
    rail->setContentsMargins(3, 6, 3, 6);
    auto* group = new QButtonGroup(this);
    const QStringList labels{tr("编辑"), tr("裁剪"), tr("蒙版"), tr("手形"), tr("修复（B）")};
    const QStringList paths{
        "<path d='M3 5h18M3 12h18M3 19h18'/><circle cx='8' cy='5' r='2'/><circle cx='16' cy='12' "
        "r='2'/><circle cx='9' cy='19' r='2'/>",
        "<path d='M7 2v15h15M2 7h15v15M4 20L20 4'/>",
        "<circle cx='12' cy='12' r='8' stroke-dasharray='2 2'/><path d='M12 7v10M7 12h10'/>",
        "<path d='M8 12V6q0-3 2-1v6-7q1-2 2 0v7-6q1-2 2 0v7-4q2-2 2 1v6q0 6-6 6-3 0-5-4l-3-4q0-3 "
        "3-1l3 2'/>",
        "<path d='m4 13 9-9q3-3 6 0t0 6l-9 9q-3 3-6 0t0-6Z M8 8l8 8'/><circle cx='12' cy='12' "
        "r='1'/>",
    };
    for (const int i : {0, 1, 4, 2, 3}) {
        auto* tool = new QToolButton(this);
        tool->setObjectName(QString("rawRail%1").arg(i));
        const auto svg = ("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' "
                          "viewBox='0 0 24 24'><g fill='none' stroke='#c8cdd2' stroke-width='1.5' "
                          "stroke-linecap='round' stroke-linejoin='round'>" +
                          paths[i] + "</g></svg>")
                             .toUtf8();
        QPixmap icon(48, 48);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        QSvgRenderer renderer(svg);
        renderer.render(&painter);
        painter.end();
        tool->setIcon(QIcon(icon));
        tool->setIconSize(QSize(22, 22));
        tool->setToolTip(labels[i]);
        tool->setAccessibleName(labels[i]);
        tool->setFixedSize(34, 34);
        tool->setCheckable(true);
        tool->setChecked(i == 0);
        group->addButton(tool, i);
        rail->addWidget(tool);
        connect(tool, &QToolButton::clicked, this,
                [this, mask_panel, crop_panel, healing_panel, i] {
                    mask_panel->setVisible(i == 2);
                    crop_panel->setVisible(i == 1);
                    healing_panel->setVisible(i == 4);
                    tone_->setVisible(i != 1 && i != 4);
                    optics_->setVisible(i == 0);
                    geometry_->setVisible(i == 1);
                    if (i == 0) {
                        adjustment_->setCurrentIndex(0);
                        edit_mode_->setCurrentIndex(0);
                    }
                    if (i == 1) {
                        adjustment_->setCurrentIndex(0);
                        edit_mode_->setCurrentIndex(5);
                    }
                    if (i == 2)
                        edit_mode_->setCurrentIndex(1);
                    if (i == 3)
                        edit_mode_->setCurrentIndex(0);
                    if (i == 4) {
                        refresh_healing();
                        edit_mode_->setCurrentIndex(6);
                    }
                });
    }
    const auto shortcut = [this](const QString& key, const auto& action) {
        auto* item = new QShortcut(QKeySequence(key), this);
        connect(item, &QShortcut::activated, this, action);
    };
    shortcut("C", [this] { findChild<QToolButton*>("rawRail1")->click(); });
    shortcut("K", [this] { findChild<QToolButton*>("rawRail2")->click(); });
    shortcut("G", [this] {
        findChild<QToolButton*>("rawRail2")->click();
        findChild<QToolButton*>("rawMaskTool3")->click();
    });
    shortcut("J", [this] {
        findChild<QToolButton*>("rawRail2")->click();
        findChild<QToolButton*>("rawMaskTool4")->click();
    });
    shortcut("H", [this] { findChild<QPushButton*>("rawHandTool")->click(); });
    shortcut("B", [this] { findChild<QToolButton*>("rawRail4")->click(); });
    shortcut("P", [this] { compare_->toggle(); });
    shortcut("U", [this] { shadow_clipping_->toggle(); });
    shortcut("O", [this] { highlight_clipping_->toggle(); });
    rail->addStretch();
    body->addLayout(rail);
    root->addLayout(body, 1);
    auto* footer = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    apply_ = footer->button(QDialogButtonBox::Ok);
    apply_->setObjectName("rawApply");
    apply_->setAutoDefault(false);
    apply_->setDefault(false);
    apply_->setEnabled(false);
    footer->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
    auto* save_images=footer->addButton(tr("保存图像…"),QDialogButtonBox::ActionRole);
    save_images->setObjectName("rawSaveImages");save_images->setAutoDefault(false);
    connect(save_images,&QPushButton::clicked,this,[this]{begin_output(1);});
    auto* save_dng=footer->addButton(tr("保存 DNG 副本…"),QDialogButtonBox::ActionRole);
    save_dng->setObjectName("rawSaveDng");save_dng->setAutoDefault(false);
    save_dng->setToolTip(tr("保存当前照片的传感器 RAW 数据和本程序显影设置，保留原片；显影设置不保证由其他软件识别"));
    connect(save_dng,&QPushButton::clicked,this,[this]{begin_output(5);});
    auto* embedded=footer->addButton(tr("改用内嵌预览…"),QDialogButtonBox::ActionRole);
    embedded->setObjectName("rawOpenEmbedded");embedded->setAutoDefault(false);embedded->hide();
    connect(embedded,&QPushButton::clicked,this,[this]{
        if(job_||commit_job_||closing_)return;
        try{const auto preview=editing::embedded_preview(sources_[current_]);
            if(QMessageBox::question(this,tr("打开受限预览"),preview.description+tr("\n仅打开当前照片的预览副本，不能进行 RAW 降噪或恢复原始高光。继续？"))!=QMessageBox::Yes)return;
            auto layer=sources_[current_];layer.pixels=preview.image;layer.kind=editing::LayerKind::pixels;
            layer.name+=tr(" · 内嵌预览 · %1位").arg(preview.bits);layer.raw_source.clear();layer.raw_recipe={};
            layer.source_metadata.insert("Camera:PreviewOnly",true);sources_={std::move(layer)};QDialog::accept();
        }catch(const std::exception& e){message_->setText(QString::fromUtf8(e.what()));}
    });
    auto* done=footer->addButton(tr("完成"),QDialogButtonBox::ActionRole);
    done->setObjectName("rawDone");done->setAutoDefault(false);
    done->setToolTip(tr("保存RAW旁车设置并关闭，不打开图片文档"));
    done->setVisible(std::all_of(sources_.begin(),sources_.end(),[](const auto& layer){return layer.pixels.isNull() && !layer.linked_path.isEmpty();}));
    connect(done,&QPushButton::clicked,this,[this]{begin_output(2);});
    auto* open_options=footer->addButton(QString::fromUtf8("▾"),QDialogButtonBox::ActionRole);
    open_options->setObjectName("rawOpenOptions");open_options->setAutoDefault(false);open_options->setMaximumWidth(28);
    auto* open_menu=new QMenu(open_options);
    for(const auto& option:{std::pair{tr("打开为源图层"),0},std::pair{tr("打开副本（不保存旁车设置）"),3},std::pair{tr("打开为像素图层"),4}}){
        auto* action=open_menu->addAction(option.first);action->setObjectName(option.second==0?"rawOpenSource":option.second==3?"rawOpenCopy":"rawOpenPixels");
        connect(action,&QAction::triggered,this,[this,mode=option.second]{begin_output(mode);});
    }
    open_options->setMenu(open_menu);apply_->setToolTip(tr("打开并保存RAW设置；按住Alt打开副本而不保存旁车设置"));
    auto* footer_row = new QHBoxLayout;
    footer_row->setContentsMargins(12, 6, 6, 6);
    footer_row->addWidget(image_info_);
    compare_layout_ = new QComboBox(this);
    compare_layout_->setObjectName("rawCompareLayout");
    compare_layout_->addItems(
        {tr("调整后"), tr("左右分屏"), tr("上下分屏"), tr("左右并排"), tr("上下并排")});
    footer_row->addWidget(compare_layout_);
    for (const auto& source : sources_)
        before_recipes_.push_back(source.raw_recipe);
    connect(compare_layout_, &QComboBox::currentIndexChanged, this, [this](int mode) {
        if (mode) {
            const QSignalBlocker blocker(compare_);
            compare_->setChecked(false);
            tone_->set_target_mode(-1);
            tone_->stop_point_sampling();
            edit_mode_->setCurrentIndex(0);
        }
        comparison_views_->setDirection(mode == 4 ? QBoxLayout::TopToBottom
                                                  : QBoxLayout::LeftToRight);
        before_view_->setVisible(mode >= 3);
        fit_next_ = true;
        display();
    });
    auto* comparison_cycle = new QShortcut(QKeySequence("Q"), view_);
    connect(comparison_cycle, &QShortcut::activated, this, [this] {
        compare_layout_->setCurrentIndex((compare_layout_->currentIndex() + 1) % 5);
    });
    auto* before_set = new QPushButton(tr("将当前设置设为之前"), this);
    before_set->setObjectName("rawSetBefore");
    before_set->setAutoDefault(false);
    footer_row->addWidget(before_set);
    connect(before_set, &QPushButton::clicked, this, [this] {
        before_recipes_[current_] = sources_[current_].raw_recipe;
        display();
    });
    footer_row->addStretch();
    footer_row->addWidget(footer);
    auto* status_row = new QHBoxLayout;
    status_row->setContentsMargins(12, 0, 12, 0);
    status_row->addWidget(message_, 1);
    status_row->addWidget(progress_);
    status_row->addWidget(stop);
    root->addLayout(status_row);
    root->addLayout(footer_row);
    connect(footer, &QDialogButtonBox::accepted, this, &RawDialog::accept);
    connect(footer, &QDialogButtonBox::rejected, this, &RawDialog::reject);
    auto* submit = new QShortcut(QKeySequence("Ctrl+Return"), this);
    connect(submit, &QShortcut::activated, this, &RawDialog::accept);
    timer_ = new QTimer(this);
    timer_->setInterval(30);
    connect(timer_, &QTimer::timeout, this, &RawDialog::finish);
    connect(files_, &QListWidget::currentRowChanged, this, &RawDialog::select);
    connect(files_, &QListWidget::itemChanged, this, [this] { update_ready(); });
    tone_->changed = [this] {
        if (!loading_) {
            auto& recipe = sources_[current_].raw_recipe;
            const int index = adjustment_->currentIndex() - 1;
            if (index < 0)
                recipe.insert("tone", editing::tone_json(tone_->settings()));
            else {
                auto locals = recipe.value("locals").toArray();
                auto local = locals.at(index).toObject();
                local.insert("tone", editing::tone_json(tone_->settings()));
                locals[index] = local;
                recipe.insert("locals", locals);
            }
            const auto sliders = tone_->findChildren<QSlider*>();
            const bool dragging =
                std::any_of(sliders.begin(), sliders.end(),
                            [](const QSlider* slider) { return slider->isSliderDown(); });
            if (!dragging && !parameter_gesture_)
                history_timer_->start();
            display();
        }
    };
    // One transaction per pointer gesture across every RAW parameter panel.
    // Install before input reaches the control; release is committed after its
    // handler has applied the final value (including custom curves and wheels).
    for (auto* widget : panel->findChildren<QWidget*>()) {
        const auto name = widget->objectName();
        if (qobject_cast<QSlider*>(widget) || name == "toneCurve" ||
            name.startsWith("rawColorWheel") || name == "rawPointField" ||
            name == "rawPointLightField" || name.startsWith("rawPointRange")) {
            widget->setProperty("rawParameterGesture", true);
            widget->installEventFilter(this);
        }
    }
    const auto invalidate = [this] {
        if (!loading_) {
            sources_[current_].raw_recipe.insert("denoise", denoise_->isChecked());
            sources_[current_].raw_recipe.insert("denoiseAmount", denoise_amount_->value());
            sources_[current_].raw_recipe.insert("directml", backend_->currentIndex() == 0);
            sources_[current_].raw_recipe.insert("modelPath", model_->text());
            cache_keys_[current_].clear();
            record_history();
            display();
            apply_->setEnabled(false);
            if (denoise_->isChecked())
                message_->setText(tr("降噪设置已修改，点击“开始降噪”应用"));
            else
                start();
        }
    };
    connect(denoise_, &QCheckBox::toggled, this, invalidate);
    connect(backend_, &QComboBox::currentIndexChanged, this, invalidate);
    connect(model_, &QLineEdit::textChanged, this, invalidate);
    ui_ready_ = true;
    files_->setCurrentRow(0);
    record_history();
    QTimer::singleShot(0, this, &RawDialog::start);
}
int RawDialog::exec() { return run_work_window(*this, parentWidget()); }
RawDialog::~RawDialog() {
    finish_profiles();
    if (preview_job_) {
        preview_job_->cancelled.store(true);
        preview_job_->result.wait();
    }
    if (commit_job_) {
        commit_job_->cancelled.store(true);
        commit_job_->result.wait();
    }
    if (job_) {
        job_->cancelled.store(true);
        job_->result.wait();
    }
}
void RawDialog::refresh_local_controls() {
    const QSignalBlocker blocker(adjustment_);
    const int previous = adjustment_->currentIndex();
    adjustment_->clear();
    adjustment_->addItem(tr("全局调整"));
    const auto locals = sources_[current_].raw_recipe.value("locals").toArray();
    for (int i = 0; i < locals.size(); ++i)
        adjustment_->addItem(locals[i].toObject().value("name").toString(tr("局部 %1").arg(i + 1)));
    adjustment_->setCurrentIndex(std::clamp(previous, 0, int(locals.size())));
    refresh_mask_components();
}
void RawDialog::select_adjustment() {
    loading_ = true;
    const auto& recipe = sources_[current_].raw_recipe;
    const int index = adjustment_->currentIndex() - 1;
    optics_->setEnabled(index < 0);
    geometry_->setEnabled(index < 0);
    const auto locals = recipe.value("locals").toArray();
    const auto settings = index >= 0 && index < locals.size()
                              ? locals.at(index).toObject().value("tone").toObject()
                              : recipe.value("tone").toObject();
    tone_->set_settings(editing::tone_from_json(settings));
    refresh_white_balance();
    loading_ = false;
    refresh_mask_components();
}
bool RawDialog::eventFilter(QObject* object, QEvent* event) {
    if (object->property("maskSampleIndex").isValid() &&
        event->type() == QEvent::MouseButtonPress &&
        static_cast<QMouseEvent*>(event)->modifiers().testFlag(Qt::AltModifier)) {
        remove_mask_sample(object->property("maskSampleIndex").toInt());
        return true;
    }
    if (!ui_ready_)
        return QDialog::eventFilter(object, event);
    if (object == view_->viewport()) {
        const bool pointer = event->type() == QEvent::MouseButtonPress ||
                             event->type() == QEvent::MouseMove ||
                             event->type() == QEvent::MouseButtonRelease;
        if (pointer) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const bool picker = white_balance_->sampling() || optics_->sampling() ||
                                tone_->point_color_sampling() || tone_->target_mode() >= 0 ||
                                mask_sampling_;
            if (mouse->button() == Qt::MiddleButton ||
                (static_cast<RawPreviewView*>(view_)->navigating() &&
                 (!picker || static_cast<RawPreviewView*>(view_)->temporary_pan())))
                return false;
        }
    }
    if ((object == shadow_clipping_ || object == highlight_clipping_) &&
        (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        object->setProperty("hovering", event->type() == QEvent::Enter);
        refresh_inspection();
    }
    if (object == view_->viewport() && event->type() == QEvent::MouseMove)
        color_readout(view_->mapToScene(static_cast<QMouseEvent*>(event)->position().toPoint()));
    if (object == view_->viewport() && compare_layout_ && compare_layout_->currentIndex() > 0 &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonRelease))
        return false;
    if (object == view_->viewport() && mask_sampling_ &&
        event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && !job_ && !commit_job_ &&
            property("previewPresented").toULongLong() == preview_revision_) {
            mask_drag_start_ = view_->mapToScene(mouse->position().toPoint());
            mask_range_append_ = mouse->modifiers().testFlag(Qt::ShiftModifier);
            mask_range_drawing_ = true;
        }
        return true;
    }
    if (object == view_->viewport() && mask_sampling_ &&
        (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease)) {
        if (mask_range_drawing_) {
            const auto point =
                view_->mapToScene(static_cast<QMouseEvent*>(event)->position().toPoint());
            if (mask_guide_) {
                scene_->removeItem(mask_guide_);
                delete mask_guide_;
                mask_guide_ = nullptr;
            }
            if (event->type() == QEvent::MouseMove) {
                QPainterPath path;
                path.addRect(QRectF(mask_drag_start_, point).normalized());
                mask_guide_ = scene_->addPath(
                    path, QPen(Qt::white, 1 / std::hypot(view_->transform().m11(),view_->transform().m12()), Qt::DashLine));
            } else {
                mask_range_drawing_ = false;
                sample_mask_range(point, mask_range_append_);
            }
        }
        return true;
    }
    if (object == view_->viewport() && tone_->target_mode() >= 0 &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonRelease)) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
            if (job_ || commit_job_ || compare_->isChecked() || point_sample_preview_.isNull() ||
                property("previewPresented").toULongLong() != preview_revision_)
                return true;
            const auto point = view_->mapToScene(mouse->position().toPoint());
            const QPoint sample(
                int(point.x() / source_size_.width() * point_sample_preview_.width()),
                int(point.y() / source_size_.height() * point_sample_preview_.height()));
            if (point_sample_preview_.rect().contains(sample)) {
                history_timer_->stop();
                record_history();
                target_start_ = mouse->position();
                parameter_gesture_ = tone_->begin_target(point_sample_preview_.pixelColor(sample));
            }
            return true;
        }
        if (tone_->target_dragging()) {
            const double delta = (target_start_.y() - mouse->position().y()) * .5;
            tone_->move_target(delta * (mouse->modifiers().testFlag(Qt::AltModifier) ? .1 : 1.));
            if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
                tone_->finish_target();
                parameter_gesture_ = false;
                history_timer_->stop();
                record_history();
            }
            return true;
        }
    }
    if (object == view_->viewport() && white_balance_->sampling() &&
        event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && !job_ && !commit_job_)
            sample_white_balance(view_->mapToScene(mouse->position().toPoint()), false);
        return true;
    }
    if (object->property("rawParameterGesture").toBool()) {
        if (event->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            history_timer_->stop();
            record_history();
            parameter_gesture_ = true;
        } else if ((event->type() == QEvent::MouseButtonRelease &&
                    static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) ||
                   (event->type() == QEvent::KeyPress &&
                    static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) ||
                   event->type() == QEvent::UngrabMouse) {
            QTimer::singleShot(0, this, [this] {
                parameter_gesture_ = false;
                history_timer_->stop();
                record_history();
                display();
            });
        }
    }
    if (object == view_->viewport() && optics_->sampling() &&
        event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton || point_sample_preview_.isNull() ||
            property("previewPresented").toULongLong() != preview_revision_)
            return true;
        const auto point = view_->mapToScene(mouse->position().toPoint());
        const QPoint sample(
            int(point.x() / source_size_.width() * point_sample_preview_.width()),
            int(point.y() / source_size_.height() * point_sample_preview_.height()));
        if (point_sample_preview_.rect().contains(sample)) {
            history_timer_->stop();
            record_history();
            optics_->sample(point_sample_preview_.pixelColor(sample));
            history_timer_->stop();
            record_history();
        }
        return true;
    }
    if (object == view_->viewport() && tone_->point_color_sampling() &&
        event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton)
            return true;
        if (job_ || commit_job_ || compare_->isChecked() || point_sample_preview_.isNull() ||
            property("previewPresented").toULongLong() != preview_revision_) {
            message_->setText(tr("正在更新取样预览，请稍候再选择颜色。"));
            return true;
        }
        const auto point = view_->mapToScene(mouse->position().toPoint());
        if (!QRectF(QPointF(), source_size_).contains(point))
            return true;
        const QPoint sample(
            int(point.x() / source_size_.width() * point_sample_preview_.width()),
            int(point.y() / source_size_.height() * point_sample_preview_.height()));
        if (point_sample_preview_.rect().contains(sample)) {
            history_timer_->stop();
            record_history();
            tone_->sample_point_color(point_sample_preview_.pixelColor(sample));
            history_timer_->stop();
            record_history();
        }
        return true;
    }
    if (object != view_->viewport() || edit_mode_->currentIndex() == 0 || compare_->isChecked() ||
        source_size_.isEmpty() || job_ || commit_job_)
        return QDialog::eventFilter(object, event);
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseMove &&
        event->type() != QEvent::MouseButtonRelease)
        return QDialog::eventFilter(object, event);
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (event->type() != QEvent::MouseMove && mouse->button() != Qt::LeftButton)
        return false;
    const int mode = edit_mode_->currentIndex();
    if (mode == 6)
        return healing_event(mouse);
    if (mode == 5)
        return crop_event(mouse);
    if (mask_component_event(mouse))
        return true;
    const auto point = view_->mapToScene(mouse->position().toPoint());
    const double x = std::clamp(point.x() / source_size_.width(), 0., 1.);
    const double y = std::clamp(point.y() / source_size_.height(), 0., 1.);
    const QJsonObject sample{{"x", x}, {"y", y}};
    if (event->type() == QEvent::MouseButtonPress) {
        if (adjustment_->currentIndex() <= 0) {
            message_->setText(tr("请先新建或选择一个局部调整，再在图上建立范围"));
            return true;
        }
        local_stroke_ = {
            {"mode", mode == 3   ? "linear"
                     : mode == 4 ? "radial"
                                 : "brush"},
            {"subtract", mode == 2},
            {"operation",
             mode == 2                                                        ? "subtract"
             : findChild<QComboBox*>("rawMaskOperation")->currentIndex() == 2 ? "intersect"
             : findChild<QComboBox*>("rawMaskOperation")->currentIndex() == 1 ? "subtract"
                                                                              : "add"},
            {"flow", findChild<QDoubleSpinBox*>("rawMask_flow")->value() / 100},
            {"density", findChild<QDoubleSpinBox*>("rawMask_density")->value() / 100},
            {"radius",
             local_radius_->value() / 2 / std::min(source_size_.width(), source_size_.height())},
            {"feather", local_feather_->value() / 100},
            {"x", x},
            {"y", y},
            {"endX", x},
            {"endY", y},
            {"points", QJsonArray{sample}}};
        QPainterPath path;
        path.moveTo(point);
        stroke_guide_ =
            scene_->addPath(path, QPen(QColor("#63baff"), 2 / std::hypot(view_->transform().m11(),view_->transform().m12())));
        return true;
    }
    if (local_stroke_.isEmpty())
        return false;
    local_stroke_.insert("endX", x);
    local_stroke_.insert("endY", y);
    auto points = local_stroke_.value("points").toArray();
    if (points.size() < 16384)
        points.append(sample);
    local_stroke_.insert("points", points);
    if (stroke_guide_) {
        auto path = stroke_guide_->path();
        const QPointF start(local_stroke_.value("x").toDouble() * source_size_.width(),
                            local_stroke_.value("y").toDouble() * source_size_.height());
        if (mode >= 3) {
            path = {};
            if (mode == 3) {
                path.moveTo(start);
                path.lineTo(point);
            } else if (mode == 4)
                path.addEllipse(QRectF(start, point).normalized());
            else
                path.addRect(QRectF(start, point).normalized());
        } else
            path.lineTo(point);
        stroke_guide_->setPath(path);
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        auto& recipe = sources_[current_].raw_recipe;
        {
            auto locals = recipe.value("locals").toArray();
            const int index = adjustment_->currentIndex() - 1;
            auto local = locals.at(index).toObject();
            auto components = local.value("components").toArray();
            if (components.size() < 4096) {
                local_stroke_.insert("geometryAtCreation", recipe.value("geometry").toObject());
                components.append(local_stroke_);
                mask_component_ = components.size() - 1;
                local.insert("components", components);
                locals[index] = local;
                recipe.insert("locals", locals);
            }
        }
        local_stroke_ = {};
        refresh_mask_components();
        record_history();
        display();
    }
    return true;
}
void RawDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && mask_dragging_) {
        update_mask_component(mask_before_);
        mask_dragging_ = false;
        parameter_gesture_ = false;
        mask_tree_->setEnabled(true);
        history_timer_->stop();
        display();
        return;
    }
    if (event->key() == Qt::Key_Escape && mask_sampling_) {
        mask_range_drawing_ = false;
        findChild<QPushButton*>("rawMaskColorPicker")->setChecked(false);
        return;
    }
    if (event->key() == Qt::Key_Delete && mask_component_ >= 0 && edit_mode_->currentIndex() >= 1 &&
        edit_mode_->currentIndex() <= 4) {
        findChild<QPushButton*>("rawMaskDeleteComponent")->click();
        return;
    }

    if (event->modifiers() == Qt::NoModifier &&
        (event->key() == Qt::Key_H || event->key() == Qt::Key_Z)) {
        findChild<QPushButton*>(event->key() == Qt::Key_H ? "rawHandTool" : "rawZoomTool")->click();
        return;
    }
    if (event->modifiers() == Qt::ControlModifier) {
        if (event->key() == Qt::Key_0) {
            findChild<QPushButton*>("rawZoomFit")->click();
            return;
        }
        if (event->key() == Qt::Key_1) {
            findChild<QPushButton*>("rawZoom100")->click();
            return;
        }
        if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal ||
            event->key() == Qt::Key_Minus) {
            static_cast<RawPreviewView*>(view_)->step_zoom(event->key() == Qt::Key_Minus,
                                                           view_->viewport()->rect().center());
            return;
        }
    }

    if (event->key() == Qt::Key_Escape && tone_->target_mode() >= 0) {
        const bool dragging = tone_->target_dragging();
        tone_->finish_target(true);
        parameter_gesture_ = false;
        history_timer_->stop();
        if (!dragging)
            tone_->set_target_mode(-1);
        return;
    }
    if (event->modifiers() == (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) {
        const int key = event->key();
        const int mode = key == Qt::Key_H   ? 0
                         : key == Qt::Key_S ? 1
                         : key == Qt::Key_L ? 2
                         : key == Qt::Key_T ? 3
                                            : -1;
        if (mode >= 0) {
            tone_->set_target_mode(mode);
            event->accept();
            return;
        }
    }

    if (edit_mode_->currentIndex() == 5) {
        if (event->key() == Qt::Key_Escape && crop_dragging_) {
            crop_dragging_ = false;
            if(crop_handle_==9){if(preview_item_)preview_item_->setTransform(crop_preview_transform_);const QSignalBlocker block(crop_angle_);crop_angle_->setValue(crop_rotation_start_);}
            display();
            return;
        }
        if (event->key() == Qt::Key_X && event->modifiers() == Qt::NoModifier) {
            findChild<QToolButton*>("rawCropSwap")->click();
            return;
        }
        if (event->key() == Qt::Key_A && event->modifiers() == Qt::NoModifier) {
            crop_straighten_->toggle();
            return;
        }
    }
    if (edit_mode_->currentIndex() == 6) {
        const int key = event->key();
        if (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight) {
            healing_size_->setValue(healing_size_->value() *
                                    (key == Qt::Key_BracketLeft ? .9 : 1.1));
            return;
        }
        if (key == Qt::Key_Slash) {
            resample_healing();
            return;
        }
        if (key == Qt::Key_Y) {
            healing_visualize_->toggle();
            return;
        }
        if (key == Qt::Key_B && event->modifiers().testFlag(Qt::ShiftModifier)) {
            healing_type_->setCurrentIndex(1 - healing_type_->currentIndex());
            return;
        }
        if (key == Qt::Key_Period || key == Qt::Key_Comma) {
            const int step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
            healing_threshold_->setValue(healing_threshold_->value() +
                                         (key == Qt::Key_Period ? step : -step));
            return;
        }
    }
    if (edit_mode_->currentIndex() == 6 && event->key() == Qt::Key_Delete) {
        delete_healing();
        return;
    }
    if (event->key() == Qt::Key_Escape &&
        (healing_drawing_ || healing_drag_source_ || healing_delete_area_)) {
        healing_drawing_ = healing_drag_source_ = healing_delete_area_ = healing_drag_size_ =
            healing_new_source_ = false;
        display();
        return;
    }
    if (event->key() == Qt::Key_Escape && white_balance_->sampling()) {
        white_balance_->stop_sampling();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_I && event->modifiers() == Qt::NoModifier &&
        white_balance_->isEnabled() && white_balance_->isVisible()) {
        white_balance_->findChild<QToolButton*>("rawWhiteBalancePicker")->toggle();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && optics_->sampling()) {
        optics_->stop_sampling();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && tone_->point_color_sampling()) {
        tone_->stop_point_sampling();
        return;
    }
    QDialog::keyPressEvent(event);
}
void RawDialog::select(int index) {
    if (index < 0 || index >= int(sources_.size()) || job_ || commit_job_)
        return;
    if (current_ != index) {
        history_timer_->stop();
        parameter_gesture_ = false;
        record_history();
        if (!source_size_.isEmpty()) {
            QVariantMap navigation;
            navigation.insert("transform", QVariant::fromValue(view_->transform()));
            navigation.insert("center", view_->mapToScene(view_->viewport()->rect().center()));
            navigation.insert("detail", detail_preview_);
            files_->item(current_)->setData(Qt::UserRole + 20, navigation);
        }
        const auto navigation = files_->item(index)->data(Qt::UserRole + 20).toMap();
        setProperty("restorePhotoNavigation", navigation);
        detail_preview_ = navigation.value("detail", false).toBool();
    }
    findChild<QPushButton*>("rawMaskColorPicker")->setChecked(false);
    mask_dragging_ = false;
    mask_component_ = -1;
    mask_tree_->setEnabled(true);
    fit_next_ = fit_next_ || current_ != index;
    if (current_ != index)
        source_size_ = {};
    current_ = index;
    try {
        const auto capability=editing::raw_capabilities(sources_[current_].raw_source);
        setProperty("rawDenoiseSupported",capability["rawDenoiseSupported"].toBool());
        setProperty("rawDngSupported",capability["rawDngSupported"].toBool());
        denoise_->setToolTip(capability["rawDenoiseReason"].toString());
        findChild<QLabel*>("rawCapabilityNote")->setText(capability["rawDenoiseReason"].toString());
        findChild<QLabel*>("rawCapabilityNote")->setVisible(!capability["rawDenoiseSupported"].toBool());
        findChild<QPushButton*>("rawSaveDng")->setToolTip(capability["rawDngSupported"].toBool()
            ? tr("保存传感器 RAW 数据和显影设置到独立 DNG 副本") : capability["rawDngReason"].toString());
        if (!capability["rawDenoiseSupported"].toBool()) sources_[current_].raw_recipe.insert("denoise",false);
    } catch (const std::exception&) {
        setProperty("rawDenoiseSupported",false);setProperty("rawDngSupported",false);
    }

    healing_drawing_ = healing_drag_source_ = healing_delete_area_ = healing_drag_size_ =
        healing_new_source_ = false;
    refresh_healing();
    crop_dragging_ = false;
    local_stroke_ = {};
    loading_ = true;
    const auto& recipe = sources_[current_].raw_recipe;
    auto settings = editing::tone_from_json(recipe.value("tone").toObject());
    if (!recipe.contains("tone"))
        settings.exposure = recipe.value("exposure").toDouble();
    tone_->set_settings(settings);
    optics_->set_settings(editing::optics_from_json(recipe.value("optics").toObject()),
                          recipe.value("metadata").toObject());
    geometry_->set_settings(editing::geometry_from_json(recipe.value("geometry").toObject()));
    crop_angle_->setValue(geometry_->settings().rotation);
    refresh_white_balance();
    refresh_local_controls();
    adjustment_->setCurrentIndex(0);
    denoise_->setChecked(recipe.value("denoise").toBool());
    denoise_amount_->setValue(
        recipe.value("denoiseAmount").toDouble(recipe.value("denoise").toBool() ? 100 : 50));
    backend_->setCurrentIndex(recipe.value("directml").toBool(true) ? 0 : 1);
    const bool custom = recipe.value("customModel").toBool();
    model_choice_->setCurrentIndex(custom ? 1 : 0);
    model_->setText(custom ? recipe.value("modelPath").toString() : bundled_model());
    displayed_recipe_ = recipe;
    loading_ = false;
    // Selecting an unvisited photo must decode (or reuse its identical source
    // cache), rather than leaving the filmstrip selection with a stale preview.
    if (!cache_.contains(cache_keys_[current_]))
        start();
    else
        display();
}
void RawDialog::apply_performance_preferences(bool clear_cache) {
    const auto preferences = read_canvas_preferences();
    try {
        cache_.configure(qint64(preferences.raw_memory_mb) * 1024 * 1024,
                         qint64(preferences.raw_disk_mb) * 1024 * 1024);
        if (clear_cache)
            cache_.clear();
        refine_timer_->setInterval(preferences.raw_refine_delay);
        setProperty("cacheMemoryBytes", cache_.memory_bytes());
        setProperty("cacheDiskBytes", cache_.disk_bytes());
        if (clear_cache) {
            preview_source_ = {};
            preview_reference_ = {};
            proxy_source_ = {};
            proxy_reference_ = {};
            preview_key_.clear();
        }
        if (preview_job_)
            preview_job_->cancelled.store(true);
        ++preview_revision_;
        if (!cache_.contains(cache_keys_[current_]))
            start();
        else
            display();
        update_ready();
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
    }
}
void RawDialog::start() {
    if (job_ || commit_job_ || closing_ || sources_.empty())
        return;
    editing::RawOptions options;
    options.denoise = denoise_->isChecked();
    options.directml = backend_->currentIndex() == 0;
    options.model_path = model_->text();
    options.custom_model = model_choice_->currentIndex() == 1;
    options.high_quality = editing::raw_high_quality(sources_[current_].raw_recipe);
    const auto bytes = sources_[current_].raw_source;
    QString key;
    try {
        key = editing::raw_cache_key(bytes, options);
        if (const auto cached = cache_.get(key)) {
            cache_keys_[current_] = key;
            sources_[current_].raw_recipe.insert("metadata", cached->metadata);
            refresh_white_balance();
            message_->setText(tr("已复用显影缓存"));
            display();
            if (batch_)
                QTimer::singleShot(0, this, &RawDialog::compute_all);
            return;
        }
    } catch (const std::exception& error) {
        cache_keys_[current_].clear();
        batch_ = false;
        message_->setText(QString::fromUtf8(error.what()));
        display();
        return;
    }
    // A new job must never leave an older result eligible for submission.
    cache_keys_[current_].clear();
    auto& recipe = sources_[current_].raw_recipe;
    recipe.insert("denoise", options.denoise);
    recipe.insert("denoiseAmount", denoise_amount_->value());
    recipe.insert("directml", options.directml);
    recipe.insert("modelPath", options.model_path);
    recipe.insert("customModel", options.custom_model);
    job_ = std::make_unique<Job>();
    job_->index = current_;
    auto* job = job_.get();
    job_->key = key;
    job_->result = std::async(std::launch::async, [job, bytes, options] {
        QTemporaryFile source;
        if (!source.open() || source.write(bytes) != bytes.size() || !source.flush())
            throw editing::EditError(QStringLiteral("无法创建 RAW 会话缓存"));
        return editing::develop_raw(
            source.fileName(), options, job->cancelled,
            [job](int done, int total) { job->percentage.store(done * 100 / std::max(total, 1)); });
    });
    files_->setEnabled(false);
    denoise_->setEnabled(false);
    backend_->setEnabled(false);
    model_->setEnabled(false);
    model_choice_->setEnabled(false);
    findChild<QPushButton*>("rawBrowse")->setEnabled(false);
    compute_->setEnabled(false);
    apply_->setEnabled(false);
    message_->setText(options.denoise ? tr("正在进行 AI RAW 降噪…") : tr("正在处理源图像…"));
    progress_->setRange(0, 100);
    progress_->show();
    findChild<QPushButton*>("rawCancelCompute")->setEnabled(true);
    findChild<QPushButton*>("rawCancelCompute")->show();
    progress_->setValue(0);
    timer_->start();
}
void RawDialog::finish() {
    if (!job_)
        return;
    progress_->setValue(job_->percentage.load());
    if (job_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    timer_->stop();
    try {
        const auto result = job_->result.get();
        if (!closing_ && !job_->cancelled.load()) {
            cache_.put(job_->key, result);
            cache_keys_[job_->index] = job_->key;
            auto& recipe = sources_[job_->index].raw_recipe;
            recipe.insert("version", 1);
            recipe.insert("metadata", result.metadata);
            message_->setText(tr("显影完成 · %1 × %2 · 高精度 RGB")
                                  .arg(result.image.width())
                                  .arg(result.image.height()));
        }
    } catch (const std::exception& error) {
        batch_ = false;
        message_->setText(QString::fromUtf8(error.what()));
        if(!closing_&&!job_->cancelled.load())findChild<QPushButton*>("rawOpenEmbedded")->show();
    }
    if (job_->cancelled.load())
        message_->setText(tr("计算已取消，可重新开始降噪或关闭 AI RAW 降噪"));
    job_.reset();
    refresh_white_balance();
    if (closing_) {
        QDialog::reject();
        return;
    }
    files_->setEnabled(true);
    denoise_->setEnabled(property("rawDenoiseSupported").toBool());
    backend_->setEnabled(true);
    model_->setEnabled(true);
    model_choice_->setEnabled(true);
    findChild<QPushButton*>("rawBrowse")->setEnabled(model_choice_->currentIndex() == 1);
    compute_->setEnabled(denoise_->isChecked());
    display();
    if (batch_)
        QTimer::singleShot(0, this, &RawDialog::compute_all);
}
void RawDialog::display() {
    apply_multiselect_delta();
    full_preview_requested_ = false;
    if (preview_job_ && preview_job_->full_resolution)
        preview_job_->cancelled.store(true);
    if (detail_preview_)
        refine_timer_->start();
    else
        refine_timer_->stop();
    ++preview_revision_;
    setProperty("previewRequested", QVariant::fromValue(preview_revision_));
    preview_pending_ = true;
    preview_timer_->start();
    update_ready();
}
void RawDialog::finish_preview() {
    if (crop_dragging_ || !local_stroke_.isEmpty() || healing_drawing_ || healing_drag_source_ ||
        healing_delete_area_ || mask_dragging_ || mask_range_drawing_)
        return;
    if (preview_job_) {
        if (preview_job_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            const auto result = preview_job_->result.get();
            if (!preview_job_->comparison && !preview_job_->full_resolution &&
                preview_job_->key == cache_keys_[current_]) {
                proxy_source_ = result[1];
                proxy_reference_ = result[4];
            }
            const int target = overlay_->isChecked() ? adjustment_->currentIndex() - 1 : -1;
            if (!closing_ && !preview_job_->cancelled.load() && preview_job_->index == current_ &&
                preview_job_->key == cache_keys_[current_] &&
                preview_job_->comparison == compare_->isChecked() &&
                preview_job_->target == target &&
                preview_job_->revision > property("previewPresented").toULongLong()) {
                point_sample_preview_ = result[3];
                inspection_source_ = result[5];
                before_preview_ = result[6];
                present_preview(result[0], result[2]);
                setProperty("previewPresented", QVariant::fromValue(preview_job_->revision));
            }
        } catch (const std::exception& error) {
            if (!preview_job_->cancelled.load())
                message_->setText(QString::fromUtf8(error.what()));
        }
        preview_job_.reset();
    }
    if (!preview_pending_ || closing_ || commit_job_) {
        preview_timer_->stop();
        return;
    }
    preview_pending_ = false;
    try {
        const auto key = cache_keys_[current_];
        if (preview_key_ != key) {
            preview_source_ = {};
            proxy_source_ = {};
            preview_reference_ = {};
            proxy_reference_ = {};
            preview_key_ = key;
            if (const auto cached = cache_.get(key)) {
                preview_source_ = cached->image;
                preview_reference_ = cached->reference;
            }
        }
        if (preview_source_.isNull())
            return;
        source_size_ = preview_source_.size();
        auto source =
            !full_preview_requested_ && !proxy_source_.isNull() ? proxy_source_ : preview_source_;
        auto reference = !full_preview_requested_ && !proxy_reference_.isNull()
                             ? proxy_reference_
                             : preview_reference_;
        denoise_compare_->setEnabled(!reference.isNull());
        const double denoise_amount = denoise_compare_->isDown() ? 0 : denoise_amount_->value();
        const bool comparison = compare_->isChecked();
        if (comparison && !sources_[current_].pixels.isNull())
            source = sources_[current_].pixels;
        const auto recipe = sources_[current_].raw_recipe;
        auto sample_recipe = recipe;
        const int compare_layout = compare_layout_->currentIndex();
        auto before_recipe = before_recipes_[current_];
        before_recipe.insert("metadata", recipe.value("metadata"));
        const int optics_auxiliary = optics_->auxiliary_mode();
        if (optics_->sampling() || optics_auxiliary != 0) {
            auto optical = editing::optics_from_json(recipe.value("optics").toObject());
            optical.purple = optical.green = 0;
            sample_recipe.insert("optics", editing::optics_json(optical));
        }
        if (mask_sampling_)
            sample_recipe.remove("locals");
        const auto point_range = tone_->point_color_visualized();
        if (tone_->point_color_sampling() || point_range || tone_->target_mode() >= 0) {
            // Sample the input to this point-color adjustment, never a previously shifted output.
            const int local_index = adjustment_->currentIndex() - 1;
            auto clear_points = [target_mode = tone_->target_mode()](QJsonObject& container) {
                auto tone = container.value("tone").toObject();
                if (!tone.isEmpty()) {
                    tone.insert("pointColors", QJsonArray{});
                    if (target_mode >= 0 && target_mode < 3) {
                        auto neutral = editing::tone_from_json(tone);
                        neutral.hue.fill(0);
                        neutral.saturation_bands.fill(0);
                        neutral.luminance_bands.fill(0);
                        tone = editing::tone_json(neutral);
                    } else if (target_mode == 3) {
                        auto neutral = editing::tone_from_json(tone);
                        neutral.parametric_curve.fill(0);
                        tone = editing::tone_json(neutral);
                    }
                    container.insert("tone", tone);
                }
            };
            auto locals = sample_recipe.value("locals").toArray();
            if (local_index >= 0 && local_index < locals.size()) {
                auto local = locals[local_index].toObject();
                clear_points(local);
                locals[local_index] = local;
                sample_recipe.insert("locals", locals);
            } else
                clear_points(sample_recipe);
        }
        const int target = overlay_->isChecked() ? adjustment_->currentIndex() - 1 : -1;
        const int mask_overlay_mode = findChild<QComboBox*>("rawMaskOverlayMode")->currentIndex();
        const int detail_preview = tone_->detail_preview();
        const int detail_target = adjustment_->currentIndex() - 1;
        const auto sliders = tone_->findChildren<QSlider*>();
        const bool dragging = parameter_gesture_ ||
            std::any_of(sliders.begin(), sliders.end(),
                        [](const QSlider* slider) { return slider->isSliderDown(); });
        const auto& settings = tone_->settings();
        const bool spatial = settings.details.luminance != 0 || settings.details.color != 0 ||
                             settings.sharpening != 0 || settings.dehaze != 0 ||
                             settings.texture != 0 || settings.clarity != 0;
        const auto preferences = read_canvas_preferences();
        const int edge = full_preview_requested_ ? std::max(source.width(), source.height())
                         : dragging && spatial   ? preferences.raw_drag_edge
                                                 : preferences.raw_preview_edge;
        setProperty("previewEdge", edge);
        preview_job_ = std::make_unique<PreviewJob>();
        preview_job_->revision = preview_revision_;
        preview_job_->key = key;
        preview_job_->comparison = comparison;
        preview_job_->index = current_;
        preview_job_->full_resolution = full_preview_requested_;
        preview_job_->target = target;
        auto* preview_job = preview_job_.get();
        preview_job_->result = std::async(std::launch::async, [source, reference, denoise_amount,
                                                               recipe, sample_recipe, comparison,
                                                               compare_layout, before_recipe,
                                                               target, mask_overlay_mode, edge,
                                                               point_range, detail_preview, optics_auxiliary,
                                                               detail_target, preview_job] {
            auto proxy = source;
            if (std::max(proxy.width(), proxy.height()) > edge)
                proxy = proxy.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            auto reference_proxy = reference;
            if (!reference_proxy.isNull() && reference_proxy.size() != proxy.size())
                reference_proxy = reference_proxy.scaled(proxy.size(), Qt::IgnoreAspectRatio,
                                                         Qt::SmoothTransformation);
            const auto blended =
                comparison ? proxy
                           : editing::blend_raw_denoise(proxy, reference_proxy, denoise_amount,
                                                        &preview_job->cancelled);
            const auto image = comparison ? proxy
                                          : editing::apply_raw_adjustments(
                                                blended, recipe, false, &preview_job->cancelled,
                                                double(proxy.width()) / source.width(),
                                                detail_preview, detail_target);
            auto visible = editing::display_image(image);
            const auto sample_preview =
                comparison || (sample_recipe == recipe && detail_preview == 0)
                    ? visible
                    : editing::display_image(editing::apply_raw_adjustments(
                          blended, sample_recipe, false, &preview_job->cancelled,
                          double(proxy.width()) / source.width()));
            const auto histogram_sample =
                (detail_preview != 0 ? sample_preview : visible)
                    .scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGB32);
            QImage inspection_source = detail_preview != 0 ? sample_preview : visible;
            if (!comparison && optics_auxiliary != 0) {
                const auto optics = editing::optics_from_json(recipe.value("optics").toObject());
                const bool purple = optics_auxiliary == 1 || optics_auxiliary == 3;
                const auto range = purple ? optics.purple_hue : optics.green_hue;
                visible = visible.convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < visible.height(); ++y) {
                    if (preview_job->cancelled.load()) throw editing::EditError("Preview cancelled");
                    auto* row = reinterpret_cast<QRgb*>(visible.scanLine(y));
                    for (int x = 0; x < visible.width(); ++x) {
                        const auto color = sample_preview.pixelColor(x, y);
                        const auto hsl = editing::point_color_hsl({color.redF(), color.greenF(), color.blueF()});
                        const double hue = (hsl[0] * 360 - (purple ? 180 : 60)) / (purple ? 1.8 : 1.2);
                        double weight = hsl[1] < .01 ? 0 : std::clamp(std::min(hue-range[0], range[1]-hue)/5., 0., 1.);
                        if (optics_auxiliary >= 3) {
                            double low = 1, high = 0;
                            for (int dy = -2; dy <= 2; ++dy)
                                for (int dx = -2; dx <= 2; ++dx) {
                                    const double value = sample_preview.pixelColor(std::clamp(x+dx, 0, visible.width()-1), std::clamp(y+dy, 0, visible.height()-1)).greenF();
                                    low = std::min(low, value); high = std::max(high, value);
                                }
                            weight *= std::clamp((high-low)*5, 0., 1.) * (purple ? optics.purple : optics.green)/20.;
                        }
                        const int gray = qRound(255 * (1-weight));
                        row[x] = qRgba(gray, gray, gray, color.alpha());
                    }
                }
            }
            if (!comparison && point_range) {
                visible = visible.convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < visible.height(); ++y) {
                    if (preview_job->cancelled.load())
                        throw editing::EditError("Preview cancelled");
                    auto* row = reinterpret_cast<QRgb*>(visible.scanLine(y));
                    for (int x = 0; x < visible.width(); ++x) {
                        const auto color = QColor::fromRgba(row[x]);
                        const auto sample = sample_preview.pixelColor(x, y);
                        const auto hsl = editing::point_color_hsl(
                            {sample.redF(), sample.greenF(), sample.blueF()});
                        const double weight = editing::point_color_weight(hsl, *point_range);
                        const int gray = qGray(row[x]);
                        row[x] =
                            qRgba(qRound(gray + (color.red() - gray) * weight),
                                  qRound(gray + (color.green() - gray) * weight),
                                  qRound(gray + (color.blue() - gray) * weight), color.alpha());
                    }
                }
            }
            const auto locals = recipe.value("locals").toArray();
            if (!comparison && target >= 0 && target < locals.size()) {
                auto range_recipe = recipe;
                range_recipe.remove("locals");
                QImage range_reference;
                for (const auto& component :
                     locals.at(target).toObject().value("components").toArray()) {
                    const auto mode = component.toObject().value("mode").toString();
                    if (mode == "color" || mode == "luminance") {
                        range_reference = editing::apply_raw_adjustments(
                            blended, range_recipe, false, &preview_job->cancelled,
                            double(proxy.width()) / source.width());
                        break;
                    }
                }
                const auto mask =
                    editing::raw_local_mask(image.size(), locals.at(target).toObject(),
                                            recipe.value("geometry").toObject(), range_reference);
                if (mask_overlay_mode != 0) {
                    visible = visible.convertToFormat(QImage::Format_ARGB32);
                    for (int y = 0; y < mask.height(); ++y) {
                        auto* row = reinterpret_cast<QRgb*>(visible.scanLine(y));
                        for (int x = 0; x < mask.width(); ++x) {
                            const int weight = mask.constScanLine(y)[x];
                            const auto rgb = row[x];
                            const int background = mask_overlay_mode == 3 ? 255 : 0;
                            row[x] = mask_overlay_mode == 1
                                         ? qRgba(weight, weight, weight, 255)
                                         : qRgba((qRed(rgb) * weight + background * (255 - weight) +
                                                  127) /
                                                     255,
                                                 (qGreen(rgb) * weight +
                                                  background * (255 - weight) + 127) /
                                                     255,
                                                 (qBlue(rgb) * weight +
                                                  background * (255 - weight) + 127) /
                                                     255,
                                                 255);
                        }
                    }
                } else {
                    QImage overlay(image.size(), QImage::Format_ARGB32);
                    for (int y = 0; y < mask.height(); ++y) {
                        auto* row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
                        for (int x = 0; x < mask.width(); ++x)
                            row[x] = qRgba(255, 55, 55, mask.constScanLine(y)[x] / 3);
                    }
                    QPainter painter(&visible);
                    painter.drawImage(0, 0, overlay);
                }
            }
            QImage before;
            if (compare_layout > 0 && !comparison) {
                before = editing::display_image(editing::apply_raw_adjustments(
                    blended, before_recipe, false, &preview_job->cancelled,
                    double(proxy.width()) / source.width()));
                if (compare_layout <= 2) {
                    QImage combined = visible.convertToFormat(QImage::Format_ARGB32);
                    QPainter painter(&combined);
                    painter.setClipRect(compare_layout == 1
                                            ? QRect(0, 0, visible.width() / 2, visible.height())
                                            : QRect(0, 0, visible.width(), visible.height() / 2));
                    painter.drawImage(0, 0, before);
                    painter.end();
                    visible = combined;
                    inspection_source = combined;
                }
            }
            return std::array{visible,        proxy,           histogram_sample,
                              sample_preview, reference_proxy, inspection_source,
                              before};
        });
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
    }
}
void RawDialog::present_preview(const QImage& visible, const QImage& histogram_sample) {
    const int layout = compare_layout_->currentIndex();
    const QSize extent = source_size_;
    setProperty("previewFullResolution", visible.size() == extent);
    auto transform = view_->transform();
    const int turns=sources_[current_].raw_recipe.value("quarterTurns").toInt();
    if(view_->property("rawQuarterTurns").toInt()!=turns){
        const double scale=std::hypot(transform.m11(),transform.m12());
        transform=QTransform().rotate(turns*90).scale(scale,scale);
        view_->setTransform(transform);view_->setProperty("rawQuarterTurns",turns);fit_next_=true;
    }
    const auto center = view_->mapToScene(view_->viewport()->rect().center());
    // A pending render must not invalidate a brush gesture currently being drawn.
    if (crop_dragging_ || !local_stroke_.isEmpty() || healing_drawing_ || healing_drag_source_ ||
        healing_delete_area_ || mask_dragging_ || mask_range_drawing_) {
        preview_pending_ = true;
        return;
    }
    stroke_guide_ = nullptr;
    mask_guide_ = nullptr;
    preview_item_ = nullptr;
    scene_->clear();
    auto* pixmap = scene_->addPixmap(QPixmap{});
    preview_item_ = pixmap;
    displayed_preview_ = visible;
    refresh_inspection();
    pixmap->setTransform(QTransform::fromScale(double(extent.width()) / visible.width(),
                                               double(extent.height()) / visible.height()));
    if (!compare_->isChecked() && layout == 0 &&
        (sources_[current_].raw_recipe.contains("crop") || geometry_->settings().constrain_crop)) {
        const auto crop = editing::raw_crop_rect(source_size_, sources_[current_].raw_recipe);
        QPainterPath shade;
        shade.addRect(QRectF(QPointF(), QSizeF(source_size_)));
        shade.addRect(crop);
        scene_->addPath(shade, Qt::NoPen, QColor(0, 0, 0, 170));
        scene_->addRect(crop, QPen(Qt::white, 1));
    }
    files_->item(current_)->setIcon(QPixmap::fromImage(
        histogram_sample.transformed(QTransform().rotate(turns*90)).scaled(112, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    tone_->set_curve_histogram(histogram_sample);
    const auto& sample = histogram_sample;
    std::array<std::array<int, 256>, 3> bins{};
    int maximum = 1;
    for (int y = 0; y < sample.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x) {
            const std::array<int, 3> values{qRed(row[x]), qGreen(row[x]), qBlue(row[x])};
            for (int c = 0; c < 3; ++c)
                maximum = std::max(maximum, ++bins[c][values[c]]);
        }
    }
    QPixmap histogram(256, 100);
    histogram.fill(QColor("#1c2024"));
    QPainter painter(&histogram);
    const std::array<QColor, 3> colors{QColor(240, 80, 75, 140), QColor(80, 210, 105, 140),
                                       QColor(65, 135, 245, 140)};
    for (int c = 0; c < 3; ++c) {
        painter.setPen(colors[c]);
        for (int i = 0; i < 256; ++i)
            painter.drawLine(i, 99, i, 99 - bins[c][i] * 98 / maximum);
    }
    painter.end();
    histogram_->setPixmap(histogram);
    histogram_->setProperty("sampleCount", sample.width() * sample.height());
    image_info_->setText(
        tr("%1 × %2 · 线性 Rec.2020 · FP32").arg(source_size_.width()).arg(source_size_.height()));
    const auto metadata = sources_[current_].raw_recipe.value("metadata").toObject();
    optics_->set_settings(optics_->settings(), metadata);
    const double shutter = metadata.value("shutter").toDouble();
    const QString shutter_text = shutter > 0 && shutter < 1 ? tr("1/%1 秒").arg(qRound(1 / shutter))
                                                            : tr("%1 秒").arg(shutter);
    findChild<QLabel*>("rawCaptureInfo")
        ->setText(tr("ISO %1    %2 mm    f/%3    %4")
                      .arg(metadata.value("iso").toDouble(), 0, 'f', 0)
                      .arg(metadata.value("focalLength").toDouble(), 0, 'f', 0)
                      .arg(metadata.value("aperture").toDouble(), 0, 'f', 1)
                      .arg(shutter_text));
    scene_->setSceneRect(QRectF(QPointF(), QSizeF(extent)));
    if (layout > 0) {
        const auto text = [&](const QString& label, QPointF at) {
            auto* item = scene_->addSimpleText(label);
            item->setBrush(Qt::white);
            item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            item->setPos(at);
        };
        if (layout <= 2) {
            text(tr("之前"), QPointF(10, 10));
            text(tr("之后"), layout == 1 ? QPointF(extent.width() / 2. + 10, 10)
                                         : QPointF(10, extent.height() / 2. + 10));
            const QPen divider(Qt::white, 0);
            if (layout == 1)
                scene_->addLine(extent.width() / 2., 0, extent.width() / 2., extent.height(),
                                divider);
            else
                scene_->addLine(0, extent.height() / 2., extent.width(), extent.height() / 2.,
                                divider);
        } else {
            text(tr("之后"), QPointF(10, 10));
            before_scene_->clear();
            before_scene_->setSceneRect(scene_->sceneRect());
            auto* item = before_scene_->addPixmap(QPixmap::fromImage(before_preview_));
            item->setTransform(
                QTransform::fromScale(double(source_size_.width()) / before_preview_.width(),
                                      double(source_size_.height()) / before_preview_.height()));
            auto* label = before_scene_->addSimpleText(tr("之前"));
            label->setBrush(Qt::white);
            label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            label->setPos(10, 10);
        }
    }
    if (fit_next_) {
        view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
        fit_next_ = false;
    } else {
        view_->setTransform(transform);
        view_->centerOn(center);
    }
    const auto restored_navigation = property("restorePhotoNavigation").toMap();
    if (!restored_navigation.isEmpty()) {
        view_->setTransform(restored_navigation.value("transform").value<QTransform>());
        const double scale = std::hypot(view_->transform().m11(), view_->transform().m12());
        const double margin = read_canvas_preferences().overscroll
                                  ? std::max(view_->viewport()->width(), view_->viewport()->height()) /
                                        std::max(.001, scale)
                                  : 0.;
        view_->setSceneRect(scene_->sceneRect().adjusted(-margin, -margin, margin, margin));
        view_->centerOn(restored_navigation.value("center").toPointF());
        setProperty("restorePhotoNavigation", QVariant{});
    }
    if (layout >= 3) {
        before_view_->setSceneRect(view_->sceneRect());
        before_view_->setTransform(view_->transform());
        before_view_->centerOn(view_->mapToScene(view_->viewport()->rect().center()));
    }
    if (!compare_->isChecked() && layout == 0 && edit_mode_->currentIndex() == 5)
        crop_overlays();
    if (layout == 0) {
        healing_overlays();
        mask_component_overlays();
    }
    update_ready();
}
void RawDialog::update_ready() {
    setProperty("cacheMemoryBytes", cache_.memory_bytes());
    setProperty("cacheDiskBytes", cache_.disk_bytes());
    const bool busy = bool(job_) || bool(commit_job_);
    compute_->setEnabled(!busy && denoise_->isChecked() && property("rawDenoiseSupported").toBool());
    denoise_->setEnabled(!busy && property("rawDenoiseSupported").toBool());
    findChild<QPushButton*>("rawCancelCompute")->setEnabled(busy);
    findChild<QPushButton*>("rawCancelCompute")->setVisible(busy);
    progress_->setVisible(busy);
    if (commit_job_)
        progress_->setRange(0, 0);
    bool ready = !busy;
    int count{};
    for (std::size_t i = 0; i < cache_keys_.size(); ++i) {
        if (included(i)) {
            ++count;
            ready = ready && cache_.contains(cache_keys_[i]);
        }
    }
    apply_->setEnabled(ready && count > 0);
    findChild<QPushButton*>("rawSaveImages")->setEnabled(ready && count>0);
    findChild<QPushButton*>("rawOpenOptions")->setEnabled(ready && count>0);
    findChild<QPushButton*>("rawDone")->setEnabled(!job_ && !commit_job_);
    findChild<QPushButton*>("rawSaveDng")->setEnabled(!job_ && !commit_job_ && !sources_[current_].raw_source.isEmpty() && property("rawDngSupported").toBool());
    apply_->setText(sources_.front().pixels.isNull() ? tr("打开为源图层（%1）").arg(count)
                                                     : tr("确定"));
}
bool RawDialog::included(std::size_t index) const {
    return index < sources_.size() && index < std::size_t(files_->count()) &&
           files_->item(int(index))->checkState() == Qt::Checked &&
           !sources_[index].raw_recipe.value("rejected").toBool();
}
void RawDialog::compute_all() {
    if (job_ || commit_job_ || closing_)
        return;
    if (!batch_)
        batch_attempts_ = 0;
    batch_ = true;
    for (std::size_t i = 0; i < cache_keys_.size(); ++i) {
        if (included(i) && !cache_.contains(cache_keys_[i])) {
            if (batch_attempts_++ >= sources_.size()) {
                batch_ = false;
                message_->setText(tr("本组图像超过 RAW 会话缓存预算，请减少同时打开的文件数"));
                return;
            }
            files_->setCurrentRow(int(i));
            start();
            return;
        }
    }
    batch_ = false;
    message_->setText(tr("全部显影完成，可提交"));
    update_ready();
}
void RawDialog::synchronize() {
    if (job_ || commit_job_)
        return;
    history_timer_->stop();
    record_history();
    QDialog dialog(this);
    dialog.setObjectName("rawSyncDialog");
    dialog.setWindowTitle(tr("同步设置"));
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    auto* layout = new QVBoxLayout(&dialog);
    auto* menu = new QMenuBar(&dialog);
    menu->addAction(tr("同步设置"))->setEnabled(false);
    layout->addWidget(new WindowHeader(menu, &dialog));
    layout->addWidget(
        new QLabel(tr("将当前照片的所选设置同步到胶片条中勾选的其他照片。"), &dialog));
    const QStringList labels{tr("光线"),       tr("颜色"),     tr("效果"), tr("曲线与色阶"),
                             tr("色彩混合器"), tr("颜色分级"), tr("锐化"), tr("AI 降噪与模型")};
    std::array<QCheckBox*, 14> choices{};
    for (int index = 0; index < 14; ++index) {
        auto* check = new QCheckBox(index < 8     ? labels[index]
                                    : index == 8  ? tr("局部蒙版与调整")
                                    : index == 9  ? tr("裁剪")
                                    : index == 10 ? tr("校正")
                                    : index == 11 ? tr("光学")
                                    : index == 12 ? tr("几何")
                                                  : tr("修复与仿制"),
                                    &dialog);
        check->setObjectName(QString("rawSync%1").arg(index));
        check->setChecked(index < 8 || index == 10 || index == 11);
        choices[index] = check;
        layout->addWidget(check);
    }
    auto* all = new QHBoxLayout;
    for (bool selected : {true, false}) {
        auto* button = new QPushButton(selected ? tr("全选") : tr("全不选"), &dialog);
        button->setObjectName(selected ? "rawSyncAll" : "rawSyncNone");
        button->setAutoDefault(false);
        all->addWidget(button);
        connect(button, &QPushButton::clicked, &dialog, [choices, selected] {
            for (auto* choice : choices)
                choice->setChecked(selected);
        });
    }
    layout->addLayout(all);
    auto* footer = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(footer);
    connect(footer, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(footer, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    editing::RawGroups groups{};
    for (int i = 0; i < 14; ++i)
        groups[i] = choices[i]->isChecked();
    bool modified{};
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        if (int(i) == current_ || !included(i))
            continue;
        auto next = editing::transfer_raw_settings(sources_[current_].raw_recipe,
                                                   sources_[i].raw_recipe, groups);
        if (next != sources_[i].raw_recipe) {
            sources_[i].raw_recipe = std::move(next);
            if (groups[7])
                cache_keys_[i].clear();
            modified = true;
        }
    }
    message_->setText(modified ? tr("所选设置已同步；未勾选的设置保持原值")
                               : tr("没有需要同步的更改"));
    if (modified)
        record_history();
    update_ready();
}
void RawDialog::record_history() {
    if (parameter_gesture_) {
        history_timer_->start();
        return;
    }
    if (auto* curve = findChild<QWidget*>("toneCurve");
        curve && curve->property("curveDragging").toBool()) {
        history_timer_->start();
        return;
    }
    std::vector<QJsonObject> recipes;
    for (const auto& source : sources_) {
        auto recipe = source.raw_recipe;
        recipe.remove("metadata");
        recipe.remove("version");
        recipes.push_back(std::move(recipe));
    }
    if (!history_.empty() && history_[history_position_] == recipes)
        return;
    if (!history_.empty())
        history_.resize(history_position_ + 1);
    history_.push_back(std::move(recipes));
    const auto limit =
        std::size_t(std::clamp(QSettings().value("history/states", 100).toInt(), 2, 1000));
    if (history_.size() > limit)
        history_.erase(history_.begin(), history_.end() - limit);
    history_position_ = history_.size() - 1;
}
void RawDialog::restore_history(int direction) {
    if (history_timer_->isActive()) {
        history_timer_->stop();
        record_history();
    }
    if (job_ || commit_job_ || history_.empty() || (direction < 0 && history_position_ == 0) ||
        (direction > 0 && history_position_ + 1 == history_.size()))
        return;
    history_position_ = std::size_t(qint64(history_position_) + direction);
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        sources_[i].raw_recipe = history_[history_position_][i];
        const auto& recipe = sources_[i].raw_recipe;
        editing::RawOptions options;
        options.denoise = recipe.value("denoise").toBool();
        options.directml = recipe.value("directml").toBool(true);
        options.model_path = recipe.value("modelPath").toString(bundled_model());
        options.custom_model = recipe.value("customModel").toBool();
        options.high_quality = editing::raw_high_quality(recipe);
        try {
            cache_keys_[i] = editing::raw_cache_key(sources_[i].raw_source, options);
            if (const auto cached = cache_.get(cache_keys_[i]))
                sources_[i].raw_recipe.insert("metadata", cached->metadata);
        } catch (const std::exception&) {
            cache_keys_[i].clear();
        }
    }
    const auto keep_fit = fit_next_;
    const auto keep_target = adjustment_->currentIndex();
    select(current_);
    refresh_filmstrip();
    adjustment_->setCurrentIndex(std::min(keep_target, adjustment_->count() - 1));
    fit_next_ = keep_fit;
    message_->setText(direction < 0 ? tr("已撤销会话修改") : tr("已重做会话修改"));
}
void RawDialog::accept() {
    begin_output(QApplication::keyboardModifiers().testFlag(Qt::AltModifier)?3:0);
}
void RawDialog::begin_output(int mode) {
    if (job_ || commit_job_ || closing_ || property("autoToneBusy").toBool() || (mode!=2 && mode!=5 && !apply_->isEnabled()))
        return;
    QString dng_path;
    if(mode==5){
        const auto& source=sources_[std::size_t(current_)];
        const auto suggestion=source.linked_path.isEmpty()?source.name+".dng":QFileInfo(source.linked_path).dir().filePath(QFileInfo(source.linked_path).completeBaseName()+"-copy.dng");
        dng_path=QFileDialog::getSaveFileName(this,tr("保存 DNG 副本"),suggestion,tr("数字负片 (*.dng)"));
        if(dng_path.isEmpty())return;
        setProperty("rawDngPath",dng_path);
    }
    commit_mode_=mode;
    struct Input {
        editing::Layer layer;
        std::function<editing::RawResult()> read;
        editing::RawOptions options;
        QString key;
    };
    std::vector<Input> inputs;
    try {
        for (std::size_t i = 0; i < sources_.size(); ++i) {
            if(mode==2||mode==5)break;
            if (!included(i))
                continue;
            auto read = cache_.read_task(cache_keys_[i]);
            editing::RawOptions options;
            const auto& recipe = sources_[i].raw_recipe;
            options.denoise = recipe.value("denoise").toBool();
            options.directml = recipe.value("directml").toBool(true);
            options.model_path = recipe.value("modelPath").toString(bundled_model());
            options.custom_model = recipe.value("customModel").toBool();
            options.high_quality = editing::raw_high_quality(recipe);
            inputs.push_back({sources_[i],std::move(read),options,cache_keys_[i]});
        }
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
        update_ready();
        return;
    }
    commit_job_ = std::make_unique<CommitJob>();
    auto* job = commit_job_.get();
    const auto originals=sources_;
    const int dng_index=current_;
    job->result = std::async(std::launch::async, [job, mode, originals, dng_path, dng_index, inputs = std::move(inputs)]() mutable {
        std::vector<editing::Layer> results;
        if(mode==2){editing::save_raw_sidecars(originals,&job->cancelled);return results;}
        if(mode==5){editing::save_raw_dng(originals[std::size_t(dng_index)],dng_path,&job->cancelled);return results;}
        for (auto& input : inputs) {
            if (job->cancelled.load())
                break;
            auto& layer=input.layer;
            if(editing::raw_cache_key(layer.raw_source,input.options)!=input.key)
                throw editing::EditError(QStringLiteral("RAW模型或输入已更改，请更新预览"));
            const auto source=input.read();
            layer.pixels = editing::apply_raw_adjustments(
                editing::blend_raw_denoise(source.image, source.reference,
                                           layer.raw_recipe.value("denoiseAmount").toDouble(100),
                                           &job->cancelled),
                layer.raw_recipe, true, &job->cancelled);
            results.push_back(std::move(layer));
        }
        if(mode==0 || mode==4){
            std::vector<editing::Layer> sidecars;
            for(const auto& layer:originals)if(layer.pixels.isNull() && !layer.linked_path.isEmpty())sidecars.push_back(layer);
            if(!sidecars.empty())editing::save_raw_sidecars(sidecars,&job->cancelled);
        }
        if(mode==4)for(auto& layer:results){layer.kind=editing::LayerKind::pixels;layer.raw_source.clear();layer.raw_recipe={};layer.linked_path.clear();layer.linked_hash.clear();}
        return results;
    });
    findChild<QWidget*>("rawParamPanel")->setEnabled(false);
    view_->setEnabled(false);
    tone_->setEnabled(false);
    files_->setEnabled(false);
    compute_->setEnabled(false);
    message_->setText(mode==2?tr("正在保存RAW设置…"):mode==5?tr("正在保存 RAW 数据和显影设置到 DNG…可以取消"):tr("正在生成全尺寸图像…可以取消"));
    commit_timer_->start();
    update_ready();
}
void RawDialog::finish_commit() {
    if (!commit_job_ ||
        commit_job_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    commit_timer_->stop();
    bool accepted = false;
    std::vector<editing::Layer> exported;
    try {
        auto result = commit_job_->result.get();
        if (!commit_job_->cancelled.load() && !closing_) {
            if(commit_mode_==0 || commit_mode_==3 || commit_mode_==4)sources_ = std::move(result);
            else if(commit_mode_==1)exported=std::move(result);
            accepted = true;
        }
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
    }
    commit_job_.reset();
    if (accepted && commit_mode_!=1 && commit_mode_!=5) {
        closing_ = true;
        if (preview_job_)
            preview_job_->cancelled.store(true);
        if(commit_mode_==2){setProperty("settingsSaved",true);QDialog::done(2);}
        else QDialog::accept();
        return;
    }
    findChild<QWidget*>("rawParamPanel")->setEnabled(true);
    view_->setEnabled(true);
    tone_->setEnabled(true);
    files_->setEnabled(true);
    compute_->setEnabled(denoise_->isChecked());
    update_ready();
    if(accepted && commit_mode_==5){setProperty("rawDngSaved",true);message_->setText(tr("已保存 DNG 副本：%1；原片未修改，可继续显影。").arg(property("rawDngPath").toString()));}
    if(accepted && commit_mode_==1){
        std::vector<editing::Snapshot> snapshots;
        for(auto& layer:exported){editing::State state;state.name=layer.name;state.size=layer.pixels.size();state.metadata=layer.source_metadata;
            editing::Layer flat;flat.name=layer.name;flat.linked_path=layer.linked_path;flat.pixels=std::move(layer.pixels);
            state.layers.push_back(std::move(flat));snapshots.push_back({std::move(state),1});}
        // Parent to the main window so queued exports survive closing RAW.
        auto* output=new ExportDialog(parentWidget());
        output->setObjectName("rawExportDialog");output->set_sources({});output->set_snapshots(std::move(snapshots));
        output->open();output->raise();output->activateWindow();
        message_->setText(tr("全尺寸快照已交给保存图像窗口；后续编辑不改变此次输出"));
    }
    if (closing_)
        QDialog::reject();
}
void RawDialog::reject() {
    if (crop_dragging_) {
        crop_dragging_ = false;
        if(crop_handle_==9){if(preview_item_)preview_item_->setTransform(crop_preview_transform_);const QSignalBlocker block(crop_angle_);crop_angle_->setValue(crop_rotation_start_);}
        display();
        return;
    }
    if (commit_job_) {
        closing_ = true;
        commit_job_->cancelled.store(true);
        message_->setText(tr("正在取消全尺寸处理…"));
        return;
    }
    if (!local_stroke_.isEmpty()) {
        local_stroke_ = {};
        display();
        return;
    }
    batch_ = false;
    closing_ = true;
    if (preview_job_)
        preview_job_->cancelled.store(true);
    if (job_) {
        closing_ = true;
        job_->cancelled.store(true);
        message_->setText(tr("正在取消计算并丢弃临时修改…"));
        return;
    }
    QDialog::reject();
}
} // namespace huelith::desktop
