#pragma once
#include <QImage>
#include <optional>
class QWidget;
namespace huelith::desktop {
std::optional<QImage> edit_portrait_liquify(QWidget* parent,const QImage& source,QSize original_size,QImage mesh,const QImage& person_mask);
}
