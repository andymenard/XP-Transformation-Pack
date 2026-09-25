// xplasma-setup: XPLasma's Setup wizard, a program of its own -- a souped-up
// self-extracting archive, in the shape of a Windows Installer wizard. It
// carries XPLasma (its release tree, appended to this file as a tar) and
// puts it on the machine:
//
//   * Windows XP's install media, downloaded from the Internet Archive (or
//     the copy already downloaded), and what XPLasma uses taken out of it
//     into XPLasma's data folder -- fonts, styles, .theme presets, wallpapers;
//   * XPLasma itself (its programs, style plugins, builds of Plasma's
//     widgets, application menu entries) into ~/.local;
//   * XP's pointer schemes as cursor themes Plasma's settings offer.
//
// Choosing and applying a look is XPLasma's own business (Display
// Properties), never the wizard's. Run again once XPLasma is installed, it
// repairs or removes it, as a Windows Installer package does.
//
//   xplasma-setup                     the wizard
//   xplasma-setup --screenshots DIR   every page drawn into DIR, then exit
//
// Its look is XPLasma's own Windows Classic rendering and its pictures its
// own: nothing of Windows ships in it.
#include "core/classic.h"
#include "app/controls.h"
#include "app/install.h"
#include "app/look.h"
#include "app/overrides.h"
#include "app/paths.h"
#include "app/pointers.h"
#include "app/apply.h"
#include "app/archive.h"
#include "app/session.h"
#include "app/setup.h"
#include "app/sounds.h"
#include "app/state.h"
#include "app/wizard/art.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
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
                *error = QStringLiteral("can't unpack XPLasma out of the Setup program");
                return QString();
            }
            qint64 left = self.size() - 16 - offset;
            while (left > 0) {
                const QByteArray chunk = self.read(qMin<qint64>(left, 1 << 20));
                if (chunk.isEmpty() || out.write(chunk) != chunk.size()) {
                    *error = QStringLiteral("can't unpack XPLasma out of the Setup program");
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
    *error = QStringLiteral("this Setup program has no XPLasma in it");
    return QString();
}

// --- the work ----------------------------------------------------------------------------

enum class Mode { Install, Repair, Remove };

struct Plan
{
    Mode mode = Mode::Install;
    QString package;
    QString prefix;
    bool deleteExtracted = false;
    bool deleteDownloads = false;
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
        // cache kept), and what XPLasma uses taken out.
        const State state = State::load();
        if (state.media.isEmpty() || !QFileInfo::exists(state.media)) {
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
            if (!setup::importMedia(state.media, nullptr, {}, &cancel, error))
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
        pointers::installSchemes(error);   // (none is no failure: XPLasma can do without)
        Q_EMIT percent(100);
        return true;
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

    static bool fail(QString *error)
    {
        if (error)
            *error = QStringLiteral("Setup was cancelled.");
        return false;
    }

    Plan m_plan;
};

// --- the pages ---------------------------------------------------------------------------

enum PageId { Welcome, Maintenance, Ready, Progress, Finish };

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
        m_title->setText(QStringLiteral("Welcome to the XPLasma Setup Wizard"));
        m_text->setText(m_plan->mode == Mode::Install
                            ? QStringLiteral("The Setup Wizard will install XPLasma on your computer. XPLasma draws its looks from "
                                             "Windows XP's own files, so Setup downloads Windows XP from the Internet Archive and takes "
                                             "out what XPLasma uses.\n\nClick Next to continue or Cancel to exit the Setup Wizard.")
                            : QStringLiteral("The Setup Wizard allows you to repair XPLasma or remove it from your computer.\n\n"
                                             "Click Next to continue or Cancel to exit the Setup Wizard."));
    }
    int nextId() const override { return m_plan->mode == Mode::Install ? Ready : Maintenance; }

private:
    Plan *m_plan;
    QLabel *m_title, *m_text;
};

class MaintenancePage : public QWizardPage
{
public:
    MaintenancePage(Plan *plan)
        : m_plan(plan)
    {
        setTitle(QStringLiteral("Repair or remove installation"));
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
        option(QStringLiteral("Repair"),
               QStringLiteral("Downloads Windows XP again if it's gone, takes its files out again, and puts back XPLasma's own files."),
               QStyle::SP_BrowserReload, Mode::Repair);
        option(QStringLiteral("Remove"), QStringLiteral("Puts Plasma's own look back and removes XPLasma from your computer."), QStyle::SP_TrashIcon,
               Mode::Remove);
        layout->addStretch(1);
    }
    // (The buttons choose and go on; Next alone would be ambiguous.)
    bool isComplete() const override { return false; }
    int nextId() const override { return Ready; }

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
        m_extracted = new QCheckBox(QStringLiteral("Also delete what XPLasma took out of Windows XP (its fonts, styles, themes)"));
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
        const QString what = m_plan->mode == Mode::Install ? QStringLiteral("install") : remove ? QStringLiteral("remove") : QStringLiteral("repair");
        setTitle(QStringLiteral("Ready to %1 XPLasma").arg(what));
        setSubTitle(remove ? QStringLiteral("You have chosen to remove XPLasma from your computer.")
                           : QStringLiteral("The Setup Wizard is ready to begin."));
        m_text->setText(
            m_plan->mode == Mode::Install
                ? QStringLiteral("Click Install to begin the installation. Setup downloads Windows XP (about 600 MB) unless it "
                                 "has before, then installs XPLasma in %1.\n\nIf you want to review or change any of your "
                                 "installation settings, click Back. Click Cancel to exit the wizard.")
                      .arg(m_plan->prefix)
            : remove ? QStringLiteral("Click Remove to remove XPLasma from your computer. Your desktop goes back to Plasma's own look.\n\n"
                                      "If you want to review or change any of your installation settings, click Back. Click Cancel "
                                      "to exit the wizard.")
                     : QStringLiteral("Click Repair to repair the installation of XPLasma.\n\nIf you want to review or change any of "
                                      "your installation settings, click Back. Click Cancel to exit the wizard."));
        m_extracted->setVisible(remove);
        const qint64 downloaded = installer::downloadsSize();
        m_downloads->setText(QStringLiteral("Also delete the downloaded Windows XP CD image (%1 MB)").arg(downloaded / 1048576));
        m_downloads->setVisible(remove && downloaded > 0);
        setButtonText(QWizard::CommitButton,
                      m_plan->mode == Mode::Install ? QStringLiteral("&Install") : remove ? QStringLiteral("&Remove") : QStringLiteral("&Repair"));
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

