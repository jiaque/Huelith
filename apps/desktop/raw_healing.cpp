#include "huelith/editing/optics.h"
#include "huelith/editing/raw_geometry.h"
#include "raw_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
QWidget* RawDialog::setup_healing(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName("rawHealingPanel");
    auto* form = new QFormLayout(panel);
    form->setContentsMargins(0, 0, 0, 0);
    healing_type_ = new QComboBox(panel);
    healing_type_->setObjectName("rawHealingType");
    healing_type_->addItems({tr("修复"), tr("仿制")});
    form->addRow(tr("模式"), healing_type_);
    const auto spin = [&](const QString& label, const char* name, double minimum, double maximum,
                          double value) {
        auto* control = new QDoubleSpinBox(panel);
        control->setObjectName(name);
        control->setRange(minimum, maximum);
        control->setValue(value);
        auto* row = new QWidget(panel);
        auto* layout = new QVBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* title = new QHBoxLayout;
        title->addWidget(new QLabel(label, row));
        title->addStretch();
        title->addWidget(control);
        layout->addLayout(title);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setObjectName(QString::fromLatin1(name) + "Slider");
        slider->setRange(int(minimum), int(maximum));
        slider->setValue(int(value));
        layout->addWidget(slider);
        form->addRow(row);
        connect(slider, &QSlider::valueChanged, control, &QDoubleSpinBox::setValue);
        connect(control, &QDoubleSpinBox::valueChanged, slider, [slider](double v) {
            const QSignalBlocker block(slider);
            slider->setValue(qRound(v));
        });
        connect(control, &QDoubleSpinBox::valueChanged, this, &RawDialog::update_healing);
        return control;
    };
    healing_size_ = spin(tr("大小（直径，像素）"), "rawHealingSize", 2, 2000, 100);
    healing_feather_ = spin(tr("羽化（%）"), "rawHealingFeather", 0, 100, 50);
    healing_opacity_ = spin(tr("不透明度（%）"), "rawHealingOpacity", 0, 100, 100);
    healing_list_ = new QListWidget(panel);
    healing_list_->setObjectName("rawHealingList");
    healing_list_->setMaximumHeight(180);
    form->addRow(healing_list_);
    connect(healing_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        const auto strokes = sources_[current_].raw_recipe.value("healing").toArray();
        if (row < 0 || row >= strokes.size())
            return;
        const auto s = editing::healing_from_json(strokes[row].toObject());
        const QSignalBlocker a(healing_type_), b(healing_size_), c(healing_feather_),
            d(healing_opacity_);
        healing_type_->setCurrentIndex(s.clone ? 1 : 0);
        healing_size_->setValue(s.radius * 2 *
                                std::min(source_size_.width(), source_size_.height()));
        healing_feather_->setValue(s.feather * 100);
        healing_opacity_->setValue(s.opacity * 100);
        for (auto* control : {healing_size_, healing_feather_, healing_opacity_}) {
            auto* slider = findChild<QSlider*>(control->objectName() + "Slider");
            const QSignalBlocker block(slider);
            slider->setValue(qRound(control->value()));
        }
        display();
    });
    connect(healing_type_, &QComboBox::currentIndexChanged, this, &RawDialog::update_healing);
    auto* remove = new QPushButton(tr("删除所选修复"), panel);
    remove->setObjectName("rawHealingDelete");
    remove->setAutoDefault(false);
    form->addRow(remove);
    connect(remove, &QPushButton::clicked, this, &RawDialog::delete_healing);
    auto* refresh = new QPushButton(tr("重新取样（/）"), panel);
    refresh->setObjectName("rawHealingRefresh");
    refresh->setAutoDefault(false);
    form->addRow(refresh);
    connect(refresh, &QPushButton::clicked, this, &RawDialog::resample_healing);
    healing_visualize_ = new QCheckBox(tr("显示污点（Y）"), panel);
    healing_visualize_->setObjectName("rawHealingVisualize");
    form->addRow(healing_visualize_);
    healing_threshold_ = new QDoubleSpinBox(panel);
    healing_threshold_->setObjectName("rawHealingThreshold");
    healing_threshold_->setRange(0, 100);
    healing_threshold_->setValue(50);
    form->addRow(tr("可视化阈值"), healing_threshold_);
    connect(healing_visualize_, &QCheckBox::toggled, this, &RawDialog::display);
    connect(healing_threshold_, &QDoubleSpinBox::valueChanged, this, &RawDialog::display);
    auto* hint = new QLabel(tr("拖动绘制；Ctrl 拖动指定来源，Ctrl+Alt 拖动调整大小。Alt "
                               "单击修复点或框选删除；Shift 单击延长笔画。"),
                            panel);
    hint->setWordWrap(true);
    form->addRow(hint);
    return panel;
}
void RawDialog::refresh_healing() {
    if (!healing_list_)
        return;
    const int selected = healing_list_->currentRow();
    const QSignalBlocker blocker(healing_list_);
    healing_list_->clear();
    const auto strokes = sources_[current_].raw_recipe.value("healing").toArray();
    for (int i = 0; i < strokes.size(); ++i)
        healing_list_->addItem(
            tr("%1 %2")
                .arg(strokes[i].toObject().value("clone").toBool() ? tr("仿制") : tr("修复"))
                .arg(i + 1));
    healing_list_->setCurrentRow(std::min(selected, int(strokes.size()) - 1));
}
void RawDialog::update_healing() {
    if (loading_ || !healing_list_ || source_size_.isEmpty())
        return;
    auto& recipe = sources_[current_].raw_recipe;
    auto strokes = recipe.value("healing").toArray();
    const int i = healing_list_->currentRow();
    if (i < 0 || i >= strokes.size())
        return;
    auto s = editing::healing_from_json(strokes[i].toObject());
    s.clone = healing_type_->currentIndex() == 1;
    s.radius = std::min(.5, healing_size_->value() / 2 /
                                std::min(source_size_.width(), source_size_.height()));
    s.feather = healing_feather_->value() / 100;
    s.opacity = healing_opacity_->value() / 100;
    strokes[i] = editing::healing_json(s);
    recipe.insert("healing", strokes);
    refresh_healing();
    history_timer_->start();
    display();
}
void RawDialog::delete_healing() {
    auto& recipe = sources_[current_].raw_recipe;
    auto strokes = recipe.value("healing").toArray();
    const int i = healing_list_->currentRow();
    if (i < 0 || i >= strokes.size())
        return;
    strokes.removeAt(i);
    recipe.insert("healing", strokes);
    refresh_healing();
    record_history();
    display();
}
void RawDialog::resample_healing() {
    if (preview_source_.isNull())
        return;
    auto& recipe = sources_[current_].raw_recipe;
    auto strokes = recipe.value("healing").toArray();
    const int selected = healing_list_->currentRow();
    if (selected < 0 || selected >= strokes.size())
        return;
    try {
        auto s = editing::healing_from_json(strokes[selected].toObject());
        auto sample =
            preview_source_.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        sample = editing::apply_optics(
            sample, editing::optics_from_json(recipe.value("optics").toObject()));
        s.offset = editing::automatic_healing_offset(sample, s, ++healing_alternative_);
        strokes[selected] = editing::healing_json(s);
        recipe.insert("healing", strokes);
        record_history();
        display();
    } catch (const std::exception& error) {
        message_->setText(QString::fromUtf8(error.what()));
    }
}
QImage RawDialog::healing_visualization(const QImage& image) const {
    if (!healing_visualize_->isChecked() || edit_mode_->currentIndex() != 6)
        return image;
    auto gray = image.convertToFormat(QImage::Format_Grayscale8);
    auto result = gray;
    const double threshold = (100 - healing_threshold_->value()) * .8;
    for (int y = 0; y < gray.height(); ++y) {
        auto* row = result.scanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            const int left = gray.constScanLine(y)[std::max(0, x - 1)];
            const int right = gray.constScanLine(y)[std::min(gray.width() - 1, x + 1)];
            const int above = gray.constScanLine(std::max(0, y - 1))[x];
            const int below = gray.constScanLine(std::min(gray.height() - 1, y + 1))[x];
            const double edge = std::hypot(right - left, below - above);
            row[x] = uchar(std::clamp((edge - threshold) * 16 + 128, 0., 255.));
        }
    }
    return result;
}
bool RawDialog::healing_event(QMouseEvent* event) {
    const auto& recipe = sources_[current_].raw_recipe;
    const auto transform = editing::geometry_transform(
        source_size_, editing::geometry_from_json(recipe.value("geometry").toObject()));
    const auto scene_point = view_->mapToScene(event->position().toPoint());
    const auto pixel = transform.inverted().map(scene_point);
    const QPointF point(std::clamp(pixel.x() / source_size_.width(), 0., 1.),
                        std::clamp(pixel.y() / source_size_.height(), 0., 1.));
    auto strokes = recipe.value("healing").toArray();
    const auto scene_position = [&](QPointF p) {
        return transform.map(QPointF(p.x() * source_size_.width(), p.y() * source_size_.height()));
    };
    if (event->type() == QEvent::MouseButtonPress) {
        history_timer_->stop();
        record_history();
        const int selected = healing_list_->currentRow();
        healing_start_ = scene_point;
        const bool alt = event->modifiers().testFlag(Qt::AltModifier);
        const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
        if (alt && !ctrl) {
            for (int i = int(strokes.size()) - 1; i >= 0; --i) {
                const auto s = editing::healing_from_json(strokes[i].toObject());
                if (QLineF(scene_point, scene_position(s.points.front())).length() *
                            std::hypot(view_->transform().m11(),view_->transform().m12()) <
                        9 ||
                    QLineF(scene_point, scene_position(s.points.front() + s.offset)).length() *
                            std::hypot(view_->transform().m11(),view_->transform().m12()) <
                        9) {
                    healing_list_->setCurrentRow(i);
                    delete_healing();
                    return true;
                }
            }
            healing_delete_area_ = true;
            stroke_guide_ = scene_->addPath({}, QPen(Qt::red, 1 / std::hypot(view_->transform().m11(),view_->transform().m12())));
            return true;
        }
        if (!ctrl && event->modifiers().testFlag(Qt::ShiftModifier) && selected >= 0 &&
            selected < strokes.size()) {
            auto s = editing::healing_from_json(strokes[selected].toObject());
            if (s.points.size() < 16384)
                s.points.push_back(point);
            strokes[selected] = editing::healing_json(s);
            sources_[current_].raw_recipe.insert("healing", strokes);
            record_history();
            display();
            return true;
        }
        if (!ctrl && selected >= 0 && selected < strokes.size()) {
            auto s = editing::healing_from_json(strokes[selected].toObject());
            if (QLineF(scene_point, scene_position(s.points.front() + s.offset)).length() *
                    std::hypot(view_->transform().m11(),view_->transform().m12()) <
                12) {
                healing_stroke_ = s;
                healing_drag_source_ = true;
                return true;
            }
        }
        for (int i = ctrl ? -1 : int(strokes.size()) - 1; i >= 0; --i) {
            const auto s = editing::healing_from_json(strokes[i].toObject());
            if (QLineF(scene_point, scene_position(s.points.front())).length() *
                    std::hypot(view_->transform().m11(),view_->transform().m12()) <
                9) {
                healing_list_->setCurrentRow(i);
                return true;
            }
        }
        if (strokes.size() >= 256) {
            message_->setText(tr("最多支持 256 个修复笔画"));
            return true;
        }
        healing_stroke_ = {};
        healing_stroke_.clone = healing_type_->currentIndex() == 1;
        healing_stroke_.radius = std::min(
            .5, healing_size_->value() / 2 / std::min(source_size_.width(), source_size_.height()));
        healing_stroke_.feather = healing_feather_->value() / 100;
        healing_stroke_.opacity = healing_opacity_->value() / 100;
        healing_stroke_.points.push_back(point);
        healing_drawing_ = true;
        healing_new_source_ = ctrl && !alt;
        healing_drag_size_ = ctrl && alt;
        QPainterPath guide;
        guide.moveTo(scene_point);
        stroke_guide_ = scene_->addPath(guide, QPen(QColor("#63baff"), healing_size_->value(),
                                                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        stroke_guide_->setOpacity(.35);
        return true;
    }
    if (healing_delete_area_) {
        QPainterPath guide;
        const auto rectangle = QRectF(healing_start_, scene_point).normalized();
        guide.addRect(rectangle);
        stroke_guide_->setPath(guide);
        if (event->type() == QEvent::MouseButtonRelease) {
            for (int i = int(strokes.size()) - 1; i >= 0; --i) {
                const auto s = editing::healing_from_json(strokes[i].toObject());
                if (rectangle.contains(scene_position(s.points.front())) ||
                    rectangle.contains(scene_position(s.points.front() + s.offset)))
                    strokes.removeAt(i);
            }
            sources_[current_].raw_recipe.insert("healing", strokes);
            healing_delete_area_ = false;
            refresh_healing();
            record_history();
            display();
        }
        return true;
    }
    if (!healing_drawing_ && !healing_drag_source_)
        return false;
    if (healing_drag_source_ || healing_new_source_)
        healing_stroke_.offset = point - healing_stroke_.points.front();
    else if (healing_drag_size_) {
        const double radius = QLineF(pixel, transform.inverted().map(healing_start_)).length();
        healing_stroke_.radius =
            std::clamp(radius / std::min(source_size_.width(), source_size_.height()), .0005, .5);
    } else if (healing_stroke_.points.size() < 16384 &&
               QLineF(point, healing_stroke_.points.back()).length() > .0002)
        healing_stroke_.points.push_back(point);
    if (stroke_guide_) {
        auto path = stroke_guide_->path();
        path.lineTo(scene_point);
        stroke_guide_->setPath(path);
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        try {
            if (healing_drawing_) {
                if (!healing_new_source_) {
                    auto sample = preview_source_.scaled(320, 320, Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation);
                    sample = editing::apply_optics(
                        sample, editing::optics_from_json(recipe.value("optics").toObject()));
                    healing_stroke_.offset =
                        editing::automatic_healing_offset(sample, healing_stroke_);
                }
                strokes.append(editing::healing_json(healing_stroke_));
            } else
                strokes[healing_list_->currentRow()] = editing::healing_json(healing_stroke_);
            sources_[current_].raw_recipe.insert("healing", strokes);
            refresh_healing();
            if (healing_drawing_)
                healing_list_->setCurrentRow(int(strokes.size()) - 1);
            record_history();
        } catch (const std::exception& error) {
            message_->setText(QString::fromUtf8(error.what()));
        }
        healing_drawing_ = healing_drag_source_ = false;
        healing_drag_size_ = healing_new_source_ = false;
        display();
    }
    return true;
}
void RawDialog::healing_overlays() {
    if (edit_mode_->currentIndex() != 6)
        return;
    const auto& recipe = sources_[current_].raw_recipe;
    const auto transform = editing::geometry_transform(
        source_size_, editing::geometry_from_json(recipe.value("geometry").toObject()));
    const auto strokes = recipe.value("healing").toArray();
    const double radius = 5 / std::hypot(view_->transform().m11(),view_->transform().m12());
    for (int i = 0; i < strokes.size(); ++i) {
        const auto s = editing::healing_from_json(strokes[i].toObject());
        const auto position = [&](QPointF p) {
            return transform.map(
                QPointF(p.x() * source_size_.width(), p.y() * source_size_.height()));
        };
        const auto target = position(s.points.front()),
                   source = position(s.points.front() + s.offset);
        QPen pen(i == healing_list_->currentRow() ? QColor("#63baff") : Qt::white);
        pen.setCosmetic(true);
        scene_->addEllipse(QRectF(target - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)),
                           pen);
        if (i == healing_list_->currentRow()) {
            scene_->addLine(QLineF(target, source), pen);
            scene_->addEllipse(
                QRectF(source - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)), pen);
        }
    }
}
} // namespace huelith::desktop
