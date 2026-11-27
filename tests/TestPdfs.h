#pragma once
// Test PDFs built with qpdf, shared by the test programs.

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QByteArray>

#include <string>

using OH = QPDFObjectHandle;

inline QByteArray write(QPDF &q)
{
    QPDFWriter w(q);
    w.setOutputMemory();
    w.write();
    auto buf = w.getBufferSharedPointer();
    return QByteArray(reinterpret_cast<const char *>(buf->getBuffer()), qsizetype(buf->getSize()));
}

// A plain page source: `n` pages of w x h, all sharing one indirect /Resources.
inline QByteArray makePages(int n, double w, double h, int rotate = 0)
{
    QPDF q;
    q.emptyPDF();
    QPDFPageDocumentHelper dh(q);
    OH res = q.makeIndirectObject(OH::parse("<< /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>"));
    for (int i = 0; i < n; ++i) {
        OH page = q.makeIndirectObject(OH::parse("<< /Type /Page /MediaBox [0 0 " + std::to_string(w) + " " +
                                                 std::to_string(h) + "] >>"));
        page.replaceKey("/Resources", res);
        page.replaceKey("/Contents", OH::newStream(&q, "BT /F1 12 Tf 40 40 Td (page) Tj ET"));
        if (rotate)
            page.replaceKey("/Rotate", OH::newInteger(rotate));
        dh.addPage(page, false);
    }
    return write(q);
}

// A two-page fillable form: text, multiline text, checkbox, radio group,
// combo box, and one field with a widget on each page.
inline QByteArray makeForm()
{
    QPDF q;
    q.emptyPDF();
    QPDFPageDocumentHelper dh(q);
    OH helv = q.makeIndirectObject(OH::parse("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"));
    OH pages[2];
    for (int i = 0; i < 2; ++i) {
        pages[i] = q.makeIndirectObject(OH::parse("<< /Type /Page /MediaBox [0 0 612 792] >>"));
        OH res = OH::newDictionary();
        OH fonts = OH::newDictionary();
        fonts.replaceKey("/Helv", helv);
        res.replaceKey("/Font", fonts);
        pages[i].replaceKey("/Resources", res);
        pages[i].replaceKey("/Contents", OH::newStream(&q, "BT /Helv 14 Tf 72 740 Td (Sample form) Tj ET"));
        dh.addPage(pages[i], false);
    }
    auto ap = [&](const std::string &content) {
        OH s = OH::newStream(&q, content);
        OH d = s.getDict();
        d.replaceKey("/Type", OH::newName("/XObject"));
        d.replaceKey("/Subtype", OH::newName("/Form"));
        d.replaceKey("/BBox", OH::parse("[0 0 12 12]"));
        return s;
    };
    auto obj = [&](const std::string &dict) { return q.makeIndirectObject(OH::parse(dict)); };

    OH name = obj("<< /Type /Annot /Subtype /Widget /F 4 /FT /Tx /T (name) /Rect [72 700 300 720] /DA (/Helv 12 Tf 0 g) >>");
    OH notes = obj("<< /Type /Annot /Subtype /Widget /F 4 /FT /Tx /Ff 4096 /T (notes) /Rect [72 600 300 680] /DA (/Helv 10 Tf 0 g) >>");
    OH agree = obj("<< /Type /Annot /Subtype /Widget /F 4 /FT /Btn /T (agree) /Rect [72 560 84 572] /V /Off /AS /Off >>");
    OH onOff = OH::newDictionary();
    onOff.replaceKey("/Yes", ap("0 0 0 rg 2 2 8 8 re f"));
    onOff.replaceKey("/Off", ap(""));
    agree.replaceKey("/AP", OH::parse("<< >>"));
    agree.getKey("/AP").replaceKey("/N", onOff);

    OH color = obj("<< /FT /Btn /Ff 49152 /T (color) /V /Off >>");
    OH kids = OH::newArray();
    const char *states[2] = {"/Red", "/Blue"};
    for (int i = 0; i < 2; ++i) {
        OH kid = obj("<< /Type /Annot /Subtype /Widget /F 4 /AS /Off /Rect [" + std::to_string(72 + i * 40) +
                     " 520 " + std::to_string(84 + i * 40) + " 532] >>");
        kid.replaceKey("/Parent", color);
        OH n = OH::newDictionary();
        n.replaceKey(states[i], ap("0 0 0 rg 3 3 6 6 re f"));
        n.replaceKey("/Off", ap(""));
        OH apd = OH::newDictionary();
        apd.replaceKey("/N", n);
        kid.replaceKey("/AP", apd);
        kids.appendItem(kid);
    }
    color.replaceKey("/Kids", kids);

    OH state = obj("<< /Type /Annot /Subtype /Widget /F 4 /FT /Ch /Ff 131072 /T (state) /Opt [(CA) (NY)] /V (CA) "
                   "/Rect [72 480 200 496] /DA (/Helv 10 Tf 0 g) >>");

    OH shared = obj("<< /FT /Tx /T (shared) /DA (/Helv 10 Tf 0 g) >>");
    OH w1 = obj("<< /Type /Annot /Subtype /Widget /F 4 /Rect [320 700 500 720] >>");
    OH w2 = obj("<< /Type /Annot /Subtype /Widget /F 4 /Rect [320 700 500 720] >>");
    w1.replaceKey("/Parent", shared);
    w2.replaceKey("/Parent", shared);
    OH sharedKids = OH::newArray();
    sharedKids.appendItem(w1);
    sharedKids.appendItem(w2);
    shared.replaceKey("/Kids", sharedKids);

    OH annots0 = OH::newArray();
    for (OH a : {name, notes, agree, kids.getArrayItem(0), kids.getArrayItem(1), state, w1})
        annots0.appendItem(a);
    pages[0].replaceKey("/Annots", annots0);
    OH annots1 = OH::newArray();
    annots1.appendItem(w2);
    pages[1].replaceKey("/Annots", annots1);

    OH acro = OH::newDictionary();
    OH fields = OH::newArray();
    for (OH f : {name, notes, agree, color, state, shared})
        fields.appendItem(f);
    acro.replaceKey("/Fields", fields);
    acro.replaceKey("/DA", OH::newString("/Helv 0 Tf 0 g"));
    OH dr = OH::newDictionary();
    OH drFonts = OH::newDictionary();
    drFonts.replaceKey("/Helv", helv);
    dr.replaceKey("/Font", drFonts);
    acro.replaceKey("/DR", dr);
    q.getRoot().replaceKey("/AcroForm", q.makeIndirectObject(acro));
    return write(q);
}

