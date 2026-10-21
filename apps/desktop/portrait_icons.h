#pragma once
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QSvgRenderer>
#include <QStyledItemDelegate>

namespace huelith::desktop {
class PortraitNavigationDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem item(option);initStyleOption(&item,index);
        const bool selected=item.state & QStyle::State_Selected;
        painter->save();painter->setRenderHint(QPainter::Antialiasing);
        if(index.data(Qt::UserRole+1).toBool()){painter->fillRect(item.rect,QColor("#252a2e"));painter->setPen(QColor("#3c4349"));painter->drawLine(item.rect.left()+20,item.rect.top()+3,item.rect.right()-20,item.rect.top()+3);item.rect.adjust(0,8,0,0);}
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected?QColor("#2f80ed"):QColor("#252a2e"));
        painter->drawRoundedRect(item.rect,selected?4:0,selected?4:0);
        const QRect icon(item.rect.left()+24,item.rect.center().y()-12,24,24);
        item.icon.paint(painter,icon,Qt::AlignCenter,selected?QIcon::Selected:QIcon::Normal);
        painter->setFont(item.font);painter->setPen(selected?Qt::white:QColor("#e1e5e8"));
        painter->drawText(item.rect.adjusted(60,0,-12,0),Qt::AlignVCenter|Qt::AlignLeft,item.text);
        painter->restore();
    }
};
// A shared 32-unit grid keeps portrait tasks optically consistent.
// Explicit raster sizes cover fractional Windows scaling without stretching a
// single 24px bitmap; selected and disabled states retain their own contrast.
inline QIcon portrait_liquify_icon(int tool) {
    static const char* paths[]{
        "M26 16H6m8-8-8 8 8 8",
        "M7 12a10 10 0 1 1 0 10M7 5v8h8",
        "M4 12q6-9 12 0t12 0M4 22q6-9 12 0t12 0",
        "M25 10a12 12 0 1 0 2 12M25 3v8h-8M20 16a4 4 0 1 1-4-4",
        "M3 16h9m-5-5 5 5-5 5M29 16h-9m5-5-5 5 5 5",
        "M12 16H3m5-5-5 5 5 5M20 16h9m-5-5 5 5-5 5",
        "M27 16H5m8-8-8 8 8 8M27 7v18",
        "M16 3v26M5 9l22 14M5 23 27 9M12 5l4 4 4-4M12 27l4-4 4 4M5 14l6-2-1-6M27 18l-6 2 1 6",
        "M6 20 19 5q2-2 4 0l4 4q2 2 0 4L14 27H9l-3-3q-2-2 0-4ZM13 12l9 9"};
    if(tool<0 || tool>=9)return {};
    const auto svg=QString("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><path d='%1' fill='none' stroke='#e1e5e8' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/></svg>").arg(paths[tool]).toUtf8();
    QSvgRenderer renderer(svg);QIcon icon;for(int size:{24,32,48,64}){QPixmap p(size,size);p.fill(Qt::transparent);QPainter painter(&p);renderer.render(&painter);painter.end();icon.addPixmap(p);}return icon;
}
inline QIcon portrait_task_icon(int task) {
    static const char* names[]{"volume","hair","makeup","face","skin","blemish","wrinkle","oil","body","slim","erase","smallhead","contour","teeth","eye"};
    if(task<0 || task>=15)return {};
    QFile file(QString(":/huelith/portrait/%1.svg").arg(names[task]));
    if(!file.open(QIODevice::ReadOnly))return {};
    const auto source=file.readAll();QIcon icon;
    for(auto mode:{QIcon::Normal,QIcon::Selected,QIcon::Active,QIcon::Disabled}){
        auto svg=source;svg.replace("#cad3dd",mode==QIcon::Selected?"#ffffff":mode==QIcon::Disabled?"#65717e":"#e1e5e8");
        QSvgRenderer renderer(svg);
        for(int size:{24,28,30,35,36,42,48,49,56,60,70,72,84}){
            QPixmap pixels(size,size);pixels.fill(Qt::transparent);QPainter painter(&pixels);renderer.render(&painter);painter.end();
            icon.addPixmap(pixels,mode,QIcon::Off);icon.addPixmap(pixels,mode,QIcon::On);
        }
    }
    return icon;
}
}
