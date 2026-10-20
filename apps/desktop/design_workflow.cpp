#include "huelith/editing/image_storage.h"
#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/layout_template.h"
#include "main_window.h"
#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTextCharFormat>
#include <QToolBar>
#include <QVBoxLayout>

namespace huelith::desktop {
using namespace editing;
void EditorController::configure_design_tools() {
    auto* options = window_->findChild<QToolBar*>("viewToolbar");
    auto* family = new QFontComboBox(options);
    family->setObjectName("textFamily");
    family->setMaximumWidth(190);
    family->setCurrentFont(QFont("Microsoft YaHei"));
    options->addWidget(family)->setObjectName("text_family");
    connect(family, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        if (auto* c = canvas()) {
            QTextCharFormat f;
            f.setFontFamilies({font.family()});
            c->format_text(f);
        }
    });
    auto* size = new QDoubleSpinBox(options);
    size->setObjectName("textSize");
    size->setRange(1, 1296);
    size->setValue(24);
    size->setSuffix(" pt");
    options->addWidget(size)->setObjectName("text_size");
    connect(size, &QDoubleSpinBox::editingFinished, this, [this, size] {
        if (auto* c = canvas()) {
            QTextCharFormat f;
            f.setFontPointSize(size->value());
            c->format_text(f);
        }
    });
    for (int i = 0; i < 3; ++i) {
        auto* action = options->addAction(QStringList{"B", "I", "U"}[i]);
        action->setObjectName(QString("text_style%1").arg(i));
        action->setCheckable(true);
        action->setToolTip(QStringList{tr("粗体"), tr("斜体"), tr("下划线")}[i]);
        connect(action, &QAction::triggered, this, [this, i](bool on) {
            if (auto* c = canvas()) {
                QTextCharFormat f;
                if (i == 0)
                    f.setFontWeight(on ? QFont::Bold : QFont::Normal);
                if (i == 1)
                    f.setFontItalic(on);
                if (i == 2)
                    f.setFontUnderline(on);
                c->format_text(f);
            }
        });
    }
    auto* color = options->addAction(tr("文字颜色"));
    color->setObjectName("text_color");
    connect(color, &QAction::triggered, this, [this] {
        if (auto* c = canvas()) {
            const auto color = QColorDialog::getColor(c->foreground, window_, tr("文字颜色"));
            if (color.isValid()) {
                QTextCharFormat f;
                f.setForeground(color);
                c->format_text(f);
            }
        }
    });
    auto* align = new QComboBox(options);
    align->setObjectName("textAlignment");
    align->addItems({tr("左对齐"), tr("居中"), tr("右对齐"), tr("两端对齐")});
    options->addWidget(align)->setObjectName("text_alignment");
    connect(align, &QComboBox::activated, this, [this](int index) {
        const Qt::Alignment modes[]{Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight,
                                    Qt::AlignJustify};
        if (auto* c = canvas())
            c->align_text(modes[index]);
    });
    auto* tracking = new QDoubleSpinBox(options);
    tracking->setObjectName("textTracking");
    tracking->setRange(-20, 100);
    tracking->setPrefix(tr("字距 "));
    tracking->setSuffix(" px");
    options->addWidget(tracking)->setObjectName("text_tracking");
    connect(tracking, &QDoubleSpinBox::editingFinished, this, [this, tracking] {
        if (auto* c = canvas()) {
            QTextCharFormat f;
            f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
            f.setFontLetterSpacing(tracking->value());
            c->format_text(f);
        }
    });
    auto* leading = new QDoubleSpinBox(options);
    leading->setObjectName("textLeading");
    leading->setRange(20, 500);
    leading->setValue(100);
    leading->setPrefix(tr("行距 "));
    leading->setSuffix(" %");
    options->addWidget(leading)->setObjectName("text_leading");
    connect(leading, &QDoubleSpinBox::editingFinished, this, [this, leading] {
        if (auto* c = canvas())
            c->text_line_height(leading->value());
    });

