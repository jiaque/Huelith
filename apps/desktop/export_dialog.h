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
class QCheckBox;
class QFormLayout;
class QPushButton;
namespace huelith::desktop {
class CanvasView;
class ExportDialog final : public QDialog {
  public:
    explicit ExportDialog(QWidget* parent);
    void set_sources(const QList<CanvasView*>& sources);
    void set_snapshots(std::vector<editing::Snapshot> snapshots);
    [[nodiscard]] editing::ExportQueue& queue() {
        return queue_;
    }

  private:
    void enqueue();
    void refresh();
    void update_example();
    void setup_presets(QFormLayout* form);
    QJsonObject output_preset() const;
    void apply_output_preset(const QJsonObject& preset);
    editing::ExportQueue queue_;
    QList<QPointer<CanvasView>> sources_;
    std::vector<editing::Snapshot> snapshots_;
    QListWidget* files_{};
    QTableWidget* tasks_{};
    QLineEdit* directory_{};
    QLineEdit* sizes_{};
    QLineEdit* naming_{};
    QComboBox* format_{};
    QComboBox* profile_{};
    QComboBox* conflict_{};
    QSpinBox* quality_{};
    QCheckBox* alpha_{};
    QCheckBox* metadata_[5]{};
    QPushButton* matte_{};
    QLabel* message_{};
    QLabel* example_{};
};
} // namespace huelith::desktop
