/*
 * isotreewidget.cpp
 * See isotreewidget.h.
 */
#include "isotreewidget.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QUrl>

/* ------------------------------------------------------------------ IsoItem */

IsoItem::IsoItem(QTreeWidget* parent, const std::string& path, Kind kind,
                 unsigned long long size)
    : QTreeWidgetItem(parent), path_(path), kind_(kind), size_(size)
{
}

IsoItem::IsoItem(QTreeWidgetItem* parent, const std::string& path, Kind kind,
                 unsigned long long size)
    : QTreeWidgetItem(parent), path_(path), kind_(kind), size_(size)
{
}

bool IsoItem::operator<(const QTreeWidgetItem& other) const
{
    const auto* o = dynamic_cast<const IsoItem*>(&other);
    if (!o)
        return QTreeWidgetItem::operator<(other);

    const int column = treeWidget() ? treeWidget()->sortColumn() : 0;

    /* directories group before everything else when sorting by name */
    if (column == 0 && isDir() != o->isDir())
        return isDir();

    if (column == 1)
        return size_ < o->size_;

    return QString::localeAwareCompare(text(column), o->text(column)) < 0;
}

/* ------------------------------------------------------------ IsoTreeWidget */

IsoTreeWidget::IsoTreeWidget(QWidget* parent) : QTreeWidget(parent)
{
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setDropIndicatorShown(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setUniformRowHeights(true);
    setAlternatingRowColors(true);
    setSortingEnabled(true);
    sortByColumn(0, Qt::AscendingOrder);
    header()->setStretchLastSection(true);
}

QStringList IsoTreeWidget::localPaths(const QMimeData* mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls())
        return paths;
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    return paths;
}

void IsoTreeWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!localPaths(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void IsoTreeWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (localPaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint pos = event->position().toPoint();
#else
    const QPoint pos = event->pos();
#endif
    /* highlight the directory that will receive the files */
    if (auto* item = dynamic_cast<IsoItem*>(itemAt(pos))) {
        QTreeWidgetItem* target = item->isDir() ? item : item->parent();
        if (target)
            setCurrentItem(target, 0, QItemSelectionModel::ClearAndSelect);
    }
    event->acceptProposedAction();
}

void IsoTreeWidget::dropEvent(QDropEvent* event)
{
    const QStringList paths = localPaths(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint pos = event->position().toPoint();
#else
    const QPoint pos = event->pos();
#endif
    event->acceptProposedAction();
    emit filesDropped(paths, dynamic_cast<IsoItem*>(itemAt(pos)));
}
