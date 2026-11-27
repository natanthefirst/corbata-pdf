#pragma once

#include "FormFields.h"
#include "PageRef.h"

#include <QMainWindow>

class DocumentView;
class Library;
class PageGrid;
class PageModel;
class QAction;
class QLabel;
class QMenu;
class QSlider;
class QStackedWidget;
class QToolBar;
class QUndoStack;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens files as a new document (asks to save unsaved changes first).
    void openFiles(const QStringList &paths);
    // Opens if nothing is open, otherwise appends.
    void openOrAppend(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // View: read, fill in and sign. Organize: the page grid.
    enum class Mode { View, Organize };
    void setMode(Mode mode);

    void createActions();
    void createMenusAndToolbar();

    // ---- state
    void commit(const QString &text, const PageList &after, const QList<quint64> &selectionAfter);
    void commitState(const QString &text, const PageList &pages, const FieldValues &fields,
                     const QList<quint64> &selectionAfter);
    void applyState(const PageList &pages, const FieldValues &fields, const QList<quint64> &selection);
    QList<quint64> selectedUids() const;
    PageList loadFiles(const QStringList &paths);
    bool maybeSave();
    void updateTitle();
    void updateActions();
    void updateStatus();

    // ---- operations
    void openDialog();
    void appendDialog();
    void insertDialog();
    void insertFiles(const QStringList &paths, int row);
    bool save();
    bool saveAs();
    void exportSelected();
    void splitIntoFiles();
    void closeDocument();
    void deleteSelected();
    void duplicateSelected();
    void insertBlank();
    void rotateRows(const QList<int> &rows, int delta, const QString &text);
    void movePages(const QList<int> &rows, int destinationRow);
    void moveSelection(int direction);
    void reverseSelected();
    void preview();
    void showContextMenu(const QPoint &pos);
    void setZoom(int px);
    void about();

    // ---- helpers
    bool writePdf(const QString &path, const PageList &pages);
    QString lastDir() const;
    void rememberDir(const QString &fileOrDir);
    void addRecent(const QString &path);
    void rebuildRecentMenu();
    QString baseName() const;
    static QString describeRows(const QList<int> &rows);

    Library *m_library;
    PageModel *m_model;
    PageGrid *m_grid;
    DocumentView *m_view = nullptr;
    QStackedWidget *m_stack = nullptr;
    QToolBar *m_organizeBar = nullptr;
    QLabel *m_zoomLabel = nullptr;
    Mode m_mode = Mode::View;
    QUndoStack *m_undo;
    QString m_currentPath;
    quint64 m_nextUid = 1;

    QLabel *m_statusLabel = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QMenu *m_recentMenu = nullptr;

    QAction *m_openAct, *m_appendAct, *m_insertAct, *m_saveAct, *m_saveAsAct, *m_exportAct,
        *m_splitAct, *m_closeAct, *m_quitAct, *m_undoAct, *m_redoAct, *m_selectAllAct,
        *m_deleteAct, *m_duplicateAct, *m_blankAct, *m_moveEarlierAct, *m_moveLaterAct,
        *m_reverseAct, *m_rotLeftAct, *m_rotRightAct, *m_rot180Act, *m_rotAllLeftAct,
        *m_rotAllRightAct, *m_previewAct, *m_viewModeAct, *m_organizeModeAct, *m_zoomInAct, *m_zoomOutAct, *m_aboutAct;
};
