// Drives the viewer (with its Fill & Sign tools) with simulated mouse and keyboard input, the
// way a person would, then saves and checks the result.
// Runs without a screen (QT_QPA_PLATFORM=offscreen).

#include "FormFields.h"
#include "Library.h"
#include "DocumentView.h"
#include "PageModel.h"
#include "PdfWriter.h"
#include "TestPdfs.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>

#include <cstdio>

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

// Same idea as the app's undo: every change is a snapshot.
class Snapshot : public QUndoCommand
{
public:
    Snapshot(PageModel *m, PageList p, FieldValues f, const QString &text)
        : QUndoCommand(text), m_model(m), m_beforeP(m->pages()), m_beforeF(m->fieldValues()), m_p(std::move(p)),
          m_f(std::move(f))
    {
    }
    void undo() override { m_model->setState(m_beforeP, m_beforeF); }
    void redo() override { m_model->setState(m_p, m_f); }

private:
    PageModel *m_model;
    PageList m_beforeP, m_p;
    FieldValues m_beforeF, m_f;
};

template <typename T> static T *visibleChild(QWidget *parent)
{
    for (T *w : parent->findChildren<T *>())
        if (w->isVisible())
            return w;
    return nullptr;
}

static void settle()
{
    for (int i = 0; i < 5; ++i)
        QTest::qWait(20);
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("form.pdf"));
    {
        QFile f(path);
        const QByteArray form = makeForm();
        CHECK(f.open(QIODevice::WriteOnly) && f.write(form) == form.size());
    }

    Library library;
    int id = -1;
    QString error;
    CHECK(library.load(path, nullptr, id, error) == Library::LoadResult::Ok);
    CHECK(library.fields(id).size() == 6);
    PageList pages;
    for (int i = 0; i < library.pageCount(id); ++i)
        pages.append(PageRef{id, i, 0, library.pageSize(id, i), quint64(100 + i)});
    PageModel model(&library);
    model.setState(pages, {});
    QUndoStack undo;

    DocumentView editor(&library, &model, [&](const QString &text, const PageList &p, const FieldValues &f) {
        undo.push(new Snapshot(&model, p, f, text));
    });
    editor.resize(900, 1000);
    editor.show();
    settle();

    QWidget *canvas = editor.canvas();
    // Page points on page 1 -> canvas pixels.
    auto at = [&](qreal x, qreal y) { return editor.canvasPoint(0, QPointF(x, y)); };
    auto value = [&](const char *name) { return model.fieldValues().value(id).value(QString::fromLatin1(name)); };

    // --- View mode is read-only: clicks and tool keys do nothing.
    CHECK(!editor.isFillMode());
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at(150, 82));
    QTest::keyClick(canvas, Qt::Key_K);
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at(450, 500));
    settle();
    CHECK(!visibleChild<QLineEdit>(canvas));
    CHECK(model.fieldValues().isEmpty() && model.pages().at(0).marks.isEmpty() && undo.count() == 0);

    // --- Left / Right turn pages, one page at a time.
    QTest::keyClick(canvas, Qt::Key_Right);
    CHECK(editor.currentRow() == 1);
    QTest::keyClick(canvas, Qt::Key_Right); // already the last page
    CHECK(editor.currentRow() == 1);
    QTest::keyClick(canvas, Qt::Key_Left);
    CHECK(editor.currentRow() == 0);

    // --- Fit Width: the page spans the window's width (minus margins and the
    // scroll bar) and follows the window size; Fit Page shows all of it.
    auto action = [&](const char *text) {
        for (QAction *a : editor.findChildren<QAction *>())
            if (a->text() == QLatin1String(text))
                return a;
        return static_cast<QAction *>(nullptr);
    };
    auto pageWidthPx = [&] { return editor.canvasPoint(0, QPointF(612, 0)).x() - editor.canvasPoint(0, QPointF(0, 0)).x(); };
    QScrollArea *scroll = editor.findChild<QScrollArea *>();
    CHECK(action("Fit Width") && action("Fit Page"));
    if (action("Fit Width")) {
        action("Fit Width")->trigger();
        settle();
        const int wide = pageWidthPx();
        CHECK(wide > scroll->width() - 80 && wide <= scroll->width());
        editor.resize(700, 1000);
        settle();
        CHECK(pageWidthPx() > scroll->width() - 80 && pageWidthPx() <= scroll->width());
        CHECK(pageWidthPx() < wide);
        action("Fit Page")->trigger();
        settle();
        const int bottom = editor.canvasPoint(0, QPointF(0, 792)).y() - scroll->verticalScrollBar()->value();
        CHECK(bottom <= scroll->viewport()->height());
        editor.resize(900, 1000);
        settle();
    }

    // --- Fill & Sign mode.
    editor.setFillMode(true);
    settle();

    // --- Text field: click, type, Tab to the next field (same row, to the right).
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at(150, 82));
    settle();
    QLineEdit *line = visibleChild<QLineEdit>(canvas);
    CHECK(line);
    if (line) {
        QTest::keyClicks(line, QStringLiteral("Jane Doe"));
        QTest::keyClick(line, Qt::Key_Tab);
        settle();
    }
    CHECK(value("name") == QLatin1String("Jane Doe"));
    line = visibleChild<QLineEdit>(canvas);
    CHECK(line); // Tab opened "shared"
    if (line) {
        QTest::keyClicks(line, QStringLiteral("Both"));
        QTest::keyClick(line, Qt::Key_Return);
        settle();
    }
    CHECK(value("shared") == QLatin1String("Both"));

    // --- Checkbox and radio buttons.
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at(78, 226));
    settle();
    CHECK(value("agree") == QLatin1String("Yes"));
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at(118, 266));
    settle();
    CHECK(value("color") == QLatin1String("Blue"));
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at(78, 266));
    settle();
    CHECK(value("color") == QLatin1String("Red"));

    // --- Drop-down.
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at(136, 304));
    settle();
    QComboBox *combo = visibleChild<QComboBox>(canvas);
    CHECK(combo);
    if (combo) {
        combo->hidePopup();
        combo->setCurrentIndex(1);
        emit combo->activated(1);
        settle();
    }
    CHECK(value("state") == QLatin1String("NY"));

    // --- Text tool: type on the page.
    QTest::keyClick(canvas, Qt::Key_T);
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at(400, 400));
    settle();
    QPlainTextEdit *typing = visibleChild<QPlainTextEdit>(canvas);
    CHECK(typing);
    if (typing) {
        QTest::keyClicks(typing, QStringLiteral("Hello"));
        QTest::keyClick(typing, Qt::Key_Escape);
        settle();
    }
    auto marks = [&] { return model.pages().at(0).marks; };
    CHECK(marks().size() == 1 && marks().value(0).kind == Mark::Text && marks().value(0).text == QLatin1String("Hello"));

    // --- Check mark tool, then move it with the Select tool.
    QTest::keyClick(canvas, Qt::Key_K);
    QTest::mouseClick(canvas, Qt::LeftButton, {}, at(450, 500));
    settle();
    CHECK(marks().size() == 2 && marks().value(1).kind == Mark::Check);
    CHECK(QLineF(marks().value(1).rect.center(), QPointF(450, 500)).length() < 2);
    QTest::keyClick(canvas, Qt::Key_Escape); // back to Select
    QTest::mousePress(canvas, Qt::LeftButton, {}, at(450, 500));
    for (int i = 1; i <= 5; ++i)
        QTest::mouseMove(canvas, at(450 + 4 * i, 500 + 4 * i));
    QTest::mouseRelease(canvas, Qt::LeftButton, {}, at(470, 520));
    settle();
    CHECK(marks().size() == 2 && QLineF(marks().value(1).rect.center(), QPointF(470, 520)).length() < 3);

    // --- Delete it, then undo the delete.
    QTest::keyClick(canvas, Qt::Key_Delete);
    settle();
    CHECK(marks().size() == 1);
    undo.undo();
    settle();
    CHECK(marks().size() == 2);

    // --- Save and read back.
    QByteArray out;
    QString err;
    CHECK(PdfWriter::build(model.pages(), library.sourceBytes(), out, err, model.fieldValues()));
    QHash<QString, QString> saved;
    for (const FieldInfo &f : FormFields::read(out))
        saved.insert(f.name, f.value);
    CHECK(saved.value(QStringLiteral("name")) == QLatin1String("Jane Doe"));
    CHECK(saved.value(QStringLiteral("shared")) == QLatin1String("Both"));
    CHECK(saved.value(QStringLiteral("agree")) == QLatin1String("Yes"));
    CHECK(saved.value(QStringLiteral("color")) == QLatin1String("Red"));
    CHECK(saved.value(QStringLiteral("state")) == QLatin1String("NY"));
    CHECK(out.size() > 0);

    // CORBATA_UI_SHOT=out.png saves a picture of the window at this point.
    const QString shot = qEnvironmentVariable("CORBATA_UI_SHOT");
    if (!shot.isEmpty())
        editor.grab().save(shot);

    editor.close();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All viewer tests passed\n");
    return 0;
}
