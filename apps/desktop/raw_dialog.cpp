#include "raw_dialog.h"
#include "huelith/editing/raw_adjustments.h"
#include "tone_panel.h"
#include "window_header.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
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
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QJsonArray>
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
    std::future<std::array<QImage, 3>> result;
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
    refine_timer_->setInterval(250);
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
        view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
        display();
    })->setObjectName("rawZoomFit");
    button(tr("100%"), [this] {
        fit_next_ = false;
        view_->resetTransform();
        detail_preview_ = true;
        display();
    })->setObjectName("rawZoom100");
    compare_ = new QCheckBox(tr("原图 / 调整后"), this);
    toolbar->addWidget(compare_);
    connect(compare_, &QCheckBox::toggled, this, &RawDialog::display);
    toolbar->addStretch();
    button(tr("同步调色与降噪设置"), [this] { synchronize(); })->setObjectName("rawSynchronize");
    button(tr("更新全部预览"), [this] { compute_all(); })->setObjectName("rawComputeAll");
    button(tr("撤销"), [this] { restore_history(-1); })->setObjectName("rawUndo");
    button(tr("重做"), [this] { restore_history(1); })->setObjectName("rawRedo");
    auto* undo = new QShortcut(QKeySequence::Undo, this);
    connect(undo, &QShortcut::activated, this, [this] { restore_history(-1); });
    auto* redo = new QShortcut(QKeySequence("Ctrl+Shift+Z"), this);
    connect(redo, &QShortcut::activated, this, [this] { restore_history(1); });
    root->addLayout(toolbar);
    auto* body = new QHBoxLayout;
    files_ = new QListWidget(this);
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
    view_ = new QGraphicsView(scene_, this);
    view_->setObjectName("rawPreview");
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setBackgroundBrush(QColor("#1c2024"));
    view_->viewport()->installEventFilter(this);
    auto* image_column = new QVBoxLayout;
    image_column->setSpacing(0);
    image_column->addWidget(view_, 1);
    image_column->addWidget(files_);
    body->addLayout(image_column, 1);
    auto* panel = new QWidget(this);
    panel->setObjectName("rawParamPanel");
    panel->setMinimumWidth(320);
    auto* form = new QFormLayout(panel);
    form->setContentsMargins(12, 8, 12, 8);
    histogram_ = new QLabel(panel);
    histogram_->setObjectName("rawHistogram");
    histogram_->setFixedHeight(100);
    form->addRow(new QLabel(tr("直方图（显示 RGB）"), panel));
    histogram_->setAlignment(Qt::AlignCenter);
    form->addRow(histogram_);
    image_info_ = new QLabel(panel);
    image_info_->setObjectName("rawImageInfo");
    form->addRow(image_info_);
    edit_mode_ = new QComboBox(panel);
    edit_mode_->setObjectName("rawEditMode");
    edit_mode_->addItems(
        {tr("平移"), tr("局部笔刷"), tr("擦除范围"), tr("线性渐变"), tr("径向渐变"), tr("裁剪")});
    auto* mask_panel = new QWidget(panel);
    mask_panel->setObjectName("rawMaskPanel");
    auto* mask_form = new QFormLayout(mask_panel);
    mask_form->setContentsMargins(0, 0, 0, 0);
    mask_form->addRow(tr("显影工具"), edit_mode_);
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
    local_button(tr("重置裁剪"), "rawResetCrop", [this] {
        sources_[current_].raw_recipe.remove("crop");
        record_history();
        display();
    });
    mask_form->addRow(local_buttons);
    local_radius_ = new QDoubleSpinBox(panel);
    local_radius_->setObjectName("rawLocalRadius");
    local_radius_->setRange(1, 1000);
    local_radius_->setValue(100);
    local_radius_->setSuffix(tr(" 像素"));
    mask_form->addRow(tr("局部笔刷半径"), local_radius_);
    local_feather_ = new QDoubleSpinBox(panel);
    local_feather_->setObjectName("rawLocalFeather");
    local_feather_->setRange(0, 100);
    local_feather_->setValue(50);
    local_feather_->setSuffix(" %");
    mask_form->addRow(tr("局部羽化"), local_feather_);
    overlay_ = new QCheckBox(tr("显示当前局部范围（红色）"), panel);
    overlay_->setObjectName("rawLocalOverlay");
    mask_form->addRow(overlay_);
    connect(overlay_, &QCheckBox::toggled, this, &RawDialog::display);
    connect(adjustment_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_) {
            local_stroke_ = {};
            select_adjustment();
            display();
        }
    });
    connect(edit_mode_, &QComboBox::currentIndexChanged, this, [this](int mode) {
        view_->setDragMode(mode == 0 ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);
        local_stroke_ = {};
        display();
    });
    tone_ = new TonePanel({}, panel);
    tone_->use_accordion();
    exposure_ = tone_->findChild<QDoubleSpinBox*>("toneExposure");
    exposure_->setObjectName("rawExposure");
    form->addRow(tone_);
    auto* detail_form =
        qobject_cast<QFormLayout*>(tone_->findChild<QWidget*>("rawSectionBody7")->layout());
    denoise_ = new QCheckBox(tr("AI RAW 降噪"), panel);
    denoise_->setObjectName("rawDenoise");
    detail_form->addRow(denoise_);
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
            message_->setText(tr("模型已修改，请更新预览"));
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
    compute_ = new QPushButton(tr("更新预览"), panel);
    compute_->setObjectName("rawCompute");
    compute_->setAutoDefault(false);
    form->addRow(compute_);
    connect(compute_, &QPushButton::clicked, this, &RawDialog::start);
    auto* stop = new QPushButton(tr("取消计算"), panel);
    stop->setAutoDefault(false);
    form->addRow(stop);
    connect(stop, &QPushButton::clicked, this, [this] {
        batch_ = false;
        if (job_)
            job_->cancelled.store(true);
    });
    progress_ = new QProgressBar(panel);
    progress_->setObjectName("rawProgress");
    form->addRow(progress_);
    message_ = new QLabel(panel);
    message_->setObjectName("rawStatus");
    message_->setWordWrap(true);
    form->addRow(message_);
    auto* sidebar = new QVBoxLayout;
    const auto detach_row = [form](QWidget* widget) {
        const auto row = form->takeRow(widget);
        delete row.labelItem;
        delete row.fieldItem;
    };
    detach_row(histogram_);
    detach_row(image_info_);
    const auto title = form->takeRow(0);
    if (title.fieldItem) {
        delete title.fieldItem->widget();
        delete title.fieldItem;
    }
    delete title.labelItem;
    sidebar->addWidget(histogram_);
    auto* capture_info = new QLabel(this);
    capture_info->setObjectName("rawCaptureInfo");
    capture_info->setAlignment(Qt::AlignCenter);
    sidebar->addWidget(capture_info);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFixedWidth(360);
    scroll->setWidget(panel);
    sidebar->addWidget(scroll, 1);
    body->addLayout(sidebar);
    auto* rail = new QVBoxLayout;
    rail->setContentsMargins(3, 6, 3, 6);
    auto* group = new QButtonGroup(this);
    const QStringList labels{tr("编辑"), tr("裁剪"), tr("蒙版"), tr("手形")};
    const QStringList paths{
        "<path d='M3 5h18M3 12h18M3 19h18'/><circle cx='8' cy='5' r='2'/><circle cx='16' cy='12' "
        "r='2'/><circle cx='9' cy='19' r='2'/>",
        "<path d='M7 2v15h15M2 7h15v15M4 20L20 4'/>",
        "<circle cx='12' cy='12' r='8' stroke-dasharray='2 2'/><path d='M12 7v10M7 12h10'/>",
        "<path d='M8 12V6q0-3 2-1v6-7q1-2 2 0v7-6q1-2 2 0v7-4q2-2 2 1v6q0 6-6 6-3 0-5-4l-3-4q0-3 "
        "3-1l3 2'/>",
    };
    for (int i = 0; i < labels.size(); ++i) {
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
        connect(tool, &QToolButton::clicked, this, [this, mask_panel, i] {
            mask_panel->setVisible(i == 1 || i == 2);
            tone_->setVisible(i != 1);
            if (i == 0) {
                adjustment_->setCurrentIndex(0);
                edit_mode_->setCurrentIndex(0);
            }
            if (i == 1)
                edit_mode_->setCurrentIndex(5);
            if (i == 2)
                edit_mode_->setCurrentIndex(1);
            if (i == 3)
                edit_mode_->setCurrentIndex(0);
        });
    }
    const auto shortcut = [this](const QString& key, const auto& action) {
        auto* item = new QShortcut(QKeySequence(key), this);
        connect(item, &QShortcut::activated, this, action);
    };
    shortcut("C", [this] { findChild<QToolButton*>("rawRail1")->click(); });
    shortcut("K", [this] { findChild<QToolButton*>("rawRail2")->click(); });
    shortcut("H", [this] { findChild<QToolButton*>("rawRail3")->click(); });
    shortcut("P", [this] { compare_->toggle(); });
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
    auto* footer_row = new QHBoxLayout;
    footer_row->setContentsMargins(12, 6, 6, 6);
    footer_row->addWidget(image_info_);
    footer_row->addStretch();
    footer_row->addWidget(footer);
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
            if (!dragging)
                history_timer_->start();
            display();
        }
    };
    for (auto* slider : tone_->findChildren<QSlider*>()) {
        connect(slider, &QSlider::sliderPressed, this, [this] {
            history_timer_->stop();
            record_history();
        });
        connect(slider, &QSlider::sliderReleased, this, [this] {
            history_timer_->stop();
            record_history();
        });
    }
    const auto invalidate = [this] {
        if (!loading_) {
            sources_[current_].raw_recipe.insert("denoise", denoise_->isChecked());
            sources_[current_].raw_recipe.insert("directml", backend_->currentIndex() == 0);
            sources_[current_].raw_recipe.insert("modelPath", model_->text());
            cache_keys_[current_].clear();
            record_history();
            display();
            apply_->setEnabled(false);
            message_->setText(tr("参数已修改，请更新预览"));
        }
    };
    connect(denoise_, &QCheckBox::toggled, this, invalidate);
    connect(backend_, &QComboBox::currentIndexChanged, this, invalidate);
    connect(model_, &QLineEdit::textChanged, this, invalidate);
    files_->setCurrentRow(0);
    record_history();
    QTimer::singleShot(0, this, &RawDialog::start);
}
RawDialog::~RawDialog() {
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
        adjustment_->addItem(tr("局部 %1").arg(i + 1));
    adjustment_->setCurrentIndex(std::clamp(previous, 0, int(locals.size())));
}
void RawDialog::select_adjustment() {
    loading_ = true;
    const auto& recipe = sources_[current_].raw_recipe;
    const int index = adjustment_->currentIndex() - 1;
    const auto locals = recipe.value("locals").toArray();
    const auto settings = index >= 0 && index < locals.size()
                              ? locals.at(index).toObject().value("tone").toObject()
                              : recipe.value("tone").toObject();
    tone_->set_settings(editing::tone_from_json(settings));
    loading_ = false;
}
bool RawDialog::eventFilter(QObject* object, QEvent* event) {
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
    const auto point = view_->mapToScene(mouse->position().toPoint());
    const double x = std::clamp(point.x() / source_size_.width(), 0., 1.);
    const double y = std::clamp(point.y() / source_size_.height(), 0., 1.);
    const QJsonObject sample{{"x", x}, {"y", y}};
    if (event->type() == QEvent::MouseButtonPress) {
        if (mode != 5 && adjustment_->currentIndex() <= 0) {
            message_->setText(tr("请先新建或选择一个局部调整，再在图上建立范围"));
            return true;
        }
        local_stroke_ = {{"mode", mode == 3   ? "linear"
                                  : mode == 4 ? "radial"
                                              : "brush"},
                         {"subtract", mode == 2},
                         {"radius", local_radius_->value() /
                                        std::min(source_size_.width(), source_size_.height())},
                         {"feather", local_feather_->value() / 100},
                         {"x", x},
                         {"y", y},
                         {"endX", x},
                         {"endY", y},
                         {"points", QJsonArray{sample}}};
        QPainterPath path;
        path.moveTo(point);
        stroke_guide_ =
            scene_->addPath(path, QPen(QColor("#63baff"), 2 / view_->transform().m11()));
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
        if (mode == 5) {
            const QRectF rect(
                QPointF(local_stroke_.value("x").toDouble(), local_stroke_.value("y").toDouble()),
                QPointF(x, y));
            if (std::abs(rect.width()) * source_size_.width() >= 1 &&
                std::abs(rect.height()) * source_size_.height() >= 1)
                recipe.insert("crop", local_stroke_);
        } else {
            auto locals = recipe.value("locals").toArray();
            const int index = adjustment_->currentIndex() - 1;
            auto local = locals.at(index).toObject();
            auto components = local.value("components").toArray();
            if (components.size() < 4096) {
                components.append(local_stroke_);
                local.insert("components", components);
                locals[index] = local;
                recipe.insert("locals", locals);
            }
        }
        local_stroke_ = {};
        record_history();
        display();
    }
    return true;
}
void RawDialog::select(int index) {
    if (index < 0 || index >= int(sources_.size()) || job_ || commit_job_)
        return;
    fit_next_ = fit_next_ || current_ != index;
    if (current_ != index)
        source_size_ = {};
    current_ = index;
    local_stroke_ = {};
    loading_ = true;
    const auto& recipe = sources_[current_].raw_recipe;
    auto settings = editing::tone_from_json(recipe.value("tone").toObject());
    if (!recipe.contains("tone"))
        settings.exposure = recipe.value("exposure").toDouble();
    tone_->set_settings(settings);
    refresh_local_controls();
    adjustment_->setCurrentIndex(0);
    denoise_->setChecked(recipe.value("denoise").toBool());
    backend_->setCurrentIndex(recipe.value("directml").toBool(true) ? 0 : 1);
    const bool custom = recipe.value("customModel").toBool();
    model_choice_->setCurrentIndex(custom ? 1 : 0);
    model_->setText(custom ? recipe.value("modelPath").toString() : bundled_model());
    loading_ = false;
    display();
}
void RawDialog::start() {
    if (job_ || commit_job_ || closing_ || sources_.empty())
        return;
    editing::RawOptions options;
    options.denoise = denoise_->isChecked();
    options.directml = backend_->currentIndex() == 0;
    options.model_path = model_->text();
    options.custom_model = model_choice_->currentIndex() == 1;
    const auto bytes = sources_[current_].raw_source;
    QString key;
    try {
        key = editing::raw_cache_key(bytes, options);
        if (const auto cached = cache_.get(key)) {
            cache_keys_[current_] = key;
            sources_[current_].raw_recipe.insert("metadata", cached->metadata);
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
    message_->setText(tr("正在处理源图像…"));
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
    }
    job_.reset();
    if (closing_) {
        QDialog::reject();
        return;
    }
    files_->setEnabled(true);
    denoise_->setEnabled(true);
    backend_->setEnabled(true);
    model_->setEnabled(true);
    model_choice_->setEnabled(true);
    findChild<QPushButton*>("rawBrowse")->setEnabled(model_choice_->currentIndex() == 1);
    compute_->setEnabled(true);
    display();
    if (batch_)
        QTimer::singleShot(0, this, &RawDialog::compute_all);
}
void RawDialog::display() {
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
    if (preview_job_) {
        if (preview_job_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            const auto result = preview_job_->result.get();
            if (!preview_job_->comparison && !preview_job_->full_resolution &&
                preview_job_->key == cache_keys_[current_])
                proxy_source_ = result[1];
            const int target = overlay_->isChecked() ? adjustment_->currentIndex() - 1 : -1;
            if (!closing_ && !preview_job_->cancelled.load() && preview_job_->index == current_ &&
                preview_job_->key == cache_keys_[current_] &&
                preview_job_->comparison == compare_->isChecked() &&
                preview_job_->target == target &&
                preview_job_->revision > property("previewPresented").toULongLong()) {
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
            preview_key_ = key;
            if (const auto cached = cache_.get(key))
                preview_source_ = cached->image;
        }
        if (preview_source_.isNull())
            return;
        source_size_ = preview_source_.size();
        auto source =
            !full_preview_requested_ && !proxy_source_.isNull() ? proxy_source_ : preview_source_;
        const bool comparison = compare_->isChecked();
        if (comparison && !sources_[current_].pixels.isNull())
            source = sources_[current_].pixels;
        const auto recipe = sources_[current_].raw_recipe;
        const int target = overlay_->isChecked() ? adjustment_->currentIndex() - 1 : -1;
        const int edge = full_preview_requested_ ? std::max(source.width(), source.height()) : 1280;
        preview_job_ = std::make_unique<PreviewJob>();
        preview_job_->revision = preview_revision_;
        preview_job_->key = key;
        preview_job_->comparison = comparison;
        preview_job_->index = current_;
        preview_job_->full_resolution = full_preview_requested_;
        preview_job_->target = target;
        auto* preview_job = preview_job_.get();
        preview_job_->result =
            std::async(std::launch::async, [source, recipe, comparison, target, edge, preview_job] {
                auto proxy = source;
                if (std::max(proxy.width(), proxy.height()) > edge)
                    proxy = proxy.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                const auto image = comparison ? proxy
                                              : editing::apply_raw_adjustments(
                                                    proxy, recipe, false, &preview_job->cancelled);
                auto visible = editing::display_image(image);
                const auto histogram_sample =
                    visible.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGB32);
                const auto locals = recipe.value("locals").toArray();
                if (!comparison && target >= 0 && target < locals.size()) {
                    const auto mask =
                        editing::raw_local_mask(image.size(), locals.at(target).toObject());
                    QImage overlay(image.size(), QImage::Format_ARGB32);
                    for (int y = 0; y < mask.height(); ++y) {
                        auto* row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
                        for (int x = 0; x < mask.width(); ++x)
                            row[x] = qRgba(255, 55, 55, mask.constScanLine(y)[x] / 3);
                    }
                    QPainter painter(&visible);
                    painter.drawImage(0, 0, overlay);
                }
                return std::array{visible, proxy, histogram_sample};
            });
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
    }
}
void RawDialog::present_preview(const QImage& visible, const QImage& histogram_sample) {
    setProperty("previewFullResolution", visible.size() == source_size_);
    const auto transform = view_->transform();
    const auto center = view_->mapToScene(view_->viewport()->rect().center());
    // A pending render must not invalidate a brush gesture currently being drawn.
    if (!local_stroke_.isEmpty()) {
        preview_pending_ = true;
        return;
    }
    stroke_guide_ = nullptr;
    scene_->clear();
    auto* pixmap = scene_->addPixmap(QPixmap::fromImage(visible));
    pixmap->setTransform(QTransform::fromScale(double(source_size_.width()) / visible.width(),
                                               double(source_size_.height()) / visible.height()));
    if (!compare_->isChecked() && sources_[current_].raw_recipe.contains("crop")) {
        const auto crop = editing::raw_crop_rect(source_size_, sources_[current_].raw_recipe);
        QPainterPath shade;
        shade.addRect(QRectF(QPointF(), QSizeF(source_size_)));
        shade.addRect(crop);
        scene_->addPath(shade, Qt::NoPen, QColor(0, 0, 0, 170));
        scene_->addRect(crop, QPen(Qt::white, 1));
    }
    files_->item(current_)->setIcon(
        QPixmap::fromImage(visible.scaled(112, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
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
    const double shutter = metadata.value("shutter").toDouble();
    const QString shutter_text = shutter > 0 && shutter < 1 ? tr("1/%1 秒").arg(qRound(1 / shutter))
                                                            : tr("%1 秒").arg(shutter);
    findChild<QLabel*>("rawCaptureInfo")
        ->setText(tr("ISO %1    %2 mm    f/%3    %4")
                      .arg(metadata.value("iso").toDouble(), 0, 'f', 0)
                      .arg(metadata.value("focalLength").toDouble(), 0, 'f', 0)
                      .arg(metadata.value("aperture").toDouble(), 0, 'f', 1)
                      .arg(shutter_text));
    scene_->setSceneRect(QRectF(QPointF(), QSizeF(source_size_)));
    if (fit_next_) {
        view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
        fit_next_ = false;
    } else {
        view_->setTransform(transform);
        view_->centerOn(center);
    }
    update_ready();
}
void RawDialog::update_ready() {
    bool ready = !job_ && !commit_job_;
    int count{};
    for (std::size_t i = 0; i < cache_keys_.size(); ++i) {
        if (included(i)) {
            ++count;
            ready = ready && cache_.contains(cache_keys_[i]);
        }
    }
    apply_->setEnabled(ready && count > 0);
    apply_->setText(sources_.front().pixels.isNull() ? tr("打开为源图层（%1）").arg(count)
                                                     : tr("确定"));
}
bool RawDialog::included(std::size_t index) const {
    return files_->item(int(index))->checkState() == Qt::Checked;
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
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        if (int(i) == current_ || !included(i))
            continue;
        for (const auto* name :
             {"tone", "exposure", "denoise", "directml", "modelPath", "customModel"}) {
            if (sources_[current_].raw_recipe.contains(QLatin1String(name)))
                sources_[i].raw_recipe.insert(
                    QLatin1String(name), sources_[current_].raw_recipe.value(QLatin1String(name)));
            else
                sources_[i].raw_recipe.remove(QLatin1String(name));
        }
        cache_keys_[i].clear();
    }
    message_->setText(tr("设置已同步；可更新全部预览后提交"));
    record_history();
    update_ready();
}
void RawDialog::record_history() {
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
    if (history_.size() > 100)
        history_.erase(history_.begin());
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
    adjustment_->setCurrentIndex(std::min(keep_target, adjustment_->count() - 1));
    fit_next_ = keep_fit;
    message_->setText(direction < 0 ? tr("已撤销会话修改") : tr("已重做会话修改"));
}
void RawDialog::accept() {
    if (job_ || commit_job_ || !apply_->isEnabled())
        return;
    std::vector<std::pair<editing::Layer, QImage>> inputs;
    try {
        for (std::size_t i = 0; i < sources_.size(); ++i) {
            if (!included(i))
                continue;
            const auto cached = cache_.get(cache_keys_[i]);
            if (!cached)
                throw editing::EditError(tr("RAW 缓存已失效，请更新全部预览"));
            editing::RawOptions options;
            const auto& recipe = sources_[i].raw_recipe;
            options.denoise = recipe.value("denoise").toBool();
            options.directml = recipe.value("directml").toBool(true);
            options.model_path = recipe.value("modelPath").toString(bundled_model());
            options.custom_model = recipe.value("customModel").toBool();
            if (editing::raw_cache_key(sources_[i].raw_source, options) != cache_keys_[i]) {
                cache_keys_[i].clear();
                throw editing::EditError(tr("RAW 模型或输入已更改，请重新更新预览"));
            }
            inputs.emplace_back(sources_[i], cached->image);
        }
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
        update_ready();
        return;
    }
    commit_job_ = std::make_unique<CommitJob>();
    auto* job = commit_job_.get();
    job->result = std::async(std::launch::async, [job, inputs = std::move(inputs)]() mutable {
        std::vector<editing::Layer> results;
        for (auto& [layer, source] : inputs) {
            if (job->cancelled.load())
                break;
            layer.pixels =
                editing::apply_raw_adjustments(source, layer.raw_recipe, true, &job->cancelled);
            results.push_back(std::move(layer));
        }
        return results;
    });
    findChild<QWidget*>("rawParamPanel")->setEnabled(false);
    view_->setEnabled(false);
    tone_->setEnabled(false);
    files_->setEnabled(false);
    compute_->setEnabled(false);
    message_->setText(tr("正在生成全尺寸图层…可以取消"));
    commit_timer_->start();
    update_ready();
}
void RawDialog::finish_commit() {
    if (!commit_job_ ||
        commit_job_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    commit_timer_->stop();
    bool accepted = false;
    try {
        auto result = commit_job_->result.get();
        if (!commit_job_->cancelled.load() && !closing_) {
            sources_ = std::move(result);
            accepted = true;
        }
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
    }
    commit_job_.reset();
    findChild<QWidget*>("rawParamPanel")->setEnabled(true);
    view_->setEnabled(true);
    tone_->setEnabled(true);
    files_->setEnabled(true);
    compute_->setEnabled(true);
    update_ready();
    if (accepted) {
        closing_ = true;
        if (preview_job_)
            preview_job_->cancelled.store(true);
        QDialog::accept();
    } else if (closing_)
        QDialog::reject();
}
void RawDialog::reject() {
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
