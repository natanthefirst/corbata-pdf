#include "PdfWriter.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <map>
#include <set>
#include <memory>
#include <stdexcept>
#include <vector>

namespace PdfWriter {

namespace {

struct Input {
    std::shared_ptr<QPDF> pdf;
    std::unique_ptr<QPDFAcroFormDocumentHelper> forms;
    std::vector<QPDFPageObjectHelper> pages;
};

std::string str(const QByteArray &b) { return std::string(b.constData(), size_t(b.size())); }

// Sets the user's field values in a source document before its pages are copied.
void applyFieldValues(QPDFAcroFormDocumentHelper &afdh, const QHash<QString, QString> &values)
{
    if (values.isEmpty() || !afdh.hasAcroForm())
        return;
    for (QPDFFormFieldObjectHelper field : afdh.getFormFields()) {
        const QString name = QString::fromStdString(field.getFullyQualifiedName());
        auto it = values.constFind(name);
        if (it == values.constEnd())
            continue;
        if (field.isCheckbox() || field.isRadioButton())
            // qpdf turns this into the right /V and /AS for each widget.
            field.setV(QPDFObjectHandle::newName("/" + it->toStdString()));
        else
            field.setV(it->toStdString());
    }
    afdh.generateAppearancesIfNeeded();
}

QPDFObjectHandle imageXObject(QPDF &pdf, const QImage &source)
{
    const QImage img = source.convertToFormat(QImage::Format_ARGB32);
    std::string rgb, alpha;
    rgb.reserve(size_t(img.width()) * img.height() * 3);
    alpha.reserve(size_t(img.width()) * img.height());
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            rgb += char(qRed(line[x]));
            rgb += char(qGreen(line[x]));
            rgb += char(qBlue(line[x]));
            alpha += char(qAlpha(line[x]));
        }
    }
    auto describe = [&](QPDFObjectHandle stream, const char *colorSpace) {
        QPDFObjectHandle d = stream.getDict();
        d.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
        d.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
        d.replaceKey("/Width", QPDFObjectHandle::newInteger(img.width()));
        d.replaceKey("/Height", QPDFObjectHandle::newInteger(img.height()));
        d.replaceKey("/ColorSpace", QPDFObjectHandle::newName(colorSpace));
        d.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    };
    QPDFObjectHandle mask = QPDFObjectHandle::newStream(&pdf, alpha);
    describe(mask, "/DeviceGray");
    QPDFObjectHandle image = QPDFObjectHandle::newStream(&pdf, rgb);
    describe(image, "/DeviceRGB");
    image.getDict().replaceKey("/SMask", mask);
    return image;
}

// copyForeignObject only takes indirect objects; this also walks direct
// dictionaries and arrays (e.g. an inline /DR << /Font << /Helv 5 0 R >> >>).
QPDFObjectHandle copyForeign(QPDF &to, QPDFObjectHandle obj)
{
    if (obj.isIndirect())
        return to.copyForeignObject(obj);
    if (obj.isDictionary()) {
        QPDFObjectHandle d = QPDFObjectHandle::newDictionary();
        for (const std::string &key : obj.getKeys())
            d.replaceKey(key, copyForeign(to, obj.getKey(key)));
        return d;
    }
    if (obj.isArray()) {
        QPDFObjectHandle a = QPDFObjectHandle::newArray();
        for (int i = 0; i < obj.getArrayNItems(); ++i)
            a.appendItem(copyForeign(to, obj.getArrayItem(i)));
        return a;
    }
    // Scalars: rebuild them in this file (objects can't be shared between files).
    return QPDFObjectHandle::parse(obj.unparse());
}

// A private copy of a resource sub-dictionary (/Font, /XObject) with extra
// entries, so pages that shared the original are not affected.
QPDFObjectHandle extendedSubdict(QPDFObjectHandle resources, const char *key,
                                 const std::vector<std::pair<std::string, QPDFObjectHandle>> &extra)
{
    QPDFObjectHandle sub = resources.getKey(key);
    QPDFObjectHandle copy = QPDFObjectHandle::newDictionary();
    if (sub.isDictionary())
        for (const std::string &k : sub.getKeys())
            copy.replaceKey(k, sub.getKey(k));
    for (const auto &[k, v] : extra)
        copy.replaceKey(k, v);
    return copy;
}

// Drops widgets (and then empty fields) that aren't on any output page, e.g.
// because their page was deleted. Returns false if nothing of `field` is left.
bool keepPlacedWidgets(QPDFObjectHandle field, const std::set<QPDFObjGen> &onPages)
{
    QPDFObjectHandle kids = field.getKey("/Kids");
    if (kids.isArray()) {
        QPDFObjectHandle left = QPDFObjectHandle::newArray();
        for (int i = 0; i < kids.getArrayNItems(); ++i)
            if (keepPlacedWidgets(kids.getArrayItem(i), onPages))
                left.appendItem(kids.getArrayItem(i));
        if (left.getArrayNItems() == 0)
            return onPages.count(field.getObjGen()) > 0;
        field.replaceKey("/Kids", left);
        return true;
    }
    return onPages.count(field.getObjGen()) > 0;
}

