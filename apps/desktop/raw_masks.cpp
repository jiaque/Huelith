#include "color_band_slider.h"
#include "huelith/editing/raw_geometry.h"
#include "raw_dialog.h"
#include "raw_optics_panel.h"
#include "raw_white_balance_panel.h"
#include "tone_panel.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace huelith::desktop {
namespace {
QString component_label(const QJsonObject& c) {
    if (!c.value("name").toString().trimmed().isEmpty())
        return c.value("name").toString();
    const auto mode = c.value("mode").toString();
    return mode == "brush"       ? QObject::tr("画笔")
           : mode == "linear"    ? QObject::tr("线性渐变")
           : mode == "radial"    ? QObject::tr("径向渐变")
           : mode == "color"     ? QObject::tr("颜色范围")
           : mode == "luminance" ? QObject::tr("明度范围")
                                 : QObject::tr("识别范围");
}
} // namespace
QWidget* RawDialog::setup_mask_components(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName("rawMaskComponents");
    auto* box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    mask_tree_ = new QTreeWidget(panel);
    mask_tree_->setObjectName("rawMaskTree");
    mask_tree_->setHeaderHidden(true);
    mask_tree_->setMinimumHeight(140);
    mask_tree_->setMaximumHeight(230);
    box->addWidget(mask_tree_);
    auto* row = new QHBoxLayout;
    box->addLayout(row);
    const auto action = [&](const QString& label, const char* id, const auto& fn) {
        auto* b = new QPushButton(label, panel);
        b->setObjectName(QLatin1String(id));
        b->setAutoDefault(false);
        row->addWidget(b);
        connect(b, &QPushButton::clicked, this, fn);
    };
    action(tr("复制"), "rawMaskDuplicate", [this] {
        auto locals = sources_[current_].raw_recipe.value("locals").toArray();
        const int index = adjustment_->currentIndex() - 1;
        if (index < 0)
            return;
        history_timer_->stop();
        record_history();
        auto local = locals[index].toObject();
        if (mask_component_ < 0) {
            if (locals.size() >= 64)
                return;
            local.insert("name",
                         local.value("name").toString(tr("局部 %1").arg(index + 1)) + tr(" 副本"));
            locals.append(local);
            sources_[current_].raw_recipe.insert("locals", locals);
            refresh_local_controls();
            adjustment_->setCurrentIndex(locals.size());
        } else {
            auto items = local.value("components").toArray();
            if (items.size() >= 4096)
                return;
            auto copy = items[mask_component_].toObject();
            copy.insert("name", component_label(copy) + tr(" 副本"));
            items.append(copy);
            local.insert("components", items);
            locals[index] = local;
            sources_[current_].raw_recipe.insert("locals", locals);
            mask_component_ = items.size() - 1;
        }
        record_history();
        refresh_mask_components();
        display();
    });
    action(tr("删除组件"), "rawMaskDeleteComponent", [this] {
        auto locals = sources_[current_].raw_recipe.value("locals").toArray();
        const int index = adjustment_->currentIndex() - 1;
        if (index < 0 || mask_component_ < 0)
            return;
        history_timer_->stop();
        record_history();
        auto local = locals[index].toObject();
        auto items = local.value("components").toArray();
        items.removeAt(mask_component_);
        local.insert("components", items);
        locals[index] = local;
        sources_[current_].raw_recipe.insert("locals", locals);
        mask_component_ = std::min(mask_component_, int(items.size()) - 1);
        record_history();
        refresh_mask_components();
        display();
    });
    action(tr("反相"), "rawMaskInvert", [this] {
        auto locals = sources_[current_].raw_recipe.value("locals").toArray();
        const int index = adjustment_->currentIndex() - 1;
        if (index < 0)
            return;
        auto local = locals[index].toObject();
        if (mask_component_ >= 0) {
            auto component = local.value("components").toArray()[mask_component_].toObject();
            component.insert("invert", !component.value("invert").toBool());
            update_mask_component(component);
        } else {
            history_timer_->stop();
            record_history();
            local.insert("invert", !local.value("invert").toBool());
            locals[index] = local;
            sources_[current_].raw_recipe.insert("locals", locals);
            record_history();
            display();
        }
    });
    auto* form = new QFormLayout;
    auto* refine = new QPushButton(tr("细化蒙版…"), panel);
    refine->setObjectName("rawMaskRefine");
    refine->setAutoDefault(false);
    box->addWidget(refine);
    connect(refine, &QPushButton::clicked, this, [this] { refine_local_mask(); });
    box->addLayout(form);
    auto* operation = new QComboBox(panel);
    operation->setObjectName("rawMaskOperation");
    operation->addItems({tr("添加"), tr("减去"), tr("相交")});
    form->addRow(tr("组合"), operation);
    const auto current = [this] {
        const auto locals = sources_[current_].raw_recipe.value("locals").toArray();
        const int index = adjustment_->currentIndex() - 1;
        if (index < 0 || mask_component_ < 0)
            return QJsonObject{};
        const auto components = locals[index].toObject().value("components").toArray();
        return mask_component_ < components.size() ? components[mask_component_].toObject()
                                                   : QJsonObject{};
    };
    connect(operation, &QComboBox::currentIndexChanged, this, [this, current](int value) {
        if (mask_controls_loading_)
            return;
        auto component = current();
        if (component.isEmpty())
            return;
        component.insert("operation", value == 0 ? "add" : value == 1 ? "subtract" : "intersect");
        component.remove("subtract");
        update_mask_component(component);
    });
    for (const auto& field :
         {std::pair{"flow", tr("流量")}, std::pair{"density", tr("密度")},
          std::pair{"low", tr("明度下限")}, std::pair{"high", tr("明度上限")},
          std::pair{"smooth", tr("平滑")}, std::pair{"range", tr("颜色范围")}}) {
        auto* holder = new QWidget(panel);
        holder->setObjectName(QString("rawMaskField_%1").arg(field.first));
        auto* field_box = new QVBoxLayout(holder);
        field_box->setContentsMargins(0, 0, 0, 0);
        field_box->setSpacing(2);
        auto* heading = new QHBoxLayout;
        heading->addWidget(new QLabel(field.second, holder));
        heading->addStretch();
        auto* spin = new QDoubleSpinBox(holder);
        spin->setMaximumWidth(80);
        spin->setObjectName(QString("rawMask_%1").arg(field.first));
        spin->setRange(0, 100);
        spin->setDecimals(0);
        spin->setValue(QString(field.first) == "flow" || QString(field.first) == "density" ||
                               QString(field.first) == "high"
                           ? 100
                       : QString(field.first) == "range"  ? 50
                       : QString(field.first) == "smooth" ? 10
                                                          : 0);
        heading->addWidget(spin);
        field_box->addLayout(heading);
        auto* slider = new ColorBandSlider(-1, 0, holder);
        slider->setObjectName(QString("rawMaskSlider_%1").arg(field.first));
        slider->setRange(0, 100);
        slider->set_default(int(spin->value()));
        slider->setValue(int(spin->value()));
        if (QString(field.first) == "low" || QString(field.first) == "high")
            slider->set_colors(Qt::black, Qt::white);
        field_box->addWidget(slider);
        form->addRow(holder);
        connect(slider, &QSlider::valueChanged, spin, &QDoubleSpinBox::setValue);
        connect(spin, &QDoubleSpinBox::valueChanged, slider, [slider](double value) {
            const QSignalBlocker blocker(slider);
            slider->setValue(qRound(value));
        });
        connect(slider, &QSlider::sliderPressed, this, [this] {
            history_timer_->stop();
            record_history();
            parameter_gesture_ = true;
        });
        connect(slider, &QSlider::sliderReleased, this, [this] {
            parameter_gesture_ = false;
            history_timer_->stop();
            record_history();
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, current, key = QString(field.first)](double value) {
                    if (mask_controls_loading_)
                        return;
                    auto component = current();
                    if (component.isEmpty())
                        return;
                    const double normalized = key == "range" ? value : value / 100;
                    if (key == "low" && normalized > component.value("high").toDouble(1))
                        component.insert("high", normalized);
                    if (key == "high" && normalized < component.value("low").toDouble())
                        component.insert("low", normalized);
                    component.insert(key, normalized);
                    update_mask_component(component);
                    if (key == "low" || key == "high") {
                        const QString other = key == "low" ? "high" : "low";
                        auto* counterpart = findChild<QDoubleSpinBox*>("rawMask_" + other);
                        const QSignalBlocker blocker(counterpart);
                        counterpart->setValue(component.value(other).toDouble() * 100);
                    }
                });
    }
    connect(local_radius_, &QDoubleSpinBox::valueChanged, this, [this, current](double value) {
        if (mask_controls_loading_ || source_size_.isEmpty())
            return;
        auto component = current();
        if (component.value("mode").toString() != "brush")
            return;
        component.insert("radius",
                         value / 2 / std::min(source_size_.width(), source_size_.height()));
        update_mask_component(component);
    });
    connect(local_feather_, &QDoubleSpinBox::valueChanged, this, [this, current](double value) {
        if (mask_controls_loading_)
            return;
        auto component = current();
        const auto mode = component.value("mode").toString();
        if (mode != "brush" && mode != "radial")
            return;
        component.insert("feather", value / 100);
        update_mask_component(component);
    });
    auto* ranges = new QHBoxLayout;
    box->addLayout(ranges);
    for (bool color : {false, true}) {
        auto* button = new QPushButton(color ? tr("颜色范围") : tr("明度范围"), panel);
        button->setObjectName(color ? "rawAddColorRange" : "rawAddLuminanceRange");
        button->setAutoDefault(false);
        ranges->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, color] {
            auto locals = sources_[current_].raw_recipe.value("locals").toArray();
            const int index = adjustment_->currentIndex() - 1;
            if (index < 0)
                return;
            history_timer_->stop();
            record_history();
            auto local = locals[index].toObject();
            auto components = local.value("components").toArray();
            if (components.size() >= 4096)
                return;
            QJsonObject component{{"mode", color ? "color" : "luminance"},
                                  {"operation", components.isEmpty() ? "add" : "intersect"},
                                  {"low", 0},
                                  {"high", 1},
                                  {"smooth", .1},
                                  {"range", 50},
                                  {"colors", QJsonArray{}}};
            components.append(component);
            local.insert("components", components);
            locals[index] = local;
            sources_[current_].raw_recipe.insert("locals", locals);
            mask_component_ = components.size() - 1;
            record_history();
            refresh_mask_components();
            findChild<QPushButton*>("rawMaskColorPicker")->setChecked(true);
            display();
        });
    }
    auto* picker = new QPushButton(tr("取样颜色（Shift 添加）"), panel);
    picker->setObjectName("rawMaskColorPicker");
    picker->setCheckable(true);
    picker->setAutoDefault(false);
    box->addWidget(picker);
    auto* swatches = new QWidget(panel);
    swatches->setObjectName("rawMaskColorSamples");
    auto* sample_row = new QHBoxLayout(swatches);
    sample_row->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 5; ++i) {
        auto* sample = new QToolButton(swatches);
        sample->setObjectName(QString("rawMaskColorSample%1").arg(i));
        sample->setProperty("maskSampleIndex", i);
        sample->setToolTip(tr("已取样颜色；Alt 单击移除"));
        sample->setFixedSize(26, 26);
        sample->installEventFilter(this);
        sample_row->addWidget(sample);
    }
    sample_row->addStretch();
    box->addWidget(swatches);
    auto* overlay_mode = new QComboBox(panel);
    overlay_mode->setObjectName("rawMaskOverlayMode");
    overlay_mode->addItems(
        {tr("颜色覆盖"), tr("黑白蒙版"), tr("图像置于黑色"), tr("图像置于白色")});
    box->addWidget(overlay_mode);
    connect(overlay_mode, &QComboBox::currentIndexChanged, this, [this] {
        overlay_->setChecked(true);
        display();
    });
    connect(picker, &QPushButton::toggled, this, [this](bool active) {
        mask_sampling_ = active;
        if (!active)
            mask_range_drawing_ = false;
        if (active) {
            tone_->set_target_mode(-1);
            tone_->stop_point_sampling();
            white_balance_->stop_sampling();
            optics_->stop_sampling();
            compare_layout_->setCurrentIndex(0);
        }
        view_->viewport()->setCursor(active ? Qt::CrossCursor : Qt::ArrowCursor);
        display();
    });
    connect(mask_tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (mask_controls_loading_ || !item)
            return;
        const int local = item->data(0, Qt::UserRole).toInt();
        mask_component_ = item->data(0, Qt::UserRole + 1).toInt();
        mask_tree_event_ = true;
        const QSignalBlocker blocker(adjustment_);
        adjustment_->setCurrentIndex(local + 1);
        select_adjustment();
        refresh_mask_components();
        display();
        mask_tree_event_ = false;
        QTimer::singleShot(0, this, &RawDialog::refresh_mask_components);
    });
    connect(mask_tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item) {
        if (mask_controls_loading_)
            return;
        const int local_index = item->data(0, Qt::UserRole).toInt(),
                  component_index = item->data(0, Qt::UserRole + 1).toInt();
        if (local_index < 0)
            return;
        mask_tree_event_ = true;
        history_timer_->stop();
        record_history();
        auto locals = sources_[current_].raw_recipe.value("locals").toArray();
        auto local = locals[local_index].toObject();
        auto changed = component_index < 0
                           ? local
                           : local.value("components").toArray()[component_index].toObject();
        changed.insert("name", item->text(0).trimmed().left(120));
        changed.insert("enabled", item->checkState(0) == Qt::Checked);
        if (component_index < 0)
            local = changed;
        else {
            auto components = local.value("components").toArray();
            components[component_index] = changed;
            local.insert("components", components);
        }
        locals[local_index] = local;
        sources_[current_].raw_recipe.insert("locals", locals);
        record_history();
        refresh_local_controls();
        refresh_mask_components();
        display();
        mask_tree_event_ = false;
        QTimer::singleShot(0, this, &RawDialog::refresh_mask_components);
    });
    return panel;
}
void RawDialog::update_mask_component(const QJsonObject& component) {
    if (mask_controls_loading_)
        return;
    auto locals = sources_[current_].raw_recipe.value("locals").toArray();
    const int index = adjustment_->currentIndex() - 1;
    if (index < 0 || mask_component_ < 0)
        return;
    auto local = locals[index].toObject();
    auto components = local.value("components").toArray();
    if (mask_component_ >= components.size())
        return;
    components[mask_component_] = component;
    local.insert("components", components);
    locals[index] = local;
    sources_[current_].raw_recipe.insert("locals", locals);
    if (!mask_dragging_ && !parameter_gesture_)
        history_timer_->start();
    display();
}
void RawDialog::refresh_mask_components() {
    if (!mask_tree_ || mask_tree_event_)
        return;
    mask_controls_loading_ = true;
    const QSignalBlocker blocker(mask_tree_);
    mask_tree_->clear();
    auto* global = new QTreeWidgetItem(mask_tree_, QStringList{tr("全局调整")});
    global->setData(0, Qt::UserRole, -1);
    global->setData(0, Qt::UserRole + 1, -1);
    const auto locals = sources_[current_].raw_recipe.value("locals").toArray();
    const int active = adjustment_->currentIndex() - 1;
    QJsonObject selected;
    if (active < 0) {
        mask_component_ = -1;
        mask_tree_->setCurrentItem(global);
    }
    for (int i = 0; i < locals.size(); ++i) {
        const auto local = locals[i].toObject();
        auto* root = new QTreeWidgetItem(
            mask_tree_, QStringList{local.value("name").toString(tr("局部 %1").arg(i + 1))});
        root->setData(0, Qt::UserRole, i);
        root->setData(0, Qt::UserRole + 1, -1);
        root->setFlags(root->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        root->setCheckState(0, local.value("enabled").toBool(true) ? Qt::Checked : Qt::Unchecked);
        root->setExpanded(true);
        const auto components = local.value("components").toArray();
        if (i == active && mask_component_ >= components.size())
            mask_component_ = -1;
        if (i == active && mask_component_ < 0)
            mask_tree_->setCurrentItem(root);
        for (int j = 0; j < components.size(); ++j) {
            const auto component = components[j].toObject();
            auto* child = new QTreeWidgetItem(root, QStringList{component_label(component)});
            child->setData(0, Qt::UserRole, i);
            child->setData(0, Qt::UserRole + 1, j);
            child->setFlags(child->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
            child->setCheckState(0, component.value("enabled").toBool(true) ? Qt::Checked
                                                                            : Qt::Unchecked);
            if (i == active && j == mask_component_) {
                selected = component;
                mask_tree_->setCurrentItem(child);
            }
        }
    }
    int rows = 1 + int(locals.size());
    for (const auto& local : locals)
        rows += int(local.toObject().value("components").toArray().size());
    mask_tree_->setFixedHeight(std::clamp(rows * 24 + 8, 80, 180));
    const auto operation = selected.value("operation")
                               .toString(selected.value("subtract").toBool() ? "subtract" : "add");
    findChild<QComboBox*>("rawMaskOperation")
        ->setCurrentIndex(operation == "subtract"    ? 1
                          : operation == "intersect" ? 2
                                                     : 0);
    for (const auto* key : {"flow", "density", "low", "high", "smooth", "range"}) {
        auto* spin = findChild<QDoubleSpinBox*>(QString("rawMask_%1").arg(key));
        const QString field(key);
        const double fallback = field == "flow" || field == "density" || field == "high" ? 1
                                : field == "smooth"                                      ? .1
                                : field == "range"                                       ? 50
                                                                                         : 0;
        spin->setValue(selected.value(field).toDouble(fallback) * (field == "range" ? 1 : 100));
        const auto mode = selected.value("mode").toString();
        const bool available =
            active >= 0 &&
            (field == "density" || (field == "flow" && mode == "brush") ||
             (field == "flow" && selected.isEmpty()) || (field == "range" && mode == "color") ||
             ((field == "low" || field == "high" || field == "smooth") && mode == "luminance"));
        spin->parentWidget()->setVisible(available);
        spin->setEnabled(available);
        findChild<QSlider*>("rawMaskSlider_" + field)->setValue(qRound(spin->value()));
    }
    const auto selected_mode = selected.value("mode").toString();
    const bool range_mode = selected_mode == "color" || selected_mode == "luminance";
    findChild<QPushButton*>("rawMaskColorPicker")->setEnabled(range_mode);
    if (!range_mode) {
        mask_sampling_ = false;
        const QSignalBlocker picker_blocker(findChild<QPushButton*>("rawMaskColorPicker"));
        findChild<QPushButton*>("rawMaskColorPicker")->setChecked(false);
    }
    const auto mode = selected.value("mode").toString();
    if (mode == "brush" && !source_size_.isEmpty())
        local_radius_->setValue(selected.value("radius").toDouble(.03) * 2 *
                                std::min(source_size_.width(), source_size_.height()));
    if (mode == "brush" || mode == "radial")
        local_feather_->setValue(selected.value("feather").toDouble(.5) * 100);
    const auto colors = selected.value("colors").toArray();
    for (int i = 0; i < 5; ++i) {
        auto* sample = findChild<QToolButton*>(QString("rawMaskColorSample%1").arg(i));
        sample->setVisible(mode == "color" && i < colors.size());
        if (i >= colors.size())
            continue;
        const auto rgb = colors[i].toArray();
        if (rgb.size() != 3)
            continue;
        QPixmap icon(20, 20);
        icon.fill(QColor::fromRgbF(std::clamp(rgb[0].toDouble(), 0., 1.),
                                   std::clamp(rgb[1].toDouble(), 0., 1.),
                                   std::clamp(rgb[2].toDouble(), 0., 1.)));
        sample->setIcon(QIcon(icon));
    }
    findChild<QWidget*>("rawMaskColorSamples")->setVisible(mode == "color");
    findChild<QPushButton*>("rawMaskColorPicker")->setVisible(range_mode);
    findChild<QPushButton*>("rawMaskColorPicker")
        ->setText(mode == "color" ? tr("取样颜色（Shift 添加，拖动区域取样）")
                                  : tr("取样明度（单击或拖动区域）"));
    auto* mask_form = qobject_cast<QFormLayout*>(findChild<QWidget*>("rawMaskPanel")->layout());
    mask_form->setRowVisible(local_radius_, mode == "brush" || selected.isEmpty());
    mask_form->setRowVisible(local_feather_,
                             mode == "brush" || mode == "radial" || selected.isEmpty());
    mask_controls_loading_ = false;
}
void RawDialog::remove_mask_sample(int sample) {
    const int index = adjustment_->currentIndex() - 1;
    if (index < 0 || mask_component_ < 0)
        return;
    const auto locals = sources_[current_].raw_recipe.value("locals").toArray();
    auto component =
        locals[index].toObject().value("components").toArray()[mask_component_].toObject();
    auto colors = component.value("colors").toArray();
    if (sample < 0 || sample >= colors.size())
        return;
    colors.removeAt(sample);
    component.insert("colors", colors);
    history_timer_->stop();
    record_history();
    update_mask_component(component);
    history_timer_->stop();
    record_history();
    refresh_mask_components();
}
void RawDialog::sample_mask_range(QPointF point, bool append) {
    if (point_sample_preview_.isNull())
        return;
    const auto pixel = [this](QPointF p) {
        return QPoint(int(p.x() / source_size_.width() * point_sample_preview_.width()),
                      int(p.y() / source_size_.height() * point_sample_preview_.height()));
    };
    const auto area = QRect(pixel(mask_drag_start_), pixel(point))
                          .normalized()
                          .intersected(point_sample_preview_.rect());
    if (area.isEmpty())
        return;
    const auto locals = sources_[current_].raw_recipe.value("locals").toArray();
    const int index = adjustment_->currentIndex() - 1;
    if (index < 0 || mask_component_ < 0)
        return;
    auto component =
        locals[index].toObject().value("components").toArray()[mask_component_].toObject();
    double red = 0, green = 0, blue = 0, low = 1, high = 0;
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            const auto color = point_sample_preview_.pixelColor(x, y);
            if (color.alpha() == 0)
                continue;
            red += color.redF();
            green += color.greenF();
            blue += color.blueF();
            ++count;
            const double luminance =
                .2126 * color.redF() + .7152 * color.greenF() + .0722 * color.blueF();
            low = std::min(low, luminance);
            high = std::max(high, luminance);
        }
    if (!count)
        return;
    history_timer_->stop();
    record_history();
    if (component.value("mode").toString() == "luminance") {
        if (area.width() <= 1 && area.height() <= 1) {
            low = std::max(0., low - .05);
            high = std::min(1., high + .05);
        }
        component.insert("low", low);
        component.insert("high", high);
        update_mask_component(component);
        history_timer_->stop();
        record_history();
        refresh_mask_components();
        return;
    }
    auto colors = append ? component.value("colors").toArray() : QJsonArray{};
    if (colors.size() >= 5) {
        message_->setText(tr("颜色范围最多五个样本；Alt 单击色块可移除样本。"));
        return;
    }
    colors.append(QJsonArray{red / count, green / count, blue / count});
    component.insert("colors", colors);
    update_mask_component(component);
    history_timer_->stop();
    record_history();
    refresh_mask_components();
}
void RawDialog::mask_component_overlays() {
    if (mask_guide_) {
        scene_->removeItem(mask_guide_);
        delete mask_guide_;
        mask_guide_ = nullptr;
    }
    const int index = adjustment_->currentIndex() - 1;
    const auto locals = sources_[current_].raw_recipe.value("locals").toArray();
    if (index < 0 || mask_component_ < 0 || edit_mode_->currentIndex() < 1 ||
        edit_mode_->currentIndex() > 4)
        return;
    const auto components = locals[index].toObject().value("components").toArray();
    if (mask_component_ >= components.size())
        return;
    const auto c = components[mask_component_].toObject();
    const auto mode = c.value("mode").toString();
    if (mode != "brush" && mode != "linear" && mode != "radial")
        return;
    QPointF a(c.value("x").toDouble() * source_size_.width(),
              c.value("y").toDouble() * source_size_.height()),
        b(c.value("endX").toDouble() * source_size_.width(),
          c.value("endY").toDouble() * source_size_.height());
    if (mode == "brush") {
        const auto points = c.value("points").toArray();
        if (points.isEmpty())
            return;
        const auto first = points.first().toObject();
        a = b = QPointF(first.value("x").toDouble() * source_size_.width(),
                        first.value("y").toDouble() * source_size_.height());
    }
    QTransform mapping;
    if (c.contains("geometryAtCreation"))
        mapping =
            editing::geometry_transform(
                source_size_, editing::geometry_from_json(c.value("geometryAtCreation").toObject()))
                .inverted() *
            editing::geometry_transform(
                source_size_, editing::geometry_from_json(
                                  sources_[current_].raw_recipe.value("geometry").toObject()));
    QPainterPath path;
    if (mode == "radial")
        path.addEllipse(QRectF(a, b).normalized());
    else if (mode == "linear") {
        path.moveTo(a);
        path.lineTo(b);
    }
    path = mapping.map(path);
    const auto p = mapping.map(a), q = mapping.map(b);
    const double radius = 5 / std::hypot(view_->transform().m11(),view_->transform().m12());
    path.addEllipse(p, radius, radius);
    path.addEllipse(q, radius, radius);
    if (mode != "brush")
        path.addRect(QRectF((p + q) / 2 - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)));
    mask_guide_ = scene_->addPath(path, QPen(QColor("#76c9ff"), 1 / std::hypot(view_->transform().m11(),view_->transform().m12())));
    mask_guide_->setZValue(20);
}
bool RawDialog::mask_component_event(QMouseEvent* event) {
    const int index = adjustment_->currentIndex() - 1;
    const auto locals = sources_[current_].raw_recipe.value("locals").toArray();
    if (index < 0 || mask_component_ < 0)
        return false;
    const auto components = locals[index].toObject().value("components").toArray();
    if (mask_component_ >= components.size())
        return false;
    const auto component = components[mask_component_].toObject();
    const auto mode = component.value("mode").toString();
    if (mode != "brush" && mode != "linear" && mode != "radial")
        return false;
    QTransform mapping;
    if (component.contains("geometryAtCreation"))
        mapping =
            editing::geometry_transform(
                source_size_,
                editing::geometry_from_json(component.value("geometryAtCreation").toObject()))
                .inverted() *
            editing::geometry_transform(
                source_size_, editing::geometry_from_json(
                                  sources_[current_].raw_recipe.value("geometry").toObject()));
    const auto at = mapping.inverted().map(view_->mapToScene(event->position().toPoint()));
    const QPointF normalized(at.x() / source_size_.width(), at.y() / source_size_.height());
    if (event->type() == QEvent::MouseButtonPress) {
        if (mode == "brush" && !event->modifiers().testFlag(Qt::ControlModifier))
            return false;
        QPointF a(component.value("x").toDouble(), component.value("y").toDouble()),
            b(component.value("endX").toDouble(), component.value("endY").toDouble());
        if (mode == "brush") {
            const auto points = component.value("points").toArray();
            if (points.isEmpty())
                return false;
            const auto first = points.first().toObject();
            a = b = QPointF(first.value("x").toDouble(), first.value("y").toDouble());
        }
        const auto screen = [&](QPointF p) {
            return view_->mapFromScene(
                mapping.map(QPointF(p.x() * source_size_.width(), p.y() * source_size_.height())));
        };
        mask_handle_ = -1;
        const std::array<QPointF, 3> handles{(a + b) / 2, a, b};
        for (int i = 0; i < 3; ++i)
            if (QLineF(event->position(), screen(handles[i])).length() < 9) {
                mask_handle_ = i;
                break;
            }
        if (mask_handle_ < 0)
            return false;
        history_timer_->stop();
        record_history();
        mask_before_ = component;
        mask_drag_start_ = normalized;
        mask_dragging_ = parameter_gesture_ = true;
        mask_tree_->setEnabled(false);
        return true;
    }
    if (!mask_dragging_)
        return false;
    auto updated = mask_before_;
    const auto delta = normalized - mask_drag_start_;
    const auto move = [&](const char* x, const char* y) {
        updated.insert(x, std::clamp(mask_before_.value(x).toDouble() + delta.x(), -4., 4.));
        updated.insert(y, std::clamp(mask_before_.value(y).toDouble() + delta.y(), -4., 4.));
    };
    if (mode == "brush") {
        auto points = updated.value("points").toArray();
        for (int i = 0; i < points.size(); ++i) {
            auto point = points[i].toObject();
            point.insert("x", std::clamp(point.value("x").toDouble() + delta.x(), -4., 4.));
            point.insert("y", std::clamp(point.value("y").toDouble() + delta.y(), -4., 4.));
            points[i] = point;
        }
        updated.insert("points", points);
        move("x", "y");
        move("endX", "endY");
    } else {
        if (mask_handle_ == 0 || mask_handle_ == 1)
            move("x", "y");
        if (mask_handle_ == 0 || mask_handle_ == 2)
            move("endX", "endY");
    }
    update_mask_component(updated);
    mask_component_overlays();
    if (event->type() == QEvent::MouseButtonRelease) {
        mask_dragging_ = parameter_gesture_ = false;
        mask_tree_->setEnabled(true);
        history_timer_->stop();
        record_history();
        display();
    }
    return true;
}

} // namespace huelith::desktop
