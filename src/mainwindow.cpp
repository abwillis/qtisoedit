/*
 * mainwindow.cpp
 */
#include "mainwindow.h"
#include "dialogs.h"
#include "isotreewidget.h"
#include "operationrunner.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidgetItemIterator>

#include <ctime>

namespace
{
enum Column { ColName = 0, ColSize, ColType, ColPerms, ColTarget };

QString permString(unsigned mode)
{
    QString s;
    if (IS_DIR(mode))
        s += QLatin1Char('d');
    else if (IS_SYMLINK(mode))
        s += QLatin1Char('l');
    else
        s += QLatin1Char('-');
    const char rwx[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        s += (mode & (0400u >> i)) ? QLatin1Char(rwx[i]) : QLatin1Char('-');
    return s;
}

void alignRight(QTreeWidgetItem* item, int column)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
#else
    item->setTextAlignment(column, static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
#endif
}

QString errText(int rc)
{
    return QString::fromStdString(IsoSession::errorString(rc));
}
} // namespace

/* ------------------------------------------------------------ encodings */

std::string MainWindow::toIso(const QString& s)
{
    return s.toLocal8Bit().toStdString();
}

QString MainWindow::fromIso(const std::string& s)
{
    return QString::fromLocal8Bit(s.c_str());
}

std::string MainWindow::toFs(const QString& path)
{
    return QFile::encodeName(path).toStdString();
}

/* ---------------------------------------------------------- construction */

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    tree_ = new IsoTreeWidget(this);
    tree_->setHeaderLabels({ tr("Name"), tr("Size"), tr("Type"),
                             tr("Permissions"), tr("Link target") });
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->header()->resizeSection(ColName, 320);
    setCentralWidget(tree_);

    statusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statusLabel_, 1);

    createActions();
    createMenus();
    loadSettings();

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateActions);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(tree_, &IsoTreeWidget::filesDropped, this, &MainWindow::onFilesDropped);

    updateTitle();
    updateStatus();
    updateActions();
}

MainWindow::~MainWindow() = default;

