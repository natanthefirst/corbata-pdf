#include "SignatureDialog.h"

#include <QBuffer>
#include <QComboBox>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kMaxSaved = 4;
const char *kSettingsKey = "signatures";

// Renders a signature into a preview image of the given size.
QPixmap preview(const Signature &s, QSize size, qreal dpr)
{
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QSizeF natural = s.naturalSize();
    if (natural.isEmpty())
        return pm;
    QRectF target(QPointF(0, 0), natural.scaled(QSizeF(size) * 0.9, Qt::KeepAspectRatio));
    target.moveCenter(QRectF(QPointF(0, 0), QSizeF(size)).center());
    if (!s.image.isNull()) {
        p.drawImage(target, s.image);
    } else {
        const QRectF src = s.path.boundingRect();
        p.translate(target.topLeft());
        p.scale(target.width() / src.width(), target.height() / src.height());
        p.translate(-src.topLeft());
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawPath(s.path);
    }
    return pm;
}

QStringList scriptFonts()
{
    static const char *candidates[] = {"Segoe Script", "Brush Script MT", "Lucida Handwriting", "Snell Roundhand",
                                       "Apple Chancery", "Savoye LET", "Zapfino", "Bradley Hand", "Ink Free",
                                       "Segoe Print", "Freestyle Script", "Edwardian Script ITC", "Vivaldi"};
    const QStringList families = QFontDatabase::families();
    QStringList out;
    for (const char *c : candidates)
        if (families.contains(QString::fromLatin1(c), Qt::CaseInsensitive))
            out << QString::fromLatin1(c);
    if (out.isEmpty())
        out << QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    return out;
}

} // namespace

QSizeF Signature::naturalSize() const
{
    if (!image.isNull())
        return QSizeF(image.size());
    return path.boundingRect().size();
}

// ---------------------------------------------------------------- drawing pad

class SignaturePad : public QWidget
{
public:
    explicit SignaturePad(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(460, 170);
        setCursor(Qt::CrossCursor);
        setAttribute(Qt::WA_StaticContents);
    }

    std::function<void()> changed;

    bool isEmpty() const { return m_strokes.isEmpty(); }
    void clear()
    {
        m_strokes.clear();
        update();
        if (changed)
            changed();
    }

    // The strokes as one filled outline (what goes into the PDF).
    QPainterPath outline() const
    {
        QPainterPath all;
        all.setFillRule(Qt::WindingFill);
        QPainterPathStroker stroker;
        stroker.setWidth(kPen);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        for (const QPolygonF &s : m_strokes) {
            if (s.size() == 1) {
                all.addEllipse(s.first(), kPen / 2, kPen / 2);
                continue;
            }
            all.addPath(stroker.createStroke(smooth(s)));
        }
        all.setFillRule(Qt::WindingFill);
        return all;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Qt::white);
        p.setPen(QPen(QColor(0, 0, 0, 60), 1, Qt::DashLine));
        const int base = height() * 3 / 4;
        p.drawLine(20, base, width() - 20, base);
        if (m_strokes.isEmpty()) {
            p.setPen(QColor(0, 0, 0, 110));
            p.drawText(rect().adjusted(0, 0, 0, -height() / 3), Qt::AlignCenter, tr("Sign here with your mouse, trackpad or pen"));
        }
        QPen pen(Qt::black, kPen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        for (const QPolygonF &s : m_strokes) {
            if (s.size() == 1)
                p.drawPoint(s.first());
            else
                p.drawPath(smooth(s));
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        m_strokes.append(QPolygonF{e->position()});
        update();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton) || m_strokes.isEmpty())
            return;
        const QPointF pt = e->position();
        if (QLineF(m_strokes.last().last(), pt).length() >= 1.5) {
            m_strokes.last().append(pt);
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        if (changed)
            changed();
    }

private:
    static constexpr qreal kPen = 2.6;

    // Quadratic curves through the midpoints: a smooth line from jittery input.
    static QPainterPath smooth(const QPolygonF &pts)
    {
        QPainterPath path(pts.first());
        if (pts.size() == 2) {
            path.lineTo(pts.last());
            return path;
        }
        for (int i = 1; i + 1 < pts.size(); ++i)
            path.quadTo(pts[i], (pts[i] + pts[i + 1]) / 2);
        path.lineTo(pts.last());
        return path;
    }

