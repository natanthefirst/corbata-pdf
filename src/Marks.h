#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>

class QPainter;

// Something the user placed on a page with Fill & Sign: text, a check mark,
// a cross, or a signature. Marks are drawn on screen with QPainter and burned
// into the page content when the PDF is written.
//
// Coordinates are page points in "display space": the page as a viewer shows
// it with the PDF's own /Rotate applied (but not the user's extra rotation),
// origin at the top-left, y pointing down.
struct Mark {
    enum Kind { Text, Check, Cross, Signature };
    Kind kind = Text;
    QRectF rect;
    quint64 uid = 0;

    // Text
    QString text;
    qreal fontSize = 12;
    QColor color = Qt::black;

    // Signature: a filled vector outline (drawn or typed), or a picture (imported).
    QPainterPath path;
    QImage image;
};

// Where a page sits in PDF user space: its visible box and its own /Rotate.
struct PageGeometry {
    qreal llx = 0, lly = 0, urx = 612, ury = 792;
    int rotate = 0; // 0, 90, 180 or 270

    QSizeF displaySize() const;
    // Maps display space (top-left origin, y down) to PDF user space.
    QTransform displayToUser() const;
};

namespace Marks {

// Text layout shared by the screen and the PDF, so both look the same.
constexpr qreal kAscent = 0.905;  // baseline of the first line, in font sizes below the top
constexpr qreal kLeading = 1.2;   // line height, in font sizes

// Text as it will appear in the PDF: characters the built-in Helvetica can't
// show (outside Windows-1252) become '?'.
QString displayText(const QString &text);
QByteArray encodeWinAnsi(const QString &text);

// The size a text mark needs (requires a GUI application for font metrics).
QSizeF textSize(const QString &text, qreal fontSize);

// The screen font matching the PDF's Helvetica (Arial on Windows).
QString screenFontFamily();

// Paints marks. `pageToDevice` maps display-space page points to the device.
void paint(QPainter &p, const QVector<Mark> &marks, const QTransform &pageToDevice);
void paintMark(QPainter &p, const Mark &m);

// The outline of a check mark or cross inside `r` (stroke it with strokeWidth(r)).
QPainterPath symbolPath(Mark::Kind kind, const QRectF &r);
qreal strokeWidth(const QRectF &r);

// PDF content-stream operators for the marks, in display space (the caller
// sets up the `cm`). Text uses the font resource /CorbataHelv; image
// signatures use /CorbataImg<index>, where index is the mark's position.
QByteArray pdfOps(const QVector<Mark> &marks);

} // namespace Marks
