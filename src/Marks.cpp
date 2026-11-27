#include "Marks.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>

#include <cmath>

// ---------------------------------------------------------------- geometry

QSizeF PageGeometry::displaySize() const
{
    const qreal w = urx - llx, h = ury - lly;
    return (rotate % 180) ? QSizeF(h, w) : QSizeF(w, h);
}

QTransform PageGeometry::displayToUser() const
{
    // Qt: x' = m11*x + m21*y + dx,  y' = m12*x + m22*y + dy
    switch (rotate) {
    case 90: return QTransform(0, 1, 1, 0, llx, lly);
    case 180: return QTransform(-1, 0, 0, 1, urx, lly);
    case 270: return QTransform(0, -1, -1, 0, urx, ury);
    default: return QTransform(1, 0, 0, -1, llx, ury);
    }
}

namespace Marks {

// ---------------------------------------------------------------- text

namespace {

// Windows-1252 code points 0x80-0x9F (0 = unused).
const char16_t kCp1252High[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};

int toCp1252(char32_t c)
{
    if ((c >= 0x20 && c < 0x7F) || (c >= 0xA0 && c <= 0xFF))
        return int(c);
    for (int i = 0; i < 32; ++i)
        if (kCp1252High[i] && kCp1252High[i] == c)
            return 0x80 + i;
    return -1;
}

QFont screenFont()
{
    // Drawn at 100 px and scaled, so any point size renders exactly.
    QFont f(screenFontFamily());
    f.setPixelSize(100);
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setKerning(false); // the PDF has no kerning either
    return f;
}

QByteArray pdfString(const QByteArray &bytes)
{
    QByteArray out = "(";
    for (char c : bytes) {
        if (c == '(' || c == ')' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + ')';
}

QByteArray num(qreal v)
{
    if (std::abs(v) < 0.00005)
        v = 0;
    QByteArray s = QByteArray::number(v, 'f', 4);
    while (s.endsWith('0'))
        s.chop(1);
    if (s.endsWith('.'))
        s.chop(1);
    return s;
}

QByteArray colorOps(const QColor &c, bool stroke)
{
    return num(c.redF()) + ' ' + num(c.greenF()) + ' ' + num(c.blueF()) + (stroke ? " RG\n" : " rg\n");
}

QByteArray pathOps(const QPainterPath &path)
{
    QByteArray out;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo()) {
            out += num(e.x) + ' ' + num(e.y) + " m\n";
        } else if (e.isLineTo()) {
            out += num(e.x) + ' ' + num(e.y) + " l\n";
        } else if (e.isCurveTo() && i + 2 < path.elementCount()) {
            const QPainterPath::Element c2 = path.elementAt(i + 1), end = path.elementAt(i + 2);
            out += num(e.x) + ' ' + num(e.y) + ' ' + num(c2.x) + ' ' + num(c2.y) + ' ' + num(end.x) + ' ' +
                   num(end.y) + " c\n";
            i += 2;
        }
    }
    return out;
}

// The signature outline, moved and scaled into the mark's rectangle.
QPainterPath placedSignature(const Mark &m)
{
    const QRectF src = m.path.boundingRect();
    if (src.isEmpty())
        return {};
    QTransform t;
    t.translate(m.rect.left(), m.rect.top());
    t.scale(m.rect.width() / src.width(), m.rect.height() / src.height());
    t.translate(-src.left(), -src.top());
    QPainterPath placed = t.map(m.path);
    placed.setFillRule(m.path.fillRule());
    return placed;
}

} // namespace

QString screenFontFamily()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("Helvetica");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Arial");
#else
    return QStringLiteral("Liberation Sans");
#endif
}

QString displayText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (char32_t c : text.toUcs4()) {
        if (c == '\n')
            out += QLatin1Char('\n');
        else if (toCp1252(c) < 0)
            out += QLatin1Char('?');
        else
            out += QString::fromUcs4(&c, 1);
    }
    return out;
}

QByteArray encodeWinAnsi(const QString &text)
{
    QByteArray out;
    for (char32_t c : text.toUcs4()) {
        const int b = toCp1252(c);
        out += char(b < 0 ? '?' : b);
    }
    return out;
}

