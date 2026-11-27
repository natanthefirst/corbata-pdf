#include "PageGrid.h"
#include "Library.h"
#include "PageModel.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QUrl>

static const char *kPageMime = "application/x-corbata-pages";
static constexpr int kPad = 14;
static constexpr int kLabel = 22;

// ---------------------------------------------------------------- delegate

PageDelegate::PageDelegate(Library *library, QObject *parent)
    : QStyledItemDelegate(parent), m_library(library)
{
}

QSize PageDelegate::cellSize() const
{
    return QSize(m_thumb + 2 * kPad, m_thumb + 2 * kPad + kLabel);
}

QSize PageDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return cellSize();
}

void PageDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const PageRef ref = index.data(PageModel::PageRefRole).value<PageRef>();
    const QRect cell = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const QPalette &pal = option.palette;

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);

    if (selected) {
        QColor fill = pal.color(QPalette::Highlight);
        fill.setAlpha(55);
        p->setPen(QPen(pal.color(QPalette::Highlight), 2));
        p->setBrush(fill);
        p->drawRoundedRect(QRectF(cell).adjusted(3, 3, -3, -3), 8, 8);
    } else if (option.state & QStyle::State_MouseOver) {
        QColor fill = pal.color(QPalette::Highlight);
        fill.setAlpha(20);
        p->setPen(Qt::NoPen);
        p->setBrush(fill);
        p->drawRoundedRect(QRectF(cell).adjusted(3, 3, -3, -3), 8, 8);
    }

    // Fit the page (with the user's rotation) into the thumbnail square.
    const QRect area(cell.left() + kPad, cell.top() + kPad, m_thumb, m_thumb);
    QSizeF disp = ref.displaySize();
    if (disp.isEmpty())
        disp = QSizeF(612, 792);
    const QSize fitted = disp.scaled(QSizeF(area.size()), Qt::KeepAspectRatio).toSize();
    QRect page(QPoint(0, 0), fitted);
    page.moveCenter(area.center());
    page.moveBottom(area.bottom()); // pages sit on a common baseline

    // Soft shadow.
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, 40));
    p->drawRect(page.translated(2, 3));

    p->setBrush(Qt::white);
    p->drawRect(page);

    if (!ref.isBlank()) {
        const qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
        const int needPx = int(qMax(fitted.width(), fitted.height()) * dpr);
        QImage img = m_library->thumbnail(ref.sourceId, ref.pageIndex, needPx);
        if (!img.isNull()) {
            if (ref.rotation)
                img = img.transformed(QTransform().rotate(ref.rotation), Qt::SmoothTransformation);
            p->drawImage(page, img);
        }
    }

    // Fill & Sign marks and changed form values, drawn over the page.
    const QTransform toPage = pageToTarget(ref, QRectF(page));
    if (!ref.isBlank()) {
        const auto *model = qobject_cast<const PageModel *>(index.model());
        const QHash<QString, QString> changed = model ? model->fieldValues().value(ref.sourceId) : QHash<QString, QString>();
        if (!changed.isEmpty())
            FormFields::paint(*p, m_library->fields(ref.sourceId), changed, ref.pageIndex, toPage, false);
    }
    Marks::paint(*p, ref.marks, toPage);

    p->setPen(QPen(QColor(0, 0, 0, 70), 1));
    p->setBrush(Qt::NoBrush);
    p->drawRect(QRectF(page).adjusted(0.5, 0.5, -0.5, -0.5));

    // Page number label.
    const QRect label(cell.left(), area.bottom() + 4, cell.width(), kLabel);
    QFont f = option.font;
    if (selected)
        f.setBold(true);
    p->setFont(f);
    p->setPen(pal.color(QPalette::Text));
    QString text = index.data(Qt::DisplayRole).toString();
    if (ref.isBlank())
        text += tr("  (blank)");
    p->drawText(label, Qt::AlignHCenter | Qt::AlignTop, text);

    p->restore();
}

// ---------------------------------------------------------------- grid

PageGrid::PageGrid(Library *library, QWidget *parent)
    : QListView(parent), m_delegate(new PageDelegate(library, this))
{
    setItemDelegate(m_delegate);
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static); // we handle reordering ourselves
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionRectVisible(true);
    setMouseTracking(true);
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(false);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    verticalScrollBar()->setSingleStep(24);
    setFrameShape(QFrame::NoFrame);
    updateGrid();

    connect(library, &Library::thumbnailsUpdated, viewport(), qOverload<>(&QWidget::update));
}

void PageGrid::updateGrid()
{
    setGridSize(m_delegate->cellSize());
}

void PageGrid::setThumbSize(int px)
{
    m_delegate->setThumbSize(px);
    updateGrid();
    doItemsLayout();
    viewport()->update();
}

