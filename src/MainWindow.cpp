#include "MainWindow.h"
#include "DefaultApp.h"
#include "Library.h"
#include "PageGrid.h"
#include "PageModel.h"
#include "PdfWriter.h"
#include "DocumentView.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QUrl>

#include <functional>

namespace {

constexpr int kMinZoom = 90;
constexpr int kMaxZoom = 420;
constexpr int kMaxRecent = 8;
const char *kPdfFilter = QT_TRANSLATE_NOOP("MainWindow", "PDF files (*.pdf *.PDF)");

// The whole editable state: the page list (with Fill & Sign marks) and the
// form values the user changed.
struct DocState {
    PageList pages;
    FieldValues fields;
};

// Undo is a full snapshot of the (small) document state, so every operation is
// trivially undoable without per-operation inverse logic.
class SnapshotCommand : public QUndoCommand
{
public:
    using Apply = std::function<void(const DocState &, const QList<quint64> &)>;
    SnapshotCommand(const QString &text, DocState before, QList<quint64> selBefore,
                    DocState after, QList<quint64> selAfter, Apply apply)
        : QUndoCommand(text), m_before(std::move(before)), m_selBefore(std::move(selBefore)),
          m_after(std::move(after)), m_selAfter(std::move(selAfter)), m_apply(std::move(apply))
    {
    }
    void undo() override { m_apply(m_before, m_selBefore); }
    void redo() override { m_apply(m_after, m_selAfter); }

private:
    DocState m_before;
    QList<quint64> m_selBefore;
    DocState m_after;
    QList<quint64> m_selAfter;
    Apply m_apply;
};

QIcon themed(QIcon::ThemeIcon icon) { return QIcon::fromTheme(icon); }

// Icons for the three modes, drawn so they match each other (and the text
// colour, in light and dark themes).
enum class ModeIcon { View, Fill, Edit };
QIcon modeIcon(ModeIcon kind)
{
    QIcon icon;
    const QColor ink = QApplication::palette().color(QPalette::WindowText);
    for (int size : {16, 22, 32, 44, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 24.0, size / 24.0); // drawn on a 24x24 grid
        QPen pen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        switch (kind) {
        case ModeIcon::View: { // an eye
            QPainterPath eye;
            eye.moveTo(2, 12);
            eye.quadTo(12, 2.5, 22, 12);
            eye.quadTo(12, 21.5, 2, 12);
            p.drawPath(eye);
            p.setBrush(ink);
            p.drawEllipse(QPointF(12, 12), 3.2, 3.2);
            break;
        }
        case ModeIcon::Fill: { // a pen signing: the pen, and a squiggle under its tip
            QPainterPath squiggle;
            squiggle.moveTo(2, 19.5);
            squiggle.cubicTo(4, 15, 6, 15, 6.5, 18.5);
            squiggle.cubicTo(7, 22, 9.5, 20, 10.5, 17.5);
            squiggle.cubicTo(11.2, 16, 12, 17.5, 12.8, 19);
            p.drawPath(squiggle);
            p.save();
            p.translate(13, 16.5);
            p.rotate(-45);
            QPainterPath body; // pen body from the tip (0,0) up and to the right
            body.moveTo(0, 0);
            body.lineTo(3.2, -1.7);
            body.lineTo(14.5, -1.7);
            body.quadTo(16, -1.7, 16, 0);
            body.quadTo(16, 1.7, 14.5, 1.7);
            body.lineTo(3.2, 1.7);
            body.closeSubpath();
            p.drawPath(body);
            p.drawLine(QPointF(5.5, -1.7), QPointF(5.5, 1.7));
            p.restore();
            break;
        }
        case ModeIcon::Edit: { // a grid of pages
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 2; ++c)
                    p.drawRoundedRect(QRectF(3 + c * 10, 2.5 + r * 10.5, 7.5, 8.5), 1, 1);
            break;
        }
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_library(new Library(this)),
      m_model(new PageModel(m_library, this)),
      m_grid(new PageGrid(m_library, this)),
      m_undo(new QUndoStack(this))
{
    m_grid->setModel(m_model);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view = new DocumentView(m_library, m_model,
                              [this](const QString &text, const PageList &pages, const FieldValues &fields) {
                                  commitState(text, pages, fields, selectedUids());
                              },
                              this);
    m_stack = new QStackedWidget;
    m_stack->addWidget(m_view); // View and Fill & Sign
    m_stack->addWidget(m_grid); // Edit
    setCentralWidget(m_stack);
    setAcceptDrops(false); // the viewer and the grid handle drops

    createActions();
    createMenusAndToolbar();

    m_statusLabel = new QLabel;
    statusBar()->addWidget(m_statusLabel, 1);
    m_zoomLabel = new QLabel(tr("Thumbnail size"));
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(kMinZoom, kMaxZoom);
    m_zoomSlider->setFixedWidth(160);
    statusBar()->addPermanentWidget(m_zoomLabel);
    statusBar()->addPermanentWidget(m_zoomSlider);
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) { m_grid->setThumbSize(v); });

    connect(m_grid, &PageGrid::pagesMoved, this, &MainWindow::movePages);
    connect(m_grid, &PageGrid::filesDropped, this, [this](const QStringList &paths, int row) {
        if (m_model->rowCount() == 0)
            openFiles(paths);
        else
            insertFiles(paths, row);
    });
    connect(m_grid, &PageGrid::zoomRequested, this, [this](int d) { setZoom(m_zoomSlider->value() + d * 30); });
    connect(m_grid, &QAbstractItemView::doubleClicked, this, [this] { preview(); });
    connect(m_view, &DocumentView::filesDropped, this, [this](const QStringList &paths) { openFiles(paths); });
    connect(m_view, &DocumentView::currentRowChanged, this, [this](int row) {
        // In the viewer, page commands (rotate, export, ...) act on the page being read.
        if (m_mode != Mode::Edit && row >= 0 && row < m_model->rowCount())
            m_grid->selectionModel()->setCurrentIndex(m_model->index(row, 0), QItemSelectionModel::ClearAndSelect);
    });
    connect(m_grid, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_grid->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        updateActions();
        updateStatus();
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        // The selection model is recreated-ish on reset; reconnect is not needed but state is.
        updateActions();
        updateStatus();
        updateTitle();
    });
    connect(m_undo, &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);

