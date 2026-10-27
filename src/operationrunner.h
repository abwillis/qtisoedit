/*
 * operationrunner.h
 * Modal progress dialog that runs one IsoSession operation on a worker thread.
 *
 * bkisofs reports progress and "continue after this failure?" questions via
 * C callbacks invoked on the thread doing the work. This dialog marshals them
 * to the GUI thread: progress is posted (queued), warnings block the worker
 * until the user answers.
 */
#ifndef OPERATIONRUNNER_H
#define OPERATIONRUNNER_H

#include <QDialog>
#include <atomic>
#include <functional>

class QLabel;
class QProgressBar;
class QPushButton;
class IsoSession;

class OperationRunner : public QDialog
{
    Q_OBJECT

public:
    OperationRunner(IsoSession& session, QWidget* parent);

    /* Runs fn on a worker thread, shows this dialog until it returns, and
     * returns fn's result. determinate=true shows a 0-100 % bar (writing). */
    int run(const QString& title, const QString& text, bool determinate,
            const std::function<int()>& fn);

    /* safe to call from the worker */
    void postText(const QString& text);
    bool cancelRequested() const { return cancelRequested_.load(); }

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;

private:
    void requestCancel();
    bool askContinue(const QString& message);

    IsoSession& session_;
    QLabel* label_;
    QProgressBar* bar_;
    QPushButton* cancelButton_;
    bool running_ = false;
    bool continueForAll_ = false;
    std::atomic<bool> cancelRequested_{false};
};

#endif
