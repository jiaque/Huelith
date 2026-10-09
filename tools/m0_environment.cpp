#include <QColorSpace>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSysInfo>
#define NOMINMAX
#include <memory>
#include <vector>
#include <windows.h>

namespace {
struct DcCloser {
    void operator()(HDC dc) const noexcept {
        if (dc)
            DeleteDC(dc);
    }
};
using DisplayDc = std::unique_ptr<std::remove_pointer_t<HDC>, DcCloser>;
} // namespace
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (app.arguments().size() != 2)
        return 2;
    QJsonArray screens;
    for (const auto* screen : app.screens()) {
        QJsonObject item{{"name", screen->name()},
                         {"width", screen->size().width()},
                         {"height", screen->size().height()},
                         {"device_pixel_ratio", screen->devicePixelRatio()},
                         {"logical_dpi", screen->logicalDotsPerInch()}};
        const auto name = screen->name().toStdWString();
        DisplayDc dc(CreateDCW(L"DISPLAY", name.c_str(), nullptr, nullptr));
        DWORD length{};
        if (dc)
            GetICMProfileW(dc.get(), &length, nullptr);
        if (length > 0 && length < 32768) {
            std::vector<wchar_t> path(length);
            if (GetICMProfileW(dc.get(), &length, path.data())) {
                const auto profile_path = QString::fromWCharArray(path.data());
                QFile file(profile_path);
                if (file.open(QIODevice::ReadOnly)) {
                    const auto profile = QColorSpace::fromIccProfile(file.readAll());
                    item["icc_path"] = profile_path;
                    item["icc_valid"] = profile.isValid();
                    item["icc_description"] = profile.description();
                }
            }
        }
        screens.append(item);
    }
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory))
        return 3;
    const QJsonObject result{{"os", QSysInfo::prettyProductName()},
                             {"architecture", QSysInfo::currentCpuArchitecture()},
                             {"qt", qVersion()},
                             {"cpu", qEnvironmentVariable("PROCESSOR_IDENTIFIER")},
                             {"physical_ram_bytes", QString::number(memory.ullTotalPhys)},
                             {"screens", screens}};
    QFile file(app.arguments()[1]);
    if (!file.open(QIODevice::WriteOnly))
        return 4;
    const auto bytes = QJsonDocument(result).toJson();
    return file.write(bytes) == bytes.size() ? 0 : 5;
}