void MainWindow::createActions()
{
    auto make = [this](const QString& text, const QKeySequence& key,
                       const QString& themeIcon, QStyle::StandardPixmap fallback) {
        auto* a = new QAction(text, this);
        if (!key.isEmpty())
            a->setShortcut(key);
        a->setIcon(QIcon::fromTheme(themeIcon, style()->standardIcon(fallback)));
        return a;
    };
    auto check = [this](const QString& text, bool on) {
        auto* a = new QAction(text, this);
        a->setCheckable(true);
        a->setChecked(on);
        return a;
    };

    actNew_ = make(tr("&New Image"), QKeySequence::New, QStringLiteral("document-new"),
                   QStyle::SP_FileIcon);
    actOpen_ = make(tr("&Open Image…"), QKeySequence::Open, QStringLiteral("document-open"),
                    QStyle::SP_DialogOpenButton);
    actSaveAs_ = make(tr("Save Image &As…"), QKeySequence::SaveAs,
                      QStringLiteral("document-save-as"), QStyle::SP_DialogSaveButton);
    actClose_ = make(tr("&Close Image"), QKeySequence::Close, QStringLiteral("document-close"),
                     QStyle::SP_DialogCloseButton);
    actQuit_ = make(tr("&Quit"), QKeySequence::Quit, QStringLiteral("application-exit"),
                    QStyle::SP_DialogCloseButton);

    actAddFiles_ = make(tr("Add &Files…"), QKeySequence(tr("Ctrl+I")),
                        QStringLiteral("list-add"), QStyle::SP_FileIcon);
    actAddFolder_ = make(tr("Add F&older…"), QKeySequence(tr("Ctrl+Shift+I")),
                         QStringLiteral("folder-add"), QStyle::SP_DirIcon);
    actNewFolder_ = make(tr("&New Folder…"), QKeySequence(tr("Ctrl+Shift+N")),
                         QStringLiteral("folder-new"), QStyle::SP_FileDialogNewFolder);
    actRename_ = make(tr("&Rename…"), QKeySequence(Qt::Key_F2),
                      QStringLiteral("edit-rename"), QStyle::SP_FileDialogDetailedView);
    actDelete_ = make(tr("&Delete"), QKeySequence::Delete, QStringLiteral("edit-delete"),
                      QStyle::SP_TrashIcon);
    actExtract_ = make(tr("E&xtract…"), QKeySequence(tr("Ctrl+E")),
                       QStringLiteral("archive-extract"), QStyle::SP_DialogSaveButton);
    actPermissions_ = make(tr("&Permissions…"), QKeySequence(),
                           QStringLiteral("document-properties"),
                           QStyle::SP_FileDialogInfoView);

    actProperties_ = make(tr("Image &Properties…"), QKeySequence(tr("Alt+Return")),
                          QStringLiteral("document-properties"), QStyle::SP_DriveCDIcon);
    actUseBootFile_ = make(tr("&Use Selected File as Boot Record"), QKeySequence(),
                           QStringLiteral("media-optical"), QStyle::SP_DriveCDIcon);
    actAddBoot_ = make(tr("&Add Boot Record from File…"), QKeySequence(),
                       QStringLiteral("media-optical"), QStyle::SP_DriveCDIcon);
    actExtractBoot_ = make(tr("&Extract Boot Record…"), QKeySequence(),
                           QStringLiteral("document-save"), QStyle::SP_DialogSaveButton);
    actRemoveBoot_ = make(tr("&Remove Boot Record"), QKeySequence(),
                          QStringLiteral("edit-delete"), QStyle::SP_TrashIcon);

    actExpandAll_ = new QAction(tr("E&xpand All"), this);
    actCollapseAll_ = new QAction(tr("&Collapse All"), this);
    actAbout_ = new QAction(tr("&About bkiso-qt"), this);

    actFollowSymlinks_ = check(tr("&Follow Symbolic Links When Adding"), false);
    actFollowSymlinks_->setToolTip(tr("Add the files that links point to instead of the links"));
    actKeepPermissions_ = check(tr("&Keep Permissions When Extracting"), true);
    actScanDuplicates_ = check(tr("Store &Identical Files Once"), true);
    actScanDuplicates_->setToolTip(tr("Takes effect on the next New or Open"));
    actWriteRockRidge_ = check(tr("Write &Rock Ridge (long names, permissions, links)"), true);
    actWriteJoliet_ = check(tr("Write &Joliet (Windows long names)"), true);

    connect(actNew_, &QAction::triggered, this, &MainWindow::newImage);
    connect(actOpen_, &QAction::triggered, this, &MainWindow::openImageDialog);
    connect(actSaveAs_, &QAction::triggered, this, &MainWindow::saveAs);
    connect(actClose_, &QAction::triggered, this, &MainWindow::closeImage);
    connect(actQuit_, &QAction::triggered, this, &QWidget::close);
    connect(actAddFiles_, &QAction::triggered, this, &MainWindow::addFiles);
    connect(actAddFolder_, &QAction::triggered, this, &MainWindow::addFolder);
    connect(actNewFolder_, &QAction::triggered, this, &MainWindow::newFolder);
    connect(actRename_, &QAction::triggered, this, &MainWindow::renameItem);
    connect(actDelete_, &QAction::triggered, this, &MainWindow::deleteItems);
    connect(actExtract_, &QAction::triggered, this, &MainWindow::extractItems);
    connect(actPermissions_, &QAction::triggered, this, &MainWindow::editPermissions);
    connect(actProperties_, &QAction::triggered, this, &MainWindow::showProperties);
    connect(actUseBootFile_, &QAction::triggered, this, &MainWindow::useAsBootFile);
    connect(actAddBoot_, &QAction::triggered, this, &MainWindow::addBootRecord);
    connect(actExtractBoot_, &QAction::triggered, this, &MainWindow::extractBootRecord);
    connect(actRemoveBoot_, &QAction::triggered, this, &MainWindow::removeBootRecord);
    connect(actExpandAll_, &QAction::triggered, tree_, &QTreeWidget::expandAll);
    connect(actCollapseAll_, &QAction::triggered, tree_, &QTreeWidget::collapseAll);
    connect(actAbout_, &QAction::triggered, this, &MainWindow::about);

    connect(actFollowSymlinks_, &QAction::toggled, this,
            [this](bool on) { session_.setFollowSymlinks(on); });
    connect(actScanDuplicates_, &QAction::toggled, this,
            [this](bool on) { session_.setScanForDuplicates(on); });
    connect(actWriteRockRidge_, &QAction::toggled, this, &MainWindow::updateStatus);
    connect(actWriteJoliet_, &QAction::toggled, this, &MainWindow::updateStatus);
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(actNew_);
    file->addAction(actOpen_);
    file->addAction(actSaveAs_);
    file->addAction(actClose_);
    file->addSeparator();
    file->addAction(actQuit_);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(actAddFiles_);
    edit->addAction(actAddFolder_);
    edit->addAction(actNewFolder_);
    edit->addSeparator();
    edit->addAction(actRename_);
    edit->addAction(actDelete_);
    edit->addAction(actPermissions_);
    edit->addSeparator();
    edit->addAction(actExtract_);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(actExpandAll_);
    view->addAction(actCollapseAll_);

    QMenu* image = menuBar()->addMenu(tr("&Image"));
    image->addAction(actProperties_);
    QMenu* boot = image->addMenu(tr("&Boot Record"));
    boot->addAction(actUseBootFile_);
    boot->addAction(actAddBoot_);
    boot->addAction(actExtractBoot_);
    boot->addAction(actRemoveBoot_);

    QMenu* options = menuBar()->addMenu(tr("&Options"));
    options->addAction(actFollowSymlinks_);
    options->addAction(actKeepPermissions_);
    options->addAction(actScanDuplicates_);
    options->addSeparator();
    options->addAction(actWriteRockRidge_);
    options->addAction(actWriteJoliet_);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(actAbout_);

    QToolBar* bar = addToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->addAction(actNew_);
    bar->addAction(actOpen_);
    bar->addAction(actSaveAs_);
    bar->addSeparator();
    bar->addAction(actAddFiles_);
    bar->addAction(actAddFolder_);
    bar->addAction(actNewFolder_);
    bar->addAction(actDelete_);
    bar->addAction(actExtract_);
    bar->addSeparator();
    bar->addAction(actProperties_);

    contextMenu_ = new QMenu(this);
    contextMenu_->addAction(actAddFiles_);
    contextMenu_->addAction(actAddFolder_);
    contextMenu_->addAction(actNewFolder_);
    contextMenu_->addSeparator();
    contextMenu_->addAction(actRename_);
    contextMenu_->addAction(actDelete_);
    contextMenu_->addAction(actPermissions_);
    contextMenu_->addAction(actExtract_);
    contextMenu_->addSeparator();
    contextMenu_->addAction(actUseBootFile_);
    contextMenu_->addSeparator();
    contextMenu_->addAction(actExpandAll_);
    contextMenu_->addAction(actCollapseAll_);
}

