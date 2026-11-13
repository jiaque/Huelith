#pragma once
#include "huelith/editing/project.h"
namespace huelith::editing {
// Export scopes: complete document, selected layers, artboards, slices.
std::vector<Snapshot> export_regions(const Snapshot& snapshot, int scope,
                                     const std::vector<QUuid>& selected = {});
QUuid create_artboard(State& state, QString name, QRect bounds,
                      const std::vector<QUuid>& contents = {});
void set_artboard_bounds(Layer& board, QRect bounds);
void resize_document(State& state,QSize size,double ppi);
}
