#include "huelith/editing/optics.h"
#include "huelith/editing/raw_geometry.h"
#include "raw_dialog.h"
#include "raw_optics_panel.h"
#include "raw_white_balance_panel.h"
#include "tone_panel.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsView>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
namespace huelith::desktop {
void RawDialog::setup_white_balance() {
    auto* body = tone_->findChild<QWidget*>("rawSectionBody1");
    auto* form = qobject_cast<QFormLayout*>(body->layout());
    white_balance_ = new RawWhiteBalancePanel(body);
    form->insertRow(0, white_balance_);
    white_balance_->changed = [this] {
        if (loading_)
            return;
        auto& recipe = sources_[current_].raw_recipe;
        recipe.insert("whiteBalance", editing::white_balance_json(white_balance_->settings()));
        if (!parameter_gesture_)
            history_timer_->start();
        display();
    };
    white_balance_->gesture_started = [this] {
        history_timer_->stop();
        record_history();
        parameter_gesture_ = true;
    };
    white_balance_->gesture_finished = [this] {
        parameter_gesture_ = false;
        history_timer_->stop();
        record_history();
        display();
    };
    white_balance_->automatic = [this] { sample_white_balance({}, true); };
    white_balance_->view_changed = [this] {
        if (white_balance_->sampling()) {
            findChild<QPushButton*>("rawMaskColorPicker")->setChecked(false);
            if (compare_layout_)
                compare_layout_->setCurrentIndex(0);
            tone_->set_target_mode(-1);
            tone_->stop_point_sampling();
            if (optics_)
                optics_->stop_sampling();
        }
        view_->viewport()->setCursor(white_balance_->sampling() ? Qt::CrossCursor
                                                                : Qt::ArrowCursor);
    };
}
void RawDialog::refresh_white_balance() {
    refresh_profiles();
    if (!white_balance_)
        return;
    const auto& recipe = sources_[current_].raw_recipe;
    const auto metadata = recipe.value("metadata").toObject();
    const bool global = adjustment_->currentIndex() <= 0;
    white_balance_->setVisible(global);
    auto* form =
        qobject_cast<QFormLayout*>(tone_->findChild<QWidget*>("rawSectionBody1")->layout());
    for (const char* name : {"toneTemperature", "toneTint"}) {
        auto* spin = tone_->findChild<QDoubleSpinBox*>(name);
        form->setRowVisible(spin->parentWidget(), !global || spin->value() != 0);
    }
    try {
        white_balance_->set_settings(
            editing::white_balance_from_json(recipe.value("whiteBalance").toObject()),
            editing::white_balance_as_shot(metadata), global);
    } catch (const std::exception&) {
        white_balance_->setEnabled(false);
    }
}
void RawDialog::sample_white_balance(QPointF point, bool automatic) {
    if (proxy_source_.isNull() || job_ || commit_job_ || adjustment_->currentIndex() > 0)
        return;
    try {
        auto& recipe = sources_[current_].raw_recipe;
        auto image = proxy_source_;
        QRect area = image.rect();
        if (!automatic) {
            image = editing::apply_geometry(
                editing::apply_optics(image,
                                      editing::optics_from_json(recipe.value("optics").toObject())),
                editing::geometry_from_json(recipe.value("geometry").toObject()));
            const QPoint pixel(qRound(point.x() / source_size_.width() * image.width()),
                               qRound(point.y() / source_size_.height() * image.height()));
            if (!image.rect().contains(pixel))
                return;
            area = QRect(pixel - QPoint(2, 2), QSize(5, 5));
        }
        auto settings =
            editing::white_balance_sample(image, recipe.value("metadata").toObject(), area);
        settings.mode = automatic ? "auto" : "custom";
        history_timer_->stop();
        record_history();
        recipe.insert("whiteBalance", editing::white_balance_json(settings));
        refresh_white_balance();
        record_history();
        display();
    } catch (const std::exception& e) {
        message_->setText(QString::fromUtf8(e.what()));
        refresh_white_balance();
    }
}
} // namespace huelith::desktop
