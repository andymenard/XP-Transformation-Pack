// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/displayproperties.h"

#include "app/editor/abouttab.h"
#include "app/editor/advanced.h"
#include "app/editor/extras.h"
#include "app/editor/jobs.h"
#include "app/editor/setupdialog.h"
#include "app/editor/soundstab.h"
#include "app/paths.h"
#include "app/session.h"
#include "app/state.h"
#include "core/themefile.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace xpl::editor {

namespace {

// Theme combo rows that aren't themes.
constexpr const char *kBrowse = "browse";
constexpr const char *kSetup = "setup";

// XP's Position drop-down, as Plasma FillModes.
const std::pair<const char *, int> kPositions[] = {{"Center", 6}, {"Tile", 3}, {"Stretch", 0}};

// The desktop the session shows now (Plasma's first image or colour
// wallpaper), for the Desktop tab to open on.
Wallpaper currentDesktop()
{
    Wallpaper w{QString(), 2, QColor(58, 110, 165)};
    QFile f(paths::config() + QLatin1String("/plasma-org.kde.plasma.desktop-appletsrc"));
    if (!f.open(QIODevice::ReadOnly))
        return w;
    QString group;
    for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1Char('['))) {
            group = line.trimmed();
            continue;
        }
        const bool image = group.startsWith(QLatin1String("[Containments]")) && group.endsWith(QLatin1String("[Wallpaper][org.kde.image][General]"));
        const bool colour = group.startsWith(QLatin1String("[Containments]")) && group.endsWith(QLatin1String("[Wallpaper][org.kde.color][General]"));
        if (!image && !colour)
            continue;
        const QString key = line.section(QLatin1Char('='), 0, 0), value = line.section(QLatin1Char('='), 1).trimmed();
        if (image && key == QLatin1String("Image") && w.image.isEmpty())
            w.image = value.startsWith(QLatin1String("file:")) ? QUrl(value).toLocalFile() : value;
        else if (image && key == QLatin1String("FillMode"))
            w.fill = value.toInt();
        else if (key == QLatin1String("Color"))
            w.colour = QColor(value.startsWith(QLatin1Char('#')) ? value : QStringLiteral("#3a6ea5"));
    }
    if (!QFileInfo(w.image).isFile())
        w.image.clear();
    return w;
}

// What a look draws from: the style and variant, or the Classic scheme's
// contents (which Advanced edits).
QString lookKey(const Look &look)
{
    if (look.classic()) {
        ThemeFile file;
        look.system.writeTo(file);
        return QStringLiteral("classic:") + QString::fromLatin1(QCryptographicHash::hash(file.toBytes(), QCryptographicHash::Sha1).toHex());
    }
    return QFileInfo(look.stylePath).canonicalFilePath() + QLatin1Char(':') + look.variant.toUpper();
}

bool sameDesktop(const Wallpaper &a, const Wallpaper &b)
{
    return a.image == b.image && a.fill == b.fill && a.colour == b.colour;
}

bool shiftHeld()
{
    return QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
}

QLabel *sampleBox(QSize size)
{
    auto *label = new QLabel;
    label->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    label->setFixedSize(size + QSize(6, 6));
    label->setAlignment(Qt::AlignCenter);
    return label;
}

} // namespace