    QSettings s;
    setZoom(s.value(QStringLiteral("zoom"), 170).toInt());
    if (!restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray()))
        resize(1100, 760);

    setMode(Mode::View);
    updateActions();
    updateStatus();
    updateTitle();
}

void MainWindow::setMode(Mode mode)
{
    m_view->finishEditing();
    m_mode = mode;
    m_stack->setCurrentIndex(mode == Mode::Edit ? 1 : 0);
    m_view->setFillMode(mode == Mode::Fill);
    (mode == Mode::View ? m_viewModeAct : mode == Mode::Fill ? m_fillModeAct : m_editModeAct)->setChecked(true);
    m_editBar->setVisible(mode == Mode::Edit);
    m_zoomLabel->setVisible(mode == Mode::Edit);
    m_zoomSlider->setVisible(mode == Mode::Edit);
    // Keys that mean something else on the page view (Delete removes what you
    // added, Return edits text) only drive page commands in Edit.
    const bool organize = mode == Mode::Edit; // page commands
    m_deleteAct->setShortcuts(organize ? QList<QKeySequence>{QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)}
                                       : QList<QKeySequence>{});
    m_previewAct->setShortcuts(organize ? QList<QKeySequence>{Qt::Key_Space, Qt::Key_Return, Qt::Key_Enter}
                                        : QList<QKeySequence>{});
    m_selectAllAct->setShortcuts(organize ? QList<QKeySequence>{QKeySequence::SelectAll} : QList<QKeySequence>{});
    if (organize) {
        const int row = m_view->currentRow();
        if (row >= 0 && row < m_model->rowCount()) {
            m_grid->selectionModel()->setCurrentIndex(m_model->index(row, 0), QItemSelectionModel::ClearAndSelect);
            m_grid->scrollTo(m_model->index(row, 0));
        }
        m_grid->setFocus();
    } else {
        const QList<int> rows = m_grid->selectedRows();
        if (!rows.isEmpty())
            m_view->showPage(rows.first());
        m_view->canvas()->setFocus();
    }
    updateActions();
    updateStatus();
}

// ------------------------------------------------------------------ setup