// Burns the marks into the page: the original content is wrapped in q/Q so
// it can't leave the graphics state changed, then the marks are drawn on top.
void addMarks(QPDF &pdf, QPDFObjectHandle page, const PageGeometry &geometry, const QVector<Mark> &marks,
              QPDFObjectHandle &helvetica)
{
    // A private /Resources with our font and images added.
    QPDFObjectHandle oldRes = page.getKey("/Resources");
    QPDFObjectHandle res = QPDFObjectHandle::newDictionary();
    if (oldRes.isDictionary())
        for (const std::string &k : oldRes.getKeys())
            res.replaceKey(k, oldRes.getKey(k));

    bool hasText = false;
    std::vector<std::pair<std::string, QPDFObjectHandle>> images;
    for (int i = 0; i < marks.size(); ++i) {
        const Mark &m = marks.at(i);
        hasText |= m.kind == Mark::Text;
        if (m.kind == Mark::Signature && !m.image.isNull())
            images.emplace_back("/CorbataImg" + std::to_string(i), imageXObject(pdf, m.image));
    }
    if (hasText) {
        if (!helvetica.isInitialized() || helvetica.isNull())
            helvetica = pdf.makeIndirectObject(QPDFObjectHandle::parse(
                "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"));
        res.replaceKey("/Font", extendedSubdict(oldRes, "/Font", {{"/CorbataHelv", helvetica}}));
    }
    if (!images.empty())
        res.replaceKey("/XObject", extendedSubdict(oldRes, "/XObject", images));
    page.replaceKey("/Resources", res);

    const QTransform t = geometry.displayToUser();
    QByteArray ops = "Q\nq\n";
    for (qreal v : {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()})
        ops += QByteArray::number(v, 'f', 4) + ' ';
    ops += "cm\n" + Marks::pdfOps(marks) + "Q\n";

    // Build a new /Contents array rather than editing the old one, which may be
    // shared with other copies of this page.
    QPDFObjectHandle contents = QPDFObjectHandle::newArray();
    contents.appendItem(QPDFObjectHandle::newStream(&pdf, "q\n"));
    QPDFObjectHandle old = page.getKey("/Contents");
    if (old.isArray()) {
        for (int i = 0; i < old.getArrayNItems(); ++i)
            contents.appendItem(old.getArrayItem(i));
    } else if (old.isStream()) {
        contents.appendItem(old);
    }
    contents.appendItem(QPDFObjectHandle::newStream(&pdf, str(ops)));
    page.replaceKey("/Contents", contents);
}

} // namespace

