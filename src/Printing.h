#pragma once

#include "FormFields.h"
#include "PageRef.h"

#include <QList>

class Library;
class QPrinter;
class QWidget;

namespace Printing {

// Prints `rows` of `pages` as they look on screen: the page, filled-in form
// values and Fill & Sign marks. Each page is scaled to fit the paper (and
// turned if it's landscape on portrait paper, or the other way round).
// Shows a progress window over `parent` for longer jobs. Returns false if
// printing failed or was cancelled.
bool print(QPrinter &printer, Library *library, const PageList &pages, const FieldValues &fields,
           const QList<int> &rows, QWidget *parent = nullptr);

} // namespace Printing
