#include "PageModel.h"
#include "Library.h"

PageModel::PageModel(Library *library, QObject *parent)
    : QAbstractListModel(parent), m_library(library)
{
}

int PageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_pages.size();
}

QVariant PageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_pages.size())
        return {};
    const PageRef &p = m_pages.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return QString::number(index.row() + 1);
    case Qt::ToolTipRole: {
        QString tip = p.isBlank()
            ? tr("Blank page")
            : tr("%1 — page %2").arg(m_library->fileName(p.sourceId)).arg(p.pageIndex + 1);
        if (p.rotation)
            tip += tr(" (rotated %1°)").arg(p.rotation);
        return tip;
    }
    case PageRefRole:
        return QVariant::fromValue(p);
    default:
        return {};
    }
}

Qt::ItemFlags PageModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

void PageModel::setPages(const PageList &pages)
{
    beginResetModel();
    m_pages = pages;
    endResetModel();
}
