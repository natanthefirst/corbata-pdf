#pragma once

#include <QDialog>

class Library;
class PageModel;
class QLabel;
class QPushButton;

// Large view of one page. Left/Right (or Page Up/Down) step through pages.
class PreviewDialog : public QDialog
{
    Q_OBJECT
public:
    PreviewDialog(Library *library, PageModel *model, int row, QWidget *parent = nullptr);

    int currentRow() const { return m_row; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void step(int delta);
    void refresh();

    Library *m_library;
    PageModel *m_model;
    int m_row;
    QLabel *m_image;
    QLabel *m_caption;
    QPushButton *m_prev;
    QPushButton *m_next;
};
