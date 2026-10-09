#pragma once
#include "huelith/validation/state.h"
#include <QDialog>

class QDoubleSpinBox;
class QLineEdit;
namespace huelith::desktop {
enum class SessionKind { raw, portrait };
class ValidationSession final : public QDialog {
  public:
    ValidationSession(validation::Document& document, SessionKind kind, QWidget* parent = nullptr);
    [[nodiscard]] const validation::Session& session() const noexcept;
    void accept() override;

  private:
    validation::Document& document_;
    validation::Session session_;
    QDoubleSpinBox* parameter_{};
    QLineEdit* text_{};
};
} // namespace huelith::desktop
