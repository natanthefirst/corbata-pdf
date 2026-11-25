#include "DefaultApp.h"

#include <QMessageBox>
#include <QPointer>
#include <QWidget>

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace DefaultApp {

bool isSupported() { return true; }

bool isDefault()
{
    NSURL *handler = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenContentType:UTTypePDF];
    if (!handler)
        return false;
    NSString *id = [[NSBundle bundleWithURL:handler] bundleIdentifier];
    return id && [id isEqualToString:[[NSBundle mainBundle] bundleIdentifier]];
}

void registerApp()
{
    // Finder registers the app from its Info.plist when it's copied to Applications.
}

void makeDefault(QWidget *parent)
{
    if (isDefault()) {
        QMessageBox::information(parent, QObject::tr("Default PDF App"),
                                 QObject::tr("Corbata PDF is already your default app for PDF files."));
        return;
    }
    NSURL *bundle = [[NSBundle mainBundle] bundleURL];
    if ([[bundle path] hasPrefix:@"/Volumes/"]) {
        QMessageBox::information(parent, QObject::tr("Default PDF App"),
                                 QObject::tr("Drag Corbata PDF into your Applications folder first, "
                                             "open it from there, and try again."));
        return;
    }
    QPointer<QWidget> guard(parent);
    // macOS asks the user to confirm.
    [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:bundle
                                           toOpenContentType:UTTypePDF
                                           completionHandler:^(NSError *error) {
        if (!error)
            return;
        NSString *message = [error localizedDescription];
        const QString text = QString::fromNSString(message);
        dispatch_async(dispatch_get_main_queue(), ^{
            QMessageBox::warning(guard, QObject::tr("Default PDF App"),
                                 QObject::tr("macOS didn't change the default app:\n%1\n\n"
                                             "You can also do it in Finder: select any PDF, choose File ▸ Get Info, "
                                             "pick Corbata PDF under \"Open with\" and click \"Change All…\".")
                                     .arg(text));
        });
    }];
}

} // namespace DefaultApp
