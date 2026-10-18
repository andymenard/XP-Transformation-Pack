// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <optional>
class QWidget;
namespace xpl::editor::cachepreferences {
// Ask once on the first theme Apply, unless the checkbox was chosen explicitly.
// nullopt means Cancel or a failed save (with error); neither applies a theme.
std::optional<bool> choose(QWidget *parent, bool checked, bool explicitlyChosen, QString *error = nullptr);
}
