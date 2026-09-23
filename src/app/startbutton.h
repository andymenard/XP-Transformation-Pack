#pragma once
// The Start button, as XP's explorer draws it: a push button themed
// Start::Button (normal, hot, pressed), XP's flag (explorer.exe's bitmap
// 143) at the left of its content, and "start" beside it in the style's
// Start font, colour and shadow. Classic draws the same in a push bevel.
//
// Built without the flag is an error, never a quiet flagless pill.
#include "app/look.h"

#include <QImage>
#include <QString>

#include <optional>

namespace xpl {

struct StartButtonArt
{
    QImage normal, hover, pressed;   // the bar's height; transparent round the button
};

// `flag` is the flag bitmap (media::startFlag(), or classicStartFlag() for
// Windows Classic); `label` what it says ("start" with a visual style,
// "Start" in Classic).
std::optional<StartButtonArt> startButton(const Look &look, const QImage &flag, const QString &label = QStringLiteral("start"),
                                          QString *error = nullptr);

} // namespace xpl