DisplayProperties::DisplayProperties(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("XPlasma Properties"));
    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget;
    m_tabs->addTab(themesTab(), QStringLiteral("Themes"));
    m_tabs->addTab(desktopTab(), QStringLiteral("Desktop"));
    m_tabs->addTab(appearanceTab(), QStringLiteral("Appearance"));
    m_tabs->addTab(pointersTab(), QStringLiteral("Pointers"));
    m_sounds = new SoundsTab;
    m_tabs->addTab(m_sounds, QStringLiteral("Sounds"));
    connect(m_sounds, &SoundsTab::changed, this, &DisplayProperties::updateApply);
    auto *about = new AboutTab;
    m_tabs->addTab(about, QStringLiteral("About"));
    // The dialog's width: its widest tab's but About's (whose paragraphs
    // wrap to it), and room for every tab's name.
    int pageWidth = 0;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->widget(i) != about)
            pageWidth = qMax(pageWidth, m_tabs->widget(i)->sizeHint().width());
    const int frame = m_tabs->sizeHint().width() - m_tabs->currentWidget()->sizeHint().width();
    pageWidth = qMax(pageWidth, m_tabs->tabBar()->sizeHint().width() + 4 - frame);
    for (int i = 0; i < m_tabs->count(); ++i)
        m_tabs->widget(i)->setMinimumWidth(pageWidth);
    about->fitWidth(pageWidth);
    root->addWidget(m_tabs, 1);
    // A property sheet: its size is its contents', not the user's to change.
    root->setSizeConstraint(QLayout::SetFixedSize);

    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    m_apply = new QPushButton(QStringLiteral("Apply"));
    m_apply->setToolTip(QStringLiteral("Hold Shift to rebuild and apply everything, changed or not."));
    ok->setDefault(true);
    for (QPushButton *b : {ok, cancel, m_apply})
        row->addWidget(b);
    root->addLayout(row);
    connect(ok, &QPushButton::clicked, this, [this] {
        if (apply())
            accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &DisplayProperties::apply);
    // (Shift, wherever the focus is in the dialog.)
    qApp->installEventFilter(this);

    loadLibrary();
    // Open on what's applied now.
    const State state = State::load();
    m_loading = true;
    if (state.appliedKind == QLatin1String("classic")) {
        if (const auto s = schemes::load(state.scheme))
            m_scheme = *s;
    }
    fillStyles();
    for (int i = 0; i < m_styles.size(); ++i)
        if (state.appliedKind == QLatin1String("style") && m_styles.at(i).path == state.style)
            m_style->setCurrentIndex(i);
    if (state.appliedKind == QLatin1String("classic"))
        m_style->setCurrentIndex(m_styles.size());
    fillColours();
    if (state.appliedKind == QLatin1String("style") && m_style->currentIndex() < m_styles.size())
        if (const StyleVariant *v = m_styles.at(m_style->currentIndex()).variant(state.variant)) {
            m_colour->setCurrentIndex(m_colour->findData(v->colour));
            fillSizes();
            m_size->setCurrentIndex(m_size->findData(v->size));
        }
    fillSizes();
    const Wallpaper desktop = currentDesktop();
    fillWallpapers();
    for (int i = 0; i < m_wallpapers->count(); ++i)
        if (m_wallpapers->item(i)->data(Qt::UserRole).toString() == desktop.image)
            m_wallpapers->setCurrentRow(i);
    if (m_wallpapers->currentRow() < 0 && !desktop.image.isEmpty()) {
        auto *item = new QListWidgetItem(QFileInfo(desktop.image).completeBaseName());
        item->setData(Qt::UserRole, desktop.image);
        m_wallpapers->addItem(item);
        m_wallpapers->setCurrentItem(item);
    }
    if (m_wallpapers->currentRow() < 0)
        m_wallpapers->setCurrentRow(0);
    for (int i = 0; i < m_position->count(); ++i)
        if (m_position->itemData(i).toInt() == desktop.fill.value_or(2))
            m_position->setCurrentIndex(i);
    m_desktopColour->setColour(desktop.colour);
    m_themeFile = state.themeFile;
    for (int i = 0; i < m_themes->count(); ++i)
        if (m_themes->itemData(i).toString() == state.themeFile)
            m_themes->setCurrentIndex(i);
    m_loading = false;
    // What's applied: the look XPlasma recorded, if the dialog could show it,
    // and the desktop as the dialog shows it.
    QString applied;
    if (state.appliedKind == QLatin1String("style"))
        applied = QFileInfo(state.style).canonicalFilePath() + QLatin1Char(':') + state.variant.toUpper();
    else if (state.appliedKind == QLatin1String("classic"))
        if (const auto s = schemes::load(state.scheme))
            applied = lookKey(Look::fromScheme(*s));
    // (made by an older XPlasma: not what this one would make, so Apply
    // stays ready until it's rebuilt)
    m_outdated = lookOutdated(state);
    m_appliedLook = applied == lookKey(look()) && !m_outdated ? applied : QString();
    m_appliedDesktop = wallpaper();
    m_appliedThemeFile = state.themeFile;
    // Pointers: what XPlasma applied, or "(None)" (Plasma's own).
    m_pointerSchemes = pointers::schemes();
    m_appliedPointers = pointers::applied(state);
    if (m_appliedPointers) {
        m_pointers = m_appliedPointers->first;
        m_pointerShadow = m_appliedPointers->second;
    }
    fillPointerSchemes();
    showPointers();
    m_appliedStartExtras = m_startExtras = StartButtonExtras::fromJson(state.settings.value(QLatin1String(kStartButtonKey)).toObject());
    // First start -- nothing of XPlasma's on the desktop yet: the "Windows
    // XP" theme chosen, as if picked (Luna, Bliss...), with XP's own
    // pointers, and Apply ready -- so Apply alone gives XP's desktop.
    if (state.appliedKind.isEmpty() && !m_appliedPointers) {
        if (const int xp = m_themes->findText(QStringLiteral("Windows XP")); xp >= 0) {
            m_themes->setCurrentIndex(xp);
            themePicked(xp);
        }
        if (!m_pointerSchemes.isEmpty()) {
            m_pointers = pointers::defaultScheme();
            m_pointerShadow = true;
            fillPointerSchemes();
            showPointers();
        }
    }
    updateApply();
    if (!m_previewer.styleAvailable())
        m_appearanceSample->setToolTip(QStringLiteral("XPlasma's style plugin isn't installed: the samples can't be drawn"));
    renderThemes();
    renderDesktop();
    renderAppearance();
}

