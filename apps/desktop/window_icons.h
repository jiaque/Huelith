#pragma once
#include <QIcon>
#include <QPainter>
#include <QPixmap>

namespace huelith::desktop {
enum class WindowIcon { minimize, maximize, restore, close };

inline QIcon window_icon(WindowIcon kind) {
    QPixmap pixels(32, 32);
    pixels.setDevicePixelRatio(2.0);
    pixels.fill(Qt::transparent);
    QPainter painter(&pixels);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#cbd0d5"), 1.0));
    switch (kind) {
    case WindowIcon::minimize:
        painter.drawLine(QPointF(3, 8), QPointF(13, 8));
        break;
    case WindowIcon::maximize:
        painter.drawRect(QRectF(3.5, 3.5, 9, 9));
        break;
    case WindowIcon::restore:
        painter.drawPolyline(QPolygonF({{6, 3}, {13, 3}, {13, 10}}));
        painter.drawRect(QRectF(3.5, 5.5, 7, 7));
        break;
    case WindowIcon::close:
        painter.drawLine(QPointF(4, 4), QPointF(12, 12));
        painter.drawLine(QPointF(12, 4), QPointF(4, 12));
        break;
    }
    painter.end();
    return QIcon(pixels);
}
} // namespace huelith::desktop
