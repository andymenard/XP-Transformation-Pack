// xplasma-setup: XPlasma's Setup wizard, a program of its own -- a souped-up
// self-extracting archive, in the shape of a Windows Installer wizard. It
// carries XPlasma (its release tree, appended to this file as a tar) and
// puts it on the machine:
//
//   * first, whether this is a desktop XPlasma can dress (desktop.h: a KDE
//     Plasma session, a Plasma it knows, the programs applying runs);
//   * Windows XP's install media -- a disc image of an SP3 CD the user has,
//     or downloaded from the Internet Archive (or the copy already
//     downloaded) -- and what XPlasma uses taken out of it into XPlasma's
//     data folder: fonts, styles, .theme presets, wallpapers;
//   * XPlasma itself (its programs, style plugins, builds of Plasma's
//     widgets, application menu entries) into ~/.local;
//   * XP's pointer schemes as cursor themes Plasma's settings offer;
//   * last, the installed XPlasma asked whether this Plasma can take its look
//     (xplasma check: what Apply checks first). If it can't, the install is
//     undone -- and on a Plasma newer than XPlasma's builds, the user is
//     asked to report it, with what to paste in the report.
//
// Choosing and applying a look is XPlasma's own business (Display
// Properties), never the wizard's. Run again once XPlasma is installed, it
// repairs or removes it, as a Windows Installer package does.
//
//   xplasma-setup                     the wizard
//   xplasma-setup --screenshots DIR   every page drawn into DIR, then exit
//
// It looks like the desktop it runs on (the system's style), before XPlasma
// is on it; its pictures are its own: nothing of Windows ships in it.
#include "app/desktop.h"
#include "app/install.h"
#include "app/overrides.h"
#include "app/paths.h"
#include "app/pointers.h"
#include "app/project.h"
#include "app/apply.h"
#include "app/archive.h"
#include "app/session.h"
#include "app/setup.h"
#include "app/sounds.h"
#include "app/state.h"
#include "app/wizard/art.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QIcon>
#include <QCheckBox>
#include <QClipboard>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyle>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWizard>

#include <atomic>
#include <functional>
#include <memory>

using namespace xpl;