// --- tabs ----------------------------------------------------------------------------------

QWidget *DisplayProperties::themesTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *blurb = new QLabel(QStringLiteral("A theme is a background plus a set of sounds, icons, and other elements to help you "
                                            "personalize your computer with one click."));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);
    layout->addWidget(new QLabel(QStringLiteral("Theme:")));
    auto *row = new QHBoxLayout;
    m_themes = new QComboBox;
    m_themes->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_themes->setMinimumContentsLength(1);
    row->addWidget(m_themes, 1);
    auto *saveAsButton = new QPushButton(QStringLiteral("Save As..."));
    m_delete = new QPushButton(QStringLiteral("Delete"));
    row->addWidget(saveAsButton);
    row->addWidget(m_delete);
    layout->addLayout(row);
    layout->addWidget(new QLabel(QStringLiteral("Sample:")));
    m_themeSample = sampleBox(preview::kThemesSize);
    layout->addWidget(m_themeSample, 0, Qt::AlignHCenter);
    layout->addStretch(1);
    connect(m_themes, &QComboBox::activated, this, &DisplayProperties::themePicked);
    connect(saveAsButton, &QPushButton::clicked, this, &DisplayProperties::saveAs);
    connect(m_delete, &QPushButton::clicked, this, &DisplayProperties::deleteTheme);
    return page;
}

QWidget *DisplayProperties::desktopTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    m_desktopSample = sampleBox(QSize(200, 170));
    m_desktopSample->setFrameStyle(QFrame::NoFrame);
    layout->addWidget(m_desktopSample, 0, Qt::AlignHCenter);
    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(QStringLiteral("Background:")), 0, 0);
    m_wallpapers = new QListWidget;
    m_wallpapers->setFixedHeight(122);
    grid->addWidget(m_wallpapers, 1, 0);
    auto *column = new QVBoxLayout;
    auto *browse = new QPushButton(QStringLiteral("Browse..."));
    column->addWidget(browse);
    column->addSpacing(8);
    column->addWidget(new QLabel(QStringLiteral("Position:")));
    m_position = new QComboBox;
    for (const auto &[name, fill] : kPositions)
        m_position->addItem(QLatin1String(name), fill);
    column->addWidget(m_position);
    column->addSpacing(8);
    column->addWidget(new QLabel(QStringLiteral("Color:")));
    m_desktopColour = new ColourButton;
    column->addWidget(m_desktopColour);
    column->addStretch(1);
    grid->addLayout(column, 1, 1);
    grid->setColumnStretch(0, 1);
    layout->addLayout(grid);
    layout->addStretch(1);
    connect(m_wallpapers, &QListWidget::currentRowChanged, this, &DisplayProperties::desktopChanged);
    connect(m_position, &QComboBox::activated, this, &DisplayProperties::desktopChanged);
    connect(m_desktopColour, &ColourButton::picked, this, &DisplayProperties::desktopChanged);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Browse"), paths::wallpapers(),
                                                          QStringLiteral("Pictures (*.jpg *.jpeg *.png *.bmp *.gif)"));
        if (file.isEmpty())
            return;
        auto *item = new QListWidgetItem(QFileInfo(file).completeBaseName());
        item->setData(Qt::UserRole, file);
        m_wallpapers->addItem(item);
        m_wallpapers->setCurrentItem(item);
    });
    return page;
}

QWidget *DisplayProperties::appearanceTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    m_appearanceSample = sampleBox(preview::kAppearanceSize);
    layout->addWidget(m_appearanceSample, 0, Qt::AlignHCenter);
    auto *grid = new QGridLayout;
    m_style = new QComboBox;
    m_colour = new QComboBox;
    m_size = new QComboBox;
    m_advanced = new QPushButton(QStringLiteral("Advanced"));
    // (Where XP had Effects...: XPlasma's own, the Start button's label and logo.)
    m_extras = new QPushButton(QStringLiteral("Extras..."));
    grid->addWidget(new QLabel(QStringLiteral("Windows and buttons:")), 0, 0);
    grid->addWidget(m_style, 1, 0);
    grid->addWidget(new QLabel(QStringLiteral("Color scheme:")), 2, 0);
    grid->addWidget(m_colour, 3, 0);
    grid->addWidget(new QLabel(QStringLiteral("Font size:")), 4, 0);
    grid->addWidget(m_size, 5, 0);
    grid->addWidget(m_extras, 3, 2);
    grid->addWidget(m_advanced, 5, 2);
    grid->setColumnStretch(0, 5);
    grid->setColumnMinimumWidth(1, 40);
    layout->addLayout(grid);
    layout->addStretch(1);
    connect(m_style, &QComboBox::activated, this, [this] {
        fillColours();
        fillSizes();
        appearanceChanged();
    });
    connect(m_colour, &QComboBox::activated, this, [this] {
        fillSizes();
        appearanceChanged();
    });
    connect(m_size, &QComboBox::activated, this, &DisplayProperties::appearanceChanged);
    connect(m_advanced, &QPushButton::clicked, this, &DisplayProperties::openAdvanced);
    connect(m_extras, &QPushButton::clicked, this, &DisplayProperties::openExtras);
    return page;
}

