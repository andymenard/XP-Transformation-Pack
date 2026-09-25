#include "app/editor/soundsproperties.h"

#include "app/media.h"
#include "app/session.h"
#include "app/state.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace xpl::editor {

namespace {

const QString kTitle = QStringLiteral("Sounds and Audio Devices Properties");
const QString kNone = QStringLiteral("(None)");
constexpr int kSoundRole = Qt::UserRole;   // a sound's value in the scheme (media name or path)
constexpr int kEventRole = Qt::UserRole;   // an event's key

// GDI's MulDiv: a * b / c, rounded.
int mulDiv(int a, int b, int c)
{
    return int((qint64(a) * b + (a * b >= 0 ? c / 2 : -c / 2)) / c);
}

// One of mmsys.cpl's 1-bit button bitmaps (play, stop): its black, on
// nothing.
QIcon glyph(int id)
{
    const auto bmp = media::bitmap(QStringLiteral("mmsys.cpl"), id);
    if (!bmp)
        return QIcon();
    QImage im = bmp->convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < im.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(im.scanLine(y));
        for (int x = 0; x < im.width(); ++x)
            if (qGray(line[x]) > 128)
                line[x] = 0;
    }
    return QIcon(QPixmap::fromImage(im));
}

QIcon icon16(const QString &file, int group)
{
    const auto im = media::icon(file, group, 16);
    return im ? QIcon(QPixmap::fromImage(*im)) : QIcon();
}

// A sound's name in the lists: a media sound's own, a file's name.
QString soundLabel(const QString &sound)
{
    return QFileInfo(sound).isAbsolute() ? QFileInfo(sound).fileName() : sound;
}

// A player for a sound file: PipeWire's, PulseAudio's, ALSA's.
QStringList playerFor(const QString &file)
{
    for (const char *p : {"pw-play", "paplay", "aplay"})
        if (const QString exe = QStandardPaths::findExecutable(QLatin1String(p)); !exe.isEmpty())
            return {exe, file};
    return {};
}

// XP's Save Scheme As: "Save this sound scheme as:", a name, OK, Cancel.
QString askName(QWidget *parent, const QString &suggested)
{
    QDialog d(parent);
    d.setWindowTitle(QStringLiteral("Save Scheme As"));
    auto *layout = new QVBoxLayout(&d);
    layout->addWidget(new QLabel(QStringLiteral("&Save this sound scheme as:")));
    auto *edit = new QLineEdit(suggested);
    edit->selectAll();
    layout->addWidget(edit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    d.setFixedWidth(QFontMetrics(d.font()).averageCharWidth() * 58);
    return d.exec() == QDialog::Accepted ? edit->text().trimmed() : QString();
}

} // namespace

