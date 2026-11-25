#pragma once

#include <QMetaType>
#include <QSizeF>
#include <QVector>

// One page in the working document. The edit is a virtual list of these:
// nothing is written to disk until Save / Export / Split.
struct PageRef {
    int sourceId = -1;   // index into the open source files; -1 means a blank page
    int pageIndex = 0;   // 0-based page in that source
    int rotation = 0;    // extra clockwise rotation applied by the user: 0, 90, 180, 270
    QSizeF size;         // displayed size in points, before the extra rotation
    quint64 uid = 0;     // stable identity, used to keep the selection across undo/redo

    bool isBlank() const { return sourceId < 0; }

    // Size as it will appear on screen / in the output file.
    QSizeF displaySize() const { return (rotation % 180) ? size.transposed() : size; }
};

using PageList = QVector<PageRef>;

Q_DECLARE_METATYPE(PageRef)
