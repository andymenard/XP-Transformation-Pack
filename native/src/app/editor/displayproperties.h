// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// xplasma-edit: XPlasma Properties -- Windows XP's Display Properties, for
// Plasma, its Themes, Desktop and Appearance tabs, Mouse Properties'
// Pointers, Sounds and Audio Devices' Sounds (soundstab.h), and About
// (abouttab.h) -- with OK,
// Cancel and Apply; Apply does what `xplasma apply` does, for what changed
// since the last Apply: only the desktop's wallpaper, say, is set without
// rebuilding the look. Apply is greyed out until something changes; with
// Shift held it's available anyway and rebuilds everything.
//
// The dialog is drawn in whatever widget style the system has (never forced
// to XPlasma); only its samples are drawn by XPlasma, in the look they show.
#include "app/apply.h"
#include "app/editor/preview.h"
#include "app/library.h"
#include "app/pointers.h"
#include "app/startbutton.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QTimer;
class QLabel;
class QListWidget;
class QPushButton;
class QTabWidget;

namespace xpl::editor {

class ColourButton;
class SoundsTab;

class DisplayProperties : public QDialog
{
    Q_OBJECT
public:
    explicit DisplayProperties(QWidget *parent = nullptr);

    // Every tab (and the Advanced dialog) rendered to DIR as PNGs.
    bool saveScreenshots(const QString &dir);

    // The tab of that name shown ("Sounds").
    void showTab(const QString &name);

    // What the dialog would apply.
    Look look() const;
    Wallpaper wallpaper() const;

private:
    QWidget *themesTab();
    QWidget *desktopTab();
    QWidget *appearanceTab();
    QWidget *pointersTab();
    QWidget *customLunaTab();
    void syncCustomColors();
    QLabel *m_customBaseLabel;
    QLabel *m_customColorLabels[4];
    void customLunaChanged();
    bool customLunaPruned() const;
    void scheduleCustomColors();
    QJsonObject liveStartColors() const;
    void liveStartColorsChanged();
    QCheckBox *m_liveStartEnabled;
    QCheckBox *m_linkTitleColor;
    bool m_appliedTitleLink = true;
    void syncTitleColorLink();
    QTimer *m_liveStartTimer;
    QJsonObject m_appliedLiveStart;
    bool m_livePreviewTouched = false;
    std::array<QColor,4> m_beforeLiveColors;
    std::array<int,4> m_beforeLiveTextModes{};
    void restoreCustomLuna(const QString &path = QString());
    QCheckBox *m_customLunaEnabled;
    QComboBox *m_customTextModes[4];
    QComboBox *m_startMenuTextMode;
    QLabel *m_customLunaSample, *m_customLunaStatus;
    ColourButton *m_lunaColours[4];
    QString m_customLunaPath, m_customLunaSource, m_customLunaVariant;
    std::optional<ClassicScheme> m_customClassic;

    void loadLibrary();
    void fillThemes();
    void fillStyles();
    void fillColours();
    void fillSizes();
    void fillWallpapers();

    void themePicked(int index);
    void installTheme(bool folder);
    void showBundle(const ThemeBundle &bundle);
    void appearanceChanged();
    void desktopChanged();
    void openAdvanced();
    void openExtras();
    void showStartLogos();
    void saveAs();
    void deleteTheme();
    bool apply();
    void updateApply();
    bool eventFilter(QObject *watched, QEvent *event) override;

    void renderThemes();
    void renderDesktop();
    void renderAppearance();
    // Pointers: the scheme's roles listed, the chosen one previewed.
    void fillPointerSchemes();
    void showPointers();
    void previewPointer();
    void pointersChanged();
    bool pointersDiffer() const;
    QList<CursorFrame> pointerFrames(int role);

    Library m_library;
    QList<StyleInfo> m_styles;        // readable ones, in the order shown
    // The Windows and buttons list: a row per style, but for XP's Royale and
    // Microsoft's later colourings of it, separate styles listed as one row's
    // colour schemes. A colour is its m_styles entry and that style's colour.
    struct StyleRow { QString label; QList<std::pair<int, StyleOption>> colours; };
    QList<StyleRow> m_styleRows;
    int styleIndex() const;           // the m_styles entry chosen; -1 for Classic
    QString colourKey() const;        // its colour
    bool classicChosen() const;
    int styleFor(const QString &path) const;
    void selectStyle(int index, const QString &colour);
    ClassicScheme m_scheme;           // Windows Classic's, as edited
    QString m_themeFile;              // the bundle chosen, unless edited since
    preview::Previewer m_previewer;
    bool m_loading = false;
    bool m_customColorsPending = false;
    // What's on the desktop now, as of the last Apply (or opening).
    QString m_appliedLook;            // lookKey(); empty: unknown
    bool m_outdated = false;          // applied by an older XPlasma (apply.h's lookOutdated)
    Wallpaper m_appliedDesktop;
    QString m_appliedThemeFile;
    // Pointers: the scheme as edited (nullopt: "(None)", Plasma's own),
    // and what's applied.
    QList<pointers::Scheme> m_pointerSchemes;
    std::optional<pointers::Scheme> m_pointers;
    bool m_pointerShadow = true;
    std::optional<std::pair<pointers::Scheme, bool>> m_appliedPointers;
    // The Start button's extras (label, logo) as edited, and as applied.
    StartButtonExtras m_startExtras, m_appliedStartExtras;
    QHash<QString, QList<CursorFrame>> m_cursorCache;   // file ("" + role: the default) -> frames
    int m_previewFrame = 0;

    QTabWidget *m_tabs;
    QPushButton *m_apply;
    // Themes
    QComboBox *m_themes;
    QPushButton *m_delete;
    QLabel *m_themeSample;
    // Desktop
    QLabel *m_desktopSample;
    QListWidget *m_wallpapers;
    QComboBox *m_position;
    ColourButton *m_desktopColour;
    // Appearance
    QLabel *m_appearanceSample;
    QComboBox *m_style, *m_colour, *m_size;
    QPushButton *m_advanced, *m_extras;
    bool m_appliedDark = false;
    bool m_appliedCrisp = false;
    QCheckBox *m_crispText;
    bool m_appliedHiDpiStart = false;
    QCheckBox *m_hidpiStart;
    QCheckBox *m_themeCache;
    bool m_appliedThemeCache = false;
    bool m_themeCacheChosen = false;
    QCheckBox *m_darkMode;
    QCheckBox *m_kdeLogo;
    QCheckBox *m_tux;   // Tux for the Start flag: a shortcut to Extras' picture
    // Pointers
    QComboBox *m_pointerScheme;
    QListWidget *m_pointerList;
    QLabel *m_pointerPreview;
    QCheckBox *m_pointerShadowBox;
    QPushButton *m_pointerDefault, *m_pointerBrowse;
    QTimer *m_pointerTimer;
    // Sounds
    SoundsTab *m_sounds;
};

} // namespace xpl::editor