    auto* type = new QComboBox(options);
    type->setObjectName("shapeType");
    type->addItems({tr("矩形"), tr("椭圆"), tr("多边形 / 星形"), tr("直线"), tr("自定路径")});
    options->addWidget(type)->setObjectName("shape_type");
    connect(type, &QComboBox::activated, this, [this](int index) {
        if (auto* c = canvas()) {
            c->shape_options.kind = index;
            if (index == 3) {
                c->shape_options.stroke_enabled = true;
                c->shape_options.stroke_width = 2;
            }
            refresh_design_tools();
        }
    });
    auto* pen = new QComboBox(options);
    pen->setObjectName("penOperation");
    pen->addItems({tr("钢笔"), tr("添加锚点"), tr("删除锚点"), tr("转换点")});
    options->addWidget(pen)->setObjectName("pen_operation");
    connect(pen, &QComboBox::activated, this, [this](int index) {
        if (canvas())
            canvas()->pen_mode = index;
    });
    auto* dock = new QDockWidget(tr("形状属性"), window_);
    dock->setObjectName("shapePropertiesDock");
    auto* scroll = new QScrollArea(dock);
    scroll->setWidgetResizable(true);
    auto* panel = new QWidget(scroll);
    auto* form = new QFormLayout(panel);
    scroll->setWidget(panel);
    dock->setWidget(scroll);
    window_->addDockWidget(Qt::RightDockWidgetArea, dock);
    window_->tabifyDockWidget(window_->findChild<QDockWidget*>("propertiesDock"), dock);
    auto apply = [this](const std::function<void(ShapeContent&)>& change) {
        if (!canvas() || refreshing_)
            return;
        guarded([&] {
            auto* c = canvas();
            const auto* layer = find_layer(c->project().state().layers, c->active_layer);
            change(c->shape_options);
            if (layer && layer->kind == LayerKind::shape) {
                if (layer_locked(c->project().state().layers, layer->id))
                    throw EditError(tr("图层已锁定"));
                c->edit(tr("形状属性"), [&](State& state) {
                    change(find_layer(state.layers, c->active_layer)->shape);
                });
            }
        });
    };
    for (const bool stroke : {false, true}) {
        auto* button = new QPushButton(stroke ? tr("描边") : tr("填充"), options);
        button->setObjectName(stroke ? "shapeTopStroke" : "shapeTopFill");
        options->addWidget(button)->setObjectName(stroke ? "shape_stroke" : "shape_fill");
        connect(button, &QPushButton::clicked, this, [this, apply, stroke] {
            if (!canvas())
                return;
            const auto color = QColorDialog::getColor(canvas()->foreground, window_,
                                                      stroke ? tr("描边颜色") : tr("填充颜色"),
                                                      QColorDialog::ShowAlphaChannel);
            if (!color.isValid())
                return;
            apply([&](ShapeContent& s) {
                (stroke ? s.stroke : s.fill) = color;
                (stroke ? s.stroke_enabled : s.fill_enabled) = true;
            });
            if (!stroke)
                canvas()->foreground = color;
            refresh_design_tools();
        });
    }
    auto number = [&](const char* name, const QString& label, double minimum, double maximum,
                      std::function<void(ShapeContent&, double)> change) {
        auto* spin = new QDoubleSpinBox(panel);
        spin->setObjectName(name);
        spin->setRange(minimum, maximum);
        form->addRow(label, spin);
        connect(spin, &QDoubleSpinBox::editingFinished, this, [spin, apply, change] {
            apply([&](ShapeContent& s) { change(s, spin->value()); });
        });
    };
    auto resize_shape = [](ShapeContent& s, double value, bool horizontal) {
        if (s.kind == 4 && !s.path.isEmpty()) {
            const auto bounds = s.path.boundingRect();
            if (bounds.width() > 0 && bounds.height() > 0) {
                QTransform scale;
                scale.translate(bounds.x(), bounds.y());
                scale.scale(horizontal ? value / bounds.width() : 1,
                            horizontal ? 1 : value / bounds.height());
                scale.translate(-bounds.x(), -bounds.y());
                s.path = scale.map(s.path);
            }
        }
        if (horizontal)
            s.size.setWidth(value);
        else
            s.size.setHeight(value);
    };
    number("shapeWidth", tr("宽度 px"), .1, 100000,
           [resize_shape](auto& s, double v) { resize_shape(s, v, true); });
    number("shapeHeight", tr("高度 px"), .1, 100000,
           [resize_shape](auto& s, double v) { resize_shape(s, v, false); });
    number("shapeRadius", tr("圆角 px"), 0, 50000, [](auto& s, double v) { s.radius = v; });
    number("shapeSides", tr("边数"), 3, 100, [](auto& s, double v) { s.sides = qRound(v); });
    number("shapeStar", tr("内径 %"), 1, 100, [](auto& s, double v) { s.star_ratio = v / 100; });
    for (const bool stroke : {false, true}) {
        auto* check = new QCheckBox(stroke ? tr("描边") : tr("填充"), panel);
        check->setObjectName(stroke ? "shapeStrokeEnabled" : "shapeFillEnabled");
        form->addRow(check);
        connect(check, &QCheckBox::toggled, this, [apply, stroke](bool v) {
            apply([&](ShapeContent& s) { (stroke ? s.stroke_enabled : s.fill_enabled) = v; });
        });
        auto* button = new QPushButton(tr("颜色…"), panel);
        button->setObjectName(stroke ? "shapeStrokeColor" : "shapeFillColor");
        form->addRow(button);
        connect(button, &QPushButton::clicked, this, [this, apply, stroke] {
            if (!canvas())
                return;
            const auto color = QColorDialog::getColor(canvas()->foreground, window_, tr("形状颜色"),
                                                      QColorDialog::ShowAlphaChannel);
            if (color.isValid()) {
                apply([&](ShapeContent& s) { (stroke ? s.stroke : s.fill) = color; });
                if (!stroke)
                    canvas()->foreground = color;
                refresh_design_tools();
            }
        });
    }
    number("shapeStrokeWidth", tr("描边宽度 px"), 0, 1000,
           [](auto& s, double v) { s.stroke_width = v; });
    auto combo = [&](const char* name, const QString& label, const QStringList& names,
                     std::function<void(ShapeContent&, int)> change) {
        auto* value = new QComboBox(panel);
        value->setObjectName(name);
        value->addItems(names);
        form->addRow(label, value);
        connect(value, &QComboBox::activated, this,
                [apply, change](int i) { apply([&](ShapeContent& s) { change(s, i); }); });
    };
    combo("shapeStrokePosition", tr("描边对齐"), {tr("居中"), tr("内部"), tr("外部")},
          [](auto& s, int v) { s.stroke_position = v; });
    combo("shapeStrokeStyle", tr("描边样式"), {tr("实线"), tr("虚线"), tr("点线")},
          [](auto& s, int v) { s.stroke_style = v; });
    auto* gradient = new QCheckBox(tr("渐变填充"), panel);
    gradient->setObjectName("shapeGradient");
    form->addRow(gradient);
    connect(gradient, &QCheckBox::toggled, this,
            [apply](bool v) { apply([&](ShapeContent& s) { s.gradient = v; }); });
    auto* end = new QPushButton(tr("渐变终点颜色…"), panel);
    form->addRow(end);
    connect(end, &QPushButton::clicked, this, [this, apply] {
        const auto color = QColorDialog::getColor(Qt::white, window_, tr("渐变终点颜色"),
                                                  QColorDialog::ShowAlphaChannel);
        if (color.isValid())
            apply([&](ShapeContent& s) { s.fill_end = color; });
    });
    number("shapeGradientAngle", tr("渐变角度"), -360, 360,
           [](auto& s, double v) { s.gradient_angle = v; });
    auto* window_menu = window_->menus_->actions().last()->menu();
    if (window_menu)
        window_menu->addAction(dock->toggleViewAction());
    dock->hide();
    for (auto* menu : window_->findChildren<QMenu*>())
        if (menu->title() == tr("图层(&L)")) {
            auto* type_menu = menu->addMenu(tr("文字"));
            auto* edit = type_menu->addAction(tr("编辑文字"));
            connect(edit, &QAction::triggered, this, [this] {
                if (canvas()) {
                    canvas()->set_tool(CanvasTool::text);
                    canvas()->begin_text({}, false, true);
                }
            });
            auto* convert = type_menu->addAction(tr("转换点文字 / 段落文字"));
            connect(convert, &QAction::triggered, this, [this] {
                guarded([&] {
                    if (auto* c = canvas()) {
                        c->finish_text(true);
                        c->edit(tr("转换文字类型"), [&](State& state) {
                            auto* layer = find_layer(state.layers, c->active_layer);
                            if (!layer || layer->kind != LayerKind::text ||
                                layer_locked(state.layers, layer->id))
                                throw EditError(tr("请选择未锁定文字图层"));
                            layer->text.paragraph = !layer->text.paragraph;
                        });
                    }
                });
            });
            auto* from_path = menu->addAction(tr("从当前路径创建形状"));
            connect(from_path, &QAction::triggered, this, [this] {
                guarded([&] {
                    if (auto* c = canvas(); c && !c->path.isEmpty()) {
                        Layer layer;
                        layer.kind = LayerKind::shape;
                        layer.name = tr("路径形状");
                        layer.shape = c->shape_options;
                        layer.shape.kind = 4;
                        layer.shape.path = c->path;
                        layer.shape.fill = c->foreground;
                        c->active_layer = layer.id;
                        c->selected_layers = {layer.id};
                        c->edit(tr("路径形状"),
                                [&](State& state) { state.layers.push_back(layer); });
                    }
                });
            });
            auto* combine = menu->addMenu(tr("合并形状"));
            const QStringList operations{tr("联合形状"),tr("减去顶层形状"),tr("与形状区域相交"),tr("排除重叠形状")};
            for(int operation=0;operation<operations.size();++operation) {
                auto* action=combine->addAction(operations[operation]);
                connect(action,&QAction::triggered,this,[this,operation,operations] {
                    guarded([&] {
                        auto* c=canvas();if(!c)return;c->finish_text(true);c->commit_transform();
                        const auto selected=c->selected_layers;
                        if(selected.size()<2)throw EditError(tr("请先选择至少两个形状图层"));
                        QUuid kept;
                        c->edit(operations[operation],[&](State& state) {
                            std::vector<QUuid> ordered;
                            std::function<void(const std::vector<Layer>&)> collect=[&](const auto& layers) {
                                for(const auto& layer:layers) {
                                    if(std::find(selected.begin(),selected.end(),layer.id)!=selected.end())ordered.push_back(layer.id);
                                    collect(layer.children);
                                }
                            };collect(state.layers);
                            QPainterPath result;bool first=true;
                            for(const auto& id:ordered) {
                                const auto* layer=find_layer(state.layers,id);
                                if(!layer || layer->kind!=LayerKind::shape || layer_locked(state.layers,id))
                                    throw EditError(tr("只能组合未锁定的形状图层"));
                                if(layer->warped)throw EditError(tr("请先取消形状变形，再组合路径"));
                                const auto path=(layer->transform*parent_matrix(state,id)).map(shape_path(layer->shape));
                                if(first){result=path;first=false;}
                                else if(operation==0)result=result.united(path);
                                else if(operation==1)result=result.subtracted(path);
                                else if(operation==2)result=result.intersected(path);
                                else result=result.united(path).subtracted(result.intersected(path));
                            }
                            if(result.isEmpty())throw EditError(tr("组合结果为空，请调整形状位置"));
                            kept=std::find(ordered.begin(),ordered.end(),c->active_layer)!=ordered.end()?c->active_layer:ordered.back();
                            auto* target=find_layer(state.layers,kept);
                            bool invertible=false;const auto inverse=(target->transform*parent_matrix(state,kept)).inverted(&invertible);
                            if(!invertible)throw EditError(tr("当前形状变换不可逆"));
                            target->shape.kind=4;target->shape.path=inverse.map(result);
                            target->name=operations[operation];
                            for(const auto& id:ordered)if(id!=kept)remove_layer(state,id);
                        });
                        c->active_layer=kept;c->selected_layers={kept};c->set_tool(CanvasTool::path_select);refresh();
                    });
                });
            }
            auto* rasterize=menu->addAction(tr("栅格化文字 / 形状"));
            rasterize->setToolTip(tr("转换当前文字或形状的外观为像素，可撤销恢复可编辑内容。"));
            connect(rasterize,&QAction::triggered,this,[this] {
                guarded([&] {
                    auto* c=canvas();if(!c)return;c->finish_text(true);c->commit_transform();
                    c->edit(tr("栅格化文字 / 形状"),[&](State& state) {
                        auto* layer=find_layer(state.layers,c->active_layer);
                        if(!layer || (layer->kind!=LayerKind::text && layer->kind!=LayerKind::shape) || layer_locked(state.layers,layer->id))
                            throw EditError(tr("请选择未锁定的文字或形状图层"));
                        const auto parent=parent_matrix(state,layer->id);
                        bool invertible=false;const auto inverse=parent.inverted(&invertible);
                        if(!invertible)throw EditError(tr("父组变换不可逆，无法栅格化"));
                        auto copy=*layer;copy.transform*=parent;copy.mask_transform*=parent;
                        copy.opacity=1;copy.blend=Blend::normal;copy.visible=true;copy.clipped=false;
                        const auto extent=QRect(QPoint(),state.size).united(layer_bounds(state,layer->id).toAlignedRect());
                        if(qint64(extent.width())*extent.height()>huelith::editing::maximum_image_pixels)throw EditError(tr("栅格化范围过大，请先缩小图层"));
                        const auto origin=QTransform::fromTranslate(-extent.x(),-extent.y());
                        copy.transform*=origin;copy.mask_transform*=origin;
                        State isolated;isolated.name=state.name;isolated.ppi=state.ppi;isolated.size=extent.size();isolated.global_light_angle=state.global_light_angle;isolated.layers={copy};
                        const auto pixels=render(isolated);
                        Layer raster;raster.id=layer->id;raster.name=layer->name;raster.pixels=pixels;
                        raster.transform=QTransform::fromTranslate(extent.x(),extent.y())*inverse;raster.opacity=layer->opacity;raster.blend=layer->blend;
                        raster.visible=layer->visible;raster.clipped=layer->clipped;
                        *layer=std::move(raster);
                    });refresh();
                });
            });
        }
}
void EditorController::refresh_design_tools() {
    auto* c = canvas();
    if (c && c->tool() == CanvasTool::text) {
        const auto format = c->current_text_format();
        if (auto* family = window_->findChild<QFontComboBox*>("textFamily")) {
            const QSignalBlocker block(family);
            if (!format.fontFamilies().toStringList().isEmpty()) family->setCurrentFont(format.font());
        }
        if (auto* size = window_->findChild<QDoubleSpinBox*>("textSize")) {
            const QSignalBlocker block(size); size->setValue(format.fontPointSize() > 0 ? format.fontPointSize() : 24);
        }
        for (int i = 0; i < 3; ++i)
            if (auto* action = window_->findChild<QAction*>(QString("text_style%1").arg(i))) {
                const QSignalBlocker block(action);
                action->setChecked(i == 0 ? format.fontWeight() >= QFont::Bold : i == 1 ? format.fontItalic() : format.fontUnderline());
            }
    }
    if (auto* warning = window_->findChild<QLabel*>("missingFontWarning")) {
        const auto missing = c ? missing_fonts(c->project().state()) : QStringList{};
        warning->setVisible(!missing.isEmpty());
        warning->setText(
            tr("缺少字体：%1。暂用系统替代字体，原字体名称仍保留。").arg(missing.join(", ")));
    }
    auto* dock = window_->findChild<QDockWidget*>("shapePropertiesDock");
    if (!dock)
        return;
    const auto* layer = c ? find_layer(c->project().state().layers, c->active_layer) : nullptr;
    const bool shape = layer && layer->kind == LayerKind::shape;
    const bool active = c && (c->tool() == CanvasTool::shape || shape);
    if (active && !dock->property("designActive").toBool()) {
        dock->show();
        dock->raise();
    }
    dock->setProperty("designActive", active);
    dock->setEnabled(active);
    if (!active)
        return;
    const auto s = shape ? layer->shape : c->shape_options;
    if (auto* form =
            qobject_cast<QFormLayout*>(dock->findChild<QScrollArea*>()->widget()->layout())) {
        for (const auto& [name, visible] : std::initializer_list<std::pair<const char*, bool>>{
                 {"shapeRadius", s.kind == 0},
                 {"shapeSides", s.kind == 2},
                 {"shapeStar", s.kind == 2},
                 {"shapeGradientAngle", s.gradient},
                 {"shapeStrokeWidth", s.stroke_enabled},
                 {"shapeStrokePosition", s.stroke_enabled && s.kind != 3},
                 {"shapeStrokeStyle", s.stroke_enabled}})
            form->setRowVisible(dock->findChild<QWidget*>(name), visible);
    }
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, double>>{
             {"shapeWidth", s.size.width()},
             {"shapeHeight", s.size.height()},
             {"shapeRadius", s.radius},
             {"shapeSides", double(s.sides)},
             {"shapeStar", s.star_ratio * 100},
             {"shapeStrokeWidth", s.stroke_width},
             {"shapeGradientAngle", s.gradient_angle}}) {
        auto* spin = dock->findChild<QDoubleSpinBox*>(name);
        const QSignalBlocker block(spin);
        spin->setValue(value);
    }
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, bool>>{
             {"shapeFillEnabled", s.fill_enabled},
             {"shapeStrokeEnabled", s.stroke_enabled},
             {"shapeGradient", s.gradient}}) {
        auto* check = dock->findChild<QCheckBox*>(name);
        const QSignalBlocker block(check);
        check->setChecked(value);
    }
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, int>>{
             {"shapeStrokePosition", s.stroke_position}, {"shapeStrokeStyle", s.stroke_style}}) {
        auto* combo = dock->findChild<QComboBox*>(name);
        const QSignalBlocker block(combo);
        combo->setCurrentIndex(value);
    }
    for (const auto& [name, color] : std::initializer_list<std::pair<const char*, QColor>>{
             {"shapeFillColor", s.fill}, {"shapeStrokeColor", s.stroke}})
        dock->findChild<QPushButton*>(name)->setStyleSheet(
            QString("background:%1;").arg(color.name()));
}
} // namespace huelith::desktop
