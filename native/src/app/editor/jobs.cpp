// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/jobs.h"

#include <QDialogButtonBox>
#include <QEventLoop>
#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace xpl::editor {

namespace {

class Worker : public QThread
{
    Q_OBJECT
public:
    std::function<bool(const JobContext &, QString *)> work;
    std::atomic_bool cancel{false};
    bool ok = false;
    QString error;

Q_SIGNALS:
    void said(const QString &line);
    void progressed(qint64 done, qint64 total);

protected:
    void run() override
    {
        JobContext context;
        context.say = [this](const QString &line) { Q_EMIT said(line); };
        context.bytes = [this](qint64 done, qint64 total) { Q_EMIT progressed(done, total); };
        context.cancel = &cancel;
        ok = work(context, &error);
    }
};

} // namespace

bool runJob(QWidget *parent, const QString &title, const std::function<bool(const JobContext &, QString *error)> &work, QString *error)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *status = new QLabel(title + QLatin1String(" ..."));
    status->setWordWrap(true);
    auto *bar = new QProgressBar;
    bar->setRange(0, 0);   // busy, until there are bytes to count
    auto *details = new QPlainTextEdit;
    details->setReadOnly(true);
    details->setMaximumBlockCount(2000);
    details->setMinimumHeight(140);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    layout->addWidget(status);
    layout->addWidget(bar);
    layout->addWidget(details, 1);
    layout->addWidget(buttons);

    Worker worker;
    worker.work = work;
    QObject::connect(&worker, &Worker::said, &dialog, [&](const QString &line) {
        details->appendPlainText(line);
        if (!line.startsWith(QLatin1Char(' ')))
            status->setText(line);
    });
    QObject::connect(&worker, &Worker::progressed, &dialog, [&](qint64 done, qint64 total) {
        if (total <= 0) {
            bar->setRange(0, 0);
            return;
        }
        bar->setRange(0, 1000);
        bar->setValue(int(done * 1000 / total));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, [&] {
        worker.cancel = true;
        status->setText(QStringLiteral("Cancelling ..."));
        buttons->setEnabled(false);
    });
    QObject::connect(&worker, &QThread::finished, &dialog, &QDialog::accept);
    worker.start();
    dialog.exec();
    worker.cancel = worker.cancel || worker.isRunning();   // (closed early: stop it)
    worker.wait();
    if (!worker.ok && error)
        *error = worker.error;
    return worker.ok;
}

bool runResponsiveTask(QWidget *parent,const std::function<bool(QString *)> &work,QString *error)
{
    class CloseGuard final : public QObject {
        QWidget *m_window;
    public:
        explicit CloseGuard(QWidget *window):m_window(window){if(window)window->installEventFilter(this);}
        ~CloseGuard() override {if(m_window)m_window->removeEventFilter(this);}
        bool eventFilter(QObject *object,QEvent *event) override {if(object==m_window && event->type()==QEvent::Close){event->ignore();return true;}return false;}
    } guard(parent);
    bool ok=false;QString result;QEventLoop loop;
    QThread *worker=QThread::create([&]{ok=work(&result);});
    QObject::connect(worker,&QThread::finished,&loop,&QEventLoop::quit,Qt::QueuedConnection);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    worker->start();loop.exec(QEventLoop::ExcludeUserInputEvents);
    worker->wait();delete worker;
    QApplication::restoreOverrideCursor();
    if(!ok && error)*error=result;
    return ok;
}

} // namespace xpl::editor

#include "jobs.moc"