void MainWindow::createActions()
{
    auto make = [this](const QString &text, std::function<void()> fn, const QList<QKeySequence> &keys = {},
                       const QIcon &icon = QIcon()) {
        auto *a = new QAction(icon, text, this);
        a->setShortcuts(keys);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, [fn] { fn(); });
        addAction(a);
        return a;
    };

    m_openAct = make(tr("&Open…"), [this] { openDialog(); }, {QKeySequence::Open}, themed(QIcon::ThemeIcon::DocumentOpen));
    m_appendAct = make(tr("&Merge PDFs (Add to End)…"), [this] { appendDialog(); },
                       {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)}, themed(QIcon::ThemeIcon::ListAdd));
    m_insertAct = make(tr("&Insert PDF After Selection…"), [this] { insertDialog(); },
                       {QKeySequence(Qt::CTRL | Qt::Key_I)}, themed(QIcon::ThemeIcon::InsertText));
    m_saveAct = make(tr("&Save"), [this] { save(); }, {QKeySequence::Save}, themed(QIcon::ThemeIcon::DocumentSave));
    m_saveAsAct = make(tr("Save &As…"), [this] { saveAs(); }, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)},
                       themed(QIcon::ThemeIcon::DocumentSaveAs));
    m_exportAct = make(tr("&Export Selected Pages…"), [this] { exportSelected(); },
                       {QKeySequence(Qt::CTRL | Qt::Key_E)}, themed(QIcon::ThemeIcon::DocumentSend));
    m_splitAct = make(tr("S&plit into Files…"), [this] { splitIntoFiles(); }, {}, themed(QIcon::ThemeIcon::EditCut));
    m_closeAct = make(tr("&Close Document"), [this] { closeDocument(); }, {QKeySequence::Close});
    m_quitAct = make(tr("&Quit"), [this] { close(); }, {QKeySequence::Quit});
    m_quitAct->setMenuRole(QAction::QuitRole);

    m_undoAct = m_undo->createUndoAction(this, tr("&Undo"));
    m_undoAct->setShortcuts(QKeySequence::Undo);
    m_undoAct->setIcon(themed(QIcon::ThemeIcon::EditUndo));
    m_redoAct = m_undo->createRedoAction(this, tr("&Redo"));
    m_redoAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    m_redoAct->setIcon(themed(QIcon::ThemeIcon::EditRedo));
    addAction(m_undoAct);
    addAction(m_redoAct);

    m_selectAllAct = make(tr("Select &All"), [this] { m_grid->selectAll(); }, {QKeySequence::SelectAll});
    m_deleteAct = make(tr("&Delete Pages"), [this] { deleteSelected(); }, {QKeySequence::Delete, Qt::Key_Backspace},
                       themed(QIcon::ThemeIcon::EditDelete));
    m_duplicateAct = make(tr("D&uplicate Pages"), [this] { duplicateSelected(); }, {QKeySequence(Qt::CTRL | Qt::Key_D)},
                          themed(QIcon::ThemeIcon::EditCopy));
    m_blankAct = make(tr("Insert &Blank Page"), [this] { insertBlank(); }, {QKeySequence(Qt::CTRL | Qt::Key_B)},
                      themed(QIcon::ThemeIcon::DocumentNew));
    m_moveEarlierAct = make(tr("Move &Earlier"), [this] { moveSelection(-1); }, {QKeySequence(Qt::ALT | Qt::Key_Left)},
                            themed(QIcon::ThemeIcon::GoPrevious));
    m_moveLaterAct = make(tr("Move &Later"), [this] { moveSelection(1); }, {QKeySequence(Qt::ALT | Qt::Key_Right)},
                          themed(QIcon::ThemeIcon::GoNext));
    m_reverseAct = make(tr("Re&verse Order"), [this] { reverseSelected(); });

    m_rotLeftAct = make(tr("Rotate &Left"), [this] { rotateRows(m_grid->selectedRows(), 270, tr("Rotate left")); },
                        {QKeySequence(Qt::CTRL | Qt::Key_L)}, themed(QIcon::ThemeIcon::ObjectRotateLeft));
    m_rotRightAct = make(tr("Rotate &Right"), [this] { rotateRows(m_grid->selectedRows(), 90, tr("Rotate right")); },
                         {QKeySequence(Qt::CTRL | Qt::Key_R)}, themed(QIcon::ThemeIcon::ObjectRotateRight));
    m_rot180Act = make(tr("Rotate &180°"), [this] { rotateRows(m_grid->selectedRows(), 180, tr("Rotate 180°")); });
    auto allRows = [this] {
        QList<int> rows;
        for (int i = 0; i < m_model->rowCount(); ++i)
            rows << i;
        return rows;
    };
    m_rotAllLeftAct = make(tr("Rotate All Pages Left"), [this, allRows] { rotateRows(allRows(), 270, tr("Rotate all left")); },
                           {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)});
    m_rotAllRightAct = make(tr("Rotate All Pages Right"), [this, allRows] { rotateRows(allRows(), 90, tr("Rotate all right")); },
                            {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R)});
    m_previewAct = make(tr("&Open Page in Viewer"), [this] { preview(); }, {Qt::Key_Space, Qt::Key_Return, Qt::Key_Enter},
                        themed(QIcon::ThemeIcon::ZoomFitBest));

    // The three ways of working on a document.
    auto *modes = new QActionGroup(this);
    m_viewModeAct = make(tr("&View"), [this] { setMode(Mode::View); }, {QKeySequence(Qt::CTRL | Qt::Key_1)},
                         modeIcon(ModeIcon::View));
    m_viewModeAct->setToolTip(tr("Read the document; nothing on the page can be changed (Ctrl+1)"));
    m_fillModeAct = make(tr("&Fill && Sign"), [this] { setMode(Mode::Fill); }, {QKeySequence(Qt::CTRL | Qt::Key_2)},
                         modeIcon(ModeIcon::Fill));
    m_fillModeAct->setToolTip(tr("Fill in form fields, type on the page, add check marks and sign (Ctrl+2)"));
    m_editModeAct = make(tr("&Edit"), [this] { setMode(Mode::Edit); }, {QKeySequence(Qt::CTRL | Qt::Key_3)},
                             modeIcon(ModeIcon::Edit));
    m_editModeAct->setToolTip(tr("Reorder, rotate, delete, merge and split pages (Ctrl+3)"));
    for (QAction *a : {m_viewModeAct, m_fillModeAct, m_editModeAct}) {
        a->setCheckable(true);
        modes->addAction(a);
    }

    m_zoomInAct = make(tr("Bigger Thumbnails"), [this] { setZoom(m_zoomSlider->value() + 30); }, {QKeySequence::ZoomIn},
                       themed(QIcon::ThemeIcon::ZoomIn));
    m_zoomOutAct = make(tr("Smaller Thumbnails"), [this] { setZoom(m_zoomSlider->value() - 30); }, {QKeySequence::ZoomOut},
                        themed(QIcon::ThemeIcon::ZoomOut));
    m_aboutAct = make(tr("&About Corbata PDF"), [this] { about(); });
    m_aboutAct->setMenuRole(QAction::AboutRole);

    // Short labels for the toolbar.
    m_appendAct->setIconText(tr("Merge"));
    m_insertAct->setIconText(tr("Insert PDF"));
    m_exportAct->setIconText(tr("Export"));
    m_splitAct->setIconText(tr("Split"));
    m_deleteAct->setIconText(tr("Delete"));
    m_duplicateAct->setIconText(tr("Duplicate"));
    m_blankAct->setIconText(tr("Blank"));
    m_rotLeftAct->setIconText(tr("Rotate Left"));
    m_rotRightAct->setIconText(tr("Rotate Right"));
    m_openAct->setIconText(tr("Open"));
    m_saveAct->setIconText(tr("Save"));
    m_undoAct->setIconText(tr("Undo"));
    m_redoAct->setIconText(tr("Redo"));
}