namespace {

// --- the payload -----------------------------------------------------------------------

// Appended to this program: a tar of the release tree, then its offset (8
// bytes, little-endian) and the magic.
constexpr char kMagic[8] = {'X', 'P', 'L', 'S', 'E', 'T', 'U', 'P'};

// The package this wizard installs: its payload unpacked into `scratch`, or,
// without one (a development build), the tree it runs from.
QString unpackPayload(const QString &scratch, QString *error)
{
    QFile self(QCoreApplication::applicationFilePath());
    if (self.open(QIODevice::ReadOnly) && self.size() > 16) {
        self.seek(self.size() - 16);
        const QByteArray tail = self.read(16);
        if (tail.mid(8) == QByteArray(kMagic, 8)) {
            qint64 offset = 0;
            for (int i = 7; i >= 0; --i)
                offset = (offset << 8) | quint8(tail.at(i));
            const QString tar = scratch + QLatin1String("/payload.tar");
            QFile out(tar);
            if (!out.open(QIODevice::WriteOnly) || !self.seek(offset)) {
                *error = QStringLiteral("can't unpack XPlasma out of the Setup program");
                return QString();
            }
            qint64 left = self.size() - 16 - offset;
            while (left > 0) {
                const QByteArray chunk = self.read(qMin<qint64>(left, 1 << 20));
                if (chunk.isEmpty() || out.write(chunk) != chunk.size()) {
                    *error = QStringLiteral("can't unpack XPlasma out of the Setup program");
                    return QString();
                }
                left -= chunk.size();
            }
            out.close();
            const QString root = scratch + QLatin1String("/package");
            if (!archive::extract(tar, root, {}, error))
                return QString();
            QFile::remove(tar);
            // (tar keeps modes; libarchive may not: the programs runnable.)
            QDirIterator it(root + QLatin1String("/bin"), QDir::Files);
            while (it.hasNext()) {
                const QString file = it.next();
                QFile::setPermissions(file, QFile::permissions(file) | QFile::ExeOwner | QFile::ExeUser);
            }
            return root;
        }
    }
    const QString here = installer::packageRoot();
    if (installer::isPackage(here))
        return here;
    *error = QStringLiteral("this Setup program has no XPlasma in it");
    return QString();
}

// --- the work ----------------------------------------------------------------------------

// (Update: a repair that brings a lower release number up to this Setup's.)
enum class Mode { Install, Update, Repair, Remove };

struct Plan
{
    Mode mode = Mode::Install;
    QString package;
    QString prefix;
    int installed = 0;           // the installed XPlasma's release number (0: from before them)
    QString media;               // the disc image the user gave; empty: download (or the one already here)
    bool deleteExtracted = false;
    bool deleteDownloads = false;
    bool demoNewerPlasma = false;   // (screenshots: the failure a too-new Plasma gets)
};

class Worker : public QThread
{
    Q_OBJECT
public:
    explicit Worker(const Plan &plan)
        : m_plan(plan)
    {
    }
    std::atomic_bool cancel{false};

Q_SIGNALS:
    void status(const QString &text);
    void percent(int value);   // -1: busy, no measure
    void done(bool ok, const QString &error);

protected:
    void run() override
    {
        QString error;
        const bool ok = m_plan.mode == Mode::Remove ? remove(&error) : install(&error);
        Q_EMIT done(ok, error);
    }

private:
    bool install(QString *error)
    {
        // Windows XP: the copy already recorded, or downloaded (the downloads
        // cache kept), and what XPlasma uses taken out.
        const State state = State::load();
        const QString given = !m_plan.media.isEmpty() ? m_plan.media : QFileInfo::exists(state.media) ? state.media : QString();
        if (given.isEmpty()) {
            for (const setup::Download &d : setup::downloads()) {
                if (!d.media)
                    continue;
                Q_EMIT status(QStringLiteral("Downloading Windows XP from the Internet Archive"));
                Q_EMIT percent(0);
                auto bytes = [this](qint64 done, qint64 total) {
                    if (total > 0)
                        Q_EMIT percent(int(done * 70 / total));
                };
                auto say = [this](const QString &line) {
                    if (line.contains(QLatin1String("Import"), Qt::CaseInsensitive) || line.contains(QLatin1String("Taking"), Qt::CaseInsensitive)) {
                        Q_EMIT status(QStringLiteral("Taking out Windows XP's fonts, styles and wallpapers"));
                        Q_EMIT percent(-1);
                    }
                };
                if (!setup::fetch(d, say, bytes, &cancel, error))
                    return false;
            }
        } else {
            Q_EMIT status(QStringLiteral("Taking out Windows XP's fonts, styles and wallpapers"));
            Q_EMIT percent(-1);
            if (!setup::importMedia(given, nullptr, {}, &cancel, error))
                return false;
        }
        if (cancel)
            return fail(error);
        Q_EMIT status(QStringLiteral("Copying new files"));
        Q_EMIT percent(75);
        if (!installer::installFiles(m_plan.package, m_plan.prefix, [this](const QString &line) { Q_EMIT status(line); }, error))
            return false;
        Q_EMIT status(QStringLiteral("Installing Windows XP's pointers"));
        Q_EMIT percent(90);
        pointers::installSchemes(error);   // (none is no failure: XPlasma can do without)
        Q_EMIT status(QStringLiteral("Checking that this desktop can take XPlasma's look"));
        Q_EMIT percent(95);
        if (!checkDesktop(error)) {
            // (A fresh install undone, Windows XP's files kept for next time;
            // a repair leaves what was there.)
            if (m_plan.mode == Mode::Install) {
                Q_EMIT status(QStringLiteral("Rolling back"));
                pointers::removeSchemes();
                installer::removeFiles(m_plan.prefix, false, false, {});
            }
            return false;
        }
        Q_EMIT percent(100);
        return true;
    }

    // The installed XPlasma's own word on this desktop (xplasma check --json).
    bool checkDesktop(QString *error)
    {
        QProcess check;
        check.start(m_plan.prefix + QLatin1String("/bin/xplasma"), {QStringLiteral("check"), QStringLiteral("--json")});
        if (!check.waitForFinished(120000)) {
            check.kill();
            return fail(error, QStringLiteral("XPlasma couldn't check this desktop: %1").arg(check.errorString()));
        }
        m_report = QJsonDocument::fromJson(check.readAllStandardOutput()).object();
        if (m_report.isEmpty())
            return fail(error, QStringLiteral("XPlasma couldn't check this desktop: %1").arg(QString::fromUtf8(check.readAllStandardError()).trimmed()));
        if (m_report.value(QLatin1String("ok")).toBool())
            return true;
        QStringList why;
        for (const QJsonValue &u : m_report.value(QLatin1String("unmet")).toArray())
            why << u.toObject().value(QLatin1String("what")).toString();
        for (const QJsonValue &c : m_report.value(QLatin1String("cannot")).toArray())
            why << c.toString();
        return fail(error, QStringLiteral("This desktop can't take XPlasma's look:\n") + why.join(QLatin1Char('\n')));
    }

    bool remove(QString *error)
    {
        Q_EMIT percent(-1);
        if (!State::load().appliedKind.isEmpty()) {
            Q_EMIT status(QStringLiteral("Putting Plasma's own look back"));
            LiveSession session;
            if (!resetDesktop(session, [this](const QString &line) { Q_EMIT status(line.trimmed()); }, error))
                return false;
        } else if (State state = State::load(); sounds::applied(state)) {
            // (a sound scheme on a desktop that's otherwise Plasma's)
            Q_EMIT status(QStringLiteral("Putting Plasma's own sounds back"));
            LiveSession session;
            sounds::revert(session, state);
            state.save();
        }
        Q_EMIT status(QStringLiteral("Removing Windows XP's pointers"));
        pointers::removeSchemes();
        installer::removeFiles(m_plan.prefix, m_plan.deleteExtracted, m_plan.deleteDownloads, [this](const QString &line) { Q_EMIT status(line); });
        Q_EMIT percent(100);
        return true;
    }

