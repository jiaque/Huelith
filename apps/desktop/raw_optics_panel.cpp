#include "raw_optics_panel.h"
#include "color_band_slider.h"
#include "huelith/editing/point_color.h"
#include "raw_section_header.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
class HueRange final : public QWidget {
  public:
    HueRange(bool purple, QWidget* parent) : QWidget(parent), purple_(purple) {
        setFixedHeight(40);
        setFocusPolicy(Qt::StrongFocus);
    }
    std::array<double, 2> range{};
    std::function<void(std::array<double, 2>)> changed;
    std::function<void()> started, finished;

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        QLinearGradient gradient(6, 0, width() - 6, 0);
        for (int i = 0; i <= 20; ++i)
            gradient.setColorAt(
                i / 20.,
                QColor::fromHsvF(
                    float(std::fmod((purple_ ? 180 : 60) + i * (purple_ ? 9. : 6.), 360) / 360),
                    .7F, .75F));
        p.fillRect(QRectF(6, 22, width() - 12, 5), gradient);
        p.setPen(QColor("#bbb"));
        p.drawText(QRect(6, 0, width() - 12, 18), Qt::AlignRight,
                   QString("%1 / %2").arg(range[0], 0, 'f', 0).arg(range[1], 0, 'f', 0));
        for (int i = 0; i < 2; ++i) {
            const double x = 6 + range[i] / 100 * (width() - 12);
            p.setBrush(hasFocus() && active_ == i ? QColor("#69beff") : Qt::white);
            p.drawPolygon(QPolygonF{QPointF(x, 23), QPointF(x - 4, 34), QPointF(x + 4, 34)});
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton)
            return;
        setFocus();
        if (started)
            started();
        anchor_ = value(e->position().x());
        initial_ = range;
        dragging_ = true;
        const double a = std::abs(anchor_ - range[0]), b = std::abs(anchor_ - range[1]);
        active_ = anchor_ > range[0] + 5 && anchor_ < range[1] - 5 ? 2 : (a < b ? 0 : 1);
        update_at(anchor_);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_ && e->buttons().testFlag(Qt::LeftButton))
            update_at(value(e->position().x()));
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            dragging_ = false;
            if (finished) finished();
        }
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape && dragging_) {
            dragging_ = false;
            range = initial_;
            if (changed) changed(range);
            if (finished) finished();
            update();
            e->accept();
            return;
        }
        if (e->key() != Qt::Key_Left && e->key() != Qt::Key_Right) {
            QWidget::keyPressEvent(e);
            return;
        }
        if (started)
            started();
        initial_ = range;
        anchor_ = 0;
        const double step = e->key() == Qt::Key_Left ? -1 : 1;
        update_at(active_ == 2 ? step : range[active_] + step);
        if (finished)
            finished();
    }
    bool focusNextPrevChild(bool next) override {
        if (next && active_ == 0) {
            active_ = 1;
            update();
            return true;
        }
        if (!next && active_ == 1) {
            active_ = 0;
            update();
            return true;
        }
        return QWidget::focusNextPrevChild(next);
    }

  private:
    double value(double x) const {
        return std::clamp((x - 6) / std::max(1, width() - 12) * 100, 0., 100.);
    }
    void update_at(double v) {
        if (active_ == 2) {
            const double delta = std::clamp(v - anchor_, -initial_[0], 100 - initial_[1]);
            range = {initial_[0] + delta, initial_[1] + delta};
        } else
            range[active_] =
                std::clamp(v, active_ == 0 ? 0 : range[0] + 10, active_ == 0 ? range[1] - 10 : 100);
        if (changed)
            changed(range);
        update();
    }
    bool purple_;
    bool dragging_{};
    int active_{};
    double anchor_{};
    std::array<double, 2> initial_{};
};
RawOpticsPanel::RawOpticsPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("rawOpticsPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    const auto header = raw_section_header(this, tr("光学"), "rawOpticsExpand", "rawOpticsEnable");
    auto* expand = header.expand;
    enabled_ = header.enabled;
    root->addLayout(header.layout);
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 0, 8, 0);
    root->addWidget(body);
    body->hide();
    connect(expand, &QToolButton::toggled, this, [body, expand](bool value) {
        body->setVisible(value);
        expand->setArrowType(value ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(enabled_, &QToolButton::toggled, this, [this](bool value) {
        if (loading_)
            return;
        settings_.enabled = value;
        update_dependencies();
        if (changed)
            changed();
    });
    auto* tabs = new QTabWidget(body);
    tabs->setObjectName("rawOpticsTabs");
    layout->addWidget(tabs);
    auto* profile_page = new QWidget(tabs);
    auto* profile_form = new QFormLayout(profile_page);
    tabs->addTab(profile_page, tr("描述文件"));
    auto* manual_page = new QWidget(tabs);
    auto* manual_form = new QFormLayout(manual_page);
    tabs->addTab(manual_page, tr("手动"));
    ca_ = new QCheckBox(tr("移除色差"), profile_page);
    ca_->setObjectName("rawOpticsCA");
    profile_form->addRow(ca_);
    profile_ = new QCheckBox(tr("使用镜头配置校正"), profile_page);
    profile_->setObjectName("rawOpticsProfileEnable");
    profile_form->addRow(profile_);
    maker_ = new QComboBox(profile_page);
    maker_->setObjectName("rawOpticsMaker");
    model_ = new QComboBox(profile_page);
    model_->setObjectName("rawOpticsModel");
    model_->setMinimumContentsLength(15);
    model_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    profile_form->addRow(tr("制造商"), maker_);
    profile_form->addRow(tr("型号"), model_);
    profile_choice_ = new QComboBox(profile_page);
    profile_choice_->setObjectName("rawOpticsProfile");
    profile_choice_->setMinimumContentsLength(15);
    profile_choice_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    profile_form->addRow(tr("描述文件"), profile_choice_);
    auto* automatic = new QPushButton(tr("根据照片识别"), profile_page);
    automatic->setAutoDefault(false);
    automatic->setObjectName("rawOpticsAuto");
    profile_form->addRow(automatic);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    profile_form->addRow(status_);
    try {
        profiles_ = editing::lens_profiles(qEnvironmentVariable(
            "HUELITH_LENSFUN_DATABASE", QCoreApplication::applicationDirPath() + "/lensfun"));
        maker_->addItem(tr("请选择镜头"));
        for (const auto& p : profiles_)
            if (maker_->findText(p.maker) < 0)
                maker_->addItem(p.maker);
    } catch (const std::exception& e) {
        status_->setText(QString::fromUtf8(e.what()));
    }
    connect(maker_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_)
            return;
        const QSignalBlocker block(model_);
        model_->clear();
        model_->addItem(tr("请选择配置"), -1);
        for (int i = 0; i < int(profiles_.size()); ++i)
            if (profiles_[std::size_t(i)].maker == maker_->currentText() &&
                model_->findText(profiles_[std::size_t(i)].model) < 0)
                model_->addItem(profiles_[std::size_t(i)].model, i);
        const QSignalBlocker profile_block(profile_choice_);
        profile_choice_->clear();
        clear_profile();
    });
    connect(model_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!loading_) {
            populate_profiles(model_->currentData().toInt());
            if (profile_choice_->count())
                choose(profile_choice_->currentData().toInt());
            else
                clear_profile();
        }
    });
    connect(profile_choice_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_ && profile_choice_->currentIndex() >= 0)
            choose(profile_choice_->currentData().toInt());
    });
    connect(automatic, &QPushButton::clicked, this, [this] {
        const int index = editing::match_lens_profile(profiles_, metadata_);
        if (index < 0) {
            status_->setText(tr("未找到唯一匹配。请选择镜头配置，或使用手动校正。"));
            return;
        }
        choose(index);
    });
    connect(profile_, &QCheckBox::toggled, this, [this](bool value) {
        if (loading_)
            return;
        if (value && settings_.profile_xml.isEmpty()) {
            const QSignalBlocker block(profile_);
            profile_->setChecked(false);
            status_->setText(tr("请先识别或选择镜头配置。"));
            return;
        }
        settings_.profile_enabled = value;
        update_dependencies();
        if (changed)
            changed();
    });
    connect(ca_, &QCheckBox::toggled, this, [this](bool value) {
        if (loading_)
            return;
        settings_.remove_ca = value;
        update_dependencies();
        if (changed)
            changed();
    });
    const auto number = [this](QFormLayout* form, const char* name, const QString& label,
                               double low, double high, double editing::OpticsSettings::* member) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QLatin1String(name));
        spin->setRange(low, high);
        spin->setDecimals(member == &editing::OpticsSettings::crop_factor ? 2 : 1);
        spin->setValue(settings_.*member);
        spin->setKeyboardTracking(false);
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label, this));
        row->addStretch();
        row->addWidget(spin);
        form->addRow(row);
        auto* slider = new ColorBandSlider(-1, 0, this);
        slider->setObjectName(QString::fromLatin1(name) + "Slider");
        slider->setRange(qRound(low * 10), qRound(high * 10));
        slider->setValue(qRound(settings_.*member * 10));
        slider->set_default(qRound(settings_.*member * 10));
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setFixedWidth(60);
        form->addRow(slider);
        numbers_.emplace_back(spin, member);
        connect(slider, &QSlider::sliderPressed, this, [this] {
            if (gesture_started)
                gesture_started();
        });
        connect(slider, &QSlider::sliderReleased, this, [this] {
            if (gesture_finished)
                gesture_finished();
        });
        connect(slider, &QSlider::valueChanged, spin, [spin](int v) { spin->setValue(v / 10.); });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, member](double v) {
            const QSignalBlocker block(slider);
            slider->setValue(qRound(v * 10));
            if (loading_)
                return;
            settings_.*member = v;
            update_dependencies();
            if (changed)
                changed();
        });
    };
    number(profile_form, "rawOpticsProfileDistortion", tr("畸变"), 0, 200,
           &editing::OpticsSettings::profile_distortion);
    number(profile_form, "rawOpticsProfileVignette", tr("暗角"), 0, 200,
           &editing::OpticsSettings::profile_vignette);
    number(profile_form, "rawOpticsCropFactor", tr("传感器裁切系数"), .1, 20,
           &editing::OpticsSettings::crop_factor);
    number(manual_form, "rawOpticsDistortion", tr("畸变"), -100, 100,
           &editing::OpticsSettings::distortion);
    number(manual_form, "rawOpticsVignette", tr("暗角"), -100, 100,
           &editing::OpticsSettings::vignette);
    number(manual_form, "rawOpticsMidpoint", tr("中点"), 0, 100,
           &editing::OpticsSettings::midpoint);
    auto* fringe = new QFormLayout;
    layout->addLayout(fringe);
    picker_ = new QToolButton(this);
    picker_->setObjectName("rawDefringePicker");
    picker_->setText(tr("修饰外缘 · 吸管取样"));
    picker_->setCheckable(true);
    fringe->addRow(picker_);
    connect(picker_, &QToolButton::toggled, this, [this] {
        if (view_changed)
            view_changed();
    });
    for (int i = 0; i < 2; ++i) {
        number(fringe, i == 0 ? "rawOpticsPurple" : "rawOpticsGreen",
               i == 0 ? tr("紫色数量") : tr("绿色数量"), 0, 20,
               i == 0 ? &editing::OpticsSettings::purple : &editing::OpticsSettings::green);
        fringe->addRow(new QLabel(i == 0 ? tr("紫色色相") : tr("绿色色相"), this));
        ranges_[i] = new HueRange(i == 0, this);
        ranges_[i]->setObjectName(i == 0 ? "rawOpticsPurpleRange" : "rawOpticsGreenRange");
        ranges_[i]->setProperty("opticsAuxiliary", i + 1);
        ranges_[i]->installEventFilter(this);
        auto* amount_slider = findChild<QSlider*>(i == 0 ? "rawOpticsPurpleSlider" : "rawOpticsGreenSlider");
        amount_slider->setProperty("opticsAuxiliary", i + 3);
        amount_slider->installEventFilter(this);
        ranges_[i]->setToolTip(tr("拖动端点调整色相范围；拖动中间移动整个范围。按住 Alt 查看受影响区域。"));
        amount_slider->setToolTip(tr("按住 Alt 拖动，查看彩边修正范围。"));
        fringe->addRow(ranges_[i]);
        ranges_[i]->changed = [this, i](auto range) {
            (i == 0 ? settings_.purple_hue : settings_.green_hue) = range;
            update_dependencies();
            if (changed)
                changed();
        };
        ranges_[i]->started = [this] {
            if (gesture_started)
                gesture_started();
        };
        ranges_[i]->finished = [this] {
            if (gesture_finished)
                gesture_finished();
        };
    }
    auto* reset = new QPushButton(tr("重置光学"), this);
    reset->setAutoDefault(false);
    reset->setObjectName("rawOpticsReset");
    layout->addWidget(reset);
    connect(reset, &QPushButton::clicked, this, [this] {
        set_settings({}, metadata_);
        update_dependencies();
        if (changed)
            changed();
    });
    refresh();
}
void RawOpticsPanel::populate_profiles(int index) {
    const QSignalBlocker block(profile_choice_);
    profile_choice_->clear();
    if (index < 0 || index >= int(profiles_.size()))
        return;
    const auto& selected = profiles_[std::size_t(index)];
    for (int i = 0; i < int(profiles_.size()); ++i) {
        const auto& profile = profiles_[std::size_t(i)];
        if (profile.maker == selected.maker && profile.model == selected.model)
            profile_choice_->addItem(tr("Lensfun · %1 · 裁切 %2×")
                                         .arg(profile.model)
                                         .arg(profile.crop_factor, 0, 'g', 3),
                                     i);
    }
    profile_choice_->setCurrentIndex(profile_choice_->findData(index));
}
void RawOpticsPanel::choose(int index) {
    if (index < 0 || index >= int(profiles_.size()))
        return;
    if (gesture_started)
        gesture_started();
    const auto& p = profiles_[std::size_t(index)];
    settings_.profile_xml = p.xml;
    settings_.profile_name =
        p.model.startsWith(p.maker, Qt::CaseInsensitive) ? p.model : p.maker + " " + p.model;
    settings_.profile_enabled = true;
    settings_.crop_factor = metadata_.value("cropFactor").toDouble(p.crop_factor);
    status_->setText(tr("Lensfun · %1\n畸变 %2 · 暗角 %3 · 色差 %4")
                         .arg(p.model, p.distortion ? tr("有") : tr("无"),
                              p.vignette ? tr("有") : tr("无"), p.ca ? tr("有") : tr("图像估计")));
    refresh();
    update_dependencies();
    if (changed)
        changed();
    if (gesture_finished)
        gesture_finished();
}
void RawOpticsPanel::clear_profile() {
    if (settings_.profile_xml.isEmpty())
        return;
    if (gesture_started)
        gesture_started();
    settings_.profile_xml.clear();
    settings_.profile_name.clear();
    settings_.profile_enabled = false;
    const QSignalBlocker block(profile_);
    profile_->setChecked(false);
    status_->setText(tr("请选择镜头配置，或使用手动校正。"));
    update_dependencies();
    if (changed)
        changed();
    if (gesture_finished)
        gesture_finished();
}
void RawOpticsPanel::set_settings(const editing::OpticsSettings& settings,
                                  const QJsonObject& metadata) {
    if (settings.profile_xml.isEmpty() && (!settings_.profile_xml.isEmpty() ||
                                           metadata.value("camera") != metadata_.value("camera"))) {
        const QSignalBlocker maker_block(maker_), model_block(model_),
            profile_block(profile_choice_);
        maker_->setCurrentIndex(0);
        model_->clear();
        profile_choice_->clear();
        if (!profiles_.empty())
            status_->setText(tr("请选择镜头配置，或根据照片识别。"));
    }
    settings_ = settings;
    const bool camera_changed = metadata.value("camera") != metadata_.value("camera");
    const auto previous_crop = metadata_.value("cropFactor");
    metadata_ = metadata;
    if (camera_changed) {
        const double crop = editing::camera_crop_factor(
            qEnvironmentVariable("HUELITH_LENSFUN_DATABASE",
                                 QCoreApplication::applicationDirPath() + "/lensfun"),
            metadata);
        if (crop > 0)
            metadata_.insert("cropFactor", crop);
    } else if (previous_crop.isDouble())
        metadata_.insert("cropFactor", previous_crop);
    if (metadata.value("focalLength").toDouble() > 0)
        settings_.focal = std::clamp(metadata.value("focalLength").toDouble(50), .1, 3000.);
    if (metadata.value("aperture").toDouble() > 0)
        settings_.aperture = std::clamp(metadata.value("aperture").toDouble(5.6), .1, 128.);
    refresh();
}
void RawOpticsPanel::update_dependencies() {
    const auto selected = std::find_if(profiles_.begin(), profiles_.end(), [this](const auto& p) {
        return p.xml == settings_.profile_xml;
    });
    for (const auto& [spin, member] : numbers_) {
        bool enabled = true;
        if (member == &editing::OpticsSettings::midpoint)
            enabled = settings_.vignette != 0;
        if (member == &editing::OpticsSettings::profile_distortion)
            enabled =
                settings_.profile_enabled && (selected == profiles_.end() || selected->distortion);
        if (member == &editing::OpticsSettings::profile_vignette)
            enabled =
                settings_.profile_enabled && (selected == profiles_.end() || selected->vignette);
        if (member == &editing::OpticsSettings::crop_factor)
            enabled = settings_.profile_enabled ||
                      (settings_.remove_ca && !settings_.profile_xml.isEmpty());
        spin->setEnabled(enabled);
        if (auto* slider = findChild<QSlider*>(spin->objectName() + "Slider"))
            slider->setEnabled(enabled);
    }
}
void RawOpticsPanel::refresh() {
    loading_ = true;
    enabled_->setChecked(settings_.enabled);
    profile_->setChecked(settings_.profile_enabled);
    ca_->setChecked(settings_.remove_ca);
    const auto selected = std::find_if(profiles_.begin(), profiles_.end(), [this](const auto& p) {
        return p.xml == settings_.profile_xml;
    });
    if (selected != profiles_.end()) {
        const QSignalBlocker maker_block(maker_), model_block(model_);
        maker_->setCurrentText(selected->maker);
        model_->clear();
        model_->addItem(tr("请选择配置"), -1);
        for (int i = 0; i < int(profiles_.size()); ++i)
            if (profiles_[std::size_t(i)].maker == selected->maker &&
                model_->findText(profiles_[std::size_t(i)].model) < 0)
                model_->addItem(profiles_[std::size_t(i)].model, i);
        model_->setCurrentText(selected->model);
        populate_profiles(int(selected - profiles_.begin()));
    }
    for (const auto& [spin, member] : numbers_)
        spin->setValue(settings_.*member);
    for (int i = 0; i < 2; ++i) {
        ranges_[i]->range = i == 0 ? settings_.purple_hue : settings_.green_hue;
        ranges_[i]->update();
    }
    if (!settings_.profile_name.isEmpty())
        status_->setText(tr("镜头配置：%1（已随配方保存）").arg(settings_.profile_name));
    loading_ = false;
    update_dependencies();
}
bool RawOpticsPanel::sampling() const {
    return picker_->isChecked();
}
bool RawOpticsPanel::eventFilter(QObject* object, QEvent* event) {
    int next = auxiliary_;
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->buttons().testFlag(Qt::LeftButton))
            next = mouse->modifiers().testFlag(Qt::AltModifier) ? object->property("opticsAuxiliary").toInt() : 0;
    } else if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::FocusOut ||
               event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide) next = 0;
    else if (event->type() == QEvent::KeyRelease && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt) next = 0;
    else if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) next = 0;
    if (next != auxiliary_) {
        auxiliary_ = next;
        if (view_changed) view_changed();
    }
    return QWidget::eventFilter(object, event);
}
void RawOpticsPanel::stop_sampling() {
    picker_->setChecked(false);
}
void RawOpticsPanel::sample(QColor color) {
    const auto hsl = editing::point_color_hsl({color.redF(), color.greenF(), color.blueF()});
    const double degrees = hsl[0] * 360;
    const bool purple = degrees >= 180;
    if (hsl[1] < .1 || hsl[2] < .01 || (!purple && degrees < 60)) {
        status_->setText(tr("请选择明显的紫色或绿色彩边，当前颜色过于中性或不在范围。"));
        return;
    }
    const double value = (degrees - (purple ? 180 : 60)) / (purple ? 1.8 : 1.2);
    auto& range = purple ? settings_.purple_hue : settings_.green_hue;
    const double low = std::clamp(value - 10, 0., 80.);
    range = {low, low + 20};
    (purple ? settings_.purple : settings_.green) = 10;
    stop_sampling();
    refresh();
    update_dependencies();
    if (changed)
        changed();
}
} // namespace huelith::desktop
