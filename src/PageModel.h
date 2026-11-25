#pragma once

#include "PageRef.h"

#include <QAbstractListModel>

class Library;

class PageModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { PageRefRole = Qt::UserRole + 1 };

    explicit PageModel(Library *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    const PageList &pages() const { return m_pages; }
    void setPages(const PageList &pages);

private:
    Library *m_library;
    PageList m_pages;
};
