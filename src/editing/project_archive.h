#pragma once
#include <QFile>
#include <QJsonObject>
#include <QTemporaryFile>

namespace huelith::editing {
void write_project_archive(QIODevice& output, QTemporaryFile& payload, const QJsonObject& manifest);
QJsonObject read_project_archive(QFile& input, QTemporaryFile& payload);
}