void MainWindow::loadSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    tree_->header()->restoreState(s.value(QStringLiteral("window/header")).toByteArray());
    if (!s.contains(QStringLiteral("window/geometry")))
        resize(960, 620);

    actFollowSymlinks_->setChecked(s.value(QStringLiteral("options/followSymlinks"), false).toBool());
    actKeepPermissions_->setChecked(s.value(QStringLiteral("options/keepPermissions"), true).toBool());
    actScanDuplicates_->setChecked(s.value(QStringLiteral("options/scanDuplicates"), true).toBool());
    actWriteRockRidge_->setChecked(s.value(QStringLiteral("options/writeRockRidge"), true).toBool());
    actWriteJoliet_->setChecked(s.value(QStringLiteral("options/writeJoliet"), true).toBool());

    lastIsoDir_ = s.value(QStringLiteral("dirs/iso"), QDir::homePath()).toString();
    lastAddDir_ = s.value(QStringLiteral("dirs/add"), QDir::homePath()).toString();
    lastExtractDir_ = s.value(QStringLiteral("dirs/extract"), QDir::homePath()).toString();

    session_.setFollowSymlinks(actFollowSymlinks_->isChecked());
    session_.setScanForDuplicates(actScanDuplicates_->isChecked());
}

void MainWindow::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    s.setValue(QStringLiteral("window/header"), tree_->header()->saveState());
    s.setValue(QStringLiteral("options/followSymlinks"), actFollowSymlinks_->isChecked());
    s.setValue(QStringLiteral("options/keepPermissions"), actKeepPermissions_->isChecked());
    s.setValue(QStringLiteral("options/scanDuplicates"), actScanDuplicates_->isChecked());
    s.setValue(QStringLiteral("options/writeRockRidge"), actWriteRockRidge_->isChecked());
    s.setValue(QStringLiteral("options/writeJoliet"), actWriteJoliet_->isChecked());
    s.setValue(QStringLiteral("dirs/iso"), lastIsoDir_);
    s.setValue(QStringLiteral("dirs/add"), lastAddDir_);
    s.setValue(QStringLiteral("dirs/extract"), lastExtractDir_);
}

/* ------------------------------------------------------------- state */

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (busy_ || !maybeSave()) {
        event->ignore();
        return;
    }
    saveSettings();
    event->accept();
}