QList<int> PageGrid::selectedRows() const
{
    QList<int> rows;
    for (const QModelIndex &i : selectionModel()->selectedIndexes())
        rows << i.row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

int PageGrid::dropRowAt(const QPoint &pos) const
{
    // Items run left-to-right, then wrap. The drop row is the first item that is
    // "after" the cursor: on a later line, or on this line and right of the cursor.
    const int n = model() ? model()->rowCount() : 0;
    for (int r = 0; r < n; ++r) {
        const QRect rect = visualRect(model()->index(r, 0));
        if (rect.top() > pos.y())
            return r;
        if (pos.y() <= rect.bottom() && pos.x() < rect.center().x())
            return r;
    }
    return n;
}

QStringList PageGrid::pdfPaths(const QMimeData *mime)
{
    QStringList paths;
    if (!mime->hasUrls())
        return paths;
    for (const QUrl &url : mime->urls()) {
        const QString local = url.toLocalFile();
        if (!local.isEmpty() && QFileInfo(local).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0)
            paths << local;
    }
    return paths;
}

bool PageGrid::acceptsDrag(const QMimeData *mime) const
{
    return mime->hasFormat(QLatin1String(kPageMime)) || !pdfPaths(mime).isEmpty();
}

void PageGrid::startDrag(Qt::DropActions)
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty())
        return;
    auto *mime = new QMimeData;
    mime->setData(QLatin1String(kPageMime), QByteArray::number(rows.size()));
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);

    const QModelIndex cur = currentIndex().isValid() ? currentIndex() : model()->index(rows.first(), 0);
    QPixmap pm = viewport()->grab(visualRect(cur));
    if (!pm.isNull()) {
        pm = pm.scaledToWidth(qMin(pm.width(), 120), Qt::SmoothTransformation);
        if (rows.size() > 1) {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const QRect badge(pm.width() - 34, 4, 30, 22);
            p.setBrush(palette().color(QPalette::Highlight));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(badge, 11, 11);
            p.setPen(palette().color(QPalette::HighlightedText));
            p.drawText(badge, Qt::AlignCenter, QString::number(rows.size()));
        }
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
    }
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
    m_dropRow = -1;
    viewport()->update();
}

void PageGrid::dragEnterEvent(QDragEnterEvent *event)
{
    if (!acceptsDrag(event->mimeData())) {
        event->ignore();
        return;
    }
    m_dropRow = dropRowAt(event->position().toPoint());
    event->setDropAction(event->mimeData()->hasFormat(QLatin1String(kPageMime)) ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
    viewport()->update();
}

void PageGrid::dragMoveEvent(QDragMoveEvent *event)
{
    if (!acceptsDrag(event->mimeData())) {
        event->ignore();
        return;
    }
    QListView::dragMoveEvent(event); // gives us auto-scroll near the edges
    m_dropRow = dropRowAt(event->position().toPoint());
    event->setDropAction(event->mimeData()->hasFormat(QLatin1String(kPageMime)) ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
    viewport()->update();
}

void PageGrid::dragLeaveEvent(QDragLeaveEvent *event)
{
    QListView::dragLeaveEvent(event);
    m_dropRow = -1;
    viewport()->update();
}

void PageGrid::dropEvent(QDropEvent *event)
{
    const int row = dropRowAt(event->position().toPoint());
    m_dropRow = -1;
    viewport()->update();

    const QMimeData *mime = event->mimeData();
    if (mime->hasFormat(QLatin1String(kPageMime)) && event->source() == this) {
        // Report as a copy so Qt doesn't try to remove the dragged rows itself.
        event->setDropAction(Qt::CopyAction);
        event->accept();
        emit pagesMoved(selectedRows(), row);
        return;
    }
    const QStringList paths = pdfPaths(mime);
    if (!paths.isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        emit filesDropped(paths, row);
        return;
    }
    event->ignore();
}

void PageGrid::paintEvent(QPaintEvent *event)
{
    QListView::paintEvent(event);
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);

    const int n = model() ? model()->rowCount() : 0;
    if (n == 0) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.4);
        p.setFont(f);
        QColor c = palette().color(QPalette::Text);
        c.setAlpha(120);
        p.setPen(c);
        QRect r = viewport()->rect().adjusted(40, 40, -40, -40);
        QPen dash(c, 2, Qt::DashLine);
        p.save();
        p.setPen(dash);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 16, 16);
        p.restore();
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Drop PDF files here\n\nor press %1 to open one")
                       .arg(QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText)));
        return;
    }

    if (m_dropRow < 0)
        return;
    QRect anchor;
    int x;
    if (m_dropRow < n) {
        anchor = visualRect(model()->index(m_dropRow, 0));
        x = anchor.left();
    } else {
        anchor = visualRect(model()->index(n - 1, 0));
        x = anchor.right();
    }
    const QColor hl = palette().color(QPalette::Highlight);
    p.setPen(Qt::NoPen);
    p.setBrush(hl);
    p.drawRoundedRect(QRectF(x - 2, anchor.top() + 8, 4, anchor.height() - 16), 2, 2);
    p.drawEllipse(QPointF(x, anchor.top() + 8), 5, 5);
    p.drawEllipse(QPointF(x, anchor.bottom() - 8), 5, 5);
}

void PageGrid::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
        return;
    }
    QListView::wheelEvent(event);
}
