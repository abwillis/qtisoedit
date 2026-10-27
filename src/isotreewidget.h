/*
 * isotreewidget.h
 * Tree of the image contents. Accepts local files dropped from a file manager.
 */
#ifndef ISOTREEWIDGET_H
#define ISOTREEWIDGET_H

#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <string>

class QMimeData;

class IsoItem : public QTreeWidgetItem
{
public:
    enum Kind { Directory, File, Symlink, Other };

    IsoItem(QTreeWidget* parent, const std::string& path, Kind kind,
            unsigned long long size = 0);
    IsoItem(QTreeWidgetItem* parent, const std::string& path, Kind kind,
            unsigned long long size = 0);

    const std::string& isoPath() const { return path_; }
    Kind kind() const { return kind_; }
    unsigned long long size() const { return size_; }
    bool isDir() const { return kind_ == Directory; }
    bool isRoot() const { return path_ == "/"; }

    bool operator<(const QTreeWidgetItem& other) const override;

private:
    std::string path_;
    Kind kind_;
    unsigned long long size_;
};

class IsoTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit IsoTreeWidget(QWidget* parent = nullptr);

signals:
    /* item is the drop target (may be null = root) */
    void filesDropped(const QStringList& localPaths, IsoItem* item);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    static QStringList localPaths(const QMimeData* mime);
};

#endif
