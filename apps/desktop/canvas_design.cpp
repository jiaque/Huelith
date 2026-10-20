#include "canvas_view.h"
#include "text_edit_view.h"
#include <QMessageBox>
#include <QPainter>
#include <QTextCursor>
#include <QTextDocument>
#include <cmath>

namespace huelith::desktop {
void CanvasView::bind_shape_path() {
    const auto* layer = editing::find_layer(project_->state().layers, active_layer);
    path_layer_ = {};
    if (!layer || layer->kind != editing::LayerKind::shape || layer->warped ||
        editing::layer_locked(project_->state().layers, layer->id))
        return;
    set_path((layer->transform * editing::parent_matrix(project_->state(), layer->id))
                 .map(editing::shape_path(layer->shape)));
    path_layer_ = layer->id;
}
void CanvasView::update_shape_path(bool commit) {
    const auto* layer = editing::find_layer(project_->state().layers, path_layer_);
    if (!layer || editing::layer_locked(project_->state().layers, path_layer_))
        return;
    const auto matrix = layer->transform * editing::parent_matrix(project_->state(), path_layer_);
    const auto changed_path = matrix.inverted().map(path);
    auto apply = [&](editing::State& state) {
        auto* target = editing::find_layer(state.layers, path_layer_);
        target->shape.kind = 4;
        target->shape.path = changed_path;
        const auto bounds = changed_path.boundingRect();
        if (bounds.width() > 0 && bounds.height() > 0)
            target->shape.size = bounds.size();
    };
    if (commit) {
        if (changed_path != editing::shape_path(layer->shape))
            edit(tr("编辑形状路径"), apply);
    } else {
        auto draft = project_->state();
        apply(draft);
        preview(draft);
    }
}
bool CanvasView::insert_pen_node(QPointF point) {
    if (pen_nodes_.size() < 2 || pen_nodes_.size() > 9998)
        return false;
    auto lerp = [](QPointF a, QPointF b, double t) { return a * (1 - t) + b * t; };
    double best = 10 / zoom_, chosen{};
    std::size_t segment{};
    bool found{};
    const auto count = pen_closed_ ? pen_nodes_.size() : pen_nodes_.size() - 1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& a = pen_nodes_[i];
        const auto& b = pen_nodes_[(i + 1) % pen_nodes_.size()];
        for (int sample = 1; sample < 64; ++sample) {
            const double t = sample / 64.;
            const auto p =
                lerp(lerp(lerp(a.anchor, a.outgoing, t), lerp(a.outgoing, b.incoming, t), t),
                     lerp(lerp(a.outgoing, b.incoming, t), lerp(b.incoming, b.anchor, t), t), t);
            const double distance = QLineF(p, point).length();
            if (distance < best) {
                found = true;
                best = distance;
                chosen = t;
                segment = i;
            }
        }
    }
    if (!found)
        return false;
    const auto next = (segment + 1) % pen_nodes_.size();
    const auto a = pen_nodes_[segment], b = pen_nodes_[next];
    const auto p0 = lerp(a.anchor, a.outgoing, chosen), p1 = lerp(a.outgoing, b.incoming, chosen),
               p2 = lerp(b.incoming, b.anchor, chosen);
    const auto q0 = lerp(p0, p1, chosen), q1 = lerp(p1, p2, chosen);
    pen_nodes_[segment].outgoing = p0;
    pen_nodes_[next].incoming = p2;
    pen_nodes_.insert(pen_nodes_.begin() + static_cast<std::ptrdiff_t>(segment + 1),
                      {lerp(q0, q1, chosen), q0, q1});
    rebuild_pen();
    return true;
}
void CanvasView::begin_text(QRectF box, bool paragraph, bool existing) {
    finish_text(true);
    commit_transform();
    editing::Layer layer;
    if (existing) {
        const auto* selected = editing::find_layer(project_->state().layers, active_layer);
        if (!selected || selected->kind != editing::LayerKind::text ||
            editing::layer_locked(project_->state().layers, selected->id))
            return;
        layer = *selected;
    } else {
        layer.kind = editing::LayerKind::text;
        layer.name = tr("文字");
        const double scale = project_->state().ppi / 96.;
        layer.transform = QTransform(scale, 0, 0, scale, box.x(), box.y());
        layer.text.paragraph = paragraph;
        if (paragraph)
            layer.text.box = box.size() / scale;
        QTextDocument document;
        editing::configure_text_document(document, layer.text);
        QTextCursor cursor(&document);
        auto format = text_defaults;
        if (!format.hasProperty(QTextFormat::ForegroundBrush))
            format.setForeground(foreground);
        cursor.insertText(tr("在此输入文字"), format);
        layer.text.html = document.toHtml();
    }
    text_revision_ = project_->revision();
    text_existing_ = existing;
    if (existing) {
        auto state = project_->state();
        editing::find_layer(state.layers, layer.id)->visible = false;
        preview(state);
    }
    text_editor_ = new TextEditView(this, layer, true);
    text_editor_->finished = [this](bool accept) { finish_text(accept); };
    text_editor_->format_changed = [this] { if (tool_changed) tool_changed(); };
    if (tool_changed)
        tool_changed();
}
void CanvasView::finish_text(bool accept) {
    if (!text_editor_)
        return;
    auto layer = text_editor_->result();
    text_editor_->hide();
    text_editor_->deleteLater();
    text_editor_ = nullptr;
    try {
        if (accept && project_->revision() == text_revision_) {
            QTextDocument document;
            editing::configure_text_document(document, layer.text);
            if (!document.toPlainText().isEmpty()) {
                editing::validate_text(layer.text);
                edit(tr("编辑文字"), [&](editing::State& state) {
                    if (text_existing_)
                        *editing::find_layer(state.layers, layer.id) = layer;
                    else
                        state.layers.push_back(layer);
                });
                active_layer = layer.id;
                selected_layers = {layer.id};
            } else if (text_existing_) {
                edit(tr("清空文字图层"),
                     [&](editing::State& state) { editing::remove_layer(state, layer.id); });
            }
        }
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("文字未提交"), QString::fromUtf8(error.what()));
    }
    refresh();
    setFocus();
    if (tool_changed)
        tool_changed();
    if (changed)
        changed();
}
void CanvasView::format_text(const QTextCharFormat& format) {
    text_defaults.merge(format);
    if (!text_editor_)
        begin_text({}, false, true);
    if (text_editor_)
        text_editor_->format(format);
}
QTextCharFormat CanvasView::current_text_format() const {
    if (text_editor_) return text_editor_->current_format();
    const auto* layer = editing::find_layer(project_->state().layers, active_layer);
    if (layer && layer->kind == editing::LayerKind::text) {
        QTextDocument document; editing::configure_text_document(document, layer->text);
        QTextCursor cursor(&document); cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        return cursor.charFormat();
    }
    return text_defaults;
}
void CanvasView::align_text(Qt::Alignment alignment) {
    if (!text_editor_)
        begin_text({}, false, true);
    if (text_editor_)
        text_editor_->alignment(alignment);
}
void CanvasView::text_line_height(double percent) {
    if (!text_editor_)
        begin_text({}, false, true);
    if (text_editor_)
        text_editor_->line_height(percent);
}
void CanvasView::create_shape(QRectF box, bool reverse_line) {
    if (box.width() < .1 || box.height() < .1)
        return;
    editing::Layer layer;
    layer.kind = editing::LayerKind::shape;
    layer.name = tr("形状");
    layer.shape = shape_options;
    layer.shape.size = box.size();
    layer.shape.fill = foreground;
    layer.transform.translate(box.x(), box.y());
    if (layer.shape.kind == 4) {
        if (path.isEmpty())
            return;
        const auto bounds = path.boundingRect();
        if (bounds.width() <= 0 || bounds.height() <= 0)
            return;
        QTransform normalize;
        normalize.scale(box.width() / bounds.width(), box.height() / bounds.height());
        normalize.translate(-bounds.x(), -bounds.y());
        layer.shape.path = normalize.map(path);
    }
    if (reverse_line && layer.shape.kind == 3) {
        layer.shape.kind = 4;
        layer.shape.path.moveTo(0, box.height());
        layer.shape.path.lineTo(box.width(), 0);
    }
    active_layer = layer.id;
    selected_layers = {layer.id};
    mask_target = false;
    edit(tr("绘制形状"), [&](editing::State& state) { state.layers.push_back(layer); });
}
} // namespace huelith::desktop
