#include "Printing.h"
#include "Library.h"
#include "Marks.h"

#include <QApplication>
#include <QPainter>
#include <QPrinter>
#include <QProgressDialog>

namespace Printing {

bool print(QPrinter &printer, Library *library, const PageList &pages, const FieldValues &fields,
           const QList<int> &rows, QWidget *parent)
{
    if (rows.isEmpty())
        return false;
    QPainter painter;
    if (!painter.begin(&printer))
        return false;
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QProgressDialog progress(QApplication::translate("Printing", "Printing…"),
                             QApplication::translate("Printing", "Cancel"), 0, int(rows.size()), parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(600);

    // Pages are rendered at the printer's resolution, up to a sane limit.
    const qreal dpi = qMin(printer.resolution(), 300);
    const QRectF paper(QPointF(0, 0), printer.pageRect(QPrinter::DevicePixel).size());

    for (int i = 0; i < rows.size(); ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) {
            painter.end();
            return false;
        }
        if (i > 0 && !printer.newPage())
            return false;
        const PageRef ref = pages.value(rows.at(i));
        QSizeF disp = ref.displaySize();
        if (disp.isEmpty())
            disp = QSizeF(612, 792);

        // Turn the page if that makes it fit the paper better.
        const bool turn = (disp.width() > disp.height()) != (paper.width() > paper.height());
        const QSizeF upright = turn ? disp.transposed() : disp;
        const QSizeF fitted = upright.scaled(paper.size(), Qt::KeepAspectRatio);
        painter.save();
        painter.translate(paper.center());
        if (turn)
            painter.rotate(90);
        const QSizeF drawn = turn ? fitted.transposed() : fitted; // in the page's own orientation
        const QRectF target(QPointF(-drawn.width() / 2, -drawn.height() / 2), drawn);

        painter.fillRect(target, Qt::white);
        if (!ref.isBlank()) {
            const int px = int(qMax(disp.width(), disp.height()) * dpi / 72.0);
            QImage img = library->render(ref.sourceId, ref.pageIndex, px);
            if (ref.rotation)
                img = img.transformed(QTransform().rotate(ref.rotation), Qt::SmoothTransformation);
            if (!img.isNull())
                painter.drawImage(target, img);
        }
        const QTransform toPage = pageToTarget(ref, target);
        if (!ref.isBlank()) {
            const QHash<QString, QString> changed = fields.value(ref.sourceId);
            if (!changed.isEmpty())
                FormFields::paint(painter, library->fields(ref.sourceId), changed, ref.pageIndex, toPage, false);
        }
        Marks::paint(painter, ref.marks, toPage);
        painter.restore();
    }
    progress.setValue(int(rows.size()));
    return painter.end();
}

} // namespace Printing