    QList<QPolygonF> m_strokes;
};

// ---------------------------------------------------------------- dialog

SignatureDialog::SignatureDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Signature"));

    m_savedBox = new QWidget;
    auto *savedLayout = new QVBoxLayout(m_savedBox);
    savedLayout->setContentsMargins(0, 0, 0, 0);
    savedLayout->addWidget(new QLabel(tr("Use a saved signature (right-click to remove):")));
    m_savedRow = new QHBoxLayout;
    savedLayout->addLayout(m_savedRow);

    // Draw
    auto *drawPage = new QWidget;
    m_pad = new SignaturePad;
    auto *clear = new QPushButton(tr("Clear"));
    connect(clear, &QPushButton::clicked, this, [this] { m_pad->clear(); });
    m_pad->changed = [this] { updateOk(); };
    auto *drawLayout = new QVBoxLayout(drawPage);
    drawLayout->addWidget(m_pad, 1);
    auto *drawBar = new QHBoxLayout;
    drawBar->addStretch();
    drawBar->addWidget(clear);
    drawLayout->addLayout(drawBar);

    // Type
    auto *typePage = new QWidget;
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Type your name"));
    m_font = new QComboBox;
    for (const QString &f : scriptFonts())
        m_font->addItem(f);
    m_typedPreview = new QLabel;
    m_typedPreview->setMinimumHeight(120);
    m_typedPreview->setAlignment(Qt::AlignCenter);
    m_typedPreview->setStyleSheet(QStringLiteral("background: white; border: 1px solid #ccc;"));
    connect(m_name, &QLineEdit::textChanged, this, [this] { updateTypedPreview(); updateOk(); });
    connect(m_font, &QComboBox::currentIndexChanged, this, [this] { updateTypedPreview(); });
    auto *typeLayout = new QVBoxLayout(typePage);
    auto *typeRow = new QHBoxLayout;
    typeRow->addWidget(m_name, 1);
    typeRow->addWidget(m_font);
    typeLayout->addLayout(typeRow);
    typeLayout->addWidget(m_typedPreview, 1);

    // Image
    auto *imagePage = new QWidget;
    auto *choose = new QPushButton(tr("Choose Image…"));
    connect(choose, &QPushButton::clicked, this, &SignatureDialog::chooseImage);
    m_imagePreview = new QLabel(tr("A photo or scan of your signature on white paper (PNG or JPG).\n"
                                   "The white background is removed automatically."));
    m_imagePreview->setAlignment(Qt::AlignCenter);
    m_imagePreview->setMinimumHeight(120);
    m_imagePreview->setStyleSheet(QStringLiteral("background: white; border: 1px solid #ccc;"));
    auto *imageLayout = new QVBoxLayout(imagePage);
    imageLayout->addWidget(m_imagePreview, 1);
    auto *imageBar = new QHBoxLayout;
    imageBar->addStretch();
    imageBar->addWidget(choose);
    imageLayout->addLayout(imageBar);

    m_tabs = new QTabWidget;
    m_tabs->addTab(drawPage, tr("Draw"));
    m_tabs->addTab(typePage, tr("Type"));
    m_tabs->addTab(imagePage, tr("Image"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { updateOk(); });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setText(tr("Use Signature"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SignatureDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_savedBox);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttons);

    rebuildSaved();
    updateTypedPreview();
    updateOk();
    resize(560, 380);
}

Signature SignatureDialog::getSignature(QWidget *parent)
{
    SignatureDialog dlg(parent);
    if (dlg.exec() != QDialog::Accepted)
        return {};
    return dlg.signature();
}

Signature SignatureDialog::current() const
{
    Signature s;
    switch (m_tabs->currentIndex()) {
    case 0:
        if (!m_pad->isEmpty())
            s.path = m_pad->outline();
        break;
    case 1:
        if (!m_name->text().trimmed().isEmpty()) {
            QFont f(m_font->currentText());
            f.setPixelSize(72);
            s.path.addText(0, 0, f, m_name->text().trimmed());
            s.path.setFillRule(Qt::WindingFill);
        }
        break;
    case 2:
        s.image = m_image;
        break;
    }
    return s;
}

void SignatureDialog::updateOk() { m_ok->setEnabled(!current().isNull()); }

