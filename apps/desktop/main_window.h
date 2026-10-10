#pragma once
#include "huelith/document/canvas_spec.h"
#include <QMainWindow>

class QTabWidget;
class QLabel;
class QStackedWidget;
class QCloseEvent;
class QMenuBar;

namespace huelith::desktop {
class CanvasView;
class EditorController;
class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    [[nodiscard]] bool create_document(const CanvasSpec& spec);
    [[nodiscard]] int document_count() const noexcept;
    void reset_workspace();
    [[nodiscard]] CanvasView* active_canvas() const;
    bool open_document(const QString& path, bool place = false);
    void open_documents(const QStringList& paths);

  protected:
    void closeEvent(QCloseEvent* event) override;
    bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result) override;

  private:
    friend class EditorController;
    EditorController* editor_{};
    void build_menus();
    void build_panels();
    void request_new_document();
    void request_close_document(int index);
    void refresh_document_state();
    void change_zoom(double multiplier);
    [[nodiscard]] bool confirm_discard();

    // Qt parent ownership; these pointers only observe child widgets.
    QTabWidget* documents_{};
    QMenuBar* menus_{};
    QStackedWidget* pages_{};
    QLabel* properties_{};
    QLabel* document_status_{};
    QByteArray default_layout_;
};
void apply_dark_theme();
} // namespace huelith::desktop
