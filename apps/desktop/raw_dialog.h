#pragma once
#include "huelith/editing/raw_cache.h"
#include "huelith/editing/raw_develop.h"
#include <QDialog>
#include <future>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTimer;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPathItem;

namespace huelith::desktop {
class TonePanel;
class RawDialog final : public QDialog {
  public:
    explicit RawDialog(std::vector<editing::Layer> sources, QWidget* parent);
    ~RawDialog() override;
    [[nodiscard]] const std::vector<editing::Layer>& results() const {
        return sources_;
    }
    void reject() override;
    void accept() override;

  protected:
    bool eventFilter(QObject* object, QEvent* event) override;

  private:
    void start();
    void finish();
    void display();
    void select(int index);
    void synchronize();
    void update_ready();
    void compute_all();
    [[nodiscard]] bool included(std::size_t index) const;
    void record_history();
    void restore_history(int direction);
    void refresh_local_controls();
    void select_adjustment();
    struct Job;
    std::unique_ptr<Job> job_;
    std::vector<editing::Layer> sources_;
    editing::RawCache cache_;
    std::vector<QString> cache_keys_;
    bool batch_{};
    std::size_t batch_attempts_{};
    std::vector<std::vector<QJsonObject>> history_;
    std::size_t history_position_{};
    bool fit_next_{true};
    int current_{};
    bool closing_{};
    bool loading_{};
    QTimer* timer_{};
    QListWidget* files_{};
    QGraphicsView* view_{};
    QGraphicsScene* scene_{};
    QCheckBox* denoise_{};
    QCheckBox* compare_{};
    QComboBox* backend_{};
    QLineEdit* model_{};
    QDoubleSpinBox* exposure_{};
    QComboBox* edit_mode_{};
    QComboBox* adjustment_{};
    QDoubleSpinBox* local_radius_{};
    QDoubleSpinBox* local_feather_{};
    QCheckBox* overlay_{};
    QJsonObject local_stroke_;
    QSize source_size_;
    QGraphicsPathItem* stroke_guide_{}; // Owned by scene_; cleared before every scene rebuild.
    TonePanel* tone_{};
    QLabel* message_{};
    QLabel* histogram_{};
    QLabel* image_info_{};
    QProgressBar* progress_{};
    QPushButton* apply_{};
    QPushButton* compute_{};
};
} // namespace huelith::desktop
