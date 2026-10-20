#include "huelith/editing/tone.h"
#include "raw_dialog.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace huelith::desktop {
namespace {
constexpr int rating_role = Qt::UserRole + 10, rejected_role = Qt::UserRole + 11;
class FilmList final : public QListWidget {
  public:
    using QListWidget::QListWidget;

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        const auto index = indexAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && index.isValid()) {
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                range_press_ = true;
                const int first = std::clamp(anchor_, 0, count() - 1), last = index.row();
                QItemSelection selection(model()->index(std::min(first, last), 0),
                                         model()->index(std::max(first, last), 0));
                selectionModel()->select(selection, event->modifiers().testFlag(Qt::ControlModifier)
                                                        ? QItemSelectionModel::Select
                                                        : QItemSelectionModel::ClearAndSelect);
                setCurrentRow(last, QItemSelectionModel::NoUpdate);
                event->accept();
                return;
            }
            anchor_ = index.row();
        }
        QListWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (range_press_) {
            range_press_ = false;
            event->accept();
            return;
        }
        QListWidget::mouseReleaseEvent(event);
    }

  private:
    int anchor_{};
    bool range_press_{};
};
class FilmDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    std::function<void(int, int)> rated;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        auto image = option;
        image.rect.setBottom(option.rect.bottom() - 20);
        QStyledItemDelegate::paint(painter, image, index);
        painter->save();
        const QRect stars(option.rect.center().x() - 45, option.rect.bottom() - 19, 90, 18);
        const int rating = index.data(rating_role).toInt();
        painter->setPen(index.data(rejected_role).toBool() ? QColor("#df8585") : QColor("#d8d8d8"));
        for (int i = 0; i < 5; ++i)
            painter->drawText(stars.adjusted(i * 18, 0, -(4 - i) * 18, 0), Qt::AlignCenter,
                              i < rating ? QStringLiteral("★") : QStringLiteral("☆"));
        if (index.data(rejected_role).toBool())
            painter->drawText(option.rect.adjusted(3, 3, -3, -3), Qt::AlignRight | Qt::AlignTop,
                              QStringLiteral("×"));
        painter->restore();
    }
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const QRect stars(option.rect.center().x() - 45, option.rect.bottom() - 19, 90, 18);
            if (mouse->button() == Qt::LeftButton && stars.contains(mouse->position().toPoint())) {
                const int value =
                    std::clamp((int(mouse->position().x()) - stars.left()) / 18 + 1, 1, 5);
                const int row = index.row(),
                          rating = index.data(rating_role).toInt() == value ? 0 : value;
                QTimer::singleShot(0, this, [this, row, rating] {
                    if (rated)
                        rated(row, rating);
                });
            }
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
};
QJsonValue delta(const QJsonValue& before, const QJsonValue& after, const QJsonValue& target) {
    if (before == after)
        return target;
    if (before.isObject() && after.isObject()) {
        auto output = target.toObject();
        const auto a = before.toObject(), b = after.toObject();
        for (auto it = a.begin(); it != a.end(); ++it)
            if (!b.contains(it.key()))
                output.remove(it.key());
        for (auto it = b.begin(); it != b.end(); ++it)
            if (a.value(it.key()) != it.value())
                output.insert(it.key(),
                              delta(a.value(it.key()), it.value(), output.value(it.key())));
        return output;
    }
    if (before.isArray() && after.isArray() && before.toArray().size() == after.toArray().size() &&
        target.isArray() && target.toArray().size() == after.toArray().size()) {
        const auto a = before.toArray(), b = after.toArray();
        auto output = target.toArray();
        for (qsizetype i = 0; i < b.size(); ++i)
            output[i] = delta(a[i], b[i], output[i]);
        return output;
    }
    return after;
}
} // namespace
QListWidget* create_raw_filmstrip(QWidget* parent) {
    return new FilmList(parent);
}
QWidget* RawDialog::setup_filmstrip(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName("rawFilmstripPanel");
    auto* box = new QVBoxLayout(panel);
    box->setContentsMargins(0, 0, 0, 0);
    files_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files_->setGridSize(QSize(144, 120));
    files_->setFixedHeight(128);
    for (int i = 0; i < files_->count(); ++i)
        files_->item(i)->setSizeHint(QSize(140, 116));
    auto* delegate = new FilmDelegate(files_);
    files_->setItemDelegate(delegate);
    delegate->rated = [this](int row, int rating) { rate_selected(rating, row); };
    box->addWidget(files_);
    auto* actions = new QHBoxLayout;
    box->addLayout(actions);
    film_filter_ = new QComboBox(panel);
    film_filter_->setObjectName("rawFilmFilter");
    film_filter_->addItems({tr("全部照片"), tr("未评级"), tr("至少 1 星"), tr("至少 2 星"),
                            tr("至少 3 星"), tr("至少 4 星"), tr("5 星"), tr("已拒绝")});
    actions->addWidget(film_filter_);
    connect(film_filter_, &QComboBox::currentIndexChanged, this, [this] { refresh_filmstrip(); });
    for (int i = 0; i <= 5; ++i) {
        auto* button = new QToolButton(panel);
        button->setObjectName(QString("rawRating%1").arg(i));
        button->setText(i == 0 ? QStringLiteral("☆") : QStringLiteral("★"));
        button->setToolTip(tr("将选中照片设为 %1 星").arg(i));
        actions->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, i] { rate_selected(i); });
        auto* shortcut = new QShortcut(QKeySequence(QString::number(i)), files_);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, i] { rate_selected(i); });
    }
    auto* reject = new QToolButton(panel);
    reject->setText(tr("拒绝"));
    reject->setObjectName("rawRejectPhoto");
    actions->addWidget(reject);
    connect(reject, &QToolButton::clicked, this, [this] { reject_selected(); });
    auto* all = new QToolButton(panel);
    all->setText(tr("全选"));
    all->setObjectName("rawSelectAllPhotos");
    actions->addWidget(all);
    connect(all, &QToolButton::clicked, files_, &QListWidget::selectAll);
    actions->addStretch();
    film_count_ = new QLabel(panel);
    film_count_->setObjectName("rawFilmCount");
    actions->addWidget(film_count_);
    connect(files_, &QListWidget::itemSelectionChanged, this, [this] {
        if (film_count_)
            refresh_filmstrip();
    });
    refresh_filmstrip();
    return panel;
}
void RawDialog::refresh_filmstrip() {
    if (!film_filter_ || !film_count_ || sources_.size() != std::size_t(files_->count()))
        return;
    const QSignalBlocker blocker(files_);
    int visible = 0;
    const int filter = film_filter_->currentIndex();
    for (int i = 0; i < files_->count(); ++i) {
        auto* item = files_->item(i);
        const auto& recipe = sources_[i].raw_recipe;
        const int rating = std::clamp(recipe.value("rating").toInt(), 0, 5);
        const bool rejected = recipe.value("rejected").toBool();
        item->setData(rating_role, rating);
        item->setData(rejected_role, rejected);
        const bool show = filter == 0 || (filter == 1 && rating == 0 && !rejected) ||
                          (filter >= 2 && filter <= 6 && rating >= filter - 1 && !rejected) ||
                          (filter == 7 && rejected);
        if (item->isHidden() == show)
            item->setHidden(!show);
        if (!show)
            item->setSelected(false);
        else
            ++visible;
    }
    film_count_->setText(tr("%1 / %2 张 · 选中 %3 张")
                             .arg(visible)
                             .arg(files_->count())
                             .arg(files_->selectedItems().size()));
    files_->viewport()->update();
}
void RawDialog::rate_selected(int rating, int clicked) {
    if (job_ || commit_job_)
        return;
    if (clicked >= 0 && !files_->item(clicked)->isSelected())
        files_->setCurrentRow(clicked, QItemSelectionModel::ClearAndSelect);
    history_timer_->stop();
    record_history();
    auto selected = files_->selectedItems();
    if (selected.empty() && current_ >= 0 && !files_->item(current_)->isHidden())
        selected.append(files_->item(current_));
    for (auto* item : selected)
        sources_[files_->row(item)].raw_recipe.insert("rating", std::clamp(rating, 0, 5));
    record_history();
    refresh_filmstrip();
}
void RawDialog::reject_selected() {
    if (job_ || commit_job_)
        return;
    auto selected = files_->selectedItems();
    if (selected.empty() && current_ >= 0 && !files_->item(current_)->isHidden())
        selected.append(files_->item(current_));
    const bool reject = std::any_of(selected.begin(), selected.end(), [this](auto* item) {
        return !sources_[files_->row(item)].raw_recipe.value("rejected").toBool();
    });
    history_timer_->stop();
    record_history();
    for (auto* item : selected)
        sources_[files_->row(item)].raw_recipe.insert("rejected", reject);
    record_history();
    refresh_filmstrip();
    update_ready();
}
void RawDialog::apply_multiselect_delta() {
    if (current_ < 0 || current_ >= int(sources_.size()) || loading_)
        return;
    const auto current = sources_[current_].raw_recipe;
    if (!displayed_recipe_.isEmpty() && files_->selectedItems().size() > 1) {
        const QStringList fields{"tone", "profile", "whiteBalance",  "optics",   "geometry",
                                 "denoise",     "denoiseAmount", "directml", "modelPath",
                                 "customModel", "demosaic"};
        for (auto* item : files_->selectedItems()) {
            const auto index = files_->row(item);
            if (index == current_ || item->isHidden())
                continue;
            auto& target = sources_[index].raw_recipe;
            for (const auto& field : fields)
                if (displayed_recipe_.value(field) != current.value(field)) {
                    auto baseline = target.value(field);
                    auto previous = displayed_recipe_.value(field);
                    if (field == "tone") {
                        if (!baseline.isObject()) {
                            auto tone = editing::tone_from_json({});
                            tone.exposure = target.value("exposure").toDouble();
                            baseline = editing::tone_json(tone);
                        }
                        if (!previous.isObject()) {
                            auto tone = editing::tone_from_json({});
                            tone.exposure = displayed_recipe_.value("exposure").toDouble();
                            previous = editing::tone_json(tone);
                        }
                    }
                    if (!current.contains(field))
                        target.remove(field);
                    else
                        target.insert(field, delta(previous, current.value(field), baseline));
                    if (field == "tone")
                        target.insert("exposure",
                                      target.value("tone").toObject().value("exposure"));
                    if (field == "denoise" || field == "directml" || field == "modelPath" ||
                        field == "customModel" || field == "demosaic")
                        cache_keys_[index].clear();
                }
        }
    }
    displayed_recipe_ = current;
}
} // namespace huelith::desktop
