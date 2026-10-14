// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
namespace xpl::editor {
inline bool wallpaperVariant(const QString &name) {
    static const QRegularExpression variant(QStringLiteral(R"(\s*\((?:\d+\s*[x×]\s*\d+|Alt|Alternative|Crop(?:ped)?|1080p|4K)\)$)"), QRegularExpression::CaseInsensitiveOption);
    return variant.match(name).hasMatch();
}
inline int wallpaperGroup(const QString &name) {
    // Names installed by the XP ISO's wallpaper/desktop-picture copy lists.
    static const QSet<QString> stock={"ascent","autumn","azul","bliss","blue lace 16","coffee bean","crystal","feathertexture","follow","friend","gone fishing","greenstone","home","moon flower","peace","power","prairie wind","purple flower","radiance","red moon desert","rhododendron","ripple","river sumida","santa fe stucco","soap bubbles","stonehenge","tulips","vortec space","wind","windows xp","zapotec"};
    if (stock.contains(name.toLower())) return 0;
    return name.startsWith(QStringLiteral("Plus!"),Qt::CaseInsensitive)?1:2;
}
inline void sortWallpapers(QFileInfoList &files) {
    files.erase(std::remove_if(files.begin(),files.end(),[](const QFileInfo &file){return wallpaperVariant(file.completeBaseName());}),files.end());
    std::stable_sort(files.begin(),files.end(),[](const QFileInfo &a,const QFileInfo &b){
        const int ag=wallpaperGroup(a.completeBaseName()),bg=wallpaperGroup(b.completeBaseName());
        return ag!=bg?ag<bg:QString::compare(a.completeBaseName(),b.completeBaseName(),Qt::CaseInsensitive)<0;
    });
}
}