SoundsProperties::SoundsProperties(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(kTitle);
    m_playIcon = glyph(4608);
    m_stopIcon = glyph(4609);
    m_speaker = icon16(QStringLiteral("mmsys.cpl"), 4354);
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    tabs->addTab(soundsTab(), QStringLiteral("Sounds"));
    root->addWidget(tabs);
    root->setSizeConstraint(QLayout::SetFixedSize);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    m_apply = new QPushButton(QStringLiteral("&Apply"));
    ok->setDefault(true);
    for (QPushButton *b : {ok, cancel, m_apply})
        row->addWidget(b);
    root->addLayout(row);
    connect(ok, &QPushButton::clicked, this, [this] {
        if (apply())
            accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &SoundsProperties::apply);

    // What's applied -- or, the first time, XP's own, ready to apply.
    const State state = State::load();
    m_saved = sounds::savedSchemes(state);
    m_applied = sounds::applied(state);
    m_scheme = m_applied ? *m_applied : sounds::defaultScheme();
    for (const QString &sound : std::as_const(m_scheme.sounds))
        if (QFileInfo(sound).isAbsolute() && !m_browsed.contains(sound))
            m_browsed << sound;
    fillSchemes();
    fillSounds();
    showEvents();
    eventPicked();
    updateApply();
}

SoundsProperties::~SoundsProperties()
{
    stop();
}

QRect SoundsProperties::du(int x, int y, int w, int h) const
{
    // The dialog's base units, as GDI works them out for its font.
    const QFontMetrics fm(font());
    const int cx = (fm.horizontalAdvance(QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")) / 26 + 1) / 2;
    const int cy = fm.height();
    return QRect(mulDiv(x, cx, 4), mulDiv(y, cy, 8), mulDiv(w, cx, 4), mulDiv(h, cy, 8));
}

QWidget *SoundsProperties::soundsTab()
{
    // mmsys.cpl's dialog 4876, "Events Schemes" (227 x 218).
    auto *page = new QWidget;
    page->setFixedSize(du(0, 0, 227, 218).size());
    auto place = [&](QWidget *w, QRect r) {
        w->setParent(page);
        w->setGeometry(r);
    };
    // (a combo box's template height is its list's: the box is a line's)
    auto combo = [&](QComboBox *c, int x, int y, int w) {
        c->setParent(page);
        const QRect r = du(x, y, w, 12);
        c->setGeometry(r.x(), r.y(), r.width(), qMax(r.height(), c->sizeHint().height()));
    };
    auto *intro = new QLabel(QStringLiteral("A sound scheme is a set of sounds applied to events in Windows and programs. "
                                            "You can select an existing scheme or save one you have modified."));
    intro->setWordWrap(true);
    intro->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    place(intro, du(7, 5, 213, 24));
    auto *schemeLabel = new QLabel(QStringLiteral("Sound s&cheme:"));
    place(schemeLabel, du(8, 34, 60, 8));
    m_schemes = new QComboBox;
    combo(m_schemes, 8, 45, 209);
    schemeLabel->setBuddy(m_schemes);
    m_saveAs = new QPushButton(QStringLiteral("Sav&e As..."));
    place(m_saveAs, du(114, 61, 50, 14));
    m_delete = new QPushButton(QStringLiteral("&Delete"));
    place(m_delete, du(167, 61, 50, 14));
    auto *how = new QLabel(QStringLiteral("To change sounds, click a program event in the following list and then select a sound to apply. "
                                          "You can save the changes as a new sound scheme."));
    how->setWordWrap(true);
    how->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    place(how, du(7, 80, 213, 24));
    auto *eventsLabel = new QLabel(QStringLiteral("&Program events:"));
    place(eventsLabel, du(8, 108, 60, 8));
    m_events = new QTreeWidget;
    m_events->setHeaderHidden(true);
    m_events->setRootIsDecorated(false);   // (XP's has no lines or buttons)
    m_events->setItemsExpandable(false);
    m_events->setIconSize(QSize(16, 16));
    m_events->setIndentation(19);
    place(m_events, du(8, 118, 209, 62));
    eventsLabel->setBuddy(m_events);
    m_soundsLabel = new QLabel(QStringLiteral("&Sounds:"));
    place(m_soundsLabel, du(8, 185, 60, 8));
    m_sounds = new QComboBox;
    combo(m_sounds, 8, 196, 139);
    m_soundsLabel->setBuddy(m_sounds);
    m_play = new QPushButton;
    m_play->setToolTip(QStringLiteral("Play and Stop Toggle"));
    m_play->setIcon(m_playIcon);
    m_play->setIconSize(QSize(23, 23));
    place(m_play, du(150, 196, 15, 14));
    m_browse = new QPushButton(QStringLiteral("&Browse..."));
    place(m_browse, du(168, 196, 50, 14));

    connect(m_schemes, &QComboBox::activated, this, &SoundsProperties::schemePicked);
    connect(m_saveAs, &QPushButton::clicked, this, &SoundsProperties::saveAs);
    connect(m_delete, &QPushButton::clicked, this, &SoundsProperties::deleteScheme);
    connect(m_events, &QTreeWidget::currentItemChanged, this, &SoundsProperties::eventPicked);
    connect(m_sounds, &QComboBox::activated, this, &SoundsProperties::soundPicked);
    connect(m_play, &QPushButton::clicked, this, [this] {
        if (m_player)
            stop();
        else
            play();
    });
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString event = currentEvent();
        const QString label = m_events->currentItem() ? m_events->currentItem()->text(0) : QString();
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Browse for %1 sound").arg(label), QDir::homePath(),
                                                          QStringLiteral("Sounds (*.wav *.ogg *.oga)"));
        if (event.isEmpty() || file.isEmpty())
            return;
        if (!m_browsed.contains(file))
            m_browsed << file;
        m_scheme.sounds.insert(event, file);
        fillSounds();
        fillSchemes();
        showEvents();
        eventPicked();
        updateApply();
    });
    return page;
}

QList<sounds::Scheme> SoundsProperties::allSchemes() const
{
    return sounds::builtinSchemes() + m_saved;
}

std::optional<sounds::Scheme> SoundsProperties::named(const QString &name) const
{
    for (const sounds::Scheme &s : allSchemes())
        if (s.name == name)
            return s;
    return std::nullopt;
}

bool SoundsProperties::modified() const
{
    const auto s = named(m_scheme.name);
    return !s || s->sounds != m_scheme.sounds;
}

