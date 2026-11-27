#pragma once

#include <QDialog>
#include <QImage>
#include <QPainterPath>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class SignaturePad;

// A signature: a filled vector outline (drawn or typed) or a picture.
struct Signature {
    QPainterPath path;
    QImage image;
    bool isNull() const { return path.isEmpty() && image.isNull(); }
    QSizeF naturalSize() const; // aspect ratio source
};

// Draw, type or import a signature. The last few are remembered for reuse.
class SignatureDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SignatureDialog(QWidget *parent = nullptr);

    // Shows the dialog; returns a null signature if cancelled.
    static Signature getSignature(QWidget *parent);

    Signature signature() const { return m_result; }

private:
    void accept() override;
    Signature current() const;
    void updateOk();
    void chooseImage();
    void updateTypedPreview();
    void rebuildSaved();

    static QList<Signature> loadSaved();
    static void storeSaved(const QList<Signature> &list);

    QTabWidget *m_tabs;
    SignaturePad *m_pad;
    QLineEdit *m_name;
    QComboBox *m_font;
    QLabel *m_typedPreview;
    QLabel *m_imagePreview;
    QImage m_image;
    QHBoxLayout *m_savedRow;
    QWidget *m_savedBox;
    QPushButton *m_ok;
    Signature m_result;
};
