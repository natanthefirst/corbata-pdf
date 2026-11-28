#include "DocumentView.h"
#include "Library.h"
#include "PageModel.h"

#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QGuiApplication>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QToolButton>
#include <QComboBox>
#include <QDate>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kMargin = 20;      // grey space around the pages, in pixels
constexpr qreal kHandle = 10;    // resize handle, in pixels
constexpr qreal kMinZoom = 0.2;
constexpr qreal kMaxZoom = 6.0;
constexpr int kMaxCachedPages = 16;
const QColor kBlueInk(20, 60, 170);
const QColor kSurround(0xd6, 0xd6, 0xd6);

QSizeF displaySizeOf(const PageRef &p) { return p.displaySize().isEmpty() ? QSizeF(612, 792) : p.displaySize(); }

QStringList pdfPaths(const QMimeData *mime)
{
    QStringList out;
    if (mime)
        for (const QUrl &u : mime->urls())
            if (u.isLocalFile() && QFileInfo(u.toLocalFile()).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0)
                out << u.toLocalFile();
    return out;
}
} // namespace

// ---------------------------------------------------------------- canvas

class ViewCanvas : public QWidget
{
public:
    explicit ViewCanvas(DocumentView *view) : QWidget(), m_view(view)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAcceptDrops(true);
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QPainter p(this);
        m_view->paintCanvas(p, e->rect());
    }
    void mousePressEvent(QMouseEvent *e) override { m_view->canvasPress(e); }
    void mouseMoveEvent(QMouseEvent *e) override { m_view->canvasMove(e); }
    void mouseReleaseEvent(QMouseEvent *e) override { m_view->canvasRelease(e); }
    void mouseDoubleClickEvent(QMouseEvent *e) override { m_view->canvasDoubleClick(e); }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (!m_view->canvasKey(e))
            QWidget::keyPressEvent(e); // let the scroll area scroll
    }
    void wheelEvent(QWheelEvent *e) override
    {
        if (e->modifiers() & Qt::ControlModifier) {
            m_view->setZoom(m_view->m_zoom * (e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15));
            e->accept();
            return;
        }
        if (m_view->m_scroll->verticalScrollBar()->maximum() == 0) {
            // The whole page is visible: the wheel turns pages.
            m_wheel += e->angleDelta().y();
            if (std::abs(m_wheel) >= 120) {
                m_view->showPage(m_view->m_current + (m_wheel < 0 ? 1 : -1));
                m_wheel = 0;
            }
            e->accept();
            return;
        }
        QWidget::wheelEvent(e);
    }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (!pdfPaths(e->mimeData()).isEmpty())
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        const QStringList paths = pdfPaths(e->mimeData());
        if (!paths.isEmpty()) {
            e->acceptProposedAction();
            emit m_view->filesDropped(paths);
        }
    }

private:
    DocumentView *m_view;
    int m_wheel = 0;
};

// ---------------------------------------------------------------- setup

DocumentView::DocumentView(Library *library, PageModel *model, Commit commit, QWidget *parent)
    : QWidget(parent), m_library(library), m_model(model), m_commit(std::move(commit))
{
    // --- Fill & Sign tools (only in Fill & Sign mode)
    m_toolsBar = new QToolBar;
    QToolBar *tb = m_toolsBar;
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_tools = new QActionGroup(this);
    const struct {
        Tool tool;
        QString text;
        QString tip;
    } tools[] = {
        {Tool::Select, tr("Select"), tr("Fill in form fields, and move or resize what you added (Esc)")},
        {Tool::Text, tr("Text"), tr("Click on the page to type (T)")},
        {Tool::Check, tr("✓ Check"), tr("Click to add a check mark (K)")},
        {Tool::Cross, tr("✗ Cross"), tr("Click to add a cross (X)")},
        {Tool::Date, tr("Date"), tr("Click to add today's date (D)")},
        {Tool::Sign, tr("Sign…"), tr("Choose your signature, then click where it goes (S)")},
    };
    for (const auto &t : tools) {
        QAction *a = tb->addAction(t.text);
        a->setCheckable(true);
        a->setToolTip(t.tip);
        m_tools->addAction(a);
        m_toolActions[int(t.tool)] = a;
        const Tool tool = t.tool;
        connect(a, &QAction::triggered, this, [this, tool] { setTool(tool); });
    }
    m_toolActions[0]->setChecked(true);

    tb->addSeparator();
    tb->addWidget(new QLabel(tr(" Size ")));
    m_fontSize = new QSpinBox;
    m_fontSize->setRange(4, 96);
    m_fontSize->setValue(12);
    m_fontSize->setToolTip(tr("Text size in points (also the size of new check marks)"));
    tb->addWidget(m_fontSize);
    tb->addWidget(new QLabel(QStringLiteral("  ")));
    m_color = new QComboBox;
    m_color->addItem(tr("Black"));
    m_color->addItem(tr("Blue"));
    m_color->setToolTip(tr("Ink colour"));
    tb->addWidget(m_color);
    connect(m_fontSize, &QSpinBox::valueChanged, this, [this] { applyStyleToSelected(); });
    connect(m_color, &QComboBox::currentIndexChanged, this, [this] { applyStyleToSelected(); });

    // --- page navigation and zoom (always shown)
    auto *nav = new QToolBar;
    nav->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    nav->setIconSize(QSize(16, 16));
    QAction *search = nav->addAction(QIcon::fromTheme(QIcon::ThemeIcon::EditFind), tr("Search"), this, [this] { showFind(); });
    search->setToolTip(tr("Find text in the document (Ctrl+F)"));
    nav->addSeparator();
    QAction *prev = nav->addAction(tr("◀"), this, [this] { showPage(m_current - 1); });
    prev->setToolTip(tr("Previous page (Left arrow)"));
    // The page number: type one and press Enter to go there.
    m_pageBox = new QSpinBox;
    m_pageBox->setObjectName(QStringLiteral("pageNumber"));
    m_pageBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pageBox->setAlignment(Qt::AlignRight);
    m_pageBox->setKeyboardTracking(false);
    m_pageBox->setMinimumWidth(44);
    m_pageBox->setToolTip(tr("Type a page number and press Enter"));
    m_pageBox->setRange(0, 0);
    connect(m_pageBox, &QSpinBox::editingFinished, this, [this] {
        showPage(m_pageBox->value() - 1);
        updateCaption();
        m_canvas->setFocus();
    });
    m_pageTotal = new QLabel;
    m_pageTotal->setMinimumWidth(40);
    nav->addWidget(m_pageBox);
    nav->addWidget(m_pageTotal);
    QAction *next = nav->addAction(tr("▶"), this, [this] { showPage(m_current + 1); });
    next->setToolTip(tr("Next page (Right arrow)"));
    nav->addSeparator();
    QAction *zoomOut = nav->addAction(tr("−"), this, [this] { setZoom(m_zoom / 1.25); });
    zoomOut->setToolTip(tr("Zoom out (Ctrl+mouse wheel)"));
    QAction *zoomIn = nav->addAction(tr("+"), this, [this] { setZoom(m_zoom * 1.25); });
    zoomIn->setToolTip(tr("Zoom in (Ctrl+mouse wheel)"));
    m_fitWidthAct = nav->addAction(tr("Fit Width"), this, [this] { setFit(Fit::Width); });
    m_fitWidthAct->setToolTip(tr("As wide as the window; scroll to see the whole page"));
    m_fitPageAct = nav->addAction(tr("Fit Page"), this, [this] { setFit(Fit::Page); });
    m_fitPageAct->setToolTip(tr("The whole page in the window"));
    for (QAction *a : {m_fitWidthAct, m_fitPageAct})
        a->setCheckable(true);
    m_fitPageAct->setChecked(true);

    auto *bars = new QHBoxLayout;
    bars->setContentsMargins(0, 0, 0, 0);
    bars->setSpacing(0);
    bars->addWidget(m_toolsBar);
    bars->addStretch();
    bars->addWidget(nav);

    // --- search field (Ctrl+F)
    m_findBar = new QWidget;
    auto *find = new QHBoxLayout(m_findBar);
    find->setContentsMargins(8, 3, 8, 3);
    m_findEdit = new QLineEdit;
    m_findEdit->setObjectName(QStringLiteral("findText"));
    m_findEdit->setPlaceholderText(tr("Find in document"));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setMaximumWidth(320);
    m_findEdit->installEventFilter(this);
    m_findCount = new QLabel;
    m_findCount->setObjectName(QStringLiteral("findCount"));
    m_findCount->setMinimumWidth(90);
    auto button = [this](const QString &text, const QString &tip, auto fn) {
        auto *b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        return b;
    };
    find->addWidget(new QLabel(tr("Find:")));
    find->addWidget(m_findEdit);
    find->addWidget(button(tr("↑"), tr("Previous match (Shift+Enter)"), [this] { findPrevious(); }));
    find->addWidget(button(tr("↓"), tr("Next match (Enter)"), [this] { findNext(); }));
    find->addWidget(m_findCount);
    find->addStretch();
    find->addWidget(button(tr("✕"), tr("Close (Esc)"), [this] { hideFind(); }));
    m_findBar->hide();
    m_findTimer.setSingleShot(true);
    m_findTimer.setInterval(250);
    connect(&m_findTimer, &QTimer::timeout, this, [this] { runSearch(); });
    connect(m_findEdit, &QLineEdit::textChanged, this, [this] { m_findTimer.start(); });

    // --- the page
    m_canvas = new ViewCanvas(this);
    m_scroll = new QScrollArea;
    m_scroll->setWidget(m_canvas);
    m_scroll->setWidgetResizable(false);
    m_scroll->setAlignment(Qt::AlignCenter);
    m_scroll->setFrameShape(QFrame::NoFrame);
    QPalette surround = m_scroll->viewport()->palette();
    surround.setColor(QPalette::Window, kSurround);
    m_scroll->viewport()->setPalette(surround);
    m_scroll->viewport()->setAutoFillBackground(true);
    // Refit whenever the page area changes size (window resized, search field shown).
    m_scroll->installEventFilter(this);

    m_empty = new QLabel(tr("Open a PDF (Ctrl+O) or drag one here."), m_scroll->viewport());
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setStyleSheet(QStringLiteral("color: #555; font-size: 15px;"));
    m_empty->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(bars);
    layout->addWidget(m_findBar);
    layout->addWidget(m_scroll, 1);

    connect(m_model, &QAbstractItemModel::modelReset, this, [this] { reloadFromModel(); });
    setFillMode(false);
    reloadFromModel();
}

