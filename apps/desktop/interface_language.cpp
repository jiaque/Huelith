#include "interface_language.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace huelith::desktop {
namespace {
class EnglishTranslator final : public QTranslator {
public:
    explicit EnglishTranslator(QObject* parent) : QTranslator(parent) {
        QFile file(":/huelith/translations/en.json");
        if (!file.open(QIODevice::ReadOnly))
            qFatal("Cannot open the English translation catalog");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            qFatal("Invalid English translation catalog");
        messages_ = document.object();
    }
    bool isEmpty() const override { return messages_.isEmpty(); }
    QString translate(const char*, const char* source, const char*, int) const override {
        return messages_.value(QString::fromUtf8(source)).toString();
    }
private:
    QJsonObject messages_;
};
}

QString interface_language() {
    const QString saved = QSettings().value("interface/language").toString();
    if (saved == "en" || saved == "zh_CN")
        return saved;
    return QLocale::system().language() == QLocale::Chinese ? "zh_CN" : "en";
}

void install_interface_language() {
    if (interface_language() == "en") {
        QCoreApplication::installTranslator(new EnglishTranslator(qApp));
        return;
    }
    auto* qt = new QTranslator(qApp);
    if (qt->load(":/huelith/translations/qtbase_zh_CN.qm") ||
        qt->load("qtbase_zh_CN", QCoreApplication::applicationDirPath() + "/translations") ||
        qt->load("qtbase_zh_CN", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(qt);
    else
        delete qt;
}
}
