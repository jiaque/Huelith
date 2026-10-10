#include "canvas_view.h"
#include "editor_controller.h"
#include "main_window.h"
#include "tone_panel.h"
#include "window_header.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <future>
#include <tuple>

namespace huelith::desktop {
namespace {
class EffectsDialog final : public QDialog {
  public:
    EffectsDialog(editing::State state, QUuid layer_id, QWidget* parent)
        : QDialog(parent), source_(std::move(state)), layer_id_(layer_id),
          effects_(editing::find_layer(source_.layers, layer_id)->effects) {
        setObjectName("effectsDialog");
        setWindowTitle(tr("智能效果"));
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        resize(1180, 780);
        auto* root = new QVBoxLayout(this);
        auto* title = new QMenuBar;
        title->addAction(tr("智能效果"))->setEnabled(false);
        root->addWidget(new WindowHeader(title, this));
        auto* body = new QHBoxLayout;
        auto* left = new QVBoxLayout;
        list_ = new QListWidget(this);
        list_->setObjectName("effectList");
        list_->setMinimumWidth(190);
        list_->setMaximumWidth(230);
        left->addWidget(new QLabel(tr("效果按列表从上到下处理"), this));
        left->addWidget(list_, 1);
        const auto button = [this, left](const QString& text, const char* name,
                                         const std::function<void()>& action) {
            auto* control = new QPushButton(text, this);
            control->setObjectName(QLatin1String(name));
            control->setAutoDefault(false);
            left->addWidget(control);
            connect(control, &QPushButton::clicked, this, [this, action] { guarded(action); });
        };
        button(tr("添加调整"), "effectAdd", [this] {
            check_capacity();
            effects_.emplace_back();
            effects_.back().name = tr("调整 %1").arg(effects_.size());
            rebuild(int(effects_.size()) - 1);
            schedule();
        });
        button(tr("加载 3D LUT…"), "effectLut", [this] {
            check_capacity();
            const auto path =
                QFileDialog::getOpenFileName(this, tr("加载3D LUT"), {}, "Cube LUT (*.cube)");
            if (path.isEmpty())
                return;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
                throw editing::EditError(tr("无法读取LUT"));
            editing::Effect effect;
            effect.name = QFileInfo(path).completeBaseName();
            effect.lut = editing::parse_cube(QString::fromUtf8(file.readAll()));
            effects_.push_back(std::move(effect));
            rebuild(int(effects_.size()) - 1);
            schedule();
        });
        button(tr("复制"), "effectDuplicate", [this] {
            if (current() < 0)
                return;
            check_capacity();
            auto copy = effects_[current()];
            copy.id = QUuid::createUuid();
            copy.name += tr(" 副本");
            effects_.insert(effects_.begin() + current() + 1, std::move(copy));
            rebuild(current() + 1);
            schedule();
        });
        button(tr("删除"), "effectDelete", [this] {
            const int index = current();
            if (index < 0)
                return;
            effects_.erase(effects_.begin() + index);
            rebuild(std::min(index, int(effects_.size()) - 1));
            schedule();
        });
        button(tr("上移"), "effectUp", [this] { move(-1); });
        button(tr("下移"), "effectDown", [this] { move(1); });
        button(tr("保存效果预设…"), "effectSavePreset", [this] { save_preset(); });
        button(tr("加载效果预设…"), "effectLoadPreset", [this] { load_preset(); });
        body->addLayout(left);
        auto* center = new QVBoxLayout;
        image_ = new QLabel(this);
        image_->setMinimumSize(400, 350);
        image_->setAlignment(Qt::AlignCenter);
        center->addWidget(image_, 1);
        preview_ = new QCheckBox(tr("预览"), this);
        preview_->setObjectName("effectPreview");
        preview_->setChecked(true);
        center->addWidget(preview_);
        body->addLayout(center, 1);
        auto* right = new QVBoxLayout;
        amount_ = new QDoubleSpinBox(this);
        amount_->setObjectName("effectAmount");
        amount_->setRange(0, 100);
        amount_->setSuffix(" %");
        right->addWidget(new QLabel(tr("效果强度"), this));
        right->addWidget(amount_);
        auto* range = new QHBoxLayout;
        for (const auto& [label, name, action] :
             {std::tuple{tr("使用选区"), "effectSelection", 0},
              std::tuple{tr("反相范围"), "effectInvert", 1},
              std::tuple{tr("全部范围"), "effectClearMask", 2}}) {
            auto* control = new QPushButton(label, this);
            control->setObjectName(name);
            control->setAutoDefault(false);
            range->addWidget(control);
            connect(control, &QPushButton::clicked, this,
                    [this, action] { guarded([&] { set_range(action); }); });
        }
        right->addLayout(range);
        panel_ = new TonePanel({}, this);
        panel_->use_accordion();
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFixedWidth(360);
        scroll->setWidget(panel_);
        right->addWidget(scroll, 1);
        body->addLayout(right);
        root->addLayout(body, 1);
        status_ = new QLabel(this);
        status_->setObjectName("effectStatus");
        root->addWidget(status_);
        auto* footer = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        root->addWidget(footer);
        connect(footer, &QDialogButtonBox::accepted, this, [this] {
            guarded([&] {
                editing::validate_effects(effects_);
                accept();
            });
        });
        connect(footer, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(list_, &QListWidget::currentRowChanged, this, [this] { select(); });
        connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
            if (loading_)
                return;
            auto& effect = effects_[list_->row(item)];
            effect.name = item->text().left(512);
            effect.enabled = item->checkState() == Qt::Checked;
            schedule();
        });
        connect(amount_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
            if (loading_ || current() < 0)
                return;
            effects_[current()].amount = value / 100;
            schedule();
        });
        panel_->changed = [this] {
            if (loading_ || current() < 0)
                return;
            effects_[current()].tone = panel_->settings();
            schedule();
        };
        connect(preview_, &QCheckBox::toggled, this, [this] { schedule(); });
        timer_.setInterval(20);
        connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
        rebuild(effects_.empty() ? -1 : 0);
        schedule();
    }
    const std::vector<editing::Effect>& effects() const {
        return effects_;
    }
    void done(int result) override {
        // Keep the dialog responsive while its outstanding render releases captured source data.
        if (worker_.valid() || (result == QDialog::Accepted && pending_)) {
            closing_ = result;
            if (result == QDialog::Rejected)
                pending_ = false;
            setEnabled(false);
            status_->setText(tr("正在结束预览…"));
            timer_.start();
            return;
        }
        QDialog::done(result);
    }

