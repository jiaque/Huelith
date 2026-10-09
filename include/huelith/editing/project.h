#pragma once
#include "huelith/editing/blend.h"
#include "huelith/editing/tone.h"
#include <QColorSpace>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QPainterPath>
#include <QString>
#include <QTransform>
#include <QUuid>
#include <array>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

namespace huelith::editing {
class EditError : public std::runtime_error {
  public:
    explicit EditError(const QString& message) : std::runtime_error(message.toStdString()) {}
};
enum class LayerKind { pixels, source, repair, adjustment, fill, group, composition };

enum class RangeOp { replace, add, subtract, intersect };
struct Layer {
    QUuid id{QUuid::createUuid()};
    QString name;
    LayerKind kind{LayerKind::pixels};
    QImage pixels; // Immutable source, linear Rec.2020 floating-point premultiplied RGBA.
    QImage mask;   // Grayscale coverage in canvas coordinates before mask_transform.
    QTransform transform;
    bool warped{};
    std::array<QPointF, 9> warp_grid{QPointF(0, 0),  QPointF(.5, 0),  QPointF(1, 0),
                                     QPointF(0, .5), QPointF(.5, .5), QPointF(1, .5),
                                     QPointF(0, 1),  QPointF(.5, 1),  QPointF(1, 1)};
    QTransform mask_transform;
    QPainterPath vector_mask;
    bool vector_enabled{};
    bool mask_enabled{true};
    bool mask_linked{true};
    bool visible{true};
    bool locked{};
    bool clipped{};
    bool isolated{};
    float opacity{1.0F};
    Blend blend{Blend::normal};
    double exposure{};
    QColor fill{Qt::white};
    QColor fill_end{Qt::black};
    bool gradient{};
    QString linked_path;
    QByteArray linked_hash;
    QByteArray raw_source;  // Original camera bytes; re-development survives a missing linked file.
    QJsonObject raw_recipe; // Versioned source-level parameters, separate from layer effects.
    ToneSettings tone;
    std::vector<Layer> children; // Bottom to top, also editable nested composition content.
};
struct State {
    QSize size;
    QString name;
    double ppi{300.0};
    std::vector<Layer> layers;
    QImage selection; // Null = no active selection; non-null zero = active empty selection.
    QMap<QString, QImage> saved_selections;
    QMap<QString, QPainterPath> paths;
    QMap<QString, QImage> snapshots;
    std::vector<double> horizontal_guides;
    std::vector<double> vertical_guides;
};
[[nodiscard]] QColorSpace working_space();
[[nodiscard]] QImage import_image(const QString& path);
[[nodiscard]] QImage to_working(QImage image);
[[nodiscard]] QImage render(const State& state);
[[nodiscard]] QImage display_image(const QImage& linear,
                                   const QColorSpace& output = QColorSpace::SRgb);
[[nodiscard]] Layer* find_layer(std::vector<Layer>& layers, const QUuid& id);
[[nodiscard]] const Layer* find_layer(const std::vector<Layer>& layers, const QUuid& id);
void validate_state(const State& state);
void move_layer(State& state, const QUuid& id, const QUuid& parent, int position);
void remove_layer(State& state, const QUuid& id);
void shift_layer(State& state, const QUuid& id, int direction);
[[nodiscard]] bool layer_locked(const std::vector<Layer>& layers, const QUuid& id);
[[nodiscard]] Layer duplicate_layer(const Layer& source);
[[nodiscard]] QImage path_range(QSize size, const QPainterPath& path);
void combine_range(State& state, const QImage& range, RangeOp operation);
void modify_range(State& state, int radius, int operation); // 0 blur, 1 expand, -1 shrink.
void export_image(const State& state, const QString& path, QColor matte = Qt::white,
                  int quality = 95);

[[nodiscard]] QTransform parent_matrix(const State& state, const QUuid& id);
[[nodiscard]] QRectF layer_bounds(const State& state, const QUuid& id);
[[nodiscard]] QUuid hit_layer(const State& state, QPointF point, double tolerance);
[[nodiscard]] std::vector<QUuid> root_targets(const State& state, const std::vector<QUuid>& ids);
void transform_targets(State& state, const std::vector<QUuid>& ids, const QTransform& delta,
                       bool mask);
struct Snapshot {
    State state;
    quint64 revision{};
};
class Project final {
  public:
    explicit Project(State state);
    [[nodiscard]] const State& state() const noexcept {
        return state_;
    }
    [[nodiscard]] quint64 revision() const noexcept {
        return revision_;
    }
    [[nodiscard]] bool dirty() const noexcept {
        return revision_ != saved_revision_;
    }
    [[nodiscard]] bool can_undo() const noexcept {
        return position_ > 0;
    }
    [[nodiscard]] bool can_redo() const noexcept {
        return position_ + 1 < history_.size();
    }
    [[nodiscard]] Snapshot snapshot() const {
        return {state_, revision_};
    }
    [[nodiscard]] QStringList history_names() const;
    void edit(const QString& name, const std::function<void(State&)>& operation);
    bool undo();
    bool redo();
    bool seek_history(std::size_t position);
    [[nodiscard]] std::size_t history_position() const noexcept {
        return position_;
    }
    [[nodiscard]] State history_state(std::size_t position) const;
    void mark_saved(quint64 revision) noexcept {
        saved_revision_ = revision;
    }

  private:
    struct Entry {
        State state;
        QString name;
        quint64 revision;
    };
    State state_;
    std::vector<Entry> history_;
    std::size_t position_{};
    quint64 revision_{1};
    quint64 next_revision_{2};
    quint64 saved_revision_{};
};
// Atomic snapshot writes. Saving a snapshot never marks newer edits as saved.
void save_project(const Snapshot& snapshot, const QString& path, bool simulate_failure = false);
[[nodiscard]] State load_project(const QString& path);
[[nodiscard]] std::optional<QPointF> warp_point(const Layer& layer, QSize size, QPointF point,
                                                bool inverse);
} // namespace huelith::editing