QSizeF textSize(const QString &text, qreal fontSize)
{
    const QFontMetricsF fm(screenFont());
    const QStringList lines = displayText(text).split(QLatin1Char('\n'));
    qreal w = 0;
    for (const QString &l : lines)
        w = qMax(w, fm.horizontalAdvance(l));
    const qreal scale = fontSize / 100.0;
    return QSizeF(qMax(w * scale, fontSize * 0.5), lines.size() * kLeading * fontSize);
}

// ---------------------------------------------------------------- symbols

qreal strokeWidth(const QRectF &r) { return 0.12 * qMin(r.width(), r.height()); }

QPainterPath symbolPath(Mark::Kind kind, const QRectF &r)
{
    const qreal in = strokeWidth(r) / 2;
    const QRectF b = r.adjusted(in, in, -in, -in);
    auto at = [&](qreal fx, qreal fy) { return QPointF(b.left() + fx * b.width(), b.top() + fy * b.height()); };
    QPainterPath p;
    if (kind == Mark::Check) {
        p.moveTo(at(0.0, 0.55));
        p.lineTo(at(0.36, 0.92));
        p.lineTo(at(1.0, 0.08));
    } else {
        p.moveTo(at(0.05, 0.05));
        p.lineTo(at(0.95, 0.95));
        p.moveTo(at(0.95, 0.05));
        p.lineTo(at(0.05, 0.95));
    }
    return p;
}

// ---------------------------------------------------------------- painting

void paintMark(QPainter &p, const Mark &m)
{
    p.save();
    switch (m.kind) {
    case Mark::Text: {
        p.setPen(m.color);
        const QFont f = screenFont();
        p.setFont(f);
        const qreal s = m.fontSize / 100.0;
        const QStringList lines = displayText(m.text).split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            p.save();
            p.translate(m.rect.left(), m.rect.top() + (kAscent + i * kLeading) * m.fontSize);
            p.scale(s, s);
            p.drawText(QPointF(0, 0), lines.at(i));
            p.restore();
        }
        break;
    }
    case Mark::Check:
    case Mark::Cross: {
        QPen pen(m.color, strokeWidth(m.rect), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(symbolPath(m.kind, m.rect));
        break;
    }
    case Mark::Signature:
        if (!m.image.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(m.rect, m.image);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(m.color);
            p.drawPath(placedSignature(m));
        }
        break;
    }
    p.restore();
}

void paint(QPainter &p, const QVector<Mark> &marks, const QTransform &pageToDevice)
{
    if (marks.isEmpty())
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setTransform(pageToDevice, true);
    for (const Mark &m : marks)
        paintMark(p, m);
    p.restore();
}

// ---------------------------------------------------------------- PDF

QByteArray pdfOps(const QVector<Mark> &marks)
{
    QByteArray out;
    for (int i = 0; i < marks.size(); ++i) {
        const Mark &m = marks.at(i);
        out += "q\n";
        switch (m.kind) {
        case Mark::Text: {
            out += colorOps(m.color, false);
            const QStringList lines = m.text.split(QLatin1Char('\n'));
            for (int l = 0; l < lines.size(); ++l) {
                if (lines.at(l).isEmpty())
                    continue;
                const qreal baseline = m.rect.top() + (kAscent + l * kLeading) * m.fontSize;
                // Display space has y pointing down, so flip the text matrix to keep letters upright.
                out += "BT /CorbataHelv " + num(m.fontSize) + " Tf 1 0 0 -1 " + num(m.rect.left()) + ' ' +
                       num(baseline) + " Tm " + pdfString(encodeWinAnsi(lines.at(l))) + " Tj ET\n";
            }
            break;
        }
        case Mark::Check:
        case Mark::Cross:
            out += colorOps(m.color, true);
            out += num(strokeWidth(m.rect)) + " w 1 J 1 j\n";
            out += pathOps(symbolPath(m.kind, m.rect)) + "S\n";
            break;
        case Mark::Signature:
            if (!m.image.isNull()) {
                // The image's unit square, flipped so its first row lands at the top.
                out += num(m.rect.width()) + " 0 0 " + num(-m.rect.height()) + ' ' + num(m.rect.left()) + ' ' +
                       num(m.rect.bottom()) + " cm /CorbataImg" + QByteArray::number(i) + " Do\n";
            } else {
                const QPainterPath placed = placedSignature(m);
                out += colorOps(m.color, false);
                out += pathOps(placed) + (placed.fillRule() == Qt::WindingFill ? "f\n" : "f*\n");
            }
            break;
        }
        out += "Q\n";
    }
    return out;
}

} // namespace Marks