void MainWindow::createMenusAndToolbar()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_openAct);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    file->addAction(m_appendAct);
    file->addAction(m_insertAct);
    file->addSeparator();
    file->addAction(m_saveAct);
    file->addAction(m_saveAsAct);
    file->addAction(m_exportAct);
    file->addAction(m_splitAct);
    file->addSeparator();
    if (DefaultApp::isSupported()) {
        file->addAction(tr("Make &Default PDF App…"), this, [this] { DefaultApp::makeDefault(this); });
        file->addSeparator();
    }
    file->addAction(m_closeAct);
    file->addAction(m_quitAct);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_undoAct);
    edit->addAction(m_redoAct);
    edit->addSeparator();
    edit->addAction(m_selectAllAct);
    edit->addAction(m_deleteAct);
    edit->addAction(m_duplicateAct);
    edit->addAction(m_blankAct);
    edit->addSeparator();
    edit->addAction(m_moveEarlierAct);
    edit->addAction(m_moveLaterAct);
    edit->addAction(m_reverseAct);

    QMenu *page = menuBar()->addMenu(tr("&Pages"));
    page->addAction(m_rotLeftAct);
    page->addAction(m_rotRightAct);
    page->addAction(m_rot180Act);
    page->addSeparator();
    page->addAction(m_rotAllLeftAct);
    page->addAction(m_rotAllRightAct);
    page->addSeparator();
    page->addAction(m_previewAct);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_viewModeAct);
    view->addAction(m_fillModeAct);
    view->addAction(m_editModeAct);
    view->addSeparator();
    view->addAction(m_zoomInAct);
    view->addAction(m_zoomOutAct);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_aboutAct);

    QToolBar *tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("mainToolbar"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(22, 22));
    tb->addAction(m_openAct);
    tb->addAction(m_saveAct);
    tb->addSeparator();
    tb->addAction(m_undoAct);
    tb->addAction(m_redoAct);
    tb->addSeparator();
    tb->addAction(m_viewModeAct);
    tb->addAction(m_fillModeAct);
    tb->addAction(m_editModeAct);

    // Page tools, shown in Edit mode.
    m_editBar = addToolBar(tr("Edit"));
    m_editBar->setObjectName(QStringLiteral("editToolbar"));
    m_editBar->setMovable(false);
    m_editBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_editBar->setIconSize(QSize(22, 22));
    m_editBar->addAction(m_appendAct);
    m_editBar->addSeparator();
    m_editBar->addAction(m_rotLeftAct);
    m_editBar->addAction(m_rotRightAct);
    m_editBar->addSeparator();
    m_editBar->addAction(m_deleteAct);
    m_editBar->addAction(m_duplicateAct);
    m_editBar->addAction(m_blankAct);
    m_editBar->addSeparator();
    m_editBar->addAction(m_exportAct);
    m_editBar->addAction(m_splitAct);
}

// ------------------------------------------------------------------ state

QList<quint64> MainWindow::selectedUids() const
{
    QList<quint64> uids;
    const PageList &pages = m_model->pages();
    for (int r : m_grid->selectedRows())
        uids << pages.at(r).uid;
    return uids;
}

void MainWindow::applyState(const PageList &pages, const FieldValues &fields, const QList<quint64> &selection)
{
    m_model->setState(pages, fields);
    const QSet<quint64> want(selection.begin(), selection.end());
    QItemSelection sel;
    int first = -1;
    for (int r = 0; r < pages.size(); ++r) {
        if (want.contains(pages.at(r).uid)) {
            const QModelIndex idx = m_model->index(r, 0);
            sel.select(idx, idx);
            if (first < 0)
                first = r;
        }
    }
    if (first >= 0) {
        m_grid->selectionModel()->setCurrentIndex(m_model->index(first, 0), QItemSelectionModel::NoUpdate);
        m_grid->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
        m_grid->scrollTo(m_model->index(first, 0));
    }
    updateActions();
    updateStatus();
    updateTitle();
}

