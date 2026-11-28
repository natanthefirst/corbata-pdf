#pragma once

#include <QImage>
#include <QSizeF>
#include <QString>

#include <functional>

class QWidget;

// Printing through the Windows 10/11 print dialog with its live preview
// (Windows.Graphics.Printing), instead of the classic Win32 dialog, which
// shows "This app doesn't support print preview".
namespace WinPrint {

enum class Result { Printed, Cancelled, Failed, Unavailable };

// Draws one page of the document onto a sheet of paper: `paper` is the sheet
// size in points, `dpi` the resolution of the image to return (white sheet,
// page fitted and centred). Called on the GUI thread.
using PageImage = std::function<QImage(int index, const QSizeF &paper, qreal dpi)>;

// Shows the print dialog for `window` and waits until the job has been sent
// (or the dialog was cancelled). Returns Unavailable if the modern dialog
// can't be used, so the caller can fall back to Qt's print dialog.
Result print(QWidget *window, const QString &title, int pageCount, const PageImage &pageImage);

} // namespace WinPrint
