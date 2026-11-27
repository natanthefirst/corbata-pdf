#include "FormFields.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <map>

namespace FormFields {

namespace {

QString fromUtf8(const std::string &s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }

QRectF userRect(const QPDFObjectHandle::Rectangle &r)
{
    return QRectF(QPointF(r.llx, r.lly), QPointF(r.urx, r.ury)).normalized();
}

bool readBox(QPDFObjectHandle box, PageGeometry &g)
{
    if (!box.isArray() || box.getArrayNItems() != 4)
        return false;
    double v[4];
    for (int i = 0; i < 4; ++i) {
        QPDFObjectHandle n = box.getArrayItem(i);
        if (!n.isNumber())
            return false;
        v[i] = n.getNumericValue();
    }
    g.llx = qMin(v[0], v[2]);
    g.urx = qMax(v[0], v[2]);
    g.lly = qMin(v[1], v[3]);
    g.ury = qMax(v[1], v[3]);
    return g.urx > g.llx && g.ury > g.lly;
}

FieldInfo describe(QPDFFormFieldObjectHelper &f)
{
    FieldInfo info;
    info.name = fromUtf8(f.getFullyQualifiedName());
    const std::string ft = f.getFieldType();
    const int flags = f.getFlags();
    if (ft == "/Tx")
        info.type = FieldInfo::Text;
    else if (ft == "/Btn")
        info.type = f.isPushbutton() ? FieldInfo::Other : f.isRadioButton() ? FieldInfo::Radio : FieldInfo::Check;
    else if (ft == "/Ch")
        info.type = FieldInfo::Choice;
    else if (ft == "/Sig")
        info.type = FieldInfo::Signature;

    info.readOnly = flags & ff_all_read_only;
    info.multiline = info.type == FieldInfo::Text && (flags & ff_tx_multiline);
    info.comb = info.type == FieldInfo::Text && (flags & ff_tx_comb);
    info.editableChoice = info.type == FieldInfo::Choice && (flags & ff_ch_edit);
    QPDFObjectHandle maxLen = f.getInheritableFieldValue("/MaxLen");
    if (maxLen.isInteger())
        info.maxLength = int(maxLen.getIntValue());

    static const QRegularExpression sizeRe(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s+Tf"));
    const auto m = sizeRe.match(fromUtf8(f.getDefaultAppearance()));
    if (m.hasMatch())
        info.fontSize = m.captured(1).toDouble();

    if (info.type == FieldInfo::Choice)
        for (const std::string &c : f.getChoices())
            info.options << fromUtf8(c);

    if (info.isButton()) {
        QPDFObjectHandle v = f.getValue();
        info.value = v.isName() ? fromUtf8(v.getName()).mid(1) : QStringLiteral("Off");
    } else {
        info.value = fromUtf8(f.getValueAsString());
    }
    return info;
}

} // namespace

PageGeometry geometryOf(QPDFObjectHandle page)
{
    PageGeometry g;
    if (!readBox(page.getKey("/CropBox"), g))
        readBox(page.getKey("/MediaBox"), g);
    QPDFObjectHandle rot = page.getKey("/Rotate");
    if (rot.isInteger()) {
        int r = int(rot.getIntValue()) % 360;
        if (r < 0)
            r += 360;
        g.rotate = (r / 90) * 90;
    }
    return g;
}

QVector<FieldInfo> read(const QByteArray &pdf, const QByteArray &password)
{
    QVector<FieldInfo> out;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        q.processMemoryFile("form.pdf", pdf.constData(), size_t(pdf.size()),
                            password.isEmpty() ? nullptr : password.constData());
        QPDFAcroFormDocumentHelper afdh(q);
        if (!afdh.hasAcroForm())
            return out;
        QPDFPageDocumentHelper dh(q);
        dh.pushInheritedAttributesToPage();
        // Widgets are grouped by the field's full name: a radio group or a field
        // shown on several pages has one widget per place.
        QHash<QString, int> byField;
        const std::vector<QPDFPageObjectHelper> pages = dh.getAllPages();
        for (int i = 0; i < int(pages.size()); ++i) {
            QPDFPageObjectHelper page = pages[size_t(i)];
            const QTransform toDisplay = geometryOf(page.getObjectHandle()).displayToUser().inverted();
            for (QPDFAnnotationObjectHelper annot : afdh.getWidgetAnnotationsForPage(page)) {
                if (annot.getFlags() & (an_hidden | an_invisible))
                    continue;
                QPDFFormFieldObjectHelper field = afdh.getFieldForAnnotation(annot);
                if (field.isNull())
                    continue;
                const QString name = fromUtf8(field.getFullyQualifiedName());
                auto it = byField.find(name);
                if (it == byField.end()) {
                    out.append(describe(field));
                    it = byField.insert(name, int(out.size()) - 1);
                }
                FieldWidget w;
                w.page = i;
                w.rect = toDisplay.mapRect(userRect(annot.getRect()));
                if (out[*it].isButton()) {
                    QPDFObjectHandle normal = annot.getAppearanceDictionary().getKey("/N");
                    if (normal.isDictionary())
                        for (const std::string &key : normal.getKeys())
                            if (key != "/Off")
                                w.onState = fromUtf8(key).mid(1);
                }
                out[*it].widgets.append(w);
            }
        }
        // Fields without any visible widget can't be filled here.
        out.removeIf([](const FieldInfo &f) { return f.widgets.isEmpty() || f.type == FieldInfo::Other; });
    } catch (const std::exception &) {
        out.clear();
    }
    return out;
}

void paint(QPainter &p, const QVector<FieldInfo> &fields, const QHash<QString, QString> &changed, int page,
           const QTransform &pageToDevice, bool tint)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setTransform(pageToDevice, true);
    for (const FieldInfo &f : fields) {
        const auto value = changed.constFind(f.name);
        for (const FieldWidget &w : f.widgets) {
            if (w.page != page)
                continue;
            const QRectF r = w.rect;
            if (value != changed.constEnd()) {
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::white);
                p.drawRect(r);
                const QString v = *value;
                if (f.type == FieldInfo::Text || f.type == FieldInfo::Choice) {
                    const qreal size = f.fontSize > 0 ? f.fontSize : qBound(4.0, r.height() * 0.65, 12.0);
                    QFont font(Marks::screenFontFamily());
                    font.setPixelSize(100);
                    const qreal s = size / 100.0;
                    p.setPen(Qt::black);
                    p.setFont(font);
                    p.save();
                    p.translate(r.topLeft());
                    p.scale(s, s);
                    const QRectF box(2 / s, 0, (r.width() - 4) / s, r.height() / s);
                    if (f.comb && f.maxLength > 0) {
                        const qreal cell = r.width() / s / f.maxLength;
                        for (int i = 0; i < v.size() && i < f.maxLength; ++i)
                            p.drawText(QRectF(i * cell, 0, cell, box.height()), Qt::AlignCenter, v.mid(i, 1));
                    } else if (f.multiline) {
                        p.drawText(box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, v);
                    } else {
                        p.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, v);
                    }
                    p.restore();
                } else if (f.type == FieldInfo::Check) {
                    p.setPen(QPen(QColor(0, 0, 0, 110), 0.75));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(r);
                    if (v != QLatin1String("Off")) {
                        const qreal side = qMin(r.width(), r.height()) * 0.8;
                        QRectF sq(0, 0, side, side);
                        sq.moveCenter(r.center());
                        p.setPen(QPen(Qt::black, Marks::strokeWidth(sq), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                        p.drawPath(Marks::symbolPath(Mark::Check, sq));
                    }
                } else if (f.type == FieldInfo::Radio) {
                    const qreal side = qMin(r.width(), r.height()) * 0.85;
                    QRectF circle(0, 0, side, side);
                    circle.moveCenter(r.center());
                    p.setPen(QPen(QColor(0, 0, 0, 140), 0.75));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(circle);
                    if (v == w.onState) {
                        p.setPen(Qt::NoPen);
                        p.setBrush(Qt::black);
                        p.drawEllipse(circle.adjusted(side * 0.25, side * 0.25, -side * 0.25, -side * 0.25));
                    }
                }
            }
            if (tint && !f.readOnly) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(70, 130, 255, 38));
                p.drawRect(r);
            }
        }
    }
    p.restore();
}

} // namespace FormFields