QWidget *DisplayProperties::pointersTab()
{
    // XP's Mouse Properties > Pointers, the parts that mean something here.
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *top = new QHBoxLayout;
    auto *schemeBox = new QGroupBox(QStringLiteral("Scheme"));
    auto *schemeRow = new QVBoxLayout(schemeBox);
    m_pointerScheme = new QComboBox;
    schemeRow->addWidget(m_pointerScheme);
    top->addWidget(schemeBox, 1);
    m_pointerPreview = new QLabel;
    m_pointerPreview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_pointerPreview->setFixedSize(56, 56);
    m_pointerPreview->setAlignment(Qt::AlignCenter);
    m_pointerPreview->setAutoFillBackground(true);
    m_pointerPreview->setBackgroundRole(QPalette::Base);
    top->addWidget(m_pointerPreview, 0, Qt::AlignBottom);
    layout->addLayout(top);
    layout->addWidget(new QLabel(QStringLiteral("Customize:")));
    m_pointerList = new QListWidget;
    m_pointerList->setIconSize(QSize(32, 32));
    layout->addWidget(m_pointerList, 1);
    auto *row = new QHBoxLayout;
    m_pointerShadowBox = new QCheckBox(QStringLiteral("Enable pointer shadow"));
    m_pointerDefault = new QPushButton(QStringLiteral("Use Default"));
    m_pointerBrowse = new QPushButton(QStringLiteral("Browse..."));
    row->addWidget(m_pointerShadowBox);
    row->addStretch(1);
    row->addWidget(m_pointerDefault);
    row->addWidget(m_pointerBrowse);
    layout->addLayout(row);
    m_pointerTimer = new QTimer(this);
    m_pointerTimer->setSingleShot(true);
    connect(m_pointerTimer, &QTimer::timeout, this, [this] {
        ++m_previewFrame;
        previewPointer();
    });
    connect(m_pointerScheme, &QComboBox::activated, this, [this](int index) {
        const QString name = m_pointerScheme->itemData(index).toString();
        m_pointers.reset();
        for (const pointers::Scheme &s : std::as_const(m_pointerSchemes))
            if (s.name == name)
                m_pointers = s;
        showPointers();
        pointersChanged();
    });
    connect(m_pointerList, &QListWidget::currentRowChanged, this, [this] {
        m_previewFrame = 0;
        previewPointer();
    });
    connect(m_pointerShadowBox, &QCheckBox::toggled, this, [this](bool on) {
        m_pointerShadow = on;
        showPointers();
        pointersChanged();
    });
    connect(m_pointerDefault, &QPushButton::clicked, this, [this] {
        const int role = m_pointerList->currentRow();
        if (!m_pointers || role < 0)
            return;
        m_pointers->files[role].clear();
        showPointers();
        pointersChanged();
    });
    connect(m_pointerBrowse, &QPushButton::clicked, this, [this] {
        const int role = m_pointerList->currentRow();
        if (!m_pointers || role < 0)
            return;
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Browse"), paths::home(),
                                                          QStringLiteral("Cursors (*.cur *.ani)"));
        if (file.isEmpty())
            return;
        m_pointers->files[role] = file;
        showPointers();
        pointersChanged();
    });
    return page;
}

void DisplayProperties::fillPointerSchemes()
{
    m_pointerScheme->clear();
    m_pointerScheme->addItem(QStringLiteral("(None)"), QString());
    for (const pointers::Scheme &s : std::as_const(m_pointerSchemes))
        m_pointerScheme->addItem(s.name, s.name);
    m_pointerScheme->setCurrentIndex(qMax(0, m_pointerScheme->findData(m_pointers ? m_pointers->name : QString())));
    m_pointerScheme->setEnabled(!m_pointerSchemes.isEmpty());
    if (m_pointerSchemes.isEmpty())
        m_pointerScheme->setToolTip(QStringLiteral("XP's pointers come from its install media: set it up under Themes > More themes."));
}

QList<CursorFrame> DisplayProperties::pointerFrames(int role)
{
    const pointers::Scheme scheme = m_pointers.value_or(pointers::defaultScheme());
    const QString file = scheme.files.value(role);
    const QString key = (file.isEmpty() ? QStringLiteral("default:%1").arg(role) : file) + (m_pointerShadow ? QStringLiteral("+shadow") : QString());
    auto it = m_cursorCache.find(key);
    if (it == m_cursorCache.end()) {
        QList<CursorFrame> frames = pointers::frames(scheme, role);
        if (m_pointerShadow)
            frames = pointers::withShadow(frames);
        it = m_cursorCache.insert(key, frames);
    }
    return *it;
}