void MainWindow::commit(const QString &text, const PageList &after, const QList<quint64> &selectionAfter)
{
    commitState(text, after, m_model->fieldValues(), selectionAfter);
}

void MainWindow::commitState(const QString &text, const PageList &pages, const FieldValues &fields,
                             const QList<quint64> &selectionAfter)
{
    m_undo->push(new SnapshotCommand(text, DocState{m_model->pages(), m_model->fieldValues()}, selectedUids(),
                                     DocState{pages, fields}, selectionAfter,
                                     [this](const DocState &d, const QList<quint64> &s) { applyState(d.pages, d.fields, s); }));
}

PageList MainWindow::loadFiles(const QStringList &paths)
{
    PageList refs;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString &path : paths) {
        int id = -1;
        QString error;
        QApplication::restoreOverrideCursor(); // a password prompt may appear
        const Library::LoadResult res = m_library->load(path, this, id, error);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        if (res == Library::LoadResult::Failed) {
            QApplication::restoreOverrideCursor();
            QMessageBox::warning(this, tr("Could not open file"),
                                 tr("Could not open \"%1\".\n\n%2").arg(QDir::toNativeSeparators(path), error));
            QApplication::setOverrideCursor(Qt::WaitCursor);
            continue;
        }
        if (res == Library::LoadResult::Cancelled)
            continue;
        for (int i = 0; i < m_library->pageCount(id); ++i)
            refs.append(PageRef{id, i, 0, m_library->pageSize(id, i), m_nextUid++});
        addRecent(path);
        rememberDir(path);
    }
    QApplication::restoreOverrideCursor();
    return refs;
}

bool MainWindow::maybeSave()
{
    if (m_undo->isClean() || m_model->rowCount() == 0)
        return true;
    QMessageBox box(QMessageBox::Warning, tr("Unsaved changes"),
                    tr("Do you want to save the changes to \"%1\"?").arg(baseName()),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Save);
    switch (box.exec()) {
    case QMessageBox::Save: return save();
    case QMessageBox::Discard: return true;
    default: return false;
    }
}

void MainWindow::updateTitle()
{
    if (m_model->rowCount() == 0 && m_currentPath.isEmpty()) {
        setWindowTitle(tr("Corbata PDF"));
        setWindowModified(false);
        return;
    }
    const QString name = m_currentPath.isEmpty() ? tr("Untitled") : QFileInfo(m_currentPath).fileName();
    setWindowTitle(tr("%1[*] — Corbata PDF").arg(name));
    setWindowFilePath(m_currentPath);
    setWindowModified(!m_undo->isClean());
}

void MainWindow::updateActions()
{
    const bool hasPages = m_model->rowCount() > 0;
    const QList<int> sel = m_grid->selectedRows();
    const bool hasSel = !sel.isEmpty();
    for (QAction *a : {m_saveAct, m_saveAsAct, m_splitAct, m_closeAct, m_selectAllAct, m_rotAllLeftAct, m_rotAllRightAct, m_insertAct})
        a->setEnabled(hasPages);
    for (QAction *a : {m_exportAct, m_deleteAct, m_duplicateAct, m_rotLeftAct, m_rotRightAct, m_rot180Act, m_previewAct})
        a->setEnabled(hasSel);
    m_blankAct->setEnabled(hasPages);
    m_reverseAct->setEnabled(sel.size() > 1 || (!hasSel && m_model->rowCount() > 1));
    m_moveEarlierAct->setEnabled(hasSel && sel.first() > 0);
    m_moveLaterAct->setEnabled(hasSel && sel.last() < m_model->rowCount() - 1);
    // Working on several pages at once happens in Edit mode.
    if (m_mode != Mode::Edit)
        for (QAction *a : {m_selectAllAct, m_moveEarlierAct, m_moveLaterAct, m_reverseAct, m_previewAct})
            a->setEnabled(false);
    m_viewModeAct->setEnabled(true);
    m_fillModeAct->setEnabled(hasPages);
    m_editModeAct->setEnabled(true);
}

void MainWindow::updateStatus()
{
    const int n = m_model->rowCount();
    const int s = m_grid->selectedRows().size();
    if (n == 0)
        m_statusLabel->setText(tr("No document open"));
    else if (s == 0 || m_mode != Mode::Edit)
        m_statusLabel->setText(tr("%n page(s)", nullptr, n));
    else
        m_statusLabel->setText(tr("%n page(s)", nullptr, n) + QStringLiteral("  ·  ") + tr("%n selected", nullptr, s));
}

// ------------------------------------------------------------------ file operations

void MainWindow::openDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open PDF"), lastDir(), tr(kPdfFilter));
    if (!paths.isEmpty())
        openFiles(paths);
}

