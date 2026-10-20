#pragma once
#include "huelith/editing/selection_refine.h"
#include <QDialog>
#include <memory>
namespace huelith::desktop {
class SelectionRefineDialog final : public QDialog {
  public:
    SelectionRefineDialog(QImage image, QImage mask, QWidget* parent, bool mask_only = false,
                          QImage background = {});
    ~SelectionRefineDialog() override;
    editing::RefinedSelection result_images() const;
    QImage preview_mask() const;
    int output_mode() const;
    void accept() override;
    void reject() override;

  protected:
    bool eventFilter(QObject* object, QEvent* event) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace huelith::desktop
