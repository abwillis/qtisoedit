/*
 * dialogs.cpp
 * See dialogs.h.
 */
#include "dialogs.h"
#include "isosession.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QStringList>
#include <QVBoxLayout>

QString namespacesText(unsigned mask)
{
    QStringList parts;
    if (mask & FNTYPE_9660)
        parts << QObject::tr("ISO 9660");
    if (mask & FNTYPE_ROCKRIDGE)
        parts << QObject::tr("Rock Ridge");
    if (mask & FNTYPE_JOLIET)
        parts << QObject::tr("Joliet");
    return parts.isEmpty() ? QObject::tr("—") : parts.join(QStringLiteral(", "));
}

/* ------------------------------------------------- VolumePropertiesDialog */

VolumePropertiesDialog::VolumePropertiesDialog(IsoSession& session, int writeTypes,
                                               QWidget* parent)
    : QDialog(parent), session_(session)
{
    setWindowTitle(tr("Image Properties"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto readOnly = [this](const QString& text) {
        auto* label = new QLabel(text, this);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        return label;
    };

    volumeName_ = new QLineEdit(QString::fromLocal8Bit(session.volumeName().c_str()), this);
    volumeName_->setMaxLength(32);          /* VolInfo::volId[33] */
    publisher_ = new QLineEdit(QString::fromLocal8Bit(session.publisher().c_str()), this);
    publisher_->setMaxLength(128);          /* VolInfo::publisher[129] */

    const QString source = session.sourcePath().empty()
        ? tr("(new image)")
        : QString::fromLocal8Bit(session.sourcePath().c_str());

    QString created = tr("—");
    if (!session.sourcePath().empty() && session.creationTime() > 0)
        created = QLocale().toString(
            QDateTime::fromSecsSinceEpoch(static_cast<qint64>(session.creationTime())),
            QLocale::LongFormat);

    QString boot = QString::fromStdString(IsoSession::bootMediaName(session.bootMediaType()));
    if (session.bootMediaType() != BOOT_MEDIA_NONE) {
        boot += tr(", %1 bytes").arg(session.bootRecordSize());
        const std::string bootFile = session.bootFilePath();
        boot += bootFile.empty()
            ? tr(" (hidden boot record)")
            : tr(" (file %1)").arg(QString::fromLocal8Bit(bootFile.c_str()));
    }

    auto* form = new QFormLayout;
    form->addRow(tr("Volume name:"), volumeName_);
    form->addRow(tr("Publisher:"), publisher_);
    form->addRow(tr("Data preparer:"),
                 readOnly(QString::fromLocal8Bit(session.dataPreparer().c_str())));
    form->addRow(tr("Source file:"), readOnly(source));
    form->addRow(tr("Created:"), readOnly(created));
    form->addRow(tr("Source namespaces:"), readOnly(namespacesText(session.sourceNamespaces())));
    form->addRow(tr("Loaded using:"), readOnly(namespacesText(
                     static_cast<unsigned>(session.loadedNamespace()))));
    form->addRow(tr("Items:"), readOnly(QLocale().toString(session.countItems())));
    form->addRow(tr("Estimated size:"), readOnly(
                     QLocale().formattedDataSize(session.estimatedSize(writeTypes))));
    form->addRow(tr("Boot record:"), readOnly(boot));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    setMinimumWidth(480);
}

bool VolumePropertiesDialog::apply(QString* error)
{
    bool changed = false;
    const std::string name = volumeName_->text().toLocal8Bit().toStdString();
    const std::string pub = publisher_->text().toLocal8Bit().toStdString();

    if (name.size() > 32 || pub.size() > 128) {
        if (error)
            *error = tr("Volume name is limited to 32 bytes and publisher to 128 bytes.");
        return false;
    }

    if (name != session_.volumeName()) {
        const int rc = session_.setVolumeName(name);
        if (IsoSession::failed(rc)) {
            if (error)
                *error = QString::fromStdString(IsoSession::errorString(rc));
            return changed;
        }
        changed = true;
    }
    if (pub != session_.publisher()) {
        const int rc = session_.setPublisher(pub);
        if (IsoSession::failed(rc)) {
            if (error)
                *error = QString::fromStdString(IsoSession::errorString(rc));
            return changed;
        }
        changed = true;
    }
    return changed;
}

/* ------------------------------------------------------ PermissionsDialog */

PermissionsDialog::PermissionsDialog(mode_t initial, const QString& itemText,
                                     QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Permissions"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* grid = new QGridLayout;
    const QString who[3] = { tr("Owner"), tr("Group"), tr("Others") };
    const QString what[3] = { tr("Read"), tr("Write"), tr("Execute") };
    for (int c = 0; c < 3; ++c)
        grid->addWidget(new QLabel(what[c], this), 0, c + 1, Qt::AlignCenter);

    for (int r = 0; r < 3; ++r) {
        grid->addWidget(new QLabel(who[r], this), r + 1, 0);
        for (int c = 0; c < 3; ++c) {
            const int index = r * 3 + c;
            const mode_t bit = static_cast<mode_t>(0400 >> index);
            bits_[index] = new QCheckBox(this);
            bits_[index]->setChecked((initial & bit) != 0);
            connect(bits_[index], &QCheckBox::toggled, this, &PermissionsDialog::updateOctal);
            grid->addWidget(bits_[index], r + 1, c + 1, Qt::AlignCenter);
        }
    }

    auto* box = new QGroupBox(itemText, this);
    box->setLayout(grid);

    octal_ = new QLabel(this);
    updateOctal();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(box);
    layout->addWidget(octal_);
    layout->addWidget(new QLabel(tr("Permissions are stored in Rock Ridge records only."), this));
    layout->addWidget(buttons);
}

mode_t PermissionsDialog::mode() const
{
    mode_t m = 0;
    for (int i = 0; i < 9; ++i)
        if (bits_[i]->isChecked())
            m |= static_cast<mode_t>(0400 >> i);
    return m;
}

void PermissionsDialog::updateOctal()
{
    octal_->setText(tr("Octal: %1").arg(static_cast<unsigned>(mode()), 3, 8, QLatin1Char('0')));
}
