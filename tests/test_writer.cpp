// Tests for PdfWriter: builds PDFs with qpdf, runs them through the same
// assembly code the app uses, and checks page order, rotation and sizes.
//
//   corbata_tests                   run the tests
//   corbata_tests --samples <dir>   also write sample PDFs for manual testing

#include "PdfWriter.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QDir>
#include <QFile>

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

// A PDF with `n` pages. Each page carries /CorbataId (firstId + i) so we can
// identify it after copying, and draws a big page number so it is visible.
static QByteArray makePdf(int n, int firstId, double w, double h, int inheritedRotate = 0)
{
    QPDF q;
    q.emptyPDF();
    QPDFPageDocumentHelper dh(q);
    QPDFObjectHandle font = q.makeIndirectObject(QPDFObjectHandle::parse(
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>"));
    for (int i = 0; i < n; ++i) {
        const int id = firstId + i;
        char content[512];
        std::snprintf(content, sizeof content,
                      "0.85 0.23 0.17 rg 20 20 %.0f 40 re f "
                      "BT /F1 %.0f Tf 0 0 0 rg %.0f %.0f Td (%d) Tj ET "
                      "BT /F1 14 Tf 30 %.0f Td (TOP of page id %d) Tj ET",
                      w - 40, h / 4, w / 4, h / 2.5, id, h - 40, id);
        QPDFObjectHandle page = q.makeIndirectObject(QPDFObjectHandle::parse(
            "<< /Type /Page /MediaBox [0 0 " + std::to_string(w) + " " + std::to_string(h) +
            "] /CorbataId " + std::to_string(id) + " >>"));
        QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
        QPDFObjectHandle fonts = QPDFObjectHandle::newDictionary();
        fonts.replaceKey("/F1", font);
        resources.replaceKey("/Font", fonts);
        page.replaceKey("/Resources", resources);
        page.replaceKey("/Contents", QPDFObjectHandle::newStream(&q, content));
        dh.addPage(page, false);
    }
    if (inheritedRotate)
        q.getRoot().getKey("/Pages").replaceKey("/Rotate", QPDFObjectHandle::newInteger(inheritedRotate));
    QPDFWriter writer(q);
    writer.setOutputMemory();
    writer.write();
    auto buf = writer.getBufferSharedPointer();
    return QByteArray(reinterpret_cast<const char *>(buf->getBuffer()), qsizetype(buf->getSize()));
}

struct PageInfo {
    int id;      // -1 for pages without /CorbataId (blank pages)
    int rotate;
    double w, h; // MediaBox size
};

static std::vector<PageInfo> inspect(const QByteArray &pdf)
{
    QPDF q;
    q.processMemoryFile("out.pdf", pdf.constData(), size_t(pdf.size()));
    std::vector<PageInfo> out;
    for (auto &p : QPDFPageDocumentHelper(q).getAllPages()) {
        QPDFObjectHandle oh = p.getObjectHandle();
        QPDFObjectHandle idObj = oh.getKey("/CorbataId");
        QPDFObjectHandle rot = p.getAttribute("/Rotate", false);
        QPDFObjectHandle box = p.getMediaBox();
        double w = box.getArrayItem(2).getNumericValue() - box.getArrayItem(0).getNumericValue();
        double h = box.getArrayItem(3).getNumericValue() - box.getArrayItem(1).getNumericValue();
        out.push_back({idObj.isInteger() ? int(idObj.getIntValue()) : -1,
                       rot.isInteger() ? int(rot.getIntValue()) : 0, w, h});
    }
    return out;
}

static PageRef ref(int src, int page, int rot = 0)
{
    PageRef r;
    r.sourceId = src;
    r.pageIndex = page;
    r.rotation = rot;
    return r;
}

int main(int argc, char **argv)
{
    QHash<int, SourceBytes> sources;
    sources[0] = {makePdf(3, 1, 612, 792), {}};           // A: ids 1,2,3, Letter portrait
    sources[1] = {makePdf(2, 101, 842, 595, 90), {}};     // B: ids 101,102, A4 landscape, /Rotate 90 inherited

    // --- merge + reorder + rotate + duplicate + blank
    {
        PageRef blank;
        blank.sourceId = -1;
        blank.size = QSizeF(200, 300);
        PageList pages = {ref(0, 2), ref(1, 1), ref(0, 0, 90), ref(0, 2, 270), blank, ref(1, 0, 180)};
        QByteArray out;
        QString err;
        CHECK(PdfWriter::build(pages, sources, out, err));
        auto info = inspect(out);
        CHECK(info.size() == 6);
        if (info.size() == 6) {
            CHECK(info[0].id == 3 && info[0].rotate == 0);
            CHECK(info[1].id == 102 && info[1].rotate == 90);   // inherited rotation kept
            CHECK(info[2].id == 1 && info[2].rotate == 90);
            CHECK(info[3].id == 3 && info[3].rotate == 270);    // duplicate rotated independently
            CHECK(info[4].id == -1 && info[4].rotate == 0);
            CHECK(int(info[4].w) == 200 && int(info[4].h) == 300);
            CHECK(info[5].id == 101 && info[5].rotate == 270);  // 90 inherited + 180
            CHECK(int(info[1].w) == 842 && int(info[1].h) == 595);
        }
        if (!err.isEmpty())
            std::fprintf(stderr, "error: %s\n", qPrintable(err));
    }

    // --- delete / export subset (pages 1 and 3 of A)
    {
        QByteArray out;
        QString err;
        CHECK(PdfWriter::build({ref(0, 0), ref(0, 2)}, sources, out, err));
        auto info = inspect(out);
        CHECK(info.size() == 2 && info[0].id == 1 && info[1].id == 3);
    }

    // --- rotate all: 4 x 90 = back to 0
    {
        QByteArray out;
        QString err;
        CHECK(PdfWriter::build({ref(0, 0, 0), ref(0, 1, 180)}, sources, out, err));
        auto info = inspect(out);
        CHECK(info.size() == 2 && info[0].rotate == 0 && info[1].rotate == 180);
    }

    // --- output can itself be used as a source (save, reopen, edit again)
    {
        QByteArray first, second;
        QString err;
        CHECK(PdfWriter::build({ref(1, 1, 90), ref(0, 1)}, sources, first, err));
        QHash<int, SourceBytes> again;
        again[7] = {first, {}};
        CHECK(PdfWriter::build({ref(7, 1), ref(7, 0, 90)}, again, second, err));
        auto info = inspect(second);
        CHECK(info.size() == 2 && info[0].id == 2 && info[1].id == 102 && info[1].rotate == 270);
    }

    // --- bad input reports an error instead of crashing
    {
        QHash<int, SourceBytes> broken;
        broken[0] = {QByteArray("this is not a pdf"), {}};
        QByteArray out;
        QString err;
        CHECK(!PdfWriter::build({ref(0, 0)}, broken, out, err));
        CHECK(!err.isEmpty());
        CHECK(!PdfWriter::build({ref(5, 0)}, sources, out, err)); // unknown source
        CHECK(!PdfWriter::build({ref(0, 99)}, sources, out, err)); // page out of range
    }

    // --- optional sample files for trying the app by hand
    if (argc == 3 && std::string(argv[1]) == "--samples") {
        QDir dir(QString::fromLocal8Bit(argv[2]));
        dir.mkpath(QStringLiteral("."));
        auto write = [&](const QString &name, const QByteArray &data) {
            QFile f(dir.filePath(name));
            if (f.open(QIODevice::WriteOnly))
                f.write(data);
        };
        write(QStringLiteral("sample-letter-12-pages.pdf"), makePdf(12, 1, 612, 792));
        write(QStringLiteral("sample-a4-landscape-4-pages.pdf"), makePdf(4, 101, 842, 595));
        write(QStringLiteral("sample-rotated-3-pages.pdf"), makePdf(3, 201, 612, 792, 90));
        std::printf("Wrote sample PDFs to %s\n", argv[2]);
    }

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All PdfWriter tests passed\n");
    return 0;
}
