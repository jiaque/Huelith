#pragma once
#include "huelith/editing/export_queue.h"
#include <QDialog>
#include <QPointer>

class QListWidget;
class QTableWidget;
class QLineEdit;
class QComboBox;
class QLabel;
class QSpinBox;
namespace huelith::desktop {
class CanvasView;
class ExportDialog final : public QDialog {
  public:
    explicit ExportDialog(QWidget* parent);
    void set_sources(const QList<CanvasView*>& sources);
    [[nodiscard]] editing::ExportQueue& queue() {
        return queue_;
    }

  private:
    void enqueue();
    void refresh();
    void update_example();
    editing::ExportQueue queue_;
    QList<QPointer<CanvasView>> sources_;
    QListWidget* files_{};
    QTableWidget* tasks_{};
    QLineEdit* directory_{};
    QLineEdit* sizes_{};
    QLineEdit* naming_{};
    QComboBox* format_{};
    QComboBox* profile_{};
    QComboBox* conflict_{};
    QSpinBox* quality_{};
    QLabel* message_{};
    QLabel* example_{};
};
} // namespace huelith::desktop
