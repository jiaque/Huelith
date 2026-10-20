#include "selection_refine_dialog.h"
#include "huelith/editing/project.h"
#include "raw_preview_view.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <future>

namespace huelith::desktop {
namespace {
struct Stroke {
    std::vector<QPointF> points;
    double radius{}; // Fraction of source width.
    int tool{};
    bool subtract{};
    bool operator==(const Stroke&) const = default;
};
void paint_stroke(QImage& mask, const Stroke& stroke) {
    if (stroke.points.empty())
        return;
    const auto point = [&](QPointF p) {
        return QPointF(p.x() * mask.width(), p.y() * mask.height());
    };
    if (stroke.tool == 2) {
        QPainterPath path;
        path.moveTo(point(stroke.points.front()));
        for (const auto p : stroke.points)
            path.lineTo(point(p));
        path.closeSubpath();
        const auto range = editing::path_range(mask.size(), path);
        for (int y = 0; y < mask.height(); ++y) {
            auto* dst = mask.scanLine(y);
            const auto* src = range.constScanLine(y);
            for (int x = 0; x < mask.width(); ++x)
                dst[x] = stroke.subtract ? std::min(dst[x], uchar(255 - src[x]))
                                         : std::max(dst[x], src[x]);
        }
        return;
    }
    const double radius = std::max(.5, stroke.radius * mask.width());
    const auto dab = [&](QPointF p) {
        const auto area =
            QRectF(p - QPointF(radius + 1, radius + 1), QSizeF(2 * radius + 2, 2 * radius + 2))
                .toAlignedRect()
                .intersected(mask.rect());
        for (int y = area.top(); y <= area.bottom(); ++y) {
            auto* row = mask.scanLine(y);
            for (int x = area.left(); x <= area.right(); ++x) {
                const double distance = std::hypot(x + .5 - p.x(), y + .5 - p.y());
                const auto coverage =
                    uchar(qRound(std::clamp(radius + .5 - distance, 0., 1.) * 255));
                row[x] = stroke.subtract ? std::min(row[x], uchar(255 - coverage))
                                         : std::max(row[x], coverage);
            }
        }
    };
    auto last = point(stroke.points.front());
    dab(last);
    for (const auto normalized : stroke.points) {
        const auto p = point(normalized);
        const int steps =
            std::max(1, int(std::ceil(QLineF(last, p).length() / std::max(.5, radius * .3))));
        for (int i = 1; i <= steps; ++i)
            dab(last + (p - last) * (double(i) / steps));
        last = p;
    }
}
struct EditState {
    QJsonObject settings;
    std::vector<Stroke> strokes;
    bool cleared{};
    bool operator==(const EditState&) const = default;
};
editing::SelectionRefineSettings parameters(const QJsonObject& object) {
    editing::SelectionRefineSettings s;
    s.radius = object.value("radius").toDouble();
    s.smart_radius = object.value("smart").toBool();
    s.smooth = object.value("smooth").toDouble();
    s.feather = object.value("feather").toDouble();
    s.contrast = object.value("contrast").toDouble() / 100.;
    s.shift = object.value("shift").toDouble() / 100.;
    s.invert = object.value("invert").toBool();
    s.decontaminate =
        object.value("decontaminate").toBool() ? object.value("amount").toDouble(100) / 100. : 0.;
    return s;
}
} // namespace
struct SelectionRefineDialog::Impl {
    SelectionRefineDialog* owner;
    QImage source, original, background, preview_source, preview_original, preview_band,
        preview_background;
    struct WorkerResult {
        editing::RefinedSelection refined;
        QImage source, original, band, background;
    };
    editing::RefinedSelection preview, result;
    RawPreviewView* view{};
    QGraphicsPixmapItem* item{};
    QGraphicsPathItem* stroke_preview{};
    QComboBox *tool{}, *mode{}, *output{};
    QDoubleSpinBox *size{}, *opacity{};
    QCheckBox *smart{}, *invert{}, *decontaminate{}, *remember{}, *show_original{}, *show_edge{},
        *quality{};
    std::array<QDoubleSpinBox*, 6> fields{};
    const std::array<const char*, 6> keys{"radius",   "smooth", "feather",
                                          "contrast", "shift",  "amount"};
    QLabel* status{};
    QDialogButtonBox* buttons{};
    QTimer *timer{}, *history_timer{};
    std::future<WorkerResult> job;
    std::atomic_bool cancelled{};
    quint64 revision{}, running_revision{};
    bool pending{}, committing{}, running_commit{}, updating{}, drawing{}, cleared{}, subtract{};
    bool bypass{};
    int ants_phase{};
    std::vector<Stroke> strokes;
    std::vector<EditState> history;
    std::size_t history_index{};
    QImage running_source, running_mask;