bool MainWindow::maybeSave()
{
    if (!dirty_ || !session_.isOpen())
        return true;

    const auto answer = QMessageBox::warning(
        this, tr("Unsaved Changes"),
        tr("The image has been modified. Save it to a new file first?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Save)
        return saveAs();
    return answer == QMessageBox::Discard;
}

void MainWindow::setDirty(bool dirty)
{
    dirty_ = dirty;
    updateTitle();
}

void MainWindow::updateTitle()
{
    if (!session_.isOpen()) {
        setWindowTitle(QStringLiteral("bkiso-qt"));
        setWindowModified(false);
        return;
    }
    const QString name = session_.sourcePath().empty()
        ? tr("Untitled")
        : QFileInfo(QFile::decodeName(QByteArray::fromStdString(session_.sourcePath()))).fileName();
    setWindowTitle(tr("%1[*] — bkiso-qt").arg(name));
    setWindowModified(dirty_);
}

int MainWindow::writeTypes() const
{
    int types = FNTYPE_9660;
    if (actWriteRockRidge_->isChecked())
        types |= FNTYPE_ROCKRIDGE;
    if (actWriteJoliet_->isChecked())
        types |= FNTYPE_JOLIET;
    return types;
}

void MainWindow::updateStatus()
{
    if (!session_.isOpen()) {
        statusLabel_->setText(tr("No image open. Use File ▸ New Image or File ▸ Open Image, "
                                 "or drop files here to start a new image."));
        return;
    }

    QString boot = QString::fromStdString(IsoSession::bootMediaName(session_.bootMediaType()));
    const std::string bootFile = session_.bootFilePath();
    if (!bootFile.empty())
        boot += QStringLiteral(" (%1)").arg(fromIso(bootFile));

    QStringList parts;
    parts << tr("Volume: %1").arg(fromIso(session_.volumeName()));
    parts << tr("%1 items").arg(QLocale().toString(session_.countItems()));
    parts << tr("≈ %1").arg(QLocale().formattedDataSize(session_.estimatedSize(writeTypes())));
    parts << tr("Boot: %1").arg(boot);
    if (session_.loadedNamespace())
        parts << tr("Read as %1").arg(namespacesText(
                     static_cast<unsigned>(session_.loadedNamespace())));
    statusLabel_->setText(parts.join(QStringLiteral("  ·  ")));
}

void MainWindow::updateActions()
{
    const bool open = session_.isOpen() && !busy_;
    const std::vector<IsoItem*> sel = selectedItems(false);
    bool anyNonRoot = false;
    for (IsoItem* it : sel)
        anyNonRoot = anyNonRoot || !it->isRoot();
    const bool single = sel.size() == 1;
    const bool singleNonRoot = single && !sel.front()->isRoot();
    const bool singleFile = single && sel.front()->kind() == IsoItem::File;
    const bool hasBoot = session_.isOpen() && session_.bootMediaType() != BOOT_MEDIA_NONE;

    actNew_->setEnabled(!busy_);
    actOpen_->setEnabled(!busy_);
    actSaveAs_->setEnabled(open);
    actClose_->setEnabled(open);
    actAddFiles_->setEnabled(open);
    actAddFolder_->setEnabled(open);
    actNewFolder_->setEnabled(open);
    actRename_->setEnabled(open && singleNonRoot);
    actDelete_->setEnabled(open && anyNonRoot);
    actExtract_->setEnabled(open && !sel.empty());
    actPermissions_->setEnabled(open && anyNonRoot);
    actProperties_->setEnabled(open);
    actUseBootFile_->setEnabled(open && singleFile);
    actAddBoot_->setEnabled(open);
    actExtractBoot_->setEnabled(open && hasBoot);
    actRemoveBoot_->setEnabled(open && hasBoot);
    actExpandAll_->setEnabled(open);
    actCollapseAll_->setEnabled(open);
}

/* ---------------------------------------------------------------- tree */

void MainWindow::populate(IsoItem* parent, const std::string& dirPath,
                          const std::string& bootPath)
{
    std::vector<IsoEntry> entries;
    if (IsoSession::failed(session_.list(dirPath, entries)))
        return;

    for (const IsoEntry& e : entries) {
        const std::string path = IsoSession::joinPath(dirPath, e.name);
        IsoItem::Kind kind = IsoItem::Other;
        switch (e.kind) {
        case IsoEntry::Directory: kind = IsoItem::Directory; break;
        case IsoEntry::File:      kind = IsoItem::File; break;
        case IsoEntry::Symlink:   kind = IsoItem::Symlink; break;
        case IsoEntry::Other:     kind = IsoItem::Other; break;
        }

        auto* item = new IsoItem(parent, path, kind, e.size);
        item->setText(ColName, fromIso(e.name));
        item->setText(ColPerms, permString(e.mode));

        if (kind == IsoItem::Directory) {
            item->setIcon(ColName, style()->standardIcon(QStyle::SP_DirIcon));
            item->setText(ColType, tr("Folder"));
            populate(item, path, bootPath);
        } else if (kind == IsoItem::File) {
            item->setIcon(ColName, style()->standardIcon(QStyle::SP_FileIcon));
            item->setText(ColSize, QLocale().formattedDataSize(static_cast<qint64>(e.size)));
            item->setToolTip(ColSize, tr("%1 bytes").arg(QLocale().toString(e.size)));
            alignRight(item, ColSize);
            item->setText(ColType, e.onImage ? tr("File") : tr("File (added)"));
        } else if (kind == IsoItem::Symlink) {
            item->setIcon(ColName, style()->standardIcon(QStyle::SP_FileLinkIcon));
            item->setText(ColType, tr("Symbolic link"));
            item->setText(ColTarget, fromIso(e.symlinkTarget));
        } else {
            item->setText(ColType, tr("Other"));
        }

        if (path == bootPath) {
            QFont f = item->font(ColName);
            f.setBold(true);
            item->setFont(ColName, f);
            item->setText(ColType, item->text(ColType) + tr(" — boot record"));
        }
    }
}

void MainWindow::rebuildTree(const std::string& selectPath)
{
    std::set<std::string> expanded;
    std::set<std::string> selected;
    std::string current;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        auto* item = static_cast<IsoItem*>(*it);
        if (item->isExpanded())
            expanded.insert(item->isoPath());
        if (item->isSelected())
            selected.insert(item->isoPath());
    }
    if (IsoItem* cur = currentIsoItem())
        current = cur->isoPath();
    const bool firstBuild = tree_->topLevelItemCount() == 0;

    tree_->blockSignals(true);
    tree_->setUpdatesEnabled(false);
    tree_->setSortingEnabled(false);
    tree_->clear();

    if (session_.isOpen()) {
        auto* root = new IsoItem(tree_, "/", IsoItem::Directory);
        const QString vol = fromIso(session_.volumeName());
        root->setText(ColName, vol.isEmpty() ? QStringLiteral("/") : vol);
        root->setIcon(ColName, style()->standardIcon(QStyle::SP_DriveCDIcon));
        root->setText(ColType, tr("Image root"));
        populate(root, "/", session_.bootFilePath());
        tree_->setSortingEnabled(true);

        IsoItem* toSelect = nullptr;
        IsoItem* toCurrent = nullptr;
        for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
            auto* item = static_cast<IsoItem*>(*it);
            if (expanded.count(item->isoPath()))
                item->setExpanded(true);
            if (selectPath.empty() && selected.count(item->isoPath()))
                item->setSelected(true);
            if (item->isoPath() == current)
                toCurrent = item;
            if (!selectPath.empty() && item->isoPath() == selectPath)
                toSelect = item;
        }
        root->setExpanded(true);

        if (toSelect) {
            for (QTreeWidgetItem* p = toSelect->parent(); p; p = p->parent())
                p->setExpanded(true);
            tree_->setCurrentItem(toSelect, 0, QItemSelectionModel::ClearAndSelect);
            tree_->scrollToItem(toSelect);
        } else if (toCurrent) {
            tree_->setCurrentItem(toCurrent, 0, QItemSelectionModel::NoUpdate);
        }

        if (firstBuild) {
            tree_->resizeColumnToContents(ColSize);
            tree_->resizeColumnToContents(ColType);
            tree_->resizeColumnToContents(ColPerms);
        }
    } else {
        tree_->setSortingEnabled(true);
    }

    tree_->setUpdatesEnabled(true);
    tree_->blockSignals(false);
    updateTitle();
    updateStatus();
    updateActions();
}

IsoItem* MainWindow::currentIsoItem() const
{
    return dynamic_cast<IsoItem*>(tree_->currentItem());
}

std::vector<IsoItem*> MainWindow::selectedItems(bool collapseNested) const
{
    std::vector<IsoItem*> out;
    const QList<QTreeWidgetItem*> sel = tree_->selectedItems();
    for (QTreeWidgetItem* i : sel)
        if (auto* item = dynamic_cast<IsoItem*>(i))
            out.push_back(item);

    if (collapseNested) {
        /* drop items whose ancestor is also selected */
        std::vector<IsoItem*> top;
        for (IsoItem* item : out) {
            bool nested = false;
            for (QTreeWidgetItem* p = item->parent(); p && !nested; p = p->parent())
                nested = p->isSelected();
            if (!nested)
                top.push_back(item);
        }
        out.swap(top);
    }
    return out;
}

