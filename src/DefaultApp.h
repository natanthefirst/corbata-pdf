#pragma once

#include <QString>

class QWidget;

// Making Corbata PDF the system's default app for .pdf files.
//
// Windows doesn't let apps set themselves as the default; they register as a
// PDF handler and the user picks them in Settings ▸ Default apps.
// macOS shows its own confirmation ("Change default app?").
namespace DefaultApp {

// Whether this platform supports it at all.
bool isSupported();

// Whether .pdf files currently open with this copy of the app.
bool isDefault();

// Registers the app as a PDF handler for the current user (Windows: registry
// under HKCU, so it appears in "Open with" and Default apps; no-op elsewhere).
// Safe to call on every launch; keeps the path current if the app moved.
void registerApp();

// Asks once (on first launch) whether to make the app the default.
void offerOnce(QWidget *parent);

// Starts the "make default" flow, telling the user what to do if needed.
void makeDefault(QWidget *parent);

} // namespace DefaultApp
