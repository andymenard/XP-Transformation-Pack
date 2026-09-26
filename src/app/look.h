// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// A look: what XPlasma turns into Plasma packages -- a visual style at one of
// its variants, or Windows Classic in one colour scheme. Either way it has
// the system appearance XP would set (colours, fonts, frame and caption
// sizes: a style's [SysMetrics], a scheme's own), and a style also has its
// renderer, which draws every part exactly as uxtheme does.
#include "core/classic.h"
#include "core/render.h"

#include <QString>

#include <memory>
#include <optional>

namespace xpl {

struct Look
{
    QString name;                  // shown in Plasma's settings ("Windows XP style (Silver)")
    QString id;                    // the base of package ids ("windows-xp-style-silver")
    ClassicScheme system;          // colours, fonts, metrics
    std::shared_ptr<const Renderer> renderer;   // null for Windows Classic
    QString stylePath, variant;    // for a visual style

    bool classic() const { return !renderer; }

    static std::optional<Look> fromStyle(const QString &path, const QString &variant = QString(), QString *error = nullptr);
    static Look fromScheme(const ClassicScheme &scheme);
};

// A package id from a name: lower-case letters and digits, '-' between words.
QString packageId(const QString &name);

} // namespace xpl