bool build(const PageList &pages, const QHash<int, SourceBytes> &sources,
           QByteArray &out, QString &error, const FieldValues &fields)
{
    try {
        // Inputs must stay alive until the writer has finished: copied streams
        // are read lazily from them.
        std::map<int, Input> inputs;

        auto inputFor = [&](int id) -> Input & {
            auto it = inputs.find(id);
            if (it != inputs.end())
                return it->second;
            auto src = sources.constFind(id);
            if (src == sources.constEnd())
                throw std::runtime_error("internal error: unknown source file");
            Input in;
            in.pdf = std::make_shared<QPDF>();
            in.pdf->setSuppressWarnings(true);
            in.pdf->processMemoryFile("source.pdf", src->data.constData(),
                                      static_cast<size_t>(src->data.size()),
                                      src->password.isEmpty() ? nullptr : src->password.constData());
            QPDFPageDocumentHelper dh(*in.pdf);
            // Rotate/MediaBox/Resources can be inherited from the page tree;
            // move them onto each page so they survive being copied.
            dh.pushInheritedAttributesToPage();
            in.forms = std::make_unique<QPDFAcroFormDocumentHelper>(*in.pdf);
            // Values go in before any page is copied (copies are cached).
            applyFieldValues(*in.forms, fields.value(id));
            in.pages = dh.getAllPages();
            return inputs[id] = std::move(in);
        };

        QPDF outPdf;
        outPdf.emptyPDF();
        QPDFPageDocumentHelper outDoc(outPdf);
        QPDFAcroFormDocumentHelper outForms(outPdf);
        QPDFObjectHandle helvetica;
        // Source pages already placed once; later copies are duplicates.
        std::set<std::pair<int, int>> placed;
        struct Duplicate {
            QPDFObjectHandle page, srcPage;
            Input *in;
        };
        std::vector<Duplicate> duplicates;

        for (const PageRef &ref : pages) {
            QPDFObjectHandle page;
            Input *in = nullptr;
            QPDFObjectHandle srcPage;
            if (ref.isBlank()) {
                const double w = ref.size.width() > 0 ? ref.size.width() : 612.0;
                const double h = ref.size.height() > 0 ? ref.size.height() : 792.0;
                const QString dict = QStringLiteral("<< /Type /Page /MediaBox [0 0 %1 %2] /Resources << >> >>")
                                         .arg(w, 0, 'f', 2)
                                         .arg(h, 0, 'f', 2);
                page = outPdf.makeIndirectObject(QPDFObjectHandle::parse(dict.toStdString()));
            } else {
                in = &inputFor(ref.sourceId);
                if (ref.pageIndex < 0 || ref.pageIndex >= static_cast<int>(in->pages.size()))
                    throw std::runtime_error("internal error: page index out of range");
                srcPage = in->pages[size_t(ref.pageIndex)].getObjectHandle();
                // copyForeignObject caches its result, so that copy is kept pristine
                // and every placed page is its own shallow copy. This lets the same
                // source page appear several times with different rotations.
                QPDFObjectHandle copied = outPdf.copyForeignObject(srcPage);
                page = outPdf.makeIndirectObject(copied.shallowCopy());
            }

            // Marks are positioned against the page's own box and rotation.
            const PageGeometry geometry = FormFields::geometryOf(page);
            QPDFPageObjectHelper helper(page);
            if (ref.rotation % 360 != 0)
                helper.rotatePage(ref.rotation, true);
            outDoc.addPage(helper, false);

            // A duplicated page needs its own annotations and form fields (below).
            if (in && !placed.insert({ref.sourceId, ref.pageIndex}).second && srcPage.getKey("/Annots").isArray()) {
                QPDFObjectHandle own = QPDFObjectHandle::newArray();
                QPDFObjectHandle shared = page.getKey("/Annots");
                for (int i = 0; i < shared.getArrayNItems(); ++i)
                    own.appendItem(shared.getArrayItem(i));
                page.replaceKey("/Annots", own);
                duplicates.push_back({page, srcPage, in});
            }

            if (!ref.marks.isEmpty())
                addMarks(outPdf, page, geometry, ref.marks, helvetica);
        }

        // Form fields: each source's field tree comes along whole (so a field
        // shown on several pages stays one field), minus widgets on pages that
        // were left out. Names that clash with another file's are renamed.
        std::set<QPDFObjGen> onPages;
        for (QPDFPageObjectHelper &p : outDoc.getAllPages()) {
            QPDFObjectHandle annots = p.getObjectHandle().getKey("/Annots");
            if (annots.isArray())
                for (int i = 0; i < annots.getArrayNItems(); ++i)
                    onPages.insert(annots.getArrayItem(i).getObjGen());
        }
        for (auto &[id, in] : inputs) {
            QPDFObjectHandle srcFields = in.pdf->getRoot().getKey("/AcroForm").getKey("/Fields");
            if (!srcFields.isArray())
                continue;
            std::vector<QPDFObjectHandle> kept;
            for (int i = 0; i < srcFields.getArrayNItems(); ++i) {
                QPDFObjectHandle field = srcFields.getArrayItem(i);
                if (!field.isIndirect())
                    continue;
                QPDFObjectHandle copy = outPdf.copyForeignObject(field);
                if (keepPlacedWidgets(copy, onPages))
                    kept.push_back(copy);
            }
            if (!kept.empty())
                outForms.addAndRenameFormFields(kept);
        }
        // Duplicated pages get private copies of their widgets as new fields.
        for (Duplicate &d : duplicates)
            outForms.fixCopiedAnnotations(d.page, d.srcPage, *d.in->forms);

        // Fonts and default appearance for the fields, so viewers can draw new values.
        if (outForms.hasAcroForm()) {
            QPDFObjectHandle acro = outPdf.getRoot().getKey("/AcroForm");
            bool changed = false;
            for (auto &[id, in] : inputs) {
                QPDFObjectHandle srcAcro = in.pdf->getRoot().getKey("/AcroForm");
                if (!srcAcro.isDictionary())
                    continue;
                if (!acro.hasKey("/DA") && srcAcro.getKey("/DA").isString())
                    acro.replaceKey("/DA", copyForeign(outPdf, srcAcro.getKey("/DA")));
                if (!acro.hasKey("/DR") && srcAcro.getKey("/DR").isDictionary())
                    acro.replaceKey("/DR", copyForeign(outPdf, srcAcro.getKey("/DR")));
                changed |= !fields.value(id).isEmpty();
            }
            if (changed)
                outForms.setNeedAppearances(true);
        }

        QPDFWriter writer(outPdf);
        writer.setOutputMemory();
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setCompressStreams(true);
        writer.write();
        auto buffer = writer.getBufferSharedPointer();
        out = QByteArray(reinterpret_cast<const char *>(buffer->getBuffer()),
                         static_cast<qsizetype>(buffer->getSize()));
        return true;
    } catch (const std::exception &e) {
        error = QString::fromLocal8Bit(e.what());
        return false;
    }
}

} // namespace PdfWriter
