#pragma once

#include "Marks.h"

#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

class QPainter;
class QPDFObjectHandle;

// A fillable form field (AcroForm) in a source PDF.
struct FieldWidget {
    int page = 0;      // 0-based page index in the source
    QRectF rect;       // display space of that page (see Marks.h)
    QString onState;   // checkbox / radio: the name of the "on" appearance, e.g. "Yes"
};

struct FieldInfo {
    enum Type { Text, Check, Radio, Choice, Signature, Other };
    QString name;      // fully qualified name, unique key within a source
    Type type = Other;
    bool multiline = false;
    bool readOnly = false;
    bool comb = false;
    bool editableChoice = false;
    int maxLength = 0; // 0 = unlimited
    qreal fontSize = 0; // from the field's /DA; 0 = auto
    QStringList options;
    QString value;     // current value; for buttons the selected on-state or "Off"
    QVector<FieldWidget> widgets;

    bool isButton() const { return type == Check || type == Radio; }
};

// Values the user typed, per source id, then field name -> value.
// Only changed fields are listed; everything else keeps the file's value.
using FieldValues = QHash<int, QHash<QString, QString>>;

namespace FormFields {

// Reads the fields of a PDF. Returns an empty list for PDFs without a form
// or if anything goes wrong (forms are optional; the PDF still opens).
QVector<FieldInfo> read(const QByteArray &pdf, const QByteArray &password = {});

// The visible box and /Rotate of a page object (after inherited attributes
// were pushed down to it).
PageGeometry geometryOf(QPDFObjectHandle page);

// Paints the fields on one page: a light tint on fillable fields (if `tint`),
// and the user's changed values over the file's own appearance.
void paint(QPainter &p, const QVector<FieldInfo> &fields, const QHash<QString, QString> &changed, int page,
           const QTransform &pageToDevice, bool tint);

} // namespace FormFields
