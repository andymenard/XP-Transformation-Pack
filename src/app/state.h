#pragma once
// XPlasma's own state, in one versioned file (paths::statePath()): what is
// applied, where the XP media is, and settings. Written atomically; unknown
// keys (a newer XPlasma's) survive a load and save.
#include <QJsonObject>
#include <QString>

namespace xpl {

struct State
{
    static constexpr int kVersion = 1;

    // What the desktop wears now. kind is "style" (a visual style), "classic"
    // (a Windows Classic scheme) or empty (nothing applied by XPlasma).
    QString appliedKind;
    QString style;         // the .msstyles, for kind "style"
    QString variant;       // its layout INI ("NORMALBLUE_INI")
    QString themeFile;     // the .theme the choice came from, if any
    QString scheme;        // the Classic scheme's name, for kind "classic"

    QString media;         // XP install media: an ISO, or an I386 directory's parent

    QJsonObject settings;  // everything else, by key

    static State load();
    // False (with *error) when the file can't be written.
    bool save(QString *error = nullptr) const;

    QJsonObject toJson() const;
    static State fromJson(const QJsonObject &json);

private:
    QJsonObject m_rest;    // keys this version doesn't know
};

} // namespace xpl
