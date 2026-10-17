// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFont>
#include <QFontDatabase>
#include <QStringList>
#include <QtMath>
namespace xpl {
inline QString osUiFamily(const QString &family) {
 return family.compare(QStringLiteral("Tahoma"),Qt::CaseInsensitive)==0?QStringLiteral("Tahoma OS"):family;
}
inline QFont osUiFont(QFont font) {font.setFamily(osUiFamily(font.family()));return font;}

inline QFont crispUiFont(QFont font,qreal dpr=1.0,qreal logicalDpi=96.0) {
 const qreal pixels=font.pixelSize()>0?font.pixelSize():font.pointSizeF()*logicalDpi/72.0;
 // Resolve once per process; existing installations retain their old pixel
 // font until the separately supplied, XP-rendered Tahoma faces are installed.
 static const QStringList pixelFamilies=[] {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return QFontDatabase::families();
#else
  return QFontDatabase().families();
#endif
 }();
 const bool haveXpPixels=pixelFamilies.contains(QStringLiteral("XPlasma Tahoma Pixel"));
 const qreal nominal=haveXpPixels?qMax(1,qRound(pixels)):qMax(1.0,pixels);
 const qreal physical=qMax(1,qRound(nominal*dpr));
 if(haveXpPixels) {
  const QString optical=QStringLiteral("XPlasma Tahoma Pixel %1").arg(qRound(physical));
  font.setFamily(pixelFamilies.contains(optical)?optical:QStringLiteral("XPlasma Tahoma Pixel"));
 }
 else {
  font.setFamily(font.bold()?QStringLiteral("MS Sans Serif Bold"):QStringLiteral("MS Sans Serif"));
  font.setWeight(QFont::Normal); // Legacy fallback has a separate bold family.
 }
 font.setPointSizeF(physical/qMax(1.0,dpr)*72.0/logicalDpi);
 font.setStyleStrategy(QFont::NoAntialias);font.setHintingPreference(QFont::PreferFullHinting);font.setKerning(false);
 return font;
}
}
