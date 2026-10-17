// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/look.h"
#include "app/session.h"
#include <functional>
#include <optional>
namespace xpl::darkmode {
std::optional<Look> applicationLook(const Look &source, bool dark, QString *error = nullptr);
bool apply(Session &session, const Look &source, bool dark, QString *error = nullptr);
bool publish(bool dark, QString *error = nullptr);
// Drops dark builds of styles `wanted` turns down, gone, or from an older builder.
void pruneCache(const std::function<bool(const QString &)> &wanted);
}