    static bool fail(QString *error, const QString &why = QStringLiteral("Setup was cancelled."))
    {
        if (error)
            *error = why;
        return false;
    }

    Plan m_plan;
    QJsonObject m_report;

public:
    // What xplasma check said, after the install (empty before, or without).
    QJsonObject report() const { return m_report; }
};

// --- the pages ---------------------------------------------------------------------------

// The installed XPlasma, by its release number ("XPlasma 1"; from before
// release numbers, "the XPlasma installed").
QString installedName(const Plan &plan)
{
    return plan.installed > 0 ? QStringLiteral("XPlasma %1").arg(plan.installed) : QStringLiteral("the XPlasma installed");
}

enum PageId { Welcome, Maintenance, Requirements, Media, Ready, Progress, Finish };

QLabel *wrapped(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

QFont titleFont()
{
    QFont f = QApplication::font();
    f.setPointSizeF(f.pointSizeF() * 1.5);
    f.setBold(true);
    return f;
}

class Wizard;

class WelcomePage : public QWizardPage
{
public:
    WelcomePage(Plan *plan)
        : m_plan(plan)
    {
        // (No subtitle: the tall picture down the left, as a Windows
        // Installer wizard's first page has.)
        auto *layout = new QVBoxLayout(this);
        m_title = new QLabel;
        m_title->setFont(titleFont());
        m_title->setWordWrap(true);
        layout->addWidget(m_title);
        layout->addSpacing(12);
        m_text = wrapped(QString());
        layout->addWidget(m_text);
        layout->addStretch(1);
    }
    void initializePage() override
    {
        m_title->setText(QStringLiteral("Welcome to the XPlasma Setup Wizard"));
        m_text->setText(m_plan->mode == Mode::Install
                            ? QStringLiteral("The Setup Wizard will install XPlasma on your computer. XPlasma draws its looks from "
                                             "Windows XP's own files, so Setup takes what XPlasma uses out of a Windows XP CD image: "
                                             "one you have, or one it downloads from the Internet Archive.\n\nClick Next to continue "
                                             "or Cancel to exit the Setup Wizard.")
                            : QStringLiteral("The Setup Wizard allows you to %1 XPlasma or remove it from your computer.\n\n"
                                             "Click Next to continue or Cancel to exit the Setup Wizard.")
                                  .arg(m_plan->mode == Mode::Update ? QStringLiteral("update") : QStringLiteral("repair")));
    }
    int nextId() const override { return m_plan->mode == Mode::Install ? Requirements : Maintenance; }

private:
    Plan *m_plan;
    QLabel *m_title, *m_text;
};

// A tick or a cross, drawn: the page's marks, in whatever font.
QPixmap mark(bool ok)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ok ? QColor(0, 128, 0) : QColor(192, 0, 0), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (ok) {
        p.drawPolyline(QPolygonF{QPointF(3, 8.5), QPointF(6.5, 12), QPointF(13, 4)});
    } else {
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
    }
    return pm;
}

// What this computer has of what XPlasma needs (desktop.h), and room for
// Windows XP; Next only when it has it all.
class RequirementsPage : public QWizardPage
{
public:
    RequirementsPage(Plan *plan, bool dryRun)
        : m_plan(plan)
        , m_dryRun(dryRun)
    {
        setTitle(QStringLiteral("System requirements"));
        setSubTitle(QStringLiteral("Setup is checking that XPlasma can run on this computer."));
        auto *layout = new QVBoxLayout(this);
        m_grid = new QGridLayout;
        m_grid->setColumnStretch(1, 1);
        m_grid->setVerticalSpacing(3);
        layout->addLayout(m_grid);
        layout->addSpacing(10);
        m_summary = wrapped(QString());
        layout->addWidget(m_summary);
        layout->addStretch(1);
    }
    void initializePage() override
    {
        while (QLayoutItem *item = m_grid->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        desktop::Report report;
        if (m_dryRun) {
            report.plasma = QStringLiteral("6.7.5");
            report.requirements = {{QStringLiteral("A KDE Plasma desktop session"), QStringLiteral("Plasma 6.7.5"), true},
                                   {QStringLiteral("KDE Plasma %1 or newer").arg(QLatin1String(desktop::kOldestPlasma)), QStringLiteral("Plasma 6.7.5"), true}};
        } else {
            LiveSession session;
            report = desktop::check(session);
        }
        // Room for Windows XP's image and what's taken out of it, and XPlasma.
        const qint64 free = QStorageInfo(paths::home()).bytesAvailable();
        report.requirements.append({QStringLiteral("1 GB free in your home folder"), QStringLiteral("%1 MB free").arg(free / 1048576),
                                    m_dryRun || free >= (qint64(1) << 30)});
        int row = 0;
        for (const desktop::Requirement &r : std::as_const(report.requirements)) {
            auto *icon = new QLabel;
            icon->setPixmap(mark(r.met));
            m_grid->addWidget(icon, row, 0, Qt::AlignTop);
            auto *what = new QLabel(r.what);
            what->setWordWrap(true);
            // (a wrapped label in a grid: its row is only made tall enough
            // for the lines it takes when its policy says so)
            QSizePolicy policy = what->sizePolicy();
            policy.setHeightForWidth(true);
            what->setSizePolicy(policy);
            m_grid->addWidget(what, row, 1);
            auto *found = new QLabel(r.found.isEmpty() ? QStringLiteral("not found") : QFileInfo(r.found).isAbsolute() ? QStringLiteral("found") : r.found);
            found->setEnabled(r.met);
            m_grid->addWidget(found, row, 2, Qt::AlignTop | Qt::AlignRight);
            ++row;
        }
        m_ok = report.ok();
        QString summary = m_ok ? QStringLiteral("This computer has what XPlasma needs. Click Next to continue.")
                               : QStringLiteral("XPlasma can't be installed on this computer until it has everything above.");
        if (m_ok && report.newer)
            summary += QStringLiteral("\n\nThis Plasma, %1, is newer than the ones this Setup was built for (up to %2). Setup will "
                                      "check that it can take XPlasma's look once XPlasma is installed.")
                           .arg(report.plasma, desktop::newestKnownPlasma());
        m_summary->setText(summary);
        Q_EMIT completeChanged();
    }
    bool isComplete() const override { return m_ok; }
    int nextId() const override { return m_plan->mode == Mode::Install ? Media : Ready; }

private:
    Plan *m_plan;
    bool m_dryRun;
    bool m_ok = false;
    QGridLayout *m_grid;
    QLabel *m_summary;
};

// Where Windows XP comes from: a disc image the user has, or the Internet
// Archive.
class MediaPage : public QWizardPage
{
public:
    MediaPage(Plan *plan)
        : m_plan(plan)
    {
        setTitle(QStringLiteral("Windows XP CD image"));
        setSubTitle(QStringLiteral("Choose where Setup gets Windows XP from."));
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(wrapped(QStringLiteral("This software requires a disc image of a Windows XP installation CD that includes Service "
                                                 "Pack 3. How would you like this wizard to obtain that copy?")));
        layout->addSpacing(8);
        m_have = new QRadioButton(QStringLiteral("I've &already downloaded a copy to provide"));
        layout->addWidget(m_have);
        auto *row = new QHBoxLayout;
        row->addSpacing(20);
        m_path = new QLineEdit;
        m_path->setPlaceholderText(QStringLiteral("A Windows XP SP3 disc image (.iso)"));
        row->addWidget(m_path, 1);
        m_browse = new QPushButton(QStringLiteral("B&rowse..."));
        row->addWidget(m_browse);
        layout->addLayout(row);
        layout->addSpacing(6);
        m_download = new QRadioButton(QStringLiteral("&Download Windows XP from the Internet Archive (about 600 MB)"));
        layout->addWidget(m_download);
        layout->addStretch(1);
        auto *group = new QButtonGroup(this);
        group->addButton(m_have);
        group->addButton(m_download);
        connect(group, &QButtonGroup::buttonToggled, this, [this] { update(); });
        connect(m_path, &QLineEdit::textChanged, this, [this] { Q_EMIT completeChanged(); });
        connect(m_browse, &QPushButton::clicked, this, [this] {
            const QString start = m_path->text().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) : m_path->text();
            const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Windows XP CD image"), start,
                                                              QStringLiteral("Disc images (*.iso *.ISO);;All files (*)"));
            if (!file.isEmpty()) {
                m_path->setText(file);
                m_path->setCursorPosition(0);
            }
        });
    }
    void initializePage() override
    {
        // The copy Setup already has (an earlier install's), else a download.
        const QString known = State::load().media;
        const bool have = !known.isEmpty() && QFileInfo(known).isFile();
        m_path->setText(have ? known : QString());
        m_path->setCursorPosition(0);   // (the path from its start)
        (have ? m_have : m_download)->setChecked(true);
        update();
    }
    bool isComplete() const override { return m_download->isChecked() || !m_path->text().trimmed().isEmpty(); }
    bool validatePage() override
    {
        if (m_download->isChecked()) {
            m_plan->media.clear();
            return true;
        }
        const QString iso = QFileInfo(m_path->text().trimmed()).absoluteFilePath();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString why;
        const bool ok = setup::checkMedia(iso, &why);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            QMessageBox::warning(this, wizard()->windowTitle(), why);
            return false;
        }
        m_plan->media = iso;
        return true;
    }
    int nextId() const override { return Ready; }