QWidget *DocumentView::canvas() const { return m_canvas; }

QPoint DocumentView::canvasPoint(int row, const QPointF &pagePt) const { return toDevice(row).map(pagePt).toPoint(); }

void DocumentView::setFillMode(bool on)
{
    finishEditing();
    m_fill = on;
    m_toolsBar->setVisible(on);
    if (!on) {
        m_selected = 0;
        m_selRow = -1;
        m_tool = Tool::Select;
        m_toolActions[0]->setChecked(true);
        m_canvas->setCursor(Qt::ArrowCursor);
    }
    m_canvas->update();
}

// ---------------------------------------------------------------- document

int DocumentView::rowCount() const { return int(m_model->pages().size()); }

PageRef DocumentView::page(int row) const
{
    const PageList &pages = m_model->pages();
    return (row >= 0 && row < pages.size()) ? pages.at(row) : PageRef();
}

QVector<Mark> DocumentView::marks(int row) const
{
    return (m_drag != Drag::None && row == m_selRow) ? m_dragMarks : page(row).marks;
}

const QVector<FieldInfo> &DocumentView::fields(int row) const
{
    static const QVector<FieldInfo> none;
    const PageRef p = page(row);
    return p.isBlank() ? none : m_library->fields(p.sourceId);
}

QHash<QString, QString> DocumentView::changedValues(int row) const
{
    const PageRef p = page(row);
    return p.isBlank() ? QHash<QString, QString>() : m_model->fieldValues().value(p.sourceId);
}

void DocumentView::commitMarks(int row, const QString &text, const QVector<Mark> &marks)
{
    PageList pages = m_model->pages();
    if (row < 0 || row >= pages.size())
        return;
    pages[row].marks = marks;
    m_commit(text, pages, m_model->fieldValues());
}

void DocumentView::commitField(int row, const QString &name, const QString &value)
{
    const PageRef p = page(row);
    if (p.isBlank())
        return;
    FieldValues values = m_model->fieldValues();
    values[p.sourceId][name] = value;
    m_commit(tr("Fill in \"%1\"").arg(name), m_model->pages(), values);
}

void DocumentView::reloadFromModel()
{
    // Stay on the same page (and scroll position) across edits and undo.
    const int hScroll = m_scroll->horizontalScrollBar()->value();
    const int vScroll = m_scroll->verticalScrollBar()->value();
    m_drag = Drag::None;
    // Drop cached images of pages that are gone.
    QSet<quint64> live;
    for (const PageRef &p : m_model->pages())
        live.insert(p.uid);
    for (auto it = m_images.begin(); it != m_images.end();)
        it = live.contains(it.key()) ? std::next(it) : m_images.erase(it);
    if (m_selRow >= rowCount())
        m_selRow = -1, m_selected = 0;
    m_current = qBound(0, m_current, qMax(0, rowCount() - 1));
    applyFit();
    m_scroll->horizontalScrollBar()->setValue(hScroll);
    m_scroll->verticalScrollBar()->setValue(vScroll);
    updateCaption();
    updateEmptyState();
    if (m_textRow >= rowCount())
        clearTextSelection();
    if (m_findBar->isVisible() && !m_searched.isEmpty())
        runSearch(false); // pages may have moved
}

