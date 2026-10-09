#pragma once
#include "huelith/document/canvas_spec.h"
#include <QDialog>

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace huelith::desktop {
class NewDocumentDialog final : public QDialog {
public:
    explicit NewDocumentDialog(QWidget* parent = nullptr);
    [[nodiscard]] CanvasSpec specification() const;
private:
    void update_validation();
    // Non-owning observers; QObject parents own all controls.
    QLineEdit* name_{};
    QSpinBox* width_{};
    QSpinBox* height_{};
    QDoubleSpinBox* resolution_{};
    QComboBox* background_{};
    QLabel* error_{};
    QPushButton* create_{};
};
} // namespace huelith::desktop