std::string MainWindow::targetDir() const
{
    IsoItem* cur = currentIsoItem();
    if (!cur)
        return "/";
    return cur->isDir() ? cur->isoPath() : IsoSession::parentPath(cur->isoPath());
}

void MainWindow::showContextMenu(const QPoint& pos)
{
    if (!session_.isOpen() || busy_)
        return;
    if (QTreeWidgetItem* item = tree_->itemAt(pos)) {
        if (!item->isSelected())
            tree_->setCurrentItem(item, 0, QItemSelectionModel::ClearAndSelect);
    }
    updateActions();
    contextMenu_->exec(tree_->viewport()->mapToGlobal(pos));
}

void MainWindow::showErrors(const QString& title, const QString& summary,
                            const QStringList& errors)
{
    QMessageBox box(QMessageBox::Warning, title, summary, QMessageBox::Ok, this);
    if (errors.size() <= 5)
        box.setInformativeText(errors.join(QLatin1Char('\n')));
    else
        box.setDetailedText(errors.join(QLatin1Char('\n')));
    box.exec();
}

void MainWindow::showRc(const QString& title, int rc)
{
    QMessageBox::warning(this, title, errText(rc));
}

/* ------------------------------------------------------------ file menu */

void MainWindow::newImage()
{
    if (!maybeSave())
        return;
    const int rc = session_.newImage();
    if (IsoSession::failed(rc))
        showRc(tr("New Image"), rc);
    setDirty(false);
    rebuildTree("/");
}

void MainWindow::openImageDialog()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Image"), lastIsoDir_,
        tr("Disc images (*.iso *.ISO *.nrg *.NRG);;All files (*)"));
    if (!path.isEmpty())
        openImage(path);
}

bool MainWindow::openImage(const QString& path)
{
    const std::string fsPath = toFs(path);

    session_.close();
    setDirty(false);
    rebuildTree();

    busy_ = true;
    updateActions();
    OperationRunner runner(session_, this);
    const int rc = runner.run(tr("Open Image"),
                              tr("Reading %1…").arg(QFileInfo(path).fileName()),
                              false, [this, fsPath] { return session_.open(fsPath); });
    busy_ = false;

    if (IsoSession::failed(rc)) {
        rebuildTree();
        if (rc == BKERROR_OPER_CANCELED_BY_USER)
            statusBar()->showMessage(tr("Opening cancelled"), 5000);
        else
            QMessageBox::warning(this, tr("Open Image"),
                                 tr("Could not open %1:\n%2").arg(path, errText(rc)));
        return false;
    }

    lastIsoDir_ = QFileInfo(path).absolutePath();
    setDirty(false);
    rebuildTree("/");
    statusBar()->showMessage(tr("Opened %1").arg(path), 5000);
    return true;
}

bool MainWindow::saveAs()
{
    if (!session_.isOpen())
        return false;

    QString suggestion = QDir(lastIsoDir_).filePath(QStringLiteral("image.iso"));
    if (!session_.sourcePath().empty()) {
        const QFileInfo src(QFile::decodeName(QByteArray::fromStdString(session_.sourcePath())));
        suggestion = QDir(lastIsoDir_).filePath(src.completeBaseName() + tr("-edited") +
                                               QStringLiteral(".iso"));
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Save Image As"), suggestion,
                                                tr("ISO images (*.iso);;All files (*)"));
    if (path.isEmpty())
        return false;

    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".iso");
        if (QFileInfo::exists(path) &&
            QMessageBox::question(this, tr("Save Image As"),
                                  tr("%1 already exists. Replace it?").arg(path))
                != QMessageBox::Yes)
            return false;
    }

    /* bkisofs reads unchanged files from the open image while writing, so
     * it refuses to write over it (BKERROR_SAVE_OVERWRITE); catch it early */
    if (!session_.sourcePath().empty()) {
        const QFileInfo out(path);
        const QFileInfo in(QFile::decodeName(QByteArray::fromStdString(session_.sourcePath())));
        if (out.exists() && out.canonicalFilePath() == in.canonicalFilePath()) {
            QMessageBox::warning(this, tr("Save Image As"),
                                 tr("The image being edited cannot be overwritten. "
                                    "Choose a different file name."));
            return false;
        }
    }

    const std::string outPath = toFs(path);
    const int types = writeTypes();

    busy_ = true;
    updateActions();
    OperationRunner runner(session_, this);
    const int rc = runner.run(tr("Save Image"),
                              tr("Writing %1…").arg(QFileInfo(path).fileName()), true,
                              [this, outPath, types] {
                                  return session_.write(outPath, types, std::time(nullptr));
                              });
    busy_ = false;
    updateActions();

    if (IsoSession::failed(rc)) {
        if (rc == BKERROR_OPER_CANCELED_BY_USER)
            statusBar()->showMessage(tr("Save cancelled"), 5000);
        else
            QMessageBox::warning(this, tr("Save Image"),
                                 tr("Could not write %1:\n%2").arg(path, errText(rc)));
        return false;
    }

    lastIsoDir_ = QFileInfo(path).absolutePath();
    setDirty(false);
    statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    return true;
}

void MainWindow::closeImage()
{
    if (!maybeSave())
        return;
    session_.close();
    setDirty(false);
    rebuildTree();
}

/* ------------------------------------------------------------ edit menu */

void MainWindow::addFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Files"), lastAddDir_);
    if (files.isEmpty())
        return;
    lastAddDir_ = QFileInfo(files.first()).absolutePath();
    addPaths(files, targetDir());
}

void MainWindow::addFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder"), lastAddDir_);
    if (dir.isEmpty())
        return;
    lastAddDir_ = QFileInfo(dir).absolutePath();
    addPaths({ dir }, targetDir());
}

