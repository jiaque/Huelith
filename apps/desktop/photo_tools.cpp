#include <QPointer>
#include "canvas_view.h"
#include "editor_controller.h"
#include "main_window.h"
#include "raw_dialog.h"
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QRadioButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QPushButton>
#include <tuple>

namespace huelith::desktop {
void EditorController::configure_photo_tools() {
    auto* toolbar = window_->findChild<QToolBar*>("editingTools");
    auto* panel = window_->findChild<QWidget*>("toolGrid");
    auto* group = window_->findChild<QActionGroup*>("toolActions");
    auto* layout = qobject_cast<QGridLayout*>(panel->layout());
    struct Tool {
        CanvasTool tool;
        const char* label;
        const char* shortcut;
        const char* drawing;
    };
    const Tool tools[]{
        {CanvasTool::object_selection, "对象选择 W", "",
         "<rect x='2' y='2' width='18' height='18' stroke-dasharray='3 2'/><path d='m11 10 2 13 3-5 6-1Z'/>"},
        {CanvasTool::patch, "修补工具 J", "",
         "<path d='M5 5h15v15H5Z M2 8h6 M2 14h6 M17 8h6 M17 14h6 M8 2v6 M14 2v6 M8 17v6 M14 "
         "17v6'/>"},
        {CanvasTool::text, "横排文字 T", "T", "<path d='M4 5V2h18v3 M13 2v21 M8 23h10'/>"},
        {CanvasTool::shape, "形状 U", "U", "<rect x='3' y='4' width='20' height='18' rx='3'/>"},
        {CanvasTool::bucket, "油漆桶 G", "G",
         "<path d='m3 11 9-9 9 9-9 9Z M5 4l8 8 M20 13q-5 7 0 7t0-7Z'/>"},
        {CanvasTool::eyedropper, "吸管 I", "I", "<path d='m3 21 2-6L17 3l4 4L9 19Z M14 4l6 6'/>"},
        {CanvasTool::clone, "仿制图章 S", "S",
         "<path d='M4 21h16v-5H4Z M8 16l2-6q-6-9 2-9t2 9l2 6'/>"},
        {CanvasTool::heal, "修复画笔 Shift+J", "Shift+J",
         "<path d='M4 19q-3-3 0-6L13 4q3-3 6 0t0 6l-9 9q-3 3-6 0Z M8 9l7 7 M10 12h2'/>"},
        {CanvasTool::spot, "污点修复 J", "J",
         "<path d='m3 17 12-12 5 5L8 22Z M8 12l5 5 M4 2v6 M1 5h6 M19 1v5 M17 3h4'/>"},
        {CanvasTool::blur, "模糊", "",
         "<path d='M12 2C10 8 4 11 4 16a8 8 0 0 0 16 0C20 11 14 8 12 2Z'/>"},
        {CanvasTool::sharpen, "锐化", "", "<path d='m12 3 9 18H3Z'/>"},
        {CanvasTool::dodge, "减淡 O", "O",
         "<ellipse cx='15' cy='8' rx='6' ry='5'/><path d='m10 12-7 10'/>"},
        {CanvasTool::burn, "加深 Shift+O", "Shift+O",
         "<path d='M6 20V9l3-3 2 3 3-5 3 2v5l4 2-4 8Z M8 14h8'/>"},
        {CanvasTool::sponge, "海绵", "",
         "<path d='M3 7q4-5 8-2t10 0v14q-5 4-9 0t-9 2Z M7 10h1 M15 9h1 M12 15h1 M6 17h1'/>"},
        {CanvasTool::crop, "裁剪 C", "C", "<path d='M6 1v17h17 M1 6h17v17 M9 3v12h12'/>"}};
    for (const auto& tool : tools) {
        const auto svg = QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 26 "
                                    "26'><g fill='none' stroke='#d5d9df' stroke-width='1.5' "
                                    "stroke-linecap='round' stroke-linejoin='round'>") +
                         tool.drawing + "</g></svg>";
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        QSvgRenderer renderer(svg);
        renderer.render(&painter);
        painter.end();
        pixmap.setDevicePixelRatio(2);
        auto* action = new QAction(QIcon(pixmap), QString::fromUtf8(tool.label), toolbar);
        action->setCheckable(true);
        action->setData(int(tool.tool));
        action->setProperty("canvasTool", int(tool.tool));
        action->setShortcutContext(Qt::WidgetShortcut);
        action->setProperty("defaultShortcut", tool.shortcut);
        action->setShortcut(QKeySequence(
            QSettings().value("shortcuts/" + action->text(), tool.shortcut).toString()));
        group->addAction(action);
        auto* button = new QToolButton(panel);
        button->setObjectName(QString("toolButton_%1").arg(int(tool.tool)));
        button->setDefaultAction(action);
        button->setAccessibleName(action->text());
        button->setIconSize(QSize(19, 19));
        button->setFixedSize(29, 29);
        connect(action, &QAction::triggered, this, [this, selected = tool.tool] {
            if (auto* c = canvas()) {
                c->set_tool(selected);
                c->setFocus();
            }
        });
    }
    // Reuse existing tool buttons and actions, replacing only the palette layout.
    while (auto* item = layout->takeAt(0)) {
        if (auto* widget = item->widget(); widget && widget->objectName().isEmpty())
            widget->deleteLater();
        delete item;
    }
    int row = 0;
    const QList<QList<int>> categories{{0, 24},          {2, 3, 4, 5, 28, 9, 10},  {7, 8, 14, 15, 13},
                                       {16, 17, 18, 27}, {19, 20, 21, 22, 23}, {25, 26, 6, 12},
                                       {1, 11}};
    for (const auto& category : categories) {
        int column = 0;
        for (const int index : category) {
            auto* button = panel->findChild<QToolButton*>(QString("toolButton_%1").arg(index));
            layout->addWidget(button, row, column);
            if (++column == 2) {
                column = 0;
                ++row;
            }
        }
        if (column)
            ++row;
        auto* separator = new QWidget(panel);
        separator->setFixedHeight(1);
        separator->setStyleSheet("background:#3b4147;");
        layout->addWidget(separator, row++, 0, 1, 2);
    }
    layout->addWidget(panel->findChild<QWidget*>("toolColors"), row, 0, 1, 2);
    auto* quick = new QAction(tr("快速蒙版 Q"), window_);
    quick->setObjectName("quickMaskAction");
    quick->setCheckable(true);
    quick->setShortcut(QKeySequence("Q"));
    quick->setProperty("defaultShortcut", "Q");
    window_->addAction(quick);
    auto* quick_button = new QToolButton(panel);
    quick_button->setObjectName("quickMaskButton");
    quick_button->setDefaultAction(quick);
    quick_button->setText("▣");
    quick_button->setFixedSize(29, 29);
    layout->addWidget(quick_button, row + 1, 0);
    connect(quick, &QAction::triggered, this, [this] {
        if (auto* c = canvas()) {
            c->toggle_quick_mask();
            c->setFocus();
        }
    });
    auto* screen_action = new QAction(tr("切换屏幕模式 F"), window_);
    screen_action->setShortcut(QKeySequence("F"));
    screen_action->setProperty("defaultShortcut", "F");
    window_->addAction(screen_action);
    auto* screen_button = new QToolButton(panel);
    screen_button->setObjectName("screenModeButton");
    screen_button->setDefaultAction(screen_action);
    screen_button->setText("▱");
    screen_button->setFixedSize(29, 29);
    layout->addWidget(screen_button, row + 1, 1);
    struct ScreenState {
        int mode{};
        QByteArray layout;
        bool maximized{};
    };
    const auto screen_state = std::make_shared<ScreenState>();
    connect(screen_action, &QAction::triggered, this, [this, screen_state] {
        auto& state = *screen_state;
        if (state.mode == 0) {
            state.layout = window_->saveState(1);
            state.maximized = window_->isMaximized();
            window_->showFullScreen();
            state.mode = 1;
        } else if (state.mode == 1) {
            window_->menuWidget()->hide();
            window_->statusBar()->hide();
            for (auto* bar : window_->findChildren<QToolBar*>())
                bar->hide();
            for (auto* dock : window_->findChildren<QDockWidget*>())
                dock->hide();
            state.mode = 2;
        } else {
            window_->menuWidget()->show();
            window_->statusBar()->show();
            window_->restoreState(state.layout, 1);
            if (state.maximized)
                window_->showMaximized();
            else
                window_->showNormal();
            state.mode = 0;
        }
        window_->setProperty("screenMode", state.mode);
        if (auto* c = canvas())
            c->setFocus();
    });
    for (auto* menu_action : window_->menus_->actions()) {
        if (menu_action->text().startsWith(tr("选择")))
            menu_action->menu()->addAction(quick);
        if (menu_action->text().startsWith(tr("视图")))
            menu_action->menu()->addAction(screen_action);
    }
    auto* options = window_->findChild<QToolBar*>("viewToolbar");
    auto* crop_options=new QWidget(options);auto* crop_layout=new QHBoxLayout(crop_options);crop_layout->setContentsMargins(0,0,0,0);
    auto* crop_ratio=new QComboBox(crop_options);crop_ratio->setObjectName("cropRatio");crop_ratio->addItems({tr("自由比例"),tr("原始比例"),"1 : 1","4 : 5","5 : 7","2 : 3","16 : 9",tr("自定比例")});crop_layout->addWidget(crop_ratio);
    auto* crop_w=new QDoubleSpinBox(crop_options);auto* crop_h=new QDoubleSpinBox(crop_options);crop_w->setObjectName("cropRatioWidth");crop_h->setObjectName("cropRatioHeight");
    for(auto* spin:{crop_w,crop_h}){spin->setRange(.01,10000);spin->setValue(1);spin->setMaximumWidth(85);spin->setKeyboardTracking(false);crop_layout->addWidget(spin);}
    auto* crop_swap=new QToolButton(crop_options);crop_swap->setObjectName("cropSwap");crop_swap->setText("⇄");crop_swap->setToolTip(tr("交换长宽比 (X)"));crop_layout->addWidget(crop_swap);
    const auto crop_custom=[this,crop_w,crop_h,crop_ratio]{if(auto* c=canvas()){const QSignalBlocker block(crop_ratio);crop_ratio->setCurrentIndex(7);c->set_crop_ratio(crop_w->value()/crop_h->value());}};
    connect(crop_w,&QDoubleSpinBox::valueChanged,this,[crop_custom]{crop_custom();});connect(crop_h,&QDoubleSpinBox::valueChanged,this,[crop_custom]{crop_custom();});
    connect(crop_ratio,&QComboBox::currentIndexChanged,this,[this,crop_w,crop_h](int i){if(auto* c=canvas()){const std::array<QSizeF,8> ratios{QSizeF(0,1),QSizeF(c->project().state().size),QSizeF(1,1),QSizeF(4,5),QSizeF(5,7),QSizeF(2,3),QSizeF(16,9),QSizeF(crop_w->value(),crop_h->value())};const QSignalBlocker a(crop_w),b(crop_h);if(i>0){crop_w->setValue(ratios[i].width());crop_h->setValue(ratios[i].height());}c->set_crop_ratio(ratios[i].width()/ratios[i].height());}});
    connect(crop_swap,&QToolButton::clicked,this,[crop_w,crop_h,crop_custom]{const double w=crop_w->value();{const QSignalBlocker a(crop_w),b(crop_h);crop_w->setValue(crop_h->value());crop_h->setValue(w);}crop_custom();});
    auto* crop_angle=new QDoubleSpinBox(crop_options);crop_angle->setObjectName("cropAngle");crop_angle->setRange(-180,180);crop_angle->setDecimals(2);crop_angle->setPrefix(tr("角度 "));crop_angle->setKeyboardTracking(false);crop_layout->addWidget(crop_angle);connect(crop_angle,&QDoubleSpinBox::valueChanged,this,[this](double value){if(auto* c=canvas()){c->crop_angle=value;if(!c->cropping())c->set_crop_ratio(c->crop_ratio);c->update();}});
    auto* straighten=new QToolButton(crop_options);straighten->setObjectName("cropStraighten");straighten->setText(tr("拉直"));straighten->setCheckable(true);crop_layout->addWidget(straighten);connect(straighten,&QToolButton::toggled,this,[this](bool value){if(auto* c=canvas())c->crop_straighten=value;});
    auto* crop_reset=new QToolButton(crop_options);crop_reset->setObjectName("cropReset");crop_reset->setText(tr("重置"));crop_layout->addWidget(crop_reset);connect(crop_reset,&QToolButton::clicked,this,[this]{if(auto* c=canvas()){c->cancel_crop();refresh_tools();}});
    auto* crop_delete = new QCheckBox(tr("删除裁剪的像素"), crop_options);
    crop_delete->setObjectName("cropDeletePixels");
    crop_delete->setChecked(true);
    crop_delete->setToolTip(tr("关闭后保留画布外像素，可再次扩展裁剪框恢复。RAW源、文字和形状保持可编辑。"));
    crop_layout->addWidget(crop_delete);
    connect(crop_delete, &QCheckBox::toggled, this, [this](bool value) {
        if (auto* c = canvas()) c->crop_delete_pixels = value;
    });
    options->addWidget(crop_options)->setObjectName("cropOptions");
    auto* scope = new QComboBox(options);
    scope->setObjectName("photoSample");
    scope->addItems({tr("取样：当前图层"), tr("取样：当前及下方"), tr("取样：所有图层")});
    scope->setCurrentIndex(1);
    options->addWidget(scope)->setObjectName("photo_sample");
    connect(scope, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (auto* c = canvas())
            c->photo_brush.sample = static_cast<editing::SampleLayers>(index);
    });
    auto* tone_options=new QWidget(options);
    auto* tone_layout=new QHBoxLayout(tone_options);tone_layout->setContentsMargins(0,0,0,0);
    auto* tone_range=new QComboBox(tone_options);tone_range->setObjectName("photoToneRange");tone_range->addItems({tr("阴影"),tr("中间调"),tr("高光")});tone_range->setCurrentIndex(1);tone_layout->addWidget(tone_range);
    auto* protect=new QCheckBox(tr("保护色调"),tone_options);protect->setObjectName("photoProtectTones");protect->setChecked(true);tone_layout->addWidget(protect);
    options->addWidget(tone_options)->setObjectName("photoToneOptions");
    connect(tone_range,&QComboBox::currentIndexChanged,this,[this](int value){if(auto* c=canvas())c->photo_brush.tone_range=value;});
    connect(protect,&QCheckBox::toggled,this,[this](bool value){if(auto* c=canvas())c->photo_brush.protect_tones=value;});
    auto* strength=new QDoubleSpinBox(options);strength->setObjectName("photoStrength");strength->setRange(0,100);strength->setValue(50);strength->setSuffix(" %");
    options->addWidget(strength)->setObjectName("photoStrengthOption");connect(strength,&QDoubleSpinBox::valueChanged,this,[this](double value){if(auto* c=canvas())c->retouch_strength=value/100;});
    auto* sponge_options=new QWidget(options);auto* sponge_layout=new QHBoxLayout(sponge_options);sponge_layout->setContentsMargins(0,0,0,0);
    auto* sponge_mode=new QComboBox(sponge_options);sponge_mode->setObjectName("photoSpongeMode");sponge_mode->addItems({tr("降低饱和度"),tr("增加饱和度")});sponge_layout->addWidget(sponge_mode);
    auto* vibrance=new QCheckBox(tr("自然饱和度"),sponge_options);vibrance->setObjectName("photoSpongeVibrance");vibrance->setChecked(true);sponge_layout->addWidget(vibrance);
    options->addWidget(sponge_options)->setObjectName("photoSpongeOptions");
    connect(sponge_mode,&QComboBox::currentIndexChanged,this,[this](int value){if(auto* c=canvas())c->photo_brush.sponge_saturate=value==1;});
    connect(vibrance,&QCheckBox::toggled,this,[this](bool value){if(auto* c=canvas())c->photo_brush.sponge_vibrance=value;});
    auto* patch_options = new QWidget(options);
    auto* object_options = new QWidget(options);
    auto* object_layout = new QHBoxLayout(object_options);
    object_layout->setContentsMargins(0,0,0,0);
    auto* object_mode = new QComboBox(object_options);
    object_mode->setObjectName("objectMode");
    object_mode->addItems({tr("矩形"),tr("套索")});
    object_layout->addWidget(object_mode);
    auto* object_all = new QCheckBox(tr("对所有图层取样"),object_options);
    object_all->setObjectName("objectAllLayers");object_all->setChecked(true);
    object_layout->addWidget(object_all);
    auto* object_show = new QCheckBox(tr("显示所有对象"),object_options);
    object_show->setObjectName("objectShowAll");object_layout->addWidget(object_show);
    auto* refine = new QPushButton(tr("选择并遮住…"),object_options);
    object_layout->addWidget(refine);connect(refine,&QPushButton::clicked,this,[this]{show_selection_refine();});
    auto* object_status = new QLabel(object_options);
    object_status->setObjectName("objectStatus");object_layout->addWidget(object_status);
    auto* status_timer = new QTimer(object_status);status_timer->setInterval(100);
    connect(status_timer,&QTimer::timeout,this,[this,object_status]{
        if(auto* c=canvas();c && c->tool()==CanvasTool::object_selection)
            object_status->setText(c->property("objectStatus").toString());
    });status_timer->start();
    connect(object_mode,&QComboBox::currentIndexChanged,this,[this](int i){if(auto* c=canvas())c->object_mode=i;});
    connect(object_all,&QCheckBox::toggled,this,[this](bool value){if(auto* c=canvas()){c->object_all_layers=value;c->update_object_options();}});
    connect(object_show,&QCheckBox::toggled,this,[this](bool value){if(auto* c=canvas()){c->object_show_all=value;c->update_object_options();}});
    options->addWidget(object_options)->setObjectName("objectOptions");
    auto* patch_layout = new QHBoxLayout(patch_options);
    patch_layout->setContentsMargins(0, 0, 0, 0);
    auto* patch_source = new QRadioButton(tr("源"), patch_options);
    patch_source->setObjectName("patchSource");
    patch_source->setChecked(true);
    auto* patch_destination = new QRadioButton(tr("目标"), patch_options);
    patch_destination->setObjectName("patchDestination");
    patch_layout->addWidget(patch_source);
    patch_layout->addWidget(patch_destination);
    auto* adaptation = new QDoubleSpinBox(patch_options);
    adaptation->setObjectName("patchAdaptation");
    adaptation->setRange(0, 100);
    adaptation->setValue(100);
    adaptation->setPrefix(tr("颜色适应 "));
    adaptation->setSuffix(" %");
    patch_layout->addWidget(adaptation);
    options->addWidget(patch_options)->setObjectName("patchOptions");
    connect(patch_destination, &QRadioButton::toggled, this, [this](bool value) {
        if (auto* c = canvas())
            c->patch_destination = value;
    });
    connect(adaptation, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (auto* c = canvas())
            c->patch_color_adaptation = value / 100;
    });
    auto* aligned = new QCheckBox(tr("对齐取样"), options);
    aligned->setObjectName("photoAligned");
    aligned->setChecked(true);
    options->addWidget(aligned)->setObjectName("photo_aligned");
    connect(aligned, &QCheckBox::toggled, this, [this](bool value) {
        if (auto* c = canvas())
            c->clone_aligned = value;
    });
    auto* all = new QCheckBox(tr("所有图层"), options);
    auto* clone_dock = new QDockWidget(tr("仿制源"), window_);
    clone_dock->setObjectName("cloneSourceDock");
    auto* clone_panel = new QWidget(clone_dock);
    auto* clone_form = new QFormLayout(clone_panel);
    const auto clone_number = [this, clone_form, clone_panel](const QString& label, const char* name,
                                      double value, double low, double high, double editing::RetouchOptions::* member, double divisor) {
        auto* spin = new QDoubleSpinBox(clone_panel);
        spin->setObjectName(name); spin->setRange(low, high); spin->setValue(value);
        spin->setKeyboardTracking(false); clone_form->addRow(label, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member, divisor](double v) {
            if (auto* c = canvas()) { c->photo_brush.*member = v/divisor; c->update(); }
        });
    };
    clone_number(tr("宽度 %"), "cloneScaleX", 100, 1, 10000, &editing::RetouchOptions::source_scale_x, 100);
    clone_number(tr("高度 %"), "cloneScaleY", 100, 1, 10000, &editing::RetouchOptions::source_scale_y, 100);
    clone_number(tr("旋转 °"), "cloneRotation", 0, -180, 180, &editing::RetouchOptions::source_rotation, 1);
    for (const auto& [name, label, member] : {
             std::tuple{"cloneFlipX", tr("水平翻转"), &editing::RetouchOptions::source_flip_x},
             std::tuple{"cloneFlipY", tr("垂直翻转"), &editing::RetouchOptions::source_flip_y}}) {
        auto* check = new QCheckBox(label, clone_panel); check->setObjectName(name); clone_form->addRow(check);
        connect(check, &QCheckBox::toggled, this, [this, member](bool v) { if (auto* c=canvas()) { c->photo_brush.*member=v; c->update(); } });
    }
    auto* overlay = new QCheckBox(tr("显示叠加预览"), clone_panel);
    overlay->setObjectName("cloneOverlay"); overlay->setChecked(true); clone_form->addRow(overlay);
    connect(overlay, &QCheckBox::toggled, this, [this](bool v) { if(auto* c=canvas()){ c->clone_overlay=v; c->update(); } });
    auto* opacity = new QDoubleSpinBox(clone_panel); opacity->setObjectName("cloneOverlayOpacity");
    opacity->setRange(0,100); opacity->setValue(50); clone_form->addRow(tr("叠加不透明度 %"), opacity);
    connect(opacity,&QDoubleSpinBox::valueChanged,this,[this](double v){if(auto* c=canvas()){c->clone_overlay_opacity=v/100;c->update();}});
    clone_form->addRow(new QLabel(tr("Alt 点击设置取样点；预览仅显示在笔尖内。"),clone_panel));
    clone_dock->setWidget(clone_panel); window_->addDockWidget(Qt::RightDockWidgetArea,clone_dock); clone_dock->hide();
    auto* clone_open = new QToolButton(options); clone_open->setText(tr("仿制源")); clone_open->setObjectName("cloneSourceOpen");
    options->addWidget(clone_open)->setObjectName("cloneSourceOption");
    connect(clone_open,&QToolButton::clicked,this,[clone_dock]{clone_dock->show();clone_dock->raise();});
    all->setObjectName("photoAllLayers");
    options->addWidget(all)->setObjectName("photo_allLayers");
    connect(all, &QCheckBox::toggled, this, [this](bool value) {
        if (auto* c = canvas())
            c->sample_all_layers = value;
    });
    auto* image_menu = new QMenu(tr("图像(&I)"), window_);
    window_->menus_->insertMenu(window_->menus_->actions().at(2), image_menu);
    auto* filters = new QMenu(tr("滤镜(&T)"), window_);
    window_->menus_->insertMenu(window_->menus_->actions().at(5), filters);
    for (auto* menu : window_->findChildren<QMenu*>())
        for (auto* action : menu->actions())
            if (action->text() == tr("智能效果…")) {
                menu->removeAction(action);
                filters->addAction(action);
                break;
            }
    auto* portrait = filters->addAction(tr("人像精修…"));
    portrait->setObjectName("portraitAction");
    connect(portrait, &QAction::triggered, this, [this] { guarded([&] { show_portrait(); }); });
    auto* liquify = filters->addAction(tr("液化…"));
    auto* blur_menu=filters->addMenu(tr("模糊"));
    auto* gaussian=blur_menu->addAction(tr("高斯模糊…"));
    connect(gaussian,&QAction::triggered,this,[this]{guarded([&]{show_effects(true);});});
    liquify->setShortcut(QKeySequence("Ctrl+Shift+X"));
    connect(liquify, &QAction::triggered, this, [this] { guarded([&] { show_liquify(); }); });
    auto* adjustments = image_menu->addMenu(tr("调整"));
    auto* fill_menu = window_->menus_->actions().at(1)->menu();
    auto* content_fill = fill_menu->addAction(tr("内容识别填充…"));
    connect(content_fill, &QAction::triggered, this, &EditorController::show_content_fill);
    const QStringList tone_names{tr("曝光与光线…"), tr("色彩平衡…"),    tr("色阶…"),
                                 tr("曲线…"),       tr("色相/饱和度…"), tr("色彩分级…")};
    for (int i = 0; i < tone_names.size(); ++i) {
        auto* action = adjustments->addAction(tone_names[i]);
        if (i == 2)
            action->setShortcut(QKeySequence("Ctrl+L"));
        if (i == 3)
            action->setShortcut(QKeySequence("Ctrl+M"));
        if (i == 4)
            action->setShortcut(QKeySequence("Ctrl+U"));
        connect(action, &QAction::triggered, this, [this, i] { guarded([&] { show_tone(i); }); });
    }
    auto* raw = image_menu->addAction(tr("编辑 RAW…"));
    connect(raw, &QAction::triggered, this, [this] {
        guarded([&] {
            auto* c = canvas();
            if (!c)
                return;
            const auto* layer = editing::find_layer(c->project().state().layers, c->active_layer);
            if (!layer || layer->raw_source.isEmpty())
                throw editing::EditError(tr("当前图层没有 RAW 源，请先打开 RAW 文件"));
            if (editing::layer_locked(c->project().state().layers, layer->id))
                throw editing::EditError(tr("当前图层已锁定"));
            const auto id = layer->id;
            const QPointer<CanvasView> document(c);
            const auto revision=c->project().revision();
            RawDialog dialog({*layer}, window_);
            if (dialog.exec() == QDialog::Accepted) {
                if(!document || document->project().revision()!=revision) {
                    const auto result=dialog.results().front();
                    CanvasSpec spec;spec.name=(result.name+tr(" · RAW 显影副本")).toStdString();
                    spec.width=result.pixels.width();spec.height=result.pixels.height();
                    if(!window_->create_document(spec))throw editing::EditError(tr("无法创建 RAW 结果副本"));
                    canvas()->edit(tr("RAW 显影副本"),[&](editing::State& state){state.layers={result};state.metadata=result.source_metadata;});
                    refresh();return;
                }
                document->edit(tr("RAW 显影"), [&](editing::State& state) {
                    auto* target=editing::find_layer(state.layers,id);
                    if(!target) throw editing::EditError(tr("RAW 原图层已删除"));
                    *target=dialog.results().front();
                });
            }
        });
    });
    auto* crop = image_menu->addAction(tr("裁剪到选区"));
    connect(crop, &QAction::triggered, this, [this] {
        guarded([&] {
            if (auto* c = canvas()) {
                const auto& range = c->project().state().selection;
                if (range.isNull())
                    throw editing::EditError(tr("请先建立选区，或使用裁剪工具 C"));
                QRect bounds;
                for (int y = 0; y < range.height(); ++y)
                    for (int x = 0; x < range.width(); ++x)
                        if (range.constScanLine(y)[x])
                            bounds = bounds.united(QRect(x, y, 1, 1));
                c->edit(tr("裁剪"),
                        [&](editing::State& state) { editing::crop_canvas(state, bounds); });
                c->fit_to_view();
            }
        });
    });
    auto* edit_menu = window_->menus_->actions().at(1)->menu();
    auto* fill = edit_menu->addAction(tr("填充…"));
    fill->setShortcut(QKeySequence("Shift+F5"));
    connect(fill, &QAction::triggered, this, [this] {
        guarded([&] {
            auto* c = canvas();
            if (!c)
                return;
            QDialog dialog(window_);
            dialog.setWindowTitle(tr("填充"));
            QFormLayout form(&dialog);
            QComboBox content;
            content.addItems({tr("前景色"), tr("背景色"), tr("黑色"), tr("白色")});
            form.addRow(tr("内容"), &content);
            QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            form.addRow(&buttons);
            connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            const QColor colors[]{c->foreground, c->background, Qt::black, Qt::white};
            c->edit(tr("填充"), [&](editing::State& state) {
                editing::fill_color(state, c->active_layer, QPoint(),
                                    colors[content.currentIndex()], 255, false, false,
                                    c->brush_opacity);
            });
        });
    });
}
} // namespace huelith::desktop