void MainWindow::openFiles(const QStringList &paths)
{
    if (paths.isEmpty() || !maybeSave())
        return;
    const PageList refs = loadFiles(paths);
    if (refs.isEmpty())
        return;
    QSet<int> ids;
    for (const PageRef &r : refs)
        ids.insert(r.sourceId);
    m_library->retainOnly(ids);
    m_undo->clear();
    // With several files opened at once there is no single original to overwrite.
    m_currentPath = ids.size() == 1 ? m_library->filePath(*ids.begin()) : QString();
    applyState(refs, {}, {});
    m_undo->setClean();
    if (ids.size() > 1)
        m_undo->resetClean(); // merged result is unsaved
    updateTitle();
    setMode(Mode::View);
    m_view->showPage(0);
}

void MainWindow::openOrAppend(const QStringList &paths)
{
    if (m_model->rowCount() == 0)
        openFiles(paths);
    else
        insertFiles(paths, m_model->rowCount());
}

void MainWindow::appendDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Merge PDFs"), lastDir(), tr(kPdfFilter));
    if (!paths.isEmpty())
        openOrAppend(paths);
}

void MainWindow::insertDialog()
{
    const QList<int> sel = m_grid->selectedRows();
    const int row = sel.isEmpty() ? m_model->rowCount() : sel.last() + 1;
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Insert PDF"), lastDir(), tr(kPdfFilter));
    if (!paths.isEmpty())
        insertFiles(paths, row);
}

void MainWindow::insertFiles(const QStringList &paths, int row)
{
    if (m_model->rowCount() == 0) {
        openFiles(paths);
        return;
    }
    const PageList refs = loadFiles(paths);
    if (refs.isEmpty())
        return;
    PageList pages = m_model->pages();
    row = qBound(0, row, int(pages.size()));
    QList<quint64> sel;
    for (int i = 0; i < refs.size(); ++i) {
        pages.insert(row + i, refs.at(i));
        sel << refs.at(i).uid;
    }
    commit(tr("Insert %n page(s)", nullptr, int(refs.size())), pages, sel);
    statusBar()->showMessage(tr("Inserted %n page(s)", nullptr, int(refs.size())), 4000);
}

bool MainWindow::writePdf(const QString &path, const PageList &pages)
{
    m_view->finishEditing();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QByteArray bytes;
    QString error;
    const bool built = PdfWriter::build(pages, m_library->sourceBytes(), bytes, error, m_model->fieldValues());
    QApplication::restoreOverrideCursor();
    if (!built) {
        QMessageBox::critical(this, tr("Could not create PDF"), tr("The PDF could not be created.\n\n%1").arg(error));
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        QMessageBox::critical(this, tr("Could not save"),
                              tr("Could not write \"%1\".\n\n%2").arg(QDir::toNativeSeparators(path), f.errorString()));
        return false;
    }
    return true;
}

bool MainWindow::save()
{
    if (m_model->rowCount() == 0)
        return false;
    if (m_currentPath.isEmpty())
        return saveAs();
    if (!writePdf(m_currentPath, m_model->pages()))
        return false;
    m_undo->setClean();
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(m_currentPath).fileName()), 4000);
    return true;
}

bool MainWindow::saveAs()
{
    if (m_model->rowCount() == 0)
        return false;
    const QString suggestion = QDir(lastDir()).filePath(baseName() + QStringLiteral(".pdf"));
    QString path = QFileDialog::getSaveFileName(this, tr("Save PDF As"), suggestion, tr(kPdfFilter));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".pdf");
    if (!writePdf(path, m_model->pages()))
        return false;
    m_currentPath = path;
    m_undo->setClean();
    addRecent(path);
    rememberDir(path);
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

QString MainWindow::describeRows(const QList<int> &rows)
{
    // {0,1,2,4} -> "1-3, 5"
    QStringList parts;
    for (int i = 0; i < rows.size();) {
        int j = i;
        while (j + 1 < rows.size() && rows.at(j + 1) == rows.at(j) + 1)
            ++j;
        parts << (i == j ? QString::number(rows.at(i) + 1)
                         : QStringLiteral("%1-%2").arg(rows.at(i) + 1).arg(rows.at(j) + 1));
        i = j + 1;
    }
    return parts.join(QStringLiteral(", "));
}

void MainWindow::exportSelected()
{
    const QList<int> rows = m_grid->selectedRows();
    if (rows.isEmpty())
        return;
    QString desc = describeRows(rows);
    if (desc.size() > 40)
        desc = tr("%n pages", nullptr, int(rows.size()));
    const QString suggestion = QDir(lastDir()).filePath(tr("%1 - pages %2.pdf").arg(baseName(), desc));
    QString path = QFileDialog::getSaveFileName(this, tr("Export Selected Pages"), suggestion, tr(kPdfFilter));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".pdf");
    PageList pages;
    for (int r : rows)
        pages << m_model->pages().at(r);
    if (writePdf(path, pages)) {
        rememberDir(path);
        statusBar()->showMessage(tr("Exported %n page(s) to %1", nullptr, int(pages.size())).arg(QFileInfo(path).fileName()), 5000);
    }
}

