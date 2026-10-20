#include <QApplication>
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
QJsonObject filmstrip_command(QDialog* dialog,const QJsonObject& request){
    auto* list=dialog->findChild<QListWidget*>("rawFiles");
    if(request.contains("row")){
        auto* item=list->item(request.value("row").toInt());list->scrollToItem(item);QApplication::processEvents();
        const auto rect=list->visualItemRect(item);
        const QPointF point=request.contains("star")?QPointF(rect.center().x()-45+(request.value("star").toInt()-.5)*18,rect.bottom()-10):QPointF(rect.center().x(),rect.top()+40);
        const auto modifiers=Qt::KeyboardModifiers(request.value("modifiers").toInt());
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
            QMouseEvent event(type,point,list->viewport()->mapToGlobal(point.toPoint()),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);
            QApplication::sendEvent(list->viewport(),&event);
        }
        QApplication::processEvents();
    }
    if(request.contains("key")){
        list->setFocus();const auto combination=QKeySequence(request.value("key").toString())[0];
        for(auto type:{QEvent::KeyPress,QEvent::KeyRelease}){QKeyEvent event(type,combination.key(),combination.keyboardModifiers());QApplication::sendEvent(list,&event);}
    }
    QJsonArray selected,hidden,ratings;
    for(int i=0;i<list->count();++i){auto* item=list->item(i);if(item->isSelected())selected.append(i);if(item->isHidden())hidden.append(i);ratings.append(item->data(Qt::UserRole+10).toInt());}
    return {{"selected",selected},{"hidden",hidden},{"ratings",ratings},{"current",list->currentRow()},{"count",dialog->findChild<QLabel*>("rawFilmCount")->text()}};
}