void DocumentView::finishEditing()
{
    finishTextEdit(true);
    finishFieldEditor(true);
}

// ---------------------------------------------------------------- layout

void DocumentView::layoutPages()
{
    // One page at a time, centred; the scroll area scrolls only if it's bigger
    // than the window.
    if (rowCount() == 0) {
        m_pageRect = QRectF();
        m_canvas->resize(m_scroll->viewport()->size());
        m_canvas->update();
        return;
    }
    const QSizeF s = displaySizeOf(page(m_current)) * m_zoom;
    // At least as big as the window (so the grey surround fills it), with the
    // page centred; bigger if the page is.
    const QSize needed(int(std::ceil(s.width())) + 2 * kMargin, int(std::ceil(s.height())) + 2 * kMargin);
    const int bar = m_scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    QSize canvas = needed.expandedTo(m_scroll->size());
    if (needed.height() > m_scroll->height())
        canvas.setWidth(qMax(needed.width(), m_scroll->width() - bar)); // room for the vertical bar
    if (needed.width() > m_scroll->width())
        canvas.setHeight(qMax(needed.height(), m_scroll->height() - bar));
    m_pageRect = QRectF(QPointF((canvas.width() - s.width()) / 2, (canvas.height() - s.height()) / 2), s);
    m_canvas->resize(canvas);
    m_canvas->update();
}

QTransform DocumentView::toDevice(int row) const { return pageToTarget(page(row), pageRect(row)); }

int DocumentView::rowAt(const QPointF &pt) const
{
    return (rowCount() > 0 && m_pageRect.contains(pt)) ? m_current : -1;
}

void DocumentView::setZoom(qreal z)
{
    m_fit = Fit::Custom;
    m_fitWidthAct->setChecked(false);
    m_fitPageAct->setChecked(false);
    zoomTo(z);
}

void DocumentView::zoomTo(qreal z)
{
    finishEditing();
    // Keep the middle of what you're looking at in the middle.
    QScrollBar *h = m_scroll->horizontalScrollBar(), *v = m_scroll->verticalScrollBar();
    const QSize vp = m_scroll->viewport()->size();
    const qreal fx = m_canvas->width() > 0 ? (h->value() + vp.width() / 2.0) / m_canvas->width() : 0.5;
    const qreal fy = m_canvas->height() > 0 ? (v->value() + vp.height() / 2.0) / m_canvas->height() : 0.0;
    m_zoom = qBound(kMinZoom, z, kMaxZoom);
    layoutPages();
    h->setValue(int(fx * m_canvas->width() - vp.width() / 2.0));
    v->setValue(int(fy * m_canvas->height() - vp.height() / 2.0));
}

void DocumentView::setFit(Fit fit)
{
    m_fit = fit;
    m_fitWidthAct->setChecked(fit == Fit::Width);
    m_fitPageAct->setChecked(fit == Fit::Page);
    applyFit();
}

void DocumentView::applyFit()
{
    if (m_fit == Fit::Custom || rowCount() == 0) {
        layoutPages();
        return;
    }
    const QSizeF s = displaySizeOf(page(m_current));
    const int bar = m_scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    const QSize area = m_scroll->size(); // the whole area, scroll bars or not
    qreal z;
    if (m_fit == Fit::Width) {
        // As wide as possible; the page scrolls vertically, so leave room for that bar.
        const qreal w = area.width() - 2 * kMargin;
        z = w / s.width();
        if (s.height() * z + 2 * kMargin > area.height())
            z = (w - bar) / s.width();
    } else {
        z = qMin((area.width() - 2 * kMargin) / s.width(), (area.height() - 2 * kMargin) / s.height());
    }
    finishEditing();
    m_zoom = qBound(kMinZoom, z, kMaxZoom);
    layoutPages();
}

void DocumentView::showPage(int row)
{
    if (rowCount() == 0)
        return;
    row = qBound(0, row, rowCount() - 1);
    if (row != m_current) {
        finishEditing();
        m_current = row;
        m_selected = 0;
        m_selRow = -1;
        clearTextSelection();
        applyFit(); // pages can differ in size
        emit currentRowChanged(row);
    }
    m_scroll->horizontalScrollBar()->setValue(0);
    m_scroll->verticalScrollBar()->setValue(0);
    updateCaption();
}

void DocumentView::updateCaption()
{
    const int n = rowCount();
    const QSignalBlocker block(m_pageBox);
    m_pageBox->setRange(n ? 1 : 0, n);
    m_pageBox->setValue(n ? m_current + 1 : 0);
    m_pageBox->setEnabled(n > 0);
    m_pageTotal->setText(n ? tr(" / %1").arg(n) : QString());
}

void DocumentView::updateEmptyState()
{
    m_empty->setVisible(rowCount() == 0);
    m_empty->setGeometry(m_scroll->viewport()->rect());
}

QImage DocumentView::pageImage(int row)
{
    const PageRef p = page(row);
    if (p.isBlank())
        return {};
    const qreal dpr = devicePixelRatioF();
    const QSizeF disp = displaySizeOf(p);
    const int px = int(qMax(disp.width(), disp.height()) * m_zoom * dpr);
    const QString key = QStringLiteral("%1/%2/%3/%4").arg(p.sourceId).arg(p.pageIndex).arg(p.rotation).arg(px);
    auto it = m_images.find(p.uid);
    if (it != m_images.end() && it->key == key)
        return it->image;
    if (m_images.size() >= kMaxCachedPages)
        m_images.clear();
    QImage img = m_library->render(p.sourceId, p.pageIndex, px);
    if (p.rotation)
        img = img.transformed(QTransform().rotate(p.rotation), Qt::SmoothTransformation);
    m_images.insert(p.uid, Cached{key, img});
    return img;
}

void DocumentView::paintCanvas(QPainter &p, const QRect &exposed)
{
    p.fillRect(exposed, kSurround);
    if (rowCount() == 0)
        return;
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const int row = m_current;
    const QRectF r = m_pageRect;
    const PageRef ref = page(row);
    p.fillRect(r.translated(2, 3), QColor(0, 0, 0, 55));
    p.fillRect(r, Qt::white);
    const QImage img = pageImage(row);
    if (!img.isNull())
        p.drawImage(r, img);

    const QTransform t = toDevice(row);
    // Fillable fields are tinted only in Fill & Sign mode.
    if (!ref.isBlank())
        FormFields::paint(p, fields(row), changedValues(row), ref.pageIndex, t, m_fill);
    QVector<Mark> shown = marks(row);
    if (m_textEdit && row == m_editRow)
        shown.removeIf([this](const Mark &m) { return m.uid == m_editingMark.uid; });
    Marks::paint(p, shown, t);

    // Search matches (the current one stronger) and the text selection.
    p.setPen(Qt::NoPen);
    for (int i = 0; i < m_matches.size(); ++i) {
        if (m_matches.at(i).row != row)
            continue;
        p.setBrush(i == m_matchIndex ? QColor(255, 140, 0, 120) : QColor(255, 230, 0, 110));
        for (const QPolygonF &poly : m_matches.at(i).bounds)
            p.drawPolygon(t.map(poly));
    }
    if (m_textRow == row && !m_textBounds.isEmpty()) {
        p.setBrush(QColor(40, 110, 255, 70));
        for (const QPolygonF &poly : m_textBounds)
            p.drawPolygon(t.map(poly));
    }

    if (m_fill && row == m_selRow)
        for (const Mark &m : shown)
            if (m.uid == m_selected) {
                p.setPen(QPen(QColor(40, 110, 255), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(t.mapRect(m.rect).adjusted(-3, -3, 3, 3));
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(QColor(40, 110, 255));
                p.drawRect(handleRect());
            }
}

void DocumentView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyFit();
    updateEmptyState();
}

void DocumentView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_textEdit && !m_fieldEdit)
        applyFit();
    updateEmptyState();
}

