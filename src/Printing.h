#pragma once

#include "FormFields.h"
#include "PageRef.h"

#include <QImage>
#include <QList>

class Library;
class QPainter;
class QPrinter;
class QWidget;

namespace Printing {

// Draws one page as it looks on screen (the page, filled-in form values and
// Fill & Sign marks) scaled to fit `paper` (in the painter's coordinates),
// turned if it's landscape on portrait paper or the other way round.
// `dpi` is the resolution the page is rendered at.
void drawPage(QPainter &painter, const QRectF &paper, qreal dpi, Library *library, const PageRef &page,
              const FieldValues &fields);

// The same, as an image of a sheet of paper of `paperPoints` at `dpi`.
QImage paperImage(Library *library, const PageRef &page, const FieldValues &fields, const QSizeF &paperPoints,
                  qreal dpi);

// Prints `rows` of `pages` with Qt's printing (used for printing to PDF in
// the tests, on macOS, and on Windows if the Windows print dialog can't be
// used). Shows a progress window over `parent` for longer jobs. Returns false
// if printing failed or was cancelled.
bool print(QPrinter &printer, Library *library, const PageList &pages, const FieldValues &fields,
           const QList<int> &rows, QWidget *parent = nullptr);

} // namespace Printing