void SignatureDialog::updateTypedPreview()
{
    const Signature s = current();
    if (m_tabs->currentIndex() != 1 || s.isNull()) {
        m_typedPreview->setPixmap({});
        return;
    }
    m_typedPreview->setPixmap(preview(s, QSize(420, 110), devicePixelRatioF()));
}

void SignatureDialog::chooseImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose Signature Image"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
    if (path.isEmpty())
        return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read().convertToFormat(QImage::Format_ARGB32);
    if (img.isNull())
        return;
    if (img.width() > 1600)
        img = img.scaledToWidth(1600, Qt::SmoothTransformation);
    // Paper becomes transparent: lighter pixels fade out, ink stays.
    int left = img.width(), top = img.height(), right = -1, bottom = -1;
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = line[x];
            const int lum = qGray(c);
            const int a = qBound(0, (225 - lum) * 255 / 120, 255) * qAlpha(c) / 255;
            line[x] = qRgba(qRed(c), qGreen(c), qBlue(c), a);
            if (a > 24) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
        }
    }
    if (right < left)
        return;
    m_image = img.copy(QRect(QPoint(left, top), QPoint(right, bottom)).adjusted(-4, -4, 4, 4) & img.rect());
    Signature s;
    s.image = m_image;
    m_imagePreview->setPixmap(preview(s, QSize(420, 110), devicePixelRatioF()));
    updateOk();
}

void SignatureDialog::accept()
{
    m_result = current();
    if (m_result.isNull())
        return;
    QList<Signature> saved = loadSaved();
    saved.prepend(m_result);
    while (saved.size() > kMaxSaved)
        saved.removeLast();
    storeSaved(saved);
    QDialog::accept();
}

void SignatureDialog::rebuildSaved()
{
    while (QLayoutItem *item = m_savedRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<Signature> saved = loadSaved();
    m_savedBox->setVisible(!saved.isEmpty());
    for (int i = 0; i < saved.size(); ++i) {
        auto *b = new QToolButton;
        b->setIcon(QIcon(preview(saved.at(i), QSize(110, 44), devicePixelRatioF())));
        b->setIconSize(QSize(110, 44));
        b->setToolTip(tr("Use this signature"));
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        const Signature s = saved.at(i);
        connect(b, &QToolButton::clicked, this, [this, s] {
            m_result = s;
            QList<Signature> list = loadSaved();
            // Move it to the front.
            for (int k = 0; k < list.size(); ++k)
                if (list.at(k).path == s.path && list.at(k).image == s.image) {
                    list.move(k, 0);
                    break;
                }
            storeSaved(list);
            QDialog::accept();
        });
        connect(b, &QWidget::customContextMenuRequested, this, [this, b, i] {
            QMenu menu;
            QAction *remove = menu.addAction(tr("Remove"));
            if (menu.exec(b->mapToGlobal(QPoint(0, b->height()))) == remove) {
                QList<Signature> list = loadSaved();
                if (i < list.size())
                    list.removeAt(i);
                storeSaved(list);
                QMetaObject::invokeMethod(this, [this] { rebuildSaved(); }, Qt::QueuedConnection);
            }
        });
        m_savedRow->addWidget(b);
    }
    m_savedRow->addStretch();
}

QList<Signature> SignatureDialog::loadSaved()
{
    QList<Signature> out;
    const QVariantList list = QSettings().value(QString::fromLatin1(kSettingsKey)).toList();
    for (const QVariant &v : list) {
        QByteArray bytes = v.toByteArray();
        QDataStream in(&bytes, QIODevice::ReadOnly);
        quint8 kind = 0;
        Signature s;
        in >> kind;
        if (kind == 0) {
            in >> s.path;
        } else {
            QByteArray png;
            in >> png;
            s.image.loadFromData(png, "PNG");
            s.image = s.image.convertToFormat(QImage::Format_ARGB32);
        }
        if (in.status() == QDataStream::Ok && !s.isNull())
            out << s;
    }
    return out;
}

void SignatureDialog::storeSaved(const QList<Signature> &list)
{
    QVariantList out;
    for (const Signature &s : list) {
        QByteArray bytes;
        QDataStream o(&bytes, QIODevice::WriteOnly);
        if (!s.image.isNull()) {
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            s.image.save(&buf, "PNG");
            o << quint8(1) << png;
        } else {
            o << quint8(0) << s.path;
        }
        out << bytes;
    }
    QSettings().setValue(QString::fromLatin1(kSettingsKey), out);
}
