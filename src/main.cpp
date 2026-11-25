#include "MainWindow.h"

#include <QApplication>
#include <QFileOpenEvent>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
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

// The app icon: a page with a necktie ("corbata") on it, drawn in code so
// it is crisp at every size.
QIcon makeAppIcon()
{
    QIcon icon;
    for (int s : {16, 24, 32, 48, 64, 128, 256, 512}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(s / 100.0, s / 100.0);
        // Rounded tile.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x1f, 0x3a, 0x5f));
        p.drawRoundedRect(QRectF(4, 4, 92, 92), 20, 20);
        // Page.
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(24, 14, 52, 72), 5, 5);
        // Necktie.
        p.setBrush(QColor(0xd9, 0x3a, 0x2b));
        QPainterPath knot;
        knot.moveTo(43, 22);
        knot.lineTo(57, 22);
        knot.lineTo(54, 31);
        knot.lineTo(46, 31);
        knot.closeSubpath();
        p.drawPath(knot);
        QPainterPath blade;
        blade.moveTo(46, 32);
        blade.lineTo(54, 32);
        blade.lineTo(61, 66);
        blade.lineTo(50, 77);
        blade.lineTo(39, 66);
        blade.closeSubpath();
        p.drawPath(blade);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setApplicationName(QStringLiteral("Corbata PDF"));
    QApplication::setOrganizationName(QStringLiteral("Corbata"));
    QApplication::setApplicationVersion(QStringLiteral(CORBATA_VERSION));
    CorbataApp app(argc, argv);
    app.setWindowIcon(makeAppIcon());

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
    }

    return app.exec();
}
