#pragma once
#include "huelith/document/canvas_spec.h"
#include <QColor>
#include <QDialog>
#include <QVariantMap>

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QPushButton;
class QCheckBox;

namespace huelith::desktop {
class NewDocumentDialog final : public QDialog {
public:
    explicit NewDocumentDialog(QWidget* parent = nullptr);
    [[nodiscard]] CanvasSpec specification() const;
private:
    void update_validation();
    void load_preset(const QVariantMap& preset);
    QVariantMap preset() const;
    // Non-owning observers; QObject parents own all controls.
    QLineEdit* name_{};
    QDoubleSpinBox* width_{};
    QDoubleSpinBox* height_{};
    QDoubleSpinBox* resolution_{};
    QComboBox* background_{};
    QLabel* error_{};
    QPushButton* create_{};
    QComboBox* units_{};
    QColor background_color_{Qt::white};
    QLabel* summary_{};
    int previous_unit_{};
    QCheckBox* artboard_{};
    QComboBox* color_mode_{};
    QComboBox* color_bits_{};
    QComboBox* color_profile_{};
    QByteArray icc_profile_;
};
} // namespace huelith::desktop
