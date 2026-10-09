#pragma once
#include <QObject>
#include <QPoint>
#include <QUuid>
#include <functional>
#include <memory>
#include <optional>
class QWidget;
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
    void refresh(bool rebuild_tree = true);
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
    void configure_photo_tools();
    void show_tone(int category);
    void refresh_tools();
    void copy_pixels(bool merged, bool cut);
    void paste_pixels(bool in_place, bool into_selection);
    void align_layers(const QString& operation);
    void show_export();
    void show_shortcuts();
    void offer_recovery();
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
    QWidget* layer_controls_{};
    bool align_to_canvas_{true};
    Qt::KeyboardModifiers layer_click_modifiers_{};
};
} // namespace huelith::desktop
