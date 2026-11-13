#pragma once
#include "huelith/editing/project.h"
namespace huelith::editing {
struct DocumentColor {
    int mode{}; // RGB, grayscale, Lab D50, CMYK with an explicit device profile.
    int bits{32};
    QColorSpace profile{QColorSpace::SRgb};
};
DocumentColor document_color(const State& state);
void set_document_color(State& state, const DocumentColor& color, bool assign = false);
QImage constrain_document_color(const QImage& source, const State& state);
QImage proof_image(const QImage& source, const QColorSpace& proof,
                   const QColorSpace& monitor, bool gamut = false, QColor warning = Qt::gray);
std::array<double,3> working_to_lab(std::array<double,3> rgb);
std::array<double,3> lab_to_working(std::array<double,3> lab);
QStringList document_channel_names(const State& state);
QImage document_channel(const QImage& working, const State& state, int component);
void apply_document_channel(Layer& layer, const State& state, int component, const QImage& mask);
}