void DisplayProperties::showPointers()
{
    // "(None)" shows Windows Default's, unchosen: Plasma's own pointers stay.
    const int current = qMax(0, m_pointerList->currentRow());
    m_pointerList->clear();
    m_pointerShadowBox->setChecked(m_pointerShadow);
    if (m_pointerSchemes.isEmpty()) {
        m_pointerList->setEnabled(false);
        m_pointerShadowBox->setEnabled(false);
        m_pointerDefault->setEnabled(false);
        m_pointerBrowse->setEnabled(false);
        return;
    }
    for (int role = 0; role < pointers::RoleCount; ++role) {
        const QList<CursorFrame> frames = pointerFrames(role);
        auto *item = new QListWidgetItem(pointers::roleName(role));
        if (!frames.isEmpty())
            item->setIcon(QIcon(QPixmap::fromImage(frames.first().image)));
        m_pointerList->addItem(item);
    }
    m_pointerList->setEnabled(m_pointers.has_value());
    m_pointerShadowBox->setEnabled(m_pointers.has_value());
    m_pointerDefault->setEnabled(m_pointers.has_value());
    m_pointerBrowse->setEnabled(m_pointers.has_value());
    m_pointerList->setCurrentRow(current);
    m_previewFrame = 0;
    previewPointer();
}

void DisplayProperties::previewPointer()
{
    // The chosen pointer, animated as it would be.
    m_pointerTimer->stop();
    const int role = m_pointerList->currentRow();
    const QList<CursorFrame> frames = role >= 0 ? pointerFrames(role) : QList<CursorFrame>();
    if (frames.isEmpty()) {
        m_pointerPreview->clear();
        return;
    }
    const CursorFrame &f = frames.at(m_previewFrame % frames.size());
    m_pointerPreview->setPixmap(QPixmap::fromImage(f.image));
    if (frames.size() > 1)
        m_pointerTimer->start(qMax(16, f.delayMs));
}

void DisplayProperties::pointersChanged()
{
    if (m_loading)
        return;
    updateApply();
}

bool DisplayProperties::pointersDiffer() const
{
    if (!m_pointers)
        return m_appliedPointers.has_value();
    return !m_appliedPointers || !(m_appliedPointers->first == *m_pointers) || m_appliedPointers->second != m_pointerShadow;
}

// --- the library ---------------------------------------------------------------------------

void DisplayProperties::loadLibrary()
{
    m_library = Library::scan();
    m_styles.clear();
    for (const StyleInfo &s : std::as_const(m_library.styles))
        if (s.readable())
            m_styles.append(s);
    fillThemes();
}

void DisplayProperties::fillThemes()
{
    m_themes->clear();
    for (const ThemeBundle &t : std::as_const(m_library.themes))
        if (t.styleMissing.isEmpty())
            m_themes->addItem(t.label, t.path);
    m_themes->insertSeparator(m_themes->count());
    m_themes->addItem(QStringLiteral("More themes..."), QLatin1String(kSetup));
    m_themes->addItem(QStringLiteral("Browse..."), QLatin1String(kBrowse));
}

void DisplayProperties::fillStyles()
{
    m_style->clear();
    for (const StyleInfo &s : std::as_const(m_styles))
        m_style->addItem(s.label, s.path);
    m_style->addItem(QStringLiteral("Windows Classic style"));
}

void DisplayProperties::fillColours()
{
    const QString previous = m_colour->currentData().toString();
    m_colour->clear();
    const int i = m_style->currentIndex();
    if (i >= 0 && i < m_styles.size()) {
        for (const StyleOption &c : m_styles.at(i).colours)
            m_colour->addItem(c.label, c.key);
    } else {
        // Classic: the stock and saved schemes (a size is the Font size's).
        const QString base = schemes::split(m_scheme.name).first;
        for (const QString &name : schemes::bases())
            m_colour->addItem(name, name);
        if (m_colour->findData(base) < 0)
            m_colour->insertItem(0, base, base);
        m_colour->setCurrentIndex(m_colour->findData(base));
        return;
    }
    const int keep = m_colour->findData(previous);
    m_colour->setCurrentIndex(keep >= 0 ? keep : 0);
}

void DisplayProperties::fillSizes()
{
    const QString previous = m_size->currentData().toString();
    m_size->clear();
    const int i = m_style->currentIndex();
    if (i >= 0 && i < m_styles.size()) {
        for (const StyleOption &s : m_styles.at(i).sizesFor(m_colour->currentData().toString()))
            m_size->addItem(s.label, s.key);
    } else {
        const QString base = m_colour->currentData().toString();
        for (const FontSize &s : fontSizes())
            if (schemes::sizeAvailable(base, QLatin1String(s.key)))
                m_size->addItem(QLatin1String(s.label), QLatin1String(s.key));
        const int keep = m_size->findData(schemes::split(m_scheme.name).second);
        m_size->setCurrentIndex(keep >= 0 ? keep : 0);
        return;
    }
    const int keep = m_size->findData(previous.isEmpty() ? QStringLiteral("NORMAL") : previous);
    m_size->setCurrentIndex(keep >= 0 ? keep : 0);
}

