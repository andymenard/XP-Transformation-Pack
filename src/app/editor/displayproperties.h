#pragma once
// xplasma-edit: Windows XP's Display Properties, for Plasma. Three of XP's
// tabs -- Themes, Desktop, Appearance -- with OK, Cancel and Apply; Apply
// does what `xplasma apply` does.
//
// The dialog is drawn in whatever widget style the system has (never forced
// to XPLasma); only its samples are drawn by XPLasma, in the look they show.
#include "app/apply.h"
#include "app/editor/preview.h"
#include "app/library.h"

#include <QDialog>

class QComboBox;
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

    void renderThemes();
    void renderDesktop();
    void renderAppearance();
    void setStatus(const QString &text);

    Library m_library;
    QList<StyleInfo> m_styles;        // readable ones, in the order shown
    ClassicScheme m_scheme;           // Windows Classic's, as edited
    QString m_themeFile;              // the bundle chosen, unless edited since
    preview::Previewer m_previewer;
    bool m_loading = false;

    QTabWidget *m_tabs;
    QLabel *m_status;
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
};

} // namespace xpl::editor
