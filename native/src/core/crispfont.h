// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFont>
#include <QtMath>
namespace xpl {
inline QFont crispUiFont(QFont font,qreal dpr=1.0,qreal logicalDpi=96.0) {
 const qreal pixels=font.pixelSize()>0?font.pixelSize():font.pointSizeF()*logicalDpi/72.0;
 const qreal physical=qMax(1,qRound(qMax(1.0,pixels)*dpr));
 font.setFamily(font.bold()?QStringLiteral("MS Sans Serif Bold"):QStringLiteral("MS Sans Serif"));
 font.setWeight(QFont::Normal); // Bold is a separate supplied face; avoid synthetic double-bold.
 font.setPointSizeF(physical/qMax(1.0,dpr)*72.0/logicalDpi);
 font.setStyleStrategy(QFont::NoAntialias);font.setHintingPreference(QFont::PreferFullHinting);font.setKerning(false);
 return font;
}
}
