#pragma once
#include <QObject>
#include <QUuid>
#include <functional>
#include <memory>
class QTreeWidget;
class QListWidget;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QMenu;
class QTimer;
namespace huelith::desktop {
class MainWindow;
class CanvasView;
class EditorController final : public QObject {
  public:
    explicit EditorController(MainWindow* window);
    ~EditorController() override;
    void bind(CanvasView* canvas);
    void refresh();
    bool open(const QString& path, bool place = false);
    bool save(CanvasView* canvas, bool save_as = false, bool asynchronous = true);
    bool confirm_close(CanvasView* canvas);

  protected:
    bool eventFilter(QObject* object, QEvent* event) override;

  private:
    void guarded(const std::function<void()>& action);
    void command(const QString& name, const std::function<void()>& action);
    void create_layer(int kind);
    void layer_action(const QString& operation);
    void selection_action(const QString& operation);
    void configure_menus();
    void refresh_tools();
    void build_properties();
    void update_recent(const QString& path);
    void autosave();
    void finish_save(bool wait);
    struct SaveJob;
    std::unique_ptr<SaveJob> save_job_;
    [[nodiscard]] CanvasView* canvas() const;
    MainWindow* window_;
    QTreeWidget* layers_{};
    QListWidget* history_{};
    QListWidget* paths_{};
    QDoubleSpinBox* opacity_{};
    QDoubleSpinBox* exposure_{};
    QCheckBox* mask_{};
    QCheckBox* linked_{};
    QCheckBox* isolated_{};
    QCheckBox* locked_{};
    QCheckBox* clipping_{};
    QMenu* recent_{};
    QComboBox* blend_{};
    bool refreshing_{};
};
} // namespace huelith::desktop
