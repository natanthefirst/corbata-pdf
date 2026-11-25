#pragma once

#include "PageRef.h"

#include <QByteArray>
#include <QHash>
#include <QString>

// The raw bytes of an opened PDF, plus the password it was opened with (if any).
struct SourceBytes {
    QByteArray data;
    QByteArray password;
};

namespace PdfWriter {

// Assembles a brand-new PDF from `pages` (which reference `sources`) using qpdf.
// Page content is copied losslessly; nothing is re-rendered.
// Returns false and fills `error` on failure.
bool build(const PageList &pages, const QHash<int, SourceBytes> &sources,
           QByteArray &out, QString &error);

} // namespace PdfWriter