void MainWindow::onFilesDropped(const QStringList& paths, IsoItem* target)
{
    if (busy_)
        return;
    if (!session_.isOpen()) {
        newImage();
        if (!session_.isOpen())
            return;
        target = nullptr;
    }
    std::string dest = "/";
    if (target)
        dest = target->isDir() ? target->isoPath() : IsoSession::parentPath(target->isoPath());
    addPaths(paths, dest);
}

void MainWindow::addPaths(const QStringList& paths, const std::string& destDir)
{
    struct Job
    {
        std::string src;
        QString display;
        std::string replacePath; /* delete this first */
        std::string newName;     /* add under a different name */
    };

    auto uniqueName = [this, &destDir](const QString& name) {
        const QFileInfo fi(name);
        const QString stem = fi.completeBaseName().isEmpty() ? name : fi.completeBaseName();
        const QString ext = fi.completeBaseName().isEmpty() || fi.suffix().isEmpty()
            ? QString() : QStringLiteral(".") + fi.suffix();
        for (int n = 2;; ++n) {
            const QString candidate = QStringLiteral("%1_%2%3").arg(stem, QString::number(n), ext);
            if (!session_.exists(destDir, toIso(candidate)))
                return candidate;
        }
    };

    std::vector<Job> jobs;
    bool replaceAll = false;
    bool skipAll = false;

    for (const QString& raw : paths) {
        const QString path = QDir::cleanPath(raw);
        const QString name = QFileInfo(path).fileName();
        if (name.isEmpty())
            continue; /* "/" itself */

        Job job{ toFs(path), name, std::string(), std::string() };
        const std::string isoName = toIso(name);

        if (session_.exists(destDir, isoName)) {
            if (skipAll)
                continue;
            if (!replaceAll) {
                QMessageBox box(QMessageBox::Question, tr("Item Exists"),
                                tr("“%1” already exists in %2.").arg(name, fromIso(destDir)),
                                QMessageBox::NoButton, this);
                QPushButton* replace = box.addButton(tr("Replace"), QMessageBox::AcceptRole);
                QPushButton* replaceAllBtn = box.addButton(tr("Replace All"), QMessageBox::AcceptRole);
                QPushButton* rename = box.addButton(tr("Add with New Name…"), QMessageBox::ActionRole);
                QPushButton* skip = box.addButton(tr("Skip"), QMessageBox::RejectRole);
                QPushButton* skipAllBtn = box.addButton(tr("Skip All"), QMessageBox::RejectRole);
                QPushButton* cancel = box.addButton(QMessageBox::Cancel);
                box.setDefaultButton(skip);
                box.setEscapeButton(cancel);
                box.exec();

                QAbstractButton* clicked = box.clickedButton();
                if (clicked == cancel || clicked == nullptr)
                    return;
                if (clicked == skip)
                    continue;
                if (clicked == skipAllBtn) {
                    skipAll = true;
                    continue;
                }
                if (clicked == replaceAllBtn)
                    replaceAll = true;
                if (clicked == rename) {
                    bool ok = false;
                    const QString newName = QInputDialog::getText(
                        this, tr("Add with New Name"), tr("Name in the image:"),
                        QLineEdit::Normal, uniqueName(name), &ok);
                    if (!ok || newName.isEmpty())
                        continue;
                    if (session_.exists(destDir, toIso(newName))) {
                        QMessageBox::warning(this, tr("Add with New Name"),
                                             tr("“%1” also exists; skipped.").arg(newName));
                        continue;
                    }
                    job.newName = toIso(newName);
                    jobs.push_back(job);
                    continue;
                }
                (void)replace;
            }
            job.replacePath = IsoSession::joinPath(destDir, isoName);
        }
        jobs.push_back(job);
    }

    if (jobs.empty())
        return;

    QStringList errors;
    int added = 0;
    busy_ = true;
    updateActions();
    OperationRunner runner(session_, this);
    const int rc = runner.run(tr("Add to Image"), tr("Adding…"), false, [&]() -> int {
        for (size_t i = 0; i < jobs.size(); ++i) {
            if (runner.cancelRequested())
                return BKERROR_OPER_CANCELED_BY_USER;
            const Job& job = jobs[i];
            runner.postText(tr("Adding %1 (%2 of %3)…")
                                .arg(job.display, QString::number(i + 1),
                                     QString::number(jobs.size())));

            if (!job.replacePath.empty()) {
                const int r = session_.remove(job.replacePath);
                if (IsoSession::failed(r)) {
                    errors << QStringLiteral("%1: %2").arg(job.display, errText(r));
                    continue;
                }
            }

            const int r = job.newName.empty()
                ? session_.add(job.src, destDir)
                : session_.addAs(job.src, destDir, job.newName);
            if (r == BKERROR_OPER_CANCELED_BY_USER)
                return r;
            if (r == BKWARNING_OPER_PARTLY_FAILED)
                errors << tr("%1: only partly added").arg(job.display);
            else if (IsoSession::failed(r))
                errors << QStringLiteral("%1: %2").arg(job.display, errText(r));
            else
                ++added;
        }
        return 1;
    });
    busy_ = false;

    setDirty(true);
    std::string select;
    if (jobs.size() == 1)
        select = IsoSession::joinPath(destDir, jobs.front().newName.empty()
                                                   ? toIso(jobs.front().display)
                                                   : jobs.front().newName);
    rebuildTree(select);

    if (rc == BKERROR_OPER_CANCELED_BY_USER)
        statusBar()->showMessage(tr("Adding cancelled after %n item(s)", nullptr, added), 5000);
    else
        statusBar()->showMessage(tr("Added %n item(s)", nullptr, added), 5000);
    if (!errors.isEmpty())
        showErrors(tr("Add to Image"), tr("Some items could not be added."), errors);
}

