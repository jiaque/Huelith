#include "application_icon.h"
#include <QPainter>
#include <QSvgRenderer>

static void initialize_logo_resource() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(huelith_assets);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace huelith::desktop {
QIcon application_icon() {
    initialize_logo_resource();
    QSvgRenderer renderer(QStringLiteral(":/huelith/huelith.svg"));
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        QPixmap pixels(size, size);
        pixels.fill(Qt::transparent);
        QPainter painter(&pixels);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pixels);
    }
    return icon;
}
}
