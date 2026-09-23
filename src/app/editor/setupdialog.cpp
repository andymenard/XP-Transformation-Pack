#include "app/editor/setupdialog.h"

#include "app/editor/jobs.h"
#include "app/paths.h"
#include "app/setup.h"
#include "app/state.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace xpl::editor {

SetupDialog::SetupDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("XPLasma Setup"));
    auto *layout = new QVBoxLayout(this);
    auto *blurb = new QLabel(QStringLiteral(
        "XPLasma themes Plasma from Windows XP's own files, which it can't carry itself: point it at your XP install "
        "media, or fetch it and the official theme packs from archive.org."));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);
    m_checks = new QLabel;
    m_checks->setTextFormat(Qt::RichText);
    m_checks->setWordWrap(true);
    layout->addWidget(m_checks);

    auto job = [this](const QString &title, const std::function<bool(const JobContext &, QString *)> &work) {
        QString error;
        if (runJob(this, title, work, &error))
            m_changed = true;
        else if (!error.isEmpty() && error != QLatin1String("cancelled"))
            QMessageBox::warning(this, title, error);
        refresh();
    };
    auto *iso = new QPushButton(QStringLiteral("Use an XP ISO..."));
    connect(iso, &QPushButton::clicked, this, [=] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Windows XP install media"), paths::home(),
                                                          QStringLiteral("Disc images (*.iso *.img)"));
        if (!file.isEmpty())
            job(QStringLiteral("Importing XP's media"), [=](const JobContext &c, QString *e) {
                return setup::importMedia(file, nullptr, c.say, c.cancel, e);
            });
    });
    auto *folder = new QPushButton(QStringLiteral("Use an XP folder (I386)..."));
    connect(folder, &QPushButton::clicked, this, [=] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Windows XP install media"), paths::home());
        if (!dir.isEmpty())
            job(QStringLiteral("Importing XP's media"), [=](const JobContext &c, QString *e) {
                return setup::importMedia(dir, nullptr, c.say, c.cancel, e);
            });
    });
    auto *download = new QPushButton(QStringLiteral("Download XP's media (589 MB)"));
    connect(download, &QPushButton::clicked, this, [=] {
        job(QStringLiteral("Downloading XP's media"), [](const JobContext &c, QString *e) {
            return setup::fetch(setup::downloads().at(0), c.say, c.bytes, c.cancel, e);
        });
    });
    auto *packs = new QPushButton(QStringLiteral("Download the official theme packs"));
    connect(packs, &QPushButton::clicked, this, [=] {
        job(QStringLiteral("Downloading theme packs"), [](const JobContext &c, QString *e) {
            for (const setup::Download &d : setup::downloads())
                if (!d.media && !d.optional && !setup::fetch(d, c.say, c.bytes, c.cancel, e))
                    return false;
            return true;
        });
    });
    for (QPushButton *b : {iso, folder, download, packs})
        layout->addWidget(b);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
    refresh();
}

void SetupDialog::refresh()
{
    QString text;
    QString detail;
    const bool plasma = setup::plasmaSession(&detail);
    text += QStringLiteral("%1 Plasma 6 session (%2)<br>").arg(plasma ? QStringLiteral("&#10004;") : QStringLiteral("&#10008;"), detail.toHtmlEscaped());
    for (const setup::Check &c : setup::preflight())
        text += QStringLiteral("%1 %2 &mdash; %3<br>")
                    .arg(c.ok() ? QStringLiteral("&#10004;") : QStringLiteral("&#10008;"), c.tool, c.purpose.toHtmlEscaped());
    const QString media = State::load().media;
    text += media.isEmpty() || !QFileInfo::exists(media)
                ? QStringLiteral("&#10008; XP's install media<br>")
                : QStringLiteral("&#10004; XP's install media: %1<br>").arg(QFileInfo(media).fileName().toHtmlEscaped());
    text += QFileInfo::exists(paths::themes() + QLatin1String("/Royale"))
                ? QStringLiteral("&#10004; the official theme packs")
                : QStringLiteral("&#10008; the official theme packs");
    m_checks->setText(text);
}

} // namespace xpl::editor