void DisplayProperties::fillWallpapers()
{
    m_wallpapers->clear();
    auto *none = new QListWidgetItem(QStringLiteral("(None)"));
    m_wallpapers->addItem(none);
    QFileInfoList files = QDir(paths::wallpapers()).entryInfoList({QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
                                                                    QStringLiteral("*.bmp"), QStringLiteral("*.gif")},
                                                                   QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &f : std::as_const(files)) {
        auto *item = new QListWidgetItem(f.completeBaseName());
        item->setData(Qt::UserRole, f.absoluteFilePath());
        m_wallpapers->addItem(item);
    }
}

// --- choices ----------------------------------------------------------------------------------

Look DisplayProperties::look() const
{
    const int i = m_style->currentIndex();
    if (i >= 0 && i < m_styles.size()) {
        const StyleInfo &s = m_styles.at(i);
        const QString ini = s.ini(m_colour->currentData().toString(), m_size->currentData().toString());
        if (auto look = Look::fromStyle(s.path, ini))
            return *look;
    }
    return Look::fromScheme(m_scheme);
}

Wallpaper DisplayProperties::wallpaper() const
{
    Wallpaper w;
    if (QListWidgetItem *item = m_wallpapers->currentItem())
        w.image = item->data(Qt::UserRole).toString();
    w.fill = m_position->currentData().toInt();
    w.colour = m_desktopColour->colour();
    return w;
}

void DisplayProperties::themePicked(int index)
{
    const QString data = m_themes->itemData(index).toString();
    if (data == QLatin1String(kSetup)) {
        SetupDialog dialog(this);
        dialog.exec();
        if (dialog.changedLibrary()) {
            loadLibrary();
            fillStyles();
            fillColours();
            fillSizes();
            fillWallpapers();
        }
        return;
    }
    if (data == QLatin1String(kBrowse)) {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Open Theme"), paths::themes(), QStringLiteral("Themes (*.theme)"));
        if (file.isEmpty())
            return;
        const ThemeBundle bundle = Library::bundle(file);
        if (!bundle.styleMissing.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Open Theme"), QStringLiteral("%1 names a visual style that isn't here (%2).")
                                                                         .arg(QFileInfo(file).fileName(), bundle.styleMissing));
            return;
        }
        m_themes->insertItem(0, bundle.label, bundle.path);
        m_themes->setCurrentIndex(0);
        showBundle(bundle);
        return;
    }
    for (const ThemeBundle &b : std::as_const(m_library.themes))
        if (b.path == data)
            showBundle(b);
}

void DisplayProperties::showBundle(const ThemeBundle &b)
{
    m_loading = true;
    m_themeFile = b.path;
    const auto file = ThemeFile::load(b.path);
    const ClassicScheme colours = file ? ClassicScheme::fromThemeFile(*file) : ClassicScheme();
    if (b.classic()) {
        m_scheme = colours;
        m_scheme.name = b.label;
        m_style->setCurrentIndex(m_styles.size());
        fillColours();
        fillSizes();
    } else {
        for (int i = 0; i < m_styles.size(); ++i)
            if (QFileInfo(m_styles.at(i).path).canonicalFilePath() == QFileInfo(b.style).canonicalFilePath())
                m_style->setCurrentIndex(i);
        fillColours();
        const StyleInfo &s = m_styles.value(m_style->currentIndex());
        const StyleVariant *v = s.variant(b.variant.isEmpty() ? s.defaultVariant : b.variant);
        if (v)
            m_colour->setCurrentIndex(m_colour->findData(v->colour));
        fillSizes();
        if (v)
            m_size->setCurrentIndex(m_size->findData(v->size));
    }
    // The desktop the theme brings.
    int row = 0;
    for (int i = 0; i < m_wallpapers->count(); ++i)
        if (!b.wallpaper.isEmpty() && QFileInfo(m_wallpapers->item(i)->data(Qt::UserRole).toString()) == QFileInfo(b.wallpaper))
            row = i;
    if (row == 0 && !b.wallpaper.isEmpty()) {
        auto *item = new QListWidgetItem(QFileInfo(b.wallpaper).completeBaseName());
        item->setData(Qt::UserRole, b.wallpaper);
        m_wallpapers->addItem(item);
        row = m_wallpapers->count() - 1;
    }
    m_wallpapers->setCurrentRow(row);
    for (int i = 0; i < m_position->count(); ++i)
        if (m_position->itemData(i).toInt() == b.fill.value_or(0))
            m_position->setCurrentIndex(i);
    m_desktopColour->setColour(colours.colour(u"Background"));
    m_loading = false;
    m_delete->setEnabled(presets::owned(b.path));
    renderThemes();
    renderDesktop();
    renderAppearance();
    updateApply();
}

