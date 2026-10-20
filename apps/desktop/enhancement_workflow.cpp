#include "work_window.h"
#include "editor_controller.h"
#include "canvas_view.h"
#include "main_window.h"
#include "huelith/editing/enhancement.h"
#include "huelith/editing/image_storage.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <future>

namespace huelith::desktop {
namespace {
bool insert_enhanced_layer(std::vector<editing::Layer>& layers, const QUuid& id,
                           const editing::Layer& enhanced) {
    for (std::size_t i=0;i<layers.size();++i) {
        if (layers[i].id==id) {
            layers[i].visible=false;
            layers.insert(layers.begin()+std::ptrdiff_t(i+1),enhanced);
            return true;
        }
        if(insert_enhanced_layer(layers[i].children,id,enhanced))return true;
    }
    return false;
}
QImage layer_depth_selection(const editing::State& source, const QUuid& id, const QImage& depth, const std::atomic_bool* cancelled) {
    using namespace editing;
    if(depth.isNull())throw EditError(QStringLiteral("请先计算图层深度"));
    const auto* selected=find_layer(source.layers,id);
    auto pixels=allocate_image(selected->pixels.size(),QImage::Format_RGBA32FPx4_Premultiplied);
    pixels.setColorSpace(working_space());
    for(int y=0;y<pixels.height();++y) {
        if(cancelled && cancelled->load())throw EditError(QStringLiteral("已取消"));
        const auto* input=reinterpret_cast<const float*>(selected->pixels.constScanLine(y));
        auto* row=reinterpret_cast<float*>(pixels.scanLine(y));
        const auto* mask=depth.constScanLine(y);
        for(int x=0;x<pixels.width();++x) {
            const float a=input[x*4+3]*mask[x]/255.F;
            for(int c=0;c<4;++c)row[x*4+c]=a;
        }
    }
    // Preserve ancestor geometry/masks, exclude siblings, colour effects and clipping backdrop.
    std::function<std::vector<Layer>(const std::vector<Layer>&)> isolate;
    isolate=[&](const std::vector<Layer>& layers)->std::vector<Layer>{
        for(const auto& original:layers) {
            auto layer=original;
            if(layer.id==id){layer.pixels=pixels;layer.children.clear();layer.kind=LayerKind::pixels;}
            else {layer.children=isolate(layer.children);if(layer.children.empty())continue;layer.kind=LayerKind::group;}
            layer.visible=true;layer.background=false;layer.clipped=false;layer.opacity=1;
            layer.blend=Blend::normal;layer.effects.clear();layer.styles={};layer.tone={};layer.exposure=0;
            return {std::move(layer)};
        }
        return {};
    };
    State state;state.name=source.name;state.ppi=source.ppi;state.size=source.size;state.layers=isolate(source.layers);
    RenderOptions options;options.cancelled=cancelled;
    const auto projected=render(state,options);
    auto mask=allocate_image(state.size,QImage::Format_Grayscale8);
    for(int y=0;y<mask.height();++y){if(cancelled && cancelled->load())throw EditError(QStringLiteral("已取消"));const auto* row=reinterpret_cast<const float*>(projected.constScanLine(y));
        auto* output=mask.scanLine(y);for(int x=0;x<mask.width();++x)output[x]=uchar(qRound(std::clamp(row[x*4+3],0.F,1.F)*255));}
    return mask;
}
class RegionLabel final : public QLabel {
  public:
    using QLabel::QLabel;
    std::function<void(QPointF)> clicked;
    void mousePressEvent(QMouseEvent* event) override {
        if (clicked && !pixmap().isNull()) {
            const auto size = pixmap().deviceIndependentSize();
            const auto origin = (QPointF(width(), height()) - QPointF(size.width(), size.height())) / 2;
            clicked(QPointF(std::clamp((event->position().x() - origin.x()) / size.width(), 0., 1.),
                            std::clamp((event->position().y() - origin.y()) / size.height(), 0., 1.)));
        }
    }
};
}
void EditorController::configure_enhancement_tools() {
    auto* automate = window_->menus_->actions().first()->menu()->addMenu(tr("自动"));
    for (int mode = 0; mode < 3; ++mode) {
        auto* action = automate->addAction(mode == 0 ? tr("合并到 HDR…") : mode == 1 ? tr("Photomerge 全景…") : tr("景深合成…"));
        connect(action, &QAction::triggered, this, [this, mode] { guarded([&] { show_photo_merge(mode); }); });
    }
    for (bool selected_layer : {false, true}) {
    auto* image_menu = window_->menus_->actions().at(selected_layer ? 3 : 2)->menu();
    auto* menu = image_menu->addMenu(tr("AI 增强"));
    menu->setObjectName(selected_layer ? "layerEnhancementMenu" : "imageEnhancementMenu");
    for (int mode = 0; mode < 3; ++mode) {
        auto* action = menu->addAction(mode == 0 ? tr("超分辨率…") : mode == 1 ? tr("RGB AI 降噪…") : tr("深度范围蒙版…"));
        action->setObjectName(QString(selected_layer ? "layerEnhancement%1" : "imageEnhancement%1").arg(mode));
        connect(action, &QAction::triggered, this, [this, mode, selected_layer] { guarded([&] { show_enhancement(mode, selected_layer); }); });
    }
    }
}
void EditorController::show_enhancement(int mode, bool selected_layer) {
    using namespace editing;
    auto* current = canvas();
    if (!current) return;
    current->commit_transform();
    const auto target_id = current->active_layer;
    if (selected_layer && current->selected_layers.size() > 1) throw EditError(tr("请只选择一个照片图层进行 AI 增强"));
    const QPointer<CanvasView> document(current);
    const auto snapshot = current->project().snapshot();
    const auto* target = selected_layer ? find_layer(snapshot.state.layers, target_id) : nullptr;
    if (selected_layer && (!target || target->pixels.isNull() || (target->kind != LayerKind::pixels && target->kind != LayerKind::source && target->kind != LayerKind::repair)))
        throw EditError(tr("请选择照片或像素图层；文字、形状和组请先显式栅格化"));
    if (selected_layer && layer_locked(snapshot.state.layers, target_id)) throw EditError(tr("请先解锁所选图层及其父组"));
    const bool depth = mode == 2;
    QDialog dialog(window_);
    dialog.setObjectName("enhancementDialog");
    dialog.setWindowTitle(mode == 0 ? tr("超分辨率") : depth ? tr("深度范围蒙版") : tr("RGB AI 降噪"));
    dialog.resize(900, 720);
    QVBoxLayout layout(&dialog);
    QLabel explanation(depth ? tr("按相对深度选择范围；白色为选中。完成后可用选区工具继续加减、羽化和创建蒙版。") :
        tr("处理当前可见图像，生成可继续编辑的新文档。点击缩略图选取预览位置；原文档保留。"));
    if (selected_layer) explanation.setText(depth ? tr("仅根据所选图层生成深度选区，按图层变换映射到画布并保留透明范围。") :
        tr("仅增强所选图层的源像素。生成增强图层并隐藏原层；保留调整、效果、蒙版及混合设置。超分保持画布上的尺寸和位置，增加源像素分辨率。"));
    explanation.setWordWrap(true);
    layout.addWidget(&explanation);
    QFormLayout form;
    QComboBox model;
    model.setObjectName("enhancementModel");
    model.addItems(mode == 0 ? QStringList{tr("内置 Real-ESRGAN · 快速"), tr("内置 Real-ESRGAN x4plus · 高画质")} :
                   QStringList{depth ? tr("内置 MiDaS · 相对深度") : tr("内置 NAFNet · 照片降噪")});
    form.addRow(tr("模型"), &model);
    QComboBox scale;
    scale.setObjectName("enhancementScale");
    scale.addItems({"2×", "4×"});
    if (mode == 0) form.addRow(tr("放大倍数"), &scale);
    QSpinBox strength;
    strength.setObjectName("enhancementStrength");
    strength.setRange(0, 100); strength.setValue(70); strength.setSuffix(" %");
    if (mode == 1) form.addRow(tr("降噪强度"), &strength);
    QComboBox provider;
    provider.setObjectName("enhancementProvider");
    provider.addItems({tr("GPU（失败时重试 CPU）"), tr("CPU")});
    form.addRow(tr("处理器"), &provider);
    QSpinBox near, far, feather;
    near.setObjectName("depthNear"); far.setObjectName("depthFar"); feather.setObjectName("depthFeather");
    near.setRange(0, 100); near.setValue(50); far.setRange(0, 100); far.setValue(100);
    feather.setRange(0, 50); feather.setValue(10);
    QCheckBox invert(tr("反选深度范围"));invert.setObjectName("depthInvert");
    if (depth) { form.addRow(tr("范围下限（远 → 近）"), &near); form.addRow(tr("范围上限"), &far); form.addRow(tr("过渡"), &feather); }
    if(depth)form.addRow(&invert);
    layout.addLayout(&form);
    RegionLabel thumbnail;
    thumbnail.setObjectName("enhancementThumbnail");
    thumbnail.setAlignment(Qt::AlignCenter);
    if (selected_layer) thumbnail.setPixmap(QPixmap::fromImage(display_image(target->pixels.scaled(360,180,Qt::KeepAspectRatio,Qt::SmoothTransformation))));
    else {const auto proxy = preview_state(snapshot.state, 360);
    thumbnail.setPixmap(QPixmap::fromImage(display_image(render(proxy, {double(proxy.size.width()) / snapshot.state.size.width()}))));}
    thumbnail.setMaximumHeight(180);
    thumbnail.setPixmap(thumbnail.pixmap().scaled(360, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout.addWidget(&thumbnail);
    QPointF location(.5, .5);
    QLabel status(tr("尚未处理"));
    status.setObjectName("enhancementStatus");
    status.setWordWrap(true);
    thumbnail.clicked = [&](QPointF point) { location = point; status.setText(tr("预览位置 %1%, %2%：点击预览更新").arg(qRound(point.x() * 100)).arg(qRound(point.y() * 100))); };
    QScrollArea scroll;
    QLabel preview;
    preview.setObjectName("enhancementPreview");
    preview.setAlignment(Qt::AlignCenter);
    scroll.setWidget(&preview);
    scroll.setWidgetResizable(false);
    scroll.setMinimumHeight(230);
    layout.addWidget(&scroll, 1);
    QCheckBox before(tr("显示处理前（相同输出比例）"));
    before.setObjectName("enhancementBefore");
    if (!depth) layout.addWidget(&before);
    layout.addWidget(&status);
    QDialogButtonBox buttons(QDialogButtonBox::Cancel);
    auto* inspect = buttons.addButton(depth ? tr("计算深度") : tr("100% 局部预览"), QDialogButtonBox::ActionRole);
    inspect->setObjectName("enhancementInspect");
    auto* apply = buttons.addButton(depth ? tr("创建选区") : tr("增强并新建文档"), QDialogButtonBox::AcceptRole);
    apply->setObjectName("enhancementApply");
    if (selected_layer && !depth) apply->setText(tr("增强并生成图层"));
    if (depth) apply->setEnabled(false);
    layout.addWidget(&buttons);
    std::atomic_bool cancelled{false};
    std::atomic_int completed{0}, total{0};
    std::future<AiResult> job;
    QImage original, before_image, after_image, depth_map, chosen_mask;
    std::optional<AiResult> final_result;
    bool full_job{};
    auto show_preview = [&] {
        const auto image = before.isChecked() ? before_image : after_image;
        auto pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(preview.devicePixelRatioF());
        preview.setPixmap(pixmap); preview.resize(pixmap.deviceIndependentSize().toSize());
    };
    auto depth_range = [&] {
        if (depth_map.isNull()) return;
        chosen_mask = QImage(depth_map.size(), QImage::Format_Grayscale8);
        const double low = near.value() / 100., high = far.value() / 100., smooth = feather.value() / 100.;
        for (int y = 0; y < depth_map.height(); ++y)
            for (int x = 0; x < depth_map.width(); ++x) {
                const double value = depth_map.constScanLine(y)[x] / 255.;
                const double alpha = low > high ? 0 : smooth == 0 ? double(value >= low && value <= high) :
                    std::clamp(std::min((value - low + smooth) / smooth, (high + smooth - value) / smooth), 0., 1.);
                chosen_mask.scanLine(y)[x] = uchar(qRound((invert.isChecked()?1-alpha:alpha) * 255));
            }
        after_image = chosen_mask.scaled(720, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        show_preview();
    };
    for (auto* spin : {&near, &far, &feather}) connect(spin, &QSpinBox::valueChanged, &dialog, depth_range);
    connect(&invert,&QCheckBox::toggled,&dialog,depth_range);
    connect(&before, &QCheckBox::toggled, &dialog, show_preview);
    auto start = [&](bool full) {
        if (job.valid()) return;
        full_job = full;
        cancelled = false; completed = 0; total = 0;
        EnhanceOptions options;
        options.mode = EnhanceMode(mode); options.scale = scale.currentIndex() == 0 ? 2 : 4;
        options.quality = model.currentIndex() == 1; options.directml = provider.currentIndex() == 0;
        options.strength = mode == 1 ? strength.value() / 100. : 1.;
        if(selected_layer && mode==0)for(const auto& effect:target->effects)if(effect.blur_radius*options.scale>1000){
            status.setText(tr("现有模糊效果在超分后超过支持半径；请先降低该效果半径或显式栅格化后再增强。"));return;
        }
        inspect->setEnabled(false); apply->setEnabled(false);
        status.setText(tr("准备图像…"));
        dialog.setProperty("enhancementRunning", true);
        const auto point = location;
        job = std::async(std::launch::async, [&, options, full, point] {
            if (original.isNull()) {
                RenderOptions rendering;
                rendering.cancelled = &cancelled;
                original = selected_layer ? target->pixels : render(snapshot.state, rendering);
            }
            QImage input = original;
            if (!full && !depth) {
                const int extent = 160;
                const int x = std::clamp(qRound(point.x() * original.width()) - extent / 2, 0, std::max(0, original.width() - extent));
                const int y = std::clamp(qRound(point.y() * original.height()) - extent / 2, 0, std::max(0, original.height() - extent));
                input = original.copy(QRect(x, y, std::min(extent, original.width()), std::min(extent, original.height())));
            }
            auto result = enhance_image(input, options, &cancelled, [&](int done, int count) { completed = done; total = count; });
            if (!full && !depth) result.reference = input.scaled(result.image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            return result;
        });
    };
    bool pending_preview{};
    thumbnail.clicked=[&](QPointF point){location=point;if(!depth){pending_preview=true;dialog.setProperty("enhancementReady",false);}};
    connect(inspect, &QPushButton::clicked, &dialog, [&] { pending_preview=false;start(false); });
    connect(apply, &QPushButton::clicked, &dialog, [&] {
        if(depth && selected_layer) {
            if(job.valid() || chosen_mask.isNull())return;
            full_job=true;cancelled=false;completed=0;total=0;
            inspect->setEnabled(false);apply->setEnabled(false);
            status.setText(tr("正在将图层深度映射到画布…可随时取消"));
            dialog.setProperty("enhancementRunning",true);
            const auto mask=chosen_mask;
            job=std::async(std::launch::async,[&,mask]{return AiResult{layer_depth_selection(snapshot.state,target_id,mask,&cancelled),{}, {}};});
        } else if(depth)dialog.accept();else start(true);
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QTimer timer;
    timer.setInterval(30);
    connect(&timer, &QTimer::timeout, &dialog, [&] {
        if (!job.valid()) {if(pending_preview){pending_preview=false;start(false);}return;}
        if (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            if (total > 0) status.setText(tr("处理进度 %1 / %2；可随时取消").arg(completed.load()).arg(total.load()));
            return;
        }
        dialog.setProperty("enhancementRunning", false);
        inspect->setEnabled(true); apply->setEnabled(!depth || !depth_map.isNull());
        try {
            auto result = job.get();
            if (full_job) { final_result = std::move(result); dialog.accept(); return; }
            if (depth) { depth_map = std::move(result.image); depth_range(); apply->setEnabled(true); }
            else {
                after_image = display_image(result.image); before_image = display_image(result.reference); show_preview();
            }
            status.setText(result.metadata["cpuFallback"].toBool() ? tr("GPU 失败，CPU 重试成功。预览已更新。") : tr("预览已更新。"));
            dialog.setProperty("enhancementReady", true);
        } catch (const std::exception& error) { status.setText(QString::fromUtf8(error.what())); }
    });
    timer.start();
    const int accepted = run_work_window(dialog, window_);
    cancelled = true;
    if (job.valid()) { try { job.get(); } catch (...) {} }
    if (accepted != QDialog::Accepted) return;
    if (depth && selected_layer) {if(!final_result)return;chosen_mask=std::move(final_result->image);}
    if (depth) {
        if (!document || document->project().revision() != snapshot.revision) {
            auto state=snapshot.state;state.selection=chosen_mask;
            state.name+=tr(" · 深度选区副本");
            CanvasSpec spec;spec.width=state.size.width();spec.height=state.size.height();
            spec.resolution_ppi=state.ppi;spec.name=state.name.toStdString();
            if(!window_->create_document(spec))throw EditError(tr("无法创建深度选区副本"));
            canvas()->load_state(std::move(state),false);refresh();return;
        }
        if (!chosen_mask.isNull()) document->edit(tr("深度范围选区"), [&](State& state) { state.selection = chosen_mask; });
        return;
    }
    if (!final_result) return;
    if (selected_layer) {
        auto result_layer = duplicate_layer(*target);
        result_layer.name += mode == 0 ? tr(" · 超分") : tr(" · 降噪");
        result_layer.kind = LayerKind::pixels; result_layer.background = false;
        result_layer.linked_path.clear(); result_layer.linked_hash.clear();
        result_layer.raw_source.clear(); result_layer.raw_recipe = {};
        result_layer.pixels = std::move(final_result->image);
        const double ratio = double(result_layer.pixels.width()) / target->pixels.width();
        result_layer.transform = QTransform::fromScale(1 / ratio, 1 / ratio) * target->transform;
        for (auto& effect : result_layer.effects) effect.blur_radius *= ratio;
        const auto new_id = result_layer.id;
        const auto insert = [&](State& state) { insert_enhanced_layer(state.layers, target_id, result_layer); };
        if (document && document->project().revision() == snapshot.revision) {
            document->edit(tr("图层 AI 增强"), insert);
            document->active_layer = new_id; document->selected_layers = {new_id};
        } else {
            auto copy = snapshot.state; insert(copy); copy.name += tr(" · 图层增强副本");
            CanvasSpec spec; spec.width=copy.size.width(); spec.height=copy.size.height();
            spec.resolution_ppi=copy.ppi; spec.name=copy.name.toStdString();
            if(!window_->create_document(spec)) throw EditError(tr("无法创建增强副本"));
            canvas()->load_state(std::move(copy),false);canvas()->active_layer=new_id;canvas()->selected_layers={new_id};
        }
        refresh();return;
    }
    State state;
    state.metadata = snapshot.state.metadata;
    state.name = snapshot.state.name + (mode == 0 ? tr(" · 超分") : tr(" · 降噪"));
    state.size = final_result->image.size(); state.ppi = snapshot.state.ppi;
    Layer layer;
    layer.source_metadata = state.metadata;
    layer.name = state.name; layer.pixels = std::move(final_result->image);
    state.layers.push_back(std::move(layer));
    CanvasSpec spec;
    spec.width = state.size.width(); spec.height = state.size.height(); spec.resolution_ppi = state.ppi;
    spec.name = state.name.toStdString();
    if (!window_->create_document(spec)) throw EditError(tr("无法创建增强文档"));
    canvas()->load_state(std::move(state), false);
    refresh();
}
} // namespace huelith::desktop
