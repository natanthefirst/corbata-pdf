// Tests for Fill & Sign: marks burned into pages, fillable form fields read,
// filled and carried through saving, and (with Qt PDF) that marks land where
// they were placed, including on rotated pages.
//
//   corbata_fill_tests                 run the tests
//   corbata_fill_tests --samples <dir> also write a sample fillable form

#include "FormFields.h"
#include "PdfWriter.h"
#include "TestPdfs.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Pl_Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QSet>
#include <QTemporaryDir>

#include <cstdio>
#include <map>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static PageRef ref(int src, int page, int rot = 0, QSizeF size = QSizeF(612, 792))
{
    PageRef r;
    r.sourceId = src;
    r.pageIndex = page;
    r.rotation = rot;
    r.size = size;
    return r;
}

static std::string contentsOf(QPDFPageObjectHelper page)
{
    Pl_Buffer buf("contents");
    page.pipeContents(&buf);
    auto b = buf.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char *>(b->getBuffer()), b->getSize());
}

// A solid filled rectangle as a "signature" path, easy to find when rendered.
static Mark block(const QRectF &r)
{
    Mark m;
    m.kind = Mark::Signature;
    QPainterPath p;
    p.addRect(QRectF(0, 0, 10, 10));
    p.setFillRule(Qt::WindingFill);
    m.path = p;
    m.rect = r;
    return m;
}

static Mark text(const QString &t, QPointF at)
{
    Mark m;
    m.kind = Mark::Text;
    m.text = t;
    m.fontSize = 14;
    m.rect = QRectF(at, QSizeF(100, 17));
    return m;
}

static std::map<std::string, QPDFFormFieldObjectHelper> fieldsByName(QPDF &q, QPDFAcroFormDocumentHelper &afdh)
{
    std::map<std::string, QPDFFormFieldObjectHelper> out;
    for (auto f : afdh.getFormFields())
        out.emplace(f.getFullyQualifiedName(), f);
    return out;
}

// ---------------------------------------------------------------- marks

static void testMarks(QHash<int, SourceBytes> &sources)
{
    PageList pages;
    PageRef marked = ref(0, 0);
    marked.marks = {text(QStringLiteral("Héllo (world) \\ €"), QPointF(50, 60)), block(QRectF(300, 300, 50, 20))};
    Mark check;
    check.kind = Mark::Check;
    check.rect = QRectF(100, 100, 14, 14);
    marked.marks.append(check);
    Mark pic;
    pic.kind = Mark::Signature;
    pic.image = QImage(8, 4, QImage::Format_ARGB32);
    pic.image.fill(qRgba(0, 0, 200, 128));
    pic.rect = QRectF(200, 500, 80, 40);
    marked.marks.append(pic);
    pages << marked << ref(0, 0); // the same source page again, without marks
    PageRef blank;
    blank.sourceId = -1;
    blank.size = QSizeF(300, 400);
    blank.marks = {text(QStringLiteral("On blank"), QPointF(20, 20))};
    pages << blank;

    QByteArray out;
    QString err;
    CHECK(PdfWriter::build(pages, sources, out, err));
    if (!err.isEmpty())
        std::fprintf(stderr, "error: %s\n", qPrintable(err));
    QPDF q;
    q.processMemoryFile("out.pdf", out.constData(), size_t(out.size()));
    auto all = QPDFPageDocumentHelper(q).getAllPages();
    CHECK(all.size() == 3);
    if (all.size() != 3)
        return;
    const std::string c0 = contentsOf(all[0]);
    CHECK(c0.rfind("q\n", 0) == 0);                                  // original content wrapped
    CHECK(c0.find("/CorbataHelv 14 Tf") != std::string::npos);
    CHECK(c0.find("(H\xe9llo \\(world\\) \\\\ \x80) Tj") != std::string::npos); // WinAnsi + escaping
    CHECK(c0.find(" S\n") != std::string::npos || c0.find("\nS\n") != std::string::npos); // check mark stroke
    CHECK(c0.find("/CorbataImg3 Do") != std::string::npos);
    OH res0 = all[0].getObjectHandle().getKey("/Resources");
    CHECK(res0.getKey("/Font").hasKey("/CorbataHelv"));
    CHECK(res0.getKey("/Font").hasKey("/F1"));                       // original fonts kept
    OH img = res0.getKey("/XObject").getKey("/CorbataImg3");
    CHECK(img.isStream() && img.getDict().getKey("/SMask").isStream());
    CHECK(img.getDict().getKey("/Width").getIntValue() == 8);

    // The unmarked copy of the same page and its shared resources are untouched.
    const std::string c1 = contentsOf(all[1]);
    CHECK(c1.find("Corbata") == std::string::npos);
    CHECK(!all[1].getObjectHandle().getKey("/Resources").getKey("/Font").hasKey("/CorbataHelv"));

    const std::string c2 = contentsOf(all[2]);
    CHECK(c2.find("(On blank) Tj") != std::string::npos);
}