private:
    void update()
    {
        m_path->setEnabled(m_have->isChecked());
        m_browse->setEnabled(m_have->isChecked());
        Q_EMIT completeChanged();
    }

    Plan *m_plan;
    QRadioButton *m_have, *m_download;
    QLineEdit *m_path;
    QPushButton *m_browse;
};

class MaintenancePage : public QWizardPage
{
public:
    MaintenancePage(Plan *plan)
        : m_plan(plan)
    {
        const bool update = m_plan->installed < project::kVersion;
        setTitle(update ? QStringLiteral("Update or remove installation") : QStringLiteral("Repair or remove installation"));
        setSubTitle(QStringLiteral("Select the operation you wish to perform."));
        auto *layout = new QVBoxLayout(this);
        auto option = [&](const QString &name, const QString &text, QStyle::StandardPixmap icon, Mode mode) {
            auto *row = new QHBoxLayout;
            auto *button = new QToolButton;
            button->setIcon(style()->standardIcon(icon));
            button->setIconSize(QSize(32, 32));
            button->setAutoRaise(false);
            connect(button, &QToolButton::clicked, this, [this, mode] {
                m_plan->mode = mode;
                wizard()->next();
            });
            row->addWidget(button, 0, Qt::AlignTop);
            auto *words = new QVBoxLayout;
            auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(name));
            words->addWidget(label);
            words->addWidget(wrapped(text));
            row->addLayout(words, 1);
            layout->addLayout(row);
            layout->addSpacing(10);
        };
        if (update)
            option(QStringLiteral("Update"),
                   QStringLiteral("Replaces %1 with XPlasma %2, keeping your look and settings.").arg(installedName(*m_plan)).arg(project::kVersion),
                   QStyle::SP_ArrowUp, Mode::Update);
        else if (m_plan->installed > project::kVersion)
            option(QStringLiteral("Replace with XPlasma %1").arg(project::kVersion),
                   QStringLiteral("XPlasma %1, newer than this Setup's, is installed. Puts this Setup's older XPlasma %2 in its place.")
                       .arg(m_plan->installed)
                       .arg(project::kVersion),
                   QStyle::SP_BrowserReload, Mode::Repair);
        else
            option(QStringLiteral("Repair"),
                   QStringLiteral("Downloads Windows XP again if it's gone, takes its files out again, and puts back XPlasma's own files."),
                   QStyle::SP_BrowserReload, Mode::Repair);
        option(QStringLiteral("Remove"), QStringLiteral("Puts Plasma's own look back and removes XPlasma from your computer."), QStyle::SP_TrashIcon,
               Mode::Remove);
        layout->addStretch(1);
    }
    // (The buttons choose and go on; Next alone would be ambiguous.)
    bool isComplete() const override { return false; }
    int nextId() const override { return m_plan->mode == Mode::Remove ? Ready : Requirements; }

