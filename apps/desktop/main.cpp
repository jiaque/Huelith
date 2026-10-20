#include "application_icon.h"
#include "main_window.h"
#include "canvas_preferences.h"
#include "huelith/editing/ai_worker.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[]) {
    if (argc == 3 && QByteArray(argv[1]) == "--ai-worker") {
        QCoreApplication worker(argc, argv);
        return huelith::editing::ai_worker_main(worker.arguments().at(2));
    }
    QCoreApplication::setOrganizationName("Huelith");
    QCoreApplication::setApplicationName("Huelith");
    huelith::desktop::configure_interface_scale();
    QApplication application(argc, argv);
    application.setWindowIcon(huelith::desktop::application_icon());
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