// ---------------------------------------------------------------- forms

static void testForms()
{
    const QByteArray form = makeForm();

    // Reading
    const QVector<FieldInfo> info = FormFields::read(form);
    CHECK(info.size() == 6);
    QHash<QString, FieldInfo> byName;
    for (const FieldInfo &f : info)
        byName.insert(f.name, f);
    CHECK(byName.value(QStringLiteral("name")).type == FieldInfo::Text);
    CHECK(byName.value(QStringLiteral("notes")).multiline);
    CHECK(byName.value(QStringLiteral("agree")).type == FieldInfo::Check);
    CHECK(byName.value(QStringLiteral("agree")).widgets.value(0).onState == QLatin1String("Yes"));
    CHECK(byName.value(QStringLiteral("color")).type == FieldInfo::Radio);
    CHECK(byName.value(QStringLiteral("color")).widgets.size() == 2);
    CHECK(byName.value(QStringLiteral("state")).type == FieldInfo::Choice);
    CHECK(byName.value(QStringLiteral("state")).options == QStringList({QStringLiteral("CA"), QStringLiteral("NY")}));
    CHECK(byName.value(QStringLiteral("state")).value == QLatin1String("CA"));
    CHECK(byName.value(QStringLiteral("shared")).widgets.size() == 2);
    const QRectF r = byName.value(QStringLiteral("name")).widgets.value(0).rect;
    CHECK(qAbs(r.x() - 72) < 0.01 && qAbs(r.y() - 72) < 0.01 && qAbs(r.width() - 228) < 0.01 && qAbs(r.height() - 20) < 0.01);
    CHECK(byName.value(QStringLiteral("name")).fontSize == 12);

    QHash<int, SourceBytes> sources;
    sources[0] = {form, {}};

    // Regression: saving without changes keeps a working form.
    {
        QByteArray out;
        QString err;
        CHECK(PdfWriter::build({ref(0, 0), ref(0, 1)}, sources, out, err));
        QPDF q;
        q.processMemoryFile("out.pdf", out.constData(), size_t(out.size()));
        QPDFAcroFormDocumentHelper afdh(q);
        CHECK(afdh.hasAcroForm());
        auto f = fieldsByName(q, afdh);
        CHECK(f.size() == 6);
        CHECK(f.count("name") && f.count("color") && f.count("shared"));
        CHECK(q.getRoot().getKey("/AcroForm").getKey("/DR").isDictionary());
        CHECK(FormFields::read(out).size() == 6);
    }

    // Filling
    {
        FieldValues values;
        values[0][QStringLiteral("name")] = QStringLiteral("Jane Doe");
        values[0][QStringLiteral("agree")] = QStringLiteral("Yes");
        values[0][QStringLiteral("color")] = QStringLiteral("Blue");
        values[0][QStringLiteral("state")] = QStringLiteral("NY");
        values[0][QStringLiteral("shared")] = QStringLiteral("Both pages");
        QByteArray out;
        QString err;
        CHECK(PdfWriter::build({ref(0, 0), ref(0, 1)}, sources, out, err, values));
        QPDF q;
        q.processMemoryFile("out.pdf", out.constData(), size_t(out.size()));
        QPDFAcroFormDocumentHelper afdh(q);
        auto f = fieldsByName(q, afdh);
        CHECK(f.count("name") && f.at("name").getValueAsString() == "Jane Doe");
        CHECK(f.count("state") && f.at("state").getValueAsString() == "NY");
        CHECK(f.count("shared") && f.at("shared").getValueAsString() == "Both pages");
        if (f.count("agree")) {
            CHECK(f.at("agree").getValue().isName() && f.at("agree").getValue().getName() == "/Yes");
            for (auto a : afdh.getAnnotationsForField(f.at("agree")))
                CHECK(a.getAppearanceState() == "/Yes");
        } else {
            CHECK(false);
        }
        if (f.count("color")) {
            CHECK(f.at("color").getValue().isName() && f.at("color").getValue().getName() == "/Blue");
            // qpdf lists each radio button separately; check them all.
            int on = 0, buttons = 0;
            for (auto ff : afdh.getFormFields())
                if (ff.getFullyQualifiedName() == "color")
                    for (auto a : afdh.getAnnotationsForField(ff)) {
                        ++buttons;
                        on += a.getAppearanceState() == "/Blue" ? 1 : 0;
                    }
            CHECK(buttons == 2 && on == 1);
        } else {
            CHECK(false);
        }
        if (f.count("name"))
            for (auto a : afdh.getAnnotationsForField(f.at("name")))
                CHECK(a.getAppearanceDictionary().getKey("/N").isStream());
        // And Corbata reads the values back.
        for (const FieldInfo &fi : FormFields::read(out)) {
            if (fi.name == QLatin1String("name"))
                CHECK(fi.value == QLatin1String("Jane Doe"));
            if (fi.name == QLatin1String("color"))
                CHECK(fi.value == QLatin1String("Blue"));
        }
    }

    // Duplicated pages and the same form merged twice keep every field, with distinct names.
    {
        QHash<int, SourceBytes> two = sources;
        two[1] = {form, {}};
        QByteArray out;
        QString err;
        CHECK(PdfWriter::build({ref(0, 0), ref(0, 0), ref(0, 1), ref(1, 0), ref(1, 1)}, two, out, err));
        QPDF q;
        q.processMemoryFile("out.pdf", out.constData(), size_t(out.size()));
        QPDFAcroFormDocumentHelper afdh(q);
        // Each name belongs to exactly one field (widgets of one field share its
        // top-level object), and the copies of "name" got distinct names.
        std::map<std::string, std::set<QPDFObjGen>> owners;
        for (auto f : afdh.getFormFields())
            owners[f.getFullyQualifiedName()].insert(f.getTopLevelField().getObjectHandle().getObjGen());
        bool oneEach = true;
        int nameCopies = 0;
        for (auto &[n, tops] : owners) {
            oneEach &= tops.size() == 1;
            nameCopies += n.rfind("name", 0) == 0 ? 1 : 0;
        }
        CHECK(oneEach);
        CHECK(nameCopies == 3); // original, duplicated page, second file
        // Every widget on every page is its own object.
        std::set<QPDFObjGen> seen;
        bool unique = true;
        for (auto &p : QPDFPageDocumentHelper(q).getAllPages())
            for (auto a : afdh.getWidgetAnnotationsForPage(p))
                unique &= seen.insert(a.getObjectHandle().getObjGen()).second;
        CHECK(unique);
    }
}