    void initializePage() override
    {
        const bool remove = m_plan->mode == Mode::Remove;
        setTitle(m_plan->mode == Mode::Install ? QStringLiteral("Installing XPLasma")
                 : remove                     ? QStringLiteral("Removing XPLasma")
                                              : QStringLiteral("Repairing XPLasma"));
        setSubTitle(remove                          ? QStringLiteral("XPLasma is being removed.")
                    : m_plan->mode == Mode::Repair ? QStringLiteral("XPLasma is being repaired.")
                                                   : QStringLiteral("XPLasma is being installed."));
        m_text->setText(remove ? QStringLiteral("Please wait while the Setup Wizard removes XPLasma. This may take a moment.")
                               : QStringLiteral("Please wait while the Setup Wizard installs XPLasma. This may take several minutes."));
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
        m_launch = new QCheckBox(QStringLiteral("Launch XPLasma"));
        m_launch->setChecked(true);
        layout->addWidget(m_launch);
        layout->addStretch(1);
    }
    void initializePage() override
    {
        const bool ok = m_dryRun || m_progress->ok();
        const bool remove = m_plan->mode == Mode::Remove;
        if (ok) {
            m_title->setText(QStringLiteral("Completing the XPLasma Setup Wizard"));
            m_text->setText(remove ? QStringLiteral("XPLasma has been removed from your computer.\n\nClick the Finish button to exit the Setup Wizard.")
                            : m_plan->mode == Mode::Repair
                                ? QStringLiteral("XPLasma has been repaired.\n\nClick the Finish button to exit the Setup Wizard.")
                                : QStringLiteral("XPLasma has been installed. Choose your look in Display Properties.\n\nClick the Finish "
                                                 "button to exit the Setup Wizard."));
        } else {
            m_title->setText(QStringLiteral("XPLasma Setup Wizard ended prematurely"));
            m_text->setText(QStringLiteral("XPLasma Setup Wizard ended prematurely because of an error. Your system has not been "
                                           "modified; to install this program at a later time, run the Setup Wizard again.\n\n")
                            + m_progress->error()
                            + QStringLiteral("\n\nClick the Finish button to exit the Setup Wizard."));
        }
        m_launch->setVisible(ok && !remove);
    }
    bool validatePage() override
    {
        // (In a transient unit of its own, as XPLasma launches things: it
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
    QLabel *m_title, *m_text;
    QCheckBox *m_launch;
};

class Wizard : public QWizard
{
public:
    Wizard(Plan *plan, bool dryRun, int start = Welcome)
    {
        setWindowTitle(QStringLiteral("XPLasma Setup"));
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
        if (QMessageBox::question(this, windowTitle(), QStringLiteral("Are you sure you want to cancel XPLasma installation?"))
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

// XPLasma's Windows Classic, from the package's own plugin, in Tahoma where
// the machine has it.
void useClassic(QApplication &app, const QString &package)
{
    QCoreApplication::addLibraryPath(package + QLatin1String("/lib/xplasma/qt6"));
    if (QStyle *style = xpStyle(Look::fromScheme(ClassicScheme()))) {
        QApplication::setStyle(style);
        QApplication::setPalette(style->standardPalette());
    }
    if (QFontDatabase::hasFamily(QStringLiteral("Tahoma")))
        app.setFont(QFont(QStringLiteral("Tahoma"), 8));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("xplasma-setup"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("XPLasma's Setup wizard."));
    parser.addHelpOption();
    const QCommandLineOption screenshots(QStringLiteral("screenshots"), QStringLiteral("Draw every page into DIR and exit."), QStringLiteral("DIR"));
    parser.addOption(screenshots);
    parser.process(app);

    QTemporaryDir scratch;
    QString error;
    Plan plan;
    plan.package = unpackPayload(scratch.path(), &error);
    if (plan.package.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("XPLasma Setup"), error);
        return 1;
    }
    useClassic(app, plan.package);
    plan.prefix = installer::installedPrefix();
    plan.mode = plan.prefix.isEmpty() ? Mode::Install : Mode::Repair;
    if (plan.prefix.isEmpty())
        plan.prefix = paths::home() + QLatin1String("/.local");

    if (parser.isSet(screenshots)) {
        const QString dir = parser.value(screenshots);
        QDir().mkpath(dir);
        bool ok = true;
        const struct { const char *name; Mode mode; int page; } shots[] = {
            {"welcome", Mode::Install, Welcome},       {"ready", Mode::Install, Ready},        {"progress", Mode::Install, Progress},
            {"finish", Mode::Install, Finish},         {"maintenance", Mode::Repair, Maintenance}, {"remove", Mode::Remove, Ready},
        };
        for (const auto &shot : shots) {
            Plan p = plan;
            p.mode = shot.mode;
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
