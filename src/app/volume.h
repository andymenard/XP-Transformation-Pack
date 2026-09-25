#pragma once
// The computer's sound output, as XP's Sounds and Audio Devices > Volume has
// it: the default output device's name, its volume and mute -- through
// PulseAudio's pactl (PipeWire's too), else WirePlumber's wpctl -- and
// whether the volume icon is in the taskbar (Plasma's system tray's volume
// item, org.kde.plasma.volume).
#include "app/session.h"

#include <QString>

#include <optional>

namespace xpl::volume {

struct Device
{
    QString name;        // "Built-in Audio Analog Stereo"
    int percent = 0;     // 0-100 (the loudest channel)
    bool muted = false;
};

// The default output; none without one (or only PipeWire's stand-in, which
// plays to nowhere).
std::optional<Device> defaultOutput();
bool setPercent(int percent);
bool setMuted(bool muted);

// Whether the tray shows the volume icon; none without a tray.
std::optional<bool> trayIcon(Session &session);
bool setTrayIcon(Session &session, bool shown);

} // namespace xpl::volume