// ---------------------------------------------------------------- rendering

static bool dark(const QImage &img, QPointF pt)
{
    const QPoint p = pt.toPoint();
    if (!img.rect().contains(p))
        return false;
    // Qt PDF renders unpainted areas transparent, so ink = opaque and dark.
    const QRgb c = img.pixel(p);
    return qAlpha(c) > 128 && qGray(c) < 100;
}

static void testRendering(QHash<int, SourceBytes> &sources)
{
    QTemporaryDir dir;
    // Letter page as is, rotated by the user, a page whose PDF has /Rotate 90,
    // that one rotated again by the user, and a page with an offset CropBox.
    const QRectF spot(300, 200, 60, 40);
    PageList pages = {ref(0, 0), ref(0, 0, 90), ref(1, 0, 0, QSizeF(595, 842)), ref(1, 0, 270, QSizeF(595, 842)),
                      ref(2, 0, 0, QSizeF(400, 500))};
    for (PageRef &p : pages)
        p.marks = {block(spot)};
    QByteArray out;
    QString err;
    CHECK(PdfWriter::build(pages, sources, out, err));
    const QString path = dir.filePath(QStringLiteral("render.pdf"));
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly) && f.write(out) == out.size());
    f.close();

    QPdfDocument doc;
    CHECK(doc.load(path) == QPdfDocument::Error::None);
    CHECK(doc.pageCount() == pages.size());
    for (int i = 0; i < doc.pageCount() && i < pages.size(); ++i) {
        const QSizeF pts = doc.pagePointSize(i);
        const QImage img = doc.render(i, pts.toSize(), QPdfDocumentRenderOptions()).convertToFormat(QImage::Format_ARGB32);
        const QTransform t = pageToTarget(pages.at(i), QRectF(QPointF(0, 0), pts));
        const QRectF where = t.mapRect(spot);
        const bool inside = dark(img, where.center());
        const bool outside = dark(img, where.center() + QPointF(where.width(), 0));
        if (!inside || outside)
            std::fprintf(stderr, "page %d: mark expected at (%.0f, %.0f) in %dx%d\n", i, where.center().x(),
                         where.center().y(), img.width(), img.height());
        CHECK(inside);
        CHECK(!outside);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QHash<int, SourceBytes> sources;
    sources[0] = {makePages(2, 612, 792), {}};
    sources[1] = {makePages(1, 842, 595, 90), {}}; // landscape page shown portrait by /Rotate 90
    {
        // CropBox offset from the MediaBox origin.
        QPDF q;
        const QByteArray plain = makePages(1, 600, 800);
        q.processMemoryFile("c.pdf", plain.constData(), size_t(plain.size()));
        auto pg = QPDFPageDocumentHelper(q).getAllPages();
        pg[0].getObjectHandle().replaceKey("/CropBox", OH::parse("[100 150 500 650]"));
        sources[2] = {write(q), {}};
    }

    testMarks(sources);
    testForms();
    testRendering(sources);

    if (argc == 3 && std::string(argv[1]) == "--samples") {
        QDir d(QString::fromLocal8Bit(argv[2]));
        d.mkpath(QStringLiteral("."));
        QFile f(d.filePath(QStringLiteral("sample-fillable-form.pdf")));
        if (f.open(QIODevice::WriteOnly))
            f.write(makeForm());
    }

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All Fill & Sign tests passed\n");
    return 0;
}
