#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <atomic>
#include <vector>
namespace huelith::editing {
enum class SemanticTarget { sky, grass, vegetation, people, subject, background, object };
struct SemanticComponent {
    QImage mask;
    QRect bounds;
    int pixels{};
};
struct SemanticInstance {
    QImage mask;
    QRect bounds;
    int category{};
    float confidence{};
    int pixels{};
};
std::vector<SemanticInstance> semantic_instances(const QImage& image, const QString& model,
                                                 const std::atomic_bool& cancelled);
QString semantic_category_name(int category);
// ADE20K zero-based label image, at aspect-preserving inference resolution.
QImage semantic_labels(const QImage& image, const QString& model,
                       const std::atomic_bool& cancelled);
QImage semantic_mask(const QImage& labels, SemanticTarget target, QSize output_size);
std::vector<SemanticComponent> semantic_people(const QImage& labels);
} // namespace huelith::editing
