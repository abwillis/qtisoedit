/*
 * dialogs.h
 * Volume properties and permissions dialogs.
 */
#ifndef DIALOGS_H
#define DIALOGS_H

#include <QDialog>
#include <sys/types.h>

class QCheckBox;
class QLabel;
class QLineEdit;
class IsoSession;

class VolumePropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    VolumePropertiesDialog(IsoSession& session, int writeTypes, QWidget* parent);

    /* applies edits to the session; returns true if anything changed */
    bool apply(QString* error);

private:
    IsoSession& session_;
    QLineEdit* volumeName_;
    QLineEdit* publisher_;
};

class PermissionsDialog : public QDialog
{
    Q_OBJECT

public:
    PermissionsDialog(mode_t initial, const QString& itemText, QWidget* parent);
    mode_t mode() const;

private:
    void updateOctal();

    QCheckBox* bits_[9];
    QLabel* octal_;
};

/* "Read as Rock Ridge" style text for a FNTYPE_* mask */
QString namespacesText(unsigned mask);

#endif