// ---------------------------------------------------------------- tools

QColor DocumentView::inkColor() const { return m_color->currentIndex() == 1 ? kBlueInk : QColor(Qt::black); }

void DocumentView::setTool(Tool tool)
{
    finishEditing();
    if (tool == Tool::Sign) {
        const Signature s = SignatureDialog::getSignature(this);
        if (s.isNull())
            tool = Tool::Select;
        else
            m_signature = s;
    }
    m_tool = tool;
    m_toolActions[int(tool)]->setChecked(true);
    m_canvas->setCursor(tool == Tool::Select ? Qt::ArrowCursor : tool == Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
    m_canvas->setFocus();
}

bool DocumentView::ensureSignature()
{
    if (!m_signature.isNull())
        return true;
    m_signature = SignatureDialog::getSignature(this);
    return !m_signature.isNull();
}

Mark DocumentView::signatureMark(const QRectF &fit)
{
    Mark m;
    m.kind = Mark::Signature;
    m.path = m_signature.path;
    m.image = m_signature.image;
    m.color = inkColor();
    m.uid = m_nextMarkUid++;
    const QSizeF size = m_signature.naturalSize().scaled(fit.size(), Qt::KeepAspectRatio);
    m.rect = QRectF(QPointF(0, 0), size);
    m.rect.moveCenter(fit.center());
    return m;
}

void DocumentView::placeMark(int row, const QPointF &pt)
{
    const qreal size = m_fontSize->value();
    QVector<Mark> list = page(row).marks;
    Mark m;
    m.uid = m_nextMarkUid++;
    m.color = inkColor();
    m.fontSize = size;
    switch (m_tool) {
    case Tool::Select:
        return;
    case Tool::Text:
        m.kind = Mark::Text;
        m.rect = QRectF(QPointF(pt.x(), pt.y() - 0.6 * size), Marks::textSize(QString(), size));
        startTextEdit(row, m, true);
        return;
    case Tool::Date:
        m.kind = Mark::Text;
        m.text = QLocale::system().toString(QDate::currentDate(), QLocale::ShortFormat);
        m.rect = QRectF(QPointF(pt.x(), pt.y() - 0.6 * size), Marks::textSize(m.text, size));
        list.append(m);
        m_selRow = row;
        m_selected = m.uid;
        commitMarks(row, tr("Add date"), list);
        return;
    case Tool::Check:
    case Tool::Cross: {
        m.kind = m_tool == Tool::Check ? Mark::Check : Mark::Cross;
        const qreal side = size * 1.1;
        m.rect = QRectF(pt.x() - side / 2, pt.y() - side / 2, side, side);
        list.append(m);
        m_selRow = row;
        m_selected = m.uid;
        commitMarks(row, m.kind == Mark::Check ? tr("Add check mark") : tr("Add cross"), list);
        return;
    }
    case Tool::Sign: {
        if (!ensureSignature())
            return;
        const qreal w = 150;
        const Mark sig = signatureMark(QRectF(pt.x() - w / 2, pt.y() - w / 2, w, w));
        list.append(sig);
        m_selRow = row;
        m_selected = sig.uid;
        commitMarks(row, tr("Add signature"), list);
        setTool(Tool::Select);
        return;
    }
    }
}

void DocumentView::applyStyleToSelected()
{
    if (m_textEdit) {
        // Restyle the text being typed.
        m_editingMark.fontSize = m_fontSize->value();
        m_editingMark.color = inkColor();
        if (auto *edit = qobject_cast<QPlainTextEdit *>(m_textEdit.data())) {
            QFont f(Marks::screenFontFamily());
            f.setPixelSize(qMax(1, qRound(m_editingMark.fontSize * m_zoom)));
            edit->setFont(f);
            QPalette pal = edit->palette();
            pal.setColor(QPalette::Text, m_editingMark.color);
            edit->setPalette(pal);
        }
        return;
    }
    if (!m_selected || m_selRow < 0)
        return;
    QVector<Mark> list = page(m_selRow).marks;
    for (Mark &m : list) {
        if (m.uid != m_selected)
            continue;
        const QColor c = inkColor();
        const qreal size = m_fontSize->value();
        bool changed = m.color != c;
        m.color = c;
        if (m.kind == Mark::Text && !qFuzzyCompare(m.fontSize, size)) {
            m.fontSize = size;
            m.rect.setSize(Marks::textSize(m.text, size));
            changed = true;
        }
        if (changed)
            commitMarks(m_selRow, tr("Change style"), list);
        return;
    }
}

void DocumentView::deleteSelected()
{
    if (!m_selected || m_selRow < 0)
        return;
    QVector<Mark> list = page(m_selRow).marks;
    const qsizetype before = list.size();
    list.removeIf([this](const Mark &m) { return m.uid == m_selected; });
    const int row = m_selRow;
    m_selected = 0;
    m_selRow = -1;
    if (list.size() != before)
        commitMarks(row, tr("Delete"), list);
}

// ---------------------------------------------------------------- hit testing

int DocumentView::markAt(int row, const QPointF &pt) const
{
    const QVector<Mark> list = marks(row);
    const qreal slop = 4 / m_zoom; // a few pixels of tolerance
    for (int i = list.size() - 1; i >= 0; --i)
        if (list.at(i).rect.adjusted(-slop, -slop, slop, slop).contains(pt))
            return i;
    return -1;
}

QRectF DocumentView::handleRect() const
{
    if (m_selRow < 0)
        return {};
    for (const Mark &m : marks(m_selRow))
        if (m.uid == m_selected) {
            const QRectF r = toDevice(m_selRow).mapRect(m.rect).adjusted(-3, -3, 3, 3);
            return QRectF(r.right() - kHandle / 2, r.bottom() - kHandle / 2, kHandle, kHandle);
        }
    return {};
}

bool DocumentView::inResizeHandle(const QPointF &canvasPt) const
{
    return m_selected && handleRect().adjusted(-3, -3, 3, 3).contains(canvasPt);
}

const FieldInfo *DocumentView::fieldAt(int row, const QPointF &pt, const FieldWidget **widget) const
{
    const int pageIndex = page(row).pageIndex;
    for (const FieldInfo &f : fields(row)) {
        if (f.readOnly)
            continue;
        for (const FieldWidget &w : f.widgets)
            if (w.page == pageIndex && w.rect.contains(pt)) {
                if (widget)
                    *widget = &w;
                return &f;
            }
    }
    return nullptr;
}

// ---------------------------------------------------------------- mouse

void DocumentView::canvasPress(QMouseEvent *e)
{
    m_canvas->setFocus();
    if (!m_fill && e->button() == Qt::LeftButton) {
        // View mode: drag to select text.
        clearTextSelection();
        const int row = rowAt(e->position());
        if (row >= 0 && !page(row).isBlank()) {
            m_selecting = true;
            m_textRow = row;
            m_textFrom = toDevice(row).inverted().map(e->position());
        }
        return;
    }
    if (e->button() != Qt::LeftButton || !m_fill)
        return;
    if (m_textEdit) {
        // A click outside the text box finishes typing.
        finishTextEdit(true);
        return;
    }
    finishFieldEditor(true);
    m_canvas->setFocus();

    if (inResizeHandle(e->position())) {
        m_drag = Drag::Resize;
    } else {
        const int row = rowAt(e->position());
        if (row < 0) {
            m_selected = 0;
            m_selRow = -1;
            m_canvas->update();
            return;
        }
        const QPointF pt = toDevice(row).inverted().map(e->position());
        if (const int i = markAt(row, pt); i >= 0) {
            m_selRow = row;
            m_selected = marks(row).at(i).uid;
            m_drag = Drag::Move;
        } else if (const FieldWidget *w = nullptr; const FieldInfo *f = fieldAt(row, pt, &w)) {
            m_selected = 0;
            m_selRow = -1;
            activateField(row, *f, *w);
            m_canvas->update();
            return;
        } else {
            m_selected = 0;
            m_selRow = -1;
            placeMark(row, pt);
            m_canvas->update();
            return;
        }
    }
    // Show the selected mark's style in the controls (without restyling it).
    for (const Mark &m : marks(m_selRow))
        if (m.uid == m_selected) {
            const QSignalBlocker b1(m_fontSize), b2(m_color);
            if (m.kind == Mark::Text)
                m_fontSize->setValue(qRound(m.fontSize));
            m_color->setCurrentIndex(m.color == kBlueInk ? 1 : 0);
        }
    m_dragMarks = page(m_selRow).marks;
    m_dragStart = toDevice(m_selRow).inverted().map(e->position());
    m_dragMoved = false;
    m_canvas->update();
}

void DocumentView::canvasMove(QMouseEvent *e)
{
    if (!m_fill) {
        if (m_selecting)
            selectText(m_textRow, m_textFrom, toDevice(m_textRow).inverted().map(e->position()));
        m_canvas->setCursor(rowAt(e->position()) >= 0 ? Qt::IBeamCursor : Qt::ArrowCursor);
        return;
    }
    if (m_drag == Drag::None) {
        // Hover feedback.
        Qt::CursorShape shape = m_tool == Tool::Select ? Qt::ArrowCursor : m_tool == Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor;
        const int row = rowAt(e->position());
        if (inResizeHandle(e->position())) {
            shape = Qt::SizeFDiagCursor;
        } else if (row >= 0) {
            const QPointF pt = toDevice(row).inverted().map(e->position());
            if (markAt(row, pt) >= 0)
                shape = Qt::SizeAllCursor;
            else if (const FieldInfo *f = fieldAt(row, pt))
                shape = (f->type == FieldInfo::Text) ? Qt::IBeamCursor : Qt::PointingHandCursor;
        } else {
            shape = Qt::ArrowCursor;
        }
        m_canvas->setCursor(shape);
        return;
    }
    const QPointF pt = toDevice(m_selRow).inverted().map(e->position());
    const QPointF delta = pt - m_dragStart;
    if (!m_dragMoved && QLineF(QPointF(), delta).length() * m_zoom < 2)
        return;
    m_dragMoved = true;
    QVector<Mark> list = page(m_selRow).marks;
    for (Mark &m : list) {
        if (m.uid != m_selected)
            continue;
        if (m_drag == Drag::Move) {
            m.rect.translate(delta);
        } else {
            const QPointF br = m.rect.bottomRight() + delta;
            switch (m.kind) {
            case Mark::Text: {
                const qreal scale = qMax(0.1, (br.y() - m.rect.top()) / m.rect.height());
                m.fontSize = qBound(4.0, m.fontSize * scale, 144.0);
                m.rect.setSize(Marks::textSize(m.text, m.fontSize));
                break;
            }
            case Mark::Check:
            case Mark::Cross: {
                const qreal side = qMax(4.0, qMax(br.x() - m.rect.left(), br.y() - m.rect.top()));
                m.rect.setSize(QSizeF(side, side));
                break;
            }
            case Mark::Signature: {
                const qreal w = qMax(10.0, br.x() - m.rect.left());
                m.rect.setSize(QSizeF(w, w * m.rect.height() / m.rect.width()));
                break;
            }
            }
        }
    }
    m_dragMarks = list;
    m_canvas->update();
}

void DocumentView::canvasRelease(QMouseEvent *)
{
    m_selecting = false;
    if (m_drag == Drag::None)
        return;
    const Drag drag = m_drag;
    const QVector<Mark> result = m_dragMarks;
    m_drag = Drag::None;
    if (m_dragMoved)
        commitMarks(m_selRow, drag == Drag::Move ? tr("Move") : tr("Resize"), result);
    m_canvas->update();
}

void DocumentView::canvasDoubleClick(QMouseEvent *e)
{
    if (!m_fill)
        return;
    const int row = rowAt(e->position());
    if (row < 0)
        return;
    const QPointF pt = toDevice(row).inverted().map(e->position());
    const int i = markAt(row, pt);
    if (i >= 0 && marks(row).at(i).kind == Mark::Text)
        startTextEdit(row, marks(row).at(i), false);
}

// ---------------------------------------------------------------- text marks

void DocumentView::startTextEdit(int row, const Mark &mark, bool isNew)
{
    finishTextEdit(true);
    m_editRow = row;
    m_editingMark = mark;
    m_editingNew = isNew;
    m_selRow = row;
    m_selected = mark.uid;

    auto *edit = new QPlainTextEdit(m_canvas);
    edit->setFrameShape(QFrame::NoFrame);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->document()->setDocumentMargin(0);
    edit->setStyleSheet(QStringLiteral("QPlainTextEdit { background: rgba(255,255,255,170); border: 1px dashed #2f6fff; }"));
    QFont f(Marks::screenFontFamily());
    f.setPixelSize(qMax(1, qRound(mark.fontSize * m_zoom)));
    edit->setFont(f);
    QPalette pal = edit->palette();
    pal.setColor(QPalette::Text, mark.color);
    edit->setPalette(pal);
    edit->setPlainText(mark.text);
    edit->moveCursor(QTextCursor::End);

    auto fit = [this, edit] {
        const QSizeF s = Marks::textSize(edit->toPlainText() + QStringLiteral("  "), m_editingMark.fontSize);
        const QRectF r = toDevice(m_editRow).mapRect(QRectF(m_editingMark.rect.topLeft(), s));
        edit->setGeometry(r.toAlignedRect().adjusted(-2, -2, 4, 4));
    };
    connect(edit, &QPlainTextEdit::textChanged, this, fit);
    fit();
    edit->installEventFilter(this);
    edit->show();
    edit->setFocus();
    m_textEdit = edit;
    m_canvas->update();
}

void DocumentView::finishTextEdit(bool commit)
{
    if (!m_textEdit)
        return;
    auto *edit = qobject_cast<QPlainTextEdit *>(m_textEdit.data());
    const QString text = edit ? edit->toPlainText() : QString();
    m_textEdit->removeEventFilter(this);
    m_textEdit->hide();
    m_textEdit->deleteLater();
    m_textEdit = nullptr;
    const int row = m_editRow;
    m_editRow = -1;

    QVector<Mark> list = page(row).marks;
    const bool empty = text.trimmed().isEmpty();
    if (!commit || (m_editingNew && empty)) {
        m_canvas->update();
        return;
    }
    if (empty) {
        list.removeIf([this](const Mark &m) { return m.uid == m_editingMark.uid; });
        m_selected = 0;
        m_selRow = -1;
        commitMarks(row, tr("Delete text"), list);
        return;
    }
    Mark m = m_editingMark;
    m.text = text;
    m.rect.setSize(Marks::textSize(text, m.fontSize));
    bool replaced = false;
    for (Mark &existing : list)
        if (existing.uid == m.uid) {
            if (existing.text == m.text && existing.rect == m.rect && existing.color == m.color) {
                m_canvas->update();
                return; // nothing changed
            }
            existing = m;
            replaced = true;
        }
    if (!replaced)
        list.append(m);
    m_selRow = row;
    m_selected = m.uid;
    commitMarks(row, replaced ? tr("Edit text") : tr("Add text"), list);
}

// ---------------------------------------------------------------- form fields

void DocumentView::activateField(int row, const FieldInfo &field, const FieldWidget &widget)
{
    const QString current = changedValues(row).value(field.name, field.value);
    switch (field.type) {
    case FieldInfo::Text:
    case FieldInfo::Choice:
        openFieldEditor(row, field, widget);
        break;
    case FieldInfo::Check: {
        const QString on = widget.onState.isEmpty() ? QStringLiteral("Yes") : widget.onState;
        commitField(row, field.name, (current.isEmpty() || current == QLatin1String("Off")) ? on : QStringLiteral("Off"));
        break;
    }
    case FieldInfo::Radio:
        if (!widget.onState.isEmpty() && current != widget.onState)
            commitField(row, field.name, widget.onState);
        break;
    case FieldInfo::Signature:
        if (ensureSignature()) {
            QVector<Mark> list = page(row).marks;
            const Mark sig = signatureMark(widget.rect.adjusted(2, 2, -2, -2));
            list.append(sig);
            m_selRow = row;
            m_selected = sig.uid;
            commitMarks(row, tr("Sign"), list);
        }
        break;
    case FieldInfo::Other:
        break;
    }
}

void DocumentView::openFieldEditor(int row, const FieldInfo &field, const FieldWidget &widget)
{
    finishFieldEditor(true);
    const QString current = changedValues(row).value(field.name, field.value);
    const QRect r = toDevice(row).mapRect(widget.rect).toAlignedRect();
    const qreal size = field.fontSize > 0 ? field.fontSize : qBound(4.0, widget.rect.height() * 0.65, 12.0);
    QFont f(Marks::screenFontFamily());
    f.setPixelSize(qMax(6, qRound(size * m_zoom)));

    QWidget *editor = nullptr;
    if (field.type == FieldInfo::Choice) {
        auto *combo = new QComboBox(m_canvas);
        combo->setEditable(field.editableChoice);
        combo->addItems(field.options);
        const int i = combo->findText(current);
        if (i >= 0)
            combo->setCurrentIndex(i);
        else if (field.editableChoice)
            combo->setEditText(current);
        connect(combo, &QComboBox::activated, this, [this] {
            QMetaObject::invokeMethod(this, [this] { finishFieldEditor(true); }, Qt::QueuedConnection);
        });
        editor = combo;
        editor->setGeometry(r.adjusted(0, 0, 0, qMax(0, 24 - r.height())));
        combo->setFont(f);
        QMetaObject::invokeMethod(combo, [combo] { combo->showPopup(); }, Qt::QueuedConnection);
    } else if (field.multiline) {
        auto *edit = new QPlainTextEdit(m_canvas);
        edit->setPlainText(current);
        edit->setFont(f);
        edit->document()->setDocumentMargin(1);
        editor = edit;
        editor->setGeometry(r);
    } else {
        auto *edit = new QLineEdit(m_canvas);
        edit->setText(current);
        if (field.maxLength > 0)
            edit->setMaxLength(field.maxLength);
        edit->setFont(f);
        edit->setFrame(false);
        edit->selectAll();
        connect(edit, &QLineEdit::returnPressed, this, [this] {
            QMetaObject::invokeMethod(this, [this] { finishFieldEditor(true); }, Qt::QueuedConnection);
        });
        editor = edit;
        editor->setGeometry(r.adjusted(0, 0, 0, qMax(0, 18 - r.height())));
    }
    editor->setStyleSheet(QStringLiteral("background: #fffbe6; border: 1px solid #2f6fff;"));
    editor->installEventFilter(this);
    editor->show();
    editor->setFocus();
    m_scroll->ensureWidgetVisible(editor, 40, 60);
    m_fieldEdit = editor;
    m_fieldRow = row;
    m_editingField = field.name;
}

void DocumentView::finishFieldEditor(bool commit)
{
    if (!m_fieldEdit)
        return;
    QString value;
    if (auto *line = qobject_cast<QLineEdit *>(m_fieldEdit.data()))
        value = line->text();
    else if (auto *text = qobject_cast<QPlainTextEdit *>(m_fieldEdit.data()))
        value = text->toPlainText();
    else if (auto *combo = qobject_cast<QComboBox *>(m_fieldEdit.data()))
        value = combo->currentText();
    QWidget *w = m_fieldEdit;
    m_fieldEdit = nullptr;
    w->removeEventFilter(this);
    w->hide();
    w->deleteLater();
    const QString name = m_editingField;
    const int row = m_fieldRow;
    m_editingField.clear();
    m_fieldRow = -1;
    m_canvas->setFocus();
    if (!commit)
        return;
    for (const FieldInfo &f : fields(row))
        if (f.name == name) {
            if (changedValues(row).value(f.name, f.value) != value)
                commitField(row, f.name, value);
            break;
        }
}

void DocumentView::moveToNextField(int direction)
{
    // Text and choice fields in reading order, across all pages.
    struct Entry {
        int row;
        const FieldInfo *field;
        const FieldWidget *widget;
    };
    QVector<Entry> list;
    for (int row = 0; row < rowCount(); ++row) {
        const int pageIndex = page(row).pageIndex;
        QVector<Entry> onPage;
        for (const FieldInfo &f : fields(row))
            if (!f.readOnly && (f.type == FieldInfo::Text || f.type == FieldInfo::Choice))
                for (const FieldWidget &w : f.widgets)
                    if (w.page == pageIndex)
                        onPage.append({row, &f, &w});
        std::sort(onPage.begin(), onPage.end(), [](const Entry &a, const Entry &b) {
            const qreal ay = std::round(a.widget->rect.top() / 4), by = std::round(b.widget->rect.top() / 4);
            return ay != by ? ay < by : a.widget->rect.left() < b.widget->rect.left();
        });
        list += onPage;
    }
    if (list.isEmpty())
        return;
    int current = -1;
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).row == m_fieldRow && list.at(i).field->name == m_editingField)
            current = i;
    finishFieldEditor(true);
    // Field info lives in the Library, so the pointers stay valid after the commit.
    const Entry next = list.at((current + direction + list.size()) % list.size());
    if (next.row != m_current)
        showPage(next.row);
    openFieldEditor(next.row, *next.field, *next.widget);
}

