// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/look.h"
#include "app/session.h"
#include <optional>
namespace xpl::darkmode {
std::optional<Look> applicationLook(const Look &source, bool dark, QString *error = nullptr);
bool apply(Session &session, const Look &source, bool dark, QString *error = nullptr);
bool publish(bool dark, QString *error = nullptr);
}
