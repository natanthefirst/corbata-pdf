#pragma once

#include "FormFields.h"
#include "PageRef.h"
#include "SignatureDialog.h"

#include <QHash>
#include <QPointer>
#include <QPolygonF>
#include <QTimer>
#include <QWidget>

#include <functional>

class Library;
class PageModel;
class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QScrollArea;
class QSpinBox;
class QToolBar;
class QToolButton;
class ViewCanvas;

// The page view, one page at a time. In View mode it only shows the document;
// in Fill & Sign mode it adds the tools: type text, add check marks, crosses
// and the date, sign, and fill in the PDF's own form fields on the page.
class DocumentView : public QWidget
{
    Q_OBJECT
public:
    using Commit = std::function<void(const QString &text, const PageList &pages, const FieldValues &fields)>;

    DocumentView(Library *library, PageModel *model, Commit commit, QWidget *parent = nullptr);

    // The page being shown.
    int currentRow() const { return m_current; }
    void showPage(int row);
    // Fill & Sign mode (tools, form filling) or read-only View mode.
    void setFillMode(bool on);
    bool isFillMode() const { return m_fill; }
    // Finishes any typing in progress (e.g. before saving).
    void finishEditing();

    // Search: shows the search field; next/previous match (wrapping around).
    void showFind();
    void findNext();
    void findPrevious();
    int matchCount() const { return int(m_matches.size()); }

    // Text selected with the mouse in View mode (Ctrl+C copies it).
    QString selectedText() const { return m_textSel; }
    void copySelection();
    void selectAllText();

    // For tests: where a page point is on the canvas, and the canvas itself.
    QPoint canvasPoint(int row, const QPointF &pagePt) const;
    QWidget *canvas() const;

signals:
    void currentRowChanged(int row);
    void filesDropped(const QStringList &paths);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class ViewCanvas;
    enum class Tool { Select, Text, Check, Cross, Date, Sign };
    enum class Drag { None, Move, Resize };
    enum class Fit { Page, Width, Custom };

    // ---- document
    int rowCount() const;
    PageRef page(int row) const;
    QVector<Mark> marks(int row) const; // including an in-progress drag
    const QVector<FieldInfo> &fields(int row) const;
    QHash<QString, QString> changedValues(int row) const;
    void commitMarks(int row, const QString &text, const QVector<Mark> &marks);
    void commitField(int row, const QString &name, const QString &value);
    void reloadFromModel();

    // ---- layout and painting
    void layoutPages();
    QRectF pageRect(int row) const { return row == m_current ? m_pageRect : QRectF(); }
    QTransform toDevice(int row) const;
    int rowAt(const QPointF &canvasPt) const;
    void setZoom(qreal pointsToPixels); // a custom zoom
    void zoomTo(qreal pointsToPixels);
    void setFit(Fit fit);
    void applyFit();
    QImage pageImage(int row);
    void paintCanvas(QPainter &p, const QRect &exposed);
    void updateCaption();
    void updateEmptyState();

    // ---- interaction
    void setTool(Tool tool);
    void canvasPress(QMouseEvent *e);
    void canvasMove(QMouseEvent *e);
    void canvasRelease(QMouseEvent *e);
    void canvasDoubleClick(QMouseEvent *e);
    bool canvasKey(QKeyEvent *e);
    int markAt(int row, const QPointF &pagePt) const;
    QRectF handleRect() const;
    bool inResizeHandle(const QPointF &canvasPt) const;
    const FieldInfo *fieldAt(int row, const QPointF &pagePt, const FieldWidget **widget = nullptr) const;
    void activateField(int row, const FieldInfo &field, const FieldWidget &widget);
    void openFieldEditor(int row, const FieldInfo &field, const FieldWidget &widget);
    void finishFieldEditor(bool commit);
    void moveToNextField(int direction);
    void placeMark(int row, const QPointF &pagePt);
    void startTextEdit(int row, const Mark &mark, bool isNew);
    void finishTextEdit(bool commit);
    void deleteSelected();
    void applyStyleToSelected();
    bool ensureSignature();
    Mark signatureMark(const QRectF &fit);
    QColor inkColor() const;

    // ---- search and text selection
    QString pageText(int row);
    void runSearch(bool jump = true);
    void goToMatch(int index);
    void hideFind();
    void updateFindCount();
    void clearTextSelection();
    void selectText(int row, const QPointF &from, const QPointF &to);
    QPointF snapToText(int row, const QPointF &pt, bool start);

    Library *m_library;
    PageModel *m_model;
    Commit m_commit;
    quint64 m_nextMarkUid = 1;

    // view
    QScrollArea *m_scroll;
    ViewCanvas *m_canvas;
    QLabel *m_empty;
    QRectF m_pageRect; // the current page on the canvas
    qreal m_zoom = 1.0;
    Fit m_fit = Fit::Page;
    bool m_fill = false;
    QToolBar *m_toolsBar;
    QAction *m_fitWidthAct, *m_fitPageAct;
    int m_current = 0;
    struct Cached {
        QString key;
        QImage image;
    };
    QHash<quint64, Cached> m_images; // by page uid
    QSpinBox *m_pageBox;  // current page, editable
    QLabel *m_pageTotal;  // "/ 12"
    QSpinBox *m_fontSize;
    QComboBox *m_color;
    QActionGroup *m_tools;
    QAction *m_toolActions[6] = {};
    Tool m_tool = Tool::Select;

    // interaction (a mark is identified by its page row and uid)
    int m_selRow = -1;
    quint64 m_selected = 0;
    Drag m_drag = Drag::None;
    QPointF m_dragStart; // page points
    QVector<Mark> m_dragMarks;
    bool m_dragMoved = false;
    Signature m_signature;

    // inline editors
    QPointer<QWidget> m_textEdit;
    int m_editRow = -1;
    Mark m_editingMark;
    bool m_editingNew = false;
    QPointer<QWidget> m_fieldEdit;
    int m_fieldRow = -1;
    QString m_editingField;

    // search
    QWidget *m_findBar;
    QLineEdit *m_findEdit;
    QLabel *m_findCount;
    QTimer m_findTimer;
    struct Match {
        int row;
        QList<QPolygonF> bounds; // page points
    };
    QVector<Match> m_matches;
    int m_matchIndex = -1;
    QString m_searched;
    QHash<quint64, QString> m_textCache; // by source page
    QHash<quint64, QVector<QRectF>> m_lineCache; // text line boxes, by source page

    // text selection (View mode)
    bool m_selecting = false;
    int m_textRow = -1;
    QPointF m_textFrom;
    QString m_textSel;
    QList<QPolygonF> m_textBounds;
};