private:
    Plan *m_plan;
};

class ReadyPage : public QWizardPage
{
public:
    ReadyPage(Plan *plan)
        : m_plan(plan)
    {
        auto *layout = new QVBoxLayout(this);
        m_text = wrapped(QString());
        layout->addWidget(m_text);
        m_extracted = new QCheckBox(QStringLiteral("Also delete what XPlasma took out of Windows XP (its fonts, styles, themes)"));
        m_downloads = new QCheckBox;
        layout->addSpacing(8);
        layout->addWidget(m_extracted);
        layout->addWidget(m_downloads);
        layout->addStretch(1);
        setCommitPage(true);
    }
    void initializePage() override
    {
        const bool remove = m_plan->mode == Mode::Remove;
        const QString what = m_plan->mode == Mode::Install  ? QStringLiteral("install")
                             : m_plan->mode == Mode::Update ? QStringLiteral("update")
                             : remove                       ? QStringLiteral("remove")
                                                            : QStringLiteral("repair");
        setTitle(QStringLiteral("Ready to %1 XPlasma").arg(what));
        setSubTitle(remove ? QStringLiteral("You have chosen to remove XPlasma from your computer.")
                           : QStringLiteral("The Setup Wizard is ready to begin."));
        m_text->setText(
            m_plan->mode == Mode::Install
                ? QStringLiteral("Click Install to begin the installation. Setup %1, then installs XPlasma in %2.\n\nIf you want "
                                 "to review or change any of your installation settings, click Back. Click Cancel to exit the wizard.")
                      .arg(m_plan->media.isEmpty() ? QStringLiteral("downloads Windows XP from the Internet Archive (about 600 MB)")
                                                   : QStringLiteral("takes what XPlasma uses out of %1").arg(QFileInfo(m_plan->media).fileName()),
                           m_plan->prefix)
            : remove ? QStringLiteral("Click Remove to remove XPlasma from your computer. Your desktop goes back to Plasma's own look.\n\n"
                                      "If you want to review or change any of your installation settings, click Back. Click Cancel "
                                      "to exit the wizard.")
            : m_plan->mode == Mode::Update
                ? QStringLiteral("Click Update to replace %1 with XPlasma %2. Your look and settings are kept.\n\nIf you want to review "
                                 "or change any of your installation settings, click Back. Click Cancel to exit the wizard.")
                      .arg(installedName(*m_plan))
                      .arg(project::kVersion)
                : QStringLiteral("Click Repair to repair the installation of XPlasma.\n\nIf you want to review or change any of "
                                 "your installation settings, click Back. Click Cancel to exit the wizard."));
        m_extracted->setVisible(remove);
        const qint64 downloaded = installer::downloadsSize();
        m_downloads->setText(QStringLiteral("Also delete the downloaded Windows XP CD image (%1 MB)").arg(downloaded / 1048576));
        m_downloads->setVisible(remove && downloaded > 0);
        setButtonText(QWizard::CommitButton,
                      m_plan->mode == Mode::Install  ? QStringLiteral("&Install")
                      : m_plan->mode == Mode::Update ? QStringLiteral("&Update")
                      : remove                       ? QStringLiteral("&Remove")
                                                     : QStringLiteral("&Repair"));
    }
    bool validatePage() override
    {
        m_plan->deleteExtracted = m_plan->mode == Mode::Remove && m_extracted->isChecked();
        m_plan->deleteDownloads = m_plan->mode == Mode::Remove && m_downloads->isVisible() && m_downloads->isChecked();
        return true;
    }

private:
    Plan *m_plan;
    QLabel *m_text;
    QCheckBox *m_extracted, *m_downloads;
};

