#pragma once
#include "huelith/editing/semantic.h"
#include <QDialog>
#include <future>
class QComboBox;
class QLabel;
class QCheckBox;
class QDialogButtonBox;
namespace huelith::desktop {
class SemanticDialog final : public QDialog {
  public:
    SemanticDialog(QImage image, editing::SemanticTarget target, QWidget* parent,
                   bool immediate = false);
    ~SemanticDialog() override;
    void reject() override;
    void accept() override;
    editing::SemanticTarget target() const;
    QImage mask() const {
        return mask_;
    }

  private:
    struct Result {
        QImage labels;
        std::vector<editing::SemanticInstance> instances;
        bool objects{};
    };
    bool eventFilter(QObject* watched, QEvent* event) override;
    void request_result();
    void populate_candidates();
    void refresh();
    QImage source_, labels_, mask_;
    std::vector<editing::SemanticInstance> instances_;
    bool instances_ready_{};
    bool immediate_{};
    QString scene_error_, instance_error_;
    std::atomic_bool cancelled_{};
    std::future<Result> worker_;
    bool worker_objects_{};
    QComboBox* target_{};
    QComboBox* person_{};
    QLabel* preview_{};
    QLabel* status_{};
    QCheckBox* invert_{};
    QDialogButtonBox* buttons_{};
};
} // namespace huelith::desktop
