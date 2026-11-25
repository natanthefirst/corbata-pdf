#include "PreviewDialog.h"
#include "Library.h"
#include "PageModel.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

PreviewDialog::PreviewDialog(Library *library, PageModel *model, int row, QWidget *parent)
    : QDialog(parent), m_library(library), m_model(model), m_row(row)
{
    setWindowTitle(tr("Preview"));
    m_image = new QLabel;
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setMinimumSize(200, 200);
    m_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    m_caption = new QLabel;
    m_prev = new QPushButton(tr("← Previous"));
    m_next = new QPushButton(tr("Next →"));
    auto *close = new QPushButton(tr("Close"));
    m_prev->setAutoDefault(false);
    m_next->setAutoDefault(false);
    connect(m_prev, &QPushButton::clicked, this, [this] { step(-1); });
    connect(m_next, &QPushButton::clicked, this, [this] { step(1); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_prev);
    bar->addWidget(m_next);
    bar->addStretch();
    bar->addWidget(m_caption);
    bar->addStretch();
    bar->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_image, 1);
    layout->addLayout(bar);

    const QSize avail = screen() ? screen()->availableSize() : QSize(1280, 900);
    resize(int(avail.width() * 0.55), int(avail.height() * 0.88));
}

void PreviewDialog::step(int delta)
{
    const int n = m_model->rowCount();
    const int next = qBound(0, m_row + delta, n - 1);
    if (next != m_row) {
        m_row = next;
        refresh();
    }
}

void PreviewDialog::refresh()
{
    const int n = m_model->rowCount();
    if (m_row < 0 || m_row >= n)
        return;
    const PageRef ref = m_model->index(m_row, 0).data(PageModel::PageRefRole).value<PageRef>();
    m_caption->setText(tr("Page %1 of %2").arg(m_row + 1).arg(n));
    m_prev->setEnabled(m_row > 0);
    m_next->setEnabled(m_row < n - 1);

    const qreal dpr = devicePixelRatioF();
    const QSize box = m_image->size() * dpr;
    QSizeF disp = ref.displaySize();
    if (disp.isEmpty())
        disp = QSizeF(612, 792);
    const QSize fitted = disp.scaled(QSizeF(box), Qt::KeepAspectRatio).toSize();
    if (fitted.isEmpty())
        return;

    QImage img;
    if (ref.isBlank()) {
        img = QImage(fitted, QImage::Format_RGB32);
        img.fill(Qt::white);
    } else {
        img = m_library->render(ref.sourceId, ref.pageIndex, qMax(fitted.width(), fitted.height()));
        if (ref.rotation)
            img = img.transformed(QTransform().rotate(ref.rotation), Qt::SmoothTransformation);
    }
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    m_image->setPixmap(pm);
}

void PreviewDialog::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp:
        step(-1);
        return;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        step(1);
        return;
    case Qt::Key_Home:
        step(-m_row);
        return;
    case Qt::Key_End:
        step(m_model->rowCount());
        return;
    default:
        QDialog::keyPressEvent(event);
    }
}

void PreviewDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    refresh();
}