void MainWindow::newFolder()
{
    const std::string dest = targetDir();
    QString suggestion = tr("New Folder");
    for (int n = 2; session_.exists(dest, toIso(suggestion)); ++n)
        suggestion = tr("New Folder %1").arg(n);

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"),
                                               tr("Folder name in %1:").arg(fromIso(dest)),
                                               QLineEdit::Normal, suggestion, &ok);
    if (!ok || name.isEmpty())
        return;

    const int rc = session_.createDir(dest, toIso(name));
    if (IsoSession::failed(rc)) {
        showRc(tr("New Folder"), rc);
        return;
    }
    setDirty(true);
    rebuildTree(IsoSession::joinPath(dest, toIso(name)));
}

void MainWindow::renameItem()
{
    IsoItem* item = currentIsoItem();
    if (!item || item->isRoot())
        return;

    bool ok = false;
    const QString oldName = item->text(ColName);
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                               QLineEdit::Normal, oldName, &ok);
    if (!ok || name.isEmpty() || name == oldName)
        return;

    const std::string path = item->isoPath();
    const int rc = session_.rename(path, toIso(name));
    if (IsoSession::failed(rc)) {
        showRc(tr("Rename"), rc);
        return;
    }
    setDirty(true);
    rebuildTree(IsoSession::joinPath(IsoSession::parentPath(path), toIso(name)));
}

void MainWindow::deleteItems()
{
    std::vector<std::string> paths;
    QStringList names;
    for (IsoItem* item : selectedItems(true)) {
        if (item->isRoot())
            continue;
        paths.push_back(item->isoPath());
        names << item->text(ColName);
    }
    if (paths.empty())
        return;

    QString question = paths.size() == 1
        ? tr("Delete “%1” from the image?").arg(names.first())
        : tr("Delete %1 items from the image?").arg(paths.size());
    if (QMessageBox::question(this, tr("Delete"), question) != QMessageBox::Yes)
        return;

    const int bootBefore = session_.bootMediaType();
    QStringList errors;
    for (size_t i = 0; i < paths.size(); ++i) {
        const int rc = session_.remove(paths[i]);
        if (IsoSession::failed(rc))
            errors << QStringLiteral("%1: %2").arg(names[static_cast<int>(i)], errText(rc));
    }

    setDirty(true);
    rebuildTree();
    if (bootBefore != BOOT_MEDIA_NONE && session_.bootMediaType() == BOOT_MEDIA_NONE)
        statusBar()->showMessage(tr("The boot record was removed because its file was deleted"),
                                 8000);
    if (!errors.isEmpty())
        showErrors(tr("Delete"), tr("Some items could not be deleted."), errors);
}

void MainWindow::extractItems()
{
    /* bkisofs cannot extract the root itself; extract its children instead */
    std::vector<std::string> sources;
    for (IsoItem* item : selectedItems(true)) {
        if (item->isRoot()) {
            std::vector<IsoEntry> entries;
            session_.list("/", entries);
            sources.clear();
            for (const IsoEntry& e : entries)
                sources.push_back(IsoSession::joinPath("/", e.name));
            break;
        }
        sources.push_back(item->isoPath());
    }
    if (sources.empty())
        return;

    const QString dest = QFileDialog::getExistingDirectory(this, tr("Extract To"), lastExtractDir_);
    if (dest.isEmpty())
        return;
    lastExtractDir_ = dest;

    /* bkisofs never overwrites; skip existing names up front */
    std::vector<std::string> todo;
    QStringList existing;
    for (const std::string& src : sources) {
        const QString name = fromIso(IsoSession::baseName(src));
        const QFileInfo target(QDir(dest).filePath(name));
        if (target.exists() || target.isSymLink())
            existing << name;
        else
            todo.push_back(src);
    }
    if (!existing.isEmpty()) {
        QMessageBox box(QMessageBox::Question, tr("Extract"),
                        tr("%n item(s) already exist in %1 and will be skipped.", nullptr,
                           static_cast<int>(existing.size())).arg(dest),
                        QMessageBox::Ok | QMessageBox::Cancel, this);
        box.setDetailedText(existing.join(QLatin1Char('\n')));
        if (box.exec() != QMessageBox::Ok)
            return;
    }
    if (todo.empty())
        return;

    const std::string destFs = toFs(dest);
    const bool keep = actKeepPermissions_->isChecked();
    QStringList errors;
    int extracted = 0;

    busy_ = true;
    updateActions();
    OperationRunner runner(session_, this);
    const int rc = runner.run(tr("Extract"), tr("Extracting…"), false, [&]() -> int {
        for (size_t i = 0; i < todo.size(); ++i) {
            if (runner.cancelRequested())
                return BKERROR_OPER_CANCELED_BY_USER;
            const QString name = fromIso(IsoSession::baseName(todo[i]));
            runner.postText(tr("Extracting %1 (%2 of %3)…")
                                .arg(name, QString::number(i + 1),
                                     QString::number(todo.size())));
            const int r = session_.extract(todo[i], destFs, keep);
            if (r == BKERROR_OPER_CANCELED_BY_USER)
                return r;
            if (r == BKWARNING_OPER_PARTLY_FAILED)
                errors << tr("%1: only partly extracted").arg(name);
            else if (IsoSession::failed(r))
                errors << QStringLiteral("%1: %2").arg(name, errText(r));
            else
                ++extracted;
        }
        return 1;
    });
    busy_ = false;
    updateActions();

    if (rc == BKERROR_OPER_CANCELED_BY_USER)
        statusBar()->showMessage(tr("Extraction cancelled"), 5000);
    else
        statusBar()->showMessage(tr("Extracted %n item(s) to %1", nullptr, extracted).arg(dest),
                                 5000);
    if (!errors.isEmpty())
        showErrors(tr("Extract"), tr("Some items could not be extracted."), errors);
}