class ProgressPage : public QWizardPage
{
public:
    ProgressPage(Plan *plan, bool dryRun)
        : m_plan(plan)
        , m_dryRun(dryRun)
    {
        auto *layout = new QVBoxLayout(this);
        m_text = wrapped(QString());
        layout->addWidget(m_text);
        layout->addSpacing(12);
        m_status = new QLabel(QStringLiteral("Status:"));
        layout->addWidget(m_status);
        m_bar = new QProgressBar;
        m_bar->setTextVisible(false);
        layout->addWidget(m_bar);
        layout->addStretch(1);
    }
    bool ok() const { return m_ok; }
    QString error() const { return m_error; }
    void cancel()
    {
        if (m_worker)
            m_worker->cancel = true;
    }
    bool running() const { return m_worker && m_worker->isRunning(); }
    QJsonObject report() const { return m_worker ? m_worker->report() : QJsonObject(); }

    void initializePage() override
    {
        const bool remove = m_plan->mode == Mode::Remove;
        const bool update = m_plan->mode == Mode::Update;
        setTitle(m_plan->mode == Mode::Install ? QStringLiteral("Installing XPlasma")
                 : update                     ? QStringLiteral("Updating XPlasma")
                 : remove                     ? QStringLiteral("Removing XPlasma")
                                              : QStringLiteral("Repairing XPlasma"));
        setSubTitle(remove                          ? QStringLiteral("XPlasma is being removed.")
                    : update                        ? QStringLiteral("XPlasma is being updated.")
                    : m_plan->mode == Mode::Repair ? QStringLiteral("XPlasma is being repaired.")
                                                   : QStringLiteral("XPlasma is being installed."));
        m_text->setText(remove   ? QStringLiteral("Please wait while the Setup Wizard removes XPlasma. This may take a moment.")
                        : update ? QStringLiteral("Please wait while the Setup Wizard updates XPlasma. This may take several minutes.")
                                 : QStringLiteral("Please wait while the Setup Wizard installs XPlasma. This may take several minutes."));
        m_done = false;
        if (m_dryRun) {
            m_status->setText(QStringLiteral("Status: Downloading Windows XP from the Internet Archive"));
            m_bar->setRange(0, 100);
            m_bar->setValue(42);
            return;
        }
        m_worker = std::make_unique<Worker>(*m_plan);
        connect(m_worker.get(), &Worker::status, this, [this](const QString &s) { m_status->setText(QStringLiteral("Status: ") + s); });
        connect(m_worker.get(), &Worker::percent, this, [this](int v) {
            if (v < 0) {
                m_bar->setRange(0, 0);
            } else {
                m_bar->setRange(0, 100);
                m_bar->setValue(v);
            }
        });
        connect(m_worker.get(), &Worker::done, this, [this](bool ok, const QString &error) {
            m_ok = ok;
            m_error = error;
            m_done = true;
            Q_EMIT completeChanged();
            wizard()->next();
        });
        m_worker->start();
    }
    bool isComplete() const override { return m_done; }
    int nextId() const override { return Finish; }

private:
    Plan *m_plan;
    bool m_dryRun;
    bool m_done = false, m_ok = false;
    QString m_error;
    QLabel *m_text, *m_status;
    QProgressBar *m_bar;
    std::unique_ptr<Worker> m_worker;
};