    explicit Impl(SelectionRefineDialog* dialog, QImage image, QImage mask, bool mask_only,
                  QImage backdrop)
        : owner(dialog), source(std::move(image)), original(std::move(mask)),
          background(std::move(backdrop)) {
        if (source.isNull() || original.size() != source.size() ||
            original.format() != QImage::Format_Grayscale8)
            throw editing::EditError(QStringLiteral("需要有效图像和选区"));
        owner->setObjectName("selectionRefineDialog");
        owner->setWindowTitle(QObject::tr("选择并遮住"));
        owner->resize(1180, 800);
        auto* root = new QVBoxLayout(owner);
        auto* body = new QHBoxLayout;
        auto* left = new QVBoxLayout;
        auto* toolbar = new QHBoxLayout;
        tool = new QComboBox(owner);
        tool->setObjectName("refineTool");
        tool->addItems({QObject::tr("调整边缘画笔 R"), QObject::tr("画笔 B"), QObject::tr("套索 L"),
                        QObject::tr("抓手 H"), QObject::tr("缩放 Z")});
        toolbar->addWidget(tool);
        size = new QDoubleSpinBox(owner);
        size->setObjectName("refineBrushSize");
        size->setRange(1, 2500);
        size->setValue(60);
        size->setSuffix(QObject::tr(" 像素"));
        toolbar->addWidget(size);
        toolbar->addStretch();
        auto* minus = new QCheckBox(QObject::tr("减去（Alt 临时切换）"), owner);
        minus->setObjectName("refineSubtract");
        toolbar->addWidget(minus);
        QObject::connect(minus, &QCheckBox::toggled, owner, [this](bool v) { subtract = v; });
        left->addLayout(toolbar);
        auto* scene = new QGraphicsScene(owner);
        item = scene->addPixmap({});
        stroke_preview = scene->addPath({});
        stroke_preview->setZValue(1);
        stroke_preview->hide();
        view = new RawPreviewView(scene, owner);
        view->setObjectName("refinePreview");
        view->setMinimumSize(460, 350);
        view->viewport()->installEventFilter(owner);
        view->installEventFilter(owner);
        view->fit_requested = [this] { view->fitInView(view->sceneRect(), Qt::KeepAspectRatio); };
        left->addWidget(view, 1);
        body->addLayout(left, 1);
        auto* panel = new QWidget(owner);
        auto* form = new QFormLayout(panel);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        mode = new QComboBox(panel);
        mode->setObjectName("refineViewMode");
        mode->addItems({QObject::tr("洋葱皮 O"), QObject::tr("蚂蚁线 M"), QObject::tr("叠加 V"),
                        QObject::tr("黑底 A"), QObject::tr("白底 T"), QObject::tr("黑白 K"),
                        QObject::tr("图层上 Y")});
        mode->setCurrentIndex(2);
        form->addRow(QObject::tr("视图"), mode);
        opacity = new QDoubleSpinBox(panel);
        opacity->setObjectName("refineOpacity");
        opacity->setRange(0, 100);
        opacity->setValue(50);
        opacity->setSuffix(" %");
        form->addRow(QObject::tr("不透明度"), opacity);
        show_original = new QCheckBox(QObject::tr("显示原稿 P"));
        show_original->setObjectName("refineShowOriginal");
        form->addRow(show_original);
        show_edge = new QCheckBox(QObject::tr("显示边缘 J"));
        show_edge->setObjectName("refineShowEdge");
        form->addRow(show_edge);
        quality = new QCheckBox(QObject::tr("高品质预览"));
        quality->setObjectName("refineHighQuality");
        form->addRow(quality);
        const std::array<QString, 6> names{QObject::tr("半径"),     QObject::tr("平滑"),
                                           QObject::tr("羽化"),     QObject::tr("对比度"),
                                           QObject::tr("移动边缘"), QObject::tr("净化数量")};
        for (int i = 0; i < 6; ++i) {
            if (i == 0)
                form->addRow(new QLabel(QObject::tr("边缘检测")));
            if (i == 1)
                form->addRow(new QLabel(QObject::tr("全局调整")));
            if (i == 5) {
                form->addRow(new QLabel(QObject::tr("输出设置")));
                decontaminate = new QCheckBox(QObject::tr("净化颜色"));
                decontaminate->setObjectName("refineDecontaminate");
                decontaminate->setEnabled(!mask_only);
                form->addRow(decontaminate);
            }
            auto* line = new QHBoxLayout;
            auto* slider = new QSlider(Qt::Horizontal, panel);
            slider->setObjectName(QString("refineSlider_%1").arg(keys[i]));
            auto* value = new QDoubleSpinBox(panel);
            fields[i] = value;
            value->setObjectName(QString("refine_%1").arg(keys[i]));
            value->setDecimals(i == 0 || i == 2 ? 1 : 0);
            value->setRange(i == 4 ? -100 : 0, i == 0 ? 250 : i == 2 ? 1000 : 100);
            value->setValue(i == 5 ? 100 : 0);
            value->setFixedWidth(83);
            slider->setRange(int(value->minimum() * 10), int(value->maximum() * 10));
            slider->setValue(qRound(value->value() * 10));
            line->addWidget(slider, 1);
            line->addWidget(value);
            form->addRow(names[i], line);
            QObject::connect(slider, &QSlider::valueChanged, owner,
                             [value](int v) { value->setValue(v / 10.); });
            QObject::connect(value, &QDoubleSpinBox::valueChanged, owner, [this, slider](double v) {
                slider->setValue(qRound(v * 10));
                changed();
            });
            if (i == 0) {
                smart = new QCheckBox(QObject::tr("智能半径"));
                smart->setObjectName("refineSmartRadius");
                form->addRow(smart);
            }
        }
        output = new QComboBox(panel);
        output->setObjectName("refineOutput");
        output->addItems({QObject::tr("选区"), QObject::tr("图层蒙版"), QObject::tr("新建图层"),
                          QObject::tr("新建带有图层蒙版的图层"), QObject::tr("新建文档"),
                          QObject::tr("新建带有图层蒙版的文档")});
        output->setEnabled(!mask_only);
        form->addRow(QObject::tr("输出到"), output);
        remember = new QCheckBox(QObject::tr("记住设置"));
        remember->setObjectName("refineRemember");
        form->addRow(remember);
        invert = new QCheckBox(QObject::tr("反转"));
        invert->setObjectName("refineInvert");
        form->addRow(invert);
        auto* clear = new QPushButton(QObject::tr("清除选区"));
        clear->setObjectName("refineClear");
        form->addRow(clear);
        auto* reset = new QPushButton(QObject::tr("重置工作区"));
        reset->setObjectName("refineReset");
        form->addRow(reset);
        auto* scroll = new QScrollArea(owner);
        scroll->setWidgetResizable(true);
        scroll->setWidget(panel);
        scroll->setMinimumWidth(330);
        scroll->setMaximumWidth(400);
        body->addWidget(scroll);
        root->addLayout(body, 1);
        status = new QLabel;
        status->setObjectName("refineStatus");
        root->addWidget(status);
        buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        root->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, owner,
                         &SelectionRefineDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, owner,
                         &SelectionRefineDialog::reject);
        history_timer = new QTimer(owner);
        history_timer->setSingleShot(true);
        history_timer->setInterval(350);
        QObject::connect(history_timer, &QTimer::timeout, owner, [this] { record(); });
        for (auto* control : {smart, invert, decontaminate})
            QObject::connect(control, &QCheckBox::toggled, owner, [this] { changed(); });
        QObject::connect(decontaminate, &QCheckBox::toggled, owner, [this](bool v) {
            auto* model = qobject_cast<QStandardItemModel*>(output->model());
            model->item(0)->setEnabled(!v);
            model->item(1)->setEnabled(!v);
            if (v && output->currentIndex() < 2)
                output->setCurrentIndex(3);
        });
        QObject::connect(mode, &QComboBox::currentIndexChanged, owner, [this] { present(); });
        QObject::connect(opacity, &QDoubleSpinBox::valueChanged, owner, [this] { present(); });
        QObject::connect(show_original, &QCheckBox::toggled, owner, [this] { present(); });
        QObject::connect(show_edge, &QCheckBox::toggled, owner, [this] { present(); });
        QObject::connect(quality, &QCheckBox::toggled, owner, [this] { schedule(); });
        QObject::connect(tool, &QComboBox::currentIndexChanged, owner, [this](int i) {
            view->set_tool(i == 3 ? 1 : i == 4 ? 2 : 0);
            view->setFocus();
        });
        QObject::connect(clear, &QPushButton::clicked, owner, [this] {
            record();
            cleared = true;
            strokes.clear();
            record();
            schedule();
        });
        QObject::connect(reset, &QPushButton::clicked, owner, [this] {
            record();
            EditState initial;
            initial.settings.insert("amount", 100);
            restore(initial);
            record();
            schedule();
        });
        const auto key = [this](const QKeySequence& sequence, const std::function<void()>& fn) {
            auto* shortcut = new QShortcut(sequence, owner);
            QObject::connect(shortcut, &QShortcut::activated, owner, fn);
        };
        key(QKeySequence::Undo, [this] {
            record();
            if (history_index > 0) {
                --history_index;
                restore(history[history_index]);
                schedule();
            }
        });
        key(QKeySequence("Ctrl+Shift+Z"), [this] {
            if (history_index + 1 < history.size()) {
                ++history_index;
                restore(history[history_index]);
                schedule();
            }
        });
        key(QKeySequence("F"), [this] { mode->setCurrentIndex((mode->currentIndex() + 1) % 7); });
        for (const auto& [name, index] :
             {std::pair{"R", 0}, {"B", 1}, {"L", 2}, {"H", 3}, {"Z", 4}})
            key(QKeySequence(name), [this, index] { tool->setCurrentIndex(index); });
        for (const auto& [name, index] :
             {std::pair{"O", 0}, {"M", 1}, {"V", 2}, {"A", 3}, {"T", 4}, {"K", 5}, {"Y", 6}})
            key(QKeySequence(name), [this, index] { mode->setCurrentIndex(index); });
        key(QKeySequence("P"), [this] { show_original->toggle(); });
        key(QKeySequence("J"), [this] { show_edge->toggle(); });
        key(QKeySequence("["), [this] { size->setValue(size->value() / 1.2); });
        key(QKeySequence("]"), [this] { size->setValue(size->value() * 1.2); });
        timer = new QTimer(owner);
        timer->setInterval(25);
        QObject::connect(timer, &QTimer::timeout, owner, [this] { poll(); });
        timer->start();
        auto* ants = new QTimer(owner);
        ants->setInterval(90);
        QObject::connect(ants, &QTimer::timeout, owner, [this] {
            if (owner->isVisible() && mode->currentIndex() == 1) {
                ants_phase = (ants_phase + 1) % 8;
                present();
            }
        });
        ants->start();
        if (QSettings().value("selectionRefine/remember", false).toBool()) {
            EditState saved;
            saved.settings = QSettings().value("selectionRefine/settings").toJsonObject();
            if (mask_only)
                saved.settings.insert("decontaminate", false);
            restore(saved);
            remember->setChecked(true);
        }
        record();
        schedule();
    }
    EditState state() const {
        EditState s;
        s.strokes = strokes;
        s.cleared = cleared;
        for (int i = 0; i < 6; ++i)
            s.settings.insert(keys[i], fields[i]->value());
        s.settings.insert("smart", smart->isChecked());
        s.settings.insert("invert", invert->isChecked());
        s.settings.insert("decontaminate", decontaminate->isChecked());
        return s;
    }
    void restore(const EditState& s) {
        updating = true;
        strokes = s.strokes;
        cleared = s.cleared;
        for (int i = 0; i < 6; ++i)
            fields[i]->setValue(s.settings.value(keys[i]).toDouble(i == 5 ? 100 : 0));
        smart->setChecked(s.settings.value("smart").toBool());
        invert->setChecked(s.settings.value("invert").toBool());
        decontaminate->setChecked(s.settings.value("decontaminate").toBool());
        updating = false;
    }
    void record() {
        history_timer->stop();
        const auto s = state();
        if (!history.empty() && history[history_index] == s)
            return;
        if (!history.empty())
            history.resize(history_index + 1);
        history.push_back(s);
        if (history.size() > 40)
            history.erase(history.begin());
        history_index = history.size() - 1;
        owner->setProperty("refineHistory", int(history.size()));
    }
    void changed() {
        if (updating)
            return;
        committing = false;
        buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        schedule();
        history_timer->start();
    }
    void schedule() {
        ++revision;
        pending = true;
        cancelled = true;
        owner->setProperty("refineReady", false);
        owner->setProperty("refineError", false);
        status->setText(QObject::tr("正在更新边缘预览…"));
    }
    void start() {
        pending = false;
        cancelled = false;
        running_revision = revision;
        running_commit = committing;
        const auto edit = state();
        const bool full = running_commit;
        const int limit = quality->isChecked() ? 1800 : 1000;
        const auto image = source, mask = original, backdrop = background;
        auto* token = &cancelled;
        job = std::async(std::launch::async, [image, mask, backdrop, edit, full, limit, token] {
            const double scale =
                full ? 1. : std::min(1., double(limit) / std::max(image.width(), image.height()));
            const QSize size(std::max(1, qRound(image.width() * scale)),
                             std::max(1, qRound(image.height() * scale)));
            const auto sampled =
                scale == 1 ? image
                           : image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            auto base = scale == 1
                            ? mask
                            : mask.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            WorkerResult output;
            if (!full)
                output.original = base;
            if (edit.cleared)
                base.fill(0);
            QImage area;
            const auto settings = parameters(edit.settings);
            for (const auto& stroke : edit.strokes) {
                if (token->load())
                    throw editing::EditError(QStringLiteral("选区细化已取消"));
                if (stroke.tool == 0) {
                    if (area.isNull()) {
                        area = QImage(size, QImage::Format_Grayscale8);
                        area.fill(settings.radius > 0 ? 255 : 0);
                    }
                    paint_stroke(area, stroke);
                } else
                    paint_stroke(base, stroke);
            }
            output.refined = editing::refine_selection(sampled, base, settings, area, token, scale);
            if (!full) {
                const auto display = [](const QImage& pixels) {
                    return (pixels.format() == QImage::Format_RGBA32FPx4_Premultiplied
                                ? editing::display_image(pixels)
                                : pixels)
                        .convertToFormat(QImage::Format_RGBA8888);
                };
                output.source =
                    display(output.refined.pixels.isNull() ? sampled : output.refined.pixels);
                output.band =
                    editing::selection_edge_band(base, qRound(settings.radius * scale), token);
                if (!area.isNull())
                    for (int y = 0; y < size.height(); ++y) {
                        auto* row = output.band.scanLine(y);
                        const auto* painted = area.constScanLine(y);
                        for (int x = 0; x < size.width(); ++x)
                            row[x] =
                                settings.radius > 0 ? std::min(row[x], painted[x]) : painted[x];
                    }
                if (!backdrop.isNull())
                    output.background = display(
                        backdrop.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            }
            return output;
        });
    }
    void poll() {
        if (job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto processed = job.get();
                if (running_revision == revision) {
                    if (running_commit) {
                        result = std::move(processed.refined);
                        QSettings settings;
                        settings.setValue("selectionRefine/remember", remember->isChecked());
                        if (remember->isChecked())
                            settings.setValue("selectionRefine/settings", state().settings);
                        owner->QDialog::accept();
                        return;
                    }
                    preview = std::move(processed.refined);
                    preview_original = std::move(processed.original);
                    preview_source = std::move(processed.source);
                    preview_band = std::move(processed.band);
                    preview_background = std::move(processed.background);
                    const bool first = view->sceneRect().isEmpty();
                    view->setSceneRect(QRectF(QPointF(), preview.mask.size()));
                    present();
                    if (first)
                        view->fitInView(view->sceneRect(), Qt::KeepAspectRatio);
                    owner->setProperty("refineReady", true);
                    status->setText(QObject::tr("预览就绪。Alt 减去；Ctrl+Z 撤销；空格临时抓手。"));
                }
            } catch (const std::exception& e) {
                if (running_revision == revision) {
                    status->setText(QString::fromUtf8(e.what()));
                    owner->setProperty("refineError", true);
                    committing = false;
                    buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
                }
            }
        }
        if (pending && !job.valid() && !drawing)
            start();
    }
    void present() {
        if (preview.mask.isNull())
            return;
        auto visible = preview_source.convertToFormat(QImage::Format_RGBA8888);
        if (bypass) {
            item->setPixmap(QPixmap::fromImage(visible));
            return;
        }
        const auto& mask = show_original->isChecked() ? preview_original : preview.mask;
        const int display = mode->currentIndex();
        for (int y = 0; y < visible.height(); ++y) {
            auto* row = visible.scanLine(y);
            const auto* m = mask.constScanLine(y);
            for (int x = 0; x < visible.width(); ++x) {
                double a = m[x] / 255.;
                const double alpha = row[x * 4 + 3] / 255.;
                if (show_edge->isChecked()) {
                    const int v = preview_band.constScanLine(y)[x];
                    for (int c = 0; c < 3; ++c)
                        row[x * 4 + c] = uchar(v);
                    row[x * 4 + 3] = 255;
                    continue;
                }
                if (display == 5) {
                    for (int c = 0; c < 3; ++c)
                        row[x * 4 + c] = m[x];
                } else if (display == 2) {
                    const double overlay = (1 - a) * opacity->value() / 100.;
                    row[x * 4] = uchar(row[x * 4] * (1 - overlay) + 255 * overlay);
                    row[x * 4 + 1] = uchar(row[x * 4 + 1] * (1 - overlay));
                    row[x * 4 + 2] = uchar(row[x * 4 + 2] * (1 - overlay));
                } else if (display != 1) {
                    const int matte = display == 3   ? 0
                                      : display == 4 ? 255
                                                     : ((x / 10 + y / 10) % 2 ? 160 : 210);
                    if (display == 0)
                        a = a + (1 - a) * (1 - opacity->value() / 100.);
                    a *= alpha;
                    for (int c = 0; c < 3; ++c) {
                        double back = matte;
                        if (display == 6 && !preview_background.isNull()) {
                            const auto* background_row = preview_background.constScanLine(y);
                            const double ba = background_row[x * 4 + 3] / 255.;
                            back = background_row[x * 4 + c] * ba + matte * (1 - ba);
                        }
                        row[x * 4 + c] = uchar(row[x * 4 + c] * a + back * (1 - a));
                    }
                } else if (x > 0 && y > 0 &&
                           ((m[x] > 127) != (m[x - 1] > 127) ||
                            (m[x] > 127) != (mask.constScanLine(y - 1)[x] > 127)))
                    for (int c = 0; c < 3; ++c)
                        row[x * 4 + c] = ((x + y + ants_phase) / 4) % 2 ? 0 : 255;
                row[x * 4 + 3] = 255;
            }
        }
        item->setPixmap(QPixmap::fromImage(visible));
    }
};
SelectionRefineDialog::SelectionRefineDialog(QImage image, QImage mask, QWidget* parent,
                                             bool mask_only, QImage background)
    : QDialog(parent), impl_(std::make_unique<Impl>(this, std::move(image), std::move(mask),
                                                    mask_only, std::move(background))) {}
