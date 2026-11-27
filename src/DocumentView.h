#pragma once

#include "FormFields.h"
#include "PageRef.h"
#include "SignatureDialog.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

#include <functional>

class Library;
class PageModel;
class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QScrollArea;
class QSpinBox;
class QToolButton;
class ViewCanvas;

// The main reading view: every page in one continuous scroll, with the
// Fill & Sign tools on top. Type text, add check marks, crosses and the
// date, sign, and fill in the PDF's own form fields, right on the page.
class DocumentView : public QWidget
{
    Q_OBJECT
public:
    using Commit = std::function<void(const QString &text, const PageList &pages, const FieldValues &fields)>;

    DocumentView(Library *library, PageModel *model, Commit commit, QWidget *parent = nullptr);

    // The page in the middle of the view.
    int currentRow() const { return m_current; }
    // Scrolls so `row` is at the top.
    void showPage(int row);
    // Finishes any typing in progress (e.g. before saving).
    void finishEditing();

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
    QRectF pageRect(int row) const { return m_rects.value(row); }
    QTransform toDevice(int row) const;
    int rowAt(const QPointF &canvasPt) const;
    void setZoom(qreal pointsToPixels);
    void fitWidth();
    void fitPage();
    void updateCurrent();
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

    Library *m_library;
    PageModel *m_model;
    Commit m_commit;
    quint64 m_nextMarkUid = 1;

    // view
    QScrollArea *m_scroll;
    ViewCanvas *m_canvas;
    QLabel *m_empty;
    QVector<QRectF> m_rects; // page rectangles on the canvas
    qreal m_zoom = 1.0;
    bool m_fitWidth = true;
    int m_current = 0;
    struct Cached {
        QString key;
        QImage image;
    };
    QHash<quint64, Cached> m_images; // by page uid
    QLabel *m_caption;
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
};