// ---------------------------------------------------------------- keys

bool DocumentView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_scroll && event->type() == QEvent::Resize) {
        if (!m_textEdit && !m_fieldEdit)
            applyFit();
        updateEmptyState();
        return false;
    }
    if (event->type() == QEvent::KeyPress && watched == m_findEdit) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (m_findTimer.isActive() || m_findEdit->text() != m_searched) {
                m_findTimer.stop();
                runSearch();
            } else if (ke->modifiers() & Qt::ShiftModifier) {
                findPrevious();
            } else {
                findNext();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            hideFind();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (watched == m_fieldEdit) {
            if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
                moveToNextField(ke->key() == Qt::Key_Tab ? 1 : -1);
                return true;
            }
            if (ke->key() == Qt::Key_Escape) {
                finishFieldEditor(false);
                return true;
            }
        }
        if (watched == m_textEdit) {
            const bool ctrlEnter = (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
                                   (ke->modifiers() & Qt::ControlModifier);
            if (ke->key() == Qt::Key_Escape || ctrlEnter) {
                finishTextEdit(true);
                m_canvas->setFocus();
                return true;
            }
        }
    } else if (event->type() == QEvent::ShortcutOverride && (watched == m_fieldEdit || watched == m_textEdit)) {
        // While typing, keys like Delete belong to the text box, not the window's shortcuts.
        event->accept();
        return false;
    } else if (event->type() == QEvent::FocusOut) {
        // Clicking elsewhere (or switching windows) keeps what was typed.
        auto *fe = static_cast<QFocusEvent *>(event);
        if (fe->reason() == Qt::ActiveWindowFocusReason || fe->reason() == Qt::PopupFocusReason)
            return QWidget::eventFilter(watched, event);
        if (watched == m_textEdit)
            QMetaObject::invokeMethod(this, [this] { finishTextEdit(true); }, Qt::QueuedConnection);
        else if (watched == m_fieldEdit && !qobject_cast<QComboBox *>(m_fieldEdit.data()))
            QMetaObject::invokeMethod(this, [this] { finishFieldEditor(true); }, Qt::QueuedConnection);
    }
    return QWidget::eventFilter(watched, event);
}