SelectionRefineDialog::~SelectionRefineDialog() {
    impl_->cancelled = true;
    if (impl_->job.valid())
        impl_->job.wait();
}
editing::RefinedSelection SelectionRefineDialog::result_images() const {
    return impl_->result;
}
QImage SelectionRefineDialog::preview_mask() const {
    return impl_->preview.mask;
}
int SelectionRefineDialog::output_mode() const {
    return impl_->output->currentIndex();
}
void SelectionRefineDialog::accept() {
    impl_->record();
    impl_->committing = true;
    impl_->schedule();
    impl_->buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    impl_->status->setText(tr("正在生成完整分辨率结果…"));
}
void SelectionRefineDialog::reject() {
    impl_->pending = false;
    impl_->cancelled = true;
    QDialog::reject();
}
bool SelectionRefineDialog::eventFilter(QObject* object, QEvent* event) {
    if (!impl_)
        return QDialog::eventFilter(object, event);
    auto& s = *impl_;
    if (object == s.view &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_X) {
        s.bypass = event->type() == QEvent::KeyPress;
        s.present();
        return true;
    }
    if (object == s.view && event->type() == QEvent::FocusOut) {
        if (s.drawing) {
            s.drawing = false;
            s.stroke_preview->hide();
            if (!s.strokes.empty())
                s.strokes.pop_back();
            s.schedule();
        }
        s.bypass = false;
        s.present();
    }
    if (object == s.view && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && s.drawing) {
        s.drawing = false;
        s.stroke_preview->hide();
        if (!s.strokes.empty())
            s.strokes.pop_back();
        s.schedule();
        return true;
    }
    if (object != s.view->viewport() || s.view->temporary_pan() || s.tool->currentIndex() >= 3)
        return QDialog::eventFilter(object, event);
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseMove &&
        event->type() != QEvent::MouseButtonRelease)
        return QDialog::eventFilter(object, event);
    auto* mouse = static_cast<QMouseEvent*>(event);
    const auto rect = s.view->sceneRect();
    if (rect.isEmpty())
        return false;
    const auto p = s.view->mapToScene(mouse->position().toPoint());
    const QPointF normalized(std::clamp(p.x() / rect.width(), 0., 1.),
                             std::clamp(p.y() / rect.height(), 0., 1.));
    if (event->type() == QEvent::MouseButtonPress) {
        if (mouse->button() != Qt::LeftButton || !rect.contains(p) || s.committing)
            return false;
        s.record();
        s.drawing = true;
        Stroke stroke;
        stroke.tool = s.tool->currentIndex();
        stroke.radius = s.size->value() * .5 / s.source.width();
        stroke.subtract = s.subtract != bool(mouse->modifiers() & Qt::AltModifier);
        stroke.points.push_back(normalized);
        s.strokes.push_back(std::move(stroke));
        s.view->setFocus();
        QPainterPath path;
        path.moveTo(p);
        s.stroke_preview->setPath(path);
        s.stroke_preview->setPen(QPen(
            QColor(255, 100, 60, 130),
            s.tool->currentIndex() == 2 ? 1 : s.size->value() * rect.width() / s.source.width(),
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        s.stroke_preview->show();
        return true;
    }
    if (!s.drawing)
        return false;
    if (s.strokes.back().points.size() < 16384)
        s.strokes.back().points.push_back(normalized);
    auto path = s.stroke_preview->path();
    path.lineTo(p);
    s.stroke_preview->setPath(path);
    if (event->type() == QEvent::MouseButtonRelease) {
        s.drawing = false;
        s.stroke_preview->hide();
        s.record();
        s.schedule();
    }
    return true;
}
} // namespace huelith::desktop