void MainWindow::editPermissions()
{
    /* bk_get/set_permissions() do not handle the root path */
    std::vector<IsoItem*> items;
    for (IsoItem* item : selectedItems(false))
        if (!item->isRoot())
            items.push_back(item);
    if (items.empty())
        return;

    mode_t initial = 0;
    const int rc = session_.permissions(items.front()->isoPath(), initial);
    if (IsoSession::failed(rc)) {
        showRc(tr("Permissions"), rc);
        return;
    }

    const QString label = items.size() == 1
        ? items.front()->text(ColName)
        : tr("%1 items").arg(items.size());
    PermissionsDialog dlg(initial, label, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QStringList errors;
    for (IsoItem* item : items) {
        const int r = session_.setPermissions(item->isoPath(), dlg.mode());
        if (IsoSession::failed(r))
            errors << QStringLiteral("%1: %2").arg(item->text(ColName), errText(r));
    }
    setDirty(true);
    rebuildTree();
    if (!actWriteRockRidge_->isChecked())
        statusBar()->showMessage(tr("Rock Ridge writing is off; permissions will not be saved"),
                                 8000);
    if (!errors.isEmpty())
        showErrors(tr("Permissions"), tr("Some permissions could not be changed."), errors);
}

/* ----------------------------------------------------------- image menu */

void MainWindow::showProperties()
{
    VolumePropertiesDialog dlg(session_, writeTypes(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString error;
    const bool changed = dlg.apply(&error);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Image Properties"), error);
    if (changed) {
        setDirty(true);
        rebuildTree();
    }
}

void MainWindow::useAsBootFile()
{
    IsoItem* item = currentIsoItem();
    if (!item || item->kind() != IsoItem::File)
        return;
    if (session_.bootMediaType() != BOOT_MEDIA_NONE &&
        QMessageBox::question(this, tr("Boot Record"),
                              tr("Replace the current boot record with “%1”?")
                                  .arg(item->text(ColName))) != QMessageBox::Yes)
        return;

    const std::string path = item->isoPath();
    const int rc = session_.setBootFile(path);
    if (IsoSession::failed(rc)) {
        showRc(tr("Boot Record"), rc);
        return;
    }
    setDirty(true);
    rebuildTree(path);
}

void MainWindow::addBootRecord()
{
    const QStringList types = {
        tr("No emulation (e.g. isolinux.bin)"),
        tr("1.2 MB floppy image (1,228,800 bytes)"),
        tr("1.44 MB floppy image (1,474,560 bytes)"),
        tr("2.88 MB floppy image (2,949,120 bytes)"),
    };
    const int media[] = { BOOT_MEDIA_NO_EMULATION, BOOT_MEDIA_1_2_FLOPPY,
                          BOOT_MEDIA_1_44_FLOPPY, BOOT_MEDIA_2_88_FLOPPY };

    bool ok = false;
    const QString choice = QInputDialog::getItem(this, tr("Add Boot Record"),
                                                 tr("Boot media type:"), types, 0, false, &ok);
    if (!ok)
        return;
    const int mediaType = media[types.indexOf(choice)];

    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select Boot Image"), lastAddDir_,
        tr("Boot images (*.img *.bin *.ima);;All files (*)"));
    if (file.isEmpty())
        return;

    if (session_.bootMediaType() != BOOT_MEDIA_NONE &&
        QMessageBox::question(this, tr("Add Boot Record"),
                              tr("Replace the current boot record?")) != QMessageBox::Yes)
        return;

    const int rc = session_.addBootRecord(toFs(file), mediaType);
    if (IsoSession::failed(rc)) {
        showRc(tr("Add Boot Record"), rc);
        return;
    }
    lastAddDir_ = QFileInfo(file).absolutePath();
    setDirty(true);
    rebuildTree();
    statusBar()->showMessage(tr("Boot record set (stored hidden, not as a visible file)"), 5000);
}

void MainWindow::extractBootRecord()
{
    const std::string bootFile = session_.bootFilePath();
    const QString name = bootFile.empty() ? QStringLiteral("boot.img")
                                          : fromIso(IsoSession::baseName(bootFile));
    const QString path = QFileDialog::getSaveFileName(this, tr("Extract Boot Record"),
                                                      QDir(lastExtractDir_).filePath(name));
    if (path.isEmpty())
        return;

    const int rc = session_.extractBootRecord(toFs(path), 0644);
    if (IsoSession::failed(rc)) {
        showRc(tr("Extract Boot Record"), rc);
        return;
    }
    lastExtractDir_ = QFileInfo(path).absolutePath();
    statusBar()->showMessage(tr("Boot record saved to %1").arg(path), 5000);
}

void MainWindow::removeBootRecord()
{
    if (QMessageBox::question(this, tr("Remove Boot Record"),
                              tr("Remove the boot record? The image will no longer be bootable. "
                                 "A visible boot file stays in the image."))
        != QMessageBox::Yes)
        return;
    session_.removeBootRecord();
    setDirty(true);
    rebuildTree();
}

void MainWindow::about()
{
    QMessageBox::about(
        this, tr("About bkiso-qt"),
        tr("<h3>bkiso-qt</h3>"
           "<p>A Qt editor for ISO 9660 images built on the <b>bkisofs</b> library "
           "(Rock Ridge, Joliet and El Torito support; NRG images are read-only).</p>"
           "<p>bkisofs is written by Andrew Smith and licensed under the GNU GPL "
           "version 2.</p>"
           "<p>Edited images are always written to a new file; bkisofs cannot "
           "overwrite the image it is reading from.</p>"
           "<p>Qt %1</p>").arg(QString::fromLatin1(qVersion())));
}
