#include "editor_controller.h"
#include "canvas_view.h"
#include "main_window.h"
#include "portrait_dialog.h"
#include "work_window.h"
#include <QPointer>
#include <QFile>
#include <QCryptographicHash>
namespace huelith::desktop {
void EditorController::show_portrait() {
    auto* c=canvas();if(!c)return;c->commit_transform();
    const auto id=c->active_layer;const auto* layer=editing::find_layer(c->project().state().layers,id);
    if(!layer || layer->pixels.isNull() || editing::layer_locked(c->project().state().layers,id))
        throw editing::EditError(tr("请选择未锁定的照片图层"));
    auto effects=layer->effects;editing::Effect effect;effect.name=tr("人像精修");
    int slot=-1;
    for(std::size_t i=0;i<effects.size();++i)if(!effects[i].portrait.isEmpty()){slot=int(i);effect=effects[i];break;}
    auto source=layer->pixels;
    const auto preceding=slot>=0?std::vector<editing::Effect>(effects.begin(),effects.begin()+slot):effects;
    const auto entry_state=c->project().state();const auto revision=c->project().revision();QPointer<CanvasView> guard(c);
    PortraitDialog dialog(source,effect.portrait,window_,[preceding](const QImage& image){return preceding.empty()?image:editing::apply_effects(image,preceding);});
    if(run_work_window(dialog,window_)!=QDialog::Accepted)return;
    if(!guard || guard->project().revision()!=revision){
        auto copy=entry_state;copy.name+=tr(" · 人像精修副本");CanvasSpec spec;spec.width=copy.size.width();spec.height=copy.size.height();spec.resolution_ppi=copy.ppi;spec.name=copy.name.toStdString();
        if(!window_->create_document(spec))throw editing::EditError(tr("无法创建人像精修副本"));guard=window_->active_canvas();guard->load_state(std::move(copy),false);guard->active_layer=id;guard->selected_layers={id};
    }
    const auto recipe=dialog.recipe();
    if(recipe!=effect.portrait){effect.portrait=recipe;
        int edited_people=0,items=0;for(const auto& value:recipe["people"].toArray()){const auto settings=value.toObject()["settings"].toObject();int count=0;const auto parameters=settings["parameters"].toObject();for(auto i=parameters.begin();i!=parameters.end();++i)if(i.value().toDouble()!=0)++count;count+=int(settings["repairs"].toArray().size());if(!settings["liquify"].toObject().isEmpty())++count;if(count){++edited_people;items+=count;}}effect.name=tr("人像精修 · %1 人 · %2 项").arg(edited_people).arg(items);
        if(slot>=0)effects[std::size_t(slot)]=effect;else effects.push_back(effect);
        guard->edit(tr("人像精修"),[&](editing::State& state){editing::find_layer(state.layers,id)->effects=effects;});}
    for(const auto& entry:dialog.property("syncTargets").toJsonArray()){
        const auto target=entry.toObject();QFile file(target["path"].toString());if(!file.open(QIODevice::ReadOnly))throw editing::EditError(tr("同步照片已不可读"));QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(&file);if(QString::fromLatin1(hash.result().toHex())!=target["fileHash"].toString())throw editing::EditError(tr("同步照片已经改变，请重新选择目标；未覆盖原片"));if(!window_->open_document(target["path"].toString()))continue;
        auto* destination=window_->active_canvas();if(!destination)continue;const auto layer_id=destination->active_layer;
        destination->edit(tr("同步人像设置"),[&](editing::State& state){auto* photo=editing::find_layer(state.layers,layer_id);if(!photo)return;editing::Effect synced;synced.name=tr("人像精修");synced.portrait=target["recipe"].toObject();photo->effects.push_back(synced);});
    }
    refresh();
}
}
