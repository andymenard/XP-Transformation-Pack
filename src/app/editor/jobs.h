#pragma once
// Long work (applying a look, importing XP's media, a download) off the
// window's thread, shown in a progress dialog: what it's doing, a details
// log, a byte count when there is one, and Cancel.
#include <QDialog>
#include <QThread>

#include <atomic>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace xpl::editor {

struct JobContext
{
    std::function<void(const QString &)> say;
    std::function<void(qint64, qint64)> bytes;
    const std::atomic_bool *cancel = nullptr;
};

// Runs `work` on a thread; true if it succeeded (false with *error). Modal:
// returns when it's done.
bool runJob(QWidget *parent, const QString &title, const std::function<bool(const JobContext &, QString *error)> &work,
            QString *error = nullptr);

} // namespace xpl::editor