void DisplayProperties::appearanceChanged()
{
    if (m_loading)
        return;
    const int i = m_style->currentIndex();
    if (i >= m_styles.size()) {
        // A Classic scheme picked by name and size.
        const QString base = m_colour->currentData().toString();
        const QString sizeKey = m_size->currentData().toString();
        QString name = base;
        for (const FontSize &s : fontSizes())
            if (sizeKey == QLatin1String(s.key))
                name += QLatin1String(s.suffix);
        if (const auto s = schemes::load(name))
            m_scheme = *s;
    }
    m_advanced->setEnabled(i >= m_styles.size());
    m_themeFile.clear();
    renderAppearance();
    renderThemes();
    updateApply();
}

void DisplayProperties::desktopChanged()
{
    if (m_loading)
        return;
    m_themeFile.clear();
    renderDesktop();
    renderThemes();
    updateApply();
}

void DisplayProperties::openExtras()
{
    ExtrasDialog dialog(look(), m_startExtras, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_startExtras = dialog.extras();
    renderThemes();
    updateApply();
}

void DisplayProperties::openAdvanced()
{
    AdvancedDialog dialog(m_scheme, &m_previewer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_scheme = dialog.scheme();
    m_themeFile.clear();
    m_loading = true;
    fillColours();
    fillSizes();
    m_loading = false;
    renderAppearance();
    renderThemes();
    updateApply();
}

void DisplayProperties::saveAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save Theme"), QStringLiteral("Save this theme as:"), QLineEdit::Normal,
                                               m_themes->currentText(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    presets::Preset p;
    p.name = name;
    const Look l = look();
    if (!l.classic()) {
        p.style = l.stylePath;
        const StyleInfo &s = m_styles.at(m_style->currentIndex());
        if (const StyleVariant *v = s.variant(l.variant)) {
            for (const StyleOption &c : s.colours)
                if (c.key == v->colour)
                    p.colorStyle = c.name;
            for (const StyleOption &z : s.sizes)
                if (z.key == v->size)
                    p.size = z.name;
        }
    } else {
        p.scheme = m_scheme;
    }
    const Wallpaper w = wallpaper();
    p.desktop = presets::Desktop{w.image, w.fill.value_or(2), w.colour};
    if (m_themeFile.endsWith(QLatin1String(".theme"), Qt::CaseInsensitive))
        p.base = m_themeFile;
    QString path, error;
    if (!presets::save(p, &path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Save Theme"), error);
        return;
    }
    m_library = Library::scan();
    fillThemes();
    m_themes->setCurrentIndex(m_themes->findData(path));
    m_themeFile = path;
    m_delete->setEnabled(true);
    updateApply();
}

void DisplayProperties::deleteTheme()
{
    const QString path = m_themes->currentData().toString();
    if (!presets::owned(path)) {
        QMessageBox::information(this, QStringLiteral("Delete"),
                                 QStringLiteral("Only themes saved here can be deleted; XP's own and your collection's are left alone."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Delete"), QStringLiteral("Delete the saved theme \"%1\"?").arg(m_themes->currentText()))
        != QMessageBox::Yes)
        return;
    QFile::remove(path);
    m_library = Library::scan();
    fillThemes();
}

bool DisplayProperties::apply()
{
    const Look l = look();
    const Wallpaper desktop = wallpaper();
    // Shift: everything, as if nothing were applied.
    const bool everything = shiftHeld();
    const bool lookChanged = everything || lookKey(l) != m_appliedLook;
    const bool desktopChanged = everything || !sameDesktop(desktop, m_appliedDesktop);
    const bool pointersChanged = pointersDiffer() || (everything && m_pointers);
    const bool startChanged = m_startExtras != m_appliedStartExtras;
    if (!lookChanged && !desktopChanged && !pointersChanged && !startChanged && m_themeFile == m_appliedThemeFile)
        return m_sounds->apply();

    QString error;
    bool ok = true;
    if (startChanged) {
        // (Kept first: a look applied now draws its Start button with them.)
        State state = State::load();
        if (m_startExtras.isDefault())
            state.settings.remove(QLatin1String(kStartButtonKey));
        else
            state.settings.insert(QLatin1String(kStartButtonKey), m_startExtras.toJson());
        state.save();
    }
    if (lookChanged) {
        ApplyOptions o;
        if (desktopChanged)
            o.wallpaper = desktop;
        o.themeFile = m_themeFile;
        ok = runJob(this, QStringLiteral("Applying %1").arg(l.name), [&](const JobContext &c, QString *e) {
            ApplyOptions options = o;
            options.progress = c.say;
            LiveSession session;
            return applyLook(session, l, options, e);
        }, &error);
    } else if (desktopChanged || m_themeFile != m_appliedThemeFile) {
        if (desktopChanged) {
            LiveSession session;
            applyWallpaper(session, desktop);
        }
        // (The same look, now from this theme: only the record changes.)
        State state = State::load();
        state.themeFile = m_themeFile;
        state.save();
    }
    if (startChanged && !lookChanged) {
        ok = runJob(this, QStringLiteral("Applying the Start button"), [&](const JobContext &, QString *e) {
            LiveSession session;
            return applyStartButton(session, l, e);
        }, &error);
    }
    if (ok && startChanged)
        m_appliedStartExtras = m_startExtras;
    if (ok) {
        if (lookChanged) {
            m_appliedLook = lookKey(l);
            m_outdated = false;
        }
        m_appliedDesktop = desktop;
        m_appliedThemeFile = m_themeFile;
    }
    if (ok && pointersChanged) {
        LiveSession session;
        State state = State::load();
        if (m_pointers)
            ok = pointers::apply(session, state, *m_pointers, m_pointerShadow, &error);
        else
            pointers::revert(session, state);
        if (ok) {
            state.save();
            m_appliedPointers.reset();
            if (m_pointers)
                m_appliedPointers = std::pair(*m_pointers, m_pointerShadow);
        }
    }
    if (!ok)
        QMessageBox::warning(this, QStringLiteral("Apply"), error);
    // (after the look, which may have put Windows Default on itself)
    if (ok)
        ok = m_sounds->apply();
    updateApply();
    return ok;
}

void DisplayProperties::updateApply()
{
    m_apply->setToolTip(m_outdated ? QStringLiteral("XPlasma has been updated since this look was applied. Apply rebuilds it with this "
                                                    "version.\nHold Shift to rebuild and apply everything, changed or not.")
                                   : QStringLiteral("Hold Shift to rebuild and apply everything, changed or not."));
    m_apply->setEnabled(shiftHeld() || lookKey(look()) != m_appliedLook || !sameDesktop(wallpaper(), m_appliedDesktop)
                        || m_themeFile != m_appliedThemeFile || pointersDiffer() || m_startExtras != m_appliedStartExtras
                        || m_sounds->pending());
}

bool DisplayProperties::eventFilter(QObject *watched, QEvent *event)
{
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Shift && isActiveWindow())
        QTimer::singleShot(0, this, &DisplayProperties::updateApply);
    return QDialog::eventFilter(watched, event);
}

// --- samples -----------------------------------------------------------------------------------

void DisplayProperties::renderThemes()
{
    if (m_loading)
        return;
    m_themeSample->setPixmap(QPixmap::fromImage(m_previewer.theme(look(), wallpaper(), preview::kThemesSize, m_startExtras)));
}

void DisplayProperties::renderDesktop()
{
    if (m_loading)
        return;
    m_desktopSample->setPixmap(QPixmap::fromImage(m_previewer.desktop(wallpaper(), QSize(200, 170))));
}

void DisplayProperties::renderAppearance()
{
    if (m_loading)
        return;
    m_advanced->setEnabled(m_style->currentIndex() >= m_styles.size());
    const Look l = look();
    m_appearanceSample->setPixmap(QPixmap::fromImage(m_previewer.appearance(l, preview::kAppearanceSize, l.system.colour(u"Background"))));
}

void DisplayProperties::showTab(const QString &name)
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabText(i).remove(QLatin1Char('&')).compare(name, Qt::CaseInsensitive) == 0)
            m_tabs->setCurrentIndex(i);
}

bool DisplayProperties::saveScreenshots(const QString &dir)
{
    QDir().mkpath(dir);
    setAttribute(Qt::WA_DontShowOnScreen);
    show();
    bool ok = true;
    m_sounds->showForScreenshot();
    const char *const names[] = {"themes", "desktop", "appearance", "pointers", "sounds", "about"};
    for (int i = 0; i < m_tabs->count(); ++i) {
        m_tabs->setCurrentIndex(i);
        QApplication::processEvents();
        ok &= grab().save(QDir(dir).filePath(QLatin1String(names[i]) + QLatin1String(".png")));
    }
    AdvancedDialog advanced(m_scheme, &m_previewer, this);
    advanced.setAttribute(Qt::WA_DontShowOnScreen);
    advanced.show();
    QApplication::processEvents();
    ok &= advanced.grab().save(QDir(dir).filePath(QStringLiteral("advanced.png")));
    ExtrasDialog extras(look(), m_startExtras, this);
    extras.setAttribute(Qt::WA_DontShowOnScreen);
    extras.show();
    QApplication::processEvents();
    ok &= extras.grab().save(QDir(dir).filePath(QStringLiteral("extras.png")));
    return ok;
}

} // namespace xpl::editor


