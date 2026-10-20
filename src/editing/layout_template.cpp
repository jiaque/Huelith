#include "huelith/editing/image_storage.h"
#include "huelith/editing/layout_template.h"
#include <QFontDatabase>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <cmath>

namespace huelith::editing {
State create_layout_template(const LayoutTemplate& layout) {
    if (layout.size.width() < 32 || layout.size.height() < 32 ||
        qint64(layout.size.width()) * layout.size.height() > huelith::editing::maximum_image_pixels || layout.rows < 1 ||
        layout.columns < 1 || layout.rows > 10 || layout.columns > 10 ||
        !std::isfinite(layout.margin) || layout.margin < 0 || !std::isfinite(layout.gap) ||
        layout.gap < 0 || !std::isfinite(layout.caption) || layout.caption < 0 ||
        !std::isfinite(layout.ppi) || layout.ppi <= 0 || layout.ppi > 9600 ||
        !layout.background.isValid() || layout.title.size() > 4096)
        throw EditError(QStringLiteral("布局参数无效"));
    const double width =
        (layout.size.width() - 2 * layout.margin - (layout.columns - 1) * layout.gap) /
        layout.columns;
    const double height = (layout.size.height() - 2 * layout.margin - layout.caption -
                           (layout.rows - 1) * layout.gap) /
                          layout.rows;
    if (width < 8 || height < 8)
        throw EditError(QStringLiteral("边距、间距或标题区域过大，相框没有足够空间"));
    State state;
    state.name = layout.title.isEmpty() ? QStringLiteral("布局模板") : layout.title;
    state.size = layout.size;
    state.ppi = layout.ppi;
    Layer background;
    background.kind = LayerKind::fill;
    background.name = QStringLiteral("背景颜色");
    background.fill = layout.background;
    state.layers.push_back(background);
    for (int row = 0; row < layout.rows; ++row)
        for (int column = 0; column < layout.columns; ++column) {
            const QRectF bounds(layout.margin + column * (width + layout.gap),
                                layout.margin + row * (height + layout.gap), width, height);
            Layer frame;
            frame.kind = LayerKind::group;
            frame.name = QStringLiteral("相框 %1").arg(row * layout.columns + column + 1);
            frame.frame = bounds;
            frame.vector_enabled = true;
            frame.vector_mask.addRect(bounds);
            Layer placeholder;
            placeholder.kind = LayerKind::shape;
            placeholder.name = QStringLiteral("相框底色");
            placeholder.shape.size = bounds.size();
            placeholder.shape.fill = QColor("#d2d5d9");
            placeholder.transform.translate(bounds.x(), bounds.y());
            frame.children.push_back(placeholder);
            state.layers.push_back(frame);
        }
    if (!layout.title.isEmpty() && layout.caption > 0) {
        Layer title;
        title.kind = LayerKind::text;
        title.name = QStringLiteral("标题");
        title.text.paragraph = true;
        const double scale = layout.ppi / 96.;
        title.text.box = QSizeF(layout.size.width() - 2 * layout.margin, layout.caption) / scale;
        title.text.html = QString("<p align='center' style='font-family:Microsoft "
                                  "YaHei;font-size:%1pt;color:#20252b'>%2</p>")
                              .arg(std::clamp(layout.caption * 72 / layout.ppi * .45, 1., 1296.))
                              .arg(layout.title.toHtmlEscaped());
        title.transform = QTransform(scale, 0, 0, scale, layout.margin,
                                     layout.size.height() - layout.margin - layout.caption);
        state.layers.push_back(title);
    }
    validate_state(state);
    return state;
}
void place_in_frame(State& state, const QUuid& target, QImage pixels, const QString& source) {
    Layer* frame{};
    std::function<void(std::vector<Layer>&, Layer*)> visit = [&](auto& layers, Layer* parent) {
        for (auto& layer : layers) {
            auto* current = layer.frame ? &layer : parent;
            if (layer.id == target)
                frame = current;
            visit(layer.children, current);
        }
    };
    visit(state.layers, nullptr);
    if (!frame || layer_locked(state.layers, target) || pixels.isNull() ||
        pixels.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        throw EditError(QStringLiteral("请选择未锁定相框或相框内的图层"));
    auto found = std::find_if(frame->children.begin(), frame->children.end(),
                              [](const Layer& layer) { return layer.kind == LayerKind::source; });
    Layer image;
    if (found != frame->children.end())
        image = *found;
    const auto box = *frame->frame;
    const double scale = std::max(box.width() / pixels.width(), box.height() / pixels.height());
    image.kind = LayerKind::source;
    image.name = QStringLiteral("相框照片");
    image.pixels = std::move(pixels);
    image.linked_path = source;
    image.linked_hash.clear();
    image.transform =
        QTransform(scale, 0, 0, scale, box.center().x() - image.pixels.width() * scale / 2,
                   box.center().y() - image.pixels.height() * scale / 2);
    if (found == frame->children.end())
        frame->children.push_back(std::move(image));
    else
        *found = std::move(image);
}
QStringList missing_fonts(const State& state) {
    const auto installed = QFontDatabase::families();
    QSet<QString> missing;
    std::function<void(const std::vector<Layer>&)> visit = [&](const auto& layers) {
        for (const auto& layer : layers) {
            if (layer.kind == LayerKind::text) {
                QTextDocument document;
                configure_text_document(document, layer.text);
                for (auto block = document.begin(); block.isValid(); block = block.next())
                    for (auto it = block.begin(); !it.atEnd(); ++it)
                        for (const auto& family :
                             it.fragment().charFormat().fontFamilies().toStringList())
                            if (!installed.contains(family, Qt::CaseInsensitive) &&
                                family != "sans-serif" && family != "serif" &&
                                family != "monospace")
                                missing.insert(family);
            }
            visit(layer.children);
        }
    };
    visit(state.layers);
    auto result = missing.values();
    result.sort();
    return result;
}
} // namespace huelith::editing
