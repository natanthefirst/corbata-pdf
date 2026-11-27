#pragma once

#include "Marks.h"

#include <QMetaType>
#include <QSizeF>
#include <QTransform>
#include <QVector>

// One page in the working document. The edit is a virtual list of these:
// nothing is written to disk until Save / Export / Split.
struct PageRef {
    int sourceId = -1;   // index into the open source files; -1 means a blank page
    int pageIndex = 0;   // 0-based page in that source
    int rotation = 0;    // extra clockwise rotation applied by the user: 0, 90, 180, 270
    QSizeF size;         // displayed size in points, before the extra rotation
    quint64 uid = 0;     // stable identity, used to keep the selection across undo/redo
    QVector<Mark> marks; // Fill & Sign: text, check marks and signatures placed on the page

    bool isBlank() const { return sourceId < 0; }

    // Size as it will appear on screen / in the output file.
    QSizeF displaySize() const { return (rotation % 180) ? size.transposed() : size; }
};

using PageList = QVector<PageRef>;

// Maps a page's display space (points, before the user's extra rotation) onto
// `target`, the rectangle where the page appears rotated on screen.
inline QTransform pageToTarget(const PageRef &ref, const QRectF &target)
{
    QSizeF size = ref.size.isEmpty() ? QSizeF(612, 792) : ref.size;
    const QSizeF disp = (ref.rotation % 180) ? size.transposed() : size;
    QTransform t;
    t.translate(target.left(), target.top());
    t.scale(target.width() / disp.width(), target.height() / disp.height());
    switch (ref.rotation) {
    case 90: t.translate(size.height(), 0); t.rotate(90); break;
    case 180: t.translate(size.width(), size.height()); t.rotate(180); break;
    case 270: t.translate(0, size.width()); t.rotate(270); break;
    default: break;
    }
    return t;
}

Q_DECLARE_METATYPE(PageRef)
