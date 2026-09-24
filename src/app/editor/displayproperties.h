#pragma once
// xplasma-edit: Windows XP's Display Properties, for Plasma. Three of XP's
// tabs -- Themes, Desktop, Appearance -- with OK, Cancel and Apply; Apply
// does what `xplasma apply` does, for what changed since the last Apply:
// only the desktop's wallpaper, say, is set without rebuilding the look.
// Apply is greyed out until something changes; with Shift held it's
// available anyway and rebuilds everything.
//
// The dialog is drawn in whatever widget style the system has (never forced
// to XPLasma); only its samples are drawn by XPLasma, in the look they show.
#include "app/apply.h"
#include "app/editor/preview.h"
#include "app/library.h"
#include "app/pointers.h"

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

class DisplayProperties : public QDialog
{
    Q_OBJECT
public:
    explicit DisplayProperties(QWidget *parent = nullptr);

    // Every tab (and the Advanced dialog) rendered to DIR as PNGs.
    bool saveScreenshots(const QString &dir);

    // What the dialog would apply.
    Look look() const;
    Wallpaper wallpaper() const;

private:
    QWidget *themesTab();
    QWidget *desktopTab();
    QWidget *appearanceTab();
    QWidget *pointersTab();

    void loadLibrary();
    void fillThemes();
    void fillStyles();
    void fillColours();
    void fillSizes();
    void fillWallpapers();

    void themePicked(int index);
    void showBundle(const ThemeBundle &bundle);
    void appearanceChanged();
    void desktopChanged();
    void openAdvanced();
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
    ClassicScheme m_scheme;           // Windows Classic's, as edited
    QString m_themeFile;              // the bundle chosen, unless edited since
    preview::Previewer m_previewer;
    bool m_loading = false;
    // What's on the desktop now, as of the last Apply (or opening).
    QString m_appliedLook;            // lookKey(); empty: unknown
    Wallpaper m_appliedDesktop;
    QString m_appliedThemeFile;
    // Pointers: the scheme as edited (nullopt: "(None)", Plasma's own),
    // and what's applied.
    QList<pointers::Scheme> m_pointerSchemes;
    std::optional<pointers::Scheme> m_pointers;
    bool m_pointerShadow = true;
    std::optional<std::pair<pointers::Scheme, bool>> m_appliedPointers;
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
    QPushButton *m_advanced;
    // Pointers
    QComboBox *m_pointerScheme;
    QListWidget *m_pointerList;
    QLabel *m_pointerPreview;
    QCheckBox *m_pointerShadowBox;
    QPushButton *m_pointerDefault, *m_pointerBrowse;
    QTimer *m_pointerTimer;
};

} // namespace xpl::editor
