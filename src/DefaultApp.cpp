// Platform-independent part of DefaultApp, plus stubs for platforms other
// than Windows (DefaultApp_win.cpp) and macOS (DefaultApp_mac.mm).
#include "DefaultApp.h"

#include <QMessageBox>
#include <QSettings>

namespace DefaultApp {

void offerOnce(QWidget *parent)
{
    QSettings settings;
    const QString key = QStringLiteral("offeredDefaultApp");
    if (settings.value(key).toBool() || !isSupported() || isDefault())
        return;
    settings.setValue(key, true);
    const auto answer = QMessageBox::question(
        parent, QObject::tr("Default PDF App"),
        QObject::tr("Make Corbata PDF your default app for PDF files?\n\n"
                    "You can do this later from File ▸ Make Default PDF App."));
    if (answer == QMessageBox::Yes)
        makeDefault(parent);
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
bool isSupported() { return false; }
bool isDefault() { return false; }
void registerApp() {}
void makeDefault(QWidget *) {}
#endif

} // namespace DefaultApp
