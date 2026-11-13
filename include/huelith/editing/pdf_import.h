#pragma once
#include <QImage>
#include <QString>
#include <atomic>
#include <vector>
namespace huelith::editing {
struct PdfPages { int count{}; std::vector<QImage> images; };
// Run on a worker thread. first=0 reads the page count only; page numbers are one-based.
PdfPages import_pdf_pages(const QString& path, const QString& password, int first, int last,
                          int ppi, const std::atomic_bool* cancel = nullptr);
}