class FinishPage : public QWizardPage
{
public:
    FinishPage(Plan *plan, ProgressPage *progress, bool dryRun)
        : m_plan(plan)
        , m_progress(progress)
        , m_dryRun(dryRun)
    {
        auto *layout = new QVBoxLayout(this);
        m_title = new QLabel;
        m_title->setFont(titleFont());
        m_title->setWordWrap(true);
        layout->addWidget(m_title);
        layout->addSpacing(12);
        m_text = wrapped(QString());
        layout->addWidget(m_text);
        layout->addSpacing(12);
        m_launch = new QCheckBox(QStringLiteral("Launch XPlasma"));
        m_launch->setChecked(true);
        layout->addWidget(m_launch);
        // (a failure on a Plasma newer than XPlasma's builds: what to report)
        m_diagnostics = new QPlainTextEdit;
        m_diagnostics->setReadOnly(true);
        m_diagnostics->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_diagnostics->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(m_diagnostics, 1);
        m_copy = new QPushButton(QStringLiteral("&Copy"));
        connect(m_copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_diagnostics->toPlainText()); });
        layout->addWidget(m_copy, 0, Qt::AlignRight);
        m_stretch = new QWidget;
        layout->addWidget(m_stretch, 1);
    }
    void initializePage() override
    {
        const bool ok = m_dryRun ? !m_plan->demoNewerPlasma : m_progress->ok();
        const bool remove = m_plan->mode == Mode::Remove;
        if (ok) {
            m_title->setText(QStringLiteral("Completing the XPlasma Setup Wizard"));
            m_text->setText(remove ? QStringLiteral("XPlasma has been removed from your computer.\n\nClick the Finish button to exit the Setup Wizard.")
                            : m_plan->mode == Mode::Update
                                ? QStringLiteral("XPlasma has been updated to XPlasma %1.\n\nClick the Finish button to exit the Setup Wizard.")
                                      .arg(project::kVersion)
                            : m_plan->mode == Mode::Repair
                                ? QStringLiteral("XPlasma has been repaired.\n\nClick the Finish button to exit the Setup Wizard.")
                                : QStringLiteral("XPlasma has been installed. Choose your look in XPlasma Properties.\n\nClick the Finish "
                                                 "button to exit the Setup Wizard."));
        } else if (const QJsonObject report = demoReport(); report.value(QLatin1String("newer")).toBool()) {
            // Installing on a Plasma newer than XPlasma's builds, which didn't
            // take its look: a bug report, and what to put in it.
            const QString url = QString::fromUtf8(project::kIssuesUrl);
            m_title->setText(QStringLiteral("Installation of XPlasma has failed"));
            m_text->setTextFormat(Qt::RichText);
            m_text->setOpenExternalLinks(true);
            m_text->setText(QStringLiteral("Your current version of KDE Plasma, %1, is newer than the latest version this installer is built "
                                           "for.<br><br>Please submit a bug report to %2, being sure to include the following information:")
                                .arg(report.value(QLatin1String("plasma")).toString().toHtmlEscaped(),
                                     url.isEmpty() ? QStringLiteral("the issue tracker")
                                                   : QStringLiteral("<a href=\"%1\">the issue tracker</a>").arg(url.toHtmlEscaped())));
            m_diagnostics->setPlainText(report.value(QLatin1String("diagnostics")).toString());
        } else {
            m_title->setText(QStringLiteral("XPlasma Setup Wizard ended prematurely"));
            m_text->setText(QStringLiteral("XPlasma Setup Wizard ended prematurely because of an error. Your system has not been "
                                           "modified; to install this program at a later time, run the Setup Wizard again.\n\n")
                            + m_progress->error()
                            + QStringLiteral("\n\nClick the Finish button to exit the Setup Wizard."));
        }
        m_launch->setVisible(ok && !remove);
        const bool reporting = !m_diagnostics->toPlainText().isEmpty();
        m_diagnostics->setVisible(reporting);
        m_copy->setVisible(reporting);
        m_stretch->setVisible(!reporting);
    }
    bool validatePage() override
    {
        // (In a transient unit of its own, as XPlasma launches things: it
        // outlives the wizard whatever started it.)
        if (!m_dryRun && m_launch->isVisible() && m_launch->isChecked())
            QProcess::startDetached(QStringLiteral("/bin/sh"),
                                    {QStringLiteral("-c"), overrides::launchCommand(m_plan->prefix + QLatin1String("/bin/xplasma-edit"))});
        return true;
    }

private:
    Plan *m_plan;
    ProgressPage *m_progress;
    bool m_dryRun;
    // What xplasma check said (the screenshots': a too-new Plasma's).
    QJsonObject demoReport() const
    {
        if (!m_dryRun || !m_plan->demoNewerPlasma)
            return m_progress->report();
        return QJsonObject{{QStringLiteral("newer"), true},
                           {QStringLiteral("plasma"), QStringLiteral("6.9.0")},
                           {QStringLiteral("diagnostics"),
                            QStringLiteral("XPlasma: %1 (builds of Plasma's widgets for 6.6, 6.7)\nPlasma: 6.9.0\nKDE Frameworks: 6.24.0\n"
                                           "Qt: 6.12.0 (XPlasma built with 6.10.0)\nDistribution: Arch Linux\nSession: KDE, wayland\n\n"
                                           "What failed:\nnone of XPlasma's builds of Plasma's widgets loads in Plasma 6.9.0")
                                .arg(project::kVersion)}};
    }

    QLabel *m_title, *m_text;
    QCheckBox *m_launch;
    QPlainTextEdit *m_diagnostics;
    QPushButton *m_copy;
    QWidget *m_stretch;
};

