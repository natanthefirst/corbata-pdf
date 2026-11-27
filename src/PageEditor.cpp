#include "PageEditor.h"
#include "Library.h"
#include "PageModel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolBar>
#include <QUndoStack>
#include <QVBoxLayout>

namespace {
constexpr int kMargin = 18;      // grey space around the page, in pixels
constexpr qreal kHandle = 10;    // resize handle, in pixels
constexpr qreal kMinZoom = 0.25;
constexpr qreal kMaxZoom = 6.0;
const QColor kBlueInk(20, 60, 170);
const QColor kSurround(0xd6, 0xd6, 0xd6);
} // namespace

// ---------------------------------------------------------------- canvas

class EditorCanvas : public QWidget
{
public:
    explicit EditorCanvas(PageEditor *editor) : QWidget(), m_editor(editor)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        m_editor->paintCanvas(p);
    }
    void mousePressEvent(QMouseEvent *e) override { m_editor->canvasPress(e); }
    void mouseMoveEvent(QMouseEvent *e) override { m_editor->canvasMove(e); }
    void mouseReleaseEvent(QMouseEvent *e) override { m_editor->canvasRelease(e); }
    void mouseDoubleClickEvent(QMouseEvent *e) override { m_editor->canvasDoubleClick(e); }
    void wheelEvent(QWheelEvent *e) override
    {
        if (e->modifiers() & Qt::ControlModifier) {
            const qreal factor = e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
            m_editor->m_fitWidth = false;
            m_editor->setZoom(m_editor->m_zoom * factor);
            e->accept();
            return;
        }
        QWidget::wheelEvent(e);
    }

private:
    PageEditor *m_editor;
};

// ---------------------------------------------------------------- setup

