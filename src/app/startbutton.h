#pragma once
// The Start button, as XP's explorer draws it: a push button themed
// Start::Button (normal, hot, pressed), XP's flag (explorer.exe's bitmap
// 143) at the left of its content, and "start" beside it in the style's
// Start font, colour and shadow. Classic draws the same in a push bevel.
//
// Built without the flag is an error, never a quiet flagless pill.
#include "app/look.h"

#include <QImage>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace xpl {

struct StartButtonArt
{
    QImage normal, hover, pressed;   // the bar's height; transparent round the button
};

// What Display Properties' Extras... makes of the Start button: its label
// and its logo, in place of XP's "start" and flag. Empty is XP's. Kept in
// State's settings under kStartButtonKey.
struct StartButtonExtras
{
    QString label;
    QString logoText;   // characters (an emoji) drawn as the logo, or...
    QString logoFile;   // ...a picture (this wins); neither: XP's flag

    bool isDefault() const { return label.isEmpty() && logoText.isEmpty() && logoFile.isEmpty(); }
    bool operator==(const StartButtonExtras &o) const { return label == o.label && logoText == o.logoText && logoFile == o.logoFile; }
    bool operator!=(const StartButtonExtras &o) const { return !(*this == o); }
    static StartButtonExtras fromJson(const QJsonObject &object);
    QJsonObject toJson() const;
};
constexpr const char *kStartButtonKey = "startButton";

// `flag` is the flag bitmap (media::startFlag(), or classicStartFlag() for
// Windows Classic); `label` what it says ("start" with a visual style,
// "Start" in Classic).
std::optional<StartButtonArt> startButton(const Look &look, const QImage &flag, const QString &label = QStringLiteral("start"),
                                          QString *error = nullptr);
// With the user's extras: their label, and their logo the flag's height
// (characters in the label's colour, or their own where the font has
// colours; a picture scaled down) -- XP's where they say nothing.
std::optional<StartButtonArt> startButton(const Look &look, const QImage &flag, const StartButtonExtras &extras,
                                          QString *error = nullptr);

} // namespace xpl