void SoundsProperties::fillSchemes()
{
    // (sorted, as XP's list is)
    m_loading = true;
    m_schemes->clear();
    QStringList names;
    for (const sounds::Scheme &s : allSchemes())
        names << s.name;
    const QString shown = modified() ? QStringLiteral("%1 (modified)").arg(m_scheme.name) : m_scheme.name;
    if (!names.contains(shown))
        names << shown;
    names.sort(Qt::CaseInsensitive);
    m_schemes->addItems(names);
    m_schemes->setCurrentText(shown);
    m_delete->setEnabled(!modified() && !(m_scheme.name == QLatin1String(sounds::kDefaultScheme) || m_scheme.name == QLatin1String(sounds::kNoSounds)));
    m_loading = false;
}

void SoundsProperties::schemePicked(int index)
{
    if (m_loading)
        return;
    const QString name = m_schemes->itemText(index);
    const auto s = named(name);
    if (!s || (s->name == m_scheme.name && !modified()))
        return;
    if (modified()) {
        // XP's question, before the changes are lost.
        const auto answer = QMessageBox::question(this, QStringLiteral("Save previous scheme"),
                                                  QStringLiteral("If you choose a new scheme, your previous scheme will be lost because you did not "
                                                                 "save it. Do you want to save the previous scheme?"),
                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) {
            fillSchemes();
            return;
        }
        if (answer == QMessageBox::Yes) {
            const sounds::Scheme picked = *s;
            saveAs();
            if (modified()) {   // (the save was cancelled)
                fillSchemes();
                return;
            }
            m_scheme = picked;
        } else {
            m_scheme = *s;
        }
    } else {
        m_scheme = *s;
    }
    fillSchemes();
    fillSounds();
    showEvents();
    eventPicked();
    updateApply();
}

void SoundsProperties::showEvents()
{
    const QString current = currentEvent();
    const QSignalBlocker block(m_events);
    m_events->clear();
    QTreeWidgetItem *group = nullptr;
    QTreeWidgetItem *select = nullptr;
    for (const sounds::Event &e : sounds::events()) {
        if (!group || group->text(0) != e.program) {
            group = new QTreeWidgetItem(m_events, {e.program});
            group->setIcon(0, e.program == QLatin1String("Windows")
                                  ? QIcon(QPixmap::fromImage(media::bitmap(QStringLiteral("mmsys.cpl"), 4625).value_or(QImage())
                                                                 .scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)))
                                  : icon16(QStringLiteral("explorer.exe"), 100));
            group->setFlags(Qt::ItemIsEnabled);
            group->setExpanded(true);
        }
        auto *item = new QTreeWidgetItem(group, {e.label});
        item->setData(0, kEventRole, e.key);
        if (m_scheme.sounds.contains(e.key))
            item->setIcon(0, m_speaker);
        else {
            QPixmap blank(16, 16);
            blank.fill(Qt::transparent);
            item->setIcon(0, QIcon(blank));
        }
        if (e.key == current)
            select = item;
    }
    m_events->expandAll();
    if (select)
        m_events->setCurrentItem(select);
}

QString SoundsProperties::currentEvent() const
{
    const QTreeWidgetItem *item = m_events->currentItem();
    return item ? item->data(0, kEventRole).toString() : QString();
}

void SoundsProperties::fillSounds()
{
    const QSignalBlocker block(m_sounds);
    m_sounds->clear();
    m_sounds->addItem(kNone, QString());
    QStringList all = sounds::mediaSounds();
    for (const QString &f : std::as_const(m_browsed))
        all << f;
    std::sort(all.begin(), all.end(), [](const QString &a, const QString &b) { return soundLabel(a).compare(soundLabel(b), Qt::CaseInsensitive) < 0; });
    for (const QString &s : std::as_const(all)) {
        m_sounds->addItem(soundLabel(s), s);
        if (QFileInfo(s).isAbsolute())
            m_sounds->setItemData(m_sounds->count() - 1, s, Qt::ToolTipRole);
    }
}

void SoundsProperties::eventPicked()
{
    stop();
    const QString event = currentEvent();
    // (nothing to set until an event is picked, as XP's)
    for (QWidget *w : std::initializer_list<QWidget *>{m_soundsLabel, m_sounds, m_browse})
        w->setEnabled(!event.isEmpty());
    const QSignalBlocker block(m_sounds);
    const QString sound = m_scheme.sounds.value(event);
    const int at = event.isEmpty() ? -1 : m_sounds->findData(sound);
    m_sounds->setCurrentIndex(at);
    m_play->setEnabled(!event.isEmpty() && !sound.isEmpty());
}

