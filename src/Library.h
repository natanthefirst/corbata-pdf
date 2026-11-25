#pragma once

#include "PdfWriter.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>

class QBuffer;
class QPdfDocument;

// Holds every PDF opened in this session (fully in memory, so the original
// files are never locked and can be overwritten on save) and renders
// thumbnails for them in small batches so the UI stays responsive.
class Library : public QObject
{
    Q_OBJECT
public:
    explicit Library(QObject *parent = nullptr);
    ~Library() override;

    enum class LoadResult { Ok, Cancelled, Failed };

    // Reads and opens a PDF, asking for a password if needed.
    // On success, returns the new source id via `id`.
    LoadResult load(const QString &path, QWidget *dialogParent, int &id, QString &error);

    void clear();
    // Drops every source not in `keep` (used when a new document replaces the old one).
    void retainOnly(const QSet<int> &keep);

    QPdfDocument *document(int id) const;
    QString fileName(int id) const;
    QString filePath(int id) const { return m_sources.value(id).path; }
    int pageCount(int id) const;
    QSizeF pageSize(int id, int page) const;
    const QHash<int, SourceBytes> &sourceBytes() const { return m_bytes; }

    // Returns the best cached thumbnail (possibly smaller than asked for, or null)
    // and queues a render at `px` (longest side, in device pixels) if needed.
    QImage thumbnail(int id, int page, int px);

    // Synchronous render for the preview window.
    QImage render(int id, int page, int px) const;

signals:
    void thumbnailsUpdated();

private:
    void processQueue();
    static quint64 key(int id, int page) { return (quint64(quint32(id)) << 32) | quint32(page); }

    struct Source {
        QString path;
        QBuffer *buffer = nullptr;
        QPdfDocument *doc = nullptr;
    };
    QHash<int, Source> m_sources;
    QHash<int, SourceBytes> m_bytes;
    int m_nextId = 0;

    struct Request { int id; int page; int px; };
    QHash<quint64, QImage> m_cache;
    QHash<quint64, int> m_pendingPx;
    QList<Request> m_queue;
    QTimer m_timer;
};
