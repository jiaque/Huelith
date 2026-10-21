#include "portrait_dialog.h"
#include "portrait_icons.h"
#include "canvas_preferences.h"
#include "portrait_controls.h"
#include "color_band_slider.h"
#include "raw_section_header.h"
#include "raw_preview_view.h"
#include "window_header.h"
#include "portrait_liquify.h"
#include "huelith/editing/liquify.h"
#include "huelith/editing/project.h"
#include "huelith/editing/portrait_effects.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QGraphicsPixmapItem>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPainter>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QInputDialog>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPainterPath>
#include <QTabWidget>
#include <QTabBar>
#include <QButtonGroup>
#include <QToolButton>
#include <QScrollArea>
#include <QMenuBar>
#include <QMenu>
#include <QFile>
#include <QFileDialog>
#include <QShortcut>
#include <QScrollBar>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QColorDialog>
#include <QSettings>
#include <QStyle>
#include <QLineEdit>

namespace huelith::desktop {
namespace {
QString feature_side_group(const QString& key){
    if(key.startsWith("feature.eye"))return "eyes";
    if(key.startsWith("feature.brow"))return "brows";
    if(key=="feature.smile")return "mouth";
    return {};
}
int parameter_side(const QJsonObject& settings,const QString& key){
    const auto group=feature_side_group(key);
    return group.isEmpty()?(settings["linked"].toBool(true)?0:settings["side"].toInt()+1):settings["featureSides"].toObject()[group].toInt();
}
double parameter_display_value(const QString& key,double value){return key=="head.size"?-value:value;}
double displayed_parameter(const QJsonObject& settings,const QString& key,const QString& suffix,QSize size){
    const auto params=settings["parameters"].toObject();
    if(key=="body.chest" && settings["chestContourPaths"].toObject().isEmpty() && params.contains(key)){
        const auto path=settings["chestContourPath"].toArray(),pose=settings["pose"].toArray();
        const auto point=path.size()==3?path[1].toArray():settings["chestContour"].toArray();
        if(point.size()==2 && pose.size()==33){
            const auto pixel=[size](QJsonArray p){return QPointF(p[0].toDouble()*size.width(),p[1].toDouble()*size.height());};
            const auto left=pixel(pose[11].toArray()),right=pixel(pose[12].toArray());
            const QString actual=QPointF::dotProduct(pixel(point)-(left+right)*.5,left-right)>=0?".left":".right";
            return suffix==actual?params[key].toDouble():0;
        }
    }
    return params[key+suffix].toDouble();
}
// Interpolate the user's landmark correction over the measured surface, retaining
// measured depth. Unchanged anchors constrain the field; no second recognition
// may replace the user's corrected geometry.
QJsonObject corrected_surface(QJsonObject surface,const QJsonArray& before,const QJsonArray& after,QSize size){
    auto points=surface["points"].toArray();
    if(points.size()!=468 || before.size()!=after.size() || before.isEmpty())return surface;
    const double aspect=double(size.height())/std::max(1,size.width());
    for(int j=0;j<points.size();++j){auto p=points[j].toArray();double sum=0,dx=0,dy=0;
        for(int i=0;i<before.size();++i){const auto a=before[i].toArray(),b=after[i].toArray();
            const double x=p[0].toDouble()-a[0].toDouble(),y=(p[1].toDouble()-a[1].toDouble())*aspect;
            const double weight=1/std::pow(std::max(1e-12,x*x+y*y),2);
            sum+=weight;dx+=weight*(b[0].toDouble()-a[0].toDouble());dy+=weight*(b[1].toDouble()-a[1].toDouble());
        }
        p[0]=std::clamp(p[0].toDouble()+dx/sum,0.,1.);p[1]=std::clamp(p[1].toDouble()+dy/sum,0.,1.);points[j]=p;
    }
    surface["points"]=points;surface["manuallyCorrected"]=true;return surface;
}
QString range_storage(const QJsonObject& settings,const QString& key){return (key=="skin.smooth" || key=="skin.bodySmooth") && settings["smoothManual"].toObject()[key].toBool()?QStringLiteral("smoothManualMasks"):QStringLiteral("masks");}
// A type eraser edits only existing strokes. Future strokes remain paintable.
void store_range(QJsonObject& settings,const QString& key,const QImage& mask){
    if(key.startsWith("wrinkle.erase.")){
        const bool lighting=key.endsWith("lighting");auto masks=settings["masks"].toObject();
        for(const auto& entry:settings["repairs"].toArray()){const auto stroke=entry.toObject();if(stroke["kind"].toString()!="wrinkle" || (stroke["wrinkleMethod"].toString()=="lighting-v2")!=lighting)continue;
            const auto name="repair.item."+stroke["id"].toString();auto coverage=QImage::fromData(QByteArray::fromBase64(masks[name].toString().toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8);
            if(coverage.isNull()){coverage=QImage(mask.size(),QImage::Format_Grayscale8);coverage.fill(255);}else coverage=coverage.scaled(mask.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            for(int y=0;y<mask.height();++y)for(int x=0;x<mask.width();++x)coverage.scanLine(y)[x]=uchar((int(coverage.constScanLine(y)[x])*(255-mask.constScanLine(y)[x])+127)/255);
            masks[name]=editing::portrait_mask_png(coverage);
        }settings["masks"]=masks;return;
    }
    if(key=="__protection")settings["protection"]=editing::portrait_mask_png(mask);
    else{const auto storage=range_storage(settings,key);auto masks=settings[storage].toObject();masks[key]=editing::portrait_mask_png(mask);if(key.startsWith("eye.") && key.count('.')==1){masks.remove(key+".left");masks.remove(key+".right");}settings[storage]=masks;}
}
QByteArray image_hash(const QImage& image) {
    QCryptographicHash h(QCryptographicHash::Sha256);
    for(int y=0;y<image.height();++y)h.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)),image.bytesPerLine()));
    return h.result().toHex();
}
}
PortraitDialog::PortraitDialog(QImage source,QJsonObject saved,QWidget* parent,std::function<QImage(const QImage&)> prepare):QDialog(parent),source_(std::move(source)),saved_recipe_(std::move(saved)) {
    const int font_sizes[]{10,11,13,15};
    const int font_pixels=font_sizes[std::clamp(read_canvas_preferences().interface_font,0,3)];
    QFont workspace_font("Microsoft YaHei UI");workspace_font.setPixelSize(font_pixels);setFont(workspace_font);
    setObjectName("portraitDialog");setWindowTitle(tr("Huelith · 人像精修"));resize(1600,1000);setMinimumSize(1100,720);
    setWindowFlags(Qt::Window|Qt::FramelessWindowHint|Qt::WindowMinMaxButtonsHint);
    preparation_=std::async(std::launch::async,[image=source_,prepare,saved=saved_recipe_]{
        PreparedSource result;result.source=prepare?prepare(image):image;
        result.linear=std::max(result.source.width(),result.source.height())>1600?result.source.scaled(1600,1600,Qt::KeepAspectRatio,Qt::SmoothTransformation):result.source;
        result.fast=result.linear;
        result.display=editing::display_image(result.linear);result.hash=image_hash(result.display);result.entry=result.display;if(!saved.isEmpty() && saved["sourceHash"].toString().toLatin1()==result.hash){editing::Effect effect;effect.portrait=saved;result.entry=editing::display_image(editing::apply_effects(result.linear,{effect}));}return result;
    });
    preview_=QImage(source_.size().scaled(1600,1600,Qt::KeepAspectRatio),QImage::Format_RGB32);preview_.fill(QColor(30,34,37));adjusted_preview_=preview_;
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    setStyleSheet(QString("QDialog#portraitDialog{background:#24292d;color:#e5e9ed;} QDialog#portraitDialog QWidget{font-family:Microsoft YaHei UI;font-size:%1px;} QListWidget#portraitPeople{border:0;background:transparent;} QDialog#portraitDialog QLabel{color:#e5e9ed;} QTabBar::tab{min-height:30px;padding:0 12px;} QTabBar::tab:selected{color:#2f80ed;border-bottom:2px solid #2f80ed;} QSlider::groove:horizontal{height:2px;background:#69727c;} QSlider::handle:horizontal{width:10px;height:10px;margin:-5px 0;background:#e5e9ed;border-radius:2px;}").arg(font_pixels));
    auto* menu=new QMenuBar(this);menu->addAction(tr("Huelith  |  人像精修"))->setEnabled(false);auto* header=new WindowHeader(menu,this);header->setFixedHeight(40);layout->addWidget(header);
    auto* people_bar=new QWidget(this);people_bar->setObjectName("portraitPeopleBar");people_bar->setFixedHeight(52);auto* top=new QHBoxLayout(people_bar);top->setContentsMargins(12,0,24,0);
    faces_=new QListWidget(this);faces_->setObjectName("portraitPeople");faces_->setViewMode(QListView::ListMode);
    faces_->setFlow(QListView::LeftToRight);faces_->setWrapping(false);faces_->setMovement(QListView::Static);
    faces_->setSelectionMode(QAbstractItemView::SingleSelection);faces_->setIconSize({40,40});faces_->setGridSize({112,50});faces_->setFixedHeight(50);faces_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    top->addWidget(faces_,1);faces_->setToolTip(tr("单击选人 · 双击定位"));
    detect_=new QPushButton(tr("重新识别"),this);detect_->setObjectName("portraitDetect");detect_->hide();auto* portrait_menu=menu->addMenu(tr("预设"));portrait_menu->addAction(tr("重新识别"),this,[this]{analyze();});portrait_menu->addAction(tr("管理预设"),this,[this]{navigation_->setCurrentRow(15);});
    undo_=new QPushButton(tr("撤销"),this);undo_->setObjectName("portraitUndo");redo_=new QPushButton(tr("重做"),this);redo_->setObjectName("portraitRedo");top->addWidget(undo_);top->addWidget(redo_);
    compare_=new QToolButton(this);compare_->setText(tr("按住比较"));compare_->setObjectName("portraitCompare");top->addWidget(compare_);layout->addWidget(people_bar);
    connect(undo_,&QPushButton::clicked,this,[this]{history_step(-1);});connect(redo_,&QPushButton::clicked,this,[this]{history_step(1);});
    auto* undo_key=new QShortcut(QKeySequence::Undo,this);connect(undo_key,&QShortcut::activated,this,[this]{history_step(-1);});
    auto* redo_key=new QShortcut(QKeySequence::Redo,this);connect(redo_key,&QShortcut::activated,this,[this]{history_step(1);});
    connect(compare_,&QToolButton::pressed,this,[this]{comparing_=true;detail_pixels_->hide();pixels_->setPixmap(QPixmap::fromImage(entry_preview_.isNull()?preview_:entry_preview_));overlay_->hide();range_pixels_->hide();update_detail();});
    connect(compare_,&QToolButton::released,this,[this]{comparing_=false;detail_pixels_->hide();pixels_->setPixmap(QPixmap::fromImage(locate_->isChecked()?preview_:adjusted_preview_));overlay_->show();update_range_overlay();update_detail();});
    auto* strength_row=new QHBoxLayout;strength_row->addWidget(new QLabel(tr("总强度"),this));
    strength_=new QSlider(Qt::Horizontal,this);strength_->setObjectName("portraitStrength");strength_->setRange(0,100);strength_->setValue(100);strength_->setMaximumWidth(200);bind_slider(strength_,100);
    strength_row->addWidget(strength_);auto* strength_value=new QLabel("100%",this);strength_value->setObjectName("portraitStrengthValue");strength_row->addWidget(strength_value);auto* restore_entry=new QPushButton(tr("恢复进入时设置"),this);restore_entry->setObjectName("portraitRestoreEntry");restore_entry->setToolTip(tr("恢复当前人物进入工作台时的参数、修复和范围；可撤销"));strength_row->addWidget(restore_entry);restore_entry->setText(tr("恢复进入时"));connect(restore_entry,&QPushButton::clicked,this,[this]{if(selected_<0)return;finish_preset(false);finish_gesture();auto& p=people_[std::size_t(selected_)];if(entry_settings_.contains(p.id)){p.settings=entry_settings_.value(p.id);remember();update_person();}});strength_row->addStretch();
    connect(strength_,&QSlider::valueChanged,this,[this,strength_value](int v){strength_value->setText(QString::number(v)+"%");if(selected_<0)return;people_[std::size_t(selected_)].settings["strength"]=v;remember();});
    preset_bar_=new QWidget(this);auto* preset_row=new QHBoxLayout(preset_bar_);preset_row->setContentsMargins(8,0,8,0);preset_row->addWidget(new QLabel(tr("组合预览 · 应用后可继续调整"),preset_bar_));
    for(bool apply:{true,false}){auto* b=new QPushButton(apply?tr("应用组合"):tr("放弃预览"),preset_bar_);b->setObjectName(apply?"portraitPresetApply":"portraitPresetDiscard");preset_row->addWidget(b);connect(b,&QPushButton::clicked,this,[this,apply]{finish_preset(apply);});}preset_row->addStretch();layout->addWidget(preset_bar_);preset_bar_->hide();
    auto* tools=new QHBoxLayout;
    for(const auto& entry:std::vector<std::pair<QString,int>>{{tr("抓手"),0},{tr("放大 +"),1},{tr("缩小 −"),2}}){auto* button=new QPushButton(entry.first,this);button->setObjectName(QString("portraitTool%1").arg(entry.second));tools->addWidget(button);connect(button,&QPushButton::clicked,this,[this,tool=entry.second]{placement_key_.clear();update_overlay();finish_liquify();liquify_tool_=-1;range_mode_=0;repair_mode_=0;repair_selected_=-1;color_sampling_=false;locate_->setChecked(false);view_->set_tool(tool);});}
    context_=new QLabel(tr("请选择人物"),this);context_->setObjectName("portraitContext");tools->addWidget(context_,1);auto* navigation_tools=new QWidget(this);navigation_tools->setLayout(tools);navigation_tools->hide();
    auto* row=new QHBoxLayout;row->setSpacing(0);
    navigation_=new QListWidget(this);navigation_->setObjectName("portraitNavigation");navigation_->setFixedWidth(84);navigation_->setIconSize({20,20});
    const QStringList panels{tr("面部"),tr("五官"),tr("头部"),tr("身材"),tr("液化"),tr("肤质"),tr("肤色"),tr("瑕疵"),tr("去皱"),tr("眼周"),tr("牙齿"),tr("美妆"),tr("头发"),tr("修容"),tr("补光"),tr("预设")};
    for(const auto& panel:panels)new QListWidgetItem(panel,navigation_);
    navigation_->hide();
    auto* tasks=new QListWidget(this);tasks->setObjectName("portraitTasks");tasks->setFixedWidth(224);tasks->setIconSize({28,28});tasks->setItemDelegate(new PortraitNavigationDelegate(tasks));tasks->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QList<QList<int>> task_panels{{0},{12},{11},{0,1,2},{5,6},{7},{8},{5},{3},{4},{7},{2},{13,14},{10},{9}};
    const QStringList task_names{tr("丰盈提拉"),tr("美发"),tr("美妆"),tr("面部重塑"),tr("磨皮"),tr("祛斑祛痘"),tr("祛皱"),tr("去油光"),tr("身材塑性"),tr("瘦脸瘦身"),tr("消除"),tr("小头"),tr("修容笔"),tr("牙齿"),tr("眼部精修")};
    const QList<int> task_order{3,11,0,8,9,5,6,4,7,14,13,2,1,12,10};
    for(int rowIndex=0;rowIndex<task_order.size();++rowIndex){const int i=task_order[rowIndex];auto* item=new QListWidgetItem(portrait_task_icon(i),task_names[i],tasks);const bool divider=rowIndex==5 || rowIndex==11 || rowIndex==14;item->setData(Qt::UserRole,i);item->setData(Qt::UserRole+1,divider);item->setSizeHint({224,divider?48:40});item->setToolTip(task_names[i]);}
    tasks->setStyleSheet("QListWidget{border:0;background:#252a2e;} QListWidget::item{padding:0 24px;} QListWidget::item:selected{background:#2f80ed;color:white;}");row->addWidget(tasks);

    scene_=new QGraphicsScene(this);pixels_=scene_->addPixmap(QPixmap::fromImage(preview_));scene_->setSceneRect(preview_.rect());
    view_=new RawPreviewView(scene_,this);view_->setObjectName("portraitView");view_->set_tool(0);row->addWidget(view_,1);
    view_->installEventFilter(this);view_->viewport()->installEventFilter(this);view_->viewport()->setMouseTracking(true);overlay_=scene_->addPath({});overlay_->setZValue(10);overlay_->setData(0,QStringLiteral("portraitCursor"));QPen overlay_pen(QColor(66,182,255));overlay_pen.setCosmetic(true);overlay_->setPen(overlay_pen);
    detail_pixels_=scene_->addPixmap({});detail_pixels_->setZValue(1);range_pixels_=scene_->addPixmap({});range_pixels_->setZValue(5);
    auto* inspector=new QWidget(this);inspector->setObjectName("portraitInspector");inspector->setFixedWidth(448);auto* inspector_layout=new QVBoxLayout(inspector);inspector_layout->setContentsMargins(16,8,16,8);inspector_layout->setSpacing(4);
    auto* heading=new QLabel(this);heading->setObjectName("portraitTaskTitle");heading->setStyleSheet("font-size:18px;font-weight:600;padding:2px 0;");inspector_layout->addWidget(heading);
    auto* tabs=new QTabBar(this);tabs->setMaximumWidth(400);tabs->setObjectName("portraitTaskTabs");tabs->setExpanding(true);inspector_layout->addWidget(tabs);
    const auto sync_task=[this,tasks,tabs,heading,task_panels,task_names,task_order](int panel){
        int task=property("portraitWorkspaceTask").toInt();
        if(task<0 || task>=task_panels.size() || !task_panels[task].contains(panel)){const int defaults[]{3,3,3,8,9,4,4,5,6,14,13,2,1,12,12,-1};task=panel>=0 && panel<16?defaults[panel]:-1;}
        if(task<0){heading->setText(tr("预设"));return;}
        setProperty("portraitWorkspaceTask",task);setProperty(qPrintable(QString("portraitTaskPanel%1").arg(task)),panel);
        const QSignalBlocker a(tasks),b(tabs);tasks->setCurrentRow(task_order.indexOf(task));heading->setText(task_names[task]);
        while(tabs->count())tabs->removeTab(0);
        for(int value:task_panels[task]){const auto label=task==3?(value==0?tr("脸型"):value==1?tr("五官"):tr("比例")):task==4?(value==5?tr("自动"):tr("肤色")):task==14?(value==9?tr("修复"):value==1?tr("眼形"):tr("眼妆")):navigation_->item(value)->text();tabs->addTab(label);tabs->setTabData(tabs->count()-1,value);}
        tabs->setCurrentIndex(task_panels[task].indexOf(panel));tabs->setVisible(tabs->count()>1);
    };
    connect(tasks,&QListWidget::currentRowChanged,this,[this,task_panels,sync_task,task_order](int taskRow){if(taskRow<0)return;const int task=task_order[taskRow];finish_brush_gesture();finish_gesture();setProperty("portraitWorkspaceTask",task);const auto remembered=property(qPrintable(QString("portraitTaskPanel%1").arg(task)));const int panel=remembered.isValid() && task_panels[task].contains(remembered.toInt())?remembered.toInt():task_panels[task][0];if(navigation_->currentRow()==panel){sync_task(panel);change_panel(panel);}else navigation_->setCurrentRow(panel);});
    connect(tabs,&QTabBar::currentChanged,this,[this,tabs](int tab){if(tab>=0)navigation_->setCurrentRow(tabs->tabData(tab).toInt());});
    connect(navigation_,&QListWidget::currentRowChanged,this,sync_task);

    details_=new QLabel(tr("选择人物后显示定位与范围"),this);details_->setObjectName("portraitDetails");details_->setWordWrap(true);details_->setAlignment(Qt::AlignTop);details_->setFixedHeight(36);inspector_layout->addWidget(details_);
    side_=new QComboBox(this);side_->setObjectName("portraitSide");side_->addItems({tr("人物左侧"),tr("人物右侧")});
    linked_=new QCheckBox(tr("左右联动"),this);linked_->setObjectName("portraitLinked");linked_->setChecked(true);
    linked_->hide();side_->hide();
    auto* sides=new QWidget(inspector);sides->setObjectName("portraitSides");auto* sides_layout=new QHBoxLayout(sides);sides_layout->setContentsMargins(0,0,0,0);auto* side_group=new QButtonGroup(sides);side_group->setExclusive(true);
    for(int i=0;i<3;++i){auto* b=new QPushButton(i==0?tr("双侧"):i==1?tr("人物左侧"):tr("人物右侧"),sides);b->setObjectName(QString("portraitSideChoice%1").arg(i));b->setCheckable(true);b->setStyleSheet("QPushButton:checked{background:#31546b;border:1px solid #5796bd;}");b->setChecked(i==0);b->setToolTip(i==0?tr("切换不改变已有左右值；下次调整将两侧设为相同值，各侧定位与遮挡独立"):tr("只调整人物自身的这一侧"));side_group->addButton(b,i);sides_layout->addWidget(b);connect(b,&QPushButton::clicked,this,[this,i]{linked_->setChecked(i==0);if(i)side_->setCurrentIndex(i-1);build_parameters();});}inspector_layout->addWidget(sides);

    locate_=new QCheckBox(tr("显示／调整关键点"),this);locate_->setObjectName("portraitLocate");locate_->setToolTip(tr("拖动关键点修正定位，不修改原图像素"));auto* locate_header=new QToolButton(inspector);locate_header->setText(tr("定位修正"));locate_header->setArrowType(Qt::RightArrow);locate_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);locate_header->setCheckable(true);locate_->hide();connect(locate_header,&QToolButton::toggled,this,[this,locate_header](bool v){locate_->setVisible(v);locate_header->setArrowType(v?Qt::DownArrow:Qt::RightArrow);if(!v)locate_->setChecked(false);});connect(locate_,&QCheckBox::toggled,this,[this]{detail_pixels_->hide();pixels_->setPixmap(QPixmap::fromImage(locate_->isChecked()?preview_:adjusted_preview_));update_overlay();update_range_overlay();update_detail();if(locate_->isChecked())status_->setText(tr("原图定位模式：拖动关键点修正位置；关闭后查看效果"));});
    confirm_=new QPushButton(tr("确认人物关联…"),this);confirm_->setObjectName("portraitConfirm");inspector_layout->addWidget(confirm_);
    inspector_tabs_=new QTabWidget(this);inspector_tabs_->setObjectName("portraitInspectorTabs");
    auto* parameter_scroll=new QScrollArea(this);parameter_scroll->setObjectName("portraitParameterScroll");parameter_scroll->setStyleSheet("QScrollBar:vertical{width:8px;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");parameter_scroll->setFrameShape(QFrame::NoFrame);parameter_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);parameter_scroll->setWidgetResizable(true);parameter_host_=new QWidget(this);parameter_layout_=new QVBoxLayout(parameter_host_);parameter_layout_->setContentsMargins(0,0,0,0);parameter_layout_->setSpacing(4);parameter_scroll->setWidget(parameter_host_);
    panel_description_=new QLabel(this);panel_description_->setWordWrap(true);panel_description_->setAlignment(Qt::AlignTop);parameter_layout_->addWidget(panel_description_);inspector_layout->addWidget(parameter_scroll,1);inspector_tabs_->addTab(new QWidget(this),tr("效果"));inspector_tabs_->setTabVisible(0,false);
    auto* range=new QWidget(this);auto* range_layout=new QVBoxLayout(range);range_key_=new QComboBox(range);range_key_->setObjectName("portraitRangeKey");range_layout->addWidget(range_key_);
    for(int mode=0;mode<3;++mode){auto* button=new QPushButton(mode==0?tr("结束涂选／抓手"):mode==1?tr("补选范围"):tr("减选范围"),range);button->setObjectName(QString("portraitRangeMode%1").arg(mode));range_layout->addWidget(button);connect(button,&QPushButton::clicked,this,[this,mode]{range_mode_=mode;repair_mode_=0;repair_selected_=-1;color_sampling_=false;locate_->setChecked(false);view_->set_tool(mode==0?0:-1);range_visible_->setChecked(mode!=0);status_->setText(mode==1?tr("补选当前效果范围"):mode==2?tr("减选当前效果范围，不擦除照片像素"):tr("抓手模式"));});}
    auto size_row=portrait_integer_row(range,tr("画笔大小"),"portraitBrushSize",1,500,80,tr(" px"));brush_size_=size_row.slider;range_layout->addWidget(size_row.host);
    auto feather_row=portrait_integer_row(range,tr("羽化"),"portraitBrushFeather",0,100,60);brush_feather_=feather_row.slider;range_layout->addWidget(feather_row.host);
    range_visible_=new QCheckBox(tr("显示范围覆盖色"),range);range_visible_->setObjectName("portraitRangeVisible");range_layout->addWidget(range_visible_);connect(range_visible_,&QCheckBox::toggled,this,[this]{update_range_overlay();});
    connect(range_key_,&QComboBox::currentIndexChanged,this,[this]{update_range_overlay();});
    auto* reset_range=new QPushButton(tr("恢复此项自动范围"),range);reset_range->setObjectName("portraitRangeReset");range_layout->addWidget(reset_range);connect(reset_range,&QPushButton::clicked,this,[this]{if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto masks=settings["masks"].toObject();masks.remove(range_key_->currentData().toString());settings["masks"]=masks;auto modes=settings["smoothManual"].toObject();modes.remove(range_key_->currentData().toString());settings["smoothManual"]=modes;remember();if(navigation_->currentRow()==5)build_parameters();update_range_overlay();});range_layout->addStretch();auto* range_scroll=new QScrollArea(this);range_scroll->setWidgetResizable(true);range_scroll->setWidget(range);inspector_tabs_->addTab(range_scroll,tr("范围"));
    auto* protect=new QWidget(this);auto* protect_layout=new QVBoxLayout(protect);auto* explanation=new QLabel(tr("其他人物始终受保护。冻结区只限制变形，与每个颜色效果的范围独立保存。"),protect);explanation->setWordWrap(true);protect_layout->addWidget(explanation);
    protect_background_=new QCheckBox(tr("保护背景直线"),protect);protect_background_->setObjectName("portraitProtectBackground");protect_background_->setChecked(true);protect_layout->addWidget(protect_background_);connect(protect_background_,&QCheckBox::toggled,this,[this](bool enabled){if(selected_<0)return;people_[std::size_t(selected_)].settings["protectBackground"]=enabled;remember();});
    for(int mode:{1,2}){auto* b=new QPushButton(mode==1?tr("涂抹冻结区"):tr("擦除冻结区"),protect);b->setObjectName(QString("portraitFreeze%1").arg(mode));protect_layout->addWidget(b);connect(b,&QPushButton::clicked,this,[this,mode]{if(range_key_->findData("__protection")<0)range_key_->addItem(tr("变形冻结区"),"__protection");range_key_->setCurrentIndex(range_key_->findData("__protection"));range_mode_=mode;repair_mode_=0;color_sampling_=false;locate_->setChecked(false);range_visible_->setChecked(true);view_->set_tool(-1);});}
    auto freeze_row=portrait_integer_row(protect,tr("画笔大小"),"portraitFreezeSize",1,500,80,tr(" px"));auto* freeze_size=freeze_row.slider;protect_layout->addWidget(freeze_row.host);connect(freeze_size,&QSlider::valueChanged,brush_size_,&QSlider::setValue);connect(brush_size_,&QSlider::valueChanged,freeze_size,&QSlider::setValue);
    auto* clear_freeze=new QPushButton(tr("清空变形冻结区"),protect);clear_freeze->setObjectName("portraitFreezeClear");protect_layout->addWidget(clear_freeze);connect(clear_freeze,&QPushButton::clicked,this,[this]{if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;settings=editing::portrait_capture_protection(source_.size(),recipe(),selected_);settings.remove("protection");auto mesh=editing::liquify_from_json(settings["liquify"].toObject());if(!mesh.isNull()){editing::freeze_liquify(mesh,0);settings["liquify"]=editing::liquify_json(mesh);}remember();update_range_overlay();});protect_layout->addStretch();auto* protect_scroll=new QScrollArea(this);protect_scroll->setWidgetResizable(true);protect_scroll->setWidget(protect);inspector_tabs_->addTab(protect_scroll,tr("保护区"));
    auto* scope_header=new QToolButton(inspector);scope_header->setText(tr("修正范围"));scope_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);scope_header->setArrowType(Qt::RightArrow);scope_header->setCheckable(true);scope_header->setObjectName("portraitScopeHeader");inspector_layout->addWidget(scope_header);
    inspector_tabs_->setCurrentIndex(1);inspector_tabs_->hide();
    connect(scope_header,&QToolButton::toggled,this,[this,scope_header,parameter_scroll](bool open){
        scope_header->setArrowType(open?Qt::LeftArrow:Qt::RightArrow);scope_header->setText(open?tr("返回调整"):tr("修正范围"));
        parameter_scroll->setVisible(!open);inspector_tabs_->setVisible(open);
        if(!open){finish_brush_gesture();range_mode_=0;range_visible_->setChecked(false);locate_->setChecked(false);view_->set_tool(0);activate_panel_tool();}
    });inspector_layout->addWidget(inspector_tabs_,1);
    auto* advanced=new QWidget(inspector);auto* advanced_layout=new QVBoxLayout(advanced);advanced_layout->setContentsMargins(0,0,0,0);advanced_layout->addWidget(locate_header);advanced_layout->addWidget(locate_);advanced_layout->addLayout(strength_row);advanced->hide();
    connect(scope_header,&QToolButton::toggled,advanced,&QWidget::setVisible);inspector_layout->addWidget(advanced);

    row->addWidget(inspector);layout->addLayout(row,1);
    connect(side_,&QComboBox::currentIndexChanged,this,[this](int value){if(selected_>=0){people_[std::size_t(selected_)].settings["side"]=value;build_parameters();refresh_ranges();}});
    linked_->setToolTip(tr("切换联动不改变已有左右值；联动调整时，两侧同名参数设为相同值，各自定位和遮挡仍独立。"));
    connect(linked_,&QCheckBox::toggled,this,[this](bool value){side_->setEnabled(!value && selected_>=0);if(selected_>=0){people_[std::size_t(selected_)].settings["linked"]=value;}});
    connect(confirm_,&QPushButton::clicked,this,[this]{
        QStringList choices{tr("当前人物为新人物，不继承其他设置")};std::vector<int> indices{-1};
        for(std::size_t i=0;i<people_.size();++i)if(int(i)!=selected_ && people_[i].unmatched){choices<<tr("继承待确认人物%1的设置").arg(i+1);indices.push_back(int(i));}
        bool ok=false;const auto chosen=QInputDialog::getItem(this,tr("确认人物关联"),tr("请根据头像确认设置归属；不会按左右顺序自动覆盖"),choices,0,false,&ok);
        if(ok)confirm_person(indices[std::size_t(choices.indexOf(chosen))]);
    });
    auto* bottom_bar=new QWidget(this);bottom_bar->setObjectName("portraitBottomBar");bottom_bar->setFixedHeight(52);auto* bottom=new QHBoxLayout(bottom_bar);bottom->setContentsMargins(16,6,16,6);
    const QStringList labels{tr("适合窗口"),tr("人脸"),tr("全身"),tr("100%")};
    for(int i=0;i<labels.size();++i){auto* button=new QPushButton(labels[i],this);button->setObjectName(QString("portraitFocus%1").arg(i));bottom->addWidget(button);button->setMinimumHeight(32);if(i==1 || i==2)button->hide();
        if(i==0){auto* focus_menu=new QMenu(button);for(int mode=0;mode<3;++mode)focus_menu->addAction(mode==0?tr("适合窗口"):mode==1?tr("定位人脸"):tr("定位全身"),this,[this,mode]{focus_person(mode);});button->setMenu(focus_menu);}
        connect(button,&QPushButton::clicked,this,[this,i]{focus_person(i);});}
    for(bool brush:{true,false}){auto* button=new QPushButton(brush?tr("画笔 B"):tr("抓手 H"),this);button->setObjectName(brush?"portraitCanvasBrush":"portraitCanvasHand");button->setToolTip(brush?tr("B：画笔；按住空格临时拖动画布"):tr("H：抓手；按 B 返回画笔"));button->hide();bottom->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,brush]{finish_brush_gesture();if(brush){activate_panel_tool();const int resume=property("portraitResumeRange").toInt();if(resume){range_mode_=resume;view_->set_tool(-1);update_range_overlay();}const auto place=property("portraitResumePlacement").toString();if(!place.isEmpty()){placement_key_=place;view_->set_tool(-1);update_overlay();}}else{setProperty("portraitResumeRange",range_mode_);setProperty("portraitResumePlacement",placement_key_);repair_mode_=range_mode_=0;repair_selected_=-1;placement_key_.clear();setProperty("portraitMaterialSampleKey",QString{});setProperty("portraitMakeupLocate",QString{});locate_->setChecked(false);overlay_->setPath({});view_->set_tool(0);}view_->setFocus();});}
    bottom->addStretch();accept_=new QPushButton(tr("确定"),this);accept_->setObjectName("portraitAccept");accept_->setMinimumSize(120,44);accept_->setStyleSheet("background:#2f80ed;color:white;border-radius:4px;");
    auto* cancel=new QPushButton(tr("取消"),this);cancel->setMinimumSize(120,44);bottom->addWidget(cancel);bottom->addWidget(accept_);
    auto* footer=new QHBoxLayout;status_=new QLabel(this);status_->setObjectName("portraitStatus");footer->addWidget(status_,1);
    cancel_compute_=new QPushButton(tr("取消识别"),this);cancel_compute_->setObjectName("portraitCancelCompute");footer->addWidget(cancel_compute_);auto* render_status=new QLabel(this);render_status->setObjectName("portraitRenderStatus");render_status->setFixedWidth(132);render_status->setFixedHeight(30);footer->addWidget(render_status);auto cancel_policy=cancel_compute_->sizePolicy();cancel_policy.setRetainSizeWhenHidden(true);cancel_compute_->setSizePolicy(cancel_policy);cancel_compute_->setFixedSize(32,30);status_->setMinimumWidth(0);status_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);status_->setFixedHeight(30);
    bottom->insertLayout(4,footer,1);layout->addWidget(bottom_bar);
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);connect(accept_,&QPushButton::clicked,this,&QDialog::accept);
    connect(detect_,&QPushButton::clicked,this,&PortraitDialog::analyze);
    connect(cancel_compute_,&QPushButton::clicked,this,[this]{volume_cancelled_=true;cancelled_=true;preview_cancelled_=true;preview_dirty_=false;preview_timer_.stop();detail_cancelled_=true;status_->setText(tr("已取消计算，保留当前设置"));});
    connect(faces_,&QListWidget::currentRowChanged,this,&PortraitDialog::select_person);
    connect(faces_,&QListWidget::itemDoubleClicked,this,[this]{focus_person(1);});
    detail_timer_.setSingleShot(true);detail_timer_.setInterval(60);connect(&detail_timer_,&QTimer::timeout,this,&PortraitDialog::update_detail);
    preview_timer_.setSingleShot(true);preview_timer_.setInterval(16);connect(&preview_timer_,&QTimer::timeout,this,&PortraitDialog::start_preview);
    view_->navigated=[this]{focus_mode_=0;detail_timer_.start();};view_->fit_requested=[this]{focus_person(0);};
    connect(view_->horizontalScrollBar(),&QScrollBar::valueChanged,this,[this]{detail_timer_.start();});connect(view_->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{detail_timer_.start();});
    connect(navigation_,&QListWidget::currentRowChanged,this,&PortraitDialog::change_panel);navigation_->setCurrentRow(0);
    poll_.setInterval(30);connect(&poll_,&QTimer::timeout,this,&PortraitDialog::poll);poll_.start();
    cancel_compute_->setEnabled(false);accept_->setEnabled(false);detect_->setEnabled(false);
    status_->setText(tr("正在准备图像…"));
}
void PortraitDialog::finish_preparation(){
    if(!preparing_ || preparation_.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
    try{
        auto ready=preparation_.get();source_=std::move(ready.source);linear_preview_=std::move(ready.linear);fast_preview_=std::move(ready.fast);preview_=std::move(ready.display);source_hash_=ready.hash;adjusted_preview_=preview_;preparing_=false;
        pixels_->setPixmap(QPixmap::fromImage(preview_));scene_->setSceneRect(preview_.rect());
        entry_preview_=std::move(ready.entry);entry_recipe_=saved_recipe_;const auto saved=std::move(saved_recipe_);
    const bool changed=!saved.isEmpty() && saved["sourceHash"].toString().toLatin1()!=source_hash_;
    if(!saved.isEmpty()) {
        for(const auto& entry:saved["people"].toArray()) {
            auto p=editing::portrait_person_from_json(entry.toObject());
            const auto mask_bytes=QByteArray::fromBase64(entry.toObject()["bodyMask"].toString().toLatin1());
            if(!mask_bytes.isEmpty())p.body_mask=QImage::fromData(mask_bytes,"PNG").convertToFormat(QImage::Format_Grayscale8);
            if(changed){p.unmatched=true;p.match_ambiguous=true;}
            entry_settings_.insert(p.id,p.settings);people_.push_back(std::move(p));
        }
        populate();
    }
    if(changed)status_->setText(tr("源图像已变化，请重新识别并核对原人物；旧效果未自动套用"));
        status_->setText(tr("就绪"));detect_->setEnabled(true);accept_->setEnabled(true);remember();schedule_preview();focus_person(0);if(people_.empty() || changed)analyze();
    }catch(const std::exception& error){preparing_=false;status_->setText(tr("图像准备失败：%1").arg(QString::fromUtf8(error.what())));}
}

PortraitDialog::~PortraitDialog(){
    // Child scroll bars can emit while QWidget tears down its layout. Disconnect
    // them before member timers and the session state have been destroyed.
    volume_cancelled_=true;if(volume_analysis_.valid())volume_analysis_.wait();
    if(preparation_.valid())preparation_.wait();
    poll_.stop();detail_timer_.stop();preview_timer_.stop();view_->navigated={};view_->fit_requested={};
    disconnect(view_->horizontalScrollBar(),nullptr,this,nullptr);disconnect(view_->verticalScrollBar(),nullptr,this,nullptr);
    disconnect(faces_,nullptr,this,nullptr);disconnect(navigation_,nullptr,this,nullptr);
    cancelled_=true;preview_cancelled_=true;detail_cancelled_=true;if(pending_.valid())pending_.wait();if(preview_pending_.valid())preview_pending_.wait();if(detail_pending_.valid())detail_pending_.wait();if(repair_analysis_.valid())repair_analysis_.wait();
}
void PortraitDialog::done(int result){volume_cancelled_=true;finish_liquify();finish_preset(result==QDialog::Accepted);finish_gesture();cancelled_=true;preview_cancelled_=true;detail_cancelled_=true;QDialog::done(result);}
void PortraitDialog::analyze() {
    if(preparing_ || busy_ || repair_analysis_busy_ || volume_analysis_busy_)return;cancelled_=false;cancel_compute_->setText(tr("取消识别"));busy_=true;detect_->setEnabled(false);accept_->setEnabled(false);cancel_compute_->setEnabled(true);cancel_compute_->show();
    status_->setText(tr("正在定位人物和身体范围…"));
    const auto image=preview_;const auto models=QDir(QCoreApplication::applicationDirPath()).filePath("models");
    // Test host is Python; production receives the application's model directory.
    const auto model_override=property("modelDirectory").toString();
    pending_=std::async(std::launch::async,[this,image,models,model_override]{return editing::analyze_portrait(image,model_override.isEmpty()?models:model_override,cancelled_);});
}
namespace { QStringList volume_part_keys(bool lift,int part); }
void PortraitDialog::poll() {
    if(liquify_bar_ && liquify_bar_->isVisible())liquify_bar_->move(view_->viewport()->width()-liquify_bar_->width()-12,12);
    if(parameters_rebuild_pending_ && !gesture_ && !painting_ && !repair_drawing_ && !repair_source_dragging_ && !liquify_drawing_)build_parameters();
    if(preparing_){finish_preparation();return;}
    for(int i=0;i<3;++i)if(auto* b=findChild<QPushButton*>(QString("portraitSideChoice%1").arg(i))){const QSignalBlocker block(b);b->setChecked(linked_->isChecked()?i==0:i==side_->currentIndex()+1);b->setEnabled(selected_>=0);}

    if(auto* label=findChild<QLabel*>("portraitRenderStatus"))label->setText(preview_busy_ || preview_dirty_?tr("预览中…"):preview_displayed_revision_==qint64(preview_revision_)?tr("预览已更新"):tr("预览未更新"));
    if(!busy_ && !repair_analysis_busy_ && !volume_analysis_busy_){cancel_compute_->setText(tr("×"));cancel_compute_->setToolTip(tr("取消当前计算，保留参数"));cancel_compute_->setVisible(preview_busy_ || preview_dirty_);cancel_compute_->setEnabled(preview_busy_ || preview_dirty_);}
    const auto checked_tool=[this](const QString& name,bool active){if(auto* b=findChild<QPushButton*>(name)){const QSignalBlocker block(b);if(!b->isCheckable()){b->setCheckable(true);b->setStyleSheet("QPushButton:checked {background:#31546b;border:1px solid #5796bd;}");}b->setChecked(active);}};
    for(int tool=0;tool<3;++tool)checked_tool(QString("portraitTool%1").arg(tool),view_->active_tool()==tool && !range_mode_ && !repair_mode_ && liquify_tool_<0);
    for(int mode=1;mode<=2;++mode){checked_tool(QString("portraitRangeMode%1").arg(mode),range_mode_==mode && range_key_->currentData().toString()!="__protection");checked_tool(QString("portraitFreeze%1").arg(mode),range_mode_==mode && range_key_->currentData().toString()=="__protection");}
    checked_tool("portraitLiquify",liquify_tool_>=0 && !range_mode_ && !locate_->isChecked() && view_->active_tool()==-1);checked_tool("portraitRepairBrush",repair_mode_==1 || repair_mode_==2 || repair_mode_==4);checked_tool("portraitMakeupRepair",repair_mode_==3);
    if(auto* header=findChild<QToolButton*>("portraitScopeHeader");header && !header->isHidden() && range_mode_ && property("portraitWorkspaceTask").toInt()!=9 && property("portraitWorkspaceTask").toInt()!=5 && property("portraitWorkspaceTask").toInt()!=6 && property("portraitWorkspaceTask").toInt()!=7 && property("portraitWorkspaceTask").toInt()!=8 && property("portraitWorkspaceTask").toInt()!=1 && property("portraitWorkspaceTask").toInt()!=12 && !(property("portraitWorkspaceTask").toInt()==4 && property("portraitSkinMode").toInt()==1))header->setChecked(true);
    if(volume_analysis_busy_ && volume_analysis_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
        volume_analysis_busy_=false;accept_->setEnabled(!busy_);detect_->setEnabled(!busy_);
        try{const auto result=volume_analysis_.get();
            if(!volume_cancelled_ && selected_>=0 && editing::portrait_person_json(people_[std::size_t(selected_)])==volume_person_before_ && !result.isEmpty()){
                auto& targetSettings=people_[std::size_t(selected_)].settings;
                targetSettings=result["settings"].toObject();
                const auto before=volume_person_before_["settings"].toObject()["parameters"].toObject();
                auto applied=targetSettings["parameters"].toObject();QJsonObject base,contribution,last;
                const auto keys=volume_part_keys(combination_lift_,0);
                for(auto it=applied.begin();it!=applied.end();++it)if(keys.contains(it.key()) || keys.contains(it.key().section('.',0,1))){base[it.key()]=before[it.key()].toDouble();contribution[it.key()]=it.value();last[it.key()]=before[it.key()].toDouble();applied[it.key()]=before[it.key()].toDouble();}
                targetSettings["parameters"]=applied;
                targetSettings[combination_lift_?"liftMix":"volumeMix"]=QJsonObject{{"base",base},{"target",contribution},{"last",last},{"amount",0}};
                const int desired=property("portraitVolumePendingAmount").isValid()?property("portraitVolumePendingAmount").toInt():100;
                setProperty("portraitVolumePendingAmount",QVariant{});
                auto mix=targetSettings[combination_lift_?"liftMix":"volumeMix"].toObject();
                for(auto it=contribution.begin();it!=contribution.end();++it){applied[it.key()]=std::clamp(base[it.key()].toDouble()+it.value().toDouble()*desired*.01,-100.,100.);last[it.key()]=applied[it.key()];}
                mix["amount"]=desired;mix["last"]=last;targetSettings[combination_lift_?"liftMix":"volumeMix"]=mix;targetSettings["parameters"]=applied;
                for(const auto& group:(combination_lift_?QStringList{tr("提拉")}:QStringList{tr("额头丰盈"),tr("泪沟丰盈"),tr("苹果肌"),tr("面颊丰盈")}))expanded_groups_["0_"+group]=true;
                remember();build_parameters();QStringList skipped;for(const auto& item:result["skipped"].toArray())skipped<<item.toString();
                status_->setText(skipped.isEmpty()?(combination_lift_?tr("已应用自然提拉，可逐项调整或重置"):tr("已应用自然丰盈，可展开逐区调整或重置")):tr("已应用可靠区域；跳过定位或皮肤不足区域：%1").arg(skipped.join(tr("、"))));
            }else{status_->setText(!volume_cancelled_ && result.isEmpty()?tr("未找到可靠的调整区域，请检查面部定位和皮肤范围；未修改原设置"):tr("组合分析已取消或编辑目标已变化，保留当前设置"));build_parameters();}
        }catch(const std::exception& error){status_->setText(QString::fromUtf8(error.what()));build_parameters();}
    }
    if(repair_analysis_busy_ && repair_analysis_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
        repair_analysis_busy_=false;if(auto* edge=findChild<QCheckBox*>("portraitHairRefineEdges"))edge->setEnabled(selected_>=0);if(auto* b=findChild<QPushButton*>("portraitPresetApply"))b->setEnabled(true);cancel_compute_->hide();detect_->setEnabled(!busy_);accept_->setEnabled(!busy_);
        const bool merge_analysis=!analysis_merge_recipe_.isEmpty() && analysis_merge_recipe_==recipe() && history_index_>=0;analysis_merge_recipe_={};
        try{const auto analysis=repair_analysis_.get();if(!cancelled_ && selected_>=0 && people_[std::size_t(selected_)].id==repair_analysis_person_ && people_[std::size_t(selected_)].settings.value("masks").toObject()==repair_analysis_masks_ && people_[std::size_t(selected_)].landmarks==repair_analysis_landmarks_){if(analysis.contains("nails")){apply_nail_analysis(analysis);}else{auto& settings=people_[std::size_t(selected_)].settings;auto masks=settings["automaticMasks"].toObject();for(const auto* key:{"teeth","eye.white","eye.deRed","eye.scleraSupport","eye.red","eye.circles","eye.bags","eyeglassFrame"})masks[key]=analysis[key];settings["automaticMasks"]=masks;if(repair_analysis_refresh_blemishes_){QJsonArray retained;for(const auto& value:settings["blemishes"].toArray()){const auto mark=value.toObject();if(mark["state"].toString()=="preserved" || mark["state"].toString()=="repaired")retained.append(mark);}
            for(const auto& value:analysis["blemishes"].toArray()){const auto candidate=value.toObject();bool overlaps=false;for(const auto& saved:retained){const auto mark=saved.toObject();const double dx=(candidate["x"].toDouble()-mark["x"].toDouble())*source_.width(),dy=(candidate["y"].toDouble()-mark["y"].toDouble())*source_.height();if(std::hypot(dx,dy)<(candidate["radius"].toDouble()+mark["radius"].toDouble())*std::min(source_.width(),source_.height())){overlaps=true;break;}}if(!overlaps)retained.append(candidate);}settings["blemishes"]=retained;settings["blemishDetectionComplete"]=true;}settings["irisMaterials"]=analysis["irisMaterials"];settings["browMaterials"]=analysis["browMaterials"];settings["lipLighting"]=analysis["lipLighting"];settings["cheekLighting"]=analysis["cheekLighting"];settings["hairLighting"]=analysis["hairLighting"];settings["teethStatus"]=analysis["teethStatus"];settings["eyeStatus"]=analysis["eyeStatus"];settings["redEyeStatus"]=analysis["redEyeStatus"];if(settings["parameters"].toObject()["face.foreheadFill"].toDouble()!=0)settings["foreheadVolume"]=editing::portrait_forehead_volume(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]));if(settings["parameters"].toObject()["face.tearFill.left"].toDouble()!=0 || settings["parameters"].toObject()["face.tearFill.right"].toDouble()!=0)settings["tearVolume"]=editing::portrait_tear_volume(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]));if(settings["parameters"].toObject()["face.malarTone.left"].toDouble()!=0 || settings["parameters"].toObject()["face.malarTone.right"].toDouble()!=0)settings["malarVolume"]=editing::portrait_malar_volume(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]));if(settings["parameters"].toObject()["face.cheekTone.left"].toDouble()!=0 || settings["parameters"].toObject()["face.cheekTone.right"].toDouble()!=0)settings["cheekVolume"]=editing::portrait_cheek_volume(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]));if(settings["parameters"].toObject()["face.orbitalTone.left"].toDouble()!=0 || settings["parameters"].toObject()["face.orbitalTone.right"].toDouble()!=0)settings["orbitalVolume"]=editing::portrait_orbital_volume(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]));if(settings["parameters"].toObject()["face.perioralTone.left"].toDouble()!=0 || settings["parameters"].toObject()["face.perioralTone.right"].toDouble()!=0)settings["perioralVolume"]=editing::portrait_perioral_volume(linear_preview_,editing::portrait_person_json(people_[std::size_t(selected_)]));if(merge_analysis){history_[std::size_t(history_index_)]=people_;schedule_preview();}else remember();build_parameters();update_range_overlay();status_->setText(tr("范围分析完成"));}}else {status_->setText(tr("分析已取消或编辑目标已变化，保留当前设置"));build_parameters();}}catch(const std::exception& e){if(repair_analysis_refresh_blemishes_ && !cancelled_)setProperty("portraitBlemishError",QString::fromUtf8(e.what()));status_->setText(QString::fromUtf8(e.what()));build_parameters();}
    }

    if(detail_busy_ && detail_pending_.valid() && detail_pending_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
        detail_busy_=false;try{const auto image=detail_pending_.get();if(!detail_cancelled_){const double ratio=double(source_.width())/preview_.width();detail_pixels_->setPixmap(QPixmap::fromImage(image));detail_pixels_->setScale(1./ratio);detail_pixels_->setPos(detail_area_.x()/ratio,detail_area_.y()/ratio);detail_pixels_->show();}}catch(const std::exception& e){if(!detail_cancelled_)status_->setText(QString::fromUtf8(e.what()));}
    }
    if(preview_busy_ && preview_pending_.valid() && preview_pending_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
        preview_busy_=false;
        try { auto image=preview_pending_.get();
            if(!preview_cancelled_ && preview_job_epoch_==preview_epoch_ &&
               (preview_job_revision_==preview_revision_ || gesture_ || painting_ || repair_source_dragging_ || repair_drawing_ || liquify_drawing_)) {
                displayed_geometry_cache_=geometry_cache_;
                if(placement_key_=="head:outline"){painting_mesh_=displayed_geometry_cache_.mesh;update_overlay();}
                adjusted_preview_=image.size()==preview_.size()?std::move(image):image.scaled(preview_.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                ++preview_frames_;setProperty("portraitPreviewFrames",qulonglong(preview_frames_));preview_displayed_revision_=qint64(preview_job_revision_);
                if(!comparing_ && !locate_->isChecked())pixels_->setPixmap(QPixmap::fromImage(adjusted_preview_));
                if(!preview_dirty_ && !gesture_ && !painting_ && !repair_drawing_ && !repair_source_dragging_ && !liquify_drawing_)detail_timer_.start();
            }
        } catch(const std::exception& e){if(!preview_cancelled_)status_->setText(QString::fromUtf8(e.what()));}
        if(preview_dirty_)start_preview();
    }
    if(!busy_ || !pending_.valid() || pending_.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
    busy_=false;detect_->setEnabled(true);accept_->setEnabled(true);cancel_compute_->setEnabled(false);cancel_compute_->hide();
    const QString pendingKey=property("pendingOrientationKey").toString();
    const QString pendingPerson=property("pendingOrientationPerson").toString();
    const int pendingValue=property("pendingOrientationValue").toInt();
    setProperty("pendingOrientationKey",QVariant{});
    try {
        auto detected=pending_.get();if(cancelled_)return;
        auto previous_people=people_;
        std::erase_if(previous_people,[](const auto& p){return p.settings.value("imageScope").toBool();});
        editing::match_portrait_people(detected,previous_people);
        // A layer editing scope is not a detected face. Keep its identity and
        // strokes across detection without matching it to any real person.
        for(const auto& old:people_)if(old.settings.value("imageScope").toBool())detected.push_back(old);
        if(detected.empty()){
            editing::PortraitPerson scope;scope.face={0,0,1,1};scope.body_ambiguous=true;
            scope.settings=QJsonObject{{"partialSkin",true},{"imageScope",true},{"protectBackground",false}};
            detected.push_back(std::move(scope));
        }
        // Retain unmatched edited records; never discard a person's work on re-detect.
        for(const auto& old:people_)if(!old.settings.isEmpty() && std::none_of(detected.begin(),detected.end(),[&](const auto& p){return p.id==old.id;})) {
            auto retained=old;retained.match_ambiguous=true;retained.unmatched=true;detected.push_back(std::move(retained));
        }
        people_=std::move(detected);for(auto& p:people_)if(!p.unmatched && p.settings["parameters"].toObject()["face.foreheadFill"].toDouble()!=0)p.settings["foreheadVolume"]=editing::portrait_forehead_volume(linear_preview_,editing::portrait_person_json(p));for(auto& p:people_)if(!p.unmatched && (p.settings["parameters"].toObject()["face.tearFill.left"].toDouble()!=0 || p.settings["parameters"].toObject()["face.tearFill.right"].toDouble()!=0))p.settings["tearVolume"]=editing::portrait_tear_volume(linear_preview_,editing::portrait_person_json(p));for(auto& p:people_)if(!p.unmatched && (p.settings["parameters"].toObject()["face.malarTone.left"].toDouble()!=0 || p.settings["parameters"].toObject()["face.malarTone.right"].toDouble()!=0))p.settings["malarVolume"]=editing::portrait_malar_volume(linear_preview_,editing::portrait_person_json(p));for(auto& p:people_)if(!p.unmatched && (p.settings["parameters"].toObject()["face.cheekTone.left"].toDouble()!=0 || p.settings["parameters"].toObject()["face.cheekTone.right"].toDouble()!=0))p.settings["cheekVolume"]=editing::portrait_cheek_volume(linear_preview_,editing::portrait_person_json(p));for(auto& p:people_)if(!p.unmatched && (p.settings["parameters"].toObject()["face.orbitalTone.left"].toDouble()!=0 || p.settings["parameters"].toObject()["face.orbitalTone.right"].toDouble()!=0))p.settings["orbitalVolume"]=editing::portrait_orbital_volume(linear_preview_,editing::portrait_person_json(p));for(auto& p:people_)if(!p.unmatched && (p.settings["parameters"].toObject()["face.perioralTone.left"].toDouble()!=0 || p.settings["parameters"].toObject()["face.perioralTone.right"].toDouble()!=0))p.settings["perioralVolume"]=editing::portrait_perioral_volume(linear_preview_,editing::portrait_person_json(p));for(const auto& p:people_)if(!entry_settings_.contains(p.id))entry_settings_.insert(p.id,p.settings);populate();remember();
        status_->setText(people_.size()==1 && people_[0].settings.value("imageScope").toBool()?tr("未识别到人脸，可继续手动液化与消除"):tr("识别完成，可选择人物继续编辑"));
        if(!pendingKey.isEmpty() && selected_>=0 && people_[std::size_t(selected_)].id.toString()==pendingPerson){
            if(people_[std::size_t(selected_)].settings["faceSurface"].isObject())set_parameter(pendingKey,pendingValue);
            else status_->setText(tr("当前照片无法可靠调整朝向，其他面部调整仍可使用"));
        }
    } catch(const std::exception& e){status_->setText(QString::fromUtf8(e.what()));}
}
void PortraitDialog::populate() {
    const QUuid active=faces_->currentItem()?QUuid(faces_->currentItem()->data(Qt::UserRole).toString()):QUuid();
    const QSignalBlocker block(faces_);faces_->clear();selected_=-1;
    for(std::size_t i=0;i<people_.size();++i) {
        const auto& p=people_[i];const auto f=p.face;
        const QRect rect(int(f.x()*preview_.width()),int(f.y()*preview_.height()),int(f.width()*preview_.width()),int(f.height()*preview_.height()));
        auto* item=new QListWidgetItem(QIcon(QPixmap::fromImage(preview_.copy(rect))),p.settings.value("imageScope").toBool()?tr("图像"):p.settings.value("partialSkin").toBool()?tr("局部皮肤"):tr("人物%1").arg(i+1),faces_);
        item->setData(Qt::UserRole,p.id.toString());if(p.match_ambiguous)item->setToolTip(tr("重识别匹配不确定，需要核对人物"));
        if(p.id==active)selected_=int(i);
    }
    if(people_.size()==1)selected_=0;
    faces_->setCurrentRow(selected_);update_person();activate_panel_tool();
}
void PortraitDialog::select_person(int index) {
    finish_brush_gesture();finish_gesture();
    placement_key_.clear();update_overlay();finish_liquify();finish_preset(true);finish_gesture();liquify_tool_=-1;view_->set_tool(0);++preview_epoch_;
    repair_mode_=0;repair_selected_=-1;repair_has_source_=false;repair_drawing_=false;painting_=false;range_mode_=0;
    selected_=index>=0 && index<int(people_.size())?index:-1;update_person();activate_panel_tool();
    schedule_preview();
    if(focus_mode_==1 || focus_mode_==2)focus_person(focus_mode_);
}
void PortraitDialog::update_person() {
    { const QSignalBlocker a(side_),b(linked_),c(strength_);const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{};
      side_->setCurrentIndex(settings["side"].toInt());linked_->setChecked(settings["linked"].toBool(true));strength_->setValue(settings["strength"].toInt(100));strength_->setEnabled(selected_>=0);findChild<QLabel*>("portraitStrengthValue")->setText(QString::number(strength_->value())+"%"); }

    {const QSignalBlocker block(protect_background_);protect_background_->setEnabled(selected_>=0);protect_background_->setChecked(selected_<0 || people_[std::size_t(selected_)].settings.value("protectBackground").toBool(true));}
    update_overlay();
    build_parameters();
    refresh_ranges();
    update_range_overlay();
    const QSignalBlocker side_block(side_),linked_block(linked_);
    linked_->setEnabled(selected_>=0);side_->setEnabled(false);confirm_->setEnabled(false);
    if(selected_<0){confirm_->hide();details_->setText(tr("请从顶部头像选择人物；当前没有编辑目标"));return;}
    const auto& p=people_[std::size_t(selected_)];
    if(p.settings.value("imageScope").toBool()){details_->setText(tr("未识别到人脸，可使用手动液化与消除"));locate_->setEnabled(false);}else if(p.settings.value("partialSkin").toBool()){details_->setText(tr("仅识别到局部皮肤，没有完整人脸。可做肤质、肤色、局部修复、手动美甲与纹身贴；不提供五官、牙齿或自动身体定位；可手动定位上背轮廓。"));locate_->setEnabled(false);}else locate_->setEnabled(true);
    linked_->setChecked(p.settings.value("linked").toBool(true));side_->setCurrentIndex(p.settings.value("side").toInt());side_->setEnabled(!linked_->isChecked());
    confirm_->setEnabled(p.match_ambiguous && !p.unmatched);confirm_->setVisible(p.match_ambiguous && !p.unmatched);
    if(!p.settings.value("partialSkin").toBool())details_->setText(tr("人物%1 · %2\n左右以人物自身为准").arg(selected_+1).arg(p.match_ambiguous?tr("关联待确认"):p.body_ambiguous?tr("身体范围待确认"):tr("定位就绪")));
    details_->setToolTip(tr("面部关键点：%1；重新识别不会按列表顺序覆盖人物参数。").arg(p.landmarks.size()));
}
QImage PortraitDialog::manual_slim_placement_mesh() const {
    auto candidate=recipe();auto list=candidate["people"].toArray();
    if(selected_>=0 && selected_<list.size()){auto p=list[selected_].toObject();auto s=p["settings"].toObject();auto values=s["parameters"].toObject();if(placement_key_=="body:manualDistort"){values["body.manualRotate"]=0;values["body.manualShear"]=0;}else values[placement_key_=="body:manualScale"?"body.manualScale":"body.manualSlim"]=0;s["parameters"]=values;p["settings"]=s;list[selected_]=p;candidate["people"]=list;}
    return editing::portrait_deformation(source_.size(),candidate);
}
void PortraitDialog::update_overlay() {
    QPainterPath path;
    if(selected_>=0 && placement_key_=="body:manualDistort"){
        const auto a=people_[std::size_t(selected_)].settings["manualDistortRegion"].toArray();if(a.size()==3){
            const QPointF center(a[0].toDouble(),a[1].toDouble());const double r=a[2].toDouble()*std::min(source_.width(),source_.height()),handle=5/std::max(.01,view_->transform().m11());
            const auto pt=[&](double x,double y){return repair_scene_position(center+QPointF(x*r/(source_.width()-1),y*r/(source_.height()-1)));};
            for(int i=0;i<=96;++i){const double angle=i*6.283185307179586/96;const auto q=pt(std::cos(angle),std::sin(angle));if(i==0)path.moveTo(q);else path.lineTo(q);}
            path.addEllipse(pt(0,0),handle,handle);path.addEllipse(pt(.35,0),handle,handle);path.addRect(QRectF(pt(0,-.35)-QPointF(handle,handle),QSizeF(handle*2,handle*2)));
            path.moveTo(pt(0,0));path.lineTo(pt(.35,0));path.moveTo(pt(0,0));path.lineTo(pt(0,-.35));
        }
    }
    if(selected_>=0 && placement_key_=="body:manualScale" && manual_scale_region_.size()==4){
        const QPointF center(manual_scale_region_[0].toDouble(),manual_scale_region_[1].toDouble());const double unit=std::min(source_.width(),source_.height());
        const double handle=5/std::max(.01,view_->transform().m11());const auto c=repair_scene_position(center);path.addEllipse(c,handle,handle);
        for(int ring=2;ring<4;++ring){const double radius=manual_scale_region_[ring].toDouble()*unit;for(int i=0;i<=96;++i){const double angle=i*6.283185307179586/96;const auto q=repair_scene_position(center+QPointF(std::cos(angle)*radius/(source_.width()-1),std::sin(angle)*radius/(source_.height()-1)));if(i==0)path.moveTo(q);else path.lineTo(q);}}
    }
    if(selected_>=0 && placement_key_=="body:manualSlim" && !manual_slim_box_.isEmpty()){
        const auto r=manual_slim_box_;const double radius=5/std::max(.01,view_->transform().m11());
        const QVector<QPointF> corners{r.topLeft(),r.topRight(),r.bottomRight(),r.bottomLeft()};
        for(int side=0;side<4;++side){for(int i=0;i<=20;++i){const auto q=repair_scene_position(corners[side]+(corners[(side+1)%4]-corners[side])*(i/20.));if(i==0)path.moveTo(q);else path.lineTo(q);}const auto q=repair_scene_position(corners[side]);path.addRect(QRectF(q-QPointF(radius,radius),QSizeF(radius*2,radius*2)));}
        path.moveTo(repair_scene_position({r.center().x(),r.top()}));path.lineTo(repair_scene_position({r.center().x(),r.bottom()}));
    }
    if(selected_>=0 && placement_key_=="body:manualHeight"){
        QVector<QPointF> points;for(const auto& v:manual_height_points_){const auto a=v.toArray();points.append(repair_scene_position({a[0].toDouble(),a[1].toDouble()}));}
        QPointF tangent(1,0);if(points.size()==2){const auto d=points[1]-points[0];const double length=std::hypot(d.x(),d.y());if(length>1)tangent={d.y()/length,-d.x()/length};}
        const double radius=6/std::max(.01,view_->transform().m11());
        for(const auto& p:points){path.moveTo(p-tangent*preview_.width()*.3);path.lineTo(p+tangent*preview_.width()*.3);path.addEllipse(p,radius,radius);}
    }
    if(selected_>=0 && locate_->isChecked()) {
        const auto& person=people_[std::size_t(selected_)];
        const auto f=person.face;if(property("portraitMakeupLocate").toString().isEmpty())path.addRect(f.x()*preview_.width(),f.y()*preview_.height(),f.width()*preview_.width(),f.height()*preview_.height());
        const auto points=navigation_->currentRow()==3?person.settings.value("pose").toArray():person.landmarks;
        for(int i=0;i<points.size();++i){const auto part=property("portraitMakeupLocate").toString();if(navigation_->currentRow()==11 && !part.isEmpty()){const bool left=person.settings["side"].toInt()==0,linked=person.settings["linked"].toBool(true);const bool allowed=part=="brow"?(i>=17 && i<27 && (linked || (left?i>=22:i<22))):(i>=36 && i<48 && (linked || (left?i>=42:i<42)));if(!allowed)continue;}const auto a=points[i].toArray();path.addEllipse(QPointF(a[0].toDouble()*preview_.width(),a[1].toDouble()*preview_.height()),2.,2.);}
    }
    if(selected_>=0 && placement_key_=="makeup.tattoo"){
        const auto quad=people_[std::size_t(selected_)].settings.value("tattooQuad").toArray();QPolygonF corners;for(const auto& v:quad){const auto a=v.toArray();corners<<QPointF(a[0].toDouble()*preview_.width(),a[1].toDouble()*preview_.height());}if(corners.size()==4){path.addPolygon(corners);path.closeSubpath();const double r=5/std::max(.01,view_->transform().m11());for(const auto& c:corners)path.addRect(QRectF(c-QPointF(r,r),QSizeF(2*r,2*r)));}
    }
    if(nail_drawing_){path.addPolygon(nail_outline_);}
    if(selected_>=0 && placement_key_=="nail:outline")for(const auto& n:people_[std::size_t(selected_)].settings.value("nails").toArray())path.addPath(editing::portrait_nail_path(n.toObject(),preview_.size()));
    if(selected_>=0 && placement_key_.startsWith("gradient:"))for(const auto& value:people_[std::size_t(selected_)].settings["hairZones"].toArray()){const auto zone=value.toObject();if(zone["key"].toString()!=placement_key_.mid(9))continue;const auto a=zone["gradientAxis"].toArray();if(a.size()!=4)continue;const QPointF first=repair_scene_position({a[0].toDouble(),a[1].toDouble()}),last=repair_scene_position({a[2].toDouble(),a[3].toDouble()});const double radius=6/std::max(.01,view_->transform().m11());path.moveTo(first);path.lineTo(last);path.addEllipse(first,radius,radius);path.addEllipse(first+(last-first)*zone["gradientMidpoint"].toDouble(.5),radius*.7,radius*.7);path.addRect(QRectF(last-QPointF(radius,radius),QSizeF(radius*2,radius*2)));}
    if(selected_>=0 && placement_key_=="makeup.iris"){
        const auto& person=people_[std::size_t(selected_)];const auto settings=person.settings,parameters=settings["parameters"].toObject();
        for(int side=0;side<2;++side){if(!settings["linked"].toBool(true) && side!=settings["side"].toInt())continue;const QString suffix=side==0?".left":".right";
            const auto e=settings["irisMaterials"].toObject()["geometry"+suffix].toObject()["ellipse"].toArray();if(e.size()!=5)continue;
            const QPointF point((e[0].toDouble()+person.face.width()*parameters["makeup.irisX"+suffix].toDouble()*.01)*preview_.width(),(e[1].toDouble()+person.face.height()*parameters["makeup.irisY"+suffix].toDouble()*.01)*preview_.height());
            const double r=5/std::max(.01,view_->transform().m11());path.addEllipse(point,r,r);path.moveTo(point-QPointF(r*1.5,0));path.lineTo(point+QPointF(r*1.5,0));}
    }
    if(selected_>=0 && placement_key_=="nail:select"){
        const auto nails=people_[std::size_t(selected_)].settings["nails"].toArray();const int i=property("portraitNailIndex").toInt();if(i>=0 && i<nails.size())path.addPath(editing::portrait_nail_path(nails[i].toObject(),preview_.size()));
    }
    if(selected_>=0 && placement_key_=="makeup.sticker"){const auto t=editing::portrait_sticker_transform(editing::portrait_person_json(people_[std::size_t(selected_)]),preview_.size());QPainterPath box;box.addRect(QRectF(-1.1,-1.1,2.2,2.2));path.addPath(t.map(box));}
    if(selected_>=0 && placement_key_=="makeup.mole"){const auto t=editing::portrait_mole_transform(editing::portrait_person_json(people_[std::size_t(selected_)]),preview_.size());const auto center=t.map(QPointF(0,0));const double radius=7/std::max(.01,view_->transform().m11());path.addEllipse(center,radius,radius);path.moveTo(center-QPointF(radius*1.5,0));path.lineTo(center+QPointF(radius*1.5,0));}
    if(selected_>=0 && placement_key_=="hair.accessory"){const auto person=editing::portrait_person_json(people_[std::size_t(selected_)]);const auto t=editing::portrait_accessory_transform(person,person["settings"].toObject()["parameters"].toObject(),preview_.size());QPainterPath box;box.addRect(QRectF(-1.1,-.9,2.2,1.8));path.addPath(t.map(box));const double radius=6/std::max(.01,view_->transform().m11());const auto scale=t.map(QPointF(1.1,.9)),rotate=t.map(QPointF(0,-1.2));path.addRect(QRectF(scale-QPointF(radius,radius),QSizeF(radius*2,radius*2)));path.moveTo(t.map(QPointF(0,-.9)));path.lineTo(rotate);path.addEllipse(rotate,radius,radius);}
    if(selected_>=0 && placement_key_=="body:back"){const double r=6/std::max(.01,view_->transform().m11());for(int i=0;i<back_placement_.size();++i){const auto q=back_placement_[i].toArray();const auto pt=repair_scene_position(QPointF(q[0].toDouble(),q[1].toDouble()));path.addEllipse(pt,r,r);}}
    if(selected_>=0 && placement_key_=="body:chest"){
        const double radius=6/std::max(.01,view_->transform().m11());QPainterPath line;
        for(int i=0;i<chest_placement_points_.size();++i){const auto q=chest_placement_points_[i].toArray();const auto point=repair_scene_position({q[0].toDouble(),q[1].toDouble()});
            if(i==0)line.moveTo(point);else line.lineTo(point);
            if(i==1)path.addRect(QRectF(point-QPointF(radius,radius),QSizeF(radius*2,radius*2)));else path.addEllipse(point,radius,radius);
        }path.addPath(line);
    }
    if(selected_>=0 && placement_key_=="head:outline"){
        painting_mesh_=displayed_geometry_cache_.mesh;
        const auto manual=people_[std::size_t(selected_)].settings["headManual"].toObject();QPolygonF polygon;QPointF center;
        for(const auto& v:manual["outline"].toArray()){const auto a=v.toArray();const QPointF q(a[0].toDouble(),a[1].toDouble());polygon<<q;center+=q;}
        if(!polygon.isEmpty()){center/=polygon.size();QPainterPath inner,outer;QJsonArray outerPoints;const double radius=5/std::max(.01,view_->transform().m11());
            for(int i=0;i<polygon.size();++i){const auto q=repair_scene_position(polygon[i]);if(i==0)inner.moveTo(q);else inner.lineTo(q);path.addEllipse(q,radius,radius);
                const auto delta=polygon[i]-center;const double pixels=std::hypot(delta.x()*source_.width(),delta.y()*source_.height());const auto sourceEdge=polygon[i]+delta*(people_[std::size_t(selected_)].face.width()*source_.width()*manual["transition"].toDouble(.65)/std::max(1.,pixels));outerPoints.append(QJsonArray{sourceEdge.x(),sourceEdge.y()});const auto edge=repair_scene_position(sourceEdge);
                if(i==2)path.addRect(QRectF(edge-QPointF(radius,radius),QSizeF(2*radius,2*radius)));}
            inner=QPainterPath{};const auto curve=editing::portrait_head_outline(manual["outline"].toArray(),QSize(1000,1000));
            for(int i=0;i<=160;++i){const auto q=repair_scene_position(curve.pointAtPercent(i/160.)/1000.);if(i==0)inner.moveTo(q);else inner.lineTo(q);}
            const auto outerCurve=editing::portrait_head_outline(outerPoints,QSize(1000,1000));
            for(int i=0;i<=160;++i){const auto q=repair_scene_position(outerCurve.pointAtPercent(i/160.)/1000.);if(i==0)outer.moveTo(q);else outer.lineTo(q);}
            inner.closeSubpath();outer.closeSubpath();path.addPath(inner);path.addPath(outer);
            const auto a=manual["anchor"].toArray();const auto q=repair_scene_position({a[0].toDouble(),a[1].toDouble()});path.moveTo(q-QPointF(radius*1.5,0));path.lineTo(q+QPointF(radius*1.5,0));path.moveTo(q-QPointF(0,radius*1.5));path.lineTo(q+QPointF(0,radius*1.5));
        }
    }
    auto* linebar=view_->viewport()->findChild<QWidget*>("portraitBodyLinesToolbar");
    const bool bodyLines=selected_>=0 && property("portraitWorkspaceTask").toInt()==8 && property("portraitBodyMode").toInt()==2 && people_[std::size_t(selected_)].settings["parameters"].toObject()["contour.lines"].toDouble()!=0;
    if(linebar)linebar->setVisible(bodyLines);
    if(bodyLines){
        const auto quad=people_[std::size_t(selected_)].settings["bodyLinesQuad"].toArray();QPolygonF polygon;QPointF center;
        for(const auto& v:quad){const auto a=v.toArray();const auto q=repair_scene_position({a[0].toDouble(),a[1].toDouble()});polygon<<q;center+=q;}
        if(polygon.size()==4){center/=4;path.addPolygon(polygon);path.closeSubpath();const double r=4/std::max(.01,view_->transform().m11());for(auto q:polygon)path.addRect(QRectF(q-QPointF(r,r),QSizeF(r*2,r*2)));const auto top=(polygon[0]+polygon[1])*.5,rotation=top+(top-center)*.35;path.moveTo(top);path.lineTo(rotation);path.addEllipse(rotation,r,r);
            if(!linebar){linebar=new QWidget(view_->viewport());linebar->setObjectName("portraitBodyLinesToolbar");linebar->setStyleSheet("background:#252a30;border-radius:4px;");auto* layout=new QHBoxLayout(linebar);layout->setContentsMargins(4,4,4,4);const QStringList labels{tr("移动"),tr("贴合"),tr("擦除"),tr("删除")};for(int i=0;i<4;++i){auto* b=new QPushButton(labels[i],linebar);b->setObjectName(QString("portraitBodyLineAction%1").arg(i));layout->addWidget(b);connect(b,&QPushButton::clicked,this,[this,i]{finish_brush_gesture();finish_gesture();if(selected_<0)return;setProperty("portraitBodyLineErase",i==2);setProperty("portraitBodyLineFit",i==1);if(i==3){auto& settings=people_[std::size_t(selected_)].settings;settings.remove("bodyLinesQuad");auto masks=settings["masks"].toObject();masks.remove("contour.lines");settings["masks"]=masks;settings["bodyMaterial"]="none";set_parameter("contour.lines",0);placement_key_.clear();}else if(i==2){placement_key_.clear();refresh_ranges();if(range_key_->findData("contour.lines")<0)range_key_->addItem(tr("线条"),"contour.lines");range_key_->setCurrentIndex(range_key_->findData("contour.lines"));range_mode_=2;view_->set_tool(-1);}else{range_mode_=0;placement_key_="body:lines";painting_mesh_=editing::portrait_deformation(source_.size(),recipe());view_->set_tool(-1);}build_parameters();update_overlay();});}linebar->adjustSize();}
            const auto pos=view_->mapFromScene(QPointF(center.x(),polygon.boundingRect().bottom()));linebar->move(std::clamp(pos.x()-linebar->width()/2,0,std::max(0,view_->viewport()->width()-linebar->width())),std::clamp(pos.y()+12,0,std::max(0,view_->viewport()->height()-linebar->height())));linebar->show();linebar->raise();
        }
    }
    update_hair_source_toolbar();
    update_decal_overlay(path);
    overlay_->setPath(path);
}
void PortraitDialog::finish_brush_gesture(){
    if(head_handle_>=0 && selected_>=0){people_[std::size_t(selected_)].settings=head_before_;head_handle_=-1;gesture_=false;view_->viewport()->releaseMouse();schedule_preview();update_overlay();}
    if(nail_drawing_)finish_nail_outline(false);
    if(selected_<0)return;bool changed=false;auto& settings=people_[std::size_t(selected_)].settings;
    if(manual_distort_handle_>=0){manual_distort_handle_=-1;settings=manual_distort_before_;gesture_=false;view_->viewport()->releaseMouse();schedule_preview();update_overlay();}
    if(manual_scale_handle_>=0){manual_scale_handle_=-1;manual_scale_region_=manual_scale_before_;view_->viewport()->releaseMouse();update_overlay();}
    if(manual_slim_handle_>=0){manual_slim_handle_=-1;manual_slim_box_=manual_slim_before_;view_->viewport()->releaseMouse();update_overlay();}
    if(manual_height_handle_>=0){manual_height_handle_=-1;manual_height_points_=settings["manualHeightRange"].toArray();view_->viewport()->releaseMouse();update_overlay();}
    if(placement_drawing_){placement_drawing_=false;view_->viewport()->releaseMouse();finish_gesture();}
    if(repair_source_dragging_){repair_source_dragging_=false;changed=true;}
    if(repair_drawing_){repair_drawing_=false;if(!repair_live_.isEmpty()){auto repairs=settings["repairs"].toArray();repairs.append(repair_live_);settings["repairs"]=repairs;repair_selected_=-1;repair_live_=QJsonObject{};changed=true;}}
    if(painting_){painting_=false;const auto key=range_key_->currentData().toString();store_range(settings,key,painting_mask_);changed=true;}
    if(changed){view_->viewport()->releaseMouse();remember();refresh_ranges();update_range_overlay();}
    finish_liquify();
}
bool PortraitDialog::event(QEvent* event){
    if(event->type()==QEvent::Resize){if(auto* tasks=findChild<QListWidget*>("portraitTasks"))tasks->setFixedWidth(width()<1440?200:224);if(auto* inspector=findChild<QWidget*>("portraitInspector"))inspector->setFixedWidth(width()<1440?400:448);}
    if(event->type()==QEvent::WindowDeactivate){finish_brush_gesture();finish_gesture();view_->cancel_navigation();}
    return QDialog::event(event);
}
bool PortraitDialog::eventFilter(QObject* watched,QEvent* event) {
    if(watched==view_->viewport() && event->type()==QEvent::Wheel && selected_>=0 && view_->active_tool()<0 && !view_->temporary_pan()){
        auto* wheel=static_cast<QWheelEvent*>(event);const auto modifiers=wheel->modifiers();
        const int task=property("portraitWorkspaceTask").toInt();
        const bool manual=task==12 || (task==4 && property("portraitSkinMode").toInt()==1) || (task==5 && property("portraitBlemishPage").toInt()==0) || task==6 || (task==7 && property("portraitOilMode").toInt()==1);
        if(manual && (modifiers==Qt::ControlModifier || modifiers==Qt::ShiftModifier)){
            const bool size=modifiers==Qt::ControlModifier;
            const QString name=size?(task==12?"portraitPaintSize":task==7?"portraitOilBrushSize":task==4?"portraitSmoothBrushSize":task==5?"portraitBlemishSize":"portraitWrinkleSize"):
                task==12?"portraitSlider_"+property("portraitPaintKey").toString():task==7?"portraitSlider_skin.oil":task==4?(property("portraitSmoothTarget").toInt()==1?"portraitSlider_skin.bodySmooth":"portraitSlider_skin.smooth"):task==5?"portraitBlemishAmount":"portraitWrinkleAmount";
            auto* slider=parameter_host_->findChild<QSlider*>(name);
            if(slider && slider->isVisible() && slider->isEnabled()){
                const char* residual=size?"portraitSizeWheelRemainder":"portraitAmountWheelRemainder";
                const int delta=wheel->angleDelta().y(),sum=property(residual).toInt()+delta,steps=sum/120;
                setProperty(residual,sum%120);
                if(steps){finish_brush_gesture();slider->setValue(slider->value()+steps*5);status_->setText(tr("%1：%2 · Ctrl＋滚轮调大小，Shift＋滚轮调程度").arg(size?tr("画笔大小"):tr("程度")).arg(slider->value()));}
            }
            wheel->accept();return true;
        }
    }

    if(watched==view_->viewport() && (placement_key_=="body:lines" || placement_key_.startsWith("decal:")) && !view_->temporary_pan() && body_lines_event(event))return true;
    if(watched==view_->viewport() && placement_key_=="head:outline" && !view_->temporary_pan() && head_range_event(event))return true;
    if(auto* slider=qobject_cast<QSlider*>(watched)){
        if(event->type()==QEvent::Wheel){event->ignore();return true;}
        if(event->type()==QEvent::KeyPress){const auto* key=static_cast<QKeyEvent*>(event);
            if(key->modifiers().testFlag(Qt::ShiftModifier) && (key->key()==Qt::Key_Left || key->key()==Qt::Key_Right || key->key()==Qt::Key_Up || key->key()==Qt::Key_Down)){
                const int direction=(key->key()==Qt::Key_Left || key->key()==Qt::Key_Down)?-1:1;
                slider->setValue(slider->value()+direction*10*slider->singleStep());return true;
            }
        }
    }
    if(watched==view_->viewport() && event->type()==QEvent::MouseButtonRelease && !view_->temporary_pan()){
        auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton && !view_->viewport()->rect().contains(mouse->position().toPoint())){
            finish_brush_gesture();
            return false;
        }
    }
    if(watched==view_){
        if(event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if(key->modifiers()==Qt::NoModifier && (key->key()==Qt::Key_B || key->key()==Qt::Key_H || key->key()==Qt::Key_Escape)){auto* button=findChild<QPushButton*>(key->key()==Qt::Key_B?"portraitCanvasBrush":"portraitCanvasHand");if(button && button->isVisible()){button->click();return true;}}}
        // Finish the stroke before navigation takes ownership of input.
        // Focus events belong to the view, not necessarily its viewport.
        if(event->type()==QEvent::FocusOut || (event->type()==QEvent::KeyPress &&
            static_cast<QKeyEvent*>(event)->key()==Qt::Key_Space && !static_cast<QKeyEvent*>(event)->isAutoRepeat()))
            finish_brush_gesture();
    }
    if(watched==view_->viewport() && selected_>=0 && placement_key_=="body:manualDistort" && !view_->temporary_pan()){
        const auto type=event->type();if(type==QEvent::MouseButtonPress || type==QEvent::MouseMove || type==QEvent::MouseButtonRelease){
            auto* mouse=static_cast<QMouseEvent*>(event);if(type==QEvent::MouseButtonPress && mouse->button()!=Qt::LeftButton)return true;
            auto& settings=people_[std::size_t(selected_)].settings;const auto scene=view_->mapToScene(mouse->position().toPoint());const auto point=source_position(scene);const double unit=std::min(source_.width(),source_.height());
            if(type==QEvent::MouseButtonPress){finish_gesture();manual_distort_before_=settings;manual_distort_start_=point;manual_distort_handle_=4;
                const auto a=settings["manualDistortRegion"].toArray();if(a.size()==3){const QPointF center(a[0].toDouble(),a[1].toDouble());const double r=a[2].toDouble()*unit,tolerance=12/std::max(.01,view_->transform().m11());
                    const auto pt=[&](double x,double y){return repair_scene_position(center+QPointF(x*r/(source_.width()-1),y*r/(source_.height()-1)));};
                    if(QLineF(scene,pt(0,0)).length()<tolerance)manual_distort_handle_=0;
                    else if(QLineF(scene,pt(.35,0)).length()<tolerance)manual_distort_handle_=2;
                    else if(QLineF(scene,pt(0,-.35)).length()<tolerance)manual_distort_handle_=3;
                    else {const double distance=std::hypot((point.x()-center.x())*(source_.width()-1),(point.y()-center.y())*(source_.height()-1));if(std::abs(distance-r)<tolerance*source_.width()/preview_.width())manual_distort_handle_=1;}
                }
                if(manual_distort_handle_==4)settings["manualDistortRegion"]=QJsonArray{point.x(),point.y(),8/unit};
                gesture_=true;view_->setFocus();view_->viewport()->grabMouse();update_overlay();return true;
            }
            if(manual_distort_handle_<0)return true;
            auto a=settings["manualDistortRegion"].toArray();const QPointF center(a[0].toDouble(),a[1].toDouble());const auto original=manual_distort_before_["manualDistortRegion"].toArray();auto params=settings["parameters"].toObject();const auto oldParams=manual_distort_before_["parameters"].toObject();
            const auto pixel=[&](QPointF q){return QPointF(q.x()*(source_.width()-1),q.y()*(source_.height()-1));};
            if(manual_distort_handle_==0){const auto p=QPointF(original[0].toDouble(),original[1].toDouble())+point-manual_distort_start_;a[0]=std::clamp(p.x(),0.,1.);a[1]=std::clamp(p.y(),0.,1.);}
            else if(manual_distort_handle_==1 || manual_distort_handle_==4){const auto d=pixel(point-center);const double radius=std::hypot(d.x(),d.y());a[2]=std::clamp(radius/unit,8/unit,1.);}
            else if(manual_distort_handle_==2){const auto from=pixel(manual_distort_start_-center),to=pixel(point-center);double angle=std::atan2(to.y(),to.x())-std::atan2(from.y(),from.x());if(angle>3.141592653589793)angle-=6.283185307179586;if(angle< -3.141592653589793)angle+=6.283185307179586;params["body.manualRotate"]=std::clamp(oldParams["body.manualRotate"].toDouble()+angle/.5235987755982988*100,-100.,100.);}
            else params["body.manualShear"]=std::clamp(oldParams["body.manualShear"].toDouble()-(point.x()-manual_distort_start_.x())*(source_.width()-1)/(a[2].toDouble()*unit*.35)*250,-100.,100.);
            settings["manualDistortRegion"]=a;settings["parameters"]=params;schedule_preview();update_overlay();
            if(type==QEvent::MouseButtonRelease){manual_distort_handle_=-1;view_->viewport()->releaseMouse();
                try{editing::portrait_deformation(source_.size(),recipe());finish_gesture();status_->setText(tr("已应用局部扭曲；圆柄旋转、方柄剪切，中心移动，外圈调整范围。"));}
                catch(const std::exception& e){settings=manual_distort_before_;gesture_=false;schedule_preview();status_->setText(QString::fromUtf8(e.what()));}build_parameters();update_overlay();
            }return true;
        }
    }
    if(watched==view_->viewport() && selected_>=0 && placement_key_=="body:manualScale" && !view_->temporary_pan()){
        const auto type=event->type();
        if(type==QEvent::MouseButtonPress || type==QEvent::MouseMove || type==QEvent::MouseButtonRelease){
            auto* mouse=static_cast<QMouseEvent*>(event);if(type==QEvent::MouseButtonPress && mouse->button()!=Qt::LeftButton)return true;
            const auto scene=view_->mapToScene(mouse->position().toPoint());const auto point=source_position(scene);const double unit=std::min(source_.width(),source_.height());
            const auto distance=[&](QPointF a,QPointF b){return std::hypot((a.x()-b.x())*(source_.width()-1),(a.y()-b.y())*(source_.height()-1))/unit;};
            if(type==QEvent::MouseButtonPress){
                manual_scale_before_=manual_scale_region_;manual_scale_start_=point;manual_scale_handle_=3;
                if(manual_scale_region_.size()==4){const QPointF center(manual_scale_region_[0].toDouble(),manual_scale_region_[1].toDouble());const double d=distance(point,center),inner=manual_scale_region_[2].toDouble(),outer=manual_scale_region_[3].toDouble();
                    const double tolerance=12/std::max(.01,view_->transform().m11())*source_.width()/preview_.width()/unit;
                    if(d<std::max(tolerance,inner*.5))manual_scale_handle_=0;
                    else if(std::abs(d-inner)<tolerance)manual_scale_handle_=1;
                    else if(std::abs(d-outer)<tolerance)manual_scale_handle_=2;
                }
                if(manual_scale_handle_==3)manual_scale_region_=QJsonArray{point.x(),point.y(),3/unit,8/unit};
                view_->setFocus();view_->viewport()->grabMouse();update_overlay();return true;
            }
            if(manual_scale_handle_<0)return true;
            const QPointF center(manual_scale_region_[0].toDouble(),manual_scale_region_[1].toDouble());
            if(manual_scale_handle_==0){const QPointF previous(manual_scale_before_[0].toDouble(),manual_scale_before_[1].toDouble());const auto moved=previous+point-manual_scale_start_;manual_scale_region_[0]=std::clamp(moved.x(),0.,1.);manual_scale_region_[1]=std::clamp(moved.y(),0.,1.);}
            else if(manual_scale_handle_==1){const QPointF c(manual_scale_before_[0].toDouble(),manual_scale_before_[1].toDouble());manual_scale_region_[2]=std::clamp(manual_scale_before_[2].toDouble()+distance(point,c)-distance(manual_scale_start_,c),3/unit,manual_scale_region_[3].toDouble()-5/unit);}
            else if(manual_scale_handle_==2){const QPointF c(manual_scale_before_[0].toDouble(),manual_scale_before_[1].toDouble());manual_scale_region_[3]=std::clamp(manual_scale_before_[3].toDouble()+distance(point,c)-distance(manual_scale_start_,c),manual_scale_region_[2].toDouble()+5/unit,1.);}
            else {const double outer=std::clamp(distance(point,center),8/unit,1.);manual_scale_region_[3]=outer;manual_scale_region_[2]=std::max(3/unit,outer*.5);}
            update_overlay();
            if(type==QEvent::MouseButtonRelease){manual_scale_handle_=-1;view_->viewport()->releaseMouse();auto& settings=people_[std::size_t(selected_)].settings;const auto previous=settings;settings["manualScaleRegion"]=manual_scale_region_;
                try{auto candidate=recipe();auto list=candidate["people"].toArray();auto p=list[selected_].toObject();auto set=p["settings"].toObject();auto values=set["parameters"].toObject();if(values["body.manualScale"].toDouble()==0)values["body.manualScale"]=1;set["parameters"]=values;p["settings"]=set;list[selected_]=p;candidate["people"]=list;editing::portrait_deformation(source_.size(),candidate);remember();painting_mesh_=manual_slim_placement_mesh();status_->setText(tr("内圈控制强作用区，外圈控制过渡；拖动中心移动，调节手动缩放查看效果。"));}
                catch(const std::exception& error){settings=previous;manual_scale_region_=manual_scale_before_;status_->setText(QString::fromUtf8(error.what()));}update_overlay();
            }
            return true;
        }
    }
    if(watched==view_->viewport() && selected_>=0 && placement_key_=="body:manualSlim" && !view_->temporary_pan()){
        const auto type=event->type();
        if(type==QEvent::MouseButtonPress || type==QEvent::MouseMove || type==QEvent::MouseButtonRelease){
            auto* mouse=static_cast<QMouseEvent*>(event);const auto scene=view_->mapToScene(mouse->position().toPoint());
            if(type==QEvent::MouseButtonPress && mouse->button()!=Qt::LeftButton)return true;
            const auto raw=source_position(scene);const QPointF point(std::clamp(raw.x(),0.,1.),std::clamp(raw.y(),0.,1.));
            if(type==QEvent::MouseButtonPress){
                manual_slim_before_=manual_slim_box_;manual_slim_start_=point;manual_slim_handle_=4;
                const auto r=manual_slim_box_;const QVector<QPointF> corners{r.topLeft(),r.topRight(),r.bottomRight(),r.bottomLeft()};
                double nearest=12/std::max(.01,view_->transform().m11());
                if(!r.isEmpty()){if(r.contains(point))manual_slim_handle_=5;for(int i=0;i<4;++i){const double distance=QLineF(scene,repair_scene_position(corners[i])).length();if(distance<nearest){nearest=distance;manual_slim_handle_=i;}}}
                if(manual_slim_handle_==4)manual_slim_box_=QRectF(point,point);
                view_->setFocus();view_->viewport()->grabMouse();update_overlay();return true;
            }
            if(manual_slim_handle_<0)return true;
            if(manual_slim_handle_==4)manual_slim_box_=QRectF(manual_slim_start_,point).normalized();
            else if(manual_slim_handle_==5){auto delta=point-manual_slim_start_;delta.setX(std::clamp(delta.x(),-manual_slim_before_.left(),1-manual_slim_before_.right()));delta.setY(std::clamp(delta.y(),-manual_slim_before_.top(),1-manual_slim_before_.bottom()));manual_slim_box_=manual_slim_before_.translated(delta);}
            else {const auto r=manual_slim_before_;const QVector<QPointF> corners{r.topLeft(),r.topRight(),r.bottomRight(),r.bottomLeft()};const auto moved=corners[manual_slim_handle_]+point-manual_slim_start_;manual_slim_box_=QRectF(corners[(manual_slim_handle_+2)%4],moved).normalized();}
            update_overlay();
            if(type==QEvent::MouseButtonRelease){
                manual_slim_handle_=-1;view_->viewport()->releaseMouse();auto& settings=people_[std::size_t(selected_)].settings;const auto previous=settings;
                const auto r=manual_slim_box_;settings["manualSlimRegion"]=QJsonArray{r.x(),r.y(),r.width(),r.height()};
                try{auto candidate=recipe();auto list=candidate["people"].toArray();auto p=list[selected_].toObject();auto set=p["settings"].toObject();auto values=set["parameters"].toObject();if(values["body.manualSlim"].toDouble()==0)values["body.manualSlim"]=1;set["parameters"]=values;p["settings"]=set;list[selected_]=p;candidate["people"]=list;editing::portrait_deformation(linear_preview_.size(),candidate);remember();painting_mesh_=manual_slim_placement_mesh();status_->setText(tr("拖动角点调整范围，拖动框内移动区域；调节手动瘦身查看效果。"));}
                catch(const std::exception& error){settings=previous;manual_slim_box_=manual_slim_before_;status_->setText(QString::fromUtf8(error.what()));}
                update_overlay();
            }
            return true;
        }
    }
    if(watched==view_->viewport() && selected_>=0 && placement_key_=="body:manualHeight" && !view_->temporary_pan()){
        const auto type=event->type();
        if(type==QEvent::MouseButtonPress || type==QEvent::MouseMove || type==QEvent::MouseButtonRelease){
            auto* mouse=static_cast<QMouseEvent*>(event);const auto scene=view_->mapToScene(mouse->position().toPoint());
            if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton){
                if(!preview_.rect().contains(scene.toPoint()))return true;
                manual_height_grab_offset_={};
                if(manual_height_points_.size()<2){const auto p=source_position(scene);manual_height_points_.append(QJsonArray{p.x(),p.y()});manual_height_handle_=manual_height_points_.size()-1;}
                else{double best=12/std::max(.01,view_->transform().m11());manual_height_handle_=-1;
                    const auto a=manual_height_points_[0].toArray(),b=manual_height_points_[1].toArray();const auto first=repair_scene_position({a[0].toDouble(),a[1].toDouble()}),last=repair_scene_position({b[0].toDouble(),b[1].toDouble()});const auto axis=(last-first)/std::max(1.,QLineF(first,last).length());
                    for(int i=0;i<2;++i){const double distance=std::abs(QPointF::dotProduct(scene-(i==0?first:last),axis));if(distance<best){best=distance;manual_height_handle_=i;}}
    if(manual_height_handle_>=0){const auto point=manual_height_points_[manual_height_handle_].toArray();manual_height_grab_offset_=source_position(scene)-QPointF(point[0].toDouble(),point[1].toDouble());}
                }
    if(manual_height_handle_>=0){view_->setFocus();view_->viewport()->grabMouse();}update_overlay();return true;
            }
            if(manual_height_handle_<0)return true;
            if(type==QEvent::MouseMove || type==QEvent::MouseButtonRelease){const auto p=source_position(scene)-manual_height_grab_offset_;manual_height_points_[manual_height_handle_]=QJsonArray{std::clamp(p.x(),0.,1.),std::clamp(p.y(),0.,1.)};update_overlay();}
            if(type==QEvent::MouseButtonRelease){manual_height_handle_=-1;view_->viewport()->releaseMouse();
                if(manual_height_points_.size()==2){auto& settings=people_[std::size_t(selected_)].settings;const auto previous=settings;settings["manualHeightRange"]=manual_height_points_;
                    try{auto candidate=recipe();auto list=candidate["people"].toArray();auto p=list[selected_].toObject();auto s=p["settings"].toObject();auto values=s["parameters"].toObject();if(values["body.manualHeight"].toDouble()==0)values["body.manualHeight"]=1;s["parameters"]=values;p["settings"]=s;list[selected_]=p;candidate["people"]=list;editing::portrait_deformation(linear_preview_.size(),candidate);remember();painting_mesh_=editing::portrait_deformation(source_.size(),recipe());status_->setText(tr("范围已设置；拖动两条线调整位置和倾斜角度，再调节手动增高。"));}
                    catch(const std::exception& e){settings=previous;manual_height_points_=previous["manualHeightRange"].toArray();status_->setText(QString::fromUtf8(e.what()));}update_overlay();
                }
            }return true;
        }
    }
    if(watched==view_->viewport() && (event->type()==QEvent::FocusOut || event->type()==QEvent::UngrabMouse))finish_brush_gesture();
    if(event->type()==QEvent::MouseButtonDblClick && watched->property("resetParameter").isValid()){
        if(auto* slider=findChild<QSlider*>("portraitSlider_"+watched->property("resetParameter").toString()))slider->setValue(0);return true;
    }

    if(auto* slider=qobject_cast<QSlider*>(watched);slider && event->type()==QEvent::MouseButtonDblClick){
        finish_gesture();slider->setValue(slider->property("resetValue").toInt());return true;
    }
    if(watched==view_->viewport() && selected_>=0 && tattoo_event(event))return true;
    if(watched==view_->viewport() && selected_>=0 && nail_event(event))return true;
    if(watched==view_->viewport() && selected_>=0 && liquify_event(event))return true;
    if(watched==view_->viewport() && selected_>=0 && repair_event(event))return true;
    if(watched==view_->viewport() && selected_>=0 && placement_key_.startsWith("gradient:") && !view_->temporary_pan()){
        const auto type=event->type();if(type!=QEvent::MouseButtonPress && type!=QEvent::MouseMove && type!=QEvent::MouseButtonRelease)return false;
        auto* mouse=static_cast<QMouseEvent*>(event);const auto original=source_position(view_->mapToScene(mouse->position().toPoint()));const QPointF point(original.x()*preview_.width(),original.y()*preview_.height());auto& settings=people_[std::size_t(selected_)].settings;auto zones=settings["hairZones"].toArray();int index=-1;for(int i=0;i<zones.size();++i)if(zones[i].toObject()["key"].toString()==placement_key_.mid(9)){index=i;break;}if(index<0)return false;auto zone=zones[index].toObject();auto axis=zone["gradientAxis"].toArray();
        if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton && preview_.rect().contains(point.toPoint())){finish_gesture();gesture_=true;placement_drawing_=true;gradient_handle_=1;const double radius=12/std::max(.01,view_->transform().m11());if(axis.size()==4){const QPointF a(axis[0].toDouble()*preview_.width(),axis[1].toDouble()*preview_.height()),b(axis[2].toDouble()*preview_.width(),axis[3].toDouble()*preview_.height());const double da=QLineF(point,a).length(),db=QLineF(point,b).length();if(QLineF(point,a+(b-a)*zone["gradientMidpoint"].toDouble(.5)).length()<radius)gradient_handle_=2;else if(std::min(da,db)<radius)gradient_handle_=da<db?0:1;else axis={point.x()/preview_.width(),point.y()/preview_.height(),point.x()/preview_.width(),point.y()/preview_.height()};}else axis={point.x()/preview_.width(),point.y()/preview_.height(),point.x()/preview_.width(),point.y()/preview_.height()};view_->setFocus();view_->viewport()->grabMouse();}
        if(!placement_drawing_ || axis.size()!=4)return true;if(gradient_handle_==2){const QPointF a(axis[0].toDouble()*preview_.width(),axis[1].toDouble()*preview_.height()),b(axis[2].toDouble()*preview_.width(),axis[3].toDouble()*preview_.height()),d=b-a;zone["gradientMidpoint"]=std::clamp(QPointF::dotProduct(point-a,d)/std::max(1.,QPointF::dotProduct(d,d)),.05,.95);}else{axis[gradient_handle_*2]=std::clamp(point.x()/preview_.width(),0.,1.);axis[gradient_handle_*2+1]=std::clamp(point.y()/preview_.height(),0.,1.);}zone["gradientAxis"]=axis;zone["mode"]="gradient";zones[index]=zone;settings["hairZones"]=zones;remember();update_overlay();if(type==QEvent::MouseButtonRelease){placement_drawing_=false;view_->viewport()->releaseMouse();finish_gesture();}return true;
    }
    if(watched==view_->viewport() && selected_>=0 && placement_key_=="body:back" && !view_->temporary_pan()){
        if(event->type()==QEvent::MouseButtonPress){auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;const auto scene=view_->mapToScene(mouse->position().toPoint());if(!preview_.rect().contains(scene.toPoint()))return true;
            const auto point=source_position(scene);back_placement_.append(QJsonArray{point.x(),point.y()});
            if(back_placement_.size()<3){update_overlay();status_->setText(back_placement_.size()==1?tr("点选上背最凸处的可见轮廓"):tr("点选这段上背轮廓的下端"));return true;}
            auto& person=people_[std::size_t(selected_)];auto candidate=editing::portrait_person_json(person);auto settings=person.settings;settings["backContour"]=back_placement_;candidate["settings"]=settings;
            const auto reason=editing::portrait_back_unavailable(candidate,linear_preview_.size());if(!reason.isEmpty()){back_placement_={};update_overlay();status_->setText(reason+tr("；请从上端重新点选"));return true;}
            finish_gesture();person.settings=settings;remember();placement_key_.clear();view_->set_tool(0);update_overlay();build_parameters();status_->setText(tr("上背轮廓已定位，调整“改善驼背”可减弱后凸；头部与定位两端保持不动"));return true;
        }if(event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseMove)return true;
    }
    if(watched==view_->viewport() && selected_>=0 && placement_key_=="body:chest" && !view_->temporary_pan()){
        if(event->type()==QEvent::MouseButtonPress){auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::RightButton){if(!chest_placement_points_.isEmpty())chest_placement_points_.removeLast();update_overlay();status_->setText(tr("已退回上一个定位点；继续点选，抓手可取消"));return true;}
            if(mouse->button()!=Qt::LeftButton)return false;
            const auto scene=view_->mapToScene(mouse->position().toPoint());if(!preview_.rect().contains(scene.toPoint()))return true;
            const auto point=source_position(scene);chest_placement_points_.append(QJsonArray{point.x(),point.y()});
            if(chest_placement_points_.size()<3){update_overlay();status_->setText(chest_placement_points_.size()==1?tr("再点胸部轮廓的调整处；右键可退回"):tr("最后点胸部轮廓的下端；右键可退回"));return true;}
            auto& person=people_[std::size_t(selected_)];auto settings=person.settings;const auto pose=settings["pose"].toArray();
            if(pose.size()!=33){chest_placement_points_=QJsonArray{};update_overlay();status_->setText(tr("肩部定位不足，请先修正人物定位"));return true;}
            const auto pixel=[this](QJsonArray p){return QPointF(p[0].toDouble()*source_.width(),p[1].toDouble()*source_.height());};
            const auto left=pixel(pose[11].toArray()),right=pixel(pose[12].toArray());
            const auto sideFor=[&](const QJsonArray& path){return QPointF::dotProduct(pixel(path[1].toArray())-(left+right)*.5,left-right)>=0?0:1;};
            const int chestSide=sideFor(chest_placement_points_);const QString sideKey=chestSide==0?"left":"right";
            auto paths=settings["chestContourPaths"].toObject();auto parameters=settings["parameters"].toObject();
            const auto legacy=settings["chestContourPath"].toArray();
            if(paths.isEmpty() && legacy.size()==3){const QString oldSide=sideFor(legacy)==0?"left":"right";paths[oldSide]=legacy;parameters["body.chest."+oldSide]=parameters["body.chest"].toDouble();}
            parameters.remove("body.chest");settings.remove("chestContourPath");settings.remove("chestContour");
            paths[sideKey]=chest_placement_points_;settings["chestContourPaths"]=paths;settings["parameters"]=parameters;settings["side"]=chestSide;settings["linked"]=false;chest_placement_points_=QJsonArray{};
            auto candidate=editing::portrait_person_json(person);auto trial=settings;auto params=parameters;params["body.chest."+sideKey]=params["body.chest."+sideKey].toDouble()==0?1:params["body.chest."+sideKey].toDouble();trial["parameters"]=params;candidate["settings"]=trial;candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);
            try{editing::portrait_deformation(linear_preview_.size(),QJsonObject{{"version",1},{"people",QJsonArray{candidate}}},1,{},nullptr,true);}catch(const std::exception& error){update_overlay();status_->setText(QString::fromUtf8(error.what())+tr("；请重新从上端定位三点"));return true;}
            finish_gesture();person.settings=settings;{const QSignalBlocker sideBlock(side_),linkBlock(linked_);side_->setCurrentIndex(chestSide);linked_->setChecked(false);}remember();placement_key_.clear();view_->set_tool(0);update_overlay();build_parameters();status_->setText(tr("已定位人物%1侧胸部轮廓；另一侧设置保留，可切换左右独立调整").arg(chestSide==0?tr("左"):tr("右")));return true;
        }if(event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseMove)return true;
    }
    if(watched==view_->viewport() && selected_>=0 && placement_key_=="body:abdomen" && !view_->temporary_pan()){
        if(event->type()==QEvent::MouseButtonPress){auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;const auto scene=view_->mapToScene(mouse->position().toPoint());if(!preview_.rect().contains(scene.toPoint()))return true;
            const auto point=source_position(scene);auto& person=people_[std::size_t(selected_)];auto candidate=editing::portrait_person_json(person);auto settings=person.settings;settings["abdomenFront"]=QJsonArray{point.x(),point.y()};candidate["settings"]=settings;candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);
            const auto reason=editing::portrait_abdomen_unavailable(candidate,linear_preview_.size(),true);if(!reason.isEmpty()){status_->setText(reason);return true;}
            finish_gesture();person.settings=settings;remember();placement_key_.clear();view_->set_tool(0);update_overlay();build_parameters();status_->setText(tr("已定位腹部前缘；收腹与局部瘦肚子只改变这一侧，后背保持不动"));return true;
        }if(event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseMove)return true;
    }
    if(watched==view_->viewport() && selected_>=0 && !placement_key_.isEmpty() && !view_->temporary_pan()){
        const auto type=event->type();if(type==QEvent::MouseButtonPress || type==QEvent::MouseMove || type==QEvent::MouseButtonRelease){
            auto* mouse=static_cast<QMouseEvent*>(event);const auto point=view_->mapToScene(mouse->position().toPoint());
            if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton && preview_.rect().contains(point.toPoint())){accessory_handle_=0;if(placement_key_=="hair.accessory"){const auto person=editing::portrait_person_json(people_[std::size_t(selected_)]);const auto t=editing::portrait_accessory_transform(person,person["settings"].toObject()["parameters"].toObject(),preview_.size());const double tolerance=10/std::max(.01,view_->transform().m11());if(QLineF(point,t.map(QPointF(1.1,.9))).length()<tolerance)accessory_handle_=1;else if(QLineF(point,t.map(QPointF(0,-1.2))).length()<tolerance)accessory_handle_=2;}
                placement_last_=point;placement_drawing_=true;gesture_=true;if(placement_key_=="makeup.sticker"){const auto uv=editing::portrait_sticker_surface(editing::portrait_person_json(people_[std::size_t(selected_)]),preview_.size()).inverted().map(point);set_parameter("makeup.stickerX",uv.x()*100);set_parameter("makeup.stickerY",uv.y()*100);update_overlay();}if(placement_key_=="makeup.mole"){const auto record=editing::portrait_person_json(people_[std::size_t(selected_)]);const auto uv=editing::portrait_mole_transform(record,preview_.size()).inverted().map(point);const auto params=record["settings"].toObject()["parameters"].toObject();set_parameter("makeup.moleX",params["makeup.moleX"].toDouble()+uv.x()*100);set_parameter("makeup.moleY",params["makeup.moleY"].toDouble()+uv.y()*100);update_overlay();}view_->setFocus();view_->viewport()->grabMouse();return true;}
            if(placement_drawing_){auto delta=point-placement_last_;placement_last_=point;const auto& person=people_[std::size_t(selected_)];const auto record=editing::portrait_person_json(person);const auto face=record["face"].toArray();double unit_x=face[2].toDouble()*preview_.width()*.01,unit_y=face[3].toDouble()*preview_.height()*.01;
                if(placement_key_=="makeup.catch"){const int start=person.settings["side"].toInt()==0?42:36;const auto a=person.landmarks[start].toArray(),b=person.landmarks[start+3].toArray();unit_x=unit_y=QLineF(QPointF(a[0].toDouble()*preview_.width(),a[1].toDouble()*preview_.height()),QPointF(b[0].toDouble()*preview_.width(),b[1].toDouble()*preview_.height())).length()*.002;if(editing::portrait_side_value(person.settings,"irisMaterialVersion",person.settings.value("side").toInt()==0).toInt()>=2){const QString side=person.settings["side"].toInt()==0?".left":".right";const auto ellipse=person.settings["irisMaterials"].toObject()["geometry"+side].toObject()["ellipse"].toArray();if(ellipse.size()==5){const double angle=ellipse[4].toDouble()*3.141592653589793/180.;delta={delta.x()*std::cos(angle)+delta.y()*std::sin(angle),-delta.x()*std::sin(angle)+delta.y()*std::cos(angle)};unit_x=ellipse[2].toDouble()*std::min(preview_.width(),preview_.height())*.01;unit_y=ellipse[3].toDouble()*std::min(preview_.width(),preview_.height())*.01;}}}
                if(placement_key_=="makeup.sticker"){const auto inverse=editing::portrait_sticker_surface(record,preview_.size()).inverted();delta=inverse.map(point)-inverse.map(point-delta);unit_x=unit_y=.01;}
                if(placement_key_=="makeup.mole"){const auto t=editing::portrait_mole_transform(record,preview_.size());const double span=std::hypot(t.m11(),t.m12());if(span>1){const QPointF a(t.m11()/span,t.m12()/span),b(t.m21()/span,t.m22()/span);delta={QPointF::dotProduct(delta,a),QPointF::dotProduct(delta,b)};unit_x=unit_y=span*.01;}}
                if(placement_key_=="contour.lines"){const auto pose=person.settings["pose"].toArray();if(pose.size()==33){auto pt=[&](int i){const auto a=pose[i].toArray();return QPointF(a[0].toDouble()*preview_.width(),a[1].toDouble()*preview_.height());};unit_x=QLineF(pt(11),pt(12)).length()*.01;unit_y=QLineF((pt(11)+pt(12))*.5,(pt(23)+pt(24))*.5).length()*.01;}}
                const auto parameters=person.settings["parameters"].toObject();const QString suffix=(placement_key_=="makeup.catch" || placement_key_=="makeup.blush" || placement_key_=="makeup.iris")?(person.settings["side"].toInt()==0?".left":".right"):"";
                if(placement_key_=="hair.accessory" && accessory_handle_){const auto t=editing::portrait_accessory_transform(record,parameters,preview_.size());const auto v=point-t.map(QPointF(0,0));if(accessory_handle_==1)set_parameter("hair.accessorySize",(std::hypot(v.x(),v.y())/std::hypot(1.1,.9)/std::max(.01,unit_x*100)-.12)/.003);else{double angle=std::atan2(v.y(),v.x())*180/3.141592653589793+90;if(angle>180)angle-=360;set_parameter("hair.accessoryAngle",angle/1.8);}}
                else{set_parameter(placement_key_+"X",parameters[placement_key_+"X"+suffix].toDouble()+delta.x()/std::max(.01,unit_x));set_parameter(placement_key_+"Y",parameters[placement_key_+"Y"+suffix].toDouble()+delta.y()/std::max(.01,unit_y));}update_overlay();
                if(type==QEvent::MouseButtonRelease){placement_drawing_=false;view_->viewport()->releaseMouse();finish_gesture();build_parameters();}return true;
            }
        }
    }
    if(watched==view_->viewport() && selected_>=0 && !property("portraitMaterialSampleKey").toString().isEmpty() && !view_->temporary_pan() && event->type()==QEvent::MouseButtonPress){
        const auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::LeftButton){const auto scene=view_->mapToScene(mouse->position().toPoint());if(!QRectF(preview_.rect()).contains(scene))return true;const auto pos=source_position(scene);const auto color=editing::portrait_sample_skin_color(source_,{int(pos.x()*source_.width()),int(pos.y()*source_.height())});if(color.isEmpty())return true;auto& settings=people_[std::size_t(selected_)].settings;const QString key=property("portraitMaterialSampleKey").toString();if(property("portraitMaterialSampleEnd").toBool()){auto zones=settings["hairZones"].toArray();for(int i=0;i<zones.size();++i){auto z=zones[i].toObject();if(z["key"].toString()==key){z["secondColor"]=color;zones[i]=z;}}settings["hairZones"]=zones;}else{editing::portrait_set_style_color(settings,key,color);editing::portrait_set_side_value(settings,"selectedColor_"+key,QString{});}auto params=settings["parameters"].toObject();const auto descriptor=editing::portrait_parameter(key);const bool sided=descriptor && descriptor->bilateral;const QString suffix=settings["side"].toInt()==0?".left":".right";if(sided){for(const auto& part:QStringList{".left",".right"})if(settings["linked"].toBool(true)||part==suffix)if(params[key+part].toDouble(params[key].toDouble())==0)params[key+part]=50;}else if(params[key].toDouble()==0)params[key]=50;settings["parameters"]=params;settings["hairColorVersion"]=2;setProperty("portraitMaterialSampleKey",QString{});remember();build_parameters();activate_panel_tool();return true;}
    }
    if(watched==view_->viewport() && selected_>=0 && navigation_->currentRow()==6 && color_sampling_ && !view_->temporary_pan() && event->type()==QEvent::MouseButtonPress){
        const auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::LeftButton){
            const auto scene=view_->mapToScene(mouse->position().toPoint());if(!QRectF(preview_.rect()).contains(scene))return true;
            const auto pos=source_position(scene);const int x=int(pos.x()*source_.width()),y=int(pos.y()*source_.height());
            if(source_.rect().contains(x,y)){
                if(balance_sampling_){auto person=editing::portrait_person_json(people_[std::size_t(selected_)]);auto candidate=person["settings"].toObject();const double radius=std::max(5.,people_[std::size_t(selected_)].face.width()*source_.width()*.04)/std::min(source_.width(),source_.height());candidate["balanceReference"]=QJsonArray{pos.x(),pos.y(),radius};person["settings"]=candidate;
                    if(editing::portrait_balance_reference(source_,person).size()!=3){status_->setText(tr("此处可靠皮肤不足，请避开高光、深阴影、毛发或衣物"));return true;}
                    auto& settings=people_[std::size_t(selected_)].settings;settings["balanceReference"]=candidate["balanceReference"];color_sampling_=false;balance_sampling_=false;view_->set_tool(color_previous_tool_);const double current=settings["parameters"].toObject()["color.balance"].toDouble();set_parameter("color.balance",current>0?current:50);build_parameters();status_->setText(tr("已设置肤色协调参照区，保持原有明暗"));return true;
                }
                const auto color=editing::portrait_sample_skin_color(source_,{x,y});
                if(!color.isEmpty()){
                    auto& settings=people_[std::size_t(selected_)].settings;settings["skinColor"]=color;
                    settings["skinSample"]=QJsonArray{pos.x(),pos.y(),2};settings["skinColorName"]=tr("画面取样");
                    color_sampling_=false;view_->set_tool(color_previous_tool_);
                    const double current=settings["parameters"].toObject()["color.custom"].toDouble();set_parameter("color.custom",current>0?current:50);build_parameters();
                    status_->setText(tr("已从 5 × 5 像素邻域取肤色，已退出取色"));
                }else status_->setText(tr("此处没有可取样的不透明像素，请选择皮肤区域"));
                return true;
            }
        }
    }
    if(watched==view_->viewport() && selected_>=0 && range_mode_>0 && !view_->temporary_pan() && !range_key_->currentData().toString().isEmpty()) {
        const auto type=event->type();
        if(type==QEvent::MouseButtonPress || type==QEvent::MouseMove || type==QEvent::MouseButtonRelease) {
            auto* mouse=static_cast<QMouseEvent*>(event);const auto pos=view_->mapToScene(mouse->position().toPoint());
            if(type==QEvent::MouseMove){QPainterPath cursor;const double radius=brush_size_->value()*.5*preview_.width()/source_.width();cursor.addEllipse(pos,radius,radius);overlay_->setPath(cursor);}
            if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton && preview_.rect().contains(pos.toPoint())) {
                if(range_key_->currentData().toString()=="__protection")people_[std::size_t(selected_)].settings=editing::portrait_capture_protection(source_.size(),recipe(),selected_);
                if(range_key_->currentData().toString()=="contour.paint" && people_[std::size_t(selected_)].settings.value("contourLocked").toBool()){status_->setText(tr("修容范围已锁定，可直接换色或调整强度；解锁后才能增减范围"));return true;}view_->setFocus();view_->viewport()->grabMouse();painting_=true;painting_mask_=editing::portrait_effect_mask(editing::portrait_person_json(people_[std::size_t(selected_)]),range_key_->currentData().toString(),range_key_->currentData().toString()=="hair.edge"?source_.size().scaled(2048,2048,Qt::KeepAspectRatio):preview_.size().scaled(768,768,Qt::KeepAspectRatio));
                const auto key=range_key_->currentData().toString();if(key.startsWith("repair.item.") || key=="hair.accessory" || key=="makeup.sticker" || key=="makeup.nails" || key=="makeup.tattoo" || key=="makeup.mole" || key=="makeup.freckles" || key=="contour.lines" || key.startsWith("makeup.catchlight")){const auto saved=people_[std::size_t(selected_)].settings.value("masks").toObject()[key].toString();if(saved.isEmpty())painting_mask_.fill(255);else painting_mask_=QImage::fromData(QByteArray::fromBase64(saved.toLatin1()),"PNG").convertToFormat(QImage::Format_Grayscale8).scaled(painting_mask_.size());}
                if(key=="hair.accessory"){auto& settings=people_[std::size_t(selected_)].settings;painting_mask_=editing::portrait_accessory_coverage(editing::portrait_person_json(people_[std::size_t(selected_)]),painting_mask_.size());if(!settings.contains("hairAccessoryMaskFollows"))settings["hairAccessoryMaskFollows"]=!settings["masks"].toObject().contains(key);if(settings["hairAccessoryMaskFollows"].toBool()){QJsonObject anchor;for(const auto* name:{"hair.accessoryX","hair.accessoryY","hair.accessorySize","hair.accessoryAngle"})anchor[name]=settings["parameters"].toObject()[name].toDouble();settings["hairAccessoryMaskAnchor"]=anchor;}}
                if(range_key_->currentData().toString().startsWith("wrinkle.erase."))painting_mask_.fill(0);
                painting_base_=painting_mask_;painting_coverage_=QImage(painting_mask_.size(),QImage::Format_Grayscale8);painting_coverage_.fill(0);painting_mesh_=editing::portrait_deformation(source_.size(),recipe());painting_last_=pos;paint_range(pos);return true;
            }
            if(painting_){paint_range(pos);if(type==QEvent::MouseButtonRelease){auto& settings=people_[std::size_t(selected_)].settings;const auto key=range_key_->currentData().toString();
    store_range(settings,key,painting_mask_);painting_=false;view_->viewport()->releaseMouse();remember();update_range_overlay();}return true;}
        }
    }
    if(watched==view_->viewport() && selected_>=0 && locate_->isChecked()) {
        const auto type=event->type();
        if(type==QEvent::MouseButtonPress || type==QEvent::MouseMove || type==QEvent::MouseButtonRelease) {
            auto* mouse=static_cast<QMouseEvent*>(event);auto& person=people_[std::size_t(selected_)];
            const bool body=navigation_->currentRow()==3;auto points=body?person.settings.value("pose").toArray():person.landmarks;
            const auto position=view_->mapToScene(mouse->position().toPoint());
            if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton) {
                double closest=10./view_->transform().m11();dragged_point_=-1;
                for(int i=0;i<points.size();++i){const auto part=property("portraitMakeupLocate").toString();if(navigation_->currentRow()==11 && !part.isEmpty()){const bool left=person.settings["side"].toInt()==0,linked=person.settings["linked"].toBool(true);const bool allowed=part=="brow"?(i>=17 && i<27 && (linked || (left?i>=22:i<22))):(i>=36 && i<48 && (linked || (left?i>=42:i<42)));if(!allowed)continue;}const auto a=points[i].toArray();const double distance=QLineF(position,{a[0].toDouble()*preview_.width(),a[1].toDouble()*preview_.height()}).length();if(distance<closest){closest=distance;dragged_point_=i;}}
                if(!body && dragged_point_>=0){drag_landmarks_=person.landmarks;drag_surface_=person.settings["faceSurface"].toObject();}
            }
            if(dragged_point_>=0) {
                points[dragged_point_]=QJsonArray{std::clamp(position.x()/preview_.width(),0.,1.),std::clamp(position.y()/preview_.height(),0.,1.),1.};
                if(body){person.settings["pose"]=points;person.settings["poseCorrected"]=true;}
                else{person.landmarks=points;person.settings["landmarksCorrected"]=true;}
                update_overlay();
                if(type==QEvent::MouseButtonRelease){
                    dragged_point_=-1;
                    if(!body){
                        if(person.settings["doubleChinVersion"].toInt()>=2){
                            const auto analysis=editing::portrait_double_chin_analysis(linear_preview_,editing::portrait_person_json(person));
                            person.settings["doubleChinAnalysis"]=analysis;
                            if(analysis.isEmpty()){auto parameters=person.settings["parameters"].toObject();parameters["face.doubleChin"]=0;person.settings["parameters"]=parameters;status_->setText(tr("定位已更新；颏下范围不足，去双下巴已归零，其他调整保留"));}
                        }
                        if(!drag_surface_.isEmpty())person.settings["faceSurface"]=corrected_surface(drag_surface_,drag_landmarks_,person.landmarks,preview_.size());
                        auto masks=person.settings["automaticMasks"].toObject();
                        if(person.settings.contains("scalpGeometry"))person.settings["scalpGeometry"]=editing::portrait_scalp_geometry(editing::portrait_person_json(person));
                        if(masks.contains("head"))masks["head"]=editing::portrait_mask_png(editing::portrait_head_region(editing::portrait_person_json(person)));
                        if(masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));
                        person.settings["automaticMasks"]=masks;
                        if(person.settings["parameters"].toObject()["face.tearFill.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.tearFill.right"].toDouble()!=0)person.settings["tearVolume"]=editing::portrait_tear_volume(linear_preview_,editing::portrait_person_json(person));if(person.settings["parameters"].toObject()["face.malarTone.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.malarTone.right"].toDouble()!=0)person.settings["malarVolume"]=editing::portrait_malar_volume(linear_preview_,editing::portrait_person_json(person));if(person.settings["parameters"].toObject()["face.cheekTone.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.cheekTone.right"].toDouble()!=0)person.settings["cheekVolume"]=editing::portrait_cheek_volume(linear_preview_,editing::portrait_person_json(person));if(person.settings["parameters"].toObject()["face.orbitalTone.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.orbitalTone.right"].toDouble()!=0)person.settings["orbitalVolume"]=editing::portrait_orbital_volume(linear_preview_,editing::portrait_person_json(person));if(person.settings["parameters"].toObject()["face.perioralTone.left"].toDouble()!=0 || person.settings["parameters"].toObject()["face.perioralTone.right"].toDouble()!=0)person.settings["perioralVolume"]=editing::portrait_perioral_volume(linear_preview_,editing::portrait_person_json(person));
                        if(person.settings["parameters"].toObject()["face.foreheadFill"].toDouble()!=0)person.settings["foreheadVolume"]=editing::portrait_forehead_volume(linear_preview_,editing::portrait_person_json(person));
                    }
                    remember();
                }
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched,event);
}
void PortraitDialog::confirm_person(int previous) {
    if(selected_<0 || people_[std::size_t(selected_)].unmatched)return;
    auto& target=people_[std::size_t(selected_)];
    if(previous>=0) {
        if(previous>=int(people_.size()) || previous==selected_ || !people_[std::size_t(previous)].unmatched)return;
        const auto old=people_[std::size_t(previous)];target.settings=old.settings;target.id=old.id;
        target.match_ambiguous=false;people_.erase(people_.begin()+previous);
        if(previous<selected_)--selected_;
    } else target.match_ambiguous=false;
    const int selected=selected_;populate();faces_->setCurrentRow(selected);
    status_->setText(tr("人物关联已确认，已保留原参数"));remember();
}
void PortraitDialog::focus_person(int mode) {
    if((mode==1 || mode==2) && selected_<0)return;
    QRectF rect=preview_.rect();
    if(mode==1 || mode==2) {
        const auto& person=people_[std::size_t(selected_)];const auto area=mode==1?person.face:person.body;
        if(area.isEmpty() || (mode==2 && person.body_ambiguous)){status_->setText(tr("身体关联不确定，暂不能自动定位全身"));return;}
        rect={area.x()*preview_.width(),area.y()*preview_.height(),area.width()*preview_.width(),area.height()*preview_.height()};
        rect=rect.adjusted(-rect.width()*.25,-rect.height()*.3,rect.width()*.25,rect.height()*.25).intersected(preview_.rect());
    }
    if(mode==3){view_->resetTransform();view_->scale(double(source_.width())/preview_.width(),double(source_.width())/preview_.width());}
    else view_->fitInView(rect,Qt::KeepAspectRatio);
    focus_mode_=mode;
    detail_timer_.start();
}
void PortraitDialog::change_panel(int index) {
    if(index<0)return;
    setProperty("portraitMakeupLocate",QString{});
    if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->setChecked(false);
    finish_brush_gesture();finish_gesture();
    auto* scroll=findChild<QScrollArea*>("portraitParameterScroll");
    const QString old_page=property("portraitActivePage").toString();
    if(scroll && !old_page.isEmpty())setProperty(qPrintable("portraitScroll_"+old_page),scroll->verticalScrollBar()->value());
    const QString page=QString("%1_%2").arg(property("portraitWorkspaceTask").toInt()).arg(index);
    setProperty("portraitActivePage",page);setProperty("portraitResumePlacement",QString{});setProperty("portraitResumeRange",0);
    QTimer::singleShot(0,this,[this,page,scroll]{if(scroll && property("portraitActivePage").toString()==page)scroll->verticalScrollBar()->setValue(property(qPrintable("portraitScroll_"+page)).toInt());});
    group_bypass_.clear();placement_key_.clear();placement_drawing_=false;finish_liquify();finish_preset(true);finish_gesture();liquify_tool_=-1;
    setProperty("portraitMaterialSampleKey",QString{});repair_mode_=0;repair_selected_=-1;repair_drawing_=false;repair_has_source_=false;color_sampling_=false;
    const bool bilateral=index==1 || index==3 || index==11;side_->hide();linked_->hide();if(auto* sides=findChild<QWidget*>("portraitSides"))sides->setVisible(bilateral);
    const auto name=navigation_->item(index)->text();context_->setText(name+tr(" · 仅编辑当前人物"));
    panel_description_->setText(QString{});
    update_overlay();
    build_parameters();
    for(const auto* toolName:{"portraitCanvasBrush","portraitCanvasHand"})if(auto* button=findChild<QPushButton*>(toolName))button->setVisible(true);
    refresh_ranges();range_mode_=0;view_->set_tool(0);activate_panel_tool();update_range_overlay();
}
void PortraitDialog::activate_panel_tool(){
    view_->setProperty("portraitBrushCursor",false);
    update_overlay();
    if(selected_<0)return;
    if(property("portraitWorkspaceTask").toInt()==11 && people_[std::size_t(selected_)].settings["headManualEnabled"].toBool()){placement_key_="head:outline";view_->set_tool(-1);update_overlay();return;}
    const int panel=navigation_->currentRow();
    if(panel==10 && property("portraitTeethMode").toInt()==1){activate_teeth_brush();return;}
    if(panel==9 && property("portraitWorkspaceTask").toInt()==14 && property("portraitEyeMode").toInt()==1){activate_eye_brush();return;}
    if(property("portraitWorkspaceTask").toInt()==12){if(property("portraitPaintType").toInt()!=5 || property("portraitPaintLightMode").toInt()==1)activate_paint_brush();return;}
    if(panel==3 && property("portraitWorkspaceTask").toInt()==8){if(property("portraitBodyMode").toInt()==1)activate_body_manual(property("portraitBodyTool").toInt());else if(property("portraitBodyMode").toInt()==2 && people_[std::size_t(selected_)].settings["bodyLinesQuad"].toArray().size()==4){placement_key_="body:lines";painting_mesh_=editing::portrait_deformation(source_.size(),recipe());view_->set_tool(-1);update_overlay();}return;}
    if(panel==7 || panel==8)view_->setProperty("portraitBrushCursor",true);
    if(panel==4){if(property("portraitSlimMode").toInt()==0){liquify_tool_=-1;view_->set_tool(0);return;}select_liquify_tool(property("portraitSlimTool").toInt());if(!liquify_diameter_)liquify_diameter_=std::clamp(source_.width()/12,80,500);brush_size_->setValue(liquify_diameter_);brush_feather_->setValue(100-liquify_hardness_);view_->set_tool(-1);}
    else if(panel==5 && property("portraitWorkspaceTask").toInt()==7 && property("portraitOilMode").toInt()==1){activate_skin_brush(1);}
    else if(panel==5 && property("portraitWorkspaceTask").toInt()==4 && property("portraitSkinMode").toInt()==1){activate_skin_brush(1);}
    else if(panel==7 && property("portraitWorkspaceTask").toInt()==5){activate_blemish_tool();}
    else if(panel==8){activate_wrinkle_tool();}
    else if(panel==7){repair_mode_=panel==8?2:(property("portraitWorkspaceTask").toInt()==10 && property("portraitEraseMode").toInt()==1?5:1);view_->set_tool(-1);}
    else if(panel==12 && (people_[std::size_t(selected_)].settings["hairLighting"].toObject()["flowVersion"].toInt()<2 || people_[std::size_t(selected_)].settings["hairLighting"].toObject()["oilVersion"].toInt()<1 || (people_[std::size_t(selected_)].settings["hairEdgeVersion"].toInt()>=1 && people_[std::size_t(selected_)].settings["hairLighting"].toObject()["edgeVersion"].toInt()<2)))QTimer::singleShot(0,this,&PortraitDialog::analyze_repairs);
    else if((panel==9 || panel==11) && (!people_[std::size_t(selected_)].settings.contains("irisMaterials") || (panel==11 && (!people_[std::size_t(selected_)].settings.contains("browMaterials") || !people_[std::size_t(selected_)].settings.contains("lipLighting") || !people_[std::size_t(selected_)].settings.contains("cheekLighting")))))QTimer::singleShot(0,this,&PortraitDialog::analyze_repairs);
    else if((panel==9 || panel==10) && !people_[std::size_t(selected_)].settings["automaticMasks"].toObject().contains(panel==9?"eye.circles":"teeth"))QTimer::singleShot(0,this,&PortraitDialog::analyze_repairs);
    if(panel==12 && !property("portraitHairBrushKey").toString().isEmpty())activate_hair_brush(property("portraitHairBrushKey").toString(),property("portraitHairBrushMode").toInt());
    if(panel==12 && property("portraitHairPage").toInt()==0 && property("portraitHairVolume").toInt()==3){repair_mode_=4;view_->set_tool(-1);}
}
void PortraitDialog::paint_range(QPointF position) {
    if(selected_<0 || painting_mask_.isNull())return;
    const double radius=brush_size_->value()*.5,feather=brush_feather_->value()*.01;
    const int steps=painting_mesh_.isNull()?1:std::clamp(int(QLineF(painting_last_,position).length()*source_.width()/preview_.width()/std::max(1.,radius*.25))+1,1,128);
    const auto original=[&](QPointF scene){QPointF p(scene.x()/preview_.width(),scene.y()/preview_.height());if(!painting_mesh_.isNull()){const int x=std::clamp(int(p.x()*(painting_mesh_.width()-1)),0,painting_mesh_.width()-1),y=std::clamp(int(p.y()*(painting_mesh_.height()-1)),0,painting_mesh_.height()-1);const auto* offset=reinterpret_cast<const float*>(painting_mesh_.constScanLine(y))+x*4;p+=QPointF(offset[0],offset[1]);}return QPointF(p.x()*source_.width(),p.y()*source_.height());};
    auto a=original(painting_last_);
    for(int step=1;step<=steps;++step){const auto b=original(painting_last_+(position-painting_last_)*(double(step)/steps));const auto delta=b-a;const double length2=QPointF::dotProduct(delta,delta);
        const double sx=double(painting_mask_.width())/source_.width(),sy=double(painting_mask_.height())/source_.height();
        const int x0=std::max(0,int((std::min(a.x(),b.x())-radius)*sx)-1),x1=std::min(painting_mask_.width()-1,int((std::max(a.x(),b.x())+radius)*sx)+1),y0=std::max(0,int((std::min(a.y(),b.y())-radius)*sy)-1),y1=std::min(painting_mask_.height()-1,int((std::max(a.y(),b.y())+radius)*sy)+1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){const QPointF pixel(x/sx,y/sy);const double along=length2>1e-12?std::clamp(QPointF::dotProduct(pixel-a,delta)/length2,0.,1.):0.;const double d=QLineF(pixel,a+delta*along).length()/radius;if(d>=1)continue;const double t=std::clamp((1-d)/std::max(.001,feather),0.,1.);auto& coverage=painting_coverage_.scanLine(y)[x];coverage=std::max(coverage,uchar(std::round(t*t*(3-2*t)*255)));const double weight=coverage/255.*((navigation_->currentRow()==4 && range_key_->currentData().toString()=="__protection" && property("portraitSlimTool").toInt()>=7)?liquify_pressure_:1.),base=painting_base_.constScanLine(y)[x];painting_mask_.scanLine(y)[x]=uchar(std::round(range_mode_==1?base+(255-base)*weight:base*(1-weight)));}
        a=b;
    }
    painting_last_=position;schedule_preview();
    if(!overlay_clock_.isValid() || overlay_clock_.elapsed()>=16){overlay_clock_.restart();update_range_overlay();}
}
void PortraitDialog::update_range_overlay() {
    if(!range_pixels_ || !range_visible_)return;
    const auto key=range_key_->currentData().toString();if(selected_<0 || key.isEmpty() || !range_visible_->isChecked() || comparing_ || locate_->isChecked()){range_pixels_->hide();return;}
    const auto size=preview_.size().scaled(512,512,Qt::KeepAspectRatio);const auto mask=painting_?painting_mask_.scaled(size):editing::portrait_effect_mask(editing::portrait_person_json(people_[std::size_t(selected_)]),key,size);
    const auto mesh=painting_?painting_mesh_:editing::portrait_deformation(source_.size(),recipe());QImage overlay(size,QImage::Format_RGBA8888);
    for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){double nx=double(x)/size.width(),ny=double(y)/size.height();if(!mesh.isNull()){const auto* p=reinterpret_cast<const float*>(mesh.constScanLine(std::clamp(int(ny*(mesh.height()-1)),0,mesh.height()-1)))+std::clamp(int(nx*(mesh.width()-1)),0,mesh.width()-1)*4;nx+=p[0];ny+=p[1];}
        auto* pixel=overlay.scanLine(y)+x*4;pixel[0]=key=="__protection"?255:50;pixel[1]=key=="__protection"?70:155;pixel[2]=key=="__protection"?70:255;pixel[3]=uchar(mask.constScanLine(std::clamp(int(ny*size.height()),0,size.height()-1))[std::clamp(int(nx*size.width()),0,size.width()-1)]*.4);}
    range_pixels_->setPixmap(QPixmap::fromImage(overlay));range_pixels_->setScale(double(preview_.width())/size.width());range_pixels_->show();
}
void PortraitDialog::set_slim_amount(int value){
    if(selected_<0)return;
    if(property("portraitSlimCard").toInt()==0){set_parameter("face.small",value);const int actual=people_[std::size_t(selected_)].settings["parameters"].toObject()["face.small"].toInt();if(actual!=value)if(auto* slider=findChild<QSlider*>("portraitSlimAmount")){const QSignalBlocker block(slider);slider->setValue(actual);if(auto* number=findChild<QSpinBox*>("portraitSlimAmountValue"))number->setValue(actual);}return;}
    auto& person=people_[std::size_t(selected_)];auto original=person.settings;auto parameters=original["parameters"].toObject();auto baseline=original["slimAutoBase"].toObject(),applied=original["slimAutoApplied"].toObject();QStringList skipped;
    const QJsonObject proposed{{"body.waist",15},{"body.legWidth.left",10},{"body.legWidth.right",10}};
    for(auto it=proposed.begin();it!=proposed.end();++it){const auto key=it.key();const double current=parameters[key].toDouble();if(!baseline.contains(key))baseline[key]=current;else if(applied.contains(key))baseline[key]=baseline[key].toDouble()+current-applied[key].toDouble();
        auto candidate=parameters;candidate[key]=baseline[key].toDouble()+it.value().toDouble()*value*.01;auto settings=person.settings;settings["parameters"]=candidate;settings["waistGeometryVersion"]=2;settings["limbGeometryVersion"]=2;
        auto record=editing::portrait_person_json(person);record["bodyMask"]=editing::portrait_mask_png(person.body_mask);record["settings"]=settings;
        try{editing::portrait_deformation(source_.size(),QJsonObject{{"version",1},{"people",QJsonArray{record}}});parameters=candidate;applied[key]=candidate[key];person.settings=settings;}catch(const std::exception&){skipped<<key;}
    }
    person.settings["slimAutoBase"]=baseline;person.settings["slimAutoApplied"]=applied;person.settings["slimAutoAmount"]=value;remember();
    status_->setText(skipped.isEmpty()?tr("自然瘦身已更新"):tr("已调整可见部位；其余部位可用手动推移"));
}
void PortraitDialog::select_liquify_tool(int tool){
    finish_brush_gesture();finish_gesture();repair_mode_=0;color_sampling_=false;locate_->setChecked(false);range_mode_=0;liquify_tool_=tool<7?tool:-1;setProperty("portraitSlimTool",tool);view_->set_tool(-1);
    if(tool>=7){if(range_key_->findData("__protection")<0)range_key_->addItem(tr("变形冻结区"),"__protection");range_key_->setCurrentIndex(range_key_->findData("__protection"));range_mode_=tool==7?1:2;}
    range_visible_->setChecked(tool>=7);update_range_overlay();
    const QStringList labels{tr("推移"),tr("恢复"),tr("平滑"),tr("扭转"),tr("收缩"),tr("膨胀"),tr("侧向推移"),tr("冻结"),tr("解冻")};
    if(auto* active=findChild<QPushButton*>("portraitLiquifyActive")){active->setText(labels.value(tool));active->setIcon(portrait_liquify_icon(tool));}
    if(liquify_bar_)for(int i:{0,7,8})if(auto* button=liquify_bar_->findChild<QPushButton*>(QString("portraitCanvasFreeze%1").arg(i)))button->setChecked(tool==i);
    status_->setText(tool>=7?tr("拖动涂抹%1区域；完成后点画布右上方“推移”继续变形").arg(labels.value(tool)):tr("%1 · 直接拖动画面，空格临时抓手，Alt 反向").arg(labels.value(tool)));
}
void PortraitDialog::build_liquify(){
    setProperty("portraitSlimMode",1);
    if(!liquify_bar_){liquify_bar_=new QWidget(view_->viewport());liquify_bar_->setObjectName("portraitLiquifyCanvasBar");auto* column=new QVBoxLayout(liquify_bar_);column->setContentsMargins(4,4,4,4);column->setSpacing(4);
        for(int tool:{0,7,8}){auto* button=new QPushButton(tool==0?tr("推移"):tool==7?tr("冻结"):tr("解冻"),liquify_bar_);button->setObjectName(QString("portraitCanvasFreeze%1").arg(tool));button->setCheckable(true);button->setFixedSize(64,44);button->setIcon(portrait_liquify_icon(tool));column->addWidget(button);connect(button,&QPushButton::clicked,this,[this,tool]{select_liquify_tool(tool);});}liquify_bar_->adjustSize();}
    liquify_bar_->show();liquify_bar_->move(view_->viewport()->width()-liquify_bar_->width()-12,12);liquify_bar_->raise();
    if(!liquify_diameter_)liquify_diameter_=std::clamp(source_.width()/12,80,500);
    brush_size_->setValue(liquify_diameter_);brush_feather_->setValue(100-liquify_hardness_);
    const QStringList labels{tr("推移"),tr("恢复"),tr("平滑"),tr("扭转"),tr("收缩"),tr("膨胀"),tr("侧向推移"),tr("冻结"),tr("解冻")};
    auto* controls=new QWidget(parameter_host_);auto* line=new QHBoxLayout(controls);line->setContentsMargins(0,8,0,8);auto* active=new QPushButton(labels.value(property("portraitSlimTool").toInt()),controls);active->setObjectName("portraitLiquifyActive");active->setIcon(portrait_liquify_icon(property("portraitSlimTool").toInt()));active->setIconSize({24,24});active->setCheckable(true);active->setChecked(true);active->setMinimumHeight(48);active->setStyleSheet("QPushButton{background:#2f80ed;border:1px solid #409cff;border-radius:4px;}");line->addWidget(active,1);connect(active,&QPushButton::clicked,this,[this]{select_liquify_tool(property("portraitSlimTool").toInt());});
    auto* more=new QToolButton(controls);more->setText(tr("更多工具"));more->setMinimumHeight(48);more->setPopupMode(QToolButton::InstantPopup);auto* menu=new QMenu(more);for(int i=0;i<labels.size();++i){auto* action=menu->addAction(portrait_liquify_icon(i),labels[i]);action->setObjectName(QString("portraitLiquifyAction%1").arg(i));connect(action,&QAction::triggered,this,[this,i]{select_liquify_tool(i);});}more->setMenu(menu);more->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);line->addWidget(more,1);parameter_layout_->addWidget(controls);
    auto* tools=new QComboBox(parameter_host_);tools->setObjectName("portraitLiquifyTool");tools->addItems(labels);tools->setCurrentIndex(property("portraitSlimTool").toInt());tools->hide();connect(tools,&QComboBox::activated,this,&PortraitDialog::select_liquify_tool);
    auto* paint=new QPushButton(parameter_host_);paint->setObjectName("portraitLiquify");paint->hide();connect(paint,&QPushButton::clicked,this,[this]{select_liquify_tool(property("portraitSlimTool").toInt());});
    const auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitLiquifySize",1,500,brush_size_->value(),tr(" px"));size.host->setMinimumHeight(52);parameter_layout_->addWidget(size.host);bind_slider(size.slider,std::clamp(source_.width()/12,80,500));connect(size.slider,&QSlider::valueChanged,this,[this](int v){liquify_diameter_=v;brush_size_->setValue(v);});connect(brush_size_,&QSlider::valueChanged,size.slider,&QSlider::setValue);
    const auto pressure=portrait_integer_row(parameter_host_,tr("程度"),"portraitLiquifyPressure",0,100,int(liquify_pressure_*100));pressure.host->setMinimumHeight(52);parameter_layout_->addWidget(pressure.host);bind_slider(pressure.slider,50);connect(pressure.slider,&QSlider::valueChanged,this,[this](int v){liquify_pressure_=v*.01;});
    auto* header=new QToolButton(parameter_host_);header->setText(tr("笔刷细节"));header->setCheckable(true);header->setArrowType(Qt::RightArrow);header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(header);
    auto* detail=new QWidget(parameter_host_);auto* column=new QVBoxLayout(detail);column->setContentsMargins(0,0,0,0);const auto hardness=portrait_integer_row(detail,tr("硬度"),"portraitLiquifyHardness",0,100,liquify_hardness_);column->addWidget(hardness.host);bind_slider(hardness.slider,40);connect(hardness.slider,&QSlider::valueChanged,this,[this](int v){liquify_hardness_=v;brush_feather_->setValue(100-v);});
    auto* reset=new QPushButton(tr("恢复全部手动液化"),detail);reset->setObjectName("portraitLiquifyReset");column->addWidget(reset);connect(reset,&QPushButton::clicked,this,[this]{if(selected_<0)return;finish_brush_gesture();auto& settings=people_[std::size_t(selected_)].settings;if(settings.contains("protectionManualAnchor")){const auto empty=editing::liquify_json(editing::create_liquify_mesh(source_.size()));settings["protectionManualAnchor"]=empty;settings["protectionManualResidual"]=empty;}auto mesh=editing::liquify_from_json(settings["liquify"].toObject());if(!mesh.isNull()){editing::reconstruct_liquify(mesh,1);settings["liquify"]=editing::liquify_json(mesh);remember();}});
    detail->hide();parameter_layout_->addWidget(detail);connect(header,&QToolButton::toggled,detail,&QWidget::setVisible);parameter_layout_->addStretch();select_liquify_tool(property("portraitSlimTool").toInt());
}
bool PortraitDialog::liquify_event(QEvent* event){
    if(navigation_->currentRow()!=4 || liquify_tool_<0 || range_mode_ || locate_->isChecked() || view_->active_tool()!=-1 || view_->temporary_pan())return false;
    if(event->type()!=QEvent::MouseButtonPress && event->type()!=QEvent::MouseMove && event->type()!=QEvent::MouseButtonRelease)return false;
    auto* mouse=static_cast<QMouseEvent*>(event);const auto pos=view_->mapToScene(mouse->position().toPoint());
    const QPointF normalized(pos.x()/preview_.width(),pos.y()/preview_.height());
    if(event->type()==QEvent::MouseMove){QPainterPath circle;const double r=brush_size_->value()*.5*preview_.width()/source_.width();circle.addEllipse(pos,r,r);overlay_->setPath(circle);}
    if(event->type()==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton && preview_.rect().contains(pos.toPoint())){
        const auto& person=people_[std::size_t(selected_)];if(person.match_ambiguous || person.unmatched)return true;
        const bool prepared=liquify_tool_==1 && displayed_geometry_cache_.source_size==source_.size() && displayed_geometry_cache_.person==selected_ && displayed_geometry_cache_.recipe==recipe();
        liquify_mesh_=prepared?displayed_geometry_cache_.raw_manual:editing::liquify_from_json(person.settings.value("liquify").toObject());if(liquify_mesh_.isNull())liquify_mesh_=editing::create_liquify_mesh(source_.size());liquify_protection_={};liquify_freeze_weights_=editing::portrait_protection_weights(person.settings,liquify_mesh_.size());liquify_limited_=false;
        if(liquify_tool_==1){liquify_protection_[0]=prepared?displayed_geometry_cache_.manual_anchor:editing::liquify_from_json(person.settings["protectionManualAnchor"].toObject());liquify_protection_[1]=prepared?displayed_geometry_cache_.manual_residual:editing::liquify_from_json(person.settings["protectionManualResidual"].toObject());liquify_freeze_weights_=editing::portrait_protection_weights(person.settings,liquify_mesh_.size());}
        if(liquify_tool_==1){
            const auto current=recipe();
            if(displayed_geometry_cache_.source_size==source_.size() && displayed_geometry_cache_.person==selected_ && displayed_geometry_cache_.recipe==current){
                liquify_restore_live_=displayed_geometry_cache_.manual;liquify_restore_fixed_=displayed_geometry_cache_.mesh;
            }else liquify_restore_fixed_=editing::portrait_deformation_with_manual(source_.size(),current,selected_,liquify_restore_live_);
            if(liquify_restore_live_.isNull())liquify_restore_live_=editing::create_liquify_mesh(source_.size());
            if(liquify_restore_fixed_.isNull())liquify_restore_fixed_=editing::create_liquify_mesh(source_.size());
            for(int y=0;y<liquify_restore_fixed_.height();++y){auto* fixed=reinterpret_cast<float*>(liquify_restore_fixed_.scanLine(y));const auto* moving=reinterpret_cast<const float*>(liquify_restore_live_.constScanLine(y));for(int x=0;x<liquify_restore_fixed_.width();++x)for(int c=0;c<2;++c)fixed[x*4+c]-=moving[x*4+c];}
            liquify_restore_committed_=liquify_restore_live_;
        }
        liquify_protection_committed_=liquify_protection_;liquify_protection_initial_=liquify_protection_;
        liquify_last_=normalized;liquify_committed_=liquify_mesh_;liquify_initial_=liquify_mesh_;liquify_sample_={normalized.x()*(source_.width()-1),normalized.y()*(source_.height()-1)};liquify_distance_=0;liquify_drawing_=true;view_->setFocus();view_->viewport()->grabMouse();
    }
    if(!liquify_drawing_)return false;
    editing::LiquifyBrush brush;brush.tool=static_cast<editing::LiquifyTool>(liquify_tool_);brush.diameter=brush_size_->value();brush.pressure=liquify_pressure_*((liquify_tool_==0 || liquify_tool_==6)?.22:1.);brush.density=1-brush_feather_->value()*.01;brush.reverse=mouse->modifiers().testFlag(Qt::AltModifier);
    const auto pixel=[this](QPointF p){return QPointF(p.x()*(source_.width()-1),p.y()*(source_.height()-1));};
    const auto dab=[&](QImage& mesh,std::array<QImage,2>& compensation,QPointF from,QPointF to,double amount=1.){
        const auto previous_mesh=mesh;const auto previous_compensation=compensation;
        auto& moving=&mesh==&liquify_committed_?liquify_restore_committed_:liquify_restore_live_;
        const auto previous_moving=moving;
        const auto apply=[&](QImage& field){
            if(field.isNull())return;
            const auto original=field;
            if(!liquify_freeze_weights_.isNull()){
                const auto weights=liquify_freeze_weights_.scaled(field.size());
                for(int y=0;y<field.height();++y){auto* row=reinterpret_cast<float*>(field.scanLine(y));for(int x=0;x<field.width();++x)row[x*4+2]=std::max(row[x*4+2],weights.constScanLine(y)[x]/255.F);}
            }
            editing::liquify_dab(field,source_.size(),from,to,brush,amount);
            for(int y=0;y<field.height();++y){auto* row=reinterpret_cast<float*>(field.scanLine(y));const auto* old=reinterpret_cast<const float*>(original.constScanLine(y));for(int x=0;x<field.width();++x)row[x*4+2]=old[x*4+2];}
        };
        apply(mesh);
        if(brush.tool==editing::LiquifyTool::reconstruct){for(auto& field:compensation)apply(field);apply(moving);}
        if(brush.tool==editing::LiquifyTool::reconstruct){
            // Apply the foldover constraint at canonical path samples, never
            // at mouse-event boundaries. Provisional tails remain disposable.
            const auto blend=[](QImage& next,const QImage& previous){if(next.isNull())return;for(int y=0;y<next.height();++y){auto* dst=reinterpret_cast<float*>(next.scanLine(y));const auto* old=reinterpret_cast<const float*>(previous.constScanLine(y));for(int x=0;x<next.width();++x)for(int c=0;c<2;++c)dst[x*4+c]=(dst[x*4+c]+old[x*4+c])*.5F;}};
            bool valid=false;
            for(int attempt=0;attempt<12;++attempt){
                try{auto composed=liquify_restore_fixed_;for(int y=0;y<composed.height();++y){auto* dst=reinterpret_cast<float*>(composed.scanLine(y));const auto* src=reinterpret_cast<const float*>(moving.constScanLine(y));for(int x=0;x<composed.width();++x)for(int c=0;c<2;++c)dst[x*4+c]+=src[x*4+c];}if(editing::liquify_minimum_jacobian(composed)<.05)throw editing::EditError("局部恢复会造成轮廓折叠");valid=true;break;}
                catch(const editing::EditError&){liquify_limited_=true;blend(moving,previous_moving);blend(mesh,previous_mesh);for(int i=0;i<2;++i)blend(compensation[i],previous_compensation[i]);}
            }
            if(!valid){mesh=previous_mesh;compensation=previous_compensation;moving=previous_moving;}
        }
    };
    const bool radial=brush.tool==editing::LiquifyTool::smooth || brush.tool==editing::LiquifyTool::reconstruct || brush.tool==editing::LiquifyTool::bloat || brush.tool==editing::LiquifyTool::pucker || brush.tool==editing::LiquifyTool::twirl;
    if(radial && event->type()==QEvent::MouseButtonPress)dab(liquify_committed_,liquify_protection_committed_,pixel(normalized),pixel(normalized));
    if(brush.tool==editing::LiquifyTool::warp || brush.tool==editing::LiquifyTool::push_left || radial){
        // Keep canonical samples independent of mouse event boundaries. The
        // unfinished tail is displayed immediately but never accumulated twice.
        const double spacing=std::max(.5,brush.diameter*.025);auto from=pixel(liquify_last_);const auto to=pixel(normalized);double remaining=QLineF(from,to).length();
        while(remaining+1e-9>=spacing-liquify_distance_){const double step=spacing-liquify_distance_;const auto next=from+(to-from)*(step/std::max(remaining,1e-12));dab(liquify_committed_,liquify_protection_committed_,liquify_sample_,next,radial?spacing/std::max(1.,brush.diameter*.1):1.);liquify_sample_=next;from=next;remaining=QLineF(from,to).length();liquify_distance_=0;}
        liquify_distance_+=remaining;liquify_mesh_=liquify_committed_;liquify_protection_=liquify_protection_committed_;if(brush.tool==editing::LiquifyTool::reconstruct)liquify_restore_live_=liquify_restore_committed_;
        if(QLineF(liquify_sample_,to).length()>1e-9)dab(liquify_mesh_,liquify_protection_,liquify_sample_,to,radial?liquify_distance_/std::max(1.,brush.diameter*.1):1.);
    }else editing::liquify_dab(liquify_mesh_,source_.size(),pixel(liquify_last_),pixel(normalized),brush);
    liquify_last_=normalized;schedule_preview();
    if(event->type()==QEvent::MouseButtonRelease)finish_liquify();return true;
}
void PortraitDialog::finish_liquify(){
    if(!liquify_drawing_)return;liquify_drawing_=false;liquify_committed_=QImage{};view_->viewport()->releaseMouse();
    const bool unchanged=liquify_mesh_==liquify_initial_ && liquify_protection_==liquify_protection_initial_;liquify_initial_=QImage{};if(unchanged){liquify_mesh_=QImage{};schedule_preview();if(liquify_limited_)status_->setText(tr("此处已达到局部恢复边界；请增大恢复画笔覆盖周围变形"));return;}
    if(selected_>=0){
        auto& settings=people_[std::size_t(selected_)].settings;const auto previous=settings;
        settings["liquify"]=editing::liquify_json(liquify_mesh_);if(!liquify_protection_[0].isNull())settings["protectionManualAnchor"]=editing::liquify_json(liquify_protection_[0]);if(!liquify_protection_[1].isNull())settings["protectionManualResidual"]=editing::liquify_json(liquify_protection_[1]);liquify_mesh_=QImage{};
        try{editing::portrait_deformation(linear_preview_.size(),recipe());}
        catch(const std::exception& e){settings=previous;schedule_preview();status_->setText(tr("本次液化未应用：%1；已保留之前的调整，可继续编辑。").arg(QString::fromUtf8(e.what())));return;}
        remember();
        if(liquify_limited_)status_->setText(tr("已逐步恢复；局部边界限制了本次强度，可增大恢复画笔覆盖周围变形"));
    }liquify_mesh_=QImage{};
}

namespace {
QStringList volume_part_keys(bool lift,int part){
    const QList<QStringList> volume{{"face.foreheadFill","face.templeFill","face.tearFill","face.malarShape","face.malarTone","face.volume","face.cheekTone","face.perioralTone"},{"face.foreheadFill"},{"face.templeFill"},{"face.tearFill"},{"face.malarShape","face.malarTone"},{"face.volume","face.cheekTone"},{"face.perioralTone"}};
    const QList<QStringList> lifting{{"face.contourLift","face.foreheadLift","face.orbitalLift","face.orbitalTone","face.midfaceLift","face.perioralLift","face.perioralTone"},{"face.contourLift"},{"face.foreheadLift"},{"face.orbitalLift","face.orbitalTone"},{"face.midfaceLift"},{"face.perioralLift","face.perioralTone"}};
    return (lift?lifting:volume).value(part);
}
}

void PortraitDialog::add_parameter_row(QVBoxLayout* layout,const QString& key,const QString& label,int defaultValue){
    const auto d=editing::portrait_parameter(key);if(!d)return;
    auto* host=new QWidget(parameter_host_);auto* column=new QVBoxLayout(host);column->setContentsMargins(0,3,0,3);column->setSpacing(0);
    auto* row=new QHBoxLayout;row->addWidget(new QLabel(label,host));row->addStretch();auto* number=new QDoubleSpinBox(host);number->setObjectName("portraitParam_"+key);number->setRange(d->minimum,d->maximum);number->setDecimals(0);number->setFixedSize(72,26);number->setButtonSymbols(QAbstractSpinBox::NoButtons);number->setKeyboardTracking(false);row->addWidget(number);column->addLayout(row);
    auto* slider=new ColorBandSlider(-1,0,host);slider->setObjectName("portraitSlider_"+key);slider->setRange(int(d->minimum),int(d->maximum));slider->setFixedHeight(24);slider->set_colors(QColor("#69727c"),QColor("#69727c"));column->addWidget(slider);host->setMinimumHeight(58);layout->addWidget(host);bind_slider(slider,defaultValue);
    const auto params=selected_>=0?people_[std::size_t(selected_)].settings["parameters"].toObject():QJsonObject{};const QString valueKey=d->bilateral && selected_>=0?key+(people_[std::size_t(selected_)].settings["side"].toInt()==0?".left":".right"):key;number->setValue(params[valueKey].toDouble(params[key].toDouble(defaultValue)));slider->setValue(int(number->value()));host->setEnabled(selected_>=0);
    connect(slider,&QSlider::valueChanged,number,&QDoubleSpinBox::setValue);connect(number,&QDoubleSpinBox::valueChanged,this,[this,key,slider](double value){const QSignalBlocker b(slider);slider->setValue(int(value));set_parameter(key,value);});
}
void PortraitDialog::activate_skin_brush(int mode){
    if(selected_<0)return;finish_brush_gesture();
    const QString target=property("portraitWorkspaceTask").toInt()==7?"skin.oil":property("portraitSmoothTarget").toInt()==1?"skin.bodySmooth":"skin.smooth";
    auto& s=people_[std::size_t(selected_)].settings;
    // Older projects could store a manual-only mask. Keep its visible result
    // while migrating to the single editable coverage used by the new tabs.
    auto manual=s.value("smoothManual").toObject();if(manual.value(target).toBool()){
        const auto mask=editing::portrait_effect_mask(editing::portrait_person_json(people_[std::size_t(selected_)]),target,preview_.size().scaled(768,768,Qt::KeepAspectRatio));
        auto masks=s["masks"].toObject();masks[target]=editing::portrait_mask_png(mask);s["masks"]=masks;manual[target]=false;s["smoothManual"]=manual;
    }
    refresh_ranges();range_key_->setCurrentIndex(range_key_->findData(target));range_mode_=mode;repair_mode_=0;liquify_tool_=-1;color_sampling_=false;locate_->setChecked(false);view_->set_tool(-1);range_visible_->setChecked(false);
    if(s["parameters"].toObject()[target].toDouble()==0){
        auto masks=s["masks"].toObject();if(!masks.contains(target)){QImage empty(preview_.size().scaled(768,768,Qt::KeepAspectRatio),QImage::Format_Grayscale8);empty.fill(0);masks[target]=editing::portrait_mask_png(empty);s["masks"]=masks;}set_parameter(target,40);
    }
    view_->setProperty("portraitBrushCursor",true);update_overlay();
}


void PortraitDialog::activate_blemish_tool(){
    repair_selected_=-1;range_mode_=repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);
    if(property("portraitBlemishPage").toInt()==1)return;
    const int tool=property("portraitBlemishTool").toInt();
    if(tool==1){refresh_ranges();range_key_->setCurrentIndex(range_key_->findData("repair.conceal"));range_mode_=property("portraitConcealErase").toBool()?2:1;range_visible_->setChecked(false);}
    else repair_mode_=tool==2?3:1;
}
void PortraitDialog::build_blemish_parameters(){
    build_repairs(7);
    auto* oldSize=findChild<QSlider*>("portraitRepairSize");auto* oldOpacity=findChild<QSlider*>("portraitRepairOpacity");auto* oldFeather=findChild<QSlider*>("portraitRepairFeather");
    auto* records=findChild<QListWidget*>("portraitRepairList")->parentWidget();auto* candidates=findChild<QListWidget*>("portraitBlemishes");auto* detect=findChild<QPushButton*>("portraitAnalyzeRepairs");
    auto* apply=findChild<QPushButton*>("portraitApplyBlemish");auto* preserve=findChild<QPushButton*>("portraitPreserveBlemish");
    auto* stash=new QWidget(parameter_host_);auto* hidden=new QVBoxLayout(stash);while(parameter_layout_->count()>1){auto* item=parameter_layout_->takeAt(1);if(auto* w=item->widget())hidden->addWidget(w);delete item;}parameter_layout_->addWidget(stash);stash->hide();
    details_->hide();if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->hide();
    auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitBlemishTabs");tabs->addTab(tr("手动"));tabs->addTab(tr("建议"));tabs->setExpanding(true);tabs->setCurrentIndex(property("portraitBlemishPage").toInt());parameter_layout_->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();setProperty("portraitBlemishPage",i);repair_has_source_=false;activate_blemish_tool();build_parameters();});
    auto fold=[this](QString title,QString name,QWidget* body){auto* header=new QToolButton(parameter_host_);header->setObjectName(name);header->setText(title);header->setCheckable(true);header->setArrowType(Qt::RightArrow);header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(header);body->setParent(parameter_host_);body->hide();parameter_layout_->addWidget(body);connect(header,&QToolButton::toggled,this,[header,body](bool open){body->setVisible(open);header->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});};
    if(tabs->currentIndex()==1){
        const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{};const auto marks=settings.value("blemishes").toArray();
        const QString error=property("portraitBlemishError").toString();
        const bool hasResults=!marks.isEmpty(),complete=settings.value("blemishDetectionComplete").toBool();
        auto* state=new QLabel(parameter_host_);state->setObjectName("portraitBlemishState");state->setWordWrap(true);parameter_layout_->addWidget(state);
        if(repair_analysis_busy_){state->setText(tr("正在检测可供确认的瑕疵…"));}
        else if(!error.isEmpty()){state->setText(tr("检测未完成：%1").arg(error));}
        else if(!hasResults){state->setText(complete?tr("本次未发现可靠的候选点。细小瑕疵仍可使用手动修复。"):tr("检测可供确认的瑕疵位置，由你决定修复或保留。不会自动修改照片。"));}
        int pending=0,kept=0;for(const auto& value:marks){const auto mark=value.toObject();if(mark["state"].toString()=="preserved")++kept;else if(mark["state"].toString()!="repaired")++pending;}
        if(hasResults && error.isEmpty() && !repair_analysis_busy_)state->setText(tr("%1 处待处理 · %2 处已保留").arg(pending).arg(kept));
        if(hasResults){
            auto* nav=new QWidget(parameter_host_);auto* navLayout=new QHBoxLayout(nav);navLayout->setContentsMargins(0,0,0,0);
            for(int direction:{-1,1}){auto* button=new QPushButton(direction<0?tr("上一处"):tr("下一处"),nav);button->setObjectName(direction<0?"portraitBlemishPrevious":"portraitBlemishNext");navLayout->addWidget(button);connect(button,&QPushButton::clicked,this,[candidates,direction]{const int n=candidates->count();for(int j=1;j<=n;++j){const int i=(std::max(0,candidates->currentRow())+direction*j+n*2)%n;if(!candidates->item(i)->isHidden()){candidates->setCurrentRow(i);break;}}});}parameter_layout_->addWidget(nav);
            candidates->setParent(parameter_host_);candidates->setMaximumHeight(300);candidates->setIconSize(QSize(56,48));parameter_layout_->addWidget(candidates);candidates->show();
            auto* processed=new QListWidget(parameter_host_);processed->setMaximumHeight(160);processed->setObjectName("portraitBlemishProcessed");
            for(int i=0;i<marks.size();++i){const auto mark=marks[i].toObject();auto* item=candidates->item(i);const bool done=mark["state"].toString()=="preserved" || mark["state"].toString()=="repaired";item->setHidden(done);const QPoint center(int(mark["x"].toDouble()*preview_.width()),int(mark["y"].toDouble()*preview_.height()));const int r=std::max(16,int(mark["radius"].toDouble()*std::min(preview_.width(),preview_.height())*3));item->setIcon(QPixmap::fromImage(preview_.copy(QRect(center-QPoint(r,r),QSize(r*2,r*2)).intersected(preview_.rect())).scaled(56,48,Qt::KeepAspectRatio,Qt::SmoothTransformation)));item->setSizeHint(QSize(240,64));if(done){auto* entry=new QListWidgetItem(item->icon(),item->text(),processed);entry->setData(Qt::UserRole,i);}}
            fold(tr("已处理（%1）").arg(processed->count()),"portraitBlemishProcessedToggle",processed);
            auto* automatic=new QCheckBox(tr("处理后自动下一处"),parameter_host_);automatic->setObjectName("portraitBlemishAutoNext");automatic->setChecked(!property("portraitBlemishAutoNext").isValid() || property("portraitBlemishAutoNext").toBool());parameter_layout_->addWidget(automatic);connect(automatic,&QCheckBox::toggled,this,[this](bool on){setProperty("portraitBlemishAutoNext",on);});
            auto* bar=new QWidget(view_->viewport());bar->setObjectName("portraitBlemishActions");bar->setStyleSheet("QWidget#portraitBlemishActions {background:#292f34;border-radius:6px;} QPushButton {padding:6px 12px;}");auto* actions=new QHBoxLayout(bar);actions->setContentsMargins(4,4,4,4);
            for(bool keep:{false,true}){auto* button=new QPushButton(keep?tr("保留"):tr("修复"),bar);button->setObjectName(keep?"portraitBlemishKeep":"portraitBlemishRepair");actions->addWidget(button);connect(button,&QPushButton::clicked,this,[this,candidates,keep,apply,preserve]{const int i=candidates->currentRow();setProperty("portraitBlemishSelection",(!property("portraitBlemishAutoNext").isValid() || property("portraitBlemishAutoNext").toBool())?i+1:-1);(keep?preserve:apply)->click();});}bar->hide();
            connect(candidates,&QListWidget::currentRowChanged,this,[this,candidates,bar,marks](int i){if(i<0 || i>=marks.size()){bar->hide();return;}setProperty("portraitBlemishSelection",i);const auto mark=marks[i].toObject();const auto at=view_->mapFromScene(repair_scene_position({mark["x"].toDouble(),mark["y"].toDouble()}));bar->adjustSize();bar->move(std::clamp(at.x()+18,0,std::max(0,view_->viewport()->width()-bar->width())),std::clamp(at.y()-bar->height()-12,0,std::max(0,view_->viewport()->height()-bar->height())));bar->show();bar->raise();});
            connect(processed,&QListWidget::currentItemChanged,this,[candidates,bar](QListWidgetItem* item){if(item){candidates->setCurrentRow(item->data(Qt::UserRole).toInt());bar->show();}});
            const int start=property("portraitBlemishSelection").isValid()?property("portraitBlemishSelection").toInt():0;if(start>=0)for(int j=0;j<marks.size();++j){int i=(start+j)%marks.size();if(!candidates->item(i)->isHidden()){candidates->setCurrentRow(i);break;}}
        }
        detect->setParent(parameter_host_);detect->setText(!error.isEmpty()?tr("重新尝试"):complete||hasResults?tr("重新检测"):tr("检测瑕疵建议"));detect->setEnabled(selected_>=0 && !repair_analysis_busy_);parameter_layout_->addWidget(detect);detect->show();
        auto* manual=new QPushButton(tr("手动修复"),parameter_host_);manual->setObjectName("portraitBlemishManual");parameter_layout_->addWidget(manual);connect(manual,&QPushButton::clicked,this,[tabs]{tabs->setCurrentIndex(0);});parameter_layout_->addStretch();return;
    }
    auto* tools=new QTabBar(parameter_host_);tools->setObjectName("portraitBlemishTools");for(const auto& title:{tr("修复"),tr("遮瑕"),tr("妆容修复")})tools->addTab(title);tools->setExpanding(true);const int tool=property("portraitBlemishTool").toInt();tools->setCurrentIndex(tool);parameter_layout_->addWidget(tools);
    connect(tools,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();setProperty("portraitBlemishTool",i);setProperty("portraitConcealErase",false);repair_has_source_=false;if(i==1 && selected_>=0 && people_[std::size_t(selected_)].settings.value("parameters").toObject()["repair.conceal"].toDouble()==0)set_parameter("repair.conceal",50);activate_blemish_tool();build_parameters();});
    if(tool==1){auto* row=new QWidget(parameter_host_);auto* colors=new QHBoxLayout(row);colors->setContentsMargins(0,6,0,6);auto* sample=new QToolButton(row);sample->setObjectName("portraitConcealSample");sample->setIcon(portrait_task_icon(12));sample->setToolTip(tr("从画面取色"));colors->addWidget(sample);connect(sample,&QToolButton::clicked,this,[this]{setProperty("portraitConcealSampling",true);status_->setText(tr("单击皮肤取色"));});int i=0;for(const auto* hex:{"#e6c7b4","#bf9478","#daa897","#89685b","#b49b8f"}){auto* swatch=portrait_color_swatch(row,QColor(hex));swatch->setObjectName(QString("portraitConcealColor%1").arg(i++));colors->addWidget(swatch);connect(swatch,&QPushButton::clicked,this,[this,hex]{if(selected_<0)return;QImage image(1,1,QImage::Format_RGBA8888);image.setColorSpace(QColorSpace::SRgb);image.fill(QColor(hex));const auto linear=editing::to_working(image);const auto* p=reinterpret_cast<const float*>(linear.constBits());people_[std::size_t(selected_)].settings["concealColor"]=QJsonArray{p[0],p[1],p[2]};setProperty("portraitConcealErase",false);activate_blemish_tool();remember();});}parameter_layout_->addWidget(row);}
    if(tool==2){auto* row=new QWidget(parameter_host_);auto* parts=new QHBoxLayout(row);parts->setContentsMargins(0,4,0,4);int i=0;for(const auto& title:{tr("唇"),tr("眉"),tr("眼影")}){const int part=i++;auto* card=new QToolButton(row);card->setObjectName(QString("portraitMakeupRepairPart%1").arg(part));card->setText(title);card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);card->setCheckable(true);card->setChecked(property("portraitMakeupRepairPart").toInt()==part);card->setMinimumHeight(64);card->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);if(selected_>=0){const auto f=people_[std::size_t(selected_)].face;const QRectF region(f.x()+f.width()*(part==0?.2:.08),f.y()+f.height()*(part==0?.64:part==1?.21:.30),f.width()*(part==0?.6:.84),f.height()*.22);const QRect crop(int(region.x()*preview_.width()),int(region.y()*preview_.height()),int(region.width()*preview_.width()),int(region.height()*preview_.height()));card->setIcon(QPixmap::fromImage(preview_.copy(crop.intersected(preview_.rect()))));}card->setIconSize(QSize(96,42));card->setStyleSheet("QToolButton {border:1px solid #434b52;border-radius:4px;padding:4px;} QToolButton:checked {background:#245787;border:2px solid #338dff;}");parts->addWidget(card);connect(card,&QToolButton::clicked,this,[this,part]{finish_brush_gesture();setProperty("portraitMakeupRepairPart",part);repair_has_source_=false;activate_blemish_tool();build_parameters();});}parameter_layout_->addWidget(row);}
    auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitBlemishSize",1,500,brush_size_->value());size.host->setMinimumHeight(58);parameter_layout_->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,oldSize,&QSlider::setValue);
    const int amount=tool==1 && selected_>=0?int(people_[std::size_t(selected_)].settings.value("parameters").toObject()["repair.conceal"].toDouble(50)):int(repair_opacity_*100);
    auto strength=portrait_integer_row(parameter_host_,tr("程度"),"portraitBlemishAmount",0,100,amount);strength.host->setMinimumHeight(58);bind_slider(strength.slider,50);parameter_layout_->addWidget(strength.host);connect(strength.slider,&QSlider::valueChanged,this,[this,tool,oldOpacity](int value){if(tool==1)set_parameter("repair.conceal",value);else oldOpacity->setValue(value);});
    if(tool==1){auto* erase=new QPushButton(tr("擦除本覆盖"),parameter_host_);erase->setObjectName("portraitConcealErase");erase->setCheckable(true);erase->setChecked(property("portraitConcealErase").toBool());parameter_layout_->addWidget(erase);connect(erase,&QPushButton::toggled,this,[this](bool on){finish_brush_gesture();setProperty("portraitConcealErase",on);activate_blemish_tool();});}
    else{auto* brush=new QWidget(parameter_host_);auto* layout=new QVBoxLayout(brush);auto feather=portrait_integer_row(brush,tr("羽化"),"portraitBlemishFeather",0,100,brush_feather_->value());feather.host->setMinimumHeight(58);layout->addWidget(feather.host);connect(feather.slider,&QSlider::valueChanged,oldFeather,&QSlider::setValue);fold(tr("笔刷细节"),"portraitBlemishDetails",brush);fold(tr("修复记录"),"portraitBlemishRecords",records);}
    parameter_layout_->addStretch();auto* hint=new QLabel(tool==1?tr("直接涂抹；Alt 取肤色，擦除仅移除遮瑕覆盖。"):tool==2?tr("在所选妆容部位拖动修复；Alt 可指定同部位来源。"):tr("直接涂抹；Alt 可指定来源。"),parameter_host_);hint->setWordWrap(true);parameter_layout_->addWidget(hint);
}

void PortraitDialog::activate_wrinkle_tool(){
    finish_brush_gesture();repair_selected_=-1;repair_has_source_=false;range_mode_=0;locate_->setChecked(false);view_->set_tool(-1);range_visible_->setChecked(false);
    if(property("portraitWrinkleErase").toBool()){
        repair_mode_=0;const auto key=property("portraitWrinkleMode").toInt()==1?QStringLiteral("wrinkle.erase.lighting"):QStringLiteral("wrinkle.erase.texture");
        if(range_key_->findData(key)<0)range_key_->addItem(tr("当前祛皱类型"),key);range_key_->setCurrentIndex(range_key_->findData(key));range_mode_=1;
    }else repair_mode_=property("portraitWrinkleMode").toInt()==1?2:6;
    overlay_->setPath(QPainterPath{});
}
void PortraitDialog::build_wrinkle_parameters(){
    build_repairs(8);
    auto* oldSize=findChild<QSlider*>("portraitRepairSize");auto* oldOpacity=findChild<QSlider*>("portraitRepairOpacity");auto* oldFeather=findChild<QSlider*>("portraitRepairFeather");auto* list=findChild<QListWidget*>("portraitRepairList");auto* records=list->parentWidget();
    auto* stash=new QWidget(parameter_host_);auto* hidden=new QVBoxLayout(stash);while(parameter_layout_->count()>1){auto* item=parameter_layout_->takeAt(1);if(auto* w=item->widget())hidden->addWidget(w);delete item;}parameter_layout_->addWidget(stash);stash->hide();details_->hide();if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->hide();
    const bool lighting=property("portraitWrinkleMode").toInt()==1,erase=property("portraitWrinkleErase").toBool();
    auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitWrinkleTabs");tabs->addTab(tr("纹理修复"));tabs->addTab(tr("明暗淡化"));tabs->setExpanding(true);tabs->setCurrentIndex(lighting?1:0);parameter_layout_->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int mode){finish_brush_gesture();finish_gesture();setProperty("portraitWrinkleMode",mode);setProperty("portraitWrinkleErase",false);activate_wrinkle_tool();build_parameters();});
    auto* mode=new QWidget(parameter_host_);auto* row=new QHBoxLayout(mode);row->setContentsMargins(0,8,0,8);
    for(bool erasing:{false,true}){auto* button=new QPushButton(erasing?tr("擦除"):lighting?tr("涂抹"):tr("修复"),mode);button->setObjectName(erasing?"portraitWrinkleTypeErase":"portraitWrinklePaint");button->setCheckable(true);button->setChecked(erase==erasing);button->setStyleSheet("QPushButton:checked {background:#287eea;color:white;border:1px solid #459cff;}");row->addWidget(button);connect(button,&QPushButton::clicked,this,[this,erasing]{finish_brush_gesture();setProperty("portraitWrinkleErase",erasing);activate_wrinkle_tool();build_parameters();});}parameter_layout_->addWidget(mode);
    auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitWrinkleSize",1,500,brush_size_->value());size.host->setMinimumHeight(58);parameter_layout_->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,oldSize,&QSlider::setValue);connect(brush_size_,&QSlider::valueChanged,size.slider,&QSlider::setValue);
    if(!erase){auto strength=portrait_integer_row(parameter_host_,lighting?tr("淡化程度"):tr("程度"),"portraitWrinkleAmount",0,100,int(repair_opacity_*100));strength.host->setMinimumHeight(58);bind_slider(strength.slider,70);parameter_layout_->addWidget(strength.host);connect(strength.slider,&QSlider::valueChanged,oldOpacity,&QSlider::setValue);connect(list,&QListWidget::currentItemChanged,this,[this,slider=strength.slider](QListWidgetItem*){const QSignalBlocker block(slider);slider->setValue(int(repair_opacity_*100));});
        auto fold=[this](QString title,QString name,QWidget* body){auto* header=new QToolButton(parameter_host_);header->setText(title);header->setObjectName(name);header->setCheckable(true);header->setArrowType(Qt::RightArrow);header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(header);body->setParent(parameter_host_);body->hide();parameter_layout_->addWidget(body);connect(header,&QToolButton::toggled,this,[body,header](bool open){body->setVisible(open);header->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});};
        auto* brush=new QWidget(parameter_host_);auto* layout=new QVBoxLayout(brush);layout->setContentsMargins(0,0,0,0);auto feather=portrait_integer_row(brush,tr("羽化"),"portraitWrinkleFeather",0,100,brush_feather_->value());feather.host->setMinimumHeight(58);layout->addWidget(feather.host);connect(feather.slider,&QSlider::valueChanged,oldFeather,&QSlider::setValue);fold(tr("笔刷细节"),"portraitWrinkleDetails",brush);
        for(int i=0;i<list->count();++i){const auto strokes=selected_>=0?people_[std::size_t(selected_)].settings.value("repairs").toArray():QJsonArray{};const int index=list->item(i)->data(Qt::UserRole).toInt();list->item(i)->setHidden(index>=strokes.size() || (strokes[index].toObject()["wrinkleMethod"].toString()=="lighting-v2")!=lighting);}fold(tr("修复记录"),"portraitWrinkleRecords",records);
    }
    parameter_layout_->addStretch();auto* hint=new QLabel(erase?tr("擦除当前%1效果，其他修图保留。").arg(lighting?tr("明暗淡化"):tr("纹理修复")):lighting?tr("沿暗纹涂抹，减弱明暗差异；无需取样。"):tr("沿皱纹拖动；Alt 可指定来源。"),parameter_host_);hint->setWordWrap(true);parameter_layout_->addWidget(hint);
}
void PortraitDialog::build_erase_parameters(){
    // Keep the established stroke editing callbacks, but present only the
    // controls belonging to this task. The old toolbox stays inaccessible.
    build_repairs(7);
    auto* oldSize=findChild<QSlider*>("portraitRepairSize");auto* oldFeather=findChild<QSlider*>("portraitRepairFeather");auto* oldOpacity=findChild<QSlider*>("portraitRepairOpacity");auto* list=findChild<QListWidget*>("portraitRepairList");
    auto* management=list?list->parentWidget():nullptr;
    auto* stash=new QWidget(parameter_host_);auto* stashLayout=new QVBoxLayout(stash);
    while(parameter_layout_->count()>1){auto* item=parameter_layout_->takeAt(1);if(auto* w=item->widget()){if(w!=management)stashLayout->addWidget(w);}delete item;}
    stash->hide();parameter_layout_->addWidget(stash);
    details_->hide();if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->hide();
    const bool clone=property("portraitEraseMode").toInt()==1;
    auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitEraseTabs");tabs->addTab(tr("修复"));tabs->addTab(tr("仿制"));tabs->setExpanding(true);tabs->setCurrentIndex(clone?1:0);parameter_layout_->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();finish_gesture();setProperty("portraitEraseMode",i);setProperty("portraitCloneOffsetValid",false);repair_mode_=i==1?5:1;repair_selected_=-1;repair_has_source_=false;range_mode_=0;view_->set_tool(-1);build_parameters();status_->setText(i==1?tr("Alt 单击取样，松开后涂抹"):tr("直接涂抹；Alt 可指定来源"));});
    auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitEraseSize",1,500,brush_size_->value());size.host->setMinimumHeight(58);parameter_layout_->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,oldSize,&QSlider::setValue);
    auto strength=portrait_integer_row(parameter_host_,tr("程度"),"portraitEraseAmount",0,100,int(repair_opacity_*100));strength.host->setMinimumHeight(58);bind_slider(strength.slider,70);parameter_layout_->addWidget(strength.host);connect(strength.slider,&QSlider::valueChanged,oldOpacity,&QSlider::setValue);
    connect(list,&QListWidget::currentItemChanged,this,[this,slider=strength.slider](QListWidgetItem*){slider->setValue(int(repair_opacity_*100));});
    auto* brushToggle=new QToolButton(parameter_host_);brushToggle->setText(tr("笔刷细节"));brushToggle->setObjectName("portraitEraseBrushDetails");brushToggle->setCheckable(true);brushToggle->setArrowType(Qt::RightArrow);brushToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(brushToggle);
    auto* brush=new QWidget(parameter_host_);auto* brushLayout=new QVBoxLayout(brush);brushLayout->setContentsMargins(0,0,0,0);auto feather=portrait_integer_row(brush,tr("羽化"),"portraitEraseFeather",0,100,brush_feather_->value());feather.host->setMinimumHeight(58);brushLayout->addWidget(feather.host);connect(feather.slider,&QSlider::valueChanged,oldFeather,&QSlider::setValue);brush->hide();parameter_layout_->addWidget(brush);connect(brushToggle,&QToolButton::toggled,this,[brush,brushToggle](bool open){brush->setVisible(open);brushToggle->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    auto* records=new QToolButton(parameter_host_);records->setText(tr("修复记录"));records->setObjectName("portraitEraseRecords");records->setCheckable(true);records->setArrowType(Qt::RightArrow);records->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(records);
    if(management){management->setParent(parameter_host_);management->hide();parameter_layout_->addWidget(management);connect(records,&QToolButton::toggled,this,[management,records](bool open){management->setVisible(open);records->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});}
    parameter_layout_->addStretch();auto* hint=new QLabel(clone?tr("Alt 单击取样，松开后涂抹。来源十字随笔刷保持偏移。"):tr("直接涂抹；Alt 可指定来源。"),parameter_host_);hint->setWordWrap(true);parameter_layout_->addWidget(hint);
}

void PortraitDialog::build_skin_colors(){
    const auto apply_color=[this](QColor color){if(selected_<0 || !color.isValid())return;QImage image(1,1,QImage::Format_RGBA8888);image.setColorSpace(QColorSpace::SRgb);image.setPixelColor(0,0,color);const auto linear=editing::to_working(image);const auto* p=reinterpret_cast<const float*>(linear.constBits());auto& s=people_[std::size_t(selected_)].settings;s["skinColor"]=QJsonArray{p[0],p[1],p[2]};s["skinColorName"]=color.name();set_parameter("color.custom",50);build_parameters();};
    auto* bar=new QWidget(parameter_host_);auto* colorsRow=new QHBoxLayout(bar);colorsRow->setContentsMargins(0,4,0,8);
    auto* sample=new QToolButton(bar);sample->setObjectName("portraitSampleSkinColor");sample->setIcon(portrait_task_icon(12));sample->setToolTip(tr("从画面取肤色"));sample->setFixedSize(40,36);colorsRow->addWidget(sample);
    connect(sample,&QToolButton::clicked,this,[this]{color_previous_tool_=std::max(0,view_->active_tool());color_sampling_=true;balance_sampling_=false;range_mode_=repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);status_->setText(tr("单击皮肤取色，完成后自动返回"));});
    int i=0;for(const auto* hex:{"#e6c7b4","#deb6a0","#d8a59b","#be927a","#a77f6c"}){auto* swatch=portrait_color_swatch(bar,QColor(hex));swatch->setObjectName(QString("portraitSkinSwatch%1").arg(i++));colorsRow->addWidget(swatch);connect(swatch,&QPushButton::clicked,this,[apply_color,hex]{apply_color(QColor(hex));});}
    auto* choose=new QToolButton(bar);choose->setArrowType(Qt::DownArrow);choose->setToolTip(tr("选择其他肤色"));choose->setObjectName("portraitChooseSkinColor");colorsRow->addWidget(choose);connect(choose,&QToolButton::clicked,this,[this,apply_color]{apply_color(QColorDialog::getColor(QColor(215,167,137),this,tr("肤色")));});parameter_layout_->addWidget(bar);
    for(const auto& entry:QList<QPair<QString,QString>>{{"color.light",tr("明暗")},{"color.warmth",tr("冷暖")},{"color.rosy",tr("红润")},{"color.redness",tr("去泛红")},{"color.balance",tr("脸颈身体协调")}})add_parameter_row(parameter_layout_,entry.first,entry.second);
    auto* autoSkin=new QCheckBox(tr("自动肤区内应用"),parameter_host_);autoSkin->setObjectName("portraitColorAutomatic");autoSkin->setChecked(selected_<0 || people_[std::size_t(selected_)].settings.value("colorAutoScope").toBool(true));parameter_layout_->addWidget(autoSkin);
    connect(autoSkin,&QCheckBox::toggled,this,[this](bool checked){if(selected_<0)return;finish_brush_gesture();auto& settings=people_[std::size_t(selected_)].settings;settings["colorAutoScope"]=checked;auto masks=settings["masks"].toObject();if(checked)masks.remove("color.scope");else{QImage empty(preview_.size().scaled(768,768,Qt::KeepAspectRatio),QImage::Format_Grayscale8);empty.fill(0);masks["color.scope"]=editing::portrait_mask_png(empty);}settings["masks"]=masks;remember();});
    auto* correct=new QToolButton(parameter_host_);correct->setText(tr("局部修正"));correct->setObjectName("portraitSkinColorCorrect");parameter_layout_->addWidget(correct);connect(correct,&QToolButton::clicked,this,[this]{refresh_ranges();if(range_key_->findData("color.scope")<0)range_key_->addItem(tr("肤色范围"),"color.scope");range_key_->setCurrentIndex(range_key_->findData("color.scope"));if(auto* scope=findChild<QToolButton*>("portraitScopeHeader")){scope->show();scope->setChecked(true);}inspector_tabs_->setCurrentIndex(1);});
    auto* expand=new QToolButton(parameter_host_);expand->setText(tr("自定义色样"));expand->setCheckable(true);expand->setArrowType(Qt::RightArrow);expand->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(expand);
    auto* content=new QWidget(parameter_host_);auto* customLayout=new QVBoxLayout(content);customLayout->setContentsMargins(0,0,0,0);content->hide();parameter_layout_->addWidget(content);connect(expand,&QToolButton::toggled,this,[expand,content](bool open){content->setVisible(open);expand->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    add_parameter_row(customLayout,"color.custom",tr("色样程度"));
        auto* palette=new QComboBox(content);palette->setObjectName("portraitSkinPalette");const auto colors=QJsonDocument::fromJson(QSettings().value("portrait/colors").toByteArray()).array();palette->addItem(tr("已保存的肤色"));for(const auto& entry:colors){const auto c=entry.toObject();palette->addItem(c["name"].toString(),c["hex"].toString());}palette->setCurrentIndex(std::clamp(QSettings().value("portrait/colorSelection").toInt(),0,int(colors.size())));customLayout->addWidget(palette);connect(palette,&QComboBox::activated,this,[this,apply_color,palette,colors](int i){
            QSettings().setValue("portrait/colorSelection",i);if(i<=0 || selected_<0)return;
            const auto entry=colors[i-1].toObject();const auto linear=entry["linearRec2020"].toArray();
            const bool valid=linear.size()==3 && std::all_of(linear.begin(),linear.end(),[](const QJsonValue& v){return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=0;});
            if(valid && entry["colorSpace"].toString()=="linear-rec2020"){
                auto& settings=people_[std::size_t(selected_)].settings;settings["skinColor"]=linear;settings["skinColorName"]=entry["name"].toString();settings.remove("skinSample");
                const double current=settings["parameters"].toObject()["color.custom"].toDouble();set_parameter("color.custom",current>0?current:50);build_parameters();
            }else apply_color(QColor(palette->itemData(i).toString()));
        });
        auto* save_color=new QPushButton(tr("命名并保存当前肤色…"),content);save_color->setObjectName("portraitSaveSkinColor");save_color->setEnabled(selected_>=0);customLayout->addWidget(save_color);
        connect(save_color,&QPushButton::clicked,this,[this]{if(selected_<0)return;const auto settings=people_[std::size_t(selected_)].settings;const auto color=settings["skinColor"].toArray();if(color.size()!=3)return;QImage image(1,1,QImage::Format_RGBA32FPx4_Premultiplied);image.setColorSpace(editing::working_space());auto* p=reinterpret_cast<float*>(image.bits());for(int c=0;c<3;++c)p[c]=float(color[c].toDouble());p[3]=1;const auto hex=editing::display_image(image).pixelColor(0,0).name();bool ok=false;const auto name=QInputDialog::getText(this,tr("保存肤色"),tr("名称"),QLineEdit::Normal,hex,&ok);if(!ok || name.trimmed().isEmpty())return;QSettings config;auto colors=QJsonDocument::fromJson(config.value("portrait/colors").toByteArray()).array();colors.append(QJsonObject{{"name",name.left(80)},{"hex",hex},{"linearRec2020",color},{"colorSpace","linear-rec2020"}});while(colors.size()>64)colors.removeFirst();config.setValue("portrait/colors",QJsonDocument(colors).toJson(QJsonDocument::Compact));build_parameters();});
        auto* remove_color=new QPushButton(tr("删除所选色样"),content);remove_color->setObjectName("portraitRemoveSkinColor");customLayout->addWidget(remove_color);connect(remove_color,&QPushButton::clicked,this,[this,palette]{const int i=palette->currentIndex()-1;if(i<0)return;QSettings config;auto colors=QJsonDocument::fromJson(config.value("portrait/colors").toByteArray()).array();if(i<colors.size())colors.removeAt(i);config.setValue("portrait/colorSelection",0);config.setValue("portrait/colors",QJsonDocument(colors).toJson(QJsonDocument::Compact));build_parameters();});
}

void PortraitDialog::build_decals(const QString& category){
    if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;const auto items=settings["decals"].toArray();
    auto add=[this,category](QString asset,QString encoded,QString name){if(selected_<0)return;finish_brush_gesture();finish_gesture();auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["decals"].toArray();if(list.size()>=24){status_->setText(tr("单个人物最多放置24个饰品"));return;}auto assets=settings["decalAssets"].toObject();assets[asset]=encoded;settings["decalAssets"]=assets;const auto f=people_[std::size_t(selected_)].face;QImage assetImage;if(encoded.startsWith("resource:"))assetImage.load(encoded.mid(9));else assetImage=QImage::fromData(QByteArray::fromBase64(encoded.toLatin1()),"PNG");const double ratio=assetImage.isNull()?1.:double(assetImage.height())/assetImage.width();const double w=f.width()*.5,h=w*source_.width()/source_.height()*ratio,cx=std::clamp(f.right(),w*.5,1-w*.5),cy=std::clamp(category=="hair"?f.top():f.center().y(),h*.5,1-h*.5);const QString id=QUuid::createUuid().toString(QUuid::WithoutBraces);list.append(QJsonObject{{"id",id},{"asset",asset},{"category",category},{"name",name},{"opacity",1.},{"quad",QJsonArray{QJsonArray{cx-w*.5,cy-h*.5},QJsonArray{cx+w*.5,cy-h*.5},QJsonArray{cx+w*.5,cy+h*.5},QJsonArray{cx-w*.5,cy+h*.5}}}});settings["decals"]=list;setProperty("portraitDecalId",id);remember();placement_key_="decal:"+id;range_mode_=repair_mode_=0;painting_mesh_=editing::portrait_deformation(source_.size(),recipe());view_->set_tool(-1);build_parameters();update_overlay();};
    auto* cards=new QWidget(parameter_host_);auto* row=new QHBoxLayout(cards);row->setContentsMargins(0,8,0,8);const QStringList assets=category=="tattoo"?QStringList{"botanical-tattoo"}:category=="makeup"?QStringList{"blue-flower","white-flower"}:QStringList{"bow","blue-flower","white-flower"},names=category=="tattoo"?QStringList{tr("植物纹身")}:category=="makeup"?QStringList{tr("蓝色花朵"),tr("白色花朵")}:QStringList{tr("缎带蝴蝶结"),tr("蓝色花朵"),tr("白色花朵")};for(int i=0;i<assets.size();++i){auto* card=portrait_style_card(cards,names[i]);card->setObjectName("portraitDecalAdd_"+assets[i]);card->setIcon(QIcon(":/huelith/portrait/decals/"+assets[i]+".png"));card->setIconSize({80,80});row->addWidget(card);connect(card,&QToolButton::clicked,this,[add,asset=assets[i],name=names[i]]{add(asset,"resource::/huelith/portrait/decals/"+asset+".png",name);});}parameter_layout_->addWidget(cards);
    auto* import=new QToolButton(parameter_host_);import->setText(tr("导入透明PNG…"));parameter_layout_->addWidget(import,0,Qt::AlignRight);connect(import,&QToolButton::clicked,this,[this,add]{const auto path=QFileDialog::getOpenFileName(this,tr("导入饰品"),{},tr("PNG图片 (*.png)"));if(path.isEmpty())return;QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>24*1024*1024){status_->setText(tr("PNG文件不可读或超过24MB"));return;}const auto bytes=file.readAll();const auto image=QImage::fromData(bytes,"PNG");if(image.isNull() || qint64(image.width())*image.height()>32000000 || !image.hasAlphaChannel()){status_->setText(tr("请选择含透明通道、不超过3200万像素的PNG"));return;}add(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()),QString::fromLatin1(bytes.toBase64()),QFileInfo(path).baseName());});
    auto* list=new QComboBox(parameter_host_);list->setObjectName("portraitDecals");for(const auto& v:items){const auto item=v.toObject();if(item["category"].toString()==category)list->addItem(item["name"].toString(),item["id"].toString());}list->setCurrentIndex(list->findData(property("portraitDecalId")));if(list->currentIndex()<0 && list->count())list->setCurrentIndex(0);parameter_layout_->addWidget(list);connect(list,&QComboBox::activated,this,[this,list](int){finish_brush_gesture();setProperty("portraitDecalId",list->currentData());placement_key_="decal:"+list->currentData().toString();range_mode_=repair_mode_=0;view_->set_tool(-1);build_parameters();update_overlay();});
    const QString id=list->currentData().toString();if(id.isEmpty())return;setProperty("portraitDecalId",id);QJsonObject active;for(const auto& v:items)if(v.toObject()["id"].toString()==id)active=v.toObject();auto amount=portrait_integer_row(parameter_host_,tr("程度"),"portraitDecalAmount",0,100,int(active["opacity"].toDouble(1)*100));parameter_layout_->addWidget(amount.host);connect(amount.slider,&QSlider::valueChanged,this,[this,id](int v){auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["decals"].toArray();for(int i=0;i<list.size();++i){auto item=list[i].toObject();if(item["id"].toString()==id){item["opacity"]=v*.01;list[i]=item;}}settings["decals"]=list;remember();});
    auto* tint=new QToolButton(parameter_host_);tint->setText(tr("颜色…"));parameter_layout_->addWidget(tint,0,Qt::AlignRight);connect(tint,&QToolButton::clicked,this,[this,id]{const QColor c=QColorDialog::getColor(Qt::white,this,tr("饰品染色：白色保留原色"));if(!c.isValid())return;auto& s=people_[std::size_t(selected_)].settings;auto list=s["decals"].toArray();for(int i=0;i<list.size();++i){auto item=list[i].toObject();if(item["id"].toString()==id){item["tint"]=QJsonArray{c.redF(),c.greenF(),c.blueF()};list[i]=item;}}s["decals"]=list;remember();});
    auto* note=new QLabel(tr("拖动饰品移动；拖角点缩放或贴合，拖外侧圆柄旋转。遮挡擦除固定在人物上。"),parameter_host_);note->setWordWrap(true);parameter_layout_->addWidget(note);
    if(range_mode_ && range_key_->currentData().toString()=="decal."+id){auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitDecalBrushSize",1,500,brush_size_->value());parameter_layout_->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,brush_size_,&QSlider::setValue);}
    if(!range_mode_){placement_key_="decal:"+id;view_->set_tool(-1);update_overlay();}
}
void PortraitDialog::update_decal_overlay(QPainterPath& path){
    auto* bar=view_->viewport()->findChild<QWidget*>("portraitDecalToolbar");const bool active=selected_>=0 && ((property("portraitWorkspaceTask").toInt()==1 && property("portraitHairPage").toInt()==3) || placement_key_.startsWith("decal:"));if(!active){if(bar)bar->hide();return;}auto& settings=people_[std::size_t(selected_)].settings;QJsonArray quad;const QString id=property("portraitDecalId").toString();for(const auto& v:settings["decals"].toArray())if(v.toObject()["id"].toString()==id)quad=v.toObject()["quad"].toArray();if(quad.size()!=4){if(bar)bar->hide();return;}
    QPolygonF points;for(const auto& v:quad){auto a=v.toArray();points<<repair_scene_position({a[0].toDouble(),a[1].toDouble()});}path.addPolygon(points);path.closeSubpath();const double radius=5/std::max(.01,view_->transform().m11());for(auto p:points)path.addRect(QRectF(p-QPointF(radius,radius),QSizeF(radius*2,radius*2)));const auto top=(points[0]+points[1])*.5,center=(points[0]+points[1]+points[2]+points[3])*.25,rotate=top+(top-center)*.35;path.moveTo(top);path.lineTo(rotate);path.addEllipse(rotate,radius,radius);
    if(!bar){bar=new QWidget(view_->viewport());bar->setObjectName("portraitDecalToolbar");bar->setStyleSheet("background:#252a30;border-radius:4px");auto* row=new QHBoxLayout(bar);row->setContentsMargins(4,4,4,4);for(int i=0;i<3;++i){auto* b=new QPushButton(i==0?tr("移动"):i==1?tr("遮挡修正"):tr("删除"),bar);b->setObjectName(QString("portraitDecalAction%1").arg(i));row->addWidget(b);connect(b,&QPushButton::clicked,this,[this,i]{finish_brush_gesture();finish_gesture();const QString id=property("portraitDecalId").toString();if(i==1){activate_hair_brush("decal."+id,2);}else if(i==0){range_mode_=0;setProperty("portraitHairBrushKey",QString{});placement_key_="decal:"+id;view_->set_tool(-1);}else{auto& s=people_[std::size_t(selected_)].settings;auto list=s["decals"].toArray();for(int j=list.size()-1;j>=0;--j)if(list[j].toObject()["id"].toString()==id)list.removeAt(j);s["decals"]=list;auto masks=s["masks"].toObject();masks.remove("decal."+id);s["masks"]=masks;placement_key_.clear();setProperty("portraitDecalId",QString{});remember();}build_parameters();update_overlay();});}bar->adjustSize();}
    const auto bottom=view_->mapFromScene(points.boundingRect().bottomLeft());bar->move(std::clamp(bottom.x(),0,std::max(0,view_->viewport()->width()-bar->width())),std::clamp(bottom.y()+10,0,std::max(0,view_->viewport()->height()-bar->height())));bar->show();bar->raise();
}

void PortraitDialog::update_hair_source_toolbar(){
    auto* bar=view_->viewport()->findChild<QWidget*>("portraitHairSourceToolbar");const bool visible=selected_>=0 && property("portraitWorkspaceTask").toInt()==1 && property("portraitHairPage").toInt()==0 && property("portraitHairVolume").toInt()==3 && repair_has_source_;
    if(!visible){if(bar)bar->hide();return;}if(!bar){bar=new QWidget(view_->viewport());bar->setObjectName("portraitHairSourceToolbar");bar->setStyleSheet("background:#252a30;border-radius:3px");auto* row=new QHBoxLayout(bar);row->setContentsMargins(4,4,4,4);for(const auto& spec:QList<QPair<QString,QString>>{{"portraitHairSourceAngle",tr("角度")},{"portraitHairSourceScale",tr("缩放 %")}}){const bool angle=spec.first.endsWith("Angle");row->addWidget(new QLabel(spec.second,bar));auto* number=new QSpinBox(bar);number->setObjectName(spec.first);number->setRange(angle?-180:25,angle?180:200);number->setValue(property(spec.first.toUtf8()).isValid()?property(spec.first.toUtf8()).toInt():(angle?0:100));row->addWidget(number);connect(number,qOverload<int>(&QSpinBox::valueChanged),this,[this,name=spec.first](int value){setProperty(name.toUtf8(),value);});}bar->adjustSize();}const auto point=view_->mapFromScene(repair_scene_position(repair_source_));bar->move(std::clamp(point.x()+15,0,std::max(0,view_->viewport()->width()-bar->width())),std::clamp(point.y()+20,0,std::max(0,view_->viewport()->height()-bar->height())));bar->show();bar->raise();
}

void PortraitDialog::activate_paint_brush(int mode){
    if(selected_<0)return;finish_brush_gesture();const int type=property("portraitPaintType").toInt();
    const QString key=type==0?(property("portraitPaintDark").toBool()?"paint.shadow":"paint.highlight"):type==1?"paint.makeup":type==2?"paint.skin":type==3?"paint.glitter":type==4?"hair.color":property("portraitPaintDark").toBool()?"paint.dark":"paint.light";
    setProperty("portraitPaintKey",key);setProperty("portraitPaintMode",mode);placement_key_.clear();repair_mode_=0;locate_->setChecked(false);refresh_ranges();if(range_key_->findData(key)<0)range_key_->addItem(tr("当前笔刷"),key);range_key_->setCurrentIndex(range_key_->findData(key));
    const bool locked=people_[std::size_t(selected_)].settings["paintLocked"].toObject()[key].toBool();range_mode_=locked?0:mode;view_->set_tool(locked?0:-1);view_->setProperty("portraitBrushCursor",true);range_visible_->setChecked(false);
}
void PortraitDialog::build_paint_parameters(){
    details_->hide();if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->hide();if(auto* old=findChild<QTabBar*>("portraitTaskTabs"))old->hide();
    const int type=property("portraitPaintType").toInt();auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitPaintTabs");for(auto t:{tr("修容"),tr("彩妆"),tr("肤色"),tr("亮片"),tr("染发"),tr("补光")})tabs->addTab(t);tabs->setExpanding(true);tabs->setCurrentIndex(type);parameter_layout_->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();finish_gesture();setProperty("portraitPaintType",i);range_mode_=repair_mode_=0;setProperty("portraitMaterialSampleKey",QString{});build_parameters();activate_panel_tool();});
    if(selected_<0){parameter_layout_->addWidget(new QLabel(tr("请先选择人物"),parameter_host_));return;}auto& settings=people_[std::size_t(selected_)].settings;
    const bool automatic=type==5 && property("portraitPaintLightMode").toInt()==0;
    if(type==5){auto* mode=new QTabBar(parameter_host_);mode->setObjectName("portraitPaintLightTabs");mode->addTab(tr("自动"));mode->addTab(tr("手动"));mode->setCurrentIndex(property("portraitPaintLightMode").toInt());parameter_layout_->addWidget(mode);connect(mode,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();setProperty("portraitPaintLightMode",i);setProperty("portraitPaintDark",false);range_mode_=0;view_->set_tool(0);build_parameters();activate_panel_tool();});}
    const QString key=type==0?(property("portraitPaintDark").toBool()?"paint.shadow":"paint.highlight"):type==1?"paint.makeup":type==2?"paint.skin":type==3?"paint.glitter":type==4?"hair.color":!automatic && property("portraitPaintDark").toBool()?"paint.dark":"paint.light";
    setProperty("portraitPaintKey",key);
    if(type==0 || (type==5 && !automatic)){auto* row=new QWidget(parameter_host_);auto* line=new QHBoxLayout(row);line->setContentsMargins(0,8,0,8);for(bool dark:{false,true}){auto* b=new QPushButton(type==0?(dark?tr("阴影"):tr("高光")):(dark?tr("压暗"):tr("提亮")),row);b->setObjectName(dark?"portraitPaintDark":"portraitPaintLight");b->setCheckable(true);b->setChecked(dark==property("portraitPaintDark").toBool());line->addWidget(b);connect(b,&QPushButton::clicked,this,[this,dark]{finish_brush_gesture();setProperty("portraitPaintDark",dark);build_parameters();activate_paint_brush();});}parameter_layout_->addWidget(row);}
    if(automatic){auto* row=new QWidget(parameter_host_);auto* line=new QHBoxLayout(row);line->setContentsMargins(0,8,0,8);for(int i=0;i<2;++i){auto* b=new QPushButton(i?tr("逆光提亮"):tr("自然"),row);b->setObjectName(QString("portraitPaintAuto%1").arg(i));line->addWidget(b);connect(b,&QPushButton::clicked,this,[this,i]{finish_brush_gesture();auto& settings=people_[std::size_t(selected_)].settings;auto masks=settings["masks"].toObject();masks["paint.light"]=settings["automaticMasks"].toObject()["skin"];settings["masks"]=masks;auto p=settings["parameters"].toObject();p["paint.light"]=i?60:30;settings["parameters"]=p;remember();build_parameters();});}parameter_layout_->addWidget(row);}
    auto params=settings["parameters"].toObject();for(const auto& v:QList<QPair<QString,int>>{{"paint.grainSize",35},{"paint.grainDensity",35},{"paint.protect",80}})if(!params.contains(v.first))params[v.first]=v.second;settings["parameters"]=params;if(!params.contains(key) && !automatic){params[key]=60;settings["parameters"]=params;}
    if(type>=1 && type<=4){auto* row=new QWidget(parameter_host_);auto* line=new QHBoxLayout(row);line->setContentsMargins(0,6,0,6);auto colors=settings["colors"].toObject();if(!colors.contains(key)){colors[key]=type==3?QJsonArray{.95,.7,.3}:type==2?QJsonArray{.5,.3,.2}:type==4?QJsonArray{.12,.04,.025}:QJsonArray{.4,.07,.08};settings["colors"]=colors;}
        auto apply=[this,key](QColor color){if(!color.isValid())return;QImage pixel(1,1,QImage::Format_RGBA8888);pixel.fill(color);pixel.setColorSpace(QColorSpace::SRgb);const auto linear=editing::to_working(pixel);const auto* p=reinterpret_cast<const float*>(linear.constBits());auto& settings=people_[std::size_t(selected_)].settings;auto colors=settings["colors"].toObject();colors[key]=QJsonArray{p[0],p[1],p[2]};settings["colors"]=colors;settings["selectedColor_"+key]=color.name();for(auto* chip:parameter_host_->findChildren<QPushButton*>())if(chip->objectName().startsWith("portraitPaintColor"))chip->setChecked(chip->toolTip()==color.name());auto history=settings["paintRecentColors"].toArray();for(int i=history.size()-1;i>=0;--i)if(history[i].toString()==color.name())history.removeAt(i);history.prepend(color.name());while(history.size()>6)history.removeLast();settings["paintRecentColors"]=history;if(key=="hair.color")settings["hairColorVersion"]=2;remember();};
        const QStringList palette=type==2?QStringList{"#e6c7b4","#bf9478","#daa897","#89685b","#b49b8f"}:type==3?QStringList{"#fff8e0","#e8be6b","#e2adce","#a0d8e8","#bdafe0"}:QStringList{"#a74755","#c8776e","#795849","#9d7652","#7b6c98"};for(int i=0;i<palette.size();++i){auto* b=portrait_color_swatch(row,QColor(palette[i]));b->setObjectName(QString("portraitPaintColor%1").arg(i));b->setChecked(settings["selectedColor_"+key].toString()==palette[i]);line->addWidget(b);connect(b,&QPushButton::clicked,this,[apply,c=QColor(palette[i])]{apply(c);});}auto* more=new QToolButton(row);more->setText(tr("更多…"));line->addWidget(more);connect(more,&QToolButton::clicked,this,[this,apply]{apply(QColorDialog::getColor(Qt::white,this,tr("选择颜色")));});auto* pick=new QToolButton(row);pick->setText(tr("取色"));pick->setObjectName("portraitPaintSample");line->addWidget(pick);connect(pick,&QToolButton::clicked,this,[this,key]{finish_brush_gesture();setProperty("portraitMaterialSampleKey",key);setProperty("portraitMaterialSampleEnd",false);range_mode_=repair_mode_=0;view_->set_tool(-1);});parameter_layout_->addWidget(row);if(!settings["paintRecentColors"].toArray().isEmpty()){auto* recent=new QComboBox(parameter_host_);recent->setObjectName("portraitPaintRecent");recent->addItem(tr("最近使用的颜色"));for(const auto& v:settings["paintRecentColors"].toArray())recent->addItem(v.toString(),v.toString());parameter_layout_->addWidget(recent);connect(recent,&QComboBox::activated,this,[recent,apply](int i){if(i>0)apply(QColor(recent->itemData(i).toString()));});}
    }
    if(!automatic){auto* row=new QWidget(parameter_host_);auto* line=new QHBoxLayout(row);line->setContentsMargins(0,4,0,4);for(int mode:{1,2}){auto* b=new QPushButton(mode==1?tr("涂抹"):tr("擦除"),row);b->setObjectName(mode==1?"portraitPaintDraw":"portraitPaintErase");line->addWidget(b);connect(b,&QPushButton::clicked,this,[this,mode]{activate_paint_brush(mode);});}parameter_layout_->addWidget(row);auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitPaintSize",1,500,brush_size_->value());parameter_layout_->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,brush_size_,&QSlider::setValue);}
    add_parameter_row(parameter_layout_,key,tr("程度"));
    if(!automatic){auto* lock=new QCheckBox(tr("锁定区域"),parameter_host_);lock->setObjectName("portraitPaintLock");lock->setChecked(settings["paintLocked"].toObject()[key].toBool());parameter_layout_->addWidget(lock);connect(lock,&QCheckBox::toggled,this,[this,key](bool locked){finish_brush_gesture();auto& settings=people_[std::size_t(selected_)].settings;auto locks=settings["paintLocked"].toObject();locks[key]=locked;settings["paintLocked"]=locks;remember();activate_paint_brush();});}
    auto* detail=new QToolButton(parameter_host_);detail->setText(tr("细节"));detail->setCheckable(true);detail->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);detail->setArrowType(Qt::RightArrow);parameter_layout_->addWidget(detail);auto* host=new QWidget(parameter_host_);auto* rows=new QVBoxLayout(host);rows->setContentsMargins(0,0,0,0);if(type==1)add_parameter_row(rows,"paint.gloss",tr("光泽"));if(type==3){add_parameter_row(rows,"paint.grainSize",tr("粒径"));add_parameter_row(rows,"paint.grainDensity",tr("密度"));}if(type==0 || type==5)add_parameter_row(rows,"paint.protect",tr("高光保护"));if(!automatic){auto feather=portrait_integer_row(host,tr("羽化"),"portraitPaintFeather",0,100,brush_feather_->value());rows->addWidget(feather.host);connect(feather.slider,&QSlider::valueChanged,brush_feather_,&QSlider::setValue);}host->hide();parameter_layout_->addWidget(host);connect(detail,&QToolButton::toggled,this,[detail,host](bool open){host->setVisible(open);detail->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    parameter_layout_->addStretch();if(!automatic)activate_paint_brush(property("portraitPaintMode").toInt()==2?2:1);
}

void PortraitDialog::activate_hair_brush(const QString& key,int mode){
    if(selected_<0)return;finish_brush_gesture();placement_key_.clear();repair_mode_=0;locate_->setChecked(false);refresh_ranges();if(range_key_->findData(key)<0)range_key_->addItem(tr("当前范围"),key);range_key_->setCurrentIndex(range_key_->findData(key));range_mode_=mode;view_->set_tool(-1);view_->setProperty("portraitBrushCursor",true);setProperty("portraitHairBrushKey",key);setProperty("portraitHairBrushMode",mode);update_overlay();
}
void PortraitDialog::build_hair_parameters(){
    details_->hide();if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->hide();if(auto* old=findChild<QTabBar*>("portraitTaskTabs"))old->hide();
    const int page=property("portraitHairPage").toInt();auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitHairTabs");for(auto t:{tr("发量"),tr("发质"),tr("染发"),tr("发饰")})tabs->addTab(t);tabs->setExpanding(true);tabs->setCurrentIndex(page);parameter_layout_->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();finish_gesture();placement_key_.clear();repair_mode_=range_mode_=0;view_->set_tool(0);setProperty("portraitHairPage",i);setProperty("portraitHairBrushKey",QString{});build_parameters();activate_panel_tool();});
    if(selected_<0){parameter_layout_->addWidget(new QLabel(tr("请先选择人物"),parameter_host_));return;}auto& s=people_[std::size_t(selected_)].settings;const auto params=s["parameters"].toObject();
    auto button=[&](const QString& text,const QString& name,auto callback){auto* b=new QPushButton(text,parameter_host_);b->setObjectName(name);parameter_layout_->addWidget(b);connect(b,&QPushButton::clicked,this,callback);return b;};
    auto fold=[&](QWidget* host){auto* toggle=new QToolButton(parameter_host_);toggle->setText(tr("细节"));toggle->setCheckable(true);toggle->setArrowType(Qt::RightArrow);toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(toggle);parameter_layout_->addWidget(host);host->hide();connect(toggle,&QToolButton::toggled,this,[toggle,host](bool open){host->setVisible(open);toggle->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});};
    auto brush=[&](const QString& key){auto* row=new QWidget(parameter_host_);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);for(int m:{1,2}){auto* b=new QPushButton(m==1?tr("涂抹"):tr("擦除"),row);b->setObjectName(m==1?"portraitHairPaint":"portraitHairErase");layout->addWidget(b);connect(b,&QPushButton::clicked,this,[this,key,m]{activate_hair_brush(key,m);});}parameter_layout_->addWidget(row);auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitHairBrushSize",1,500,brush_size_->value());parameter_layout_->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,brush_size_,&QSlider::setValue);};
    auto color=[&](const QString& key,bool second=false){if(!second){auto* family=new QTabBar(parameter_host_);family->addTab(tr("自然色"));family->addTab(tr("彩色"));family->setObjectName("portraitHairColorFamily");family->setCurrentIndex(property("portraitHairColorFamily").toInt());parameter_layout_->addWidget(family);connect(family,&QTabBar::currentChanged,this,[this](int i){setProperty("portraitHairColorFamily",i);build_parameters();});}auto* row=new QWidget(parameter_host_);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,6,0,6);auto apply=[this,key,second](QColor c){if(!c.isValid() || selected_<0)return;QImage pixel(1,1,QImage::Format_RGBA8888);pixel.fill(c);pixel.setColorSpace(QColorSpace::SRgb);auto linear=editing::to_working(pixel);auto* v=reinterpret_cast<const float*>(linear.constBits());QJsonArray rgb{v[0],v[1],v[2]};auto& settings=people_[std::size_t(selected_)].settings;settings["hairColorVersion"]=2;settings["selectedColor_"+key+(second?"_end":"")]=c.name();for(auto* chip:parameter_host_->findChildren<QPushButton*>())if(chip->property("hairColorSlot").toString()==key+(second?"_end":""))chip->setChecked(chip->toolTip()==c.name());auto recent=settings["hairRecentColors"].toArray();for(int i=recent.size()-1;i>=0;--i)if(recent[i].toString()==c.name())recent.removeAt(i);recent.prepend(c.name());while(recent.size()>6)recent.removeLast();settings["hairRecentColors"]=recent;auto amounts=settings["parameters"].toObject();if(amounts[key].toDouble()==0)amounts[key]=50;settings["parameters"]=amounts;if(second){auto zones=settings["hairZones"].toArray();for(int i=0;i<zones.size();++i){auto z=zones[i].toObject();if(z["key"].toString()==key){z["secondColor"]=rgb;zones[i]=z;}}settings["hairZones"]=zones;}else{auto colors=settings["colors"].toObject();colors[key]=rgb;settings["colors"]=colors;}remember();if(auto* number=findChild<QDoubleSpinBox*>("portraitParam_"+key)){QSignalBlocker block(number);number->setValue(amounts[key].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){QSignalBlocker block(slider);slider->setValue(amounts[key].toInt());}};for(const auto& hex:(property("portraitHairColorFamily").toInt()==0?QStringList{"#402b23","#6b4034","#9e6845","#b48c72","#674b3d","#c4ae8d"}:QStringList{"#9b4168","#534d96","#40888a","#a35337","#795577","#6f797f"})){auto* chip=portrait_color_swatch(row,QColor(hex));chip->setProperty("hairColorSlot",key+(second?"_end":""));chip->setChecked(s["selectedColor_"+key+(second?"_end":"")].toString()==hex);chip->setObjectName("portraitHairColor_"+QString(hex).mid(1)+(second?"_end":""));chip->setFixedSize(34,32);chip->setStyleSheet(QString("QPushButton{background:%1;border:1px solid #68727b;border-radius:4px;color:white;}QPushButton:checked{border:3px solid #63b4ff;font-weight:bold;}").arg(hex));layout->addWidget(chip);connect(chip,&QPushButton::clicked,this,[apply,hex]{apply(QColor(hex));});}auto* more=new QToolButton(row);more->setText(tr("更多…"));layout->addWidget(more);connect(more,&QToolButton::clicked,this,[this,apply]{apply(QColorDialog::getColor(Qt::darkRed,this,tr("选择发色")));});auto* pick=new QToolButton(row);pick->setText(tr("取色"));pick->setObjectName("portraitHairSample");layout->addWidget(pick);connect(pick,&QToolButton::clicked,this,[this,key,second]{finish_brush_gesture();setProperty("portraitMaterialSampleKey",key);setProperty("portraitMaterialSampleEnd",second);range_mode_=repair_mode_=0;placement_key_.clear();view_->set_tool(-1);status_->setText(tr("单击画布取色"));});parameter_layout_->addWidget(row);if(!second && !s["hairRecentColors"].toArray().isEmpty()){auto* recent=new QWidget(parameter_host_);auto* line=new QHBoxLayout(recent);line->setContentsMargins(0,0,0,0);line->addWidget(new QLabel(tr("最近"),recent));for(const auto& v:s["hairRecentColors"].toArray()){auto* chip=new QPushButton(recent);chip->setFixedSize(24,24);chip->setStyleSheet("background:"+v.toString());line->addWidget(chip);connect(chip,&QPushButton::clicked,this,[apply,c=QColor(v.toString())]{apply(c);});}line->addStretch();parameter_layout_->addWidget(recent);}};
    if(page<=1){
        const bool quality=page==1;const int selected=std::clamp(property(quality?"portraitHairQuality":"portraitHairVolume").toInt(),0,quality?4:3);const QStringList labels=quality?QStringList{tr("原图"),tr("去油"),tr("柔顺"),tr("光泽"),tr("水光")}:QStringList{tr("高颅顶"),tr("头发丰盈"),tr("发际线"),tr("补发")};
        auto* cards=new QWidget(parameter_host_);auto* row=new QHBoxLayout(cards);row->setContentsMargins(0,8,0,8);row->setSpacing(4);for(int i=0;i<labels.size();++i){auto* card=portrait_style_card(cards,labels[i]);card->setObjectName(QString("portraitHairCard%1").arg(i));const QPixmap sheet(quality?":/huelith/portrait/hair-quality-approved.png":":/huelith/portrait/hair-volume-approved.png");card->setIcon(QIcon(sheet.copy(quality?QRect(1144+i*87,242,74,88):QRect(1166+i*103,242,90,90))));card->setIconSize(quality?QSize(64,76):QSize(76,76));card->setChecked(i==selected);row->addWidget(card);connect(card,&QToolButton::clicked,this,[this,quality,i]{finish_brush_gesture();finish_gesture();setProperty(quality?"portraitHairQuality":"portraitHairVolume",i);range_mode_=repair_mode_=0;setProperty("portraitHairBrushKey",QString{});view_->set_tool(0);if(quality){auto& settings=people_[std::size_t(selected_)].settings;auto p=settings["parameters"].toObject();const QStringList keys{"","hair.oil","hair.smooth","hair.shine","hair.water"};for(int n=1;n<keys.size();++n)p[keys[n]]=n==i?std::max(40.,p[keys[n]].toDouble()):0;settings["parameters"]=p;settings["hairShineVersion"]=2;settings["hairSmoothVersion"]=2;remember();}build_parameters();activate_panel_tool();});}parameter_layout_->addWidget(cards);
        const QStringList keys=quality?QStringList{"","hair.oil","hair.smooth","hair.shine","hair.water"}:QStringList{"head.crown","head.hairVolume","head.hairline",""};const auto key=keys[selected];
        if(!quality && selected==3){auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitHairFillSize",1,500,brush_size_->value());parameter_layout_->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,brush_size_,&QSlider::setValue);auto amount=portrait_integer_row(parameter_host_,tr("程度"),"portraitHairFillAmount",0,100,int(repair_opacity_*100));parameter_layout_->addWidget(amount.host);connect(amount.slider,&QSlider::valueChanged,this,[this](int v){repair_opacity_=v*.01;});parameter_layout_->addWidget(new QLabel(tr("直接涂抹补发；自动取附近发束，Alt 可指定来源"),parameter_host_));}
        else if(!key.isEmpty()){add_parameter_row(parameter_layout_,key,tr("程度"));if(quality && key=="hair.smooth")add_parameter_row(parameter_layout_,"hair.detail",tr("细节保留"));if(quality)button(tr("局部涂抹"),"portraitHairLocal",[this,key,quality]{activate_hair_brush(quality?key:"hair.edge");build_parameters();});if(!property("portraitHairBrushKey").toString().isEmpty())brush(property("portraitHairBrushKey").toString());}
    }else if(page==2){
        const int mode=property("portraitHairDyeMode").toInt();auto* types=new QTabBar(parameter_host_);types->setObjectName("portraitHairDyeTabs");for(auto t:{tr("单色"),tr("分区"),tr("渐变"),tr("挑染")})types->addTab(t);types->setCurrentIndex(mode);types->setExpanding(true);parameter_layout_->addWidget(types);
        connect(types,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();finish_gesture();setProperty("portraitHairDyeMode",i);if(i>0 && selected_>=0){auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["hairZones"].toArray();const QString mode=i==2?"gradient":i==3?"highlights":"solid";int chosen=-1;for(int n=0;n<list.size();++n)if(list[n].toObject()["mode"].toString()==mode){chosen=n;break;}if(chosen<0 && i>=2){const auto f=people_[std::size_t(selected_)].face;const QString key="hair.zone_"+QUuid::createUuid().toString(QUuid::WithoutBraces);list.append(QJsonObject{{"key",key},{"name",i==2?tr("渐变"):tr("挑染")},{"mode",mode},{"strandVersion",2},{"strandWidth",35},{"strandDensity",40},{"gradientAxis",QJsonArray{f.center().x(),std::max(0.,f.top()-.5*f.height()),f.center().x(),f.bottom()}},{"secondColor",QJsonArray{.15,.04,.025}}});chosen=list.size()-1;settings["hairZones"]=list;settings["hairColorVersion"]=2;auto p=settings["parameters"].toObject();p[key]=50;settings["parameters"]=p;if(i==3){QImage mask(preview_.size().scaled(768,768,Qt::KeepAspectRatio),QImage::Format_Grayscale8);mask.fill(0);auto masks=settings["masks"].toObject();masks[key]=editing::portrait_mask_png(mask);settings["masks"]=masks;}remember();}setProperty("portraitHairZoneIndex",std::max(0,chosen));}setProperty("portraitHairRoots",false);setProperty("portraitHairBrushKey",QString{});placement_key_.clear();range_mode_=repair_mode_=0;view_->set_tool(0);build_parameters();});
        QString key=property("portraitHairRoots").toBool()?"hair.roots":"hair.color";auto zones=s["hairZones"].toArray();int zoneIndex=property("portraitHairZoneIndex").toInt();
        if(mode>0){
            auto addZone=[this,mode]{auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["hairZones"].toArray();const QString key="hair.zone_"+QUuid::createUuid().toString(QUuid::WithoutBraces);const auto f=people_[std::size_t(selected_)].face;QJsonObject zone{{"key",key},{"name",tr("分区 %1").arg(list.size()+1)},{"mode",mode==2?"gradient":mode==3?"highlights":"solid"},{"strandVersion",2},{"strandWidth",35},{"strandDensity",40},{"gradientAxis",QJsonArray{f.center().x(),std::max(0.,f.top()-.5*f.height()),f.center().x(),f.bottom()}},{"secondColor",QJsonArray{.15,.04,.025}}};list.append(zone);settings["hairZones"]=list;settings["hairColorVersion"]=2;auto p=settings["parameters"].toObject();p[key]=50;settings["parameters"]=p;if(mode!=2){QImage mask(preview_.size().scaled(768,768,Qt::KeepAspectRatio),QImage::Format_Grayscale8);mask.fill(0);auto masks=settings["masks"].toObject();masks[key]=editing::portrait_mask_png(mask);settings["masks"]=masks;}setProperty("portraitHairZoneIndex",list.size()-1);remember();build_parameters();if(mode==2){placement_key_="gradient:"+key;view_->set_tool(-1);update_overlay();}else activate_hair_brush(key);};
            auto* list=new QComboBox(parameter_host_);list->setObjectName("portraitHairZones");for(int i=0;i<zones.size();++i)if(zones[i].toObject()["mode"].toString()==(mode==2?"gradient":mode==3?"highlights":"solid"))list->addItem(zones[i].toObject()["name"].toString(),i);list->setCurrentIndex(list->findData(zoneIndex));if(list->currentIndex()<0 && list->count())list->setCurrentIndex(0);zoneIndex=list->currentData().toInt();parameter_layout_->addWidget(list);connect(list,&QComboBox::activated,this,[this,list](int){finish_brush_gesture();setProperty("portraitHairZoneIndex",list->currentData().toInt());build_parameters();});button(tr("添加区域"),"portraitHairAddZone",addZone);
            if(list->count()==0){parameter_layout_->addStretch();return;}key=zones[zoneIndex].toObject()["key"].toString();auto row=portrait_integer_row(parameter_host_,tr("程度"),"portraitHairZoneAmount",0,100,params[key].toInt(50));parameter_layout_->addWidget(row.host);connect(row.slider,&QSlider::valueChanged,this,[this,key](int v){auto& settings=people_[std::size_t(selected_)].settings;auto p=settings["parameters"].toObject();p[key]=v;settings["parameters"]=p;remember();});
            auto* menu=new QMenu(list);list->setContextMenuPolicy(Qt::CustomContextMenu);menu->addAction(tr("删除此区域"),this,[this,zoneIndex,key]{auto& settings=people_[std::size_t(selected_)].settings;auto z=settings["hairZones"].toArray();z.removeAt(zoneIndex);settings["hairZones"]=z;for(auto name:{"parameters","colors","masks"}){auto o=settings[name].toObject();o.remove(key);settings[name]=o;}placement_key_.clear();remember();build_parameters();});menu->addAction(tr("移到最上层"),this,[this,zoneIndex]{auto& settings=people_[std::size_t(selected_)].settings;auto z=settings["hairZones"].toArray();auto item=z.takeAt(zoneIndex);z.append(item);settings["hairZones"]=z;setProperty("portraitHairZoneIndex",z.size()-1);remember();build_parameters();});connect(list,&QWidget::customContextMenuRequested,this,[list,menu](QPoint p){menu->exec(list->mapToGlobal(p));});
            if(mode==2){parameter_layout_->addWidget(new QLabel(tr("起点颜色"),parameter_host_));color(key);parameter_layout_->addWidget(new QLabel(tr("终点颜色"),parameter_host_));color(key,true);auto transition=portrait_integer_row(parameter_host_,tr("过渡宽度"),"portraitHairGradientWidth",5,100,zones[zoneIndex].toObject()["gradientWidth"].toInt(100));fold(transition.host);connect(transition.slider,&QSlider::valueChanged,this,[this,zoneIndex](int v){auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["hairZones"].toArray();auto z=list[zoneIndex].toObject();z["gradientWidth"]=v;list[zoneIndex]=z;settings["hairZones"]=list;remember();});placement_key_="gradient:"+key;view_->set_tool(-1);update_overlay();}
            else{color(key);brush(key);if(range_mode_==0 || range_key_->currentData().toString()!=key)activate_hair_brush(key);}
            if(mode==3){auto density=portrait_integer_row(parameter_host_,tr("密度"),"portraitHairStrandDensity",0,100,zones[zoneIndex].toObject()["strandDensity"].toInt(40));fold(density.host);connect(density.slider,&QSlider::valueChanged,this,[this,zoneIndex](int v){auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["hairZones"].toArray();auto z=list[zoneIndex].toObject();z["strandDensity"]=v;list[zoneIndex]=z;settings["hairZones"]=list;remember();});auto width=portrait_integer_row(parameter_host_,tr("发束宽度"),"portraitHairStrandWidth",0,100,zones[zoneIndex].toObject()["strandWidth"].toInt(35));parameter_layout_->addWidget(width.host);connect(width.slider,&QSlider::valueChanged,this,[this,zoneIndex](int v){auto& settings=people_[std::size_t(selected_)].settings;auto list=settings["hairZones"].toArray();auto z=list[zoneIndex].toObject();z["strandWidth"]=v;list[zoneIndex]=z;settings["hairZones"]=list;remember();});}
        }else{color(key);add_parameter_row(parameter_layout_,key,tr("程度"));{auto* details=new QWidget(parameter_host_);auto* rows=new QVBoxLayout(details);rows->setContentsMargins(0,0,0,0);add_parameter_row(rows,"hair.preserveGloss",tr("保留光泽"));fold(details);}if(key=="hair.roots")add_parameter_row(parameter_layout_,"hair.rootWidth",tr("宽度"));button(key=="hair.roots"?tr("返回整体染发"):tr("发根补染"),"portraitHairRoots",[this]{setProperty("portraitHairRoots",!property("portraitHairRoots").toBool());build_parameters();});button(tr("修正发区"),"portraitHairDyeCorrect",[this,key]{activate_hair_brush(key);build_parameters();});if(range_mode_)brush(key);}
    }else{build_decals("hair");}
    parameter_layout_->addStretch();
}

void PortraitDialog::activate_body_lines(const QString& material){
    if(selected_<0)return;finish_brush_gesture();finish_gesture();auto& person=people_[std::size_t(selected_)];auto& settings=person.settings;
    const auto previousMaterial=settings["bodyMaterial"].toString();
    auto placements=settings["bodyLinePlacements"].toObject();if(settings.contains("bodyLinesQuad"))placements[previousMaterial]=settings["bodyLinesQuad"];
    settings["bodyLinePlacements"]=placements;
    if(previousMaterial!=material){
        settings.remove("bodyLinesQuad");if(placements.contains(material))settings["bodyLinesQuad"]=placements[material];
        auto masks=settings["masks"].toObject();auto storedMasks=settings["bodyLineMasks"].toObject();
        if(masks.contains("contour.lines"))storedMasks[previousMaterial]=masks["contour.lines"];else storedMasks.remove(previousMaterial);
        masks.remove("contour.lines");if(storedMasks.contains(material))masks["contour.lines"]=storedMasks[material];
        settings["bodyLineMasks"]=storedMasks;settings["masks"]=masks;
    }
    settings["bodyMaterial"]=material;
    if(material=="none"){set_parameter("contour.lines",0);placement_key_.clear();update_overlay();return;}
    if(!settings.contains("bodyLinesQuad")){
        QRectF body=person.body.isEmpty()?QRectF(.25,.3,.5,.6):person.body;const double cx=body.center().x();double y=material=="collarbone"?person.face.bottom()+person.face.height()*.22:body.top()+body.height()*.48;
        const double w=std::clamp(body.width()*.55,.1,.5),h=material=="collarbone"?w*.25:w*.9;y=std::clamp(y,.05,.95-h);
        settings["bodyLinesQuad"]=QJsonArray{QJsonArray{cx-w*.5,y},QJsonArray{cx+w*.5,y},QJsonArray{cx+w*.5,y+h},QJsonArray{cx-w*.5,y+h}};
    }
    setProperty("portraitBodyLineErase",false);range_mode_=repair_mode_=0;placement_key_="body:lines";painting_mesh_=editing::portrait_deformation(source_.size(),recipe());view_->set_tool(-1);
    if(settings["parameters"].toObject()["contour.lines"].toDouble()==0)set_parameter("contour.lines",30);else remember();update_overlay();
}
bool PortraitDialog::body_lines_event(QEvent* event){
    if(selected_<0)return false;const auto type=event->type();if(type!=QEvent::MouseButtonPress && type!=QEvent::MouseMove && type!=QEvent::MouseButtonRelease)return false;
    auto* mouse=static_cast<QMouseEvent*>(event);auto& settings=people_[std::size_t(selected_)].settings;const bool decal=placement_key_.startsWith("decal:");auto decals=settings["decals"].toArray();int index=-1;for(int i=0;i<decals.size();++i)if(decals[i].toObject()["id"].toString()==placement_key_.mid(6))index=i;auto a=decal?(index>=0?decals[index].toObject()["quad"].toArray():QJsonArray{}):settings["bodyLinesQuad"].toArray();if(a.size()!=4)return false;
    const auto scene=view_->mapToScene(mouse->position().toPoint());const auto q=source_position(scene);QPolygonF points;for(const auto& v:a){const auto xy=v.toArray();points<<QPointF(xy[0].toDouble(),xy[1].toDouble());}
    QPointF center;for(auto v:points)center+=v;center/=4;
    const auto top=(points[0]+points[1])*.5,rotation=top+(top-center)*.35;
    if(type==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton){
        accessory_handle_=-1;double best=12/std::max(.01,view_->transform().m11());
        if(decal || property("portraitBodyLineFit").toBool())for(int i=0;i<4;++i){const double d=QLineF(scene,repair_scene_position(points[i])).length();if(d<best){best=d;accessory_handle_=i;}}
        if(QLineF(scene,repair_scene_position(rotation)).length()<12/std::max(.01,view_->transform().m11()))accessory_handle_=5;
        if(accessory_handle_<0 && points.containsPoint(q,Qt::OddEvenFill))accessory_handle_=4;
        if(accessory_handle_<0)return false;placement_drawing_=true;placement_last_=q;gesture_=true;view_->viewport()->grabMouse();return true;
    }
    if(!placement_drawing_)return false;
    if(accessory_handle_<4)points[accessory_handle_]+=q-placement_last_;
    else if(accessory_handle_==4)for(auto& point:points)point+=q-placement_last_;
    else{const double aspect=double(source_.height())/source_.width();const auto prev=placement_last_-center,now=q-center;const double angle=std::atan2(now.y()*aspect,now.x())-std::atan2(prev.y()*aspect,prev.x());for(auto& point:points){auto d=point-center;point=center+QPointF(d.x()*std::cos(angle)-d.y()*aspect*std::sin(angle),(d.x()*std::sin(angle)+d.y()*aspect*std::cos(angle))/aspect);}}
    bool valid=true;double sign=0;for(int i=0;i<4;++i){const auto x=points[(i+1)%4]-points[i],y=points[(i+2)%4]-points[(i+1)%4];const double cross=x.x()*y.y()-x.y()*y.x();if(std::abs(cross)<.00001 || (i && cross*sign<=0))valid=false;sign=cross;if(points[i].x()<0 || points[i].x()>1 || points[i].y()<0 || points[i].y()>1)valid=false;}
    if(valid){QJsonArray quad;for(auto point:points)quad.append(QJsonArray{point.x(),point.y()});if(decal){auto item=decals[index].toObject();item["quad"]=quad;decals[index]=item;settings["decals"]=decals;}else settings["bodyLinesQuad"]=quad;placement_last_=q;schedule_preview();update_overlay();}
    if(type==QEvent::MouseButtonRelease){placement_drawing_=false;view_->viewport()->releaseMouse();finish_gesture();}
    return true;
}

void PortraitDialog::activate_body_manual(int tool){
    if(selected_<0)return;finish_brush_gesture();finish_gesture();placement_key_.clear();range_mode_=repair_mode_=0;liquify_tool_=-1;locate_->setChecked(false);
    auto& person=people_[std::size_t(selected_)];auto& settings=person.settings;
    const QRectF body=person.body.isEmpty()?QRectF(.25,.3,.5,.6):person.body;
    const double cx=std::clamp(body.center().x(),.1,.9),top=std::clamp(std::max(body.top()+body.height()*.2,person.face.bottom()+person.face.height()*.25),.1,.75),bottom=std::clamp(body.bottom()-.05,top+.1,.95);
    const QStringList names{"manualHeight","manualSlim","manualScale","manualDistort"};placement_key_="body:"+names[std::clamp(tool,0,3)];
    if(tool==0){if(!settings.contains("manualHeightRange"))settings["manualHeightRange"]=QJsonArray{QJsonArray{cx,top},QJsonArray{cx,bottom}};manual_height_points_=settings["manualHeightRange"].toArray();manual_height_handle_=-1;}
    if(tool==1){if(!settings.contains("manualSlimRegion"))settings["manualSlimRegion"]=QJsonArray{std::max(.02,cx-body.width()*.4),top,std::min(.8,body.width()*.8),bottom-top};const auto a=settings["manualSlimRegion"].toArray();manual_slim_box_={a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()};manual_slim_handle_=-1;}
    if(tool==2){if(!settings.contains("manualScaleRegion"))settings["manualScaleRegion"]=QJsonArray{cx,(top+bottom)*.5,.10,.24};manual_scale_region_=settings["manualScaleRegion"].toArray();manual_scale_handle_=-1;}
    if(tool==3){if(!settings.contains("manualDistortRegion"))settings["manualDistortRegion"]=QJsonArray{cx,(top+bottom)*.5,.22};manual_distort_handle_=-1;}
    painting_mesh_=tool==0?editing::portrait_deformation(source_.size(),recipe()):manual_slim_placement_mesh();view_->set_tool(-1);update_overlay();
    status_->setText(tool==0?tr("拖动上下两条线夹住拉伸区域；头部保持比例"):tool==1?tr("拖动范围框定位，再调整程度"):tool==2?tr("拖中心移动，内圈控制目标，外圈控制过渡"):tr("拖动中心和方向柄调整局部扭曲"));
}
void PortraitDialog::build_body_parameters(){
    details_->hide();if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->hide();if(auto* sides=findChild<QWidget*>("portraitSides"))sides->hide();
    if(auto* old=findChild<QTabBar*>("portraitTaskTabs"))old->hide();
    const int mode=property("portraitBodyMode").toInt();auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitBodyTabs");for(const auto& t:{tr("塑形"),tr("手动"),tr("线条")})tabs->addTab(t);tabs->setExpanding(true);tabs->setCurrentIndex(mode);parameter_layout_->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();finish_gesture();placement_key_.clear();range_mode_=repair_mode_=0;view_->set_tool(0);setProperty("portraitBodyMode",i);if(i==1)activate_body_manual(property("portraitBodyTool").toInt());build_parameters();update_overlay();});
    if(selected_<0){parameter_layout_->addWidget(new QLabel(tr("请先选择人物"),parameter_host_));return;}
    auto& person=people_[std::size_t(selected_)];
    if(mode==0){
        const QStringList titles{tr("组合"),tr("肩颈"),tr("躯干"),tr("手臂"),tr("腿部"),tr("胸胯")};
        const QList<QStringList> groups{{},{"body.shoulderWidth","body.shoulder","body.squareShoulder","body.trapezius","body.neck","body.neckLength"},{"body.waist","body.abdomen","body.belly","body.back"},{"body.arm","body.upperArm"},{"body.legWidth","body.thigh","body.calf","body.straightLeg","body.legLength","body.height"},{"body.chest","body.hipWidth"}};
        const int current=property("portraitBodyGroup").toInt();
        for(int i=0;i<titles.size();++i){auto* header=new QToolButton(parameter_host_);header->setObjectName(QString("portraitBodyGroup%1").arg(i));header->setText(titles[i]);header->setArrowType(current==i?Qt::DownArrow:Qt::RightArrow);header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);header->setMinimumHeight(38);parameter_layout_->addWidget(header);connect(header,&QToolButton::clicked,this,[this,i]{setProperty("portraitBodyGroup",property("portraitBodyGroup").toInt()==i?-1:i);build_parameters();});if(i!=current)continue;
            if(i==0){auto* natural=new QPushButton(tr("自然美体"),parameter_host_);natural->setObjectName("portraitBodyNatural");natural->setMinimumHeight(64);parameter_layout_->addWidget(natural);connect(natural,&QPushButton::clicked,this,[this]{preview_body_combination(property("portraitBodyCombinationAmount").isValid()?property("portraitBodyCombinationAmount").toDouble()*.01:.6);});auto row=portrait_integer_row(parameter_host_,tr("程度"),"portraitBodyCombinationAmount",0,100,property("portraitBodyCombinationAmount").isValid()?property("portraitBodyCombinationAmount").toInt():60);parameter_layout_->addWidget(row.host);connect(row.slider,&QSlider::valueChanged,this,[this](int v){setProperty("portraitBodyCombinationAmount",v);if(preset_active_)preview_body_combination(v*.01);});continue;}
            if(i!=2){auto* side=new QTabBar(parameter_host_);side->setObjectName("portraitBodySide");for(auto text:{tr("双侧"),tr("人物左侧"),tr("人物右侧")})side->addTab(text);side->setCurrentIndex(person.settings["linked"].toBool(true)?0:person.settings["side"].toInt()+1);parameter_layout_->addWidget(side);connect(side,&QTabBar::currentChanged,this,[this](int v){auto& s=people_[std::size_t(selected_)].settings;s["linked"]=v==0;s["side"]=std::max(0,v-1);build_parameters();});}
            for(const auto& key:groups[i])if(auto d=editing::portrait_parameter(key))add_parameter_row(parameter_layout_,key,key=="body.waist"?tr("瘦腰"):key=="body.belly"?tr("瘦肚子"):d->label);
            auto* correct=new QToolButton(parameter_host_);correct->setText(tr("修正位置"));correct->setObjectName("portraitBodyCorrect");parameter_layout_->addWidget(correct,0,Qt::AlignRight);auto* corrections=new QMenu(correct);correct->setMenu(corrections);correct->setPopupMode(QToolButton::InstantPopup);corrections->addAction(tr("肩、髋与肢体位置"),this,[this]{locate_->setChecked(!locate_->isChecked());view_->set_tool(-1);status_->setText(tr("拖动当前人物关键点修正位置"));update_overlay();});
            for(const auto& kind:(i==2?QStringList{"back","abdomen"}:i==5?QStringList{"chest"}:QStringList{})){auto* action=corrections->addAction(kind=="back"?tr("上背轮廓"):kind=="abdomen"?tr("腹部前缘"):tr("胸部轮廓"));action->setObjectName("portraitBodyCorrect_"+kind);connect(action,&QAction::triggered,this,[this,kind]{finish_brush_gesture();finish_gesture();range_mode_=repair_mode_=0;locate_->setChecked(false);placement_key_="body:"+kind;back_placement_={};chest_placement_points_={};painting_mesh_=editing::portrait_deformation(source_.size(),recipe());view_->set_tool(-1);status_->setText(kind=="abdomen"?tr("点选下腹前缘；抓手退出"):tr("依次点选可见轮廓的上端、调整处、下端；抓手退出"));update_overlay();});}
        }
    }else if(mode==1){
        const int tool=std::clamp(property("portraitBodyTool").toInt(),0,3);auto* types=new QTabBar(parameter_host_);types->setObjectName("portraitBodyManualTabs");for(auto t:{tr("增高"),tr("瘦身"),tr("缩放"),tr("扭曲")})types->addTab(t);types->setCurrentIndex(tool);types->setExpanding(true);parameter_layout_->addWidget(types);connect(types,&QTabBar::currentChanged,this,[this](int i){setProperty("portraitBodyTool",i);activate_body_manual(i);build_parameters();});
        const QStringList keys{"body.manualHeight","body.manualSlim","body.manualScale","body.manualRotate"};add_parameter_row(parameter_layout_,keys[tool],tool==0?tr("拉伸程度"):tool==3?tr("角度"):tr("程度"));
        if(tool==0){const auto range=person.settings["manualHeightRange"].toArray();int angle=0;if(range.size()==2){const auto a=range[0].toArray(),b=range[1].toArray();angle=int(std::round(std::atan2((b[0].toDouble()-a[0].toDouble())*source_.width(),(b[1].toDouble()-a[1].toDouble())*source_.height())*180/3.141592653589793));}auto row=portrait_integer_row(parameter_host_,tr("角度"),"portraitBodyHeightAngle",-45,45,angle);parameter_layout_->addWidget(row.host);connect(row.slider,&QSlider::valueChanged,this,[this](int degrees){auto& s=people_[std::size_t(selected_)].settings;const auto a=s["manualHeightRange"].toArray();if(a.size()!=2)return;const auto first=a[0].toArray(),last=a[1].toArray();const double cx=(first[0].toDouble()+last[0].toDouble())*.5,cy=(first[1].toDouble()+last[1].toDouble())*.5,dy=(last[1].toDouble()-first[1].toDouble())*.5,dx=std::tan(degrees*3.141592653589793/180.)*dy*source_.height()/source_.width();const auto previous=s["manualHeightRange"];if(cx-std::abs(dx)<0 || cx+std::abs(dx)>1)return;s["manualHeightRange"]=QJsonArray{QJsonArray{cx-dx,cy-dy},QJsonArray{cx+dx,cy+dy}};try{editing::portrait_deformation(source_.size(),recipe());}catch(const std::exception& e){s["manualHeightRange"]=previous;status_->setText(QString::fromUtf8(e.what()));return;}manual_height_points_=s["manualHeightRange"].toArray();remember();update_overlay();});auto* protect=new QCheckBox(tr("头部保护"),parameter_host_);protect->setObjectName("portraitBodyHeadProtect");protect->setChecked(person.settings["manualHeightProtectHead"].toBool(true));parameter_layout_->addWidget(protect);connect(protect,&QCheckBox::toggled,this,[this,protect](bool value){auto& settings=people_[std::size_t(selected_)].settings;settings["manualHeightProtectHead"]=value;try{editing::portrait_deformation(source_.size(),recipe());remember();}catch(const std::exception& e){settings["manualHeightProtectHead"]=!value;QSignalBlocker block(protect);protect->setChecked(!value);status_->setText(QString::fromUtf8(e.what()));}});}
        else{auto* protect=new QCheckBox(tr("背景保护"),parameter_host_);protect->setChecked(person.settings["protectBackground"].toBool(true));parameter_layout_->addWidget(protect);connect(protect,&QCheckBox::toggled,this,[this](bool v){people_[std::size_t(selected_)].settings["protectBackground"]=v;remember();});}
        if(tool==2){auto* aspect=new QCheckBox(tr("保持比例"),parameter_host_);aspect->setObjectName("portraitBodyScaleAspect");aspect->setChecked(person.settings["manualScaleKeepAspect"].toBool(true));aspect->setToolTip(tr("关闭后只调整横向宽度"));parameter_layout_->addWidget(aspect);connect(aspect,&QCheckBox::toggled,this,[this](bool v){people_[std::size_t(selected_)].settings["manualScaleKeepAspect"]=v;remember();});}
        if(tool==3)add_parameter_row(parameter_layout_,"body.manualShear",tr("剪切"));
    }else{
        const QStringList materials{"none","collarbone","abdomen","waist"},labels{tr("原图"),tr("锁骨"),tr("腹部"),tr("腰侧")};auto* cards=new QWidget(parameter_host_);auto* row=new QHBoxLayout(cards);row->setContentsMargins(0,8,0,8);
        for(int i=0;i<materials.size();++i){auto* b=portrait_style_card(cards,labels[i]);const QPixmap sheet(":/huelith/portrait/body-lines-approved.png");b->setIcon(QIcon(sheet.copy(QRect(1175+i*102,329,82,96))));b->setIconSize(QSize(72,83));b->setMinimumHeight(110);b->setObjectName("portraitBodyMaterial_"+materials[i]);b->setCheckable(true);b->setChecked(person.settings["bodyMaterial"].toString()==materials[i]);b->setMinimumHeight(64);row->addWidget(b);connect(b,&QToolButton::clicked,this,[this,material=materials[i]]{activate_body_lines(material);build_parameters();});}parameter_layout_->addWidget(cards);
        add_parameter_row(parameter_layout_,"contour.lines",tr("程度"));
        if(property("portraitBodyLineErase").toBool()){auto brush=portrait_integer_row(parameter_host_,tr("大小"),"portraitBodyLineBrushSize",1,500,brush_size_->value());parameter_layout_->addWidget(brush.host);connect(brush.slider,&QSlider::valueChanged,brush_size_,&QSlider::setValue);}
        auto* hint=new QLabel(tr("拖中心移动，拖角点贴合，拖上方圆柄旋转；线条仅作用于皮肤"),parameter_host_);hint->setWordWrap(true);parameter_layout_->addWidget(hint);
    }parameter_layout_->addStretch();
}

void PortraitDialog::build_oil_parameters(){
    const int mode=property("portraitOilMode").toInt();
    details_->hide();if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->hide();
    if(auto* old=findChild<QTabBar*>("portraitTaskTabs"))old->hide();
    auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitOilTabs");tabs->addTab(tr("自动"));tabs->addTab(tr("手动"));tabs->setExpanding(true);tabs->setCurrentIndex(mode);parameter_layout_->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();finish_gesture();setProperty("portraitOilMode",i);range_mode_=repair_mode_=0;range_visible_->setChecked(false);view_->set_tool(0);if(i==1)activate_skin_brush(1);build_parameters();});
    if(mode==0){
        auto* correct=new QToolButton(parameter_host_);correct->setText(tr("修正范围"));correct->setObjectName("portraitOilCorrect");parameter_layout_->addWidget(correct,0,Qt::AlignRight);connect(correct,&QToolButton::clicked,tabs,[tabs]{tabs->setCurrentIndex(1);});
        add_parameter_row(parameter_layout_,"skin.oil",tr("程度"));add_parameter_row(parameter_layout_,"skin.oilGloss",tr("保留光泽"));
    }else{
        auto* brushes=new QWidget(parameter_host_);auto* row=new QHBoxLayout(brushes);row->setContentsMargins(0,8,0,8);auto* group=new QButtonGroup(brushes);
        for(int m:{1,2}){auto* b=new QPushButton(m==1?tr("涂抹"):tr("擦除"),brushes);b->setObjectName(m==1?"portraitOilPaint":"portraitOilErase");b->setCheckable(true);b->setChecked(range_mode_==m || (!range_mode_ && m==1));b->setMinimumHeight(36);b->setStyleSheet("QPushButton:checked{background:#2f80ed;color:white;}");group->addButton(b);row->addWidget(b);connect(b,&QPushButton::clicked,this,[this,m]{activate_skin_brush(m);});}parameter_layout_->addWidget(brushes);
        auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitOilBrushSize",1,500,brush_size_->value());parameter_layout_->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,brush_size_,&QSlider::setValue);connect(brush_size_,&QSlider::valueChanged,size.slider,&QSlider::setValue);
        add_parameter_row(parameter_layout_,"skin.oil",tr("程度"));
        auto* details=new QToolButton(parameter_host_);details->setText(tr("保留光泽与羽化"));details->setObjectName("portraitOilDetails");details->setCheckable(true);details->setArrowType(Qt::RightArrow);details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(details);
        auto* content=new QWidget(parameter_host_);auto* layout=new QVBoxLayout(content);layout->setContentsMargins(0,0,0,0);content->hide();parameter_layout_->addWidget(content);connect(details,&QToolButton::toggled,this,[details,content](bool open){content->setVisible(open);details->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
        add_parameter_row(layout,"skin.oilGloss",tr("保留光泽"));auto feather=portrait_integer_row(content,tr("羽化"),"portraitOilFeather",0,100,brush_feather_->value());layout->addWidget(feather.host);connect(feather.slider,&QSlider::valueChanged,brush_feather_,&QSlider::setValue);
    }parameter_layout_->addStretch();
}

void PortraitDialog::build_skin_parameters(){
    const int mode=navigation_->currentRow()==6?2:std::min(1,property("portraitSkinMode").toInt());
    if(auto* tabs=findChild<QTabBar*>("portraitTaskTabs"))tabs->hide();
    details_->hide();if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->hide();
    auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitSkinTabs");for(const auto& name:{tr("自动"),tr("手动"),tr("肤色")})tabs->addTab(name);tabs->setExpanding(true);tabs->setCurrentIndex(mode);parameter_layout_->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();finish_gesture();setProperty("portraitSkinMode",i);range_mode_=repair_mode_=0;range_visible_->setChecked(false);view_->set_tool(0);if(navigation_->currentRow()!=(i==2?6:5))navigation_->setCurrentRow(i==2?6:5);else build_parameters();if(i==1)activate_skin_brush(1);});
    const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{};
    if(mode==2){build_skin_colors();parameter_layout_->addStretch();return;}
    if(mode==0){
        add_parameter_row(parameter_layout_,"skin.smooth",tr("面部"));add_parameter_row(parameter_layout_,"skin.bodySmooth",tr("身体"));add_parameter_row(parameter_layout_,"skin.retention",tr("纹理保留"),100);
        auto* styles=new QWidget(parameter_host_);auto* row=new QHBoxLayout(styles);row->setContentsMargins(0,6,0,6);
        const QStringList keys{"classic","texture","native"},labels{tr("经典"),tr("质感"),tr("原生")};
        for(int i=0;i<3;++i){auto* b=new QPushButton(labels[i],styles);b->setObjectName("portraitSmoothStyle_"+keys[i]);b->setCheckable(true);b->setChecked(settings["smoothStyle"].toString("classic")==keys[i]);row->addWidget(b);connect(b,&QPushButton::clicked,this,[this,key=keys[i]]{if(selected_<0)return;finish_gesture();auto& s=people_[std::size_t(selected_)].settings;s["smoothStyle"]=key;s["smoothVersion"]=2;remember();build_parameters();});}parameter_layout_->addWidget(styles);
        auto* correct=new QToolButton(parameter_host_);correct->setText(tr("修正范围"));correct->setObjectName("portraitSmoothCorrect");parameter_layout_->addWidget(correct);connect(correct,&QToolButton::clicked,tabs,[tabs]{tabs->setCurrentIndex(1);});
    }else{
        auto* brushes=new QWidget(parameter_host_);auto* row=new QHBoxLayout(brushes);row->setContentsMargins(0,8,0,8);auto* group=new QButtonGroup(brushes);
        for(int m:{1,2}){auto* b=new QPushButton(m==1?tr("涂抹"):tr("擦除"),brushes);b->setObjectName(m==1?"portraitSmoothPaint":"portraitSmoothErase");b->setCheckable(true);b->setChecked(range_mode_==m || (range_mode_==0 && m==1));b->setStyleSheet("QPushButton:checked{background:#2f80ed;color:white;}");b->setIcon(portrait_task_icon(m==1?12:10));b->setMinimumHeight(36);group->addButton(b);row->addWidget(b);connect(b,&QPushButton::clicked,this,[this,m]{activate_skin_brush(m);});}parameter_layout_->addWidget(brushes);
        auto size=portrait_integer_row(parameter_host_,tr("大小"),"portraitSmoothBrushSize",1,500,brush_size_->value());size.host->setMinimumHeight(58);parameter_layout_->addWidget(size.host);connect(size.slider,&QSlider::valueChanged,brush_size_,&QSlider::setValue);
        const QString target=property("portraitSmoothTarget").toInt()==1?"skin.bodySmooth":"skin.smooth";add_parameter_row(parameter_layout_,target,tr("程度"));
        auto* details=new QToolButton(parameter_host_);details->setText(tr("笔刷细节"));details->setCheckable(true);details->setArrowType(Qt::RightArrow);details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);parameter_layout_->addWidget(details);auto* content=new QWidget(parameter_host_);auto* layout=new QVBoxLayout(content);layout->setContentsMargins(0,0,0,0);content->hide();parameter_layout_->addWidget(content);connect(details,&QToolButton::toggled,this,[details,content](bool open){content->setVisible(open);details->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
        auto* targets=new QTabBar(content);targets->setObjectName("portraitSmoothTarget");targets->addTab(tr("面部"));targets->addTab(tr("身体"));targets->setCurrentIndex(target=="skin.bodySmooth"?1:0);layout->addWidget(targets);connect(targets,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();setProperty("portraitSmoothTarget",i);activate_skin_brush(1);build_parameters();});
        auto* restore=new QPushButton(tr("恢复此部位自动范围"),content);restore->setObjectName("portraitSmoothRestoreRange");layout->addWidget(restore);connect(restore,&QPushButton::clicked,this,[this,target]{if(selected_<0)return;finish_brush_gesture();auto& s=people_[std::size_t(selected_)].settings;auto masks=s["masks"].toObject();masks.remove(target);s["masks"]=masks;remember();update_range_overlay();});
        auto hardness=portrait_integer_row(content,tr("硬度"),"portraitSmoothHardness",0,100,100-brush_feather_->value());hardness.host->setMinimumHeight(58);layout->addWidget(hardness.host);connect(hardness.slider,&QSlider::valueChanged,this,[this](int value){brush_feather_->setValue(100-value);});
    }parameter_layout_->addStretch();
}

void PortraitDialog::set_volume_amount(int value){
    if(selected_<0)return;
    const bool lift=property("portraitVolumeMode").toInt()==1;const int part=property(lift?"portraitLiftPart":"portraitVolumePart").toInt();
    auto& settings=people_[std::size_t(selected_)].settings;
    const QString mixKey=lift?"liftMix":"volumeMix";
    if(part==0){
        auto mix=settings[mixKey].toObject();
        if(mix.isEmpty()){
            setProperty("portraitVolumePendingAmount",value);apply_volume_combination(lift);return;
        }
        auto params=settings["parameters"].toObject(),base=mix["base"].toObject(),target=mix["target"].toObject(),last=mix["last"].toObject();
        for(auto it=target.begin();it!=target.end();++it){
            // Retain direct edits made since this combination was last applied.
            const double delta=params[it.key()].toDouble()-last[it.key()].toDouble(base[it.key()].toDouble());
            base[it.key()]=base[it.key()].toDouble()+delta;
            params[it.key()]=std::clamp(base[it.key()].toDouble()+it.value().toDouble()*value*.01,-100.,100.);
            last[it.key()]=params[it.key()];
        }
        mix["base"]=base;mix["last"]=last;mix["amount"]=value;settings[mixKey]=mix;settings["parameters"]=params;remember();return;
    }
    const bool held=gesture_;gesture_=true;
    const auto keys=volume_part_keys(lift,part);
    // The degree drives actual regional parameters, not an extra effect layer.
    // Bilateral means both anatomical sides here; detailed edits remain shared.
    const auto oldLink=settings["linked"],oldSide=settings["side"];
    settings["linked"]=true;settings["side"]=0;
    for(int i=0;i<keys.size();++i)set_parameter(keys[i],value*(i==0?1.:.65));
    settings["linked"]=oldLink;settings["side"]=oldSide;
    gesture_=held;if(!held)remember();
}
void PortraitDialog::build_volume_parameters(){
    const bool lift=property("portraitVolumeMode").toInt()==1;
    const QString partProperty=lift?"portraitLiftPart":"portraitVolumePart";
    const int part=property(partProperty.toUtf8().constData()).toInt();
    auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitVolumeTabs");tabs->addTab(tr("丰盈"));tabs->addTab(tr("提拉"));tabs->setExpanding(true);tabs->setCurrentIndex(lift?1:0);parameter_layout_->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){finish_brush_gesture();finish_gesture();setProperty("portraitVolumeMode",i);range_mode_=0;range_visible_->setChecked(false);build_parameters();});
    const QStringList labels=lift?QStringList{tr("自然提拉"),tr("轮廓"),tr("额头"),tr("眼周"),tr("面中"),tr("口周")}:
        QStringList{tr("自然丰盈"),tr("额头"),tr("太阳穴"),tr("泪沟"),tr("苹果肌"),tr("面颊"),tr("口周")};
    auto* cards=new QWidget(parameter_host_);auto* grid=new QGridLayout(cards);grid->setContentsMargins(0,8,0,8);grid->setSpacing(4);
    for(int i=0;i<labels.size();++i){
        auto* card=portrait_style_card(cards,labels[i]);card->setMinimumSize(70,82);card->setObjectName(QString("portraitVolumeCard%1").arg(i));card->setChecked(i==part);
        // Use the approved face thumbnails, including their anatomical highlights.
        const QPixmap sheet(lift?":/huelith/portrait/lift-approved.png":":/huelith/portrait/volume-approved.png");
        const QRect crop=lift?QRect(1174+(i%3)*139,244+(i/3)*162,110,110):QRect(1175+(i%4)*103,241+(i/4)*133,78,78);
        card->setIcon(QIcon(sheet.copy(crop)));card->setIconSize(lift?QSize(88,88):QSize(72,72));card->setMinimumHeight(lift?118:102);
        grid->addWidget(card,i/(lift?3:4),i%(lift?3:4));
        connect(card,&QToolButton::clicked,this,[this,partProperty,i]{finish_brush_gesture();finish_gesture();setProperty(partProperty.toUtf8().constData(),i);range_mode_=0;range_visible_->setChecked(false);build_parameters();});
    }parameter_layout_->addWidget(cards);
    const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{},params=settings["parameters"].toObject();
    const auto keys=volume_part_keys(lift,part);const QString first=keys.value(0);
    const auto descriptors=editing::portrait_parameters(0);const auto descriptor=std::find_if(descriptors.begin(),descriptors.end(),[&](const auto& d){return d.key==first;});
    const QString suffix=descriptor!=descriptors.end() && descriptor->bilateral?".left":"";
    const int amount=part==0?settings[lift?"liftMix":"volumeMix"].toObject()["amount"].toInt():int(params[first+suffix].toDouble());
    auto* heading=new QWidget(parameter_host_);auto* line=new QHBoxLayout(heading);line->setContentsMargins(0,8,0,0);line->addWidget(new QLabel(labels.value(part),heading));line->addStretch();
    if(part!=0){auto* correct=new QToolButton(heading);correct->setText(tr("修正范围"));correct->setObjectName("portraitVolumeCorrect");line->addWidget(correct);connect(correct,&QToolButton::clicked,this,[this,first,keys]{finish_brush_gesture();if(selected_>=0){auto& s=people_[std::size_t(selected_)].settings;auto links=s["volumeRangeLinks"].toObject();for(const auto& key:keys)links[key]=first;s["volumeRangeLinks"]=links;}refresh_ranges();const int i=range_key_->findData(first);if(i>=0)range_key_->setCurrentIndex(i);range_visible_->setChecked(true);if(auto* scope=findChild<QToolButton*>("portraitScopeHeader")){scope->show();scope->setChecked(true);}inspector_tabs_->setCurrentIndex(1);});}parameter_layout_->addWidget(heading);
    auto degree=portrait_integer_row(parameter_host_,tr("程度"),"portraitVolumeAmount",0,100,amount);degree.host->setMinimumHeight(52);degree.host->setEnabled(selected_>=0);bind_slider(degree.slider);parameter_layout_->addWidget(degree.host);
    connect(degree.slider,&QSlider::valueChanged,this,[this](int value){set_volume_amount(value);});
    auto* details=new QToolButton(parameter_host_);details->setText(tr("组合明细"));details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);details->setCheckable(true);details->setArrowType(Qt::RightArrow);details->setObjectName("portraitVolumeDetails");parameter_layout_->addWidget(details);
    auto* content=new QWidget(parameter_host_);auto* rows=new QVBoxLayout(content);rows->setContentsMargins(0,0,0,0);rows->setSpacing(0);content->hide();parameter_layout_->addWidget(content);
    for(const auto& d:descriptors)if(keys.contains(d.key)){
        auto row=portrait_integer_row(content,d.label,"portraitVolumeDetail_"+d.key,int(d.minimum),int(d.maximum),int(params[d.key+(d.bilateral?".left":"")].toDouble()));row.host->setMinimumHeight(52);bind_slider(row.slider);rows->addWidget(row.host);
        connect(row.slider,&QSlider::valueChanged,this,[this,key=d.key](int value){set_parameter(key,value);});
    }
    connect(details,&QToolButton::toggled,this,[content,details](bool open){content->setVisible(open);details->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    auto* note=new QLabel(tr("明暗修饰保留纹理与原光照；轮廓调整只作用于选中部位。"),parameter_host_);note->setWordWrap(true);note->setStyleSheet("color:#a4adb5;font-size:11px;");parameter_layout_->addWidget(note);parameter_layout_->addStretch();
}

void PortraitDialog::apply_volume_combination(bool lift){
    if(selected_<0 || volume_analysis_busy_ || busy_ || repair_analysis_busy_)return;
    finish_gesture();finish_brush_gesture();const auto person=editing::portrait_person_json(people_[std::size_t(selected_)]);
    if(person["unmatched"].toBool() || person["matchAmbiguous"].toBool())return;
    combination_lift_=lift;volume_person_before_=person;volume_cancelled_=false;volume_analysis_busy_=true;
    const auto source=linear_preview_;volume_analysis_=std::async(std::launch::async,[this,source,person,lift]{return lift?editing::portrait_lift_combination(source,person,&volume_cancelled_):editing::portrait_volume_combination(source,person,&volume_cancelled_);});
    accept_->setEnabled(false);detect_->setEnabled(false);cancel_compute_->show();cancel_compute_->setEnabled(true);cancel_compute_->setToolTip(tr("取消组合分析，保留原设置"));status_->setText(lift?tr("正在分析提拉区域…"):tr("正在分析丰盈区域…"));if(auto* button=findChild<QPushButton*>("portraitVolumeCombination"))button->setEnabled(false);
}
void PortraitDialog::initialize_head_outline(){
    if(selected_<0)return;auto& person=people_[std::size_t(selected_)];if(person.landmarks.size()<66)return;if(!person.settings["headManual"].toObject().isEmpty())return;
    auto record=editing::portrait_person_json(person);auto settings=person.settings;settings["headManualEnabled"]=false;record["settings"]=settings;
    const auto mask=editing::portrait_head_region(record);QRectF bounds;QPointF center=person.face.center();
    if(!mask.isNull()){QRect pixels;for(int y=0;y<mask.height();++y)for(int x=0;x<mask.width();++x)if(mask.constScanLine(y)[x]>127)pixels|=QRect(x,y,1,1);bounds={double(pixels.x())/mask.width(),double(pixels.y())/mask.height(),double(pixels.width())/mask.width(),double(pixels.height())/mask.height()};center=bounds.center();}
    if(bounds.isEmpty())bounds={person.face.x()-.12*person.face.width(),person.face.y()-.35*person.face.height(),person.face.width()*1.24,person.face.height()*1.4};
    center=bounds.center();QJsonArray outline;
    for(int i=0;i<10;++i){const double angle=-1.57079632679+i*6.28318530718/10;const QPointF delta(std::cos(angle)*bounds.width()*.5,std::sin(angle)*bounds.height()*.5);double extent=1;
        if(!mask.isNull()){extent=0;for(double t=.1;t<1.6;t+=.01){const auto q=center+delta*t;const int x=int(q.x()*mask.width()),y=int(q.y()*mask.height());if(x>=0 && y>=0 && x<mask.width() && y<mask.height() && mask.constScanLine(y)[x]>127)extent=t;}extent=std::max(.7,extent)+.05;}
        const auto q=center+delta*extent;outline.append(QJsonArray{std::clamp(q.x(),0.,1.),std::clamp(q.y(),0.,1.)});}
    const auto chin=person.landmarks.size()>8?person.landmarks[8].toArray():QJsonArray{center.x(),bounds.bottom()};
    person.settings["headManual"]=QJsonObject{{"outline",outline},{"anchor",QJsonArray{chin[0].toDouble(),std::min(1.,chin[1].toDouble()+person.face.width()*source_.width()/source_.height()*.03)}},{"transition",.65}};
}
void PortraitDialog::build_head_parameters(){
    if(selected_>=0 && people_[std::size_t(selected_)].landmarks.size()<66){panel_description_->setText(tr("未找到完整人脸定位；可使用瘦脸瘦身中的手动液化。"));parameter_layout_->addStretch();return;}
    if(auto* sides=findChild<QWidget*>("portraitSides"))sides->hide();
    const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{};const bool manual=settings["headManualEnabled"].toBool();
    auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitHeadTabs");tabs->setExpanding(true);tabs->addTab(tr("自动"));tabs->addTab(tr("手动"));tabs->setCurrentIndex(manual?1:0);parameter_layout_->addWidget(tabs);tabs->setEnabled(selected_>=0);
    connect(tabs,&QTabBar::currentChanged,this,[this](int mode){if(selected_<0 || people_[std::size_t(selected_)].landmarks.size()<66)return;finish_brush_gesture();finish_gesture();if(mode)initialize_head_outline();people_[std::size_t(selected_)].settings["headManualEnabled"]=mode==1;remember();build_parameters();});
    auto* host=new QWidget(parameter_host_);auto* column=new QVBoxLayout(host);column->setContentsMargins(0,8,0,8);column->setSpacing(2);auto* row=new QHBoxLayout;
    row->addWidget(new QLabel(tr("程度"),host));row->addStretch();auto* spin=new QDoubleSpinBox(host);spin->setObjectName("portraitParam_head.size");spin->setRange(-100,100);spin->setDecimals(0);spin->setKeyboardTracking(false);spin->setButtonSymbols(QAbstractSpinBox::NoButtons);spin->setFixedSize(72,26);spin->setValue(-settings["parameters"].toObject()["head.size"].toDouble());row->addWidget(spin);column->addLayout(row);
    auto* slider=new ColorBandSlider(-1,0,host);slider->setObjectName("portraitSlider_head.size");slider->setRange(-100,100);slider->setFixedHeight(24);slider->set_colors(QColor("#69727c"),QColor("#69727c"));slider->setValue(int(spin->value()));bind_slider(slider);column->addWidget(slider);parameter_layout_->addWidget(host);host->setEnabled(selected_>=0);
    const auto hint=tr("正值缩小整个头部，包含头发和五官；负值放大。与面部重塑共用数值，双击归零。");slider->setToolTip(hint);spin->setToolTip(hint);
    connect(slider,&QSlider::valueChanged,spin,[spin](int value){spin->setValue(value);});connect(spin,&QDoubleSpinBox::valueChanged,this,[this,slider](double value){const QSignalBlocker block(slider);slider->setValue(int(value));set_parameter("head.size",-value);});
    auto* protect=new QCheckBox(tr("背景保护"),parameter_host_);protect->setObjectName("portraitHeadBackground");protect->setChecked(settings["protectBackground"].toBool(true));protect->setEnabled(selected_>=0);parameter_layout_->addWidget(protect);connect(protect,&QCheckBox::toggled,this,[this](bool checked){protect_background_->setChecked(checked);});
    if(!manual){auto* correct=new QPushButton(tr("修正头部范围"),parameter_host_);correct->setObjectName("portraitHeadCorrect");correct->setStyleSheet("text-align:left;color:#409cff;border:0;padding:8px 0;");correct->setEnabled(selected_>=0);parameter_layout_->addWidget(correct);connect(correct,&QPushButton::clicked,tabs,[tabs]{tabs->setCurrentIndex(1);});placement_key_.clear();view_->set_tool(0);}
    else{placement_key_="head:outline";view_->set_tool(-1);status_->setText(tr("拖动轮廓包含头发；外圈方点调整过渡，十字修正颈部锚点"));}
    panel_description_->clear();parameter_layout_->addStretch();update_overlay();
}
bool PortraitDialog::head_range_event(QEvent* event){
    if(selected_<0)return false;const auto type=event->type();
    if(type!=QEvent::MouseButtonPress && type!=QEvent::MouseMove && type!=QEvent::MouseButtonRelease)return false;
    auto* mouse=static_cast<QMouseEvent*>(event);auto& person=people_[std::size_t(selected_)];auto manual=person.settings["headManual"].toObject();auto points=manual["outline"].toArray();if(points.size()!=10)return false;
    const auto scene=view_->mapToScene(mouse->position().toPoint());QPointF center;for(const auto& v:points){const auto a=v.toArray();center+=QPointF(a[0].toDouble(),a[1].toDouble());}center/=points.size();
    const auto outer=[&]{const auto a=points[2].toArray();const QPointF q(a[0].toDouble(),a[1].toDouble()),delta=q-center;return q+delta*(person.face.width()*source_.width()*manual["transition"].toDouble(.65)/std::max(1.,std::hypot(delta.x()*source_.width(),delta.y()*source_.height())));};
    if(type==QEvent::MouseButtonPress){if(mouse->button()!=Qt::LeftButton)return false;double nearest=12/view_->transform().m11();head_handle_=-1;
        for(int i=0;i<12;++i){const auto a=i<10?points[i].toArray():manual["anchor"].toArray();const auto q=i==11?outer():QPointF(a[0].toDouble(),a[1].toDouble());const double d=QLineF(repair_scene_position(q),scene).length();if(d<nearest){head_handle_=i;nearest=d;}}
        if(head_handle_<0)return false;finish_gesture();head_before_=person.settings;gesture_=true;view_->viewport()->grabMouse();return true;
    }
    if(head_handle_<0)return false;
    // The transition handle may be in the canvas margin outside the photo.
    // Preserve that coordinate instead of snapping it to the image border.
    auto q=QPointF(scene.x()/preview_.width(),scene.y()/preview_.height());
    if(head_handle_!=11 || (q.x()>=0 && q.x()<=1 && q.y()>=0 && q.y()<=1))q=source_position(scene);
    if(head_handle_<10){points[head_handle_]=QJsonArray{q.x(),q.y()};manual["outline"]=points;}
    else if(head_handle_==10){const auto chin=person.landmarks[8].toArray();q.setY(std::max(q.y(),chin[1].toDouble()));manual["anchor"]=QJsonArray{q.x(),q.y()};}
    else{const auto a=points[2].toArray();const QPointF delta=q-QPointF(a[0].toDouble(),a[1].toDouble());manual["transition"]=std::clamp(std::hypot(delta.x()*source_.width(),delta.y()*source_.height())/(person.face.width()*source_.width()),.65,1.5);}
    person.settings["headManual"]=manual;person.settings["headGeometryVersion"]=2;
    if(type==QEvent::MouseButtonRelease){head_handle_=-1;view_->viewport()->releaseMouse();gesture_=false;
        try{QPolygonF polygon;for(const auto& v:points){const auto a=v.toArray();polygon<<QPointF(a[0].toDouble(),a[1].toDouble());}for(int i:{27,30,36,45,48,54}){const auto a=person.landmarks[i].toArray();if(!polygon.containsPoint({a[0].toDouble(),a[1].toDouble()},Qt::OddEvenFill))throw editing::EditError("头部轮廓需包含完整五官，请向外拖动范围");}
            editing::portrait_deformation(linear_preview_.size(),recipe());remember();status_->setText(tr("头部范围已更新"));
        }catch(const std::exception& e){person.settings=head_before_;status_->setText(QString::fromUtf8(e.what()));schedule_preview();}
    }else schedule_preview();update_overlay();return true;
}
void PortraitDialog::build_parameters() {
    if(auto* bar=view_->viewport()->findChild<QWidget*>("portraitBlemishActions")){bar->hide();bar->deleteLater();bar->setObjectName({});}

    // Async analysis may complete while a slider owns the pointer. Keep that
    // control alive until the gesture releases; deleting it here loses input.
    if(gesture_ || painting_ || repair_drawing_ || repair_source_dragging_ || liquify_drawing_){parameters_rebuild_pending_=true;return;}
    parameters_rebuild_pending_=false;
    // Headers can be nested layouts, not just widgets. Their child widgets still
    // belong to parameter_host_; deleting only the layout leaves them visible.
    const auto retire=[](auto&& self,QLayoutItem* item)->void {
        if(auto* widget=item->widget()){
            widget->hide();widget->setObjectName({});
            for(auto* child:widget->findChildren<QObject*>())child->setObjectName({});
            widget->deleteLater();
        }else if(auto* layout=item->layout()){
            while(auto* child=layout->takeAt(0))self(self,child);
        }
        delete item;
    };
    while(parameter_layout_->count()>1)retire(retire,parameter_layout_->takeAt(1));
    const int panel=navigation_->currentRow();auto controls=editing::portrait_parameters(panel);
    const int workspace_task=property("portraitWorkspaceTask").toInt();
    // Several navigation entries share the same persisted algorithm panel. Filter
    // the displayed controls, never duplicate or clear the underlying settings.
    if(panel==0){const QStringList volume_keys{"face.foreheadFill","face.templeFill","face.tearFill","face.malarShape","face.malarTone","face.volume","face.cheekTone","face.contourLift","face.foreheadLift","face.orbitalLift","face.orbitalTone","face.midfaceLift","face.perioralLift","face.perioralTone"};
        std::erase_if(controls,[&](const auto& p){return volume_keys.contains(p.key)!=(workspace_task==0);});}
    if(workspace_task==3){
        // Display order is independent of the persisted algorithm identifiers.
        const QStringList order=panel==0?QStringList{"face.width","face.small","face.vShape","face.cheek","face.jawAngle","face.smoothContour","face.chin","face.chinWidth","face.doubleChin","face.jaw"}:
            panel==1?QStringList{"feature.eyeSize","feature.eyeAperture","feature.eyeLength","feature.eyeSpace","feature.eyeHeight","feature.eyeTilt","feature.eyeInnerCorner","feature.eyeOuterCorner","feature.eyeOuterLift","feature.eyeUpperLift","feature.eyeLowerLid"}:
            QStringList{"head.size","head.hairVolume","head.crown","face.forehead","head.midface","head.philtrum","head.lowerface","head.hairline","head.yaw","head.pitch"};
        if(panel==0)std::erase_if(controls,[](const auto& p){return p.key=="face.forehead" || p.key=="face.jaw";});
        if(panel==2)for(const auto& p:editing::portrait_parameters(0))if(p.key=="face.forehead")controls.push_back(p);
        if(panel==2)std::erase_if(controls,[&](const auto& p){return !order.contains(p.key);});
        std::stable_sort(controls.begin(),controls.end(),[&](const auto& a,const auto& b){const int ai=order.indexOf(a.key),bi=order.indexOf(b.key);return (ai<0?999:ai)<(bi<0?999:bi);});
        const QHash<QString,QString> labels{{"face.width",tr("脸宽")},{"face.chin",tr("长短")},{"face.chinWidth",tr("宽窄")},{"face.doubleChin",tr("去双下巴")},{"head.size",tr("小头")},{"head.crown",tr("颅顶")},{"head.hairline",tr("发际线")}};
        for(auto& p:controls)if(labels.contains(p.key))p.label=labels.value(p.key);
        if(panel==2)for(auto& p:controls){if(p.key=="head.yaw")p.label=tr("左右");if(p.key=="head.pitch")p.label=tr("上下");}
    }
    if(panel==2 && workspace_task==11)std::erase_if(controls,[](const auto& p){return p.key!="head.size";});
    if(panel==5)std::erase_if(controls,[workspace_task](const auto& p){return (p.key=="skin.oil")!=(workspace_task==7);});
    details_->setVisible(workspace_task!=3 && workspace_task!=11 && workspace_task!=9);
    if(liquify_bar_)liquify_bar_->setVisible(workspace_task==9 && property("portraitSlimMode").toInt()==1);
    if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->setVisible(workspace_task!=11 && workspace_task!=9);
    const bool partial=selected_>=0 && people_[std::size_t(selected_)].settings.value("partialSkin").toBool();
    if(workspace_task==11){build_head_parameters();return;}
    if(panel==4){build_liquify();return;}
    if(workspace_task==4){build_skin_parameters();return;}
    if(workspace_task==7){build_oil_parameters();return;}
    if(workspace_task==8){build_body_parameters();return;}
    if(workspace_task==1){build_hair_parameters();return;}
    if(workspace_task==12){build_paint_parameters();return;}
    if(workspace_task==2){build_makeup_parameters();return;}
    if(workspace_task==13){build_teeth_parameters();return;}
    if(workspace_task==14){build_eye_parameters();return;}
    if(workspace_task==10){build_erase_parameters();return;}
    if(workspace_task==5){build_blemish_parameters();return;}
    if(workspace_task==6){build_wrinkle_parameters();return;}
    if(workspace_task==0){details_->hide();if(auto* sides=findChild<QWidget*>("portraitSides"))sides->hide();if(auto* scope=findChild<QToolButton*>("portraitScopeHeader"))scope->hide();build_volume_parameters();return;}
    if(selected_>=0 && people_[std::size_t(selected_)].settings.value("imageScope").toBool() && panel!=4 && panel!=7){panel_description_->setText(tr("未识别到人脸，可切换到瘦脸瘦身的手动液化或消除。"));return;}
    if(partial && panel==3)std::erase_if(controls,[](const auto& p){return p.key!="body.back";});
    if(panel==9 || (panel>=11 && panel<=13)){
        const QString property_name=QString("portraitSection%1").arg(panel);const int section=property(property_name.toUtf8().constData()).toInt();
        const QStringList titles=panel==9?QStringList{tr("眼周修复"),tr("美瞳"),tr("眼神光")}:panel==11?QStringList{tr("唇妆"),tr("眉妆"),tr("眼妆"),tr("腮红"),tr("眼神光"),tr("整妆"),tr("底妆"),tr("装饰痣"),tr("雀斑"),tr("面部装饰"),tr("美甲"),tr("纹身贴")}:panel==12?QStringList{tr("染发"),tr("发质"),tr("补发"),tr("发饰")}:QStringList{tr("自动"),tr("手绘"),tr("身体线条")};
        auto* tabs=new QTabBar(parameter_host_);tabs->setObjectName("portraitStyleTabs");tabs->setExpanding(true);tabs->setUsesScrollButtons(true);tabs->setStyleSheet("QTabBar::tab{padding:6px 3px;}");for(const auto& title:titles)tabs->addTab(title);tabs->setCurrentIndex(section);parameter_layout_->addWidget(tabs);if(panel==11)tabs->hide();
        connect(tabs,&QTabBar::currentChanged,this,[this,property_name,panel](int index){placement_key_.clear();finish_gesture();setProperty(property_name.toUtf8().constData(),index);if(panel==11)setProperty("portraitMakeupCollapsed",false);repair_mode_=0;range_mode_=0;view_->set_tool(0);build_parameters();refresh_ranges();if(panel==12 && index==2){repair_mode_=4;view_->set_tool(-1);}if(panel==13 && index==1 && selected_>=0){range_key_->setCurrentIndex(range_key_->findData("contour.paint"));range_mode_=1;view_->set_tool(-1);}});
        std::erase_if(controls,[panel,section](const auto& p){const auto k=p.key;
            if(panel==9){const int group=k.startsWith("makeup.iris")?1:k.startsWith("makeup.catch")?2:0;return group!=section;}
            if(panel==11){const int group=k.startsWith("makeup.tattoo")?11:k.startsWith("makeup.nails")?10:k.startsWith("makeup.sticker")?9:k.startsWith("makeup.freckles")?8:k.startsWith("makeup.mole")?7:k=="makeup.foundation"?6:k.contains("lip")?0:k.contains("brow")?1:k.contains("blush")?3:k.contains("catch")?4:2;return group!=section;}
            if(panel==12){const int group=k.contains("accessory")?3:(k=="hair.color" || k=="hair.roots" || k=="hair.rootWidth")?0:1;return group!=section;}
            const int group=k=="contour.paint"?1:k.startsWith("contour.lines")?2:0;return group!=section;
        });
    }

    if(auto* sides=findChild<QWidget*>("portraitSides"))sides->setVisible(workspace_task!=3 && std::any_of(controls.begin(),controls.end(),[](const auto& parameter){return parameter.bilateral;}));
    if(selected_>=0 && people_[std::size_t(selected_)].settings.value("partialSkin").toBool() && ((panel<5 && panel!=3 && panel!=4) || (panel>8 && panel<15)) && !(panel==11 && property("portraitSection11").toInt()>=10)){panel_description_->setText(tr("局部照片缺少完整人脸定位，此项不可用。肤质、肤色、瑕疵和去皱可继续使用。"));return;}
    if(panel>=7 && panel<=10)build_repairs(panel);
    if(panel==9 || (panel>=11 && panel<=14))build_style(panel);
    if(panel==0 && property("portraitWorkspaceTask").toInt()==0){auto* apply=new QPushButton(tr("一键自然丰盈"),parameter_host_);apply->setObjectName("portraitVolumeCombination");apply->setEnabled(selected_>=0 && !volume_analysis_busy_);apply->setToolTip(tr("组合额头、泪沟、苹果肌与面颊；只应用可靠区域，实际参数可逐区调整。一键应用只记一次撤销。"));parameter_layout_->addWidget(apply);connect(apply,&QPushButton::clicked,this,[this]{apply_volume_combination(false);});auto* lift=new QPushButton(tr("一键自然提拉"),parameter_host_);lift->setObjectName("portraitLiftCombination");lift->setEnabled(selected_>=0 && !volume_analysis_busy_);lift->setToolTip(tr("组合轮廓、额头、眼周、面中和口周；替换对应参数，可逐项调整，重复点击不叠加。"));parameter_layout_->addWidget(lift);connect(lift,&QPushButton::clicked,this,[this]{apply_volume_combination(true);});}
    if(panel==6){auto* reference=new QWidget(parameter_host_);auto* row=new QHBoxLayout(reference);row->setContentsMargins(0,0,0,0);auto* choose=new QPushButton(tr("选择协调参照区"),reference);choose->setObjectName("portraitBalanceReference");choose->setEnabled(selected_>=0);row->addWidget(choose);auto* automatic=new QPushButton(tr("以面部为参照"),reference);automatic->setObjectName("portraitBalanceAutomatic");automatic->setEnabled(selected_>=0);row->addWidget(automatic);parameter_layout_->addWidget(reference);
        connect(choose,&QPushButton::clicked,this,[this]{finish_brush_gesture();color_previous_tool_=std::max(0,view_->active_tool());color_sampling_=true;balance_sampling_=true;range_mode_=0;repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);status_->setText(tr("单击一块光线均匀的皮肤作为协调参照"));});
        connect(automatic,&QPushButton::clicked,this,[this]{if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;settings.remove("balanceReference");const double value=settings["parameters"].toObject()["color.balance"].toDouble();set_parameter("color.balance",value>0?value:50);build_parameters();});
        const auto chosen=selected_>=0?people_[std::size_t(selected_)].settings.value("balanceReference").toArray():QJsonArray{};auto* note=new QLabel(chosen.size()==3?tr("手动参照区 · 区内肤色保持，协调其他皮肤"):tr("自动参照：面部可靠皮肤"),parameter_host_);note->setObjectName("portraitBalanceStatus");parameter_layout_->addWidget(note);
    }
    if(panel==15)build_presets();
    if(panel==3){auto* back=new QPushButton(tr("定位上背轮廓…"),parameter_host_);back->setObjectName("portraitBackLocate");back->setEnabled(selected_>=0);back->setToolTip(tr("自动定位不足时，在侧面照片依次点选背部轮廓的上端、最凸处、下端。只修饰这段轮廓，不自动改变坐姿或头部姿态。"));parameter_layout_->addWidget(back);
        connect(back,&QPushButton::clicked,this,[this]{finish_brush_gesture();finish_gesture();range_mode_=repair_mode_=0;liquify_tool_=-1;locate_->setChecked(false);back_placement_={};placement_key_="body:back";view_->set_tool(-1);update_overlay();status_->setText(tr("点选上背轮廓的上端；点击抓手可取消"));});
        if(selected_>=0 && people_[std::size_t(selected_)].settings.value("backContour").toArray().size()==3){auto* clear=new QPushButton(tr("清除上背定位"),parameter_host_);clear->setObjectName("portraitBackClear");parameter_layout_->addWidget(clear);connect(clear,&QPushButton::clicked,this,[this]{finish_gesture();auto& settings=people_[std::size_t(selected_)].settings;settings.remove("backContour");auto params=settings["parameters"].toObject();params["body.back"]=0;settings["parameters"]=params;remember();build_parameters();update_overlay();status_->setText(tr("已清除上背定位并归零改善驼背，可撤销"));});}}
    if(panel==3 && !partial){
        auto* swan=new QPushButton(tr("天鹅颈 · 自然"),parameter_host_);swan->setObjectName("portraitSwanNeck");swan->setEnabled(selected_>=0);
        swan->setToolTip(tr("设置颈部粗细为 −30、长度为 +30；下方两项可分别调整。重复点击替换这两项，不累加。"));parameter_layout_->addWidget(swan);
        connect(swan,&QPushButton::clicked,this,[this]{finish_brush_gesture();finish_gesture();finish_preset(true);preview_preset(QJsonObject{{"neckGeometryVersion",2},{"parameters",QJsonObject{{"body.neck",-30},{"body.neckLength",30}}}},1);finish_preset(true);});
    }
    if(panel==3 && !partial){auto* distort=new QPushButton(tr("设置手动扭曲范围…"),parameter_host_);distort->setObjectName("portraitManualDistortRegion");distort->setEnabled(selected_>=0);parameter_layout_->addWidget(distort);connect(distort,&QPushButton::clicked,this,[this]{finish_brush_gesture();finish_gesture();range_mode_=repair_mode_=0;liquify_tool_=-1;locate_->setChecked(false);placement_key_="body:manualDistort";manual_distort_handle_=-1;painting_mesh_=manual_slim_placement_mesh();view_->set_tool(-1);update_overlay();status_->setText(tr("拖出范围；右侧圆柄旋转，上方方柄剪切，拖中心移动，拖外圈改范围。"));});
        auto* reset=new QPushButton(tr("恢复当前局部扭曲"),parameter_host_);reset->setObjectName("portraitManualDistortReset");reset->setEnabled(selected_>=0);parameter_layout_->addWidget(reset);if(workspace_task==3)reset->hide();connect(reset,&QPushButton::clicked,this,[this]{finish_brush_gesture();finish_gesture();auto& settings=people_[std::size_t(selected_)].settings;auto params=settings["parameters"].toObject();params["body.manualRotate"]=0;params["body.manualShear"]=0;settings["parameters"]=params;remember();build_parameters();update_overlay();});}
    if(panel==3 && !partial){auto* scale=new QPushButton(tr("设置手动缩放范围…"),parameter_host_);scale->setObjectName("portraitManualScaleRegion");scale->setToolTip(tr("内外圈独立调整；过渡环越窄，可用缩放幅度越小，以保持外圈固定并避免折叠。"));scale->setEnabled(selected_>=0);parameter_layout_->addWidget(scale);connect(scale,&QPushButton::clicked,this,[this]{finish_brush_gesture();finish_gesture();range_mode_=repair_mode_=0;liquify_tool_=-1;locate_->setChecked(false);placement_key_="body:manualScale";manual_scale_handle_=-1;manual_scale_region_=people_[std::size_t(selected_)].settings["manualScaleRegion"].toArray();painting_mesh_=manual_slim_placement_mesh();view_->set_tool(-1);update_overlay();status_->setText(tr("从中心向外拖出范围，再分别拖动内圈、外圈或中心。"));});}
    if(panel==3 && !partial){auto* slim=new QPushButton(tr("设置手动瘦身区域…"),parameter_host_);slim->setObjectName("portraitManualSlimRegion");slim->setEnabled(selected_>=0);parameter_layout_->addWidget(slim);connect(slim,&QPushButton::clicked,this,[this]{finish_brush_gesture();finish_gesture();range_mode_=repair_mode_=0;liquify_tool_=-1;locate_->setChecked(false);placement_key_="body:manualSlim";manual_slim_handle_=-1;const auto a=people_[std::size_t(selected_)].settings["manualSlimRegion"].toArray();manual_slim_box_=a.size()==4?QRectF(a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()):QRectF{};painting_mesh_=manual_slim_placement_mesh();view_->set_tool(-1);update_overlay();status_->setText(tr("按住拖出瘦身区域；已有框可拖动角点缩放或拖动框内移动。"));});}
    if(panel==3 && !partial){auto* height=new QPushButton(tr("设置手动增高范围…"),parameter_host_);height->setObjectName("portraitManualHeightRange");height->setEnabled(selected_>=0);parameter_layout_->addWidget(height);connect(height,&QPushButton::clicked,this,[this]{finish_brush_gesture();finish_gesture();range_mode_=repair_mode_=0;liquify_tool_=-1;locate_->setChecked(false);placement_key_="body:manualHeight";manual_height_handle_=-1;manual_height_points_=people_[std::size_t(selected_)].settings["manualHeightRange"].toArray();painting_mesh_=editing::portrait_deformation(source_.size(),recipe());view_->set_tool(-1);update_overlay();status_->setText(tr("依次点击上、下范围线的中心；已有范围可直接拖动，横向移动可改变角度。"));});}
    if(panel==3 && !partial){auto* chest=new QPushButton(tr("定位胸部轮廓（3点）…"),parameter_host_);chest->setObjectName("portraitChestLocate");chest->setEnabled(selected_>=0);parameter_layout_->addWidget(chest);connect(chest,&QPushButton::clicked,this,[this]{finish_brush_gesture();finish_gesture();range_mode_=repair_mode_=0;liquify_tool_=-1;locate_->setChecked(false);placement_key_="body:chest";chest_placement_points_=QJsonArray{};painting_mesh_=editing::portrait_deformation(source_.size(),recipe());view_->set_tool(-1);update_overlay();status_->setText(tr("依次点胸部可见外轮廓的上端、调整处、下端；先点上端，抓手可退出"));});}
    if(panel==3 && !partial){auto* locate=new QPushButton(tr("定位腹部前缘…"),parameter_host_);locate->setObjectName("portraitAbdomenLocate");locate->setEnabled(selected_>=0);locate->setToolTip(tr("在照片上点选腹部前突处的可见轮廓，用于侧身收腹和局部瘦肚子；不改变后背。"));parameter_layout_->addWidget(locate);connect(locate,&QPushButton::clicked,this,[this]{finish_brush_gesture();finish_gesture();range_mode_=repair_mode_=0;liquify_tool_=-1;locate_->setChecked(false);placement_key_="body:abdomen";view_->set_tool(-1);status_->setText(tr("点选下腹前缘的可见轮廓；点击抓手可退出"));});
        if(selected_>=0 && people_[std::size_t(selected_)].settings.value("abdomenFront").toArray().size()==2){auto* clear=new QPushButton(tr("清除前缘定位"),parameter_host_);clear->setObjectName("portraitAbdomenClear");parameter_layout_->addWidget(clear);connect(clear,&QPushButton::clicked,this,[this]{finish_gesture();auto& settings=people_[std::size_t(selected_)].settings;settings.remove("abdomenFront");auto params=settings["parameters"].toObject();params["body.belly"]=0;params["body.abdomen"]=0;settings["parameters"]=params;remember();build_parameters();update_overlay();status_->setText(tr("已清除前缘定位并归零收腹参数，可撤销"));});}}
    if(panel==3 && !partial){auto* combo=new QPushButton(tr("预览一键美体组合…"),parameter_host_);combo->setObjectName("portraitCombination");combo->setEnabled(selected_>=0);parameter_layout_->addWidget(combo);connect(combo,&QPushButton::clicked,this,[this]{preview_body_combination();});}
    if(panel==4)build_liquify();
    if(controls.empty()){if(panel==11)build_makeup_accordion();parameter_layout_->addStretch();return;}
    const auto settings=selected_>=0?people_[std::size_t(selected_)].settings:QJsonObject{};
    const auto params=settings["parameters"].toObject();
    if(panel==5 && workspace_task!=7){
        auto* row=new QWidget(parameter_host_);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
        const QStringList keys{"classic","texture","native"},labels{tr("经典"),tr("质感"),tr("原生")},tips{tr("更强的中频平滑，适度柔化细纹理"),tr("平滑中频差异，保留细纹理"),tr("较小处理尺度，更多保留原有结构")};
        for(int i=0;i<keys.size();++i){auto* button=new QPushButton(labels[i],row);button->setObjectName("portraitSmoothStyle_"+keys[i]);button->setCheckable(true);button->setChecked(settings["smoothStyle"].toString()==keys[i]);button->setEnabled(selected_>=0);button->setToolTip(tips[i]);layout->addWidget(button);connect(button,&QPushButton::clicked,this,[this,key=keys[i]]{if(selected_<0)return;finish_gesture();auto& settings=people_[std::size_t(selected_)].settings;settings["smoothStyle"]=key;settings["smoothVersion"]=2;const double strength=settings["parameters"].toObject()["skin.smooth"].toDouble();if(strength==0)set_parameter("skin.smooth",40);else remember();build_parameters();});}
        parameter_layout_->addWidget(row);
        const QString target=property("portraitSmoothTarget").toInt()==1?QStringLiteral("skin.bodySmooth"):QStringLiteral("skin.smooth");
        auto* targets=new QTabBar(parameter_host_);targets->setObjectName("portraitSmoothTarget");targets->addTab(tr("面部范围"));targets->addTab(tr("身体范围"));targets->setCurrentIndex(target=="skin.bodySmooth"?1:0);parameter_layout_->addWidget(targets);
        connect(targets,&QTabBar::currentChanged,this,[this](int value){finish_brush_gesture();setProperty("portraitSmoothTarget",value);range_key_->setCurrentIndex(range_key_->findData(value==1?"skin.bodySmooth":"skin.smooth"));build_parameters();update_range_overlay();});
        auto* modes=new QTabBar(parameter_host_);modes->setObjectName("portraitSmoothMode");modes->addTab(tr("自动范围"));modes->addTab(tr("手动涂抹"));modes->setCurrentIndex(settings["smoothManual"].toObject()[target].toBool()?1:0);modes->setEnabled(selected_>=0);parameter_layout_->addWidget(modes);
        connect(modes,&QTabBar::currentChanged,this,[this,target](int value){if(selected_<0)return;finish_brush_gesture();auto& settings=people_[std::size_t(selected_)].settings;auto modes=settings["smoothManual"].toObject();modes[target]=value==1;settings["smoothManual"]=modes;range_key_->setCurrentIndex(range_key_->findData(target));range_mode_=value==1?1:0;repair_mode_=0;repair_selected_=-1;locate_->setChecked(false);view_->set_tool(value==1?-1:0);range_visible_->setChecked(value==1);if(value==1 && settings["parameters"].toObject()[target].toDouble()==0)set_parameter(target,40);else remember();build_parameters();update_range_overlay();status_->setText(value==1?tr("只处理手动涂抹区域；切换自动不会丢失手绘范围"):tr("使用自动范围及其补减修正"));});
        auto* brushes=new QWidget(parameter_host_);auto* brushRow=new QHBoxLayout(brushes);brushRow->setContentsMargins(0,0,0,0);auto* brushGroup=new QButtonGroup(brushes);brushGroup->setExclusive(true);
        for(int mode:{1,2}){auto* button=new QPushButton(mode==1?tr("涂抹／补选"):tr("擦除／减选"),brushes);button->setObjectName(mode==1?"portraitSmoothPaint":"portraitSmoothErase");button->setCheckable(true);button->setChecked(range_mode_==mode);brushGroup->addButton(button);button->setEnabled(selected_>=0);brushRow->addWidget(button);connect(button,&QPushButton::clicked,this,[this,target,mode]{finish_brush_gesture();range_key_->setCurrentIndex(range_key_->findData(target));range_mode_=mode;repair_mode_=0;repair_selected_=-1;color_sampling_=false;locate_->setChecked(false);view_->set_tool(-1);range_visible_->setChecked(true);update_range_overlay();});}
        parameter_layout_->addWidget(brushes);
        auto* size=new QSlider(Qt::Horizontal,parameter_host_);size->setObjectName("portraitSmoothBrushSize");size->setRange(1,500);size->setValue(brush_size_->value());parameter_layout_->addWidget(new QLabel(tr("画笔大小（原图像素）"),parameter_host_));parameter_layout_->addWidget(size);connect(size,&QSlider::valueChanged,brush_size_,&QSlider::setValue);connect(brush_size_,&QSlider::valueChanged,size,&QSlider::setValue);
    }

    auto* enabled=new QCheckBox(tr("启用当前项目"),parameter_host_);enabled->setChecked(settings["enabled"].toObject()[controls.front().key.section('.',0,0)].toBool(true));enabled->setEnabled(selected_>=0);enabled->hide();
    connect(enabled,&QCheckBox::toggled,this,[this,group=controls.front().key.section('.',0,0)](bool value){if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto groups=settings["enabled"].toObject();groups[group]=value;settings["enabled"]=groups;remember();});
    bool body_ready=true;
    if(panel==3){const auto pose=settings["pose"].toArray();body_ready=selected_>=0 && !people_[std::size_t(selected_)].body_ambiguous && pose.size()==33;for(int i:{11,12,23,24})body_ready=body_ready && pose[i].toArray().size()==3 && pose[i].toArray()[2].toDouble()>.5;
        if(!body_ready)panel_description_->setText(tr("身体姿态或关联不确定。请显示关键点核对并修正肩、髋定位，或使用手动液化；自动身材滑块暂不可用。"));}
    auto group_name=[panel,workspace_task](const QString& key)->QString {
        if(workspace_task==3 && panel==0)return key=="face.chin" || key=="face.chinWidth" || key=="face.doubleChin"?QStringLiteral("下巴"):QStringLiteral("轮廓");
        if(workspace_task==3 && panel==2)return key=="head.yaw" || key=="head.pitch" || key=="head.roll"?QStringLiteral("整体朝向"):QStringLiteral("比例");
        if(panel==0 && (key=="face.contourLift" || key=="face.foreheadLift" || key=="face.orbitalLift" || key=="face.orbitalTone" || key=="face.midfaceLift" || key=="face.perioralLift" || key=="face.perioralTone"))return QStringLiteral("提拉");
        if(panel==0 && (key=="face.volume" || key=="face.cheekTone"))return QStringLiteral("面颊丰盈");
        if(panel==0 && (key=="face.malarShape" || key=="face.malarTone"))return QStringLiteral("苹果肌");
        if(panel==0 && key=="face.foreheadFill")return QStringLiteral("额头丰盈");
        if(panel==0 && key=="face.tearFill")return QStringLiteral("泪沟丰盈");
        if(panel==11){if(key.startsWith("makeup.tattoo"))return QStringLiteral("纹身材质");if(key.startsWith("makeup.nails"))return QStringLiteral("甲油材质");if(key.startsWith("makeup.sticker"))return QStringLiteral("面部装饰");if(key.startsWith("makeup.mole"))return QStringLiteral("装饰痣");if(key.startsWith("makeup.freckles"))return QStringLiteral("雀斑");}
        if(panel==1){if(key.contains("eye",Qt::CaseInsensitive))return QStringLiteral("眼睛");if(key.contains("nose"))return QStringLiteral("鼻子");if(key.contains("brow"))return QStringLiteral("眉毛");return QStringLiteral("嘴唇");}
        if(panel==11){if(key=="makeup.foundation")return QStringLiteral("底妆");if(key.startsWith("makeup.lipSparkle"))return QStringLiteral("细闪");if(key=="makeup.lipGloss")return QStringLiteral("光泽与哑光");if(key=="makeup.lipFeather" || key.startsWith("makeup.lipBleed"))return QStringLiteral("唇缘");if(key.contains("lip"))return QStringLiteral("颜色与覆盖");if(key.contains("brow"))return QStringLiteral("眉妆");if(key.contains("blush"))return QStringLiteral("腮红");if(key.contains("catch"))return QStringLiteral("眼神光");return QStringLiteral("眼妆");}
        if(panel==12){if(key.contains("accessory"))return QStringLiteral("发饰");if(key.contains("color") || key.contains("gradient"))return QStringLiteral("染发");return QStringLiteral("发质");}
        if(panel==3){if(key.contains("leg",Qt::CaseInsensitive) || key.contains("height") || key=="body.thigh" || key=="body.calf")return QStringLiteral("腿部与身高");if(key.contains("arm",Qt::CaseInsensitive))return QStringLiteral("手臂");return QStringLiteral("躯干");}
        return QStringLiteral("调整");
    };
    QHash<QString,QVBoxLayout*> groups;
    for(const auto& descriptor:controls){const auto group=group_name(descriptor.key);if(groups.contains(group))continue;
        const auto id=QString::number(panel)+"_"+group;auto* section=new QWidget(parameter_host_);auto* section_layout=new QVBoxLayout(section);section_layout->setContentsMargins(0,0,0,0);section_layout->setSpacing(2);
        auto header=raw_section_header(section,group,"portraitGroup_"+id,"portraitGroupEnabled_"+id,expanded_groups_.value(id,groups.isEmpty() || (workspace_task==3 && (panel==2 || (panel==0 && group==tr("下巴"))))));section_layout->addLayout(header.layout);
        auto* content=new QWidget(section);auto* rows=new QVBoxLayout(content);rows->setContentsMargins(0,0,0,0);rows->setSpacing(0);content->setVisible(header.expand->isChecked());section_layout->addWidget(content);groups.insert(group,rows);parameter_layout_->addWidget(section);
        connect(header.expand,&QToolButton::toggled,this,[this,content,id,button=header.expand](bool open){expanded_groups_[id]=open;content->setVisible(open);button->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
        if(workspace_task==3 && panel==1 && (group==tr("眼睛") || group==tr("眉毛") || group==tr("嘴唇"))){
            const auto sideGroup=group==tr("眼睛")?QStringLiteral("eyes"):group==tr("眉毛")?QStringLiteral("brows"):QStringLiteral("mouth");
            auto* choices=new QButtonGroup(section);
            for(int side=0;side<3;++side){
                auto* choice=new QToolButton(section);choice->setText(side==0?tr("整体"):side==1?tr("左"):tr("右"));
                choice->setCheckable(true);choice->setFixedSize(side==0?52:36,32);
                choice->setToolTip(side==0?tr("同时调整左右；切换本身不修改数值"):tr("以人物自身的左右为准"));
                choice->setObjectName("portraitFeatureSide_"+sideGroup+"_"+QString::number(side));
                choice->setChecked(settings["featureSides"].toObject()[sideGroup].toInt()==side);
                choice->setStyleSheet("QToolButton{padding:0;border:0;min-width:0;}QToolButton:checked{background:#2d80dc;color:white;border-radius:4px;}");
                choices->addButton(choice,side);header.layout->addWidget(choice);
                connect(choice,&QToolButton::clicked,this,[this,side,sideGroup]{if(selected_<0)return;finish_gesture();auto& settings=people_[std::size_t(selected_)].settings;auto sides=settings["featureSides"].toObject();sides[sideGroup]=side;settings["featureSides"]=sides;build_parameters();});
            }
        }
        QStringList keys;for(const auto& d:controls)if(group_name(d.key)==group)keys<<d.key;
        header.enabled->setCheckable(false);header.enabled->setToolTip(tr("按住比较本组调整，松开恢复；不更改参数"));
        connect(header.enabled,&QToolButton::pressed,this,[this,keys]{group_bypass_=keys;schedule_preview();});
        connect(header.enabled,&QToolButton::released,this,[this]{group_bypass_.clear();schedule_preview();});
        auto* reset_group=new QToolButton(section);reset_group->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));reset_group->setToolTip(tr("重置本组参数，保留范围与保护"));reset_group->setObjectName("portraitGroupReset_"+id);header.layout->addWidget(reset_group);
        if(workspace_task==3 && panel==2 && group==tr("比例")){
            for(int i=0;i<header.layout->count();++i)if(auto* w=header.layout->itemAt(i)->widget())w->hide();
        }
        connect(reset_group,&QToolButton::clicked,this,[this,keys]{if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto values=settings["parameters"].toObject();for(const auto& key:keys){values.remove(key);values.remove(key+".left");values.remove(key+".right");}settings["parameters"]=values;remember();build_parameters();});
    }
    for(const auto& parameter:controls){auto* widget=new QWidget(parameter_host_);auto* column=new QVBoxLayout(widget);column->setContentsMargins(0,1,0,1);column->setSpacing(0);
        auto* label=new QCheckBox(parameter.label,widget);label->setObjectName("portraitEnabled_"+parameter.key);label->setChecked(!settings["disabledParameters"].toObject()[parameter.key].toBool());label->hide();auto* title=new QLabel(parameter.label,widget);title->setObjectName("portraitLabel_"+parameter.key);title->setToolTip(tr("双击滑条恢复默认；右键可暂时停用此参数"));title->setMinimumWidth(0);title->setWordWrap(true);title->setToolTip(parameter.label+tr(" · 双击恢复默认"));title->installEventFilter(this);title->setProperty("resetParameter",parameter.key);
        const auto update_label=[title,label,text=parameter.label](bool enabled){title->setText(text+(enabled?QString{}:QStringLiteral("（已停用）")));title->setEnabled(enabled);Q_UNUSED(label);};update_label(label->isChecked());connect(label,&QCheckBox::toggled,title,update_label);auto* row=new QHBoxLayout;row->addWidget(title);
        connect(label,&QCheckBox::toggled,this,[this,key=parameter.key](bool checked){if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto disabled=settings["disabledParameters"].toObject();if(checked)disabled.remove(key);else disabled[key]=true;settings["disabledParameters"]=disabled;remember();});
        auto* slider=new ColorBandSlider(-1,0,widget);slider->setFixedHeight(24);slider->set_colors(QColor("#69727c"),QColor("#69727c"));slider->setRange(int(parameter.minimum),int(parameter.maximum));slider->setObjectName("portraitSlider_"+parameter.key);slider->setAccessibleName(parameter.label);bind_slider(slider);auto* spin=new QDoubleSpinBox(widget);spin->setKeyboardTracking(false);spin->setDecimals(0);spin->setRange(parameter.minimum,parameter.maximum);spin->setFixedSize(72,26);spin->setButtonSymbols(QAbstractSpinBox::NoButtons);spin->setObjectName("portraitParam_"+parameter.key);spin->setAccessibleName(parameter.label);
        if(parameter.key=="face.width" || parameter.key=="face.cheek" || parameter.key=="face.jaw" || parameter.key=="face.chin"){
            const QString direction=parameter.key=="face.chin"?tr("负值缩短下巴，正值延长下巴；固定嘴部位置"):
                parameter.key=="face.width"?tr("负值收窄脸部轮廓，正值加宽；固定眼鼻口"):
                parameter.key=="face.cheek"?tr("负值收窄颧部，正值加宽颧部；保持下巴位置"):
                tr("负值收窄下颌，正值加宽下颌；保持嘴部位置");
            const auto hint=direction+tr("。双击恢复默认。旧工程首次调整这组参数后采用新的轮廓算法，可撤销。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="body.shoulder" || parameter.key=="body.shoulderWidth"){const auto hint=parameter.key=="body.shoulder"?tr("负值抬肩，正值降肩；颈根与肘部保持位置。选择人物左侧或右侧可单独校正高低肩，双击归零。"):tr("负值收窄，正值加宽；肩部衣物随动，保护颈部与头发。左右可独立调整，双击归零。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="body.trapezius"){const auto hint=tr("正值降低颈根到肩峰之间的斜方肌轮廓，负值反向恢复饱满；保持肩宽，左右可独立调整。头发遮挡或轮廓不足时使用手动液化。双击归零。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="body.squareShoulder"){const auto hint=tr("调整可见肩线曲率，使外侧肩线更平缓；保持颈根与肩袖平滑连接。左右可独立调整，双击归零。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="body.height"){const auto hint=tr("脚部落点固定，将长度变化分配给腿和躯干；头部与手臂随肩部整体移动，不直接拉长头部。需可靠完整站姿；正值增高，负值降低。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="body.legLength"){const auto hint=tr("固定髋部和躯干，分配大腿/小腿长度变化；膝部与脚部保持局部形状。脚部随腿长整体移动，落点会改变。正值伸长、负值缩短；双侧同量时协调伸展；超出画布或单侧作用域与另一腿重叠时不处理。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="body.straightLeg"){const auto hint=tr("使膝部向髋踝连线靠拢，保持髋、踝与脚部落点；不是瘦腿。明显屈腿、坐姿、交叉或遮挡时不自动处理。左右可独立调整。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="body.back"){const auto hint=tr("减弱侧面照片中可见的上背后凸，保持前胸、头部、肩髋和四肢位置；不矫正头部前倾，也不把侧弯或弯腰当成驼背。坐姿、屈腿、遮挡或定位不足时不自动处理。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="body.abdomen"){const auto hint=tr("内收下腹两侧可见轮廓，固定中线、上胸和骨盆；与整段收腰独立。侧身、手臂遮挡或定位不足时改用手动液化。双击归零。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="body.waist"){
            const auto hint=tr("正值收腰，负值加宽；沿躯干轴调整腰段轮廓，固定上胸和骨盆。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="face.tearFill"){const auto hint=tr("沿内眼角下方的局部凹陷暗带调整明暗，不改变眼睑开度或卧蚕；与黑眼圈去色、眼袋处理分开。定位不足的一侧不应用。双击归零。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="face.foreheadFill"){const auto hint=tr("减轻可见额头皮肤的低频凹暗，保留原光照方向与细节；不移动眉毛或发际线。轮廓仍使用独立的额头控制。双击归零。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);}
        if(parameter.key=="body.arm" || parameter.key=="body.upperArm" || parameter.key=="body.legWidth" || parameter.key=="body.thigh" || parameter.key=="body.calf"){
            const auto hint=tr("正值变细，负值增粗；沿对应肢段法线调整，固定关节位置与长度。需要清晰可见的两侧轮廓；贴近躯干或遮挡时请修正范围或使用手动液化。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="face.jawAngle"){
            const auto hint=tr("调整下颌角下方轮廓的转折；正值向内转，负值向外转。固定颌角位置、耳根与下巴尖，不整体收窄下颌。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="face.chinWidth" || parameter.key=="face.smoothContour"){
            const auto hint=(parameter.key=="face.chinWidth"?tr("负值收窄颏部两侧，正值加宽；保持下巴尖高度、下颌角与嘴部"):
                tr("柔化轮廓局部折转；固定耳根、下颌角与下巴尖，保留左右自然差异"))+tr("。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="head.yaw" || parameter.key=="head.pitch"){
            const auto hint=tr("依据当前人脸曲面小幅调整朝向，最大约6°；保留原片纹理，不生成被遮挡区域。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="head.roll"){
            const auto hint=tr("负值逆时针，正值顺时针；头部整体在画面内小幅倾斜，保持面部比例并过渡到颈肩。不是转头或抬头。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="head.size"){
            const auto hint=tr("正值缩小头部，负值放大；脸、头发和眼镜整体随动，以颈部连接处为支点。身材页的头身比例共用此参数，不重复叠加。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="face.forehead" || parameter.key=="head.crown" || parameter.key=="head.hairline" || parameter.key=="head.hairVolume"){
            const auto hint=(parameter.key=="face.forehead"?tr("负值缩短额头，正值延长；固定眉眼，按识别到的发际线协调头发"):
                parameter.key=="head.hairVolume"?tr("负值收拢头发外轮廓，正值外扩；保持脸部和发际线，不缩小五官"):
                parameter.key=="head.crown"?tr("负值降低颅顶，正值抬高颅顶；保持发际线和额头"):
                tr("负值上移发际线，正值下移；保留头顶和眉眼，不生成新头发"))+tr("。需有清楚、完整的发际线与头顶。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="face.small" || parameter.key=="face.vShape"){
            const auto hint=(parameter.key=="face.small"?tr("组合贡献：100 时脸宽 −35、下颌 −22、下巴宽窄 −12；五官保持原比例，不缩放头发。"):
                tr("组合贡献：100 时下颌 −45、下巴宽窄 −30、下巴长短 +8；不另加脸宽缩放。"))+
                tr("与单项合并后计算一次；关闭只移除此组合的贡献，保留手调值。合并目标限制在单项允许范围内。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key.startsWith("feature.eye")){
            const auto direction=parameter.key=="feature.eyeSize"?tr("负值缩小，正值放大；保持眼睛中心"):
                parameter.key=="feature.eyeSpace"?tr("负值靠近，正值远离面部中线；保持单眼大小"):
                parameter.key=="feature.eyeHeight"?tr("负值下移，正值上移；保持眼形"):
                parameter.key=="feature.eyeInnerCorner"?tr("负值回收，正值延展内眼角；保持外眼角与眼睛中央"):
                parameter.key=="feature.eyeOuterCorner"?tr("负值回收，正值延展外眼角；保持内眼角与眼睛中央"):
                parameter.key=="feature.eyeUpperLift"?tr("抬高上眼睑，保持下睑和两侧眼角；不能将闭眼变为睁眼"):
                parameter.key=="feature.eyeLowerLid"?tr("降低下眼睑，保持上睑和两侧眼角"):
                parameter.key=="feature.eyeOuterLift"?tr("负值降低，正值抬高外眼角；保持内眼角，不旋转整只眼"):
                parameter.key=="feature.eyeAperture"?tr("负值减小，正值增加上下眼睑开度；固定眼角，闭眼时不可用"):
                parameter.key=="feature.eyeLength"?tr("负值缩短，正值延长眼裂；沿眼轴调整两端，保持纵向开度"):
                tr("负值眼尾向下，正值眼尾向上；围绕单眼中心旋转");
            const auto hint=direction+tr("。识别到镜框时，自动保护镜框并限制移动幅度。双击恢复默认；旧工程首次调整本组后使用新算法，可撤销。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key.startsWith("feature.brow")){
            const auto direction=parameter.key=="feature.browHeight"?tr("负值下移，正值上移；保持眼睛位置"):
                parameter.key=="feature.browTilt"?tr("以眉头为支点；负值眉尾下转，正值眉尾上转"):
                parameter.key=="feature.browLength"?tr("固定眉头；负值缩短眉尾，正值延长眉尾"):
                parameter.key=="feature.browSpace"?tr("负值整眉靠近中线，正值整眉远离中线"):
                parameter.key=="feature.browHeadSpace"?tr("只调整眉头；负值靠近中线，正值远离中线"):
                parameter.key=="feature.browThickness"?tr("沿眉毛轮廓调整粗细；负值变细，正值变粗，眉长与眉峰位置保持"):
                parameter.key=="feature.browArchHeight"?tr("固定两端；负值降低眉峰，正值抬高眉峰"):
                tr("固定两端；负值眉峰向眉头移动，正值向眉尾移动");
            const auto hint=direction+tr("。识别到镜框时自动减小可用幅度，保护镜框。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="head.midface" || parameter.key=="head.philtrum" || parameter.key=="head.lowerface"){
            const auto direction=parameter.key=="head.midface"?tr("负值缩短中庭，正值延长；固定眼眶，下方面部随鼻底协调移动"):
                parameter.key=="head.philtrum"?tr("负值缩短人中，正值延长；鼻底与下巴固定，嘴部保持口型整体移动"):
                tr("负值缩短下庭，正值延长；固定鼻底，嘴部保持口型并协调移动");
            const auto hint=direction+tr("。双击恢复默认；旧工程首次调整本组后使用新算法，可撤销。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="feature.noseRootWidth" || parameter.key=="feature.noseBridgeWidth" || parameter.key=="feature.noseTipWidth"){
            const auto hint=tr("负值收窄，正值加宽当前部位；保持鼻中线、鼻翼与邻近五官。仅调整二维宽度，不改变真实鼻部高度。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="feature.noseSize" || parameter.key=="feature.noseWidth" || parameter.key=="feature.noseLength" || parameter.key=="feature.lipSize" || parameter.key=="feature.lipThickness" || parameter.key=="feature.smile"){
            const auto direction=parameter.key=="feature.noseSize"?tr("负值缩小，正值增大鼻部轮廓；协调鼻翼与鼻底，固定鼻根和嘴部"):
                parameter.key=="feature.noseWidth"?tr("负值收窄鼻翼，正值加宽鼻翼；保持鼻根与鼻长"):
                parameter.key=="feature.noseLength"?tr("负值缩短鼻部，正值延长；固定鼻根与嘴部"):
                parameter.key=="feature.lipSize"?tr("负值缩小唇部外轮廓，正值放大；保护口腔与牙齿"):
                parameter.key=="feature.lipThickness"?tr("负值变薄，正值变厚；保持嘴宽与口腔范围"):
                tr("负值降低嘴角，正值轻微上扬嘴角；不生成牙齿");
            const auto hint=direction+tr("。双击恢复默认；旧工程首次调整本组后使用新算法，可撤销。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="feature.lipWidth" || parameter.key=="feature.lipHeight" || parameter.key=="feature.lipOffset" || parameter.key=="feature.lipTilt" || parameter.key=="feature.lipUpper" || parameter.key=="feature.lipLower" || parameter.key=="feature.lipCupid"){
            const auto direction=parameter.key=="feature.lipWidth"?tr("负值收窄嘴角，正值加宽；保持唇高和牙齿大小"):
                parameter.key=="feature.lipHeight"?tr("负值下移，正值上移；嘴唇和口腔整体移动"):
                parameter.key=="feature.lipOffset"?tr("负值向画面左侧移动，正值向画面右侧移动；保持口型"):
                parameter.key=="feature.lipTilt"?tr("负值逆时针，正值顺时针；围绕嘴部中心旋转"):
                parameter.key=="feature.lipCupid"?tr("负值柔化唇峰，正值突出上唇双峰；保持嘴角和口腔"):
                tr("负值变薄，正值变厚；只调整对应唇部，保持口腔边界");
            const auto hint=direction+tr("。双击恢复默认。");title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="color.warmth" || parameter.key=="color.rosy" || parameter.key=="color.redness" || parameter.key=="skin.oil"){
            const auto hint=parameter.key=="color.warmth"?tr("负值偏冷，正值偏暖；保持原有明暗。双击恢复默认。"):
                parameter.key=="color.rosy"?tr("调节皮肤红润程度，限制局部过红区域的增强；保持明暗。双击恢复默认。"):
                parameter.key=="color.redness"?tr("淡化比附近皮肤更红的局部色差，保留明暗和纹理；不消除痘坑或深色痘印。双击恢复默认。"):
                tr("压低比附近皮肤更亮、色彩更淡的局部油光；不降低整脸曝光，也不能恢复已过曝的纹理。双击恢复默认。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        if(parameter.key=="skin.smooth" || parameter.key=="skin.bodySmooth"){
            const bool face=parameter.key=="skin.smooth";
            const bool available=settings["automaticMasks"].toObject().contains(face?"faceSkin":"bodySkin") || settings["masks"].toObject().contains(parameter.key);
            slider->setEnabled(available);spin->setEnabled(available);
            const auto hint=available?(face?tr("只处理面部皮肤，身体磨皮独立控制；可补选或减选本项范围。双击恢复默认。"):tr("只处理身体皮肤，排除面部；衣物与首饰依赖皮肤范围排除，可手动减选。双击恢复默认。")):
                tr("此工程尚无独立面部/身体皮肤范围。点击重新识别补齐；已有编辑保留，也可手动涂选本项范围。");
            title->setToolTip(hint);slider->setToolTip(hint);spin->setToolTip(hint);
        }
        const auto suffix=parameter.bilateral?(parameter_side(settings,parameter.key)==2?".right":".left"):"";const double value=parameter_display_value(parameter.key,displayed_parameter(settings,parameter.key,suffix,source_.size()));slider->setValue(int(value));spin->setValue(value);
        row->addStretch();row->addWidget(spin);column->addLayout(row);slider->setMinimumHeight(24);column->addWidget(slider);widget->setMinimumHeight(52);
        if((parameter.key=="face.small" || parameter.key=="face.vShape") && workspace_task!=3){
            auto* expand=new QToolButton(widget);expand->setText(tr("组合明细"));expand->setCheckable(true);expand->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);expand->setArrowType(Qt::RightArrow);column->addWidget(expand);
            auto* details=new QLabel(widget);details->setWordWrap(true);details->setMinimumHeight(54);details->hide();column->addWidget(details);
            const auto update=[details,small=parameter.key=="face.small",this](double amount){
                details->setText(small?tr("贡献：脸宽 %1 · 下颌 %2 · 下巴宽窄 %3\n原有手调值保留，关闭组合只移除这些贡献。")
                    .arg(-.35*amount,0,'f',1).arg(-.22*amount,0,'f',1).arg(-.12*amount,0,'f',1):
                    tr("贡献：下颌 %1 · 下巴宽窄 %2 · 下巴长短 %3\n原有手调值保留，关闭组合只移除这些贡献。")
                    .arg(-.45*amount,0,'f',1).arg(-.30*amount,0,'f',1).arg(.08*amount,0,'f',1));
            };
            connect(spin,&QDoubleSpinBox::valueChanged,details,update);update(spin->value());
            connect(expand,&QToolButton::toggled,details,[expand,details](bool open){details->setVisible(open);expand->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
        }
        groups.value(group_name(parameter.key))->addWidget(widget);widget->setEnabled(selected_>=0 && body_ready && !people_[std::size_t(selected_)].match_ambiguous && !people_[std::size_t(selected_)].unmatched);
        widget->setContextMenuPolicy(Qt::CustomContextMenu);connect(widget,&QWidget::customContextMenuRequested,this,[widget,label,spin](QPoint point){QMenu menu(widget);auto* enable=menu.addAction(QObject::tr("启用此参数"));enable->setCheckable(true);enable->setChecked(label->isChecked());QObject::connect(enable,&QAction::toggled,label,&QCheckBox::setChecked);auto* reset=menu.addAction(QObject::tr("恢复默认值"));QObject::connect(reset,&QAction::triggered,spin,[spin]{spin->setValue(0);});menu.exec(widget->mapToGlobal(point));});
        connect(slider,&QSlider::valueChanged,spin,[spin](int v){spin->setValue(v);});connect(spin,&QDoubleSpinBox::valueChanged,this,[this,slider,key=parameter.key](double v){const QSignalBlocker block(slider);slider->setValue(int(v));set_parameter(key,parameter_display_value(key,v));});
    }
    if(workspace_task==3 && panel==0){
        auto* toggle=new QToolButton(parameter_host_);toggle->setText(tr("组合明细"));toggle->setCheckable(true);toggle->setArrowType(Qt::RightArrow);toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);toggle->setMinimumHeight(30);parameter_layout_->addWidget(toggle);
        auto* description=new QLabel(tr("小脸：脸宽 −35%、下颌 −22%、下巴宽窄 −12%。\nV脸：下颌 −45%、下巴宽窄 −30%、下巴长短 +8%。\n组合按当前程度计算，保留单项手动值；关闭只移除组合贡献。"),parameter_host_);description->setWordWrap(true);description->hide();parameter_layout_->addWidget(description);
        connect(toggle,&QToolButton::toggled,description,[toggle,description](bool open){description->setVisible(open);toggle->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    }
    auto* reset=new QPushButton(tr("重置当前项目"),parameter_host_);reset->setEnabled(selected_>=0);parameter_layout_->addWidget(reset);if(workspace_task==3)reset->hide();
    connect(reset,&QPushButton::clicked,this,[this,controls]{if(selected_<0)return;auto& settings=people_[std::size_t(selected_)].settings;auto parameters=settings["parameters"].toObject();for(const auto& p:controls){parameters.remove(p.key);parameters.remove(p.key+".left");parameters.remove(p.key+".right");}settings["parameters"]=parameters;remember();build_parameters();});
    if(panel==5 && selected_>=0 && !people_[std::size_t(selected_)].settings["automaticMasks"].toObject().contains("faceSkin"))panel_description_->setText(tr("旧版磨皮保留原范围。点击重新识别补齐面部/身体范围，调整任一磨皮后切换为独立范围。"));
    if(panel==6){
        const auto apply_color=[this](QColor color){if(selected_<0 || !color.isValid())return;QImage image(1,1,QImage::Format_RGBA8888);image.setColorSpace(QColorSpace::SRgb);image.setPixelColor(0,0,color);const auto linear=editing::to_working(image);const auto* p=reinterpret_cast<const float*>(linear.constBits());people_[std::size_t(selected_)].settings["skinColor"]=QJsonArray{p[0],p[1],p[2]};people_[std::size_t(selected_)].settings["skinColorName"]=color.name();set_parameter("color.custom",50);build_parameters();};
        auto* choose=new QPushButton(tr("选择肤色…"),parameter_host_);choose->setObjectName("portraitChooseSkinColor");choose->setEnabled(selected_>=0);parameter_layout_->addWidget(choose);connect(choose,&QPushButton::clicked,this,[this,apply_color]{apply_color(QColorDialog::getColor(QColor(215,167,137),this,tr("肤色")));});
        auto* sample_host=new QWidget(parameter_host_);auto* sample_row=new QHBoxLayout(sample_host);sample_row->setContentsMargins(0,0,0,0);auto* sample_preview=new QLabel(sample_host);sample_preview->setObjectName("portraitSkinSamplePreview");sample_preview->setFixedSize(56,56);sample_preview->setAlignment(Qt::AlignCenter);sample_preview->setText(tr("取样\n位置"));
        if(selected_>=0){const auto position=people_[std::size_t(selected_)].settings["skinSample"].toArray();if(position.size()>=2){
            const QPoint point(int(position[0].toDouble()*source_.width()),int(position[1].toDouble()*source_.height()));
            const auto crop=QRect(point-QPoint(16,16),QSize(33,33)).intersected(source_.rect());
            if(!crop.isEmpty()){
                auto thumbnail=editing::display_image(source_.copy(crop)).scaled(56,56,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                const QPointF center((point.x()-crop.x()+.5)*56/crop.width(),(point.y()-crop.y()+.5)*56/crop.height());
                QPainter painter(&thumbnail);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(Qt::black,3));painter.drawEllipse(center,5,5);painter.setPen(QPen(Qt::white,1));painter.drawEllipse(center,5,5);painter.end();
                sample_preview->setPixmap(QPixmap::fromImage(thumbnail));sample_preview->setToolTip(tr("原图取样位置：%1, %2；5 × 5 像素邻域").arg(point.x()).arg(point.y()));
            }
        }}
        sample_row->addWidget(sample_preview);auto* sample_color=new QPushButton(tr("从画面取肤色"),sample_host);sample_color->setObjectName("portraitSampleSkinColor");sample_row->addWidget(sample_color,1);parameter_layout_->addWidget(sample_host);connect(sample_color,&QPushButton::clicked,this,[this]{color_previous_tool_=std::max(0,view_->active_tool());color_sampling_=true;balance_sampling_=false;range_mode_=0;repair_mode_=0;locate_->setChecked(false);view_->set_tool(-1);status_->setText(tr("单击画面取肤色；使用邻域平均排除噪点，取色后自动返回"));});
        auto* palette=new QComboBox(parameter_host_);palette->setObjectName("portraitSkinPalette");const auto colors=QJsonDocument::fromJson(QSettings().value("portrait/colors").toByteArray()).array();palette->addItem(tr("已保存的肤色"));for(const auto& entry:colors){const auto c=entry.toObject();palette->addItem(c["name"].toString(),c["hex"].toString());}palette->setCurrentIndex(std::clamp(QSettings().value("portrait/colorSelection").toInt(),0,int(colors.size())));parameter_layout_->addWidget(palette);connect(palette,&QComboBox::activated,this,[this,apply_color,palette,colors](int i){
            QSettings().setValue("portrait/colorSelection",i);if(i<=0 || selected_<0)return;
            const auto entry=colors[i-1].toObject();const auto linear=entry["linearRec2020"].toArray();
            const bool valid=linear.size()==3 && std::all_of(linear.begin(),linear.end(),[](const QJsonValue& v){return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=0;});
            if(valid && entry["colorSpace"].toString()=="linear-rec2020"){
                auto& settings=people_[std::size_t(selected_)].settings;settings["skinColor"]=linear;settings["skinColorName"]=entry["name"].toString();settings.remove("skinSample");
                const double current=settings["parameters"].toObject()["color.custom"].toDouble();set_parameter("color.custom",current>0?current:50);build_parameters();
            }else apply_color(QColor(palette->itemData(i).toString()));
        });
        auto* save_color=new QPushButton(tr("命名并保存当前肤色…"),parameter_host_);save_color->setObjectName("portraitSaveSkinColor");save_color->setEnabled(selected_>=0);parameter_layout_->addWidget(save_color);
        connect(save_color,&QPushButton::clicked,this,[this]{if(selected_<0)return;const auto settings=people_[std::size_t(selected_)].settings;const auto color=settings["skinColor"].toArray();if(color.size()!=3)return;QImage image(1,1,QImage::Format_RGBA32FPx4_Premultiplied);image.setColorSpace(editing::working_space());auto* p=reinterpret_cast<float*>(image.bits());for(int c=0;c<3;++c)p[c]=float(color[c].toDouble());p[3]=1;const auto hex=editing::display_image(image).pixelColor(0,0).name();bool ok=false;const auto name=QInputDialog::getText(this,tr("保存肤色"),tr("名称"),QLineEdit::Normal,hex,&ok);if(!ok || name.trimmed().isEmpty())return;QSettings config;auto colors=QJsonDocument::fromJson(config.value("portrait/colors").toByteArray()).array();colors.append(QJsonObject{{"name",name.left(80)},{"hex",hex},{"linearRec2020",color},{"colorSpace","linear-rec2020"}});while(colors.size()>64)colors.removeFirst();config.setValue("portrait/colors",QJsonDocument(colors).toJson(QJsonDocument::Compact));build_parameters();});
        auto* remove_color=new QPushButton(tr("删除所选色样"),parameter_host_);remove_color->setObjectName("portraitRemoveSkinColor");parameter_layout_->addWidget(remove_color);connect(remove_color,&QPushButton::clicked,this,[this,palette]{const int i=palette->currentIndex()-1;if(i<0)return;QSettings config;auto colors=QJsonDocument::fromJson(config.value("portrait/colors").toByteArray()).array();if(i<colors.size())colors.removeAt(i);config.setValue("portrait/colorSelection",0);config.setValue("portrait/colors",QJsonDocument(colors).toJson(QJsonDocument::Compact));build_parameters();});
    }
    if(panel==11)build_makeup_accordion();
    parameter_layout_->addStretch();
}
void PortraitDialog::set_parameter(const QString& key,double value) {
    if(selected_<0)return;auto& person=people_[std::size_t(selected_)];if(person.match_ambiguous || person.unmatched)return;
    const auto found=editing::portrait_parameter(key);if(!found)return;
    if(key=="face.doubleChin" && value!=0 && person.settings["doubleChinAnalysis"].toObject().isEmpty()){
        const auto analysis=editing::portrait_double_chin_analysis(linear_preview_,editing::portrait_person_json(person));
        if(analysis.isEmpty()){
            for(auto* control:findChildren<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(control);control->setValue(person.settings["parameters"].toObject()[key].toDouble());}
            if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(person.settings["parameters"].toObject()[key].toDouble()));}
            status_->setText(tr("未找到清晰可见的颏下皮肤暗带，本项未应用；可使用瘦脸瘦身中的手动推移。其他调整可继续使用。"));return;
        }
        person.settings["doubleChinAnalysis"]=analysis;person.settings["doubleChinVersion"]=2;
    }

    if((key=="head.yaw" || key=="head.pitch") && value!=0 && !person.settings["faceSurface"].isObject()){
        const QString id=person.id.toString();
        if(property("orientationAttemptedPerson").toString()!=id || busy_){
            setProperty("orientationAttemptedPerson",id);
            setProperty("pendingOrientationKey",key);setProperty("pendingOrientationValue",value);setProperty("pendingOrientationPerson",id);
            analyze();status_->setText(tr("正在补充朝向定位，完成后应用当前调整…"));
        }else status_->setText(tr("当前照片无法可靠调整朝向，其他面部调整仍可使用"));
        parameters_rebuild_pending_=true;return;
    }
    const auto previousSettings=person.settings;
    if(key=="head.yaw" || key=="head.pitch")person.settings["headOrientationVersion"]=3;
    auto parameters=person.settings.value("parameters").toObject();value=std::clamp(value,found->minimum,found->maximum);
    if(key=="body.chest" && person.settings["chestContourPaths"].toObject().isEmpty()){
        const auto path=person.settings["chestContourPath"].toArray();
        const auto pose=person.settings["pose"].toArray();
        if(path.size()==3 && pose.size()==33){
            const auto pixel=[this](QJsonArray p){return QPointF(p[0].toDouble()*source_.width(),p[1].toDouble()*source_.height());};
            const auto left=pixel(pose[11].toArray()),right=pixel(pose[12].toArray());
            const QString side=QPointF::dotProduct(pixel(path[1].toArray())-(left+right)*.5,left-right)>=0?"left":"right";
            person.settings["chestContourPaths"]=QJsonObject{{side,path}};
            parameters["body.chest."+side]=parameters["body.chest"];
            parameters.remove("body.chest");
            person.settings.remove("chestContourPath");person.settings.remove("chestContour");
        }else if(value==0 && displayed_parameter(person.settings,key,person.settings["side"].toInt()==0?".left":".right",source_.size())!=0){
            // The earliest one-point contour remains render-compatible; allow
            // resetting its existing effect without requiring new placement.
            parameters.remove("body.chest");
        }
    }
    if(key=="face.foreheadLift" && value!=0){
        auto candidate=person;auto masks=person.settings.value("automaticMasks").toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));candidate.settings["automaticMasks"]=masks;
        const QJsonObject support{{"landmarks",person.landmarks},{"skin",masks["faceSkin"]},{"frames",masks["eyeglassFrame"]}};
        const auto signature=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(support).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        if(person.settings.value("foreheadLiftSignature").toString()!=signature && !editing::portrait_forehead_lift_available(editing::portrait_person_json(candidate),linear_preview_.size())){status_->setText(tr("可见额头皮肤或定位不足，未应用提拉；眉线保持不变"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key].toInt());}return;}
        person.settings["automaticMasks"]=masks;person.settings["foreheadLiftSignature"]=signature;
    }
    if(key=="face.orbitalLift" && value!=0){
        auto candidate=person;auto masks=person.settings.value("automaticMasks").toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));candidate.settings["automaticMasks"]=masks;
        const QJsonObject support{{"side",person.settings.value("side")},{"landmarks",person.landmarks},{"skin",masks["faceSkin"]},{"frames",masks["eyeglassFrame"]}};
        const auto signature=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(support).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        if(person.settings.value("orbitalLiftSignature").toString()!=signature && !editing::portrait_orbital_lift_available(editing::portrait_person_json(candidate),linear_preview_.size(),person.settings.value("side").toInt())){status_->setText(tr("当前侧外眶皮肤或定位不足，未应用提拉；眼睑开度保持不变"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key+(person.settings.value("side").toInt()==0?".left":".right")].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key+(person.settings.value("side").toInt()==0?".left":".right")].toInt());}return;}
        if(person.settings.value("orbitalLiftSignature").toString()!=signature)for(int side=0;side<2;++side)person.settings[side==0?"orbitalLiftValid.left":"orbitalLiftValid.right"]=editing::portrait_orbital_lift_available(editing::portrait_person_json(candidate),linear_preview_.size(),side);
        person.settings["automaticMasks"]=masks;person.settings["orbitalLiftSignature"]=signature;
    }
    if(key=="face.perioralLift" && value!=0){
        auto candidate=person;auto masks=person.settings.value("automaticMasks").toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));candidate.settings["automaticMasks"]=masks;
        const QJsonObject support{{"side",person.settings.value("side")},{"landmarks",person.landmarks},{"skin",masks["faceSkin"]},{"frames",masks["eyeglassFrame"]}};
        const auto signature=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(support).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        if(person.settings.value("perioralLiftSignature").toString()!=signature && !editing::portrait_perioral_lift_available(editing::portrait_person_json(candidate),linear_preview_.size(),person.settings.value("side").toInt())){status_->setText(tr("当前侧口周皮肤或定位不足，未应用提拉；嘴唇轮廓保持不变"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key+(person.settings.value("side").toInt()==0?".left":".right")].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key+(person.settings.value("side").toInt()==0?".left":".right")].toInt());}return;}
        if(person.settings.value("perioralLiftSignature").toString()!=signature)for(int side=0;side<2;++side)person.settings[side==0?"perioralLiftValid.left":"perioralLiftValid.right"]=editing::portrait_perioral_lift_available(editing::portrait_person_json(candidate),linear_preview_.size(),side);
        person.settings["automaticMasks"]=masks;person.settings["perioralLiftSignature"]=signature;
    }
    if(key=="face.contourLift" && value!=0){
        const auto lm=person.landmarks;bool reliable=lm.size()>=66;for(int i=0;reliable && i<17;++i){const auto point=lm[i].toArray();if(point.size()<2 || (point.size()>2 && point[2].toDouble()<.3))reliable=false;}
        if(!reliable){status_->setText(tr("脸部轮廓定位不足，未应用提拉；请检查关键点或重新识别"));const auto suffix=person.settings.value("side").toInt()==0?".left":".right";const double previous=parameters[key+suffix].toDouble();if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(previous);}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(previous));}return;}
        auto masks=person.settings.value("automaticMasks").toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));person.settings["automaticMasks"]=masks;
    }
    if(key=="face.malarShape" && value!=0){
        const int side=person.settings.value("side").toInt();bool reliable=person.landmarks.size()>=66;
        if(reliable)for(int i:side==0?std::array<int,4>{46,35,13,62}:std::array<int,4>{41,31,3,58}){const auto point=person.landmarks[i].toArray();if(point.size()<2 || (point.size()>2 && point[2].toDouble()<.3))reliable=false;}
        if(!reliable){status_->setText(tr("当前侧面颊定位不足，未应用苹果肌调整；请检查定位或重新识别"));const double previous=parameters[key+(side==0?".left":".right")].toDouble();if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(previous);}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(previous));}return;}
        auto masks=person.settings.value("automaticMasks").toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));person.settings["automaticMasks"]=masks;
    }
    if(key=="face.midfaceLift" && value!=0){
        const int side=person.settings.value("side").toInt();bool reliable=person.landmarks.size()>=66;
        if(reliable)for(int i:side==0?std::array<int,4>{46,35,13,62}:std::array<int,4>{41,31,3,58}){const auto point=person.landmarks[i].toArray();if(point.size()<2 || (point.size()>2 && point[2].toDouble()<.3))reliable=false;}
        if(!reliable){status_->setText(tr("当前侧面颊定位不足，未应用面中提拉调整；请检查定位或重新识别"));const double previous=parameters[key+(side==0?".left":".right")].toDouble();if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(previous);}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(previous));}return;}
        auto masks=person.settings.value("automaticMasks").toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));person.settings["automaticMasks"]=masks;
    }
    if(key=="face.tearFill" && value!=0){
        auto candidate=person;auto masks=candidate.settings["automaticMasks"].toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(candidate)));candidate.settings["automaticMasks"]=masks;
        auto analysis=person.settings.value("tearVolume").toObject();if(analysis["version"].toInt()!=1 || analysis["samplingVersion"].toInt()!=2)analysis=editing::portrait_tear_volume(linear_preview_,editing::portrait_person_json(candidate));const QString side=person.settings.value("side").toInt()==0?".left":".right";
        if(!analysis["valid"+side].toBool() && !(person.settings["linked"].toBool(true) && analysis["valid"+(side==".left"?QString(".right"):QString(".left"))].toBool())){status_->setText(masks.contains("faceSkin")?tr("当前侧泪沟附近皮肤或定位不足，未应用；可检查遮挡或切换另一侧"):tr("此工程缺少独立面部皮肤范围，请重新识别后使用泪沟丰盈"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key+side].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key+side].toInt());}return;}
        person.settings["automaticMasks"]=masks;person.settings["tearVolume"]=analysis;
    }
    if(key=="face.malarTone" && value!=0){
        auto candidate=person;auto masks=candidate.settings["automaticMasks"].toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(candidate)));candidate.settings["automaticMasks"]=masks;
        auto analysis=person.settings.value("malarVolume").toObject();if(analysis["version"].toInt()!=1 || analysis["samplingVersion"].toInt()!=2)analysis=editing::portrait_malar_volume(linear_preview_,editing::portrait_person_json(candidate));const QString side=person.settings.value("side").toInt()==0?".left":".right";
        if(!analysis["valid"+side].toBool() && !(person.settings["linked"].toBool(true) && analysis["valid"+(side==".left"?QString(".right"):QString(".left"))].toBool())){status_->setText(masks.contains("faceSkin")?tr("当前侧苹果肌附近皮肤或定位不足，未应用；可检查遮挡或切换另一侧"):tr("此工程缺少独立面部皮肤范围，请重新识别后使用苹果肌丰盈"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key+side].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key+side].toInt());}return;}
        person.settings["automaticMasks"]=masks;person.settings["malarVolume"]=analysis;
    }
    if(key=="face.cheekTone" && value!=0){
        auto candidate=person;auto masks=candidate.settings["automaticMasks"].toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(candidate)));candidate.settings["automaticMasks"]=masks;
        auto analysis=person.settings.value("cheekVolume").toObject();if(analysis["version"].toInt()!=1 || analysis["samplingVersion"].toInt()!=2)analysis=editing::portrait_cheek_volume(linear_preview_,editing::portrait_person_json(candidate));const QString side=person.settings.value("side").toInt()==0?".left":".right";
        if(!analysis["valid"+side].toBool() && !(person.settings["linked"].toBool(true) && analysis["valid"+(side==".left"?QString(".right"):QString(".left"))].toBool())){status_->setText(masks.contains("faceSkin")?tr("当前侧面颊附近皮肤或定位不足，未应用；可检查遮挡或切换另一侧"):tr("此工程缺少独立面部皮肤范围，请重新识别后使用面颊丰盈"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key+side].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key+side].toInt());}return;}
        person.settings["automaticMasks"]=masks;person.settings["cheekVolume"]=analysis;
    }
    if(key=="face.orbitalTone" && value!=0){
        auto candidate=person;auto masks=candidate.settings["automaticMasks"].toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(candidate)));candidate.settings["automaticMasks"]=masks;
        auto analysis=person.settings.value("orbitalVolume").toObject();if(analysis["version"].toInt()!=1 || analysis["samplingVersion"].toInt()!=2)analysis=editing::portrait_orbital_volume(linear_preview_,editing::portrait_person_json(candidate));const QString side=person.settings.value("side").toInt()==0?".left":".right";
        if(!analysis["valid"+side].toBool() && !(person.settings["linked"].toBool(true) && analysis["valid"+(side==".left"?QString(".right"):QString(".left"))].toBool())){status_->setText(masks.contains("faceSkin")?tr("当前侧外眶附近皮肤或定位不足，未应用；可检查遮挡或切换另一侧"):tr("此工程缺少独立面部皮肤范围，请重新识别后使用外眶丰盈"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key+side].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key+side].toInt());}return;}
        person.settings["automaticMasks"]=masks;person.settings["orbitalVolume"]=analysis;
    }
    if(key=="face.perioralTone" && value!=0){
        auto candidate=person;auto masks=candidate.settings["automaticMasks"].toObject();if(!masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(candidate)));candidate.settings["automaticMasks"]=masks;
        auto analysis=person.settings.value("perioralVolume").toObject();if(analysis["version"].toInt()!=1 || analysis["samplingVersion"].toInt()!=2)analysis=editing::portrait_perioral_volume(linear_preview_,editing::portrait_person_json(candidate));const QString side=person.settings.value("side").toInt()==0?".left":".right";
        if(!analysis["valid"+side].toBool() && !(person.settings["linked"].toBool(true) && analysis["valid"+(side==".left"?QString(".right"):QString(".left"))].toBool())){status_->setText(masks.contains("faceSkin")?tr("当前侧口周附近皮肤或定位不足，未应用；可检查遮挡或切换另一侧"):tr("此工程缺少独立面部皮肤范围，请重新识别后使用口周丰盈"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key+side].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key+side].toInt());}return;}
        person.settings["automaticMasks"]=masks;person.settings["perioralVolume"]=analysis;
    }
    if(key=="face.foreheadFill" && value!=0 && person.settings.value("foreheadVolume").toObject()["version"].toInt()!=1){
        const auto illumination=editing::portrait_forehead_volume(linear_preview_,editing::portrait_person_json(person));
        if(illumination["version"].toInt()!=1){status_->setText(person.settings["automaticMasks"].toObject().contains("faceSkin")?tr("可见额头皮肤或定位不足，未应用丰盈明暗；原轮廓不变"):tr("此工程缺少独立面部皮肤范围，请重新识别后使用额头丰盈"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key].toInt());}return;}
        person.settings["foreheadVolume"]=illumination;
    }
    if(key=="hair.roots"){
        if(person.settings["scalpGeometry"].toObject()["version"].toInt()<3)person.settings["scalpGeometry"]=editing::portrait_scalp_geometry(editing::portrait_person_json(person));
        if(value!=0 && !person.settings["scalpGeometry"].toObject()["valid"].toBool()){status_->setText(tr("没有可靠的发际线，无法自动补染发根；请使用手绘染发分区"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker b(spin);spin->setValue(parameters[key].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker b(slider);slider->setValue(parameters[key].toInt());}return;}
        person.settings["hairColorVersion"]=2;
    }
    if(key=="face.forehead" || key=="head.crown" || key=="head.hairline" || key=="head.hairVolume"){
        auto masks=person.settings["automaticMasks"].toObject();
        if(!masks.contains("head")){auto automatic=person;automatic.settings["headManualEnabled"]=false;masks["head"]=editing::portrait_mask_png(editing::portrait_head_region(editing::portrait_person_json(automatic)));}
        person.settings["automaticMasks"]=masks;
        if(person.settings["scalpGeometry"].toObject()["version"].toInt()<3)person.settings["scalpGeometry"]=editing::portrait_scalp_geometry(editing::portrait_person_json(person));
        const auto scalp=person.settings["scalpGeometry"].toObject();
        if(value!=0 && (!scalp["valid"].toBool() || scalp["cropped"].toBool())){
            status_->setText(scalp["cropped"].toBool()?tr("头部外沿被裁切，不能可靠自动调整额头或颅顶"):scalp["reason"].toString());
            if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker b(spin);spin->setValue(parameters[key].toDouble());}
            if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker b(slider);slider->setValue(parameters[key].toInt());}
            return;
        }
        person.settings["scalpGeometryVersion"]=2;
    }
    if(key=="head.size" || key=="head.roll"){
        if(key=="head.roll" && person.settings["headGeometryVersion"].toInt(1)<2 && parameters.contains("head.proportion"))
            parameters["head.size"]=std::clamp(parameters["head.size"].toDouble()+parameters["head.proportion"].toDouble(),-100.,100.);
        auto masks=person.settings["automaticMasks"].toObject();
        if(!masks.contains("head"))masks["head"]=editing::portrait_mask_png(editing::portrait_head_region(editing::portrait_person_json(person)));
        if(value!=0 && !person.settings["headManualEnabled"].toBool() && QImage::fromData(QByteArray::fromBase64(masks["head"].toString().toLatin1())).isNull()){
            const double prior=parameter_display_value(key,parameters[key].toDouble());if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(prior);}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(prior));}
            status_->setText(tr("自动头部范围不足，请点击修正头部范围；其他调整可继续使用"));return;
        }
        person.settings["automaticMasks"]=masks;person.settings["headGeometryVersion"]=2;
        parameters.remove("head.proportion");
    }
    if(key=="face.volume"){
        if(value!=0 && person.landmarks.size()<66){status_->setText(tr("面部定位不足，未应用面颊轮廓调整；请重新识别"));return;}
        auto masks=person.settings.value("automaticMasks").toObject();if(value!=0 && !masks.contains("eyeglassFrame"))masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));person.settings["automaticMasks"]=masks;
        person.settings["cheekVolumeVersion"]=2;
    }
    if(key=="color.warmth" || key=="color.rosy")person.settings["colorChromaVersion"]=2;
    if(key=="color.redness")person.settings["rednessVersion"]=2;
    if(key=="skin.oil" || key=="skin.oilGloss")person.settings["oilVersion"]=2;
    if(key=="skin.texture")person.settings["textureVersion"]=2;
    if(key=="eye.circles")person.settings["eyeCirclesVersion"]=2;
    if(key=="eye.bags")person.settings["eyeBagsVersion"]=2;
    if(key.startsWith("teeth."))person.settings["teethColorVersion"]=3;
    if(key=="eye.white" || key=="eye.deRed")person.settings["eyeWhiteVersion"]=2;
    if(key=="repair.conceal")person.settings["concealVersion"]=2;
    if(key=="color.custom")person.settings["skinCustomVersion"]=2;
    if(key=="skin.uniform")person.settings["skinUniformVersion"]=2;
    if(key=="color.light")person.settings["skinLightVersion"]=2;
    if(key=="color.balance")person.settings["skinBalanceVersion"]=2;
    if(key=="hair.accessory" || key=="hair.accessoryRoughness")person.settings["hairAccessoryVersion"]=2;
    if(key=="hair.smooth")person.settings["hairSmoothVersion"]=2;
    if(key=="hair.shine")person.settings["hairShineVersion"]=2;
    if(key=="contour.highlight" || key=="contour.shadow")person.settings["contourLightVersion"]=2;
    if(key=="hair.color")person.settings["hairColorVersion"]=2;
    if(key.startsWith("makeup.blush"))editing::portrait_set_side_value(person.settings,"blushMaterialVersion",2);
    if(key.startsWith("makeup.brow"))editing::portrait_set_side_value(person.settings,"browMaterialVersion",2);
    if(key=="makeup.lash")editing::portrait_set_side_value(person.settings,"eyeLashVersion",2);
    if(key.startsWith("makeup.liner"))editing::portrait_set_side_value(person.settings,"eyeLinerVersion",2);
    if(key.startsWith("makeup.shadow"))editing::portrait_set_side_value(person.settings,"eyeShadowVersion",2);
    if(key.startsWith("makeup.iris") || key.startsWith("makeup.catch"))editing::portrait_set_side_value(person.settings,"irisMaterialVersion",2);
    if(key=="eye.red")person.settings["redEyeVersion"]=2;
    if(key=="skin.smooth" || key=="skin.bodySmooth"){person.settings["smoothVersion"]=2;if(person.settings["automaticMasks"].toObject().contains("faceSkin"))person.settings["smoothScopeVersion"]=2;}
    if(key=="body.legLength" && value!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);QString reason;const int selectedSide=person.settings.value("side").toInt();for(int side=0;side<2;++side)if(side==selectedSide || person.settings.value("linked").toBool(true)){reason=editing::portrait_leg_length_unavailable(candidate,linear_preview_.size(),side,value*.01,person.settings.value("linked").toBool(true) || std::abs(parameters[key+(selectedSide==0?".right":".left")].toDouble()-value)<1e-9);if(!reason.isEmpty())break;}
        if(!reason.isEmpty()){status_->setText(reason);const double prior=parameters[key+(selectedSide==0?".left":".right")].toDouble();if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(prior);}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(prior));}return;}}
    if(key=="body.arm" && value!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);QString reason;const int selectedSide=person.settings.value("side").toInt();for(int side=0;side<2;++side)if(side==selectedSide || person.settings.value("linked").toBool(true)){reason=editing::portrait_arm_unavailable(candidate,linear_preview_.size(),side);if(!reason.isEmpty())break;}
        if(!reason.isEmpty()){status_->setText(reason);const double prior=parameters[key+(selectedSide==0?".left":".right")].toDouble();if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(prior);}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(prior));}return;}}
    if(key=="body.straightLeg" && value!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);QString reason;const int selectedSide=person.settings.value("side").toInt();for(int side=0;side<2;++side)if(side==selectedSide || person.settings.value("linked").toBool(true)){reason=editing::portrait_straight_leg_unavailable(candidate,linear_preview_.size(),side);if(!reason.isEmpty())break;}
        if(!reason.isEmpty()){status_->setText(reason);const double prior=parameters[key+(selectedSide==0?".left":".right")].toDouble();if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(prior);}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(prior));}return;}}
    if(key=="body.height" && value!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const QJsonObject geometry{{"face",candidate["face"]},{"landmarks",candidate["landmarks"]},{"bodyAmbiguous",candidate["bodyAmbiguous"]},{"pose",person.settings.value("pose")},{"width",linear_preview_.width()},{"height",linear_preview_.height()}};
        const auto signature=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(geometry).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        if(signature!=height_validation_signature_){height_validation_signature_=signature;height_positive_valid_=editing::portrait_height_unavailable(candidate,linear_preview_.size(),1).isEmpty();height_negative_valid_=editing::portrait_height_unavailable(candidate,linear_preview_.size(),-1).isEmpty();}
        // A valid extreme covers intermediate values for the same geometry.
        // If the extreme is invalid, check the requested value instead: a
        // smaller adjustment may still fit inside the canvas.
        const auto reason=(value>0?height_positive_valid_:height_negative_valid_)?QString{}:editing::portrait_height_unavailable(candidate,linear_preview_.size(),value*.01);if(!reason.isEmpty()){status_->setText(reason);if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key].toInt());}return;}}
    if(key=="body.neck" && value!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const auto reason=editing::portrait_neck_unavailable(candidate,linear_preview_.size());if(!reason.isEmpty()){status_->setText(reason);if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key].toInt());}return;}}
    if(key=="body.back" && value!=0){auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const auto reason=editing::portrait_back_unavailable(candidate,linear_preview_.size());if(!reason.isEmpty()){status_->setText(reason);if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key].toInt());}return;}}
    if((key=="body.abdomen" || key=="body.belly") && value!=0){
        if(key=="body.belly" && person.settings.value("abdomenFront").toArray().size()!=2){status_->setText(tr("先点击“定位腹部前缘”，再在照片中点选可见腹部轮廓"));if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(parameters[key].toDouble());}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(parameters[key].toInt());}return;}
        auto candidate=editing::portrait_person_json(person);candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);const auto reason=editing::portrait_abdomen_unavailable(candidate,linear_preview_.size(),key=="body.belly");
        if(!reason.isEmpty()){status_->setText(reason);const auto previous=parameters[key].toDouble();if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(previous);}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(previous));}return;}
    }
    if(key=="body.shoulder")person.settings["shoulderGeometryVersion"]=2;
    if(key=="body.trapezius")person.settings["trapeziusGeometryVersion"]=2;
    if(key=="body.neck")person.settings["neckGeometryVersion"]=2;
    if(key=="body.height")person.settings["heightVersion"]=2;
    if(key=="body.legLength")person.settings["legLengthVersion"]=2;
    if(key=="body.waist")person.settings["waistGeometryVersion"]=2;
    if(key=="body.arm" || key=="body.upperArm" || key=="body.legWidth" || key=="body.thigh" || key=="body.calf")person.settings["limbGeometryVersion"]=2;
    if(key=="face.jawAngle" || key=="face.small" || key=="face.vShape" || key=="face.width" || key=="face.cheek" || key=="face.jaw" || key=="face.chin" || key=="face.chinWidth" || key=="face.smoothContour")person.settings["contourVersion"]=2;
    if(key.startsWith("feature.nose"))person.settings["noseGeometryVersion"]=2;
    if(key=="head.midface" || key=="head.philtrum" || key=="head.lowerface")person.settings["proportionGeometryVersion"]=2;
    if(key.startsWith("feature.brow"))person.settings["browGeometryVersion"]=2;
    if(value>0 && (key=="makeup.lipBleed" || key=="makeup.lipSparkle")){
        const QString field=key=="makeup.lipBleed"?"makeup.lipBleedWidth":"makeup.lipSparkleSize";const int initial=key=="makeup.lipBleed"?40:30;
        if(!parameters.contains(field)){parameters[field]=initial;if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+field)){const QSignalBlocker block(spin);spin->setValue(initial);}if(auto* slider=findChild<QSlider*>("portraitSlider_"+field)){const QSignalBlocker block(slider);slider->setValue(initial);}}
    }
    if(key=="makeup.lipGloss")person.settings["lipFinishVersion"]=2;
    if(key.startsWith("makeup.lip")){person.settings["mouthTopologyVersion"]=2;person.settings["lipMaterialVersion"]=2;}
    if(key.startsWith("feature.lip") || key=="feature.smile"){
        person.settings["mouthGeometryVersion"]=2;person.settings["mouthTopologyVersion"]=2;
    }
    if(key.startsWith("feature.eye") || key.startsWith("feature.brow") || key=="head.midface" || key=="head.philtrum" || key=="head.lowerface"){
        if(key.startsWith("feature.eye"))person.settings["eyeGeometryVersion"]=2;
        auto masks=person.settings["automaticMasks"].toObject();
        if(!masks.contains("eyeglassFrame")){
            masks["eyeglassFrame"]=editing::portrait_mask_png(editing::portrait_frame_protection(linear_preview_,editing::portrait_person_json(person)));
            person.settings["automaticMasks"]=masks;
        }
    }
    if(found->bilateral){const int side=parameter_side(person.settings,key);const auto suffix=side==2?".right":".left";parameters[key+suffix]=value;if(side==0){parameters[key+".left"]=value;parameters[key+".right"]=value;}}
    else parameters[key]=value;
    if(key=="face.orbitalLift")for(const auto* suffix:{".left",".right"})if(!person.settings.value(QString("orbitalLiftValid")+suffix).toBool())parameters[key+suffix]=0;
    if(key=="face.perioralLift")for(const auto* suffix:{".left",".right"})if(!person.settings.value(QString("perioralLiftValid")+suffix).toBool())parameters[key+suffix]=0;
    if(key=="face.tearFill")for(const auto* suffix:{".left",".right"})if(!person.settings["tearVolume"].toObject()[QString("valid")+suffix].toBool())parameters[key+suffix]=0;if(key=="face.malarTone")for(const auto* suffix:{".left",".right"})if(!person.settings["malarVolume"].toObject()[QString("valid")+suffix].toBool())parameters[key+suffix]=0;if(key=="face.cheekTone")for(const auto* suffix:{".left",".right"})if(!person.settings["cheekVolume"].toObject()[QString("valid")+suffix].toBool())parameters[key+suffix]=0;if(key=="face.orbitalTone")for(const auto* suffix:{".left",".right"})if(!person.settings["orbitalVolume"].toObject()[QString("valid")+suffix].toBool())parameters[key+suffix]=0;if(key=="face.perioralTone")for(const auto* suffix:{".left",".right"})if(!person.settings["perioralVolume"].toObject()[QString("valid")+suffix].toBool())parameters[key+suffix]=0;if((key.startsWith("body.") || key.startsWith("feature.") || key=="head.yaw" || key=="head.pitch") && value!=0){
        auto candidate=editing::portrait_person_json(person);auto settings=person.settings;settings["parameters"]=parameters;candidate["settings"]=settings;candidate["bodyMask"]=editing::portrait_mask_png(person.body_mask);
        try{editing::portrait_deformation(linear_preview_.size(),QJsonObject{{"version",1},{"people",QJsonArray{candidate}}},1,{},nullptr,false);}
        catch(const std::exception& e){
            person.settings=previousSettings;
            const auto suffix=found->bilateral?(person.settings.value("side").toInt()==0?".left":".right"):"";
            const double prior=displayed_parameter(previousSettings,key,suffix,source_.size());
            if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker block(spin);spin->setValue(prior);}
            if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker block(slider);slider->setValue(int(prior));}
            status_->setText(tr("%1未应用：%2。其他调整可继续使用。").arg(found->label,QString::fromUtf8(e.what())));return;
        }
    }
    person.settings["parameters"]=parameters;if((key=="body.manualHeight" && placement_key_=="body:manualHeight") || (key=="body.manualSlim" && placement_key_=="body:manualSlim") || (key=="body.manualScale" && placement_key_=="body:manualScale")){painting_mesh_=(key=="body.manualSlim" || key=="body.manualScale")?manual_slim_placement_mesh():editing::portrait_deformation(source_.size(),recipe());update_overlay();}if(auto* spin=findChild<QDoubleSpinBox*>("portraitParam_"+key)){const QSignalBlocker b(spin);spin->setValue(parameter_display_value(key,value));}if(auto* slider=findChild<QSlider*>("portraitSlider_"+key)){const QSignalBlocker b(slider);slider->setValue(int(parameter_display_value(key,value)));}remember();status_->setText(tr("已调整人物%1 · %2 %3").arg(selected_+1).arg(key=="head.size"?tr("小头"):found->label).arg(parameter_display_value(key,value)));
}
void PortraitDialog::bind_slider(QSlider* slider,int default_value){
    slider->setProperty("resetValue",default_value);slider->installEventFilter(this);
    connect(slider,&QSlider::sliderPressed,this,[this]{finish_gesture();gesture_=true;});
    connect(slider,&QSlider::sliderReleased,this,[this]{finish_gesture();});
}
void PortraitDialog::finish_gesture(){
    if(!gesture_)return;gesture_=false;remember();schedule_preview();
}
void PortraitDialog::schedule_preview(){
    ++preview_revision_;preview_dirty_=true;detail_cancelled_=true;
    if(detail_pixels_)detail_pixels_->hide();
    if(!preview_busy_ && !preview_timer_.isActive())preview_timer_.start();
}
void PortraitDialog::start_preview(){
    if(preparing_ || preview_busy_ || !preview_dirty_)return;preview_cancelled_=false;preview_dirty_=false;preview_busy_=true;
    preview_job_revision_=preview_revision_;preview_job_epoch_=preview_epoch_;QElapsedTimer prepare;prepare.start();
    const auto source=(gesture_ || painting_ || repair_source_dragging_ || repair_drawing_ || liquify_drawing_)?fast_preview_:linear_preview_;const auto settings=preview_recipe();const auto original_size=source_.size();const int selected_person=selected_;preview_prepare_ms_=prepare.nsecsElapsed()/1e6;preview_pending_=std::async(std::launch::async,[this,source,settings,original_size,selected_person]{
        auto color_recipe=settings;auto people=color_recipe["people"].toArray();
        for(int i=0;i<people.size();++i){auto person=people[i].toObject();auto s=person["settings"].toObject();for(const auto* key:{"liquify","protection","protectionAutoAnchor","protectionAutoResidual","protectionManualAnchor","protectionManualResidual","protectionCoordinates","protectBackground","backgroundLines"})s.remove(key);auto parameters=s["parameters"].toObject();for(auto it=parameters.begin();it!=parameters.end();){if((it.key().startsWith("face.") && it.key()!="face.foreheadFill" && !it.key().startsWith("face.tearFill") && !it.key().startsWith("face.malarTone") && !it.key().startsWith("face.cheekTone") && !it.key().startsWith("face.orbitalTone") && !it.key().startsWith("face.perioralTone") && it.key()!="face.doubleChin" && it.key()!="face.volume") || it.key().startsWith("feature.") || it.key().startsWith("head.") || it.key().startsWith("body."))it=parameters.erase(it);else ++it;}s["parameters"]=parameters;person["settings"]=s;people[i]=person;}color_recipe["people"]=people;
        QElapsedTimer stage;stage.start();auto& cache=color_cache_[source.size()==linear_preview_.size()?1:0];
        if(cache.source_key!=source.cacheKey() || cache.recipe!=color_recipe){++preview_color_passes_;auto colored=editing::portrait_color_region(source,color_recipe,source.rect(),1,&preview_cancelled_);if(preview_cancelled_)throw editing::EditError("cancelled");cache.image=std::move(colored);cache.source_key=source.cacheKey();cache.recipe=color_recipe;}
        preview_color_ms_=stage.nsecsElapsed()/1e6;stage.restart();
        // Fast and settled previews sample one original-coordinate target field.
        if(geometry_cache_.source_size!=original_size || geometry_cache_.person!=selected_person || geometry_cache_.recipe!=settings){
            ++preview_geometry_passes_;QImage manual;editing::PortraitManualInputs inputs;auto mesh=editing::portrait_deformation_with_manual(original_size,settings,selected_person,manual,&preview_cancelled_,&inputs);
            if(preview_cancelled_)throw editing::EditError("cancelled");
            geometry_cache_.mesh=std::move(mesh);geometry_cache_.manual=std::move(manual);geometry_cache_.raw_manual=std::move(inputs.mesh);geometry_cache_.manual_anchor=std::move(inputs.anchor);geometry_cache_.manual_residual=std::move(inputs.residual);geometry_cache_.person=selected_person;geometry_cache_.recipe=settings;geometry_cache_.source_size=original_size;
        }
        preview_geometry_ms_=stage.nsecsElapsed()/1e6;stage.restart();auto image=editing::display_image(editing::apply_liquify(cache.image,geometry_cache_.mesh,&preview_cancelled_));preview_warp_ms_=stage.nsecsElapsed()/1e6;return image;});
}
void PortraitDialog::remember() {
    if(restoring_)return;
    if(preset_active_){schedule_preview();return;}
    if(gesture_){schedule_preview();return;}
    if(history_index_>=0){QJsonArray before,after;for(const auto& p:history_[std::size_t(history_index_)])before.append(editing::portrait_person_json(p));for(const auto& p:people_)after.append(editing::portrait_person_json(p));if(before==after)return;}
    history_.resize(std::size_t(history_index_+1));history_.push_back(people_);
    if(history_.size()>64)history_.erase(history_.begin());history_index_=int(history_.size())-1;
    undo_->setEnabled(history_index_>0);redo_->setEnabled(false);
    schedule_preview();
}
void PortraitDialog::history_step(int direction) {
    finish_preset(true);finish_gesture();++preview_epoch_;
    QHash<QUuid,QJsonObject> navigation;for(const auto& p:people_)navigation[p.id]=QJsonObject{{"side",p.settings["side"]},{"linked",p.settings["linked"]},{"featureSides",p.settings["featureSides"]}};
    const int target=history_index_+direction;if(busy_ || target<0 || target>=int(history_.size()))return;
    restoring_=true;history_index_=target;people_=history_[std::size_t(target)];for(auto& p:people_)if(navigation.contains(p.id)){for(const auto* key:{"side","linked","featureSides"}){const QJsonValue value=navigation.value(p.id).value(key);if(value.isUndefined())p.settings.remove(key);else p.settings[key]=value;}}populate();restoring_=false;
    undo_->setEnabled(target>0);redo_->setEnabled(target+1<int(history_.size()));
    status_->setText(direction<0?tr("已撤销本次人像操作"):tr("已重做本次人像操作"));
    schedule_preview();
}
void PortraitDialog::update_detail() {
    if(preparing_ || gesture_ || painting_ || repair_source_dragging_ || repair_drawing_ || liquify_drawing_ || preview_dirty_ || preview_busy_)return;
    if(detail_busy_){detail_cancelled_=true;detail_timer_.start();return;}
    const double ratio=double(source_.width())/preview_.width();
    if(ratio<=1.01 || view_->transform().m11()<ratio*.7){detail_pixels_->hide();return;}
    auto visible=view_->mapToScene(view_->viewport()->rect()).boundingRect();
    auto area=QRectF(visible.x()*ratio,visible.y()*ratio,visible.width()*ratio,visible.height()*ratio).toAlignedRect().intersected(source_.rect());
    if(area.isEmpty() || qint64(area.width())*area.height()>16'000'000){detail_pixels_->hide();return;}
    detail_area_=area;detail_cancelled_=false;detail_busy_=true;const auto source=source_;const auto settings=comparing_?entry_recipe_:locate_->isChecked()?QJsonObject{}:preview_recipe();
    detail_pending_=std::async(std::launch::async,[this,source,settings,area]{return editing::display_image(editing::portrait_region(source,settings,area,&detail_cancelled_));});
}
QJsonObject PortraitDialog::preview_recipe() const {
    auto result=recipe();if(group_bypass_.isEmpty() || selected_<0)return result;
    auto records=result["people"].toArray();auto person=records[selected_].toObject();auto settings=person["settings"].toObject();auto disabled=settings["disabledParameters"].toObject();
    for(const auto& key:group_bypass_)disabled[key]=true;settings["disabledParameters"]=disabled;person["settings"]=settings;records[selected_]=person;result["people"]=records;return result;
}
QJsonObject PortraitDialog::recipe() const {
    QJsonArray records;
    for(const auto& p:people_){auto record=editing::portrait_person_json(p);
        if(selected_>=0 && p.id==people_[std::size_t(selected_)].id){auto settings=record["settings"].toObject();
            if(painting_){const auto key=range_key_->currentData().toString();store_range(settings,key,painting_mask_);}
            if(repair_drawing_ && !repair_live_.isEmpty()){auto repairs=settings["repairs"].toArray();repairs.append(repair_live_);settings["repairs"]=repairs;}
            record["settings"]=settings;
        }
        if(liquify_drawing_ && selected_>=0 && p.id==people_[std::size_t(selected_)].id){auto settings=record["settings"].toObject();settings["liquify"]=editing::liquify_json(liquify_mesh_);if(!liquify_protection_[0].isNull())settings["protectionManualAnchor"]=editing::liquify_json(liquify_protection_[0]);if(!liquify_protection_[1].isNull())settings["protectionManualResidual"]=editing::liquify_json(liquify_protection_[1]);record["settings"]=settings;}if(!p.body_mask.isNull()){const auto key=p.body_mask.cacheKey();if(!mask_cache_.contains(key)){QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);p.body_mask.save(&buffer,"PNG");mask_cache_[key]=QString::fromLatin1(png.toBase64());}record["bodyMask"]=mask_cache_.value(key);}records.append(record);}
    return {{"version",1},{"sourceHash",QString::fromLatin1(source_hash_)},{"people",records},{"models",QJsonObject{{"landmarks","OpenSeeFace-66:50c0974"},{"skin","MediaPipe-selfie-multiclass:35ec1ecd"},{"pose","MediaPipe-pose:9d89c599"},{"skinRefinement",2}}}};
}
QJsonObject PortraitDialog::state() const {return {{"ready",!preparing_ && !busy_ && !repair_analysis_busy_ && !volume_analysis_busy_},{"selected",selected_},{"count",int(people_.size())},{"focusMode",focus_mode_},{"zoom",view_->transform().m11()},{"recipe",recipe()},{"status",status_->text()},{"panel",navigation_->currentRow()},{"history",history_index_},{"comparing",comparing_},{"detail",detail_pixels_->isVisible()},{"previewFrames",qint64(preview_frames_)},{"previewTimings",QJsonObject{{"prepareMs",preview_prepare_ms_},{"colorMs",preview_color_ms_.load()},{"geometryMs",preview_geometry_ms_.load()},{"warpMs",preview_warp_ms_.load()}}},{"previewColorPasses",qint64(preview_color_passes_.load())},{"previewGeometryPasses",qint64(preview_geometry_passes_.load())},{"previewRevision",qint64(preview_revision_)},{"displayedRevision",preview_displayed_revision_},{"previewBusy",preview_busy_ || preview_dirty_},{"gesture",gesture_}};}
}
