#pragma once

#include "FormFields.h"
#include "PageRef.h"
#include "SignatureDialog.h"

#include <QDialog>
#include <QPointer>

#include <functional>

class Library;
class PageModel;
class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QUndoStack;
class EditorCanvas;

// Large view of one page with Fill & Sign tools: type text, add check marks,
// crosses and the date, sign, and fill in the PDF's own form fields.
// Left/Right (or Page Up/Down) step through pages.
class PageEditor : public QDialog
{
    Q_OBJECT
public:
    using Commit = std::function<void(const QString &text, const PageList &pages, const FieldValues &fields)>;

    PageEditor(Library *library, PageModel *model, QUndoStack *undo, int row, Commit commit,
               QWidget *parent = nullptr);

    int currentRow() const { return m_row; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class EditorCanvas;
    enum class Tool { Select, Text, Check, Cross, Date, Sign };
    enum class Drag { None, Move, Resize };

    // ---- state
    PageRef page() const;
    QVector<Mark> marks() const; // marks being shown (including an in-progress drag)
    const QVector<FieldInfo> &fields() const;
    QHash<QString, QString> changedValues() const;
    void commitMarks(const QString &text, const QVector<Mark> &marks);
    void commitField(const QString &name, const QString &value);
    void reloadFromModel();

    // ---- view
    void step(int delta);
    void goToRow(int row);
    void setZoom(qreal pointsToPixels);
    void fitWidth();
    void fitPage();
    void relayout();
    QTransform toDevice() const;
    QImage pageImage();
    void paintCanvas(QPainter &p);
    void updateCaption();

    // ---- interaction
    void setTool(Tool tool);
    void canvasPress(QMouseEvent *e);
    void canvasMove(QMouseEvent *e);
    void canvasRelease(QMouseEvent *e);
    void canvasDoubleClick(QMouseEvent *e);
    int markAt(const QPointF &pagePt) const;
    bool inResizeHandle(const QPointF &devicePt) const;
    QRectF handleRect() const;
    const FieldInfo *fieldAt(const QPointF &pagePt, const FieldWidget **widget = nullptr) const;
    void activateField(const FieldInfo &field, const FieldWidget &widget);
    void openFieldEditor(const FieldInfo &field, const FieldWidget &widget);
    void finishFieldEditor(bool commit);
    void moveToNextField(int direction);
    void placeMark(const QPointF &pagePt);
    void startTextEdit(const Mark &mark, bool isNew);
    void finishTextEdit(bool commit);
    void deleteSelected();
    void applyStyleToSelected();
    bool ensureSignature();
    Mark signatureMark(const QRectF &fit) const;
    QColor inkColor() const;

    Library *m_library;
    PageModel *m_model;
    QUndoStack *m_undo;
    Commit m_commit;
    int m_row;
    quint64 m_pageUid = 0;
    quint64 m_nextMarkUid = 1;

    // view
    QScrollArea *m_scroll;
    EditorCanvas *m_canvas;
    qreal m_zoom = 1.0;
    bool m_fitWidth = true;
    QImage m_cachedImage;
    QString m_cachedKey;
    QLabel *m_caption;
    QPushButton *m_prev, *m_next;
    QSpinBox *m_fontSize;
    QComboBox *m_color;
    QActionGroup *m_tools;
    QAction *m_toolActions[6] = {};
    Tool m_tool = Tool::Select;

    // interaction
    quint64 m_selected = 0;
    Drag m_drag = Drag::None;
    QPointF m_dragStart;          // page points
    QVector<Mark> m_dragMarks;    // live copy while dragging
    bool m_dragMoved = false;
    Signature m_signature;

    // inline editors
    QPointer<QWidget> m_textEdit;
    Mark m_editingMark;
    bool m_editingNew = false;
    QPointer<QWidget> m_fieldEdit;
    QString m_editingField;
};
