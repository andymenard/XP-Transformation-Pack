// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <atomic>
#include <functional>
namespace xpl::themeimport {
// Imported preset files may be removed; their shared assets remain available.
bool removable(const QString &theme);
bool remove(const QString &theme, QString *error = nullptr);
struct Result { QString directory; QString theme; int styles = 0; int themes = 0; };
// Import files as data into a new library directory; never apply or execute them.
bool install(const QString &source, Result *result, QString *error,
             const std::function<void(const QString &)> &say = {}, const std::atomic_bool *cancel = nullptr);
}