void SoundsProperties::soundPicked(int index)
{
    const QString event = currentEvent();
    if (event.isEmpty())
        return;
    stop();
    const QString sound = m_sounds->itemData(index, kSoundRole).toString();
    if (sound == m_scheme.sounds.value(event))
        return;
    if (sound.isEmpty())
        m_scheme.sounds.remove(event);
    else
        m_scheme.sounds.insert(event, sound);
    fillSchemes();
    showEvents();
    m_play->setEnabled(!sound.isEmpty());
    updateApply();
}

void SoundsProperties::play()
{
    const QString sound = m_scheme.sounds.value(currentEvent());
    if (sound.isEmpty())
        return;
    QString error;
    const QString file = sounds::soundFile(sound, &error);
    const QStringList argv = file.isEmpty() ? QStringList() : playerFor(file);
    if (argv.isEmpty()) {
        QMessageBox::warning(this, kTitle, file.isEmpty() ? error : QStringLiteral("Windows cannot play the sound %1: there's no sound player here "
                                                                                   "(pw-play, paplay or aplay).").arg(soundLabel(sound)));
        return;
    }
    m_player = new QProcess(this);
    connect(m_player, &QProcess::finished, this, [this] { stop(); });
    m_player->start(argv.first(), argv.mid(1));
    m_play->setIcon(m_stopIcon);
}

void SoundsProperties::stop()
{
    if (!m_player)
        return;
    QProcess *p = m_player;
    m_player = nullptr;
    p->disconnect(this);
    if (p->state() != QProcess::NotRunning) {
        p->kill();
        p->waitForFinished(1000);
    }
    p->deleteLater();
    if (m_play)
        m_play->setIcon(m_playIcon);
}

void SoundsProperties::saveAs()
{
    const QString suggested = modified() ? QString() : m_scheme.name;
    const QString name = askName(this, suggested);
    if (name.isEmpty())
        return;
    if (name == QLatin1String(sounds::kDefaultScheme) || name == QLatin1String(sounds::kNoSounds) || name.endsWith(QLatin1String("(modified)"))) {
        QMessageBox::warning(this, QStringLiteral("Save Scheme"), QStringLiteral("You cannot use the name %1 for a scheme. Type a different name.").arg(name));
        return;
    }
    for (const sounds::Scheme &s : std::as_const(m_saved)) {
        if (s.name == name
            && QMessageBox::question(this, QStringLiteral("Save Scheme"),
                                     QStringLiteral("A scheme named %1 already exists. Do you want to replace the existing scheme?").arg(name))
                   != QMessageBox::Yes)
            return;
    }
    m_scheme.name = name;
    State state = State::load();
    sounds::saveScheme(state, m_scheme);
    state.save();
    m_saved = sounds::savedSchemes(state);
    fillSchemes();
    updateApply();
}

void SoundsProperties::deleteScheme()
{
    const QString name = m_scheme.name;
    if (QMessageBox::question(this, QStringLiteral("Delete"), QStringLiteral("Are you sure you want to remove the %1 sound scheme?").arg(name))
        != QMessageBox::Yes)
        return;
    State state = State::load();
    sounds::deleteScheme(state, name);
    state.save();
    m_saved = sounds::savedSchemes(state);
    // (the sounds stay, as XP's did, now no scheme's but their own)
    fillSchemes();
    updateApply();
}

void SoundsProperties::updateApply()
{
    m_apply->setEnabled(!m_applied || *m_applied != m_scheme);
}

bool SoundsProperties::apply()
{
    if (m_applied && *m_applied == m_scheme)
        return true;
    stop();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    LiveSession session;
    State state = State::load();
    QString error;
    const bool ok = sounds::apply(session, state, m_scheme, &error) && state.save(&error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, kTitle, error);
        return false;
    }
    m_applied = m_scheme;
    updateApply();
    return true;
}

bool SoundsProperties::saveScreenshots(const QString &dir)
{
    QDir().mkpath(dir);
    m_scheme = sounds::defaultScheme();
    fillSchemes();
    showEvents();
    if (QTreeWidgetItem *windows = m_events->topLevelItem(0); windows && windows->childCount())
        m_events->setCurrentItem(windows->child(0));
    eventPicked();
    setAttribute(Qt::WA_DontShowOnScreen);
    show();
    QApplication::processEvents();
    return grab().save(QDir(dir).filePath(QStringLiteral("sounds.png")));
}

} // namespace xpl::editor
