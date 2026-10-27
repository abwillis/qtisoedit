/*
 * operationrunner.cpp
 * See operationrunner.h.
 */
#include "operationrunner.h"
#include "isosession.h"

#include <QCloseEvent>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

OperationRunner::OperationRunner(IsoSession& session, QWidget* parent)
    : QDialog(parent), session_(session)
{
    setModal(true);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(420);

    label_ = new QLabel(this);
    label_->setWordWrap(true);
    bar_ = new QProgressBar(this);
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    connect(cancelButton_, &QPushButton::clicked, this, &OperationRunner::requestCancel);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancelButton_);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(label_);
    layout->addWidget(bar_);
    layout->addLayout(buttons);
}

int OperationRunner::run(const QString& title, const QString& text,
                         bool determinate, const std::function<int()>& fn)
{
    setWindowTitle(title);
    label_->setText(text);
    bar_->setRange(0, determinate ? 100 : 0); /* 0..0 = busy indicator */
    bar_->setValue(0);
    cancelButton_->setEnabled(true);
    continueForAll_ = false;
    cancelRequested_.store(false);

    session_.setCallbacks(
        nullptr,
        [this](double percent) {
            QMetaObject::invokeMethod(this, [this, percent] {
                bar_->setValue(qBound(0, static_cast<int>(percent), 100));
            }, Qt::QueuedConnection);
        },
        [this](const std::string& message) {
            bool goOn = false;
            const QString text = QString::fromLocal8Bit(message.c_str());
            /* the worker waits here until the user answers */
            QMetaObject::invokeMethod(this, [this, &goOn, &text] {
                goOn = askContinue(text);
            }, Qt::BlockingQueuedConnection);
            return goOn;
        });

    running_ = true;
    show();

    QEventLoop loop;
    QFutureWatcher<int> watcher;
    connect(&watcher, &QFutureWatcher<int>::finished, &loop, &QEventLoop::quit);
    std::function<int()> task = fn;
    watcher.setFuture(QtConcurrent::run([task]() -> int { return task(); }));
    loop.exec();

    running_ = false;
    session_.setCallbacks(nullptr, nullptr, nullptr);
    hide();
    return watcher.result();
}

void OperationRunner::postText(const QString& text)
{
    QMetaObject::invokeMethod(this, [this, text] { label_->setText(text); },
                              Qt::QueuedConnection);
}

void OperationRunner::requestCancel()
{
    if (!running_ || cancelRequested_.load())
        return;
    cancelRequested_.store(true);
    session_.cancel();
    cancelButton_->setEnabled(false);
    label_->setText(tr("Cancelling…"));
}

void OperationRunner::reject()
{
    /* Esc must not close the dialog while the worker is still running */
    if (running_)
        requestCancel();
    else
        QDialog::reject();
}

void OperationRunner::closeEvent(QCloseEvent* event)
{
    if (running_) {
        requestCancel();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

bool OperationRunner::askContinue(const QString& message)
{
    if (continueForAll_)
        return true;
    if (cancelRequested_.load())
        return false;

    QMessageBox box(QMessageBox::Warning, windowTitle(), message,
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("Continue with the remaining items?"));
    QPushButton* cont = box.addButton(tr("Continue"), QMessageBox::AcceptRole);
    QPushButton* all = box.addButton(tr("Continue for All"), QMessageBox::AcceptRole);
    QPushButton* stop = box.addButton(tr("Stop"), QMessageBox::RejectRole);
    box.setDefaultButton(cont);
    box.setEscapeButton(stop);
    box.exec();

    if (box.clickedButton() == all) {
        continueForAll_ = true;
        return true;
    }
    return box.clickedButton() == cont;
}
