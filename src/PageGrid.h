#pragma once

#include <QListView>
#include <QStyledItemDelegate>

class Library;

class PageDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    PageDelegate(Library *library, QObject *parent = nullptr);

    void setThumbSize(int px) { m_thumb = px; }
    int thumbSize() const { return m_thumb; }
    QSize cellSize() const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    Library *m_library;
    int m_thumb = 170;
};

// Thumbnail grid: multi-select, drag to reorder, drop PDF files in at a position.
class PageGrid : public QListView
{
    Q_OBJECT
public:
    PageGrid(Library *library, QWidget *parent = nullptr);

    PageDelegate *pageDelegate() const { return m_delegate; }
    void setThumbSize(int px);
    QList<int> selectedRows() const;
    int dropRowAt(const QPoint &pos) const;

signals:
    void pagesMoved(const QList<int> &rows, int destinationRow);
    void filesDropped(const QStringList &paths, int destinationRow);
    void zoomRequested(int delta);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    static QStringList pdfPaths(const QMimeData *mime);
    bool acceptsDrag(const QMimeData *mime) const;
    void updateGrid();

    PageDelegate *m_delegate;
    int m_dropRow = -1;
};