  private:
    void check_capacity() const {
        if (effects_.size() >= 64)
            throw editing::EditError(tr("单层最多64个效果"));
    }
    int current() const {
        return list_->currentRow();
    }
    void guarded(const std::function<void()>& work) {
        try {
            work();
        } catch (const std::exception& error) {
            status_->setText(QString::fromUtf8(error.what()));
        }
    }
    void rebuild(int index) {
        loading_ = true;
        list_->clear();
        for (const auto& effect : effects_) {
            auto* item = new QListWidgetItem(effect.name, list_);
            item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
            item->setCheckState(effect.enabled ? Qt::Checked : Qt::Unchecked);
        }
        list_->setCurrentRow(index);
        loading_ = false;
        select();
    }
    void select() {
        if (loading_)
            return;
        loading_ = true;
        panel_->setEnabled(current() >= 0);
        amount_->setEnabled(current() >= 0);
        if (current() >= 0) {
            panel_->set_settings(effects_[current()].tone);
            amount_->setValue(effects_[current()].amount * 100);
        }
        loading_ = false;
    }
    void move(int direction) {
        const int index = current(), next = index + direction;
        if (index < 0 || next < 0 || next >= int(effects_.size()))
            return;
        std::swap(effects_[index], effects_[next]);
        rebuild(next);
        schedule();
    }
    void set_range(int action) {
        if (current() < 0)
            return;
        auto& mask = effects_[current()].mask;
        if (action == 2)
            mask = {};
        else if (action == 1) {
            if (mask.isNull())
                throw editing::EditError(tr("请先设置效果范围"));
            mask.invertPixels();
        } else {
            if (source_.selection.isNull())
                throw editing::EditError(tr("请先在画布中建立选区"));
            const auto& layer = *editing::find_layer(source_.layers, layer_id_);
            const auto size = layer.pixels.isNull() ? source_.size : layer.pixels.size();
            mask = QImage(size, QImage::Format_Grayscale8);
            mask.fill(0);
            const auto matrix = layer.transform * editing::parent_matrix(source_, layer_id_);
            for (int y = 0; y < size.height(); ++y)
                for (int x = 0; x < size.width(); ++x) {
                    const auto warped =
                        editing::warp_point(layer, size, QPointF(x + .5, y + .5), false);
                    if (!warped)
                        continue;
                    const auto scene = layer.kind == editing::LayerKind::adjustment ||
                                               layer.kind == editing::LayerKind::fill
                                           ? QPointF(x + .5, y + .5)
                                           : matrix.map(*warped);
                    const QPoint point(int(std::floor(scene.x())), int(std::floor(scene.y())));
                    if (source_.selection.rect().contains(point))
                        mask.scanLine(y)[x] = source_.selection.constScanLine(point.y())[point.x()];
                }
        }
        schedule();
    }
    void save_preset() {
        if (current() < 0)
            return;
        const auto path =
            QFileDialog::getSaveFileName(this, tr("保存效果预设"), {}, "Huelith Effect (*.hfx)");
        if (path.isEmpty())
            return;
        const auto& effect = effects_[current()];
        const QJsonObject json{{"version", 1},
                               {"name", effect.name},
                               {"amount", effect.amount},
                               {"tone", editing::tone_json(effect.tone)},
                               {"lut", effect.lut ? effect.lut->source : QString{}}};
        QSaveFile file(path);
        const auto bytes = QJsonDocument(json).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            throw editing::EditError(tr("预设写入失败"));
    }
    void load_preset() {
        check_capacity();
        const auto path =
            QFileDialog::getOpenFileName(this, tr("加载效果预设"), {}, "Huelith Effect (*.hfx)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 20 * 1024 * 1024)
            throw editing::EditError(tr("无法读取预设"));
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        const auto json = document.object();
        if (error.error != QJsonParseError::NoError || !document.isObject() ||
            json.value("version").toInt() != 1 || !json.value("tone").isObject() ||
            !json.value("amount").isDouble())
            throw editing::EditError(tr("预设格式无效"));
        editing::Effect effect;
        effect.name = json.value("name").toString(tr("预设"));
        effect.amount = json.value("amount").toDouble(1);
        effect.tone = editing::tone_from_json(json.value("tone").toObject());
        const auto lut = json.value("lut").toString();
        if (!lut.isEmpty())
            effect.lut = editing::parse_cube(lut);
        editing::validate_effects({effect});
        effects_.push_back(std::move(effect));
        rebuild(int(effects_.size()) - 1);
        schedule();
    }
    void schedule() {
        pending_ = true;
        timer_.start();
    }
    void poll() {
        if (worker_.valid()) {
            if (worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            try {
                const auto result = worker_.get();
                image_->setPixmap(QPixmap::fromImage(
                    result.scaled(image_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                status_->setText(tr("预览已更新；原始图像保留，确认后可继续编辑效果"));
            } catch (const std::exception& error) {
                status_->setText(QString::fromUtf8(error.what()));
                if (closing_ != QDialog::Rejected) {
                    closing_ = -1;
                    setEnabled(true);
                    return;
                }
            }
        }
        if (closing_ >= 0 && !pending_) {
            QDialog::done(closing_);
            timer_.stop();
            return;
        }
        if (!pending_) {
            timer_.stop();
            return;
        }
        pending_ = false;
        auto draft = source_;
        if (preview_->isChecked())
            editing::find_layer(draft.layers, layer_id_)->effects = effects_;
        status_->setText(tr("正在更新预览…"));
        worker_ = std::async(std::launch::async, [draft = std::move(draft)] {
            return editing::display_image(editing::render(editing::preview_state(draft)));
        });
    }
    editing::State source_;
    QUuid layer_id_;
    std::vector<editing::Effect> effects_;
    QListWidget* list_{};
    QLabel* image_{};
    QLabel* status_{};
    TonePanel* panel_{};
    QDoubleSpinBox* amount_{};
    QCheckBox* preview_{};
    QTimer timer_;
    std::future<QImage> worker_;
    bool loading_{};
    bool pending_{};
    int closing_{-1};
};
} // namespace
void EditorController::show_effects() {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    const auto base = c->project().state();
    const auto id = c->active_layer;
    if (!editing::find_layer(base.layers, id) || editing::layer_locked(base.layers, id))
        throw editing::EditError(tr("请选择未锁定的图层"));
    const auto revision = c->project().revision();
    QPointer<CanvasView> original(c);
    EffectsDialog dialog(base, id, window_);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!original || original->project().revision() != revision)
        throw editing::EditError(tr("文档已变化，无法提交旧效果"));
    original->edit(tr("智能效果"), [&](editing::State& state) {
        editing::find_layer(state.layers, id)->effects = dialog.effects();
    });
}
} // namespace huelith::desktop
