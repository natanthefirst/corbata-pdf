#include "PdfWriter.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace PdfWriter {

bool build(const PageList &pages, const QHash<int, SourceBytes> &sources,
           QByteArray &out, QString &error)
{
    try {
        // Inputs must stay alive until the writer has finished: copied streams
        // are read lazily from them.
        std::map<int, std::shared_ptr<QPDF>> inputs;
        std::map<int, std::vector<QPDFPageObjectHelper>> inputPages;

        auto pagesOf = [&](int id) -> std::vector<QPDFPageObjectHelper> & {
            auto it = inputPages.find(id);
            if (it != inputPages.end())
                return it->second;
            auto src = sources.constFind(id);
            if (src == sources.constEnd())
                throw std::runtime_error("internal error: unknown source file");
            auto q = std::make_shared<QPDF>();
            q->setSuppressWarnings(true);
            q->processMemoryFile("source.pdf", src->data.constData(),
                                 static_cast<size_t>(src->data.size()),
                                 src->password.isEmpty() ? nullptr : src->password.constData());
            QPDFPageDocumentHelper dh(*q);
            // Rotate/MediaBox/Resources can be inherited from the page tree;
            // move them onto each page so they survive being copied.
            dh.pushInheritedAttributesToPage();
            inputs[id] = q;
            return inputPages[id] = dh.getAllPages();
        };

        QPDF outPdf;
        outPdf.emptyPDF();
        QPDFPageDocumentHelper outDoc(outPdf);

        for (const PageRef &ref : pages) {
            QPDFObjectHandle page;
            if (ref.isBlank()) {
                const double w = ref.size.width() > 0 ? ref.size.width() : 612.0;
                const double h = ref.size.height() > 0 ? ref.size.height() : 792.0;
                const QString dict = QStringLiteral("<< /Type /Page /MediaBox [0 0 %1 %2] /Resources << >> >>")
                                         .arg(w, 0, 'f', 2)
                                         .arg(h, 0, 'f', 2);
                page = outPdf.makeIndirectObject(QPDFObjectHandle::parse(dict.toStdString()));
            } else {
                auto &srcPages = pagesOf(ref.sourceId);
                if (ref.pageIndex < 0 || ref.pageIndex >= static_cast<int>(srcPages.size()))
                    throw std::runtime_error("internal error: page index out of range");
                // copyForeignObject caches its result, so that copy is kept pristine
                // and every placed page is its own shallow copy. This lets the same
                // source page appear several times with different rotations.
                QPDFObjectHandle copied = outPdf.copyForeignObject(srcPages[ref.pageIndex].getObjectHandle());
                page = outPdf.makeIndirectObject(copied.shallowCopy());
            }

            QPDFPageObjectHelper helper(page);
            if (ref.rotation % 360 != 0)
                helper.rotatePage(ref.rotation, true);
            outDoc.addPage(helper, false);
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