PageEditor::PageEditor(Library *library, PageModel *model, QUndoStack *undo, int row, Commit commit, QWidget *parent)
    : QDialog(parent), m_library(library), m_model(model), m_undo(undo), m_commit(std::move(commit)), m_row(row)
{
    setWindowTitle(tr("Fill & Sign"));
    setSizeGripEnabled(true);

    // --- tools
    auto *tb = new QToolBar;
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_tools = new QActionGroup(this);
    const struct {
        Tool tool;
        QString text;
        QString tip;
        QKeySequence key;
    } tools[] = {
        {Tool::Select, tr("Select"), tr("Select, move and resize what you added (Esc)"), QKeySequence(Qt::Key_Escape)},
        {Tool::Text, tr("Text"), tr("Click on the page to type (T)"), QKeySequence(Qt::Key_T)},
        {Tool::Check, tr("✓ Check"), tr("Click to add a check mark (K)"), QKeySequence(Qt::Key_K)},
        {Tool::Cross, tr("✗ Cross"), tr("Click to add a cross (X)"), QKeySequence(Qt::Key_X)},
        {Tool::Date, tr("Date"), tr("Click to add today's date (D)"), QKeySequence(Qt::Key_D)},
        {Tool::Sign, tr("Sign…"), tr("Choose your signature, then click where it goes (S)"), QKeySequence(Qt::Key_S)},
    };
    for (const auto &t : tools) {
        QAction *a = tb->addAction(t.text);
        a->setCheckable(true);
        a->setToolTip(t.tip);
        m_tools->addAction(a);
        m_toolActions[int(t.tool)] = a;
        const Tool tool = t.tool;
        connect(a, &QAction::triggered, this, [this, tool] { setTool(tool); });
        // Single-key shortcuts, only when no text box has focus (see keyPressEvent).
        a->setData(QVariant::fromValue(int(t.key[0].key())));
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

    tb->addSeparator();
    QAction *undoAct = m_undo->createUndoAction(this, tr("Undo"));
    undoAct->setShortcuts(QKeySequence::Undo);
    QAction *redoAct = m_undo->createRedoAction(this, tr("Redo"));
    redoAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    tb->addAction(undoAct);
    tb->addAction(redoAct);
    addAction(undoAct);
    addAction(redoAct);

    tb->addSeparator();
    QAction *zoomOut = tb->addAction(tr("−"), this, [this] { m_fitWidth = false; setZoom(m_zoom / 1.25); });
    zoomOut->setToolTip(tr("Zoom out (Ctrl+−)"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    QAction *zoomIn = tb->addAction(tr("+"), this, [this] { m_fitWidth = false; setZoom(m_zoom * 1.25); });
    zoomIn->setToolTip(tr("Zoom in (Ctrl++)"));
    zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    tb->addAction(tr("Fit Width"), this, [this] { fitWidth(); });
    tb->addAction(tr("Fit Page"), this, [this] { fitPage(); });

    // --- page
    m_canvas = new EditorCanvas(this);
    m_scroll = new QScrollArea;
    m_scroll->setWidget(m_canvas);
    m_scroll->setWidgetResizable(false);
    m_scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    QPalette surround = m_scroll->viewport()->palette();
    surround.setColor(QPalette::Window, kSurround);
    m_scroll->viewport()->setPalette(surround);
    m_scroll->viewport()->setAutoFillBackground(true);

    // --- bottom bar
    m_caption = new QLabel;
    m_prev = new QPushButton(tr("← Previous"));
    m_next = new QPushButton(tr("Next →"));
    auto *close = new QPushButton(tr("Done"));
    for (QPushButton *b : {m_prev, m_next, close}) {
        b->setAutoDefault(false);
        b->setFocusPolicy(Qt::NoFocus);
    }
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
    layout->setContentsMargins(6, 6, 6, 8);
    layout->addWidget(tb);
    layout->addWidget(m_scroll, 1);
    layout->addLayout(bar);

    connect(m_model, &QAbstractItemModel::modelReset, this, [this] { reloadFromModel(); });

    const QSize avail = screen() ? screen()->availableSize() : QSize(1280, 900);
    resize(int(avail.width() * 0.6), int(avail.height() * 0.9));

    goToRow(row);
}

// ---------------------------------------------------------------- state

PageRef PageEditor::page() const
{
    const PageList &pages = m_model->pages();
    return (m_row >= 0 && m_row < pages.size()) ? pages.at(m_row) : PageRef();
}

QVector<Mark> PageEditor::marks() const { return m_drag != Drag::None ? m_dragMarks : page().marks; }

const QVector<FieldInfo> &PageEditor::fields() const
{
    static const QVector<FieldInfo> none;
    const PageRef p = page();
    return p.isBlank() ? none : m_library->fields(p.sourceId);
}

QHash<QString, QString> PageEditor::changedValues() const
{
    const PageRef p = page();
    return p.isBlank() ? QHash<QString, QString>() : m_model->fieldValues().value(p.sourceId);
}

void PageEditor::commitMarks(const QString &text, const QVector<Mark> &marks)
{
    PageList pages = m_model->pages();
    if (m_row < 0 || m_row >= pages.size())
        return;
    pages[m_row].marks = marks;
    m_commit(text, pages, m_model->fieldValues());
}

void PageEditor::commitField(const QString &name, const QString &value)
{
    const PageRef p = page();
    if (p.isBlank())
        return;
    FieldValues values = m_model->fieldValues();
    values[p.sourceId][name] = value;
    m_commit(tr("Fill in \"%1\"").arg(name), m_model->pages(), values);
}

void PageEditor::reloadFromModel()
{
    // Follow our page if it moved (e.g. undo of an insert); otherwise stay put.
    const PageList &pages = m_model->pages();
    for (int i = 0; i < pages.size(); ++i)
        if (pages.at(i).uid == m_pageUid) {
            m_row = i;
            break;
        }
    if (pages.isEmpty()) {
        reject();
        return;
    }
    m_row = qBound(0, m_row, int(pages.size()) - 1);
    m_pageUid = pages.at(m_row).uid;
    m_drag = Drag::None;
    relayout();
    updateCaption();
}

// ---------------------------------------------------------------- view

void PageEditor::goToRow(int row)
{
    finishTextEdit(true);
    finishFieldEditor(true);
    const int n = m_model->rowCount();
    if (n == 0)
        return;
    m_row = qBound(0, row, n - 1);
    m_pageUid = m_model->pages().at(m_row).uid;
    m_selected = 0;
    if (m_fitWidth)
        fitWidth();
    else
        relayout();
    updateCaption();
    m_scroll->verticalScrollBar()->setValue(0);
}

void PageEditor::step(int delta) { goToRow(m_row + delta); }

void PageEditor::updateCaption()
{
    const int n = m_model->rowCount();
    m_caption->setText(tr("Page %1 of %2").arg(m_row + 1).arg(n));
    m_prev->setEnabled(m_row > 0);
    m_next->setEnabled(m_row < n - 1);
}

void PageEditor::setZoom(qreal z)
{
    finishTextEdit(true);
    finishFieldEditor(true);
    m_zoom = qBound(kMinZoom, z, kMaxZoom);
    relayout();
}

void PageEditor::fitWidth()
{
    m_fitWidth = true;
    const QSizeF disp = page().displaySize().isEmpty() ? QSizeF(612, 792) : page().displaySize();
    const int avail = m_scroll->viewport()->width() - 2 * kMargin - m_scroll->verticalScrollBar()->sizeHint().width();
    setZoom(qMax(50, avail) / disp.width());
}

void PageEditor::fitPage()
{
    m_fitWidth = false;
    const QSizeF disp = page().displaySize().isEmpty() ? QSizeF(612, 792) : page().displaySize();
    const QSize avail = m_scroll->viewport()->size() - QSize(2 * kMargin, 2 * kMargin);
    setZoom(qMin(avail.width() / disp.width(), avail.height() / disp.height()));
}

void PageEditor::relayout()
{
    const QSizeF disp = page().displaySize().isEmpty() ? QSizeF(612, 792) : page().displaySize();
    m_canvas->resize(int(disp.width() * m_zoom) + 2 * kMargin, int(disp.height() * m_zoom) + 2 * kMargin);
    m_canvas->update();
}

QTransform PageEditor::toDevice() const
{
    const PageRef p = page();
    const QSizeF disp = p.displaySize().isEmpty() ? QSizeF(612, 792) : p.displaySize();
    return pageToTarget(p, QRectF(kMargin, kMargin, disp.width() * m_zoom, disp.height() * m_zoom));
}

QImage PageEditor::pageImage()
{
    const PageRef p = page();
    if (p.isBlank())
        return {};
    const qreal dpr = devicePixelRatioF();
    const QSizeF disp = p.displaySize();
    const int px = int(qMax(disp.width(), disp.height()) * m_zoom * dpr);
    const QString key = QStringLiteral("%1/%2/%3/%4").arg(p.sourceId).arg(p.pageIndex).arg(p.rotation).arg(px);
    if (key != m_cachedKey) {
        QImage img = m_library->render(p.sourceId, p.pageIndex, px);
        if (p.rotation)
            img = img.transformed(QTransform().rotate(p.rotation), Qt::SmoothTransformation);
        m_cachedImage = img;
        m_cachedKey = key;
    }
    return m_cachedImage;
}

void PageEditor::paintCanvas(QPainter &p)
{
    const PageRef ref = page();
    const QSizeF disp = ref.displaySize().isEmpty() ? QSizeF(612, 792) : ref.displaySize();
    const QRectF pageRect(kMargin, kMargin, disp.width() * m_zoom, disp.height() * m_zoom);
    p.fillRect(m_canvas->rect(), kSurround);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(pageRect.translated(2, 3), QColor(0, 0, 0, 60));
    p.fillRect(pageRect, Qt::white);
    const QImage img = pageImage();
    if (!img.isNull())
        p.drawImage(pageRect, img);

    const QTransform t = toDevice();
    if (!ref.isBlank())
        FormFields::paint(p, fields(), changedValues(), ref.pageIndex, t, true);

    QVector<Mark> shown = marks();
    if (m_textEdit)
        shown.removeIf([this](const Mark &m) { return m.uid == m_editingMark.uid; });
    Marks::paint(p, shown, t);

    // Selection outline and resize handle.
    for (const Mark &m : shown) {
        if (m.uid != m_selected)
            continue;
        const QRectF r = t.mapRect(m.rect).adjusted(-3, -3, 3, 3);
        p.setPen(QPen(QColor(40, 110, 255), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(QColor(40, 110, 255));
        p.drawRect(handleRect());
    }
}

void PageEditor::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_fitWidth)
        fitWidth();
    m_canvas->setFocus();
}

void PageEditor::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    if (m_fitWidth && !m_textEdit && !m_fieldEdit)
        fitWidth();
}

// ---------------------------------------------------------------- tools

QColor PageEditor::inkColor() const { return m_color->currentIndex() == 1 ? kBlueInk : QColor(Qt::black); }

void PageEditor::setTool(Tool tool)
{
    finishTextEdit(true);
    finishFieldEditor(true);
    if (tool == Tool::Sign) {
        const Signature s = SignatureDialog::getSignature(this);
        if (s.isNull()) {
            tool = Tool::Select;
        } else {
            m_signature = s;
        }
    }
    m_tool = tool;
    m_toolActions[int(tool)]->setChecked(true);
    m_canvas->setCursor(tool == Tool::Select ? Qt::ArrowCursor : tool == Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
    m_canvas->setFocus();
}

bool PageEditor::ensureSignature()
{
    if (!m_signature.isNull())
        return true;
    m_signature = SignatureDialog::getSignature(this);
    return !m_signature.isNull();
}

Mark PageEditor::signatureMark(const QRectF &fit) const
{
    Mark m;
    m.kind = Mark::Signature;
    m.path = m_signature.path;
    m.image = m_signature.image;
    m.color = inkColor();
    m.uid = const_cast<PageEditor *>(this)->m_nextMarkUid++;
    QSizeF size = m_signature.naturalSize().scaled(fit.size(), Qt::KeepAspectRatio);
    m.rect = QRectF(QPointF(0, 0), size);
    m.rect.moveCenter(fit.center());
    return m;
}

void PageEditor::placeMark(const QPointF &pt)
{
    const qreal size = m_fontSize->value();
    QVector<Mark> list = page().marks;
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
        startTextEdit(m, true);
        return;
    case Tool::Date:
        m.kind = Mark::Text;
        m.text = QLocale::system().toString(QDate::currentDate(), QLocale::ShortFormat);
        m.rect = QRectF(QPointF(pt.x(), pt.y() - 0.6 * size), Marks::textSize(m.text, size));
        list.append(m);
        m_selected = m.uid;
        commitMarks(tr("Add date"), list);
        return;
    case Tool::Check:
    case Tool::Cross: {
        m.kind = m_tool == Tool::Check ? Mark::Check : Mark::Cross;
        const qreal side = size * 1.1;
        m.rect = QRectF(pt.x() - side / 2, pt.y() - side / 2, side, side);
        list.append(m);
        m_selected = m.uid;
        commitMarks(m.kind == Mark::Check ? tr("Add check mark") : tr("Add cross"), list);
        return;
    }
    case Tool::Sign: {
        if (!ensureSignature())
            return;
        const qreal w = 150;
        const Mark sig = signatureMark(QRectF(pt.x() - w / 2, pt.y() - w / 2, w, w));
        list.append(sig);
        m_selected = sig.uid;
        commitMarks(tr("Add signature"), list);
        setTool(Tool::Select);
        return;
    }
    }
}

void PageEditor::applyStyleToSelected()
{
    if (!m_selected)
        return;
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
    QVector<Mark> list = page().marks;
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
            commitMarks(tr("Change style"), list);
        return;
    }
}

void PageEditor::deleteSelected()
{
    if (!m_selected)
        return;
    QVector<Mark> list = page().marks;
    const qsizetype before = list.size();
    list.removeIf([this](const Mark &m) { return m.uid == m_selected; });
    m_selected = 0;
    if (list.size() != before)
        commitMarks(tr("Delete"), list);
}

// ---------------------------------------------------------------- hit testing

int PageEditor::markAt(const QPointF &pt) const
{
    const QVector<Mark> list = marks();
    const qreal slop = 4 / m_zoom; // a few pixels of tolerance
    for (int i = list.size() - 1; i >= 0; --i)
        if (list.at(i).rect.adjusted(-slop, -slop, slop, slop).contains(pt))
            return i;
    return -1;
}

QRectF PageEditor::handleRect() const
{
    for (const Mark &m : marks())
        if (m.uid == m_selected) {
            const QRectF r = toDevice().mapRect(m.rect).adjusted(-3, -3, 3, 3);
            return QRectF(r.right() - kHandle / 2, r.bottom() - kHandle / 2, kHandle, kHandle);
        }
    return {};
}

bool PageEditor::inResizeHandle(const QPointF &devicePt) const
{
    return m_selected && handleRect().adjusted(-3, -3, 3, 3).contains(devicePt);
}

const FieldInfo *PageEditor::fieldAt(const QPointF &pt, const FieldWidget **widget) const
{
    const int pageIndex = page().pageIndex;
    for (const FieldInfo &f : fields()) {
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

void PageEditor::canvasPress(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_textEdit) {
        // A click outside the text box finishes typing.
        finishTextEdit(true);
        return;
    }
    finishFieldEditor(true);
    m_canvas->setFocus();

    const QPointF pt = toDevice().inverted().map(e->position());
    if (inResizeHandle(e->position())) {
        m_drag = Drag::Resize;
    } else if (const int i = markAt(pt); i >= 0) {
        m_selected = marks().at(i).uid;
        m_drag = Drag::Move;
    } else if (const FieldWidget *w = nullptr; const FieldInfo *f = fieldAt(pt, &w)) {
        m_selected = 0;
        activateField(*f, *w);
        m_canvas->update();
        return;
    } else {
        m_selected = 0;
        placeMark(pt);
        m_canvas->update();
        return;
    }
    // Sync the style controls with the selected mark (without restyling it).
    for (const Mark &m : marks())
        if (m.uid == m_selected) {
            const QSignalBlocker b1(m_fontSize), b2(m_color);
            if (m.kind == Mark::Text)
                m_fontSize->setValue(qRound(m.fontSize));
            m_color->setCurrentIndex(m.color == kBlueInk ? 1 : 0);
        }
    m_dragMarks = page().marks;
    m_dragStart = pt;
    m_dragMoved = false;
    m_canvas->update();
}

void PageEditor::canvasMove(QMouseEvent *e)
{
    const QPointF pt = toDevice().inverted().map(e->position());
    if (m_drag == Drag::None) {
        // Hover feedback.
        Qt::CursorShape shape = m_tool == Tool::Select ? Qt::ArrowCursor : m_tool == Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor;
        if (inResizeHandle(e->position()))
            shape = Qt::SizeFDiagCursor;
        else if (markAt(pt) >= 0)
            shape = Qt::SizeAllCursor;
        else if (const FieldInfo *f = fieldAt(pt))
            shape = (f->type == FieldInfo::Text) ? Qt::IBeamCursor : Qt::PointingHandCursor;
        m_canvas->setCursor(shape);
        return;
    }
    const QPointF delta = pt - m_dragStart;
    if (!m_dragMoved && QLineF(QPointF(), delta).length() * m_zoom < 2)
        return;
    m_dragMoved = true;
    QVector<Mark> list = page().marks;
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

void PageEditor::canvasRelease(QMouseEvent *)
{
    if (m_drag == Drag::None)
        return;
    const Drag drag = m_drag;
    const QVector<Mark> result = m_dragMarks;
    m_drag = Drag::None;
    if (m_dragMoved)
        commitMarks(drag == Drag::Move ? tr("Move") : tr("Resize"), result);
    m_canvas->update();
}

void PageEditor::canvasDoubleClick(QMouseEvent *e)
{
    const QPointF pt = toDevice().inverted().map(e->position());
    const int i = markAt(pt);
    if (i >= 0 && marks().at(i).kind == Mark::Text)
        startTextEdit(marks().at(i), false);
}

// ---------------------------------------------------------------- text marks

void PageEditor::startTextEdit(const Mark &mark, bool isNew)
{
    finishTextEdit(true);
    m_editingMark = mark;
    m_editingNew = isNew;
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
        const QRectF r = toDevice().mapRect(QRectF(m_editingMark.rect.topLeft(), s));
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

void PageEditor::finishTextEdit(bool commit)
{
    if (!m_textEdit)
        return;
    auto *edit = qobject_cast<QPlainTextEdit *>(m_textEdit.data());
    const QString text = edit ? edit->toPlainText() : QString();
    m_textEdit->removeEventFilter(this);
    m_textEdit->hide();
    m_textEdit->deleteLater();
    m_textEdit = nullptr;

    QVector<Mark> list = page().marks;
    const bool empty = text.trimmed().isEmpty();
    if (!commit || (m_editingNew && empty)) {
        m_canvas->update();
        return;
    }
    if (empty) {
        list.removeIf([this](const Mark &m) { return m.uid == m_editingMark.uid; });
        m_selected = 0;
        commitMarks(tr("Delete text"), list);
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
    m_selected = m.uid;
    commitMarks(replaced ? tr("Edit text") : tr("Add text"), list);
}

// ---------------------------------------------------------------- form fields

void PageEditor::activateField(const FieldInfo &field, const FieldWidget &widget)
{
    const QString current = changedValues().value(field.name, field.value);
    switch (field.type) {
    case FieldInfo::Text:
    case FieldInfo::Choice:
        openFieldEditor(field, widget);
        break;
    case FieldInfo::Check: {
        const QString on = widget.onState.isEmpty() ? QStringLiteral("Yes") : widget.onState;
        commitField(field.name, (current.isEmpty() || current == QLatin1String("Off")) ? on : QStringLiteral("Off"));
        break;
    }
    case FieldInfo::Radio:
        if (!widget.onState.isEmpty() && current != widget.onState)
            commitField(field.name, widget.onState);
        break;
    case FieldInfo::Signature:
        if (ensureSignature()) {
            QVector<Mark> list = page().marks;
            const Mark sig = signatureMark(widget.rect.adjusted(2, 2, -2, -2));
            list.append(sig);
            m_selected = sig.uid;
            commitMarks(tr("Sign"), list);
        }
        break;
    case FieldInfo::Other:
        break;
    }
}

void PageEditor::openFieldEditor(const FieldInfo &field, const FieldWidget &widget)
{
    finishFieldEditor(true);
    const QString current = changedValues().value(field.name, field.value);
    const QRect r = toDevice().mapRect(widget.rect).toAlignedRect();
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
    m_fieldEdit = editor;
    m_editingField = field.name;
}

void PageEditor::finishFieldEditor(bool commit)
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
    m_editingField.clear();
    m_canvas->setFocus();
    if (!commit)
        return;
    for (const FieldInfo &f : fields())
        if (f.name == name) {
            if (changedValues().value(f.name, f.value) != value)
                commitField(f.name, value);
            break;
        }
}

void PageEditor::moveToNextField(int direction)
{
    // Text and choice fields on this page, in reading order.
    struct Entry {
        const FieldInfo *field;
        const FieldWidget *widget;
    };
    QVector<Entry> list;
    const int pageIndex = page().pageIndex;
    for (const FieldInfo &f : fields())
        if (!f.readOnly && (f.type == FieldInfo::Text || f.type == FieldInfo::Choice))
            for (const FieldWidget &w : f.widgets)
                if (w.page == pageIndex)
                    list.append({&f, &w});
    if (list.isEmpty())
        return;
    std::sort(list.begin(), list.end(), [](const Entry &a, const Entry &b) {
        const qreal ay = std::round(a.widget->rect.top() / 4), by = std::round(b.widget->rect.top() / 4);
        return ay != by ? ay < by : a.widget->rect.left() < b.widget->rect.left();
    });
    int current = -1;
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).field->name == m_editingField)
            current = i;
    finishFieldEditor(true);
    // The commit reset the model; look the fields up again by position.
    const int next = (current + direction + list.size()) % list.size();
    const QString name = list.at(next).field->name;
    const QRectF rect = list.at(next).widget->rect;
    for (const FieldInfo &f : fields())
        if (f.name == name)
            for (const FieldWidget &w : f.widgets)
                if (w.page == pageIndex && w.rect == rect) {
                    openFieldEditor(f, w);
                    return;
                }
}

// ---------------------------------------------------------------- keys

bool PageEditor::eventFilter(QObject *watched, QEvent *event)
{
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
    } else if (event->type() == QEvent::FocusOut) {
        // Clicking elsewhere (or switching windows) keeps what was typed.
        auto *fe = static_cast<QFocusEvent *>(event);
        if (fe->reason() == Qt::ActiveWindowFocusReason || fe->reason() == Qt::PopupFocusReason)
            return QDialog::eventFilter(watched, event);
        if (watched == m_textEdit)
            QMetaObject::invokeMethod(this, [this] { finishTextEdit(true); }, Qt::QueuedConnection);
        else if (watched == m_fieldEdit && !qobject_cast<QComboBox *>(m_fieldEdit.data()))
            QMetaObject::invokeMethod(this, [this] { finishFieldEditor(true); }, Qt::QueuedConnection);
    }
    return QDialog::eventFilter(watched, event);
}

void PageEditor::keyPressEvent(QKeyEvent *event)
{
    if (m_textEdit || m_fieldEdit) {
        QDialog::keyPressEvent(event);
        return;
    }
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelected();
        return;
    case Qt::Key_Escape:
        if (m_tool != Tool::Select)
            setTool(Tool::Select);
        else if (m_selected) {
            m_selected = 0;
            m_canvas->update();
        } else {
            QDialog::keyPressEvent(event);
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Enter on a selected text mark starts editing it.
        for (const Mark &m : page().marks)
            if (m.uid == m_selected && m.kind == Mark::Text) {
                startTextEdit(m, false);
                return;
            }
        return;
    case Qt::Key_PageUp:
        step(-1);
        return;
    case Qt::Key_PageDown:
        step(1);
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (!m_selected) {
            step(event->key() == Qt::Key_Left || event->key() == Qt::Key_Up ? -1 : 1);
            return;
        }
        // Nudge the selected mark (Shift = bigger steps).
        const qreal d = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        QPointF delta;
        switch (event->key()) {
        case Qt::Key_Left: delta = {-d, 0}; break;
        case Qt::Key_Right: delta = {d, 0}; break;
        case Qt::Key_Up: delta = {0, -d}; break;
        default: delta = {0, d}; break;
        }
        QVector<Mark> list = page().marks;
        for (Mark &m : list)
            if (m.uid == m_selected)
                m.rect.translate(delta);
        commitMarks(tr("Move"), list);
        return;
    }
    default:
        break;
    }
    if (plain) {
        for (QAction *a : m_tools->actions())
            if (a->data().toInt() == event->key() && event->key() != Qt::Key_Escape) {
                a->trigger();
                return;
            }
    }
    QDialog::keyPressEvent(event);
}
