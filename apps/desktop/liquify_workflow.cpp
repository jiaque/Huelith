#include "canvas_view.h"
#include "portrait_liquify.h"
#include "editor_controller.h"
#include "huelith/editing/liquify.h"
#include "main_window.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScrollArea>
#include <QTabletEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <future>

namespace huelith::desktop {
namespace {
class LiquifyView final : public QWidget {
  public:
    LiquifyView(QImage source, QSize size, editing::Effect effect, editing::ToneSettings tone,
                double exposure, QWidget* parent)
        : QWidget(parent), mesh(effect.liquify), source_(std::move(source)),
          effect_(std::move(effect)), tone_(std::move(tone)), exposure_(exposure), size_(size) {
        setObjectName("liquifyView");
        setMinimumSize(400, 350);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        original_ = display(source_);
        frame_ = original_;
        timer_.setInterval(25);
        connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
        timer_.start();
        schedule();
    }
    ~LiquifyView() override {
        cancelled_.store(true);
        if (job_.valid())
            job_.wait();
    }
    QImage mesh;
    editing::LiquifyBrush brush;
    bool show_mask{true}, show_mesh{}, preview{true};
    bool stylus_pressure{true}, show_backdrop{};
    QImage backdrop;
    double backdrop_opacity{.5};
    bool backdrop_front{};
    int navigation{}; // 0 paint, 1 hand, 2 zoom
    double rate{.5};
    std::function<void()> changed;
    void checkpoint() {
        undo_.push_back(mesh);
        if (undo_.size() > 30)
            undo_.erase(undo_.begin());
        redo_.clear();
    }
    void undo() {
        if (undo_.empty())
            return;
        redo_.push_back(mesh);
        mesh = undo_.back();
        undo_.pop_back();
        schedule();
    }
    void redo() {
        if (redo_.empty())
            return;
        undo_.push_back(mesh);
        mesh = redo_.back();
        redo_.pop_back();
        schedule();
    }
    void schedule() {
        ++revision_;
        pending_ = true;
        cancelled_.store(true);
        setProperty("ready", false);
        if (changed)
            changed();
        update();
    }
    void fit() {
        zoom_ = 1;
        pan_ = {};
        update();
    }
    void actual() {
        zoom_ = size_.width() / std::max(1., fit_rect().width());
        pan_ = {};
        update();
    }
    void global_freeze(int op) {
        checkpoint();
        editing::freeze_liquify(mesh, op);
        schedule();
    }
    void reconstruct(double amount) {
        checkpoint();
        editing::reconstruct_liquify(mesh, amount);
        schedule();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(40, 42, 44));
        const QRectF area = image_rect();
        setProperty("imageRect", area);
        setProperty("zoom", zoom_);
        p.save();
        p.setClipRect(area);
        for (int y = int(area.top()); y < area.bottom(); y += 12)
            for (int x = int(area.left()); x < area.right(); x += 12)
                p.fillRect(x, y, 12, 12,
                           ((x - int(area.left())) / 12 + (y - int(area.top())) / 12) % 2
                               ? QColor(100, 100, 100)
                               : QColor(140, 140, 140));
        const auto background = [&] {
            if (show_backdrop && !backdrop.isNull()) {
                p.save(); p.setOpacity(backdrop_opacity); p.drawImage(area, backdrop); p.restore();
            }
        };
        if (!backdrop_front) background();
        p.drawImage(area, preview ? frame_ : original_);
        if (backdrop_front) background();
        if (show_mask) {
            QImage mask(mesh.size(), QImage::Format_ARGB32);
            for (int y = 0; y < mesh.height(); ++y) {
                const auto* row = reinterpret_cast<const float*>(mesh.constScanLine(y));
                auto* dst = reinterpret_cast<QRgb*>(mask.scanLine(y));
                for (int x = 0; x < mesh.width(); ++x)
                    dst[x] = qRgba(255, 30, 30, qRound(row[x * 4 + 2] * 120));
            }
            p.drawImage(area, mask);
        }
        if (show_mesh) {
            p.setPen(QPen(QColor(100, 205, 255, 180), 1));
            // Contours of the inverse map, not a negated displacement approximation.
            if (grid_key_ != mesh.cacheKey()) {
                grid_key_ = mesh.cacheKey();
                grid_.clear();
                const double mw = mesh.width() - 1, mh = mesh.height() - 1;
                for (int y = 0; y < mesh.height() - 1; ++y)
                    for (int x = 0; x < mesh.width() - 1; ++x) {
                        const std::array<QPointF, 4> points{
                            QPointF(x / mw, y / mh), QPointF((x + 1) / mw, y / mh),
                            QPointF((x + 1) / mw, (y + 1) / mh), QPointF(x / mw, (y + 1) / mh)};
                        const auto* top = reinterpret_cast<const float*>(mesh.constScanLine(y));
                        const auto* bottom =
                            reinterpret_cast<const float*>(mesh.constScanLine(y + 1));
                        for (int axis = 0; axis < 2; ++axis) {
                            std::array<double, 4> values{};
                            const std::array<float, 4> delta{
                                top[x * 4 + axis], top[(x + 1) * 4 + axis],
                                bottom[(x + 1) * 4 + axis], bottom[x * 4 + axis]};
                            for (int k = 0; k < 4; ++k)
                                values[k] = (axis ? points[k].y() : points[k].x()) + delta[k];
                            const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
                            for (int level = std::max(0, int(std::ceil(*lo * 24)));
                                 level <= std::min(24, int(std::floor(*hi * 24))); ++level) {
                                const double threshold = level / 24.;
                                std::vector<QPointF> crossings;
                                for (int a = 0; a < 4; ++a) {
                                    const int b = (a + 1) % 4;
                                    if ((values[a] < threshold && values[b] >= threshold) ||
                                        (values[b] < threshold && values[a] >= threshold))
                                        crossings.push_back(points[a] +
                                                            (points[b] - points[a]) *
                                                                ((threshold - values[a]) /
                                                                 (values[b] - values[a])));
                                }
                                for (std::size_t i = 1; i < crossings.size(); i += 2)
                                    grid_.emplace_back(crossings[i - 1], crossings[i]);
                            }
                        }
                    }
            }
            p.translate(area.topLeft());
            p.scale(area.width(), area.height());
            auto pen = p.pen();
            pen.setCosmetic(true);
            p.setPen(pen);
            if (!grid_.empty())
                p.drawLines(grid_.data(), int(grid_.size()));
        }
        p.restore();
        if (underMouse() && navigation == 0 && !space_) {
            const double radius = brush.diameter * .5 * area.width() / size_.width();
            p.setPen(QPen(Qt::white, 1));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(cursor_, radius, radius);
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton)
            return;
        setFocus();
        modifiers_ = e->modifiers();
        cursor_ = e->position();
        last_screen_ = cursor_;
        if (navigation == 2 && !space_) {
            zoom_at(cursor_, e->modifiers().testFlag(Qt::AltModifier) ? 1 / 1.25 : 1.25);
            return;
        }
        dragging_ = true;
        panning_ = space_ || navigation == 1;
        if (panning_)
            return;
        checkpoint();
        last_ = image_point(cursor_);
        dab(last_, last_, 1);
        hold_.restart();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        modifiers_ = e->modifiers();
        cursor_ = e->position();
        if (dragging_) {
            if (panning_)
                pan_ += cursor_ - last_screen_;
            else {
                const QPointF next = image_point(cursor_), start = last_;
                const int steps = std::clamp(int(std::ceil(QLineF(start, next).length() /
                                                           std::max(1., brush.diameter * .08))),
                                             1, 256);
                for (int i = 1; i <= steps; ++i) {
                    const auto at = start + (next - start) * (double(i) / steps);
                    dab(last_, at, 1);
                    last_ = at;
                }
            }
        }
        last_screen_ = cursor_;
        update();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            dragging_ = false;
            panning_ = false;
        }
    }
    void wheelEvent(QWheelEvent* e) override {
        zoom_at(e->position(), std::pow(1.25, e->angleDelta().y() / 120.));
        e->accept();
    }
    void tabletEvent(QTabletEvent* e) override {
        pressure_ = stylus_pressure ? e->pressure() : 1;
        const auto type = e->type() == QEvent::TabletPress ? QEvent::MouseButtonPress :
                          e->type() == QEvent::TabletRelease ? QEvent::MouseButtonRelease : QEvent::MouseMove;
        QMouseEvent mouse(type, e->position(), e->globalPosition(),
                          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, e->modifiers());
        if (type == QEvent::MouseButtonPress) mousePressEvent(&mouse);
        else if (type == QEvent::MouseMove) mouseMoveEvent(&mouse);
        else { mouseReleaseEvent(&mouse); pressure_ = 1; }
        e->accept();
    }
    void keyPressEvent(QKeyEvent* e) override {
        modifiers_ = e->modifiers();
        if (e->key() == Qt::Key_Space) {
            space_ = true;
            e->accept();
            update();
            return;
        }
        if (e->key() == Qt::Key_Escape && dragging_) {
            dragging_ = false;
            undo();
            e->accept();
            return;
        }
        QWidget::keyPressEvent(e);
    }
    void keyReleaseEvent(QKeyEvent* e) override {
        modifiers_ = e->modifiers();
        if (e->key() == Qt::Key_Space) {
            space_ = false;
            update();
            return;
        }
        QWidget::keyReleaseEvent(e);
    }

