/*
 * mainwindow.h
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "isosession.h"

#include <QMainWindow>
#include <QStringList>
#include <set>
#include <string>
#include <vector>

class QAction;
class QLabel;
class QMenu;
class IsoItem;
class IsoTreeWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openImage(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void newImage();
    void openImageDialog();
    bool saveAs();
    void closeImage();

    void addFiles();
    void addFolder();
    void newFolder();
    void renameItem();
    void deleteItems();
    void extractItems();
    void editPermissions();

    void showProperties();
    void useAsBootFile();
    void addBootRecord();
    void extractBootRecord();
    void removeBootRecord();

    void about();
    void updateActions();
    void showContextMenu(const QPoint& pos);
    void onFilesDropped(const QStringList& paths, IsoItem* target);

private:
    void createActions();
    void createMenus();
    void loadSettings();
    void saveSettings() const;

    bool maybeSave();
    void setDirty(bool dirty);
    void updateTitle();
    void updateStatus();

    void rebuildTree(const std::string& selectPath = std::string());
    void populate(IsoItem* parent, const std::string& dirPath,
                  const std::string& bootPath);

    void addPaths(const QStringList& paths, const std::string& destDir);

    IsoItem* currentIsoItem() const;
    std::vector<IsoItem*> selectedItems(bool collapseNested) const;
    std::string targetDir() const;
    int writeTypes() const;
    void showErrors(const QString& title, const QString& summary,
                    const QStringList& errors);
    void showRc(const QString& title, int rc);

    static std::string toIso(const QString& s);
    static QString fromIso(const std::string& s);
    static std::string toFs(const QString& path);

    IsoSession session_;
    IsoTreeWidget* tree_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QMenu* contextMenu_ = nullptr;
    bool dirty_ = false;
    bool busy_ = false;
    QString lastIsoDir_;
    QString lastAddDir_;
    QString lastExtractDir_;

    QAction* actNew_ = nullptr;
    QAction* actOpen_ = nullptr;
    QAction* actSaveAs_ = nullptr;
    QAction* actClose_ = nullptr;
    QAction* actQuit_ = nullptr;
    QAction* actAddFiles_ = nullptr;
    QAction* actAddFolder_ = nullptr;
    QAction* actNewFolder_ = nullptr;
    QAction* actRename_ = nullptr;
    QAction* actDelete_ = nullptr;
    QAction* actExtract_ = nullptr;
    QAction* actPermissions_ = nullptr;
    QAction* actProperties_ = nullptr;
    QAction* actUseBootFile_ = nullptr;
    QAction* actAddBoot_ = nullptr;
    QAction* actExtractBoot_ = nullptr;
    QAction* actRemoveBoot_ = nullptr;
    QAction* actFollowSymlinks_ = nullptr;
    QAction* actKeepPermissions_ = nullptr;
    QAction* actScanDuplicates_ = nullptr;
    QAction* actWriteRockRidge_ = nullptr;
    QAction* actWriteJoliet_ = nullptr;
    QAction* actExpandAll_ = nullptr;
    QAction* actCollapseAll_ = nullptr;
    QAction* actAbout_ = nullptr;
};

#endif
