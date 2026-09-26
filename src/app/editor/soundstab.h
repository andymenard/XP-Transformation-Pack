// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// XPlasma Properties' Sounds tab: Windows XP's, from Sounds and Audio
// Devices Properties -- XP's dialog template's words and order (mmsys.cpl's
// "Events Schemes"), laid out to the tab's width; its bitmaps and icons taken
// from the XP media. (XP's Volume tab isn't here: Plasma's audio settings
// do all it did.)
//
//   Sound scheme     Windows Default, No Sounds and the user's own; changing
//                    an event's sound makes it "(modified)", for Save As...
//   Program events   XP's events Plasma has a sound for (sounds.h), a
//                    speaker by each that has one
//   Sounds           WINDOWS\Media's sounds, (None), or a file of one's own
//                    (Browse...); the play button plays it, and stops it
//
// The dialog's Apply makes the scheme Plasma's sound theme (sounds::apply).
// Opened with nothing applied, it shows Windows Default, to be applied; with
// sounds turned off (`xplasma sounds off`), No Sounds, as it is.
#include "app/sounds.h"

#include <QIcon>
#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QProcess;
class QPushButton;
class QTreeWidget;

namespace xpl::editor {

class SoundsTab : public QWidget
{
    Q_OBJECT
public:
    explicit SoundsTab(QWidget *parent = nullptr);
    ~SoundsTab() override;

    // Whether what's shown isn't what's applied.
    bool pending() const;
    // What's shown applied; false, having said why, if it couldn't be.
    bool apply();
    // Windows Default shown, Asterisk picked (tests' screenshots).
    void showForScreenshot();

Q_SIGNALS:
    // What's shown changed (for the dialog's Apply).
    void changed();

private:
    void build();
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

    QList<sounds::Scheme> allSchemes() const;
    std::optional<sounds::Scheme> named(const QString &name) const;
    bool modified() const;

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
    bool m_off = false;                         // turned off, nothing applied
    QList<sounds::Scheme> m_saved;
    QStringList m_browsed;                      // files picked with Browse..., for the list
    bool m_loading = false;
};

} // namespace xpl::editor
