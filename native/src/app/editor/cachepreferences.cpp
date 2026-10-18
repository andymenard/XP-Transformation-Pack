// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/cachepreferences.h"
#include "app/state.h"
#include <QMessageBox>
namespace xpl::editor::cachepreferences {
std::optional<bool> choose(QWidget *parent, bool checked, bool explicitlyChosen, QString *error)
{
    State state = State::load();
    const QString key = QStringLiteral("themeCacheEnabled");
    if (!state.settings.contains(key) && !explicitlyChosen) {
        const auto answer = QMessageBox::question(parent, QStringLiteral("Theme caching"),
            QStringLiteral("Would you like to use theme caching?\n\n"
                           "Caching saves time when switching themes, but uses more disk space. "
                           "You can enable or disable it later on the Appearance tab.\n\n"
                           "Your current theme and recovery data are kept either way."),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
        if (answer == QMessageBox::Cancel) return {};
        checked = answer == QMessageBox::Yes;
    }
    if (!state.settings.contains(key) || state.settings.value(key).toBool() != checked) {
        state.settings.insert(key, checked);
        if (!state.save(error)) return {};
    }
    return checked;
}
}
