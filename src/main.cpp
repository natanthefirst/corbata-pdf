#include "DefaultApp.h"
#include "MainWindow.h"

#include <QApplication>
#include <QFileOpenEvent>
#include <QIcon>
#include <QTimer>

namespace {

// Receives "open this file" requests from macOS Finder (double-click / Open With).
class CorbataApp : public QApplication
{
public:
    using QApplication::QApplication;
    MainWindow *window = nullptr;
    QStringList pending;

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent *>(e)->file();
            if (window)
                window->openOrAppend({path});
            else
                pending << path;
            return true;
        }
        return QApplication::event(e);
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setApplicationName(QStringLiteral("Corbata PDF"));
    QApplication::setOrganizationName(QStringLiteral("Corbata"));
    QApplication::setApplicationVersion(QStringLiteral(CORBATA_VERSION));
    CorbataApp app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/AppIcon.png")));

    const QStringList args = app.arguments();
    MainWindow w;
    app.window = &w;
    w.show();

    QStringList files = args.mid(1);
    files.removeIf([](const QString &a) { return a.startsWith(QLatin1Char('-')); });
    files += app.pending;
    if (!files.isEmpty())
        QTimer::singleShot(0, &w, [&w, files] { w.openFiles(files); });

    // Test hook: CORBATA_SCREENSHOT=out.png saves a window screenshot and quits.
    const QString shot = qEnvironmentVariable("CORBATA_SCREENSHOT");
    if (!shot.isEmpty()) {
        QTimer::singleShot(2500, &w, [&w, shot] {
            w.grab().save(shot);
            QApplication::exit(0);
        });
    } else {
        DefaultApp::registerApp();
        QTimer::singleShot(800, &w, [&w] { DefaultApp::offerOnce(&w); });
    }

    return app.exec();
}
