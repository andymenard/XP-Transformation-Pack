#pragma once
// xplasma-edit --sounds: Windows XP's Sounds and Audio Devices Properties,
// its Volume and Sounds tabs -- laid out from XP's own dialog templates
// (mmsys.cpl's "Volume" and "Events Schemes", in dialog units), its words,
// its bitmaps and icons taken from the XP media.
//
//   Volume           the default output's name, its volume (Low to High) and
//                    mute, as they change; the volume icon in the taskbar
//                    (Plasma's tray), on Apply; Advanced... and Speaker
//                    Volume... open Plasma's audio settings
//   Sound scheme     Windows Default, No Sounds and the user's own; changing
//                    an event's sound makes it "(modified)", for Save As...
//   Program events   XP's events Plasma has a sound for (sounds.h), a
//                    speaker by each that has one
//   Sounds           WINDOWS\Media's sounds, (None), or a file of one's own
//                    (Browse...); the play button plays it, and stops it
//
// Apply makes the scheme Plasma's sound theme (sounds::apply). Opened with
// nothing applied, it shows Windows Default with Apply ready.
#include "app/sounds.h"

#include <QDialog>
#include <QIcon>

#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QTabWidget;
class QProcess;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace xpl::editor {

class SoundsProperties : public QDialog
{
    Q_OBJECT
public:
    explicit SoundsProperties(QWidget *parent = nullptr);
    ~SoundsProperties() override;

    // The tabs rendered to DIR/volume.png and sounds.png (tests; Windows
    // Default, Asterisk picked).
    bool saveScreenshots(const QString &dir);

private:
    QWidget *volumeTab();
    QWidget *soundsTab();
    QRect du(int x, int y, int w, int h) const;

    // The scheme combo's entries: XP's, the user's, and "(modified)" when
    // the sounds shown are no scheme's.
    void fillSchemes();
    void schemePicked(int index);
    void showEvents();
    void eventPicked();
    void soundPicked(int index);
    void fillSounds();
    QString currentEvent() const;
    void play();
    void stop();
    void saveAs();
    void deleteScheme();
    void updateApply();
    bool apply();

    QList<sounds::Scheme> allSchemes() const;
    std::optional<sounds::Scheme> named(const QString &name) const;
    bool modified() const;

    QTabWidget *m_tabs = nullptr;
    QPushButton *m_apply = nullptr;
    // Volume
    QSlider *m_volume = nullptr;
    QCheckBox *m_mute = nullptr;
    QCheckBox *m_trayIcon = nullptr;
    std::optional<bool> m_appliedTrayIcon;
    // Sounds
    QComboBox *m_schemes = nullptr;
    QPushButton *m_saveAs = nullptr;
    QPushButton *m_delete = nullptr;
    QTreeWidget *m_events = nullptr;
    QLabel *m_soundsLabel = nullptr;
    QComboBox *m_sounds = nullptr;
    QPushButton *m_play = nullptr;
    QPushButton *m_browse = nullptr;
    QProcess *m_player = nullptr;
    QIcon m_playIcon, m_stopIcon, m_speaker;

    sounds::Scheme m_scheme;                    // what's shown
    std::optional<sounds::Scheme> m_applied;    // what's applied
    QList<sounds::Scheme> m_saved;
    QStringList m_browsed;                      // files picked with Browse..., for the list
    bool m_loading = false;
};

} // namespace xpl::editor
