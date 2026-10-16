// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/customluna.h"
#include "core/theme.h"
#include "core/render.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
using namespace xpl;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2 && argc != 3) return 2;
    QTemporaryDir dir;
    QString error;
    const std::array<QColor, 4> colours{QColor("#862ba8"), QColor("#9d3535"), QColor("#268a64"), QColor("#bd8825")};
    const QString generated = editor::customLuna(app.arguments()[1], colours, (argc == 3 ? app.arguments()[2] : dir.path()), &error);
    if (generated.isEmpty()) { QTextStream(stderr) << error << '\n'; return 1; }
    QTextStream(stdout) << generated << "\n";
    const auto original = Theme::load(app.arguments()[1], "NORMALBLUE_INI");
    const auto custom = Theme::load(generated, "NORMALBLUE_INI");
    if (!original || !custom || original->bitmapNames() != custom->bitmapNames()) return 3;
    for (const auto &variant : custom->variants().iniNames) {
        if (variant != custom->variant()) continue;
        const auto theme = Theme::load(generated, variant);
        QRgb highlight;
        if (!theme || !parseColour(theme->property(u"sysmetrics", u"", u"", u"highlight").value_or(QString()), &highlight)
            || QColor(highlight) != colours[1]) return 13;
    }
    const QStringList changed{"BLUE_STARTBUTTON_BMP", "BLUE_TASKBARBACKGROUND_BMP", "BLUE_FRAMECAPTION_BMP", "BLUE_CAPTIONBUTTON_BMP", "BLUE_STARTUSERPANEL_BMP", "BLUE_STARTPANELPLACESBACKGROUND_BMP", "BLUE_STARTPANELLOGOFFBACKGROUND_BMP"};
    for (const auto &name : original->bitmapNames()) {
        const auto a = original->bitmap(name).image, b = custom->bitmap(name).image;
        if (a.size() != b.size()) return 4;
        if (changed.contains(name) && a == b) { QTextStream(stderr) << "Unchanged: " << name; return 5; }
        if (!name.startsWith("BLUE_") && a != b) return 6;
        for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            if (name.contains("CLOSEBUTTON")) {
                const QColor color(p);
                if (color.hsvSaturationF()>.3 && (color.hsvHueF()<.09 || color.hsvHueF()>.92) && p!=q) return 14;
            }
            if (qAlpha(p) != qAlpha(q)) return 7;
            if (qRed(p) == 255 && qGreen(p) == 0 && qBlue(p) == 255 && p != q) return 8;
        }
    }
    // The rounded Start edge contains the taskbar backdrop, not Start color.
    const auto edge = custom->bitmap("BLUE_STARTBUTTON_BMP").image;
    const auto oldEdge = original->bitmap("BLUE_STARTBUTTON_BMP").image;
    bool checkedEdge = false;
    for (int y = 0; y < edge.height(); ++y) {
        const QColor old(oldEdge.pixel(edge.width()-1, y));
        if (old.rgb() == qRgb(255, 0, 255) || old.hsvSaturationF() < .12) continue;
        const QColor pixel(edge.pixel(edge.width()-1, y));
        const float distance = qAbs(pixel.hsvHueF() - colours[1].hsvHueF());
        if (qMin(distance, 1-distance) > .04) return 11;
        checkedEdge = true;
    }
    if (!checkedEdge) return 12;
    if (editor::customLuna(app.arguments()[1], colours, (argc == 3 ? app.arguments()[2] : dir.path()), &error) != generated) return 9;
    if (!editor::customLuna(dir.path() + "/missing.msstyles", colours, dir.path(), &error).isEmpty()) return 10;
    QTextStream(stdout) << "Custom Luna: artwork changed, variants and transparency preserved; repeat generation stable.\n";
}