  private:
    QImage source_, original_, frame_;
    editing::Effect effect_;
    editing::ToneSettings tone_;
    double exposure_{};
    qint64 grid_key_{};
    std::vector<QLineF> grid_;
    QSize size_;
    QTimer timer_;
    QElapsedTimer hold_;
    std::future<QImage> job_;
    std::atomic_bool cancelled_{};
    std::vector<QImage> undo_, redo_;
    int revision_{}, running_revision_{};
    bool pending_{}, dragging_{}, panning_{}, space_{};
    QPointF cursor_, last_, last_screen_, pan_;
    double zoom_{1};
    double pressure_{1};
    Qt::KeyboardModifiers modifiers_;
    QRectF fit_rect() const {
        QSizeF extent = size_;
        extent.scale(QSizeF(width() - 30, height() - 30), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - extent.width()) / 2, (height() - extent.height()) / 2),
                      extent);
    }
    QRectF image_rect() const {
        auto r = fit_rect();
        auto s = r.size() * zoom_;
        return QRectF(QPointF(width() / 2., height() / 2.) -
                          QPointF(s.width() / 2, s.height() / 2) + pan_,
                      s);
    }
    QPointF image_point(QPointF p) const {
        const auto r = image_rect();
        return {(p.x() - r.left()) / r.width() * (size_.width() - 1),
                (p.y() - r.top()) / r.height() * (size_.height() - 1)};
    }
    void zoom_at(QPointF point, double factor) {
        const auto before = image_rect();
        const auto relative = point - before.topLeft();
        const double next = std::clamp(zoom_ * factor, .1, 32.);
        factor = next / zoom_;
        zoom_ = next;
        const auto after = image_rect();
        pan_ += point - (after.topLeft() + relative * factor);
        update();
    }
    void dab(QPointF from, QPointF to, double amount) {
        auto active = brush;
        active.pressure *= pressure_;
        active.reverse = modifiers_.testFlag(Qt::AltModifier);
        if (active.reverse && active.tool == editing::LiquifyTool::freeze)
            active.tool = editing::LiquifyTool::thaw;
        else if (active.reverse && active.tool == editing::LiquifyTool::thaw)
            active.tool = editing::LiquifyTool::freeze;
        editing::liquify_dab(mesh, size_, from, to, active, amount);
        schedule();
    }
    QImage display(QImage image) {
        if (exposure_ != 0) {
            const float gain = float(std::exp2(exposure_));
            for (int y = 0; y < image.height(); ++y) {
                auto* row = reinterpret_cast<float*>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    for (int c = 0; c < 3; ++c)
                        row[x * 4 + c] *= gain;
            }
        }
        return editing::display_image(editing::apply_tone(image, tone_, &cancelled_));
    }
    void poll() {
        if (dragging_ && !panning_ && brush.tool != editing::LiquifyTool::warp &&
            brush.tool != editing::LiquifyTool::push_left && hold_.isValid()) {
            const double seconds = std::min(.1, hold_.restart() / 1000.);
            dab(last_, last_, seconds * 20 * rate);
        }
        if (job_.valid() &&
            job_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            try {
                auto frame = job_.get();
                if (running_revision_ == revision_) {
                    frame_ = std::move(frame);
                    setProperty("ready", true);
                    update();
                }
            } catch (const std::exception& error) {
                if (running_revision_ == revision_)
                    setProperty("error", QString::fromUtf8(error.what()));
            }
        }
        if (pending_ && !job_.valid()) {
            cancelled_.store(false);
            pending_ = false;
            running_revision_ = revision_;
            job_ = std::async(std::launch::async, [this, field = mesh] {
                auto effect = effect_;
                effect.liquify = field;
                return display(editing::apply_effects(source_, {effect}, &cancelled_));
            });
        }
    }
};
class LiquifyDialog final : public QDialog {
  public:
    LiquifyDialog(QImage source, QImage backdrop, QSize size, editing::Effect effect, editing::ToneSettings tone,
                  double exposure, QWidget* parent)
        : QDialog(parent) {
        setObjectName("liquifyDialog");
        setWindowTitle(tr("液化"));
        resize(1150, 780);
        auto* root = new QHBoxLayout(this);
        auto* tools = new QVBoxLayout;
        root->addLayout(tools);
        view_ = new LiquifyView(std::move(source), size, std::move(effect), std::move(tone),
                                exposure, this);
        view_->backdrop = std::move(backdrop);
        root->addWidget(view_, 1);
        auto* side = new QVBoxLayout;
        auto* side_widget = new QWidget(this); side_widget->setLayout(side);
        auto* side_scroll = new QScrollArea(this); side_scroll->setWidgetResizable(true);
        side_scroll->setWidget(side_widget); side_scroll->setFixedWidth(330);
        auto* right_column = new QVBoxLayout;
        right_column->addWidget(side_scroll, 1); root->addLayout(right_column);
        auto* group = new QButtonGroup(this);
        const QStringList names{tr("向前变形 (W)"),   tr("重建 (R)"),     tr("平滑 (E)"),
                                tr("顺时针旋转 (C)"), tr("褶皱 (S)"),     tr("膨胀 (B)"),
                                tr("左推 (O)"),       tr("冻结蒙版 (F)"), tr("解冻蒙版 (D)"),
                                tr("抓手 (H)"),       tr("缩放 (Z)")};
        const QStringList glyphs{"↗", "↶", "≈", "↻", "↘", "↖", "⇐", "▧", "▨", "✋", "⊕"};
        const QStringList keys{"W", "R", "E", "C", "S", "B", "O", "F", "D", "H", "Z"};
        for (int i = 0; i < names.size(); ++i) {
            auto* b = new QToolButton(this);
            b->setObjectName(QString("liquifyTool%1").arg(i));
            b->setText(glyphs[i]);
            b->setToolTip(names[i]);
            b->setCheckable(true);
            b->setFixedSize(34, 34);
            group->addButton(b, i);
            tools->addWidget(b);
            b->setChecked(i == 0);
            auto* key = new QShortcut(QKeySequence(keys[i]), this);
            connect(key, &QShortcut::activated, b, &QToolButton::click);
        }
        connect(group, &QButtonGroup::idClicked, this, [this](int id) {
            view_->navigation = id >= 9 ? id - 8 : 0;
            if (id < 9)
                view_->brush.tool = editing::LiquifyTool(id);
            view_->setFocus();
            view_->update();
        });
        tools->addStretch();
        auto* options = new QGroupBox(tr("画笔工具选项"), this);
        auto* form = new QFormLayout(options);
        side->addWidget(options);
        const auto spin = [&](const QString& label, const char* name, double value, double maximum,
                              const std::function<void(double)>& set) {
            auto* control = new QDoubleSpinBox(this);
            control->setObjectName(name);
            control->setDecimals(0);
            control->setRange(0, maximum);
            control->setValue(value);
            auto* row = new QWidget(this);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            auto* slider = new QSlider(Qt::Horizontal, row);
            slider->setObjectName(QString(name) + "Slider");
            slider->setRange(name == QString("liquifySize") ? 1 : 0, int(maximum));
            slider->setValue(int(value));
            layout->addWidget(slider, 1);
            layout->addWidget(control);
            control->setMaximumWidth(90);
            connect(slider, &QSlider::valueChanged, control,
                    [control](int v) { control->setValue(v); });
            connect(control, &QDoubleSpinBox::valueChanged, slider,
                    [slider](double v) { slider->setValue(qRound(v)); });
            form->addRow(label, row);
            connect(control, &QDoubleSpinBox::valueChanged, this, set);
            return control;
        };
        diameter_ = spin(tr("大小"), "liquifySize", 100, 15000, [this](double v) {
            view_->brush.diameter = v;
            view_->update();
        });
        diameter_->setMinimum(1);
        spin(tr("密度"), "liquifyDensity", 50, 100,
             [this](double v) { view_->brush.density = v / 100; });
        spin(tr("压力"), "liquifyPressure", 50, 100,
             [this](double v) { view_->brush.pressure = v / 100; });
        spin(tr("速率"), "liquifyRate", 50, 100, [this](double v) { view_->rate = v / 100; });
        const auto button = [&](const QString& text, const char* name,
                                const std::function<void()>& fn) {
            auto* b = new QPushButton(text, this);
            b->setObjectName(name);
            b->setAutoDefault(false);
            side->addWidget(b);
            connect(b, &QPushButton::clicked, this, fn);
            return b;
        };
        button(tr("全部冻结"), "liquifyFreezeAll", [this] { view_->global_freeze(1); });
        button(tr("无蒙版"), "liquifyThawAll", [this] { view_->global_freeze(0); });
        button(tr("反相蒙版"), "liquifyInvert", [this] { view_->global_freeze(2); });
        auto* reconstruction = new QDoubleSpinBox(this);
        reconstruction->setObjectName("liquifyReconstructAmount");
        reconstruction->setRange(0, 100);
        reconstruction->setValue(50);
        reconstruction->setSuffix(" %");
        side->addWidget(new QLabel(tr("重建数量"), this));
        side->addWidget(reconstruction);
        button(tr("重建"), "liquifyReconstruct",
               [this, reconstruction] { view_->reconstruct(reconstruction->value() / 100); });
        button(tr("恢复全部"), "liquifyRestore", [this] { view_->reconstruct(1); });
        const auto check = [&](const QString& text, const char* name, bool initial,
                               const std::function<void(bool)>& fn) {
            auto* b = new QCheckBox(text, this);
            b->setObjectName(name);
            b->setChecked(initial);
            side->addWidget(b);
            connect(b, &QCheckBox::toggled, this, fn);
        };
        check(tr("显示蒙版"), "liquifyMask", true, [this](bool v) {
            view_->show_mask = v;
            view_->update();
        });
        check(tr("显示网格"), "liquifyMesh", false, [this](bool v) {
            view_->show_mesh = v;
            view_->update();
        });
        check(tr("预览"), "liquifyPreview", true, [this](bool v) {
            view_->preview = v;
            view_->update();
        });
        check(tr("使用数位笔压力"), "liquifyStylusPressure", true, [this](bool v) {view_->stylus_pressure=v;});
        check(tr("显示背景（所有图层）"), "liquifyBackdrop", false, [this](bool v) {view_->show_backdrop=v;view_->update();});
        auto* backdrop_mode = new QComboBox(this); backdrop_mode->setObjectName("liquifyBackdropMode");
        backdrop_mode->addItems({tr("背景在后面"),tr("背景在前面")}); side->addWidget(backdrop_mode);
        connect(backdrop_mode,&QComboBox::currentIndexChanged,this,[this](int i){view_->backdrop_front=i==1;view_->update();});
        auto* backdrop_opacity = new QDoubleSpinBox(this); backdrop_opacity->setObjectName("liquifyBackdropOpacity");
        backdrop_opacity->setRange(0,100);backdrop_opacity->setValue(50);backdrop_opacity->setSuffix(" %");
        side->addWidget(new QLabel(tr("背景不透明度"),this));side->addWidget(backdrop_opacity);
        connect(backdrop_opacity,&QDoubleSpinBox::valueChanged,this,[this](double v){view_->backdrop_opacity=v/100;view_->update();});
        auto* resource_status=new QLabel(this);resource_status->setObjectName("liquifyResourceStatus");resource_status->setWordWrap(true);
        button(tr("保存网格…"),"liquifySaveMesh",[this,resource_status]{
            const auto path=QFileDialog::getSaveFileName(this,tr("保存液化网格"),{},"Huelith Mesh (*.hlmesh)");
            if(path.isEmpty())return;
            try {const auto data=QJsonDocument(editing::liquify_json(view_->mesh)).toJson(QJsonDocument::Compact);
                QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit())throw editing::EditError(tr("网格保存失败"));
                resource_status->setText(tr("网格已保存（含冻结区域）"));
            }catch(const std::exception& e){resource_status->setText(QString::fromUtf8(e.what()));}
        });
        button(tr("载入网格…"),"liquifyLoadMesh",[this,resource_status]{
            const auto path=QFileDialog::getOpenFileName(this,tr("载入液化网格"),{},"Huelith Mesh (*.hlmesh)");
            if(path.isEmpty())return;
            try {QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>32*1024*1024)throw editing::EditError(tr("无法读取网格"));
                QJsonParseError error;const auto json=QJsonDocument::fromJson(file.readAll(),&error);
                if(error.error!=QJsonParseError::NoError||!json.isObject())throw editing::EditError(tr("网格格式无效"));
                const auto mesh=editing::liquify_from_json(json.object());if(mesh.isNull())throw editing::EditError(tr("网格为空"));
                view_->checkpoint();view_->mesh=mesh;view_->schedule();resource_status->setText(tr("网格已载入，可撤销"));
            }catch(const std::exception& e){resource_status->setText(QString::fromUtf8(e.what()));}
        });
        side->addWidget(resource_status);
        button(tr("适合视图"), "liquifyFit", [this] { view_->fit(); });
        button(tr("100%"), "liquifyActual", [this] { view_->actual(); });
        side->addStretch();
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        right_column->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        const auto shortcut = [this](const QString& key, const std::function<void()>& fn) {
            auto* s = new QShortcut(QKeySequence(key), this);
            connect(s, &QShortcut::activated, this, fn);
        };
        shortcut("Ctrl+Z", [this] { view_->undo(); });
        shortcut("Ctrl+Shift+Z", [this] { view_->redo(); });
        shortcut("[", [this] { diameter_->setValue(std::max(1., diameter_->value() / 1.1)); });
        shortcut("]", [this] { diameter_->setValue(diameter_->value() * 1.1); });
        view_->changed = [this] { setProperty("meshChanged", true); };
    }
    QImage mesh() const {
        return view_->mesh;
    }

  private:
    LiquifyView* view_{};
    QDoubleSpinBox* diameter_{};
};
} // namespace
std::optional<QImage> edit_portrait_liquify(QWidget* parent,const QImage& source,QSize original_size,QImage mesh,const QImage& person_mask) {
    if(mesh.isNull()) {
        mesh=editing::create_liquify_mesh(original_size);
        if(!person_mask.isNull()){const auto mask=person_mask.scaled(mesh.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);for(int y=0;y<mesh.height();++y){auto* row=reinterpret_cast<float*>(mesh.scanLine(y));for(int x=0;x<mesh.width();++x)row[x*4+2]=1-mask.constScanLine(y)[x]/255.F;}}
    }
    editing::Effect effect;effect.name=QStringLiteral("人像液化");effect.liquify=mesh;
    LiquifyDialog dialog(source,editing::display_image(source),original_size,effect,{},0,parent);
    dialog.setWindowModality(Qt::WindowModal);
    if(dialog.exec()!=QDialog::Accepted)return std::nullopt;
    return dialog.mesh();
}
void EditorController::show_liquify() {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    const auto id = c->active_layer;
    const auto* layer = editing::find_layer(c->project().state().layers, id);
    if (!layer || layer->pixels.isNull() || editing::layer_locked(c->project().state().layers, id))
        throw editing::EditError(tr("请选择未锁定的像素或智能对象图层"));
    auto effects = layer->effects;
    editing::Effect effect;
    effect.name = tr("液化");
    const bool reopen = !effects.empty() && !effects.back().liquify.isNull();
    if (reopen) {
        effect = effects.back();
        effects.pop_back();
    } else
        effect.liquify = editing::create_liquify_mesh(layer->pixels.size());
    if (!reopen && !c->project().state().selection.isNull()) {
        const auto& state = c->project().state();
        const auto matrix = layer->transform * editing::parent_matrix(state, id);
        effect.mask = QImage(layer->pixels.size(), QImage::Format_Grayscale8);
        effect.mask.fill(0);
        for (int y = 0; y < effect.mask.height(); ++y)
            for (int x = 0; x < effect.mask.width(); ++x) {
                const auto warped = editing::warp_point(*layer, layer->pixels.size(),
                                                        QPointF(x + .5, y + .5), false);
                if (!warped)
                    continue;
                const auto point = matrix.map(*warped);
                const QPoint pixel(int(std::floor(point.x())), int(std::floor(point.y())));
                if (state.selection.rect().contains(pixel))
                    effect.mask.scanLine(y)[x] =
                        state.selection.constScanLine(pixel.y())[pixel.x()];
            }
        const auto coverage = effect.mask.scaled(effect.liquify.size(), Qt::IgnoreAspectRatio,
                                                 Qt::SmoothTransformation);
        for (int y = 0; y < effect.liquify.height(); ++y) {
            auto* row = reinterpret_cast<float*>(effect.liquify.scanLine(y));
            for (int x = 0; x < effect.liquify.width(); ++x)
                row[x * 4 + 2] = 1 - coverage.constScanLine(y)[x] / 255.F;
        }
    }
    auto proxy = layer->pixels;
    if (std::max(proxy.width(), proxy.height()) > 900)
        proxy = proxy.scaled(900, 900, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    proxy = editing::apply_effects(proxy, effects);
    const auto revision = c->project().revision();
    QPointer<CanvasView> original(c);
    const auto background_state = editing::preview_state(c->project().state(), 900);
    const auto background = editing::display_image(editing::render(background_state));
    QImage backdrop(proxy.size(),QImage::Format_ARGB32_Premultiplied);backdrop.fill(Qt::transparent);
    {
        const double scale = double(proxy.width())/layer->pixels.width();
        const auto matrix = layer->transform * editing::parent_matrix(c->project().state(), id);
        QPainter painter(&backdrop);painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setTransform(matrix.inverted()*QTransform::fromScale(scale,scale));
        painter.drawImage(QRectF(QPointF(),c->project().state().size),background);
    }
    LiquifyDialog dialog(proxy, backdrop, layer->pixels.size(), effect, layer->tone, layer->exposure,
                         window_);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!original || original->project().revision() != revision)
        throw editing::EditError(tr("文档已变化，液化未提交"));
    effect.liquify = dialog.mesh();
    effects.push_back(std::move(effect));
    original->edit(tr("液化"), [&](editing::State& state) {
        editing::find_layer(state.layers, id)->effects = effects;
    });
    refresh();
}
} // namespace huelith::desktop
