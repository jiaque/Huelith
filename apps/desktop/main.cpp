#include "main_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("Huelith");
    QCoreApplication::setApplicationName("Huelith");
    QCoreApplication::setApplicationVersion("0.1.0-dev");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smoke_test("smoke-test", "Initialize, render and exit for deployment validation.");
    parser.addOption(smoke_test);
    parser.process(application);
    huelith::desktop::apply_dark_theme();
    huelith::desktop::MainWindow window;
    if (parser.isSet(smoke_test)) {
        QTimer::singleShot(0, &window, [&window] {
            QCoreApplication::exit(window.grab().isNull() ? 1 : 0);
        });
    } else {
        window.show();
    }
    return application.exec();
}
