#include "color_band_slider.h"
#include "huelith/editing/raw_adjustments.h"
#include "raw_dialog.h"
#include "raw_geometry_panel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace huelith::desktop {
namespace {
std::array<QPointF, 8> handles(const QRectF& r) {
    return {r.topLeft(),     QPointF(r.center().x(), r.top()),
            r.topRight(),    QPointF(r.right(), r.center().y()),
            r.bottomRight(), QPointF(r.center().x(), r.bottom()),
            r.bottomLeft(),  QPointF(r.left(), r.center().y())};
}
QJsonObject crop_json(const QRectF& r, QSize size) {
    return {{"x", r.left() / size.width()},
            {"y", r.top() / size.height()},
            {"endX", r.right() / size.width()},
            {"endY", r.bottom() / size.height()}};
}
QRectF bounded(QRectF r, QSize size) {
    if (r.width() > size.width() || r.height() > size.height()) {
        const double factor = std::min(size.width() / r.width(), size.height() / r.height());
        const auto center = r.center();
        r.setSize(r.size() * factor);
        r.moveCenter(center);
    }
    r.moveLeft(std::clamp(r.left(), 0., size.width() - r.width()));
    r.moveTop(std::clamp(r.top(), 0., size.height() - r.height()));
    return r;
}
} // namespace
QWidget* RawDialog::setup_crop(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName("rawCropPanel");
    auto* form = new QFormLayout(panel);
    form->setContentsMargins(0, 0, 0, 0);
    crop_ratio_ = new QComboBox(panel);
    crop_ratio_->setObjectName("rawCropRatio");
    crop_ratio_->addItems(
        {tr("自由"), tr("原始"), "1 × 1", "4 × 5", "5 × 7", "2 × 3", "16 × 9", tr("自定")});
    auto* ratios = new QHBoxLayout;
    ratios->addWidget(crop_ratio_, 1);
    auto* swap = new QToolButton(panel);
    swap->setObjectName("rawCropSwap");
    swap->setText("⇄");
    swap->setToolTip(tr("旋转长宽比 (X)"));
    ratios->addWidget(swap);
    crop_lock_ = new QCheckBox(tr("锁定"), panel);
    crop_lock_->setObjectName("rawCropLock");
    ratios->addWidget(crop_lock_);
    form->addRow(tr("预设"), ratios);
    auto* custom = new QHBoxLayout;
    crop_width_ = new QDoubleSpinBox(panel);
    crop_height_ = new QDoubleSpinBox(panel);
    crop_width_->setObjectName("rawCropWidth");
    crop_height_->setObjectName("rawCropHeight");
    for (auto* number : {crop_width_, crop_height_}) {
        number->setRange(.01, 10000);
        number->setValue(1);
        number->setKeyboardTracking(false);
        custom->addWidget(number);
        connect(number, &QDoubleSpinBox::valueChanged, this, [this] {
            const QSignalBlocker blocker(crop_ratio_);
            crop_ratio_->setCurrentIndex(7);
            if (crop_lock_->isChecked())
                apply_crop_ratio();
        });
    }
    form->addRow(tr("长宽比"), custom);
    crop_angle_ = new QDoubleSpinBox(panel);
    crop_angle_->setObjectName("rawCropAngle");
    crop_angle_->setRange(-180, 180);
    crop_angle_->setDecimals(2);
    crop_angle_->setKeyboardTracking(false);
    form->addRow(tr("角度"), crop_angle_);
    auto* angle_slider = new ColorBandSlider(-1, 0, panel);
    angle_slider->setObjectName("rawCropAngleSlider");
    angle_slider->setRange(-18000, 18000);
    angle_slider->set_default(0);
    form->addRow(angle_slider);
    connect(angle_slider, &QSlider::valueChanged, crop_angle_,
            [this](int value) { crop_angle_->setValue(value / 100.); });
    connect(angle_slider, &QSlider::sliderPressed, this, [this] {
        history_timer_->stop();
        record_history();
        parameter_gesture_ = true;
    });
    connect(angle_slider, &QSlider::sliderReleased, this, [this] {
        parameter_gesture_ = false;
        history_timer_->stop();
        record_history();
    });
    connect(crop_angle_, &QDoubleSpinBox::valueChanged, this, [this, angle_slider](double value) {
        const QSignalBlocker blocker(angle_slider);
        angle_slider->setValue(qRound(value * 100));
        if (loading_ || !geometry_ || source_size_.isEmpty())
            return;
        auto settings = geometry_->settings();
        settings.rotation = value;
        geometry_->set_settings(settings);
        sources_[current_].raw_recipe.insert("geometry", editing::geometry_json(settings));
        display();
        if (!parameter_gesture_)
            history_timer_->start();
    });
    connect(crop_ratio_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (source_size_.isEmpty())
            return;
        const std::array<QSizeF, 8> presets{
            QSizeF(1, 1),  sources_[current_].raw_recipe.value("quarterTurns").toInt()%2?QSizeF(source_size_.height(),source_size_.width()):QSizeF(source_size_),
            QSizeF(1, 1),  QSizeF(4, 5),
            QSizeF(5, 7),  QSizeF(2, 3),
            QSizeF(16, 9), QSizeF(crop_width_->value(), crop_height_->value())};
        const QSignalBlocker width_block(crop_width_), height_block(crop_height_),
            lock_block(crop_lock_);
        crop_width_->setValue(presets[index].width());
        crop_height_->setValue(presets[index].height());
        crop_lock_->setChecked(index != 0);
        if (index != 0)
            apply_crop_ratio();
    });
    connect(swap, &QToolButton::clicked, this, [this] {
        const QSignalBlocker width_block(crop_width_), height_block(crop_height_);
        const double width = crop_width_->value();
        crop_width_->setValue(crop_height_->value());
        crop_height_->setValue(width);
        if (crop_lock_->isChecked())
            apply_crop_ratio();
    });
    connect(crop_lock_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && !source_size_.isEmpty()) {
            const auto r = editing::raw_crop_rect(source_size_, sources_[current_].raw_recipe);
            const QSignalBlocker width_block(crop_width_), height_block(crop_height_);
            const bool rotated=sources_[current_].raw_recipe.value("quarterTurns").toInt()%2;
            crop_width_->setValue(rotated?r.height():r.width());
            crop_height_->setValue(rotated?r.width():r.height());
        }
    });
    crop_straighten_ = new QToolButton(panel);
    crop_straighten_->setObjectName("rawCropStraighten");
    crop_straighten_->setText(tr("拉直 (A)"));
    crop_straighten_->setToolTip(tr("沿应当水平或垂直的边缘拖出参考线"));
    crop_straighten_->setCheckable(true);
    form->addRow(crop_straighten_);
    auto* rotation_buttons=new QHBoxLayout;
    for(int direction : {-1,1}){
        auto* rotate=new QPushButton(direction<0?tr("向左旋转 90°"):tr("向右旋转 90°"),panel);
        rotate->setObjectName(direction<0?"rawRotateLeft":"rawRotateRight");rotate->setAutoDefault(false);rotation_buttons->addWidget(rotate);
        connect(rotate,&QPushButton::clicked,this,[this,direction]{
            if(source_size_.isEmpty())return;
            history_timer_->stop();record_history();
            auto& recipe=sources_[current_].raw_recipe;
            recipe.insert("quarterTurns",(recipe.value("quarterTurns").toInt()+direction+4)%4);
            record_history();fit_next_=true;display();
        });
    }
    form->addRow(rotation_buttons);
    auto* reset = new QPushButton(tr("重置裁剪"), panel);
    reset->setObjectName("rawResetCrop");
    reset->setAutoDefault(false);
    connect(reset, &QPushButton::clicked, this, [this] {
        crop_dragging_ = false;
        sources_[current_].raw_recipe.remove("crop");
        sources_[current_].raw_recipe.remove("quarterTurns");
        auto geometry=geometry_->settings();geometry.rotation=0;geometry_->set_settings(geometry);
        sources_[current_].raw_recipe.insert("geometry",editing::geometry_json(geometry));
        {const QSignalBlocker angle(crop_angle_),slider(findChild<QSlider*>("rawCropAngleSlider"));crop_angle_->setValue(0);findChild<QSlider*>("rawCropAngleSlider")->setValue(0);}
        record_history();
        display();
    });
    form->addRow(reset);
    return panel;
}
void RawDialog::apply_crop_ratio() {
    if (source_size_.isEmpty() || loading_)
        return;
    auto r = QRectF(editing::raw_crop_rect(source_size_, sources_[current_].raw_recipe));
    const auto center = r.center();
    double ratio = crop_width_->value() / crop_height_->value();
    if(sources_[current_].raw_recipe.value("quarterTurns").toInt()%2)ratio=1/ratio;
    if (r.width() / r.height() > ratio)
        r.setWidth(r.height() * ratio);
    else
        r.setHeight(r.width() / ratio);
    r.moveCenter(center);
    sources_[current_].raw_recipe.insert("crop", crop_json(bounded(r, source_size_), source_size_));
    record_history();
    display();
}
void RawDialog::crop_overlays() {
    if (source_size_.isEmpty())
        return;
    const auto r =
        crop_dragging_
            ? crop_draft_
            : QRectF(editing::raw_crop_rect(source_size_, sources_[current_].raw_recipe));
    const double unit = 1 / std::max(.001, std::hypot(view_->transform().m11(),view_->transform().m12()));
    QPainterPath path;
    path.addRect(r);
    for (int i = 1; i < 3; ++i) {
        const double x = r.left() + r.width() * i / 3, y = r.top() + r.height() * i / 3;
        path.moveTo(x, r.top());
        path.lineTo(x, r.bottom());
        path.moveTo(r.left(), y);
        path.lineTo(r.right(), y);
    }
    for (const auto& p : handles(r))
        path.addRect(QRectF(p - QPointF(3 * unit, 3 * unit), QSizeF(6 * unit, 6 * unit)));
    if (!stroke_guide_)
        stroke_guide_ = scene_->addPath(path, QPen(Qt::white, unit));
    else
        stroke_guide_->setPath(path);
}
bool RawDialog::crop_event(QMouseEvent* mouse) {
    auto point = view_->mapToScene(mouse->position().toPoint());
    const auto unbounded=point;
    point.setX(std::clamp(point.x(), 0., double(source_size_.width())));
    point.setY(std::clamp(point.y(), 0., double(source_size_.height())));
    if (mouse->type() == QEvent::MouseButtonPress) {
        history_timer_->stop();
        record_history();
        crop_start_ = point;
        crop_before_ = editing::raw_crop_rect(source_size_, sources_[current_].raw_recipe);
        crop_draft_ = crop_before_;
        crop_handle_ = -1;
        crop_rotation_start_=geometry_->settings().rotation;crop_rotation_delta_=0;
        if(preview_item_)crop_preview_transform_=preview_item_->transform();
        if (!crop_straighten_->isChecked()) {
            const auto positions = handles(crop_before_);
            double closest = 10 / std::max(.001, std::hypot(view_->transform().m11(),view_->transform().m12()));
            for (int i = 0; i < 8; ++i) {
                const double distance = QLineF(positions[i], point).length();
                if (distance < closest) {
                    closest = distance;
                    crop_handle_ = i;
                }
            }
            if (crop_handle_ < 0 && sources_[current_].raw_recipe.contains("crop") &&
                crop_before_.contains(point))
                crop_handle_ = 8;
            if(crop_handle_<0 && sources_[current_].raw_recipe.contains("crop") && !crop_before_.contains(unbounded)){
                crop_handle_=9;crop_start_=unbounded;
            }
        }
        crop_dragging_ = true;
        return true;
    }
    if (!crop_dragging_) {
        const auto r = QRectF(editing::raw_crop_rect(source_size_, sources_[current_].raw_recipe));
        const auto positions = handles(r);
        const double tolerance = 10 / std::max(.001, std::hypot(view_->transform().m11(),view_->transform().m12()));
        Qt::CursorShape cursor = Qt::CrossCursor;
        if(sources_[current_].raw_recipe.contains("crop") && !r.contains(unbounded))cursor=Qt::SizeAllCursor;
        if (!crop_straighten_->isChecked()) {
            if (sources_[current_].raw_recipe.contains("crop") && r.contains(point))
                cursor = Qt::SizeAllCursor;
            for (int i = 0; i < 8; ++i) {
                if (QLineF(positions[i], point).length() > tolerance)
                    continue;
                cursor = i % 2 ? (i == 1 || i == 5 ? Qt::SizeVerCursor : Qt::SizeHorCursor)
                               : (i == 0 || i == 4 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
                break;
            }
        }
        view_->viewport()->setCursor(cursor);
        return false;
    }
    if (crop_straighten_->isChecked()) {
        QPainterPath line;
        line.moveTo(crop_start_);
        line.lineTo(point);
        if (stroke_guide_)
            stroke_guide_->setPath(line);
    } else if(crop_handle_==9){
        const auto center=crop_before_.center(),a=crop_start_-center,b=unbounded-center;
        crop_rotation_delta_=std::remainder((std::atan2(b.y(),b.x())-std::atan2(a.y(),a.x()))*180/std::numbers::pi,360.);
        double angle=std::remainder(crop_rotation_start_+crop_rotation_delta_,360.);
        if(mouse->modifiers().testFlag(Qt::ShiftModifier))angle=std::round(angle/15)*15;
        crop_rotation_delta_=angle-crop_rotation_start_;
        {const QSignalBlocker block(crop_angle_);crop_angle_->setValue(angle);}
        if(preview_item_){QTransform rotate;rotate.translate(source_size_.width()/2.,source_size_.height()/2.);rotate.rotate(crop_rotation_delta_);rotate.translate(-source_size_.width()/2.,-source_size_.height()/2.);preview_item_->setTransform(crop_preview_transform_*rotate);}
    } else if (crop_handle_ == 8) {
        crop_draft_ = bounded(crop_before_.translated(point - crop_start_), source_size_);
        crop_overlays();
    } else {
        QPointF anchor = crop_start_;
        if (crop_handle_ >= 0)
            anchor = handles(crop_before_)[(crop_handle_ + 4) % 8];
        const bool centered = crop_handle_ >= 0 && mouse->modifiers().testFlag(Qt::AltModifier);
        if (centered)
            anchor = crop_before_.center();
        auto delta = point - anchor;
        const bool side = crop_handle_ >= 0 && crop_handle_ % 2 == 1;
        if (side) {
            if (crop_handle_ == 1 || crop_handle_ == 5)
                delta.setX(crop_before_.width() / 2);
            else
                delta.setY(crop_before_.height() / 2);
        }
        const bool constrained =
            crop_lock_->isChecked() != mouse->modifiers().testFlag(Qt::ShiftModifier);
        if (constrained) {
            double ratio = crop_lock_->isChecked()
                                     ? crop_width_->value() / crop_height_->value()
                                     : crop_before_.width() / crop_before_.height();
            if(crop_lock_->isChecked() && sources_[current_].raw_recipe.value("quarterTurns").toInt()%2)ratio=1/ratio;
            if (side && (crop_handle_ == 1 || crop_handle_ == 5))
                delta.setX(std::abs(delta.y()) * ratio / (centered ? 1 : 2));
            else if (side)
                delta.setY(std::abs(delta.x()) / ratio / (centered ? 1 : 2));
            else if (std::abs(delta.x()) > std::abs(delta.y()) * ratio)
                delta.setY(std::copysign(std::abs(delta.x()) / ratio, delta.y()));
            else
                delta.setX(std::copysign(std::abs(delta.y()) * ratio, delta.x()));
        }
        // Keep the opposite handle (or Alt center) fixed at the image boundary.
        // Translating a fitted rectangle here would make the supposedly fixed
        // anchor drift when a constrained corner reaches an edge.
        if (constrained || centered) {
            const bool centered_x = centered || (side && (crop_handle_ == 1 || crop_handle_ == 5));
            const bool centered_y = centered || (side && (crop_handle_ == 3 || crop_handle_ == 7));
            const double available_x =
                centered_x ? std::min(anchor.x(), source_size_.width() - anchor.x())
                           : (delta.x() < 0 ? anchor.x() : source_size_.width() - anchor.x());
            const double available_y =
                centered_y ? std::min(anchor.y(), source_size_.height() - anchor.y())
                           : (delta.y() < 0 ? anchor.y() : source_size_.height() - anchor.y());
            const double factor =
                std::min({1., std::abs(delta.x()) > 0 ? available_x / std::abs(delta.x()) : 1.,
                          std::abs(delta.y()) > 0 ? available_y / std::abs(delta.y()) : 1.});
            delta *= factor;
        }
        crop_draft_ = QRectF(anchor, anchor + delta).normalized();
        if (centered) {
            crop_draft_ = QRectF(anchor - QPointF(std::abs(delta.x()), std::abs(delta.y())),
                                 anchor + QPointF(std::abs(delta.x()), std::abs(delta.y())));
        } else if (side) {
            if (crop_handle_ == 1 || crop_handle_ == 5)
                crop_draft_.setLeft(anchor.x() - std::abs(delta.x()));
            else
                crop_draft_.setTop(anchor.y() - std::abs(delta.y()));
        }
        crop_draft_ = bounded(crop_draft_, source_size_);
        crop_overlays();
    }
    if (mouse->type() == QEvent::MouseButtonRelease) {
        crop_dragging_ = false;
        if(crop_handle_==9){
            auto settings=geometry_->settings();settings.rotation=std::remainder(crop_rotation_start_+crop_rotation_delta_,360.);
            geometry_->set_settings(settings);sources_[current_].raw_recipe.insert("geometry",editing::geometry_json(settings));
            const QSignalBlocker block(findChild<QSlider*>("rawCropAngleSlider"));findChild<QSlider*>("rawCropAngleSlider")->setValue(qRound(settings.rotation*100));
        } else if (crop_straighten_->isChecked()) {
            const auto delta = point - crop_start_;
            if (QLineF(point, crop_start_).length() > 2) {
                double angle = std::atan2(delta.y(), delta.x()) * 180 / std::numbers::pi;
                angle = std::remainder(angle, 90.);
                auto settings = geometry_->settings();
                settings.rotation = std::clamp(settings.rotation - angle, -180., 180.);
                geometry_->set_settings(settings);
                const QSignalBlocker blocker(crop_angle_);
                crop_angle_->setValue(settings.rotation);
                const QSignalBlocker slider_block(findChild<QSlider*>("rawCropAngleSlider"));
                findChild<QSlider*>("rawCropAngleSlider")
                    ->setValue(qRound(settings.rotation * 100));
                sources_[current_].raw_recipe.insert("geometry", editing::geometry_json(settings));
            }
            crop_straighten_->setChecked(false);
        } else if (crop_draft_.width() >= 1 && crop_draft_.height() >= 1)
            sources_[current_].raw_recipe.insert("crop", crop_json(crop_draft_, source_size_));
        record_history();
        display();
    }
    return true;
}
} // namespace huelith::desktop