void MainWindow::splitIntoFiles()
{
    const PageList all = m_model->pages();
    const int n = all.size();
    if (n == 0)
        return;
    bool ok = false;
    const int per = QInputDialog::getInt(this, tr("Split into Files"),
                                         tr("Pages per file (1 = one file per page):"), 1, 1, n, 1, &ok);
    if (!ok)
        return;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder for the split files"), lastDir());
    if (dir.isEmpty())
        return;

    const int chunks = (n + per - 1) / per;
    const int width = QString::number(chunks).size();
    QStringList names;
    for (int c = 0; c < chunks; ++c) {
        const QString num = QStringLiteral("%1").arg(c + 1, width, 10, QLatin1Char('0'));
        names << QDir(dir).filePath(per == 1 ? tr("%1 - page %2.pdf").arg(baseName(), num)
                                             : tr("%1 - part %2.pdf").arg(baseName(), num));
    }
    int existing = 0;
    for (const QString &f : names)
        existing += QFileInfo::exists(f) ? 1 : 0;
    if (existing > 0 &&
        QMessageBox::question(this, tr("Replace files?"),
                              tr("%n file(s) with the same name already exist in that folder. Replace them?", nullptr, existing))
            != QMessageBox::Yes)
        return;

    for (int c = 0; c < chunks; ++c) {
        const PageList part = all.mid(c * per, per);
        if (!writePdf(names.at(c), part))
            return;
    }
    rememberDir(dir);

    QMessageBox box(QMessageBox::Information, tr("Split complete"),
                    tr("Created %n file(s) in:\n%1", nullptr, chunks).arg(QDir::toNativeSeparators(dir)),
                    QMessageBox::Ok, this);
    QPushButton *show = box.addButton(tr("Show Folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == show)
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::closeDocument()
{
    if (!maybeSave())
        return;
    m_undo->clear();
    m_currentPath.clear();
    applyState({}, {}, {});
    m_library->clear();
}

// ------------------------------------------------------------------ page operations

void MainWindow::deleteSelected()
{
    const QList<int> rows = m_grid->selectedRows();
    if (rows.isEmpty())
        return;
    const QSet<int> drop(rows.begin(), rows.end());
    PageList result;
    for (int i = 0; i < m_model->rowCount(); ++i)
        if (!drop.contains(i))
            result << m_model->pages().at(i);
    QList<quint64> sel;
    if (!result.isEmpty())
        sel << result.at(qMin(rows.first(), int(result.size()) - 1)).uid;
    commit(tr("Delete %n page(s)", nullptr, int(rows.size())), result, sel);
}

void MainWindow::duplicateSelected()
{
    const QList<int> rows = m_grid->selectedRows();
    if (rows.isEmpty())
        return;
    const QSet<int> dup(rows.begin(), rows.end());
    PageList result;
    QList<quint64> sel;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const PageRef &p = m_model->pages().at(i);
        result << p;
        if (dup.contains(i)) {
            PageRef copy = p;
            copy.uid = m_nextUid++;
            result << copy;
            sel << copy.uid;
        }
    }
    commit(tr("Duplicate %n page(s)", nullptr, int(rows.size())), result, sel);
}

void MainWindow::insertBlank()
{
    PageList pages = m_model->pages();
    if (pages.isEmpty())
        return;
    const QList<int> rows = m_grid->selectedRows();
    const int after = rows.isEmpty() ? pages.size() - 1 : rows.last();
    PageRef blank;
    blank.sourceId = -1;
    blank.size = pages.at(after).displaySize(); // same size as its neighbour
    blank.uid = m_nextUid++;
    pages.insert(after + 1, blank);
    commit(tr("Insert blank page"), pages, {blank.uid});
}

void MainWindow::rotateRows(const QList<int> &rows, int delta, const QString &text)
{
    if (rows.isEmpty())
        return;
    PageList pages = m_model->pages();
    for (int r : rows)
        pages[r].rotation = (pages[r].rotation + delta) % 360;
    QList<quint64> sel = selectedUids();
    commit(text, pages, sel);
}

void MainWindow::movePages(const QList<int> &rows, int destinationRow)
{
    if (rows.isEmpty())
        return;
    const PageList &pages = m_model->pages();
    const QSet<int> moving(rows.begin(), rows.end());
    PageList moved, rest;
    int insertAt = destinationRow;
    for (int i = 0; i < pages.size(); ++i) {
        if (moving.contains(i)) {
            moved << pages.at(i);
            if (i < destinationRow)
                --insertAt;
        } else {
            rest << pages.at(i);
        }
    }
    insertAt = qBound(0, insertAt, int(rest.size()));
    PageList result = rest;
    QList<quint64> sel;
    for (int k = 0; k < moved.size(); ++k) {
        result.insert(insertAt + k, moved.at(k));
        sel << moved.at(k).uid;
    }
    bool same = true;
    for (int i = 0; i < pages.size() && same; ++i)
        same = pages.at(i).uid == result.at(i).uid;
    if (same)
        return;
    commit(tr("Move %n page(s)", nullptr, int(moved.size())), result, sel);
}

void MainWindow::moveSelection(int direction)
{
    const QList<int> rows = m_grid->selectedRows();
    if (rows.isEmpty())
        return;
    if (direction < 0 && rows.first() > 0)
        movePages(rows, rows.first() - 1);
    else if (direction > 0 && rows.last() < m_model->rowCount() - 1)
        movePages(rows, rows.last() + 2);
}

void MainWindow::reverseSelected()
{
    QList<int> rows = m_grid->selectedRows();
    if (rows.size() < 2) {
        rows.clear();
        for (int i = 0; i < m_model->rowCount(); ++i)
            rows << i;
    }
    PageList pages = m_model->pages();
    for (int a = 0, b = rows.size() - 1; a < b; ++a, --b)
        std::swap(pages[rows.at(a)], pages[rows.at(b)]);
    commit(tr("Reverse order"), pages, selectedUids());
}

void MainWindow::preview()
{
    const QModelIndex cur = m_grid->currentIndex();
    const QList<int> rows = m_grid->selectedRows();
    const int row = cur.isValid() ? cur.row() : (rows.isEmpty() ? -1 : rows.first());
    if (row < 0)
        return;
    setMode(Mode::View);
    m_view->showPage(row);
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    const QModelIndex idx = m_grid->indexAt(pos);
    if (idx.isValid() && !m_grid->selectionModel()->isSelected(idx))
        m_grid->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    QMenu menu(this);
    if (m_model->rowCount() == 0) {
        menu.addAction(m_openAct);
    } else {
        menu.addAction(m_previewAct);
        menu.addSeparator();
        menu.addAction(m_rotLeftAct);
        menu.addAction(m_rotRightAct);
        menu.addAction(m_rot180Act);
        menu.addSeparator();
        menu.addAction(m_duplicateAct);
        menu.addAction(m_blankAct);
        menu.addAction(m_insertAct);
        menu.addAction(m_deleteAct);
        menu.addSeparator();
        menu.addAction(m_exportAct);
    }
    menu.exec(m_grid->viewport()->mapToGlobal(pos));
}

void MainWindow::setZoom(int px)
{
    px = qBound(kMinZoom, px, kMaxZoom);
    m_zoomSlider->setValue(px);
    m_grid->setThumbSize(px);
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About Corbata PDF"),
                       tr("<h3>Corbata PDF %1</h3>"
                          "<p>A small, fast tool for rearranging, filling and signing PDFs.</p>"
                          "<p><b>Tips</b><br>"
                          "Drag thumbnails to reorder. Drag PDF files from your desktop between pages to insert them.<br>"
                          "View (Ctrl+1): read, one page at a time; ← → turn pages.<br>"
                          "Fill &amp; Sign (Ctrl+2): click form fields to fill them in; Text, ✓, ✗, Date and Sign write on the page.<br>"
                          "Edit (Ctrl+3): rearrange pages; double-click a page to view it.<br>"
                          "Ctrl+mouse wheel resizes thumbnails.</p>"
                          "<p>Built with Qt %2 and qpdf. Pages are copied without re-rendering, so quality is never lost.</p>")
                           .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

// ------------------------------------------------------------------ helpers

QString MainWindow::baseName() const
{
    return m_currentPath.isEmpty() ? tr("Untitled") : QFileInfo(m_currentPath).completeBaseName();
}

QString MainWindow::lastDir() const
{
    const QString d = QSettings().value(QStringLiteral("lastDir")).toString();
    if (!d.isEmpty() && QDir(d).exists())
        return d;
    return QDir::homePath();
}

void MainWindow::rememberDir(const QString &fileOrDir)
{
    const QFileInfo fi(fileOrDir);
    QSettings().setValue(QStringLiteral("lastDir"), fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
}

void MainWindow::addRecent(const QString &path)
{
    QSettings s;
    QStringList recent = s.value(QStringLiteral("recentFiles")).toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    recent.removeAll(abs);
    recent.prepend(abs);
    while (recent.size() > kMaxRecent)
        recent.removeLast();
    s.setValue(QStringLiteral("recentFiles"), recent);
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList recent = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString &path : recent) {
        if (!QFileInfo::exists(path))
            continue;
        QAction *a = m_recentMenu->addAction(QFileInfo(path).fileName());
        a->setToolTip(QDir::toNativeSeparators(path));
        connect(a, &QAction::triggered, this, [this, path] { openFiles({path}); });
    }
    if (m_recentMenu->isEmpty()) {
        m_recentMenu->addAction(tr("(none)"))->setEnabled(false);
    } else {
        m_recentMenu->addSeparator();
        connect(m_recentMenu->addAction(tr("Clear Menu")), &QAction::triggered, this,
                [] { QSettings().remove(QStringLiteral("recentFiles")); });
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    QSettings s;
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("zoom"), m_zoomSlider->value());
    event->accept();
}
