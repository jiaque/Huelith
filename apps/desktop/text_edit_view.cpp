#include "text_edit_view.h"
#include "canvas_view.h"
#include <QLabel>
#include <QAbstractTextDocumentLayout>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <cmath>

namespace huelith::desktop {
namespace {
class TextItem final : public QGraphicsTextItem {
  public:
    explicit TextItem(QSizeF box, bool paragraph) : box_(box), paragraph_(paragraph) {}
    QRectF boundingRect() const override {
        return paragraph_ ? QRectF(QPointF(), box_).adjusted(-3, -3, 3, 3)
                          : QGraphicsTextItem::boundingRect();
    }
    QSizeF text_box() const {
        return box_;
    }
    bool contains(const QPointF& point) const override {
        return paragraph_ ? boundingRect().contains(point) : QGraphicsTextItem::contains(point);
    }
    QPainterPath shape() const override {
        if (!paragraph_)
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
        QGraphicsTextItem::paint(painter, option, widget);
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
        QGraphicsTextItem::mousePressEvent(event);
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
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
            document()->setTextWidth(box_.width());
            setTransform(QTransform::fromTranslate(box.x(), box.y()) * original_transform_);
            update();
            event->accept();
            return;
        }
        QGraphicsTextItem::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        if (edges_) {
            mouseMoveEvent(event);
            edges_ = 0;
            event->accept();
            return;
        }
        QGraphicsTextItem::mouseReleaseEvent(event);
    }

  private:
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
      item_(new TextItem(layer_.text.box, layer_.text.paragraph)) {
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
        auto* hint=new QLabel(tr("正在编辑变形文字：编辑时显示原始轮廓，完成后恢复原有变形。Esc 取消。"),this);
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