bool DocumentView::canvasKey(QKeyEvent *event)
{
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool nudging = m_fill && m_selected && m_selRow >= 0;
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        return true;
    }
    if (event->matches(QKeySequence::FindNext)) {
        findNext();
        return true;
    }
    if (event->matches(QKeySequence::FindPrevious)) {
        findPrevious();
        return true;
    }
    if (!m_fill && event->matches(QKeySequence::SelectAll)) {
        selectAllText();
        return true;
    }
    if (!m_fill && event->key() == Qt::Key_Escape) {
        if (!m_textSel.isEmpty())
            clearTextSelection();
        else
            hideFind();
        return true;
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (!nudging) {
            showPage(m_current + (event->key() == Qt::Key_Left ? -1 : 1));
            return true;
        }
        break;
    case Qt::Key_PageUp:
        showPage(m_current - 1);
        return true;
    case Qt::Key_PageDown:
        showPage(m_current + 1);
        return true;
    case Qt::Key_Home:
        showPage(0);
        return true;
    case Qt::Key_End:
        showPage(rowCount() - 1);
        return true;
    default:
        break;
    }
    if (!m_fill)
        return false; // View mode: arrows up/down scroll
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelected();
        return true;
    case Qt::Key_Escape:
        if (m_tool != Tool::Select) {
            setTool(Tool::Select);
        } else if (m_selected) {
            m_selected = 0;
            m_selRow = -1;
            m_canvas->update();
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Enter on a selected text mark starts editing it.
        for (const Mark &m : page(m_selRow).marks)
            if (m.uid == m_selected && m.kind == Mark::Text) {
                startTextEdit(m_selRow, m, false);
                return true;
            }
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (!m_selected || m_selRow < 0)
            return false; // scroll
        // Nudge the selected mark (Shift = bigger steps).
        const qreal d = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        QPointF delta;
        switch (event->key()) {
        case Qt::Key_Left: delta = {-d, 0}; break;
        case Qt::Key_Right: delta = {d, 0}; break;
        case Qt::Key_Up: delta = {0, -d}; break;
        default: delta = {0, d}; break;
        }
        QVector<Mark> list = page(m_selRow).marks;
        for (Mark &m : list)
            if (m.uid == m_selected)
                m.rect.translate(delta);
        commitMarks(m_selRow, tr("Move"), list);
        return true;
    }
    default:
        break;
    }
    if (plain) {
        // One-key tool shortcuts.
        const struct {
            int key;
            Tool tool;
        } keys[] = {{Qt::Key_T, Tool::Text}, {Qt::Key_K, Tool::Check}, {Qt::Key_X, Tool::Cross},
                    {Qt::Key_D, Tool::Date}, {Qt::Key_S, Tool::Sign}};
        for (const auto &k : keys)
            if (event->key() == k.key) {
                setTool(k.tool);
                return true;
            }
    }
    return false;
}

