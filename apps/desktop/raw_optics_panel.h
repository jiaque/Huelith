#pragma once
#include "huelith/editing/optics.h"
#include <QWidget>
#include <functional>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QToolButton;
namespace huelith::desktop {
class HueRange;
class RawOpticsPanel final : public QWidget {
  public:
    explicit RawOpticsPanel(QWidget* parent);
    void set_settings(const editing::OpticsSettings& settings, const QJsonObject& metadata);
    const editing::OpticsSettings& settings() const {
        return settings_;
    }
    bool sampling() const;
    void sample(QColor color);
    void stop_sampling();
    int auxiliary_mode() const { return auxiliary_; }
    std::function<void()> changed, view_changed, gesture_started, gesture_finished;

  private:
    bool eventFilter(QObject* object, QEvent* event) override;
    void refresh();
    void update_dependencies();
    void choose(int index);
    void clear_profile();
    void populate_profiles(int index);
    editing::OpticsSettings settings_;
    QJsonObject metadata_;
    std::vector<editing::LensProfile> profiles_;
    QToolButton* enabled_{};
    QCheckBox* profile_{};
    QCheckBox* ca_{};
    QComboBox* maker_{};
    QComboBox* model_{};
    QComboBox* profile_choice_{};
    QLabel* status_{};
    QToolButton* picker_{};
    std::array<HueRange*, 2> ranges_{};
    std::vector<std::pair<QDoubleSpinBox*, double editing::OpticsSettings::*>> numbers_;
    bool loading_{};
    int auxiliary_{};
};
} // namespace huelith::desktop
