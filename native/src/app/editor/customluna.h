// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QString>
#include <array>
#include "app/look.h"
#include <QJsonObject>
namespace xpl::editor {
// Bumped when generated styles change: saved themes baked earlier are rebuilt.
constexpr int kCustomLunaGenerator = 20;
// Writes a standalone msstyles: no renderer extensions or runtime filters.
QString customLuna(const QString &source, const std::array<QColor, 4> &colours,
                   const QString &directory, QString *error, const QString &variant = QString(), int textMode = 0, std::array<int,4> textModes = {-1,-1,-1,-1}, int menuTextMode = -1);
QJsonObject colorMetadata(const QString &path, const QString &variant = {});
std::optional<Look> staticColorLook(const Look &base, const QJsonObject &colors, QString *error);
}