class Wizard : public QWizard
{
public:
    Wizard(Plan *plan, bool dryRun, int start = Welcome)
    {
        setWindowTitle(QStringLiteral("XPlasma Setup"));
        setWizardStyle(QWizard::ModernStyle);
        setPixmap(QWizard::LogoPixmap, wizard::bannerIcon(48));
        setOption(QWizard::NoBackButtonOnLastPage);
        setOption(QWizard::NoCancelButtonOnLastPage);
        setButtonText(QWizard::BackButton, QStringLiteral("< &Back"));
        setButtonText(QWizard::NextButton, QStringLiteral("&Next >"));
        setButtonText(QWizard::FinishButton, QStringLiteral("&Finish"));
        setButtonText(QWizard::CancelButton, QStringLiteral("Cancel"));
        m_progress = new ProgressPage(plan, dryRun);
        // (The tall picture on the first and last pages only; the ones
        // between have the banner across the top.)
        auto *welcome = new WelcomePage(plan);
        auto *finish = new FinishPage(plan, m_progress, dryRun);
        welcome->setPixmap(QWizard::WatermarkPixmap, wizard::sidePicture());
        finish->setPixmap(QWizard::WatermarkPixmap, wizard::sidePicture());
        setPage(Welcome, welcome);
        setPage(Maintenance, new MaintenancePage(plan));
        setPage(Requirements, new RequirementsPage(plan, dryRun));
        setPage(Media, new MediaPage(plan));
        setPage(Ready, new ReadyPage(plan));
        setPage(Progress, m_progress);
        setPage(Finish, finish);
        setStartId(start);
        // A Windows Installer wizard's one size (370 x 270 dialog units in
        // Tahoma 8), whatever the page.
        setFixedSize(494, 361);
    }

    void reject() override
    {
        // As a Windows Installer wizard asks, and a running step is stopped
        // where it can be (a download, an import), its partial work undone.
        if (currentId() == Finish) {
            QWizard::reject();
            return;
        }
        if (QMessageBox::question(this, windowTitle(), QStringLiteral("Are you sure you want to cancel XPlasma installation?"))
            != QMessageBox::Yes)
            return;
        if (m_progress->running()) {
            m_progress->cancel();
            return;   // (it ends, and the wizard goes to its last page)
        }
        QWizard::reject();
    }

private:
    ProgressPage *m_progress;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("xplasma-setup"));
    // Its boxed product for an icon: it has no desktop entry for the
    // taskbar to find one by (Wayland's compositors take it from the window).
    QIcon icon;
    for (int size : {16, 22, 32, 48, 64})
        icon.addPixmap(xpl::wizard::bannerIcon(size));
    QApplication::setWindowIcon(icon);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("XPlasma's Setup wizard."));
    parser.addHelpOption();
    const QCommandLineOption screenshots(QStringLiteral("screenshots"), QStringLiteral("Draw every page into DIR and exit."), QStringLiteral("DIR"));
    parser.addOption(screenshots);
    parser.process(app);

    QTemporaryDir scratch;
    QString error;
    Plan plan;
    plan.package = unpackPayload(scratch.path(), &error);
    if (plan.package.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("XPlasma Setup"), error);
        return 1;
    }
    plan.prefix = installer::installedPrefix();
    plan.installed = plan.prefix.isEmpty() ? 0 : installer::installedVersion(plan.prefix);
    plan.mode = plan.prefix.isEmpty()                    ? Mode::Install
                : plan.installed < project::kVersion      ? Mode::Update
                                                          : Mode::Repair;
    if (plan.prefix.isEmpty())
        plan.prefix = paths::home() + QLatin1String("/.local");

    if (parser.isSet(screenshots)) {
        const QString dir = parser.value(screenshots);
        QDir().mkpath(dir);
        bool ok = true;
        const struct { const char *name; Mode mode; int page; bool newer; } shots[] = {
            {"welcome", Mode::Install, Welcome, false},         {"requirements", Mode::Install, Requirements, false},
            {"media", Mode::Install, Media, false},             {"ready", Mode::Install, Ready, false},
            {"progress", Mode::Install, Progress, false},       {"finish", Mode::Install, Finish, false},
            {"newer-plasma", Mode::Install, Finish, true},      {"maintenance", Mode::Repair, Maintenance, false},
            {"remove", Mode::Remove, Ready, false},             {"update", Mode::Update, Maintenance, false},
            {"update-ready", Mode::Update, Ready, false},
        };
        for (const auto &shot : shots) {
            Plan p = plan;
            p.mode = shot.mode;
            // (maintenance: this Setup's own release installed; update: the one before)
            p.installed = shot.mode == Mode::Update ? project::kVersion - 1 : project::kVersion;
            p.demoNewerPlasma = shot.newer;
            Wizard w(&p, true, shot.page);
            w.setAttribute(Qt::WA_DontShowOnScreen);
            w.show();
            QApplication::processEvents();
            ok &= w.grab().save(QDir(dir).filePath(QLatin1String(shot.name) + QLatin1String(".png")));
        }
        return ok ? 0 : 1;
    }

    Wizard wizard(&plan, false);
    wizard.show();
    return app.exec();
}

#include "main.moc"
