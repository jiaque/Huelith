#include "text_edit_view.h"
#include "canvas_view.h"
#include <QAbstractTextDocumentLayout>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
namespace {
class TextItem final : public QGraphicsTextItem {
  public:
    explicit TextItem(const editing::TextContent& text)
        : box_(text.box), paragraph_(text.paragraph), text_(text) {
        setDefaultTextColor(Qt::black);
        connect(document(), &QTextDocument::contentsChanged, this, [this] {
            prepareGeometryChange();
            update();
        });
    }
    QRectF boundingRect() const override {
        if (text_.layout) {
            QRectF bounds = paragraph_ ? QRectF(QPointF(), box_) : QRectF(0, 0, 32, 32);
            for (const auto& glyph : editing::positioned_text_glyphs(*document(), text_))
                bounds = bounds.united(glyph.outline.boundingRect())
                             .united(QRectF(glyph.before.p1(), glyph.after.p2()).normalized());
            return bounds.adjusted(-5, -5, 5, 5);
        }
        return paragraph_ ? QRectF(QPointF(), box_).adjusted(-3, -3, 3, 3)
                          : QGraphicsTextItem::boundingRect();
    }
    QSizeF text_box() const {
        return box_;
    }
    bool contains(const QPointF& point) const override {
        return paragraph_ || text_.layout ? boundingRect().contains(point)
                                          : QGraphicsTextItem::contains(point);
    }
    QPainterPath shape() const override {
        if (!paragraph_ && !text_.layout)
            return QGraphicsTextItem::shape();
        QPainterPath path;
        path.addRect(boundingRect());
        return path;
    }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override {
        painter->save();
        if (paragraph_)
            painter->setClipRect(QRectF(QPointF(), box_));
        if (!text_.layout)
            QGraphicsTextItem::paint(painter, option, widget);
        else {
            const auto cursor = textCursor();
            QLineF caret(0, 0, 24, 0);
            bool located = false;
            for (const auto& glyph : editing::positioned_text_glyphs(*document(), text_)) {
                if (cursor.hasSelection() && glyph.end > cursor.selectionStart() &&
                    glyph.start < cursor.selectionEnd())
                    painter->fillRect(glyph.outline.boundingRect(), QColor(45, 128, 225, 120));
                painter->fillPath(glyph.outline, glyph.color);
                if (cursor.position() >= glyph.start && cursor.position() < glyph.end) {
                    caret = glyph.before;
                    located = true;
                } else if (!located && cursor.position() >= glyph.end)
                    caret = glyph.after;
            }
            if (hasFocus()) {
                painter->setPen(QPen(QColor("#72b7f7"), 0));
                painter->drawLine(caret);
            }
            if (!preedit_.isEmpty()) {
                painter->save();
                painter->translate(caret.p1());
                if (text_.layout == 1)
                    painter->rotate(90);
                QFont font = cursor.charFormat().font();
                font.setUnderline(true);
                painter->setFont(font);
                painter->setPen(Qt::white);
                painter->drawText(QPointF(0, QFontMetricsF(font).ascent()), preedit_);
                painter->restore();
            }
        }
        painter->restore();
        painter->setPen(QPen(QColor("#72b7f7"), 0, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        const auto box = paragraph_ ? QRectF(QPointF(), box_) : boundingRect();
        painter->drawRect(box);
        if (paragraph_) {
            painter->setBrush(Qt::white);
            for (const auto point : {box.topLeft(), QPointF(box.center().x(), box.top()),
                                     box.topRight(), QPointF(box.right(), box.center().y()),
                                     box.bottomRight(), QPointF(box.center().x(), box.bottom()),
                                     box.bottomLeft(), QPointF(box.left(), box.center().y())})
                painter->drawRect(QRectF(point - QPointF(1.5, 1.5), QSizeF(3, 3)));
        }
    }

  protected:
    void inputMethodEvent(QInputMethodEvent* event) override {
        preedit_ = event->preeditString();
        QGraphicsTextItem::inputMethodEvent(event);
        update();
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
        if (!text_.layout) {
            QGraphicsTextItem::mouseDoubleClickEvent(event);
            return;
        }
        set_cursor_at(event->pos(), false);
        auto cursor = textCursor();
        cursor.select(QTextCursor::WordUnderCursor);
        setTextCursor(cursor);
        update();
        event->accept();
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (text_.layout == 1 && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
            (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
            auto cursor = textCursor();
            cursor.movePosition(event->key() == Qt::Key_Up ? QTextCursor::PreviousCharacter
                                                           : QTextCursor::NextCharacter,
                                event->modifiers().testFlag(Qt::ShiftModifier)
                                    ? QTextCursor::KeepAnchor
                                    : QTextCursor::MoveAnchor);
            setTextCursor(cursor);
            update();
            event->accept();
            return;
        }
        QGraphicsTextItem::keyPressEvent(event);
        update();
    }
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        if (paragraph_) {
            const auto p = event->pos();
            const auto linear = sceneTransform();
            const double tolerance = 5 / std::max(.1, std::hypot(linear.m11(), linear.m12()));
            edges_ = (std::abs(p.x()) < tolerance                  ? 1
                      : std::abs(p.x() - box_.width()) < tolerance ? 2
                                                                   : 0) |
                     (std::abs(p.y()) < tolerance                   ? 4
                      : std::abs(p.y() - box_.height()) < tolerance ? 8
                                                                    : 0);
            if (edges_) {
                original_box_ = box_;
                original_transform_ = transform();
                press_ = event->scenePos();
                event->accept();
                return;
            }
        }
        if (text_.layout && event->button() == Qt::LeftButton) {
            set_cursor_at(event->pos(), event->modifiers().testFlag(Qt::ShiftModifier));
            event->accept();
            return;
        }
        QGraphicsTextItem::mousePressEvent(event);
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (text_.layout && !edges_ && event->buttons().testFlag(Qt::LeftButton)) {
            set_cursor_at(event->pos(), true);
            event->accept();
            return;
        }
        if (edges_) {
            const auto inverse = original_transform_.inverted();
            const auto delta = inverse.map(event->scenePos()) - inverse.map(press_);
            QRectF box(QPointF(), original_box_);
            if (edges_ & 1)
                box.setLeft(std::min(box.right() - 10, delta.x()));
            if (edges_ & 2)
                box.setRight(std::max(10., original_box_.width() + delta.x()));
            if (edges_ & 4)
                box.setTop(std::min(box.bottom() - 10, delta.y()));
            if (edges_ & 8)
                box.setBottom(std::max(10., original_box_.height() + delta.y()));
            prepareGeometryChange();
            box_ = box.size();
            text_.box = box_;
            document()->setTextWidth(box_.width());
            setTransform(QTransform::fromTranslate(box.x(), box.y()) * original_transform_);
            update();
            event->accept();
            return;
        }
        QGraphicsTextItem::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        if (text_.layout && !edges_) {
            event->accept();
            return;
        }
        if (edges_) {
            mouseMoveEvent(event);
            edges_ = 0;
            event->accept();
            return;
        }
        QGraphicsTextItem::mouseReleaseEvent(event);
    }

  private:
    void set_cursor_at(QPointF point, bool keep) {
        double nearest = 1e100;
        int position = 0;
        for (const auto& glyph : editing::positioned_text_glyphs(*document(), text_)) {
            for (const auto& candidate :
                 {std::pair{glyph.before, glyph.start}, std::pair{glyph.after, glyph.end}}) {
                const auto center = (candidate.first.p1() + candidate.first.p2()) * .5;
                const double distance = QLineF(center, point).length();
                if (distance < nearest) {
                    nearest = distance;
                    position = candidate.second;
                }
            }
        }
        auto cursor = textCursor();
        cursor.setPosition(std::clamp(position, 0, document()->characterCount() - 1),
                           keep ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
        setTextCursor(cursor);
        setFocus();
        update();
    }
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override {
        if (text_.layout && query == Qt::ImCursorRectangle) {
            const int position = textCursor().position();
            QLineF caret(0, 0, 24, 0);
            for (const auto& glyph : editing::positioned_text_glyphs(*document(), text_)) {
                if (position >= glyph.start && position < glyph.end) {
                    caret = glyph.before;
                    break;
                }
                if (position >= glyph.end)
                    caret = glyph.after;
            }
            return QRectF(caret.p1(), caret.p2()).normalized().adjusted(-1, -1, 1, 1);
        }
        return QGraphicsTextItem::inputMethodQuery(query);
    }
    editing::TextContent text_;
    QString preedit_;
    QSizeF box_;
    bool paragraph_;
    int edges_{};
    QSizeF original_box_;
    QTransform original_transform_;
    QPointF press_;
};
} // namespace
TextEditView::TextEditView(CanvasView* canvas, editing::Layer layer, bool existing)
    : QGraphicsView(canvas), canvas_(canvas), layer_(std::move(layer)),
      item_(new TextItem(layer_.text)) {
    setObjectName("canvasTextEditor");
    setAccessibleName(tr("画布文字编辑，Ctrl+Enter 确认，Esc 取消"));
    auto* scene = new QGraphicsScene(this);
    scene->setSceneRect(-1000000, -1000000, 2000000, 2000000);
    setScene(scene);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet("QGraphicsView { background: transparent; border: none; }");
    setBackgroundBrush(Qt::NoBrush);
    viewport()->setAutoFillBackground(false);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    scene->addItem(item_);
    editing::configure_text_document(*item_->document(), layer_.text);
    item_->setTransform(layer_.transform *
                        editing::parent_matrix(canvas->project().state(), layer_.id));
    item_->setTextInteractionFlags(Qt::TextEditorInteraction);
    item_->setFlag(QGraphicsItem::ItemIsFocusable);
    item_->setFocus();
    auto cursor = item_->textCursor();
    if (existing)
        cursor.select(QTextCursor::Document);
    item_->setTextCursor(cursor);
    sync_view();
    if (layer_.warped) {
        const auto message =
            tr("正在编辑变形文字：编辑时显示原始轮廓，完成后恢复原有变形。Esc 取消。");
        auto* hint = new QLabel(message, this);
        hint->setObjectName("textWarpHint");
        hint->setStyleSheet("QLabel { background: #343b42; color: #ffffff; padding: 8px; }");
        hint->adjustSize();hint->move(12,12);hint->show();
    }
    show();
    raise();
    setFocus();
}
void TextEditView::sync_view() {
    setGeometry(canvas_->rect());
    auto matrix = canvas_->view_transform();
    matrix.setMatrix(matrix.m11(), matrix.m12(), matrix.m13(), matrix.m21(), matrix.m22(),
                     matrix.m23(), 0, 0, matrix.m33());
    setTransform(matrix);
    centerOn(canvas_->image_point(QPointF(canvas_->width() / 2., canvas_->height() / 2.)));
}
editing::Layer TextEditView::result() const {
    auto layer = layer_;
    layer.text.html = item_->document()->toHtml();
    layer.text.box = static_cast<TextItem*>(item_)->text_box();
    layer.transform = item_->sceneTransform() *
                      editing::parent_matrix(canvas_->project().state(), layer_.id).inverted();
    const auto label = item_->document()->toPlainText().simplified().left(40);
    if (!label.isEmpty())
        layer.name = label;
    return layer;
}
void TextEditView::format(const QTextCharFormat& format) {
    auto cursor = item_->textCursor();
    cursor.mergeCharFormat(format);
    item_->setTextCursor(cursor);
    item_->setFocus();
    setFocus();
    if (format_changed) format_changed();
}
QTextCharFormat TextEditView::current_format() const { return item_->textCursor().charFormat(); }
void TextEditView::mouseReleaseEvent(QMouseEvent* event) {
    QGraphicsView::mouseReleaseEvent(event);
    if (format_changed) format_changed();
}
void TextEditView::inputMethodEvent(QInputMethodEvent* event) {
    QGraphicsView::inputMethodEvent(event);
    if (format_changed) format_changed();
}
void TextEditView::alignment(Qt::Alignment alignment) {
    auto cursor = item_->textCursor();
    QTextBlockFormat format;
    format.setAlignment(alignment);
    cursor.mergeBlockFormat(format);
    item_->setTextCursor(cursor);
    item_->setFocus();
    setFocus();
}
void TextEditView::line_height(double percent) {
    auto cursor = item_->textCursor();
    QTextBlockFormat format;
    format.setLineHeight(percent, QTextBlockFormat::ProportionalHeight);
    cursor.mergeBlockFormat(format);
    item_->setTextCursor(cursor);
    item_->setFocus();
    setFocus();
}
void TextEditView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape ||
        ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
         event->modifiers().testFlag(Qt::ControlModifier))) {
        if (finished)
            finished(event->key() != Qt::Key_Escape);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
    if (format_changed) format_changed();
}
void TextEditView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton &&
        !item_->contains(item_->mapFromScene(mapToScene(event->position().toPoint())))) {
        if (finished)
            finished(true);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}
} // namespace huelith::desktop