// ---------------------------------------------------------------- search

QString DocumentView::pageText(int row)
{
    const PageRef p = page(row);
    QPdfDocument *doc = p.isBlank() ? nullptr : m_library->document(p.sourceId);
    if (!doc)
        return {};
    const quint64 key = (quint64(quint32(p.sourceId)) << 32) | quint32(p.pageIndex);
    auto it = m_textCache.find(key);
    if (it == m_textCache.end())
        it = m_textCache.insert(key, doc->getAllText(p.pageIndex).text());
    return *it;
}

void DocumentView::showFind()
{
    finishEditing();
    m_findBar->show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void DocumentView::hideFind()
{
    m_findTimer.stop();
    m_findBar->hide();
    m_matches.clear();
    m_matchIndex = -1;
    m_searched.clear();
    m_canvas->setFocus();
    m_canvas->update();
}

void DocumentView::runSearch(bool jump)
{
    const QString query = m_findEdit->text().simplified();
    const int keep = m_matchIndex;
    m_searched = m_findEdit->text();
    m_matches.clear();
    m_matchIndex = -1;
    if (!query.isEmpty()) {
        for (int row = 0; row < rowCount(); ++row) {
            const QString text = pageText(row);
            if (text.isEmpty())
                continue;
            QPdfDocument *doc = m_library->document(page(row).sourceId);
            for (qsizetype at = text.indexOf(query, 0, Qt::CaseInsensitive); at >= 0;
                 at = text.indexOf(query, at + query.size(), Qt::CaseInsensitive)) {
                const QPdfSelection sel = doc->getSelectionAtIndex(page(row).pageIndex, int(at), int(query.size()));
                m_matches.append({row, sel.bounds()});
            }
        }
    }
    if (!jump) {
        m_matchIndex = m_matches.isEmpty() ? -1 : qBound(0, keep, int(m_matches.size()) - 1);
    } else if (!m_matches.isEmpty()) {
        // Start from the page being read.
        int first = 0;
        for (int i = 0; i < m_matches.size(); ++i)
            if (m_matches.at(i).row >= m_current) {
                first = i;
                break;
            }
        goToMatch(first);
    }
    updateFindCount();
    m_canvas->update();
}

void DocumentView::goToMatch(int index)
{
    if (m_matches.isEmpty())
        return;
    m_matchIndex = (index % int(m_matches.size()) + int(m_matches.size())) % int(m_matches.size());
    const Match &m = m_matches.at(m_matchIndex);
    if (m.row != m_current)
        showPage(m.row);
    // Scroll the match into view.
    QRectF box;
    for (const QPolygonF &poly : m.bounds)
        box |= toDevice(m.row).map(poly).boundingRect();
    if (!box.isEmpty())
        m_scroll->ensureVisible(int(box.center().x()), int(box.center().y()), int(box.width() / 2) + 60, 120);
    updateFindCount();
    m_canvas->update();
}

void DocumentView::findNext()
{
    if (!m_findBar->isVisible() || m_findEdit->text().isEmpty()) {
        showFind();
        return;
    }
    if (m_findEdit->text() != m_searched)
        runSearch();
    else
        goToMatch(m_matchIndex + 1);
}

void DocumentView::findPrevious()
{
    if (!m_findBar->isVisible() || m_findEdit->text().isEmpty()) {
        showFind();
        return;
    }
    if (m_findEdit->text() != m_searched)
        runSearch();
    else
        goToMatch(m_matchIndex - 1);
}

void DocumentView::updateFindCount()
{
    if (m_searched.trimmed().isEmpty())
        m_findCount->setText(QString());
    else if (m_matches.isEmpty())
        m_findCount->setText(tr("No matches"));
    else
        m_findCount->setText(tr("%1 of %2").arg(m_matchIndex + 1).arg(m_matches.size()));
}

// ---------------------------------------------------------------- text selection

void DocumentView::clearTextSelection()
{
    m_selecting = false;
    m_textRow = -1;
    m_textSel.clear();
    m_textBounds.clear();
    m_canvas->update();
}

void DocumentView::selectText(int row, const QPointF &from, const QPointF &to)
{
    const PageRef p = page(row);
    QPdfDocument *doc = p.isBlank() ? nullptr : m_library->document(p.sourceId);
    if (!doc)
        return;
    // Qt PDF only selects if both ends are on a character, so a drag that
    // starts in the margin or ends past the end of a line is moved onto the
    // nearest line of text first.
    const bool forward = from.y() < to.y() - 2 || (qAbs(from.y() - to.y()) <= 2 && from.x() <= to.x());
    const QPointF a = snapToText(row, forward ? from : to, true);
    const QPointF b = snapToText(row, forward ? to : from, false);
    const QPdfSelection sel = doc->getSelection(p.pageIndex, a, b);
    m_textRow = row;
    m_textSel = sel.text();
    m_textBounds = sel.bounds();
    m_canvas->update();
}

QPointF DocumentView::snapToText(int row, const QPointF &pt, bool start)
{
    const PageRef p = page(row);
    QPdfDocument *doc = m_library->document(p.sourceId);
    const quint64 key = (quint64(quint32(p.sourceId)) << 32) | quint32(p.pageIndex);
    auto it = m_lineCache.find(key);
    if (it == m_lineCache.end()) {
        QVector<QRectF> lines;
        for (const QPolygonF &poly : doc->getAllText(p.pageIndex).bounds())
            lines.append(poly.boundingRect());
        it = m_lineCache.insert(key, lines);
    }
    // The line the point is on, or the closest one above/below it.
    const QRectF *best = nullptr;
    qreal bestDist = 1e9;
    for (const QRectF &r : *it) {
        const qreal dy = pt.y() < r.top() ? r.top() - pt.y() : pt.y() > r.bottom() ? pt.y() - r.bottom() : 0;
        const qreal dx = pt.x() < r.left() ? r.left() - pt.x() : pt.x() > r.right() ? pt.x() - r.right() : 0;
        const qreal d = dy * 4 + dx; // lines matter more than columns
        if (d < bestDist) {
            bestDist = d;
            best = &r;
        }
    }
    if (!best)
        return pt;
    // Inside the line box, by half a character from the edge it was beyond.
    const qreal inset = qMin(2.0, best->width() / 4);
    qreal x = qBound(best->left() + inset, pt.x(), best->right() - inset);
    if (pt.y() < best->top())
        x = start ? best->left() + inset : best->right() - inset; // a line below the point: from its start
    else if (pt.y() > best->bottom())
        x = start ? best->left() + inset : best->right() - inset;
    return QPointF(x, best->center().y());
}

void DocumentView::selectAllText()
{
    const PageRef p = page(m_current);
    QPdfDocument *doc = p.isBlank() ? nullptr : m_library->document(p.sourceId);
    if (!doc)
        return;
    const QPdfSelection sel = doc->getAllText(p.pageIndex);
    m_textRow = m_current;
    m_textSel = sel.text();
    m_textBounds = sel.bounds();
    m_canvas->update();
}

void DocumentView::copySelection()
{
    if (!m_textSel.isEmpty())
        QGuiApplication::clipboard()->setText(m_textSel);
}
