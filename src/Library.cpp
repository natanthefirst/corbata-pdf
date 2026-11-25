#include "Library.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QPdfDocument>
#include <QTimer>

Library::Library(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &Library::processQueue);
}

Library::~Library() { clear(); }

static void waitUntilLoaded(QPdfDocument *doc)
{
    if (doc->status() != QPdfDocument::Status::Loading)
        return;
    QEventLoop loop;
    QObject::connect(doc, &QPdfDocument::statusChanged, &loop, [&](QPdfDocument::Status s) {
        if (s != QPdfDocument::Status::Loading)
            loop.quit();
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    if (doc->status() == QPdfDocument::Status::Loading)
        loop.exec();
}

Library::LoadResult Library::load(const QString &path, QWidget *dialogParent, int &id, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return LoadResult::Failed;
    }
    const QByteArray data = file.readAll();
    file.close();

    auto *buffer = new QBuffer(this);
    buffer->setData(data);
    buffer->open(QIODevice::ReadOnly);
    auto *doc = new QPdfDocument(this);

    QByteArray password;
    doc->load(buffer);
    waitUntilLoaded(doc);

    bool askedOnce = false;
    while (doc->error() == QPdfDocument::Error::IncorrectPassword) {
        bool ok = false;
        const QString label = askedOnce
            ? tr("Wrong password. Try again for \"%1\":").arg(QFileInfo(path).fileName())
            : tr("\"%1\" is password-protected. Password:").arg(QFileInfo(path).fileName());
        const QString pwd = QInputDialog::getText(dialogParent, tr("Password required"), label,
                                                  QLineEdit::Password, QString(), &ok);
        askedOnce = true;
        if (!ok) {
            delete doc;
            delete buffer;
            return LoadResult::Cancelled;
        }
        password = pwd.toUtf8();
        doc->close();
        doc->setPassword(pwd);
        buffer->seek(0);
        doc->load(buffer);
        waitUntilLoaded(doc);
    }

    if (doc->status() != QPdfDocument::Status::Ready || doc->pageCount() <= 0) {
        switch (doc->error()) {
        case QPdfDocument::Error::InvalidFileFormat: error = tr("This is not a valid PDF file."); break;
        case QPdfDocument::Error::UnsupportedSecurityScheme: error = tr("This PDF uses an unsupported security scheme."); break;
        default: error = tr("The PDF could not be opened."); break;
        }
        delete doc;
        delete buffer;
        return LoadResult::Failed;
    }

    id = m_nextId++;
    m_sources.insert(id, Source{path, buffer, doc});
    m_bytes.insert(id, SourceBytes{data, password});
    return LoadResult::Ok;
}

void Library::clear()
{
    m_timer.stop();
    m_queue.clear();
    m_pendingPx.clear();
    m_cache.clear();
    for (auto &s : m_sources) {
        delete s.doc;
        delete s.buffer;
    }
    m_sources.clear();
    m_bytes.clear();
}

void Library::retainOnly(const QSet<int> &keep)
{
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        if (keep.contains(it.key())) {
            ++it;
            continue;
        }
        const int id = it.key();
        delete it->doc;
        delete it->buffer;
        m_bytes.remove(id);
        for (auto c = m_cache.begin(); c != m_cache.end();)
            c = (int(c.key() >> 32) == id) ? m_cache.erase(c) : std::next(c);
        it = m_sources.erase(it);
    }
}

QPdfDocument *Library::document(int id) const
{
    auto it = m_sources.constFind(id);
    return it == m_sources.constEnd() ? nullptr : it->doc;
}

QString Library::fileName(int id) const
{
    auto it = m_sources.constFind(id);
    return it == m_sources.constEnd() ? QString() : QFileInfo(it->path).fileName();
}

int Library::pageCount(int id) const
{
    QPdfDocument *doc = document(id);
    return doc ? doc->pageCount() : 0;
}

QSizeF Library::pageSize(int id, int page) const
{
    QPdfDocument *doc = document(id);
    return doc ? doc->pagePointSize(page) : QSizeF(612, 792);
}

QImage Library::render(int id, int page, int px) const
{
    QPdfDocument *doc = document(id);
    if (!doc || px <= 0)
        return {};
    QSizeF pts = doc->pagePointSize(page);
    if (pts.isEmpty())
        pts = QSizeF(612, 792);
    const QSize size = pts.scaled(QSizeF(px, px), Qt::KeepAspectRatio).toSize().expandedTo(QSize(1, 1));
    return doc->render(page, size);
}

QImage Library::thumbnail(int id, int page, int px)
{
    // Round up so small zoom changes don't trigger re-renders.
    px = ((px + 63) / 64) * 64;
    const quint64 k = key(id, page);
    QImage cached = m_cache.value(k);
    const int have = cached.isNull() ? 0 : qMax(cached.width(), cached.height());
    if (have < px && m_pendingPx.value(k, 0) < px) {
        m_pendingPx.insert(k, px);
        m_queue.append({id, page, px});
        if (!m_timer.isActive())
            m_timer.start();
    }
    return cached;
}

void Library::processQueue()
{
    QElapsedTimer budget;
    budget.start();
    bool changed = false;
    // Render for up to ~30 ms per event-loop turn, then let the UI breathe.
    while (!m_queue.isEmpty() && budget.elapsed() < 30) {
        const Request r = m_queue.takeLast(); // most recently requested first (what's on screen)
        const quint64 k = key(r.id, r.page);
        if (m_pendingPx.value(k) != r.px)
            continue; // superseded by a bigger request
        m_pendingPx.remove(k);
        QImage img = render(r.id, r.page, r.px);
        if (!img.isNull()) {
            m_cache.insert(k, img);
            changed = true;
        }
    }
    if (m_queue.isEmpty())
        m_timer.stop();
    if (changed)
        emit thumbnailsUpdated();
}
