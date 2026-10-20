#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/content_fill.h"
#include "main_window.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <future>

namespace huelith::desktop {
namespace {
class SamplingView final : public QWidget {
  public:
    QImage image, selected, allowed;
    bool subtract{true}, overlay{true};
    double radius{20};
    std::function<void()> edited;
    std::vector<QImage> undo;
    explicit SamplingView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(260, 260);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }
    QRectF image_rect() const {
        QSizeF size = image.size();
        size.scale(QSizeF(width() - 10, height() - 10), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#202428"));
        if (image.isNull())
            return;
        const auto area = image_rect();
        setProperty("imageRect", area);
        p.drawImage(area, image);
        if (overlay && !allowed.isNull()) {
            QImage cover(image.size(), QImage::Format_ARGB32_Premultiplied);
            cover.fill(Qt::transparent);
            for (int y = 0; y < cover.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(cover.scanLine(y));
                for (int x = 0; x < cover.width(); ++x) {
                    if (selected.constScanLine(y)[x])
                        row[x] = qRgba(110, 20, 20, 110);
                    else if (allowed.constScanLine(y)[x])
                        row[x] = qRgba(15, 85, 30, 95);
                }
            }
            p.drawImage(area, cover);
        }
        if (!allowed.isNull()) {
            p.setPen(QPen(Qt::white, 1));
            p.setBrush(Qt::NoBrush);
            const double r = radius * area.width() / image.width();
            p.drawEllipse(cursor_, r, r);
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (allowed.isNull() || event->button() != Qt::LeftButton ||
            !image_rect().contains(event->position()))
            return;
        undo.push_back(allowed);
        if (undo.size() > 30)
            undo.erase(undo.begin());
        drawing_ = true;
        paint(event->position(), event->modifiers());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        cursor_ = event->position();
        if (drawing_)
            paint(event->position(), event->modifiers());
        update();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!drawing_)
            return;
        paint(event->position(), event->modifiers());
        drawing_ = false;
        if (edited)
            edited();
    }

  private:
    bool drawing_{};
    QPointF cursor_;
    void paint(QPointF point, Qt::KeyboardModifiers modifiers) {
        cursor_ = point;
        const auto area = image_rect();
        const QPointF position((point.x() - area.x()) / area.width() * image.width(),
                               (point.y() - area.y()) / area.height() * image.height());
        const int left = std::max(0, int(std::floor(position.x() - radius))),
                  right = std::min(image.width() - 1, int(std::ceil(position.x() + radius))),
                  top = std::max(0, int(std::floor(position.y() - radius))),
                  bottom = std::min(image.height() - 1, int(std::ceil(position.y() + radius)));
        const bool erase = subtract != modifiers.testFlag(Qt::AltModifier);
        for (int y = top; y <= bottom; ++y) {
            auto* row = allowed.scanLine(y);
            for (int x = left; x <= right; ++x)
                if (std::hypot(x - position.x(), y - position.y()) <= radius &&
                    selected.constScanLine(y)[x] == 0)
                    row[x] = erase ? 0 : 255;
        }
        update();
    }
};
class ContentFillDialog final : public QDialog {
  public:
    ContentFillDialog(QImage source, QImage visible, QImage selected, bool writable,
                      bool duplicable, QWidget* parent)
        : QDialog(parent), source_(source), current_source_(std::move(source)),
          visible_source_(std::move(visible)), selection_(std::move(selected)) {
        setObjectName("contentFillDialog");
        setWindowTitle(tr("内容识别填充"));
        resize(1180, 720);
        auto* root = new QVBoxLayout(this);
        auto* body = new QHBoxLayout;
        auto* left = new QVBoxLayout;
        left->addWidget(new QLabel(tr("取样区域：绿色可取样，红色为填充范围")));
        sampling_ = new SamplingView(this);
        sampling_->setObjectName("fillSamplingView");
        sampling_->image = editing::display_image(source_);
        if (std::max(source_.width(), source_.height()) > 900)
            sampling_->image =
                sampling_->image.scaled(900, 900, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        sampling_->selected = selection_.scaled(sampling_->image.size(), Qt::IgnoreAspectRatio,
                                                Qt::FastTransformation);
        sampling_->allowed = QImage(sampling_->image.size(), QImage::Format_Grayscale8);
        sampling_->allowed.fill(0);
        left->addWidget(sampling_, 1);
        body->addLayout(left, 1);
        auto* right = new QVBoxLayout;
        right->addWidget(new QLabel(tr("预览")));
        preview_ = new SamplingView(this);
        preview_->setObjectName("fillPreview");
        preview_->image = sampling_->image;
        right->addWidget(preview_, 1);
        body->addLayout(right, 1);
        auto* settings = new QFormLayout;
        settings->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        mode_ = new QComboBox;
        mode_->setObjectName("fillSamplingMode");
        mode_->addItems({tr("自动"), tr("矩形周围"), tr("自定义")});
        settings->addRow(tr("取样区域选项"), mode_);
        all_ = new QCheckBox(tr("使用所有可用区域"));
        all_->setObjectName("fillAllSampling");
        settings->addRow(all_);
        auto* all_layers = new QCheckBox(tr("对所有图层取样"));
        all_layers->setObjectName("fillSampleAllLayers");
        settings->addRow(all_layers);
        auto* subtract = new QCheckBox(tr("减去取样区域（Alt 临时切换）"));
        subtract->setObjectName("fillSubtract");
        subtract->setChecked(true);
        settings->addRow(subtract);
        auto* size = new QSpinBox;
        size->setObjectName("fillBrushSize");
        size->setRange(2, 500);
        size->setValue(40);
        settings->addRow(tr("取样画笔大小"), size);
        auto* overlay = new QCheckBox(tr("显示取样区域"));
        overlay->setChecked(true);
        overlay->setObjectName("fillShowSampling");
        settings->addRow(overlay);
        adaptation_ = new QComboBox;
        adaptation_->setObjectName("fillColorAdaptation");
        adaptation_->addItems({tr("无"), tr("默认"), tr("高"), tr("很高")});
        adaptation_->setCurrentIndex(1);
        settings->addRow(tr("颜色适应"), adaptation_);
        rotation_ = new QComboBox;
        rotation_->setObjectName("fillRotationAdaptation");
        rotation_->addItems({tr("无"), tr("低"), tr("中"), tr("高"), tr("完全")});
        settings->addRow(tr("旋转适应"), rotation_);
        scale_ = new QCheckBox(tr("缩放"));
        scale_->setObjectName("fillScale");
        settings->addRow(scale_);
        mirror_ = new QCheckBox(tr("镜像"));
        mirror_->setObjectName("fillMirror");
        settings->addRow(mirror_);
        radius_ = new QSpinBox;
        radius_->setObjectName("fillPatchRadius");
        radius_->setRange(1, 12);
        radius_->setValue(4);
        settings->addRow(tr("纹理块半径"), radius_);
        output_ = new QComboBox;
        output_->setObjectName("fillOutput");
        output_->addItems({tr("新建图层"), tr("当前图层"), tr("复制图层")});
        auto* output_model = qobject_cast<QStandardItemModel*>(output_->model());
        output_model->item(1)->setEnabled(writable);
        output_model->item(2)->setEnabled(duplicable);
        settings->addRow(tr("输出到"), output_);
        auto* reset = new QPushButton(tr("重置取样区域"));
        reset->setObjectName("fillResetSampling");
        settings->addRow(reset);
        body->addLayout(settings);
        root->addLayout(body, 1);
        status_ = new QLabel;
        status_->setObjectName("fillStatus");
        root->addWidget(status_);
        buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        root->addWidget(buttons_);
        timer_ = new QTimer(this);
        timer_->setInterval(30);
        connect(timer_, &QTimer::timeout, this, [this] { poll(); });
        timer_->start();
        sampling_->edited = [this] { schedule(); };
        connect(subtract, &QCheckBox::toggled, this,
                [this](bool value) { sampling_->subtract = value; });
        connect(size, &QSpinBox::valueChanged, this, [this](int value) {
            sampling_->radius = value / 2.;
            sampling_->update();
        });
        connect(overlay, &QCheckBox::toggled, this, [this](bool value) {
            sampling_->overlay = value;
            sampling_->update();
        });
        connect(mode_, &QComboBox::currentIndexChanged, this, [this] { reset_sampling(); });
        connect(all_, &QCheckBox::toggled, this, [this] { reset_sampling(); });
        connect(reset, &QPushButton::clicked, this, [this] { reset_sampling(); });
        connect(adaptation_, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
        connect(rotation_, &QComboBox::currentIndexChanged, this, [this] { schedule(); });
        connect(scale_, &QCheckBox::toggled, this, [this] { schedule(); });
        connect(mirror_, &QCheckBox::toggled, this, [this] { schedule(); });
        connect(all_layers, &QCheckBox::toggled, this, [this](bool enabled) {
            source_ = enabled ? visible_source_ : current_source_;
            sampling_->image = editing::display_image(source_.scaled(
                sampling_->image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            sampling_->update();
            schedule();
        });
        connect(radius_, &QSpinBox::valueChanged, this, [this] { schedule(); });
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
            commit_requested_ = true;
            if (ready_ && source_.size() == sampling_->image.size()) {
                result_ = preview_result_;
                QDialog::accept();
            } else {
                schedule();
                commit_requested_ = true;
            }
        });
        auto* undo = new QShortcut(QKeySequence::Undo, this);
        connect(undo, &QShortcut::activated, this, [this] {
            if (sampling_->undo.empty())
                return;
            sampling_->allowed = sampling_->undo.back();
            sampling_->undo.pop_back();
            sampling_->update();
            schedule();
        });
        reset_sampling();
    }
    ~ContentFillDialog() override {
        cancel_.store(true);
        if (job_.valid())
            job_.wait();
    }
    QImage result() const {
        return result_;
    }
    int output() const {
        return output_->currentIndex();
    }
    void reject() override {
        cancel_.store(true);
        QDialog::reject();
    }

  private:
    QImage source_, current_source_, visible_source_, selection_, result_, preview_result_;
    SamplingView* sampling_{};
    SamplingView* preview_{};
    QComboBox* mode_{};
    QComboBox* adaptation_{};
    QComboBox* rotation_{};
    QCheckBox* scale_{};
    QCheckBox* mirror_{};
    QComboBox* output_{};
    QSpinBox* radius_{};
    QCheckBox* all_{};
    QLabel* status_{};
    QDialogButtonBox* buttons_{};
    QTimer* timer_{};
    std::future<QImage> job_;
    std::atomic_bool cancel_{};
    std::atomic<double> progress_{};
    int revision_{}, running_revision_{};
    bool pending_{}, ready_{}, commit_requested_{}, running_full_{};
    void reset_sampling() {
        sampling_->undo.push_back(sampling_->allowed);
        if (sampling_->undo.size() > 30)
            sampling_->undo.erase(sampling_->undo.begin());
        const auto& selected = sampling_->selected;
        QRect bounds;
        for (int y = 0; y < selected.height(); ++y)
            for (int x = 0; x < selected.width(); ++x)
                if (selected.constScanLine(y)[x])
                    bounds = bounds.united(QRect(x, y, 1, 1));
        const int extent = std::max(bounds.width(), bounds.height());
        const int expansion = mode_->currentIndex() == 0 ? extent : extent / 2;
        bounds = bounds.adjusted(-expansion, -expansion, expansion, expansion);
        for (int y = 0; y < selected.height(); ++y) {
            auto* row = sampling_->allowed.scanLine(y);
            for (int x = 0; x < selected.width(); ++x)
                row[x] = selected.constScanLine(y)[x] == 0 &&
                                 (all_->isChecked() ||
                                  mode_->currentIndex() != 2 && bounds.contains(x, y))
                             ? 255
                             : 0;
        }
        sampling_->update();
        schedule();
    }
    void schedule() {
        setProperty("fillError", false);
        setProperty("samplingHash",
                    QString::fromLatin1(
                        QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(
                                                                    sampling_->allowed.constBits()),
                                                                sampling_->allowed.sizeInBytes()),
                                                 QCryptographicHash::Sha256)
                            .toHex()));
        ++revision_;
        pending_ = true;
        ready_ = false;
        commit_requested_ = false;
        cancel_.store(true);
        setProperty("fillReady", false);
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
    void start() {
        pending_ = false;
        cancel_.store(false);
        progress_.store(0);
        running_revision_ = revision_;
        running_full_ = commit_requested_;
        const bool full = running_full_;
        const auto size = full ? source_.size() : sampling_->image.size();
        const QImage input = source_.size() == size ? source_
                                                    : source_.scaled(size, Qt::IgnoreAspectRatio,
                                                                     Qt::SmoothTransformation);
        const QImage selected =
            selection_.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        const QImage allowed =
            sampling_->allowed.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        editing::ContentFillOptions options;
        options.patch_radius = radius_->value();
        constexpr double adaptations[]{0, .5, .8, 1};
        options.color_adaptation = adaptations[adaptation_->currentIndex()];
        options.rotation_adaptation = rotation_->currentIndex();
        options.scale = scale_->isChecked();
        options.mirror = mirror_->isChecked();
        status_->setText(full ? tr("正在生成全分辨率结果…") : tr("正在计算预览…"));
        job_ = std::async(std::launch::async, [this, input, selected, allowed, options] {
            return editing::content_fill(input, selected, allowed, options, &cancel_,
                                         [this](double value) { progress_.store(value); });
        });
    }
    void poll() {
        if (job_.valid()) {
            if (job_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                status_->setText((running_full_ ? tr("全分辨率处理 %1%") : tr("预览处理 %1%"))
                                     .arg(qRound(progress_.load() * 100)));
                return;
            }
            try {
                auto result = job_.get();
                if (running_revision_ == revision_) {
                    if (running_full_) {
                        result_ = std::move(result);
                        QDialog::accept();
                        return;
                    }
                    preview_result_ = std::move(result);
                    auto displayed = preview_result_;
                    const auto selected = selection_.scaled(displayed.size(), Qt::IgnoreAspectRatio,
                                                            Qt::FastTransformation);
                    const auto original = source_.scaled(displayed.size(), Qt::IgnoreAspectRatio,
                                                         Qt::SmoothTransformation);
                    for (int y = 0; y < displayed.height(); ++y) {
                        auto* row = reinterpret_cast<float*>(displayed.scanLine(y));
                        const auto* src = reinterpret_cast<const float*>(original.constScanLine(y));
                        for (int x = 0; x < displayed.width(); ++x) {
                            const float amount = selected.constScanLine(y)[x] / 255.F;
                            for (int c = 0; c < 4; ++c)
                                row[x * 4 + c] =
                                    src[x * 4 + c] * (1 - amount) + row[x * 4 + c] * amount;
                        }
                    }
                    preview_->image = editing::display_image(displayed);
                    preview_->update();
                    ready_ = true;
                    setProperty("fillReady", true);
                    buttons_->button(QDialogButtonBox::Ok)->setEnabled(true);
                    status_->setText(tr("预览就绪；可在左侧增减取样区域"));
                }
            } catch (const std::exception& error) {
                if (running_revision_ == revision_) {
                    setProperty("fillError", true);
                    status_->setText(QString::fromUtf8(error.what()));
                    commit_requested_ = false;
                }
            }
        }
        if (pending_ && !job_.valid())
            start();
    }
};
} // namespace
void EditorController::show_content_fill() {
    auto* c = canvas();
    if (!c)
        return;
    guarded([&] {
        const auto source = c->project().state();
        if (source.selection.isNull())
            throw editing::EditError(tr("请先选择需要移除或填充的区域"));
        const auto* active = editing::find_layer(source.layers, c->active_layer);
        if (!active)
            throw editing::EditError(tr("请选择图层"));
        const auto sampled =
            editing::sample_layers(source, c->active_layer, editing::SampleLayers::visible);
        const bool raster = active->kind == editing::LayerKind::pixels ||
                            active->kind == editing::LayerKind::repair;
        const auto current =
            editing::sample_layers(source, c->active_layer, editing::SampleLayers::current);
        ContentFillDialog dialog(current, sampled, source.selection,
                                 raster && !editing::layer_locked(source.layers, active->id),
                                 raster, window_);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QImage result = dialog.result();
        const int output = dialog.output();
        c->edit(tr("内容识别填充"), [&](editing::State& state) {
            QUuid target = c->active_layer;
            if (output == 0) {
                editing::Layer layer;
                layer.name = tr("内容识别填充");
                layer.kind = editing::LayerKind::repair;
                target = layer.id;
                state.layers.push_back(std::move(layer));
            } else if (output == 2) {
                auto layer = *editing::find_layer(state.layers, target);
                layer.id = QUuid::createUuid();
                layer.name += tr(" 副本");
                layer.locked = false;
                layer.background = false;
                target = layer.id;
                state.layers.push_back(std::move(layer));
            }
            editing::paint_replacement(state, target, result, source.selection);
            c->active_layer = target;
        });
        refresh();
    });
}
} // namespace huelith::desktop
