#pragma once
#include "huelith/editing/project.h"
namespace huelith::editing {
struct LayoutTemplate {
    QSize size{1600, 1200};
    double ppi{300};
    int rows{2}, columns{2};
    double margin{60}, gap{24}, caption{100};
    QColor background{Qt::white};
    QString title{QStringLiteral("照片集")};
};
[[nodiscard]] State create_layout_template(const LayoutTemplate& layout);
void place_in_frame(State& state, const QUuid& target, QImage pixels, const QString& source);
[[nodiscard]] QStringList missing_fonts(const State& state);
} // namespace huelith::editing
