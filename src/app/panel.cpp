#include "app/panel.h"

#include "app/package.h"
#include "core/schema.h"

#include <QDir>
#include <QPainter>

namespace xpl {

namespace {

using Elements = QList<std::pair<QString, QImage>>;
constexpr int kTypicalWidth = 800;   // what stretched middles are sampled from (as the decoration's)

// An image drawn at the height Plasma shows it, cut into FrameSvg's nine
// pieces by `m`. Only the middle column varies with the item's width: it is
// stretched (hint-stretch-borders), or -- `tilePeriod` > 0 -- one whole
// tile KSvg repeats from where XP starts tiling. Hint margins say where
// the item's contents go.
void frame(Elements &out, const QString &prefix, const QImage &image, QMargins m, int tilePeriod,
           const QMargins &content = QMargins())
{
    const int w = image.width(), h = image.height();
    m.setLeft(qBound(0, m.left(), w));
    m.setRight(qBound(0, m.right(), w - m.left()));
    m.setTop(qBound(0, m.top(), h));
    m.setBottom(qBound(0, m.bottom(), h - m.top()));
    const int mid = tilePeriod > 0 ? qMin(tilePeriod, w - m.left() - m.right()) : w - m.left() - m.right();
    const int xs[] = {0, m.left(), w - m.right()};
    const int ws[] = {m.left(), mid, m.right()};
    const int ys[] = {0, m.top(), h - m.bottom()};
    const int hs[] = {m.top(), h - m.top() - m.bottom(), m.bottom()};
    static const char *const names[3][3] = {{"topleft", "top", "topright"}, {"left", "center", "right"},
                                            {"bottomleft", "bottom", "bottomright"}};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            if (ws[col] > 0 && hs[row] > 0)
                out.append({prefix + QLatin1String(names[row][col]), image.copy(xs[col], ys[row], ws[col], hs[row])});
    if (tilePeriod <= 0)
        out.append({prefix + QLatin1String("hint-stretch-borders"), QImage()});
    auto hint = [&](const char *side, int px, bool horizontal) {
        QImage img(horizontal ? QSize(qMax(1, px), 1) : QSize(1, qMax(1, px)), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        if (px > 0)
            out.append({prefix + QLatin1String("hint-") + QLatin1String(side) + QLatin1String("-margin"), img});
    };
    hint("left", content.left(), true);
    hint("right", content.right(), true);
    hint("top", content.top(), false);
    hint("bottom", content.bottom(), false);
}

int tilePeriod(const Renderer &r, const PartRef &ref)
{
    if (r.property(ref, u"sizingtype").value_or(QString()).compare(QLatin1String("tile"), Qt::CaseInsensitive) != 0)
        return 0;
    const QMargins m = r.margins(ref, u"sizingmargins");
    return qMax(1, r.stateImage(ref).width() - m.left() - m.right());
}

QColor average(const QImage &image)
{
    qint64 r = 0, g = 0, b = 0, n = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QRgb c = image.pixel(x, y);
            if (qAlpha(c) < 128)
                continue;
            r += qRed(c);
            g += qGreen(c);
            b += qBlue(c);
            ++n;
        }
    return n ? QColor(int(r / n), int(g / n), int(b / n)) : QColor(Qt::gray);
}

// Plasma's task states, and the TaskBand toolbar button state XP draws for
// each: an active window's button is the checked one.
const struct { const char *prefix; int state; } kTaskStates[] = {
    {"normal-", 1}, {"hover-", 2}, {"focus-", 5}, {"minimized-", 1},
};

void styleArt(const Look &look, PanelArt &art)
{
    const Renderer &r = *look.renderer;
    const int H = art.height;

    // The bar.
    const PartRef bar{QStringLiteral("taskbar"), schema::partId(u"taskbar", u"backgroundbottom"), 0, {}};
    const QImage barImage = r.background(bar, QSize(kTypicalWidth, H));
    for (const char *prefix : {"south-", ""})
        frame(art.panel, QLatin1String(prefix), barImage, r.margins(bar, u"sizingmargins"), tilePeriod(r, bar));
    art.panelColour = average(barImage);

    // Task buttons, the height of the bar.
    const int button = schema::partId(u"toolbar", u"button");
    for (const auto &t : kTaskStates) {
        const PartRef ref{QStringLiteral("toolbar"), button, t.state, QStringLiteral("taskband")};
        const QImage img = r.background(ref, QSize(160, H));
        for (const char *location : {"south-", ""})
            frame(art.tasks, QLatin1String(location) + QLatin1String(t.prefix), img, r.margins(ref, u"sizingmargins"),
                  tilePeriod(r, ref), r.contentMargins(ref));
    }
    // A window asking for attention: XP flashes its button.
    const PartRef flash{QStringLiteral("taskband"), schema::partId(u"taskband", u"flashbutton"), 1, {}};
    if (r.hasPart(flash)) {
        const QImage img = r.background(flash, QSize(160, H));
        for (const char *location : {"south-", ""})
            frame(art.tasks, QLatin1String(location) + QLatin1String("attention-"), img, r.margins(flash, u"sizingmargins"),
                  tilePeriod(r, flash), r.contentMargins(flash));
    }
    QRgb text = qRgb(255, 255, 255);
    parseColour(r.property({QStringLiteral("toolbar"), button, 1, QStringLiteral("taskband")}, u"textcolor").value_or(QString()), &text);
    art.textColour = QColor(text);

    // The notification area: its backdrop (the clock carries on its middle,
    // without the tray's left end), and the button that shows hidden icons.
    const PartRef tray{QStringLiteral("traynotify"), schema::partId(u"traynotify", u"background"), 0, QStringLiteral("traynotifyhoriz")};
    if (r.hasPart(tray)) {
        const QImage img = r.background(tray, QSize(200, H));
        const QMargins m = r.margins(tray, u"sizingmargins");
        frame(art.tray, QStringLiteral("tray-"), img, m, tilePeriod(r, tray), QMargins(r.contentMargins(tray).left(), 0, 0, 0));
        frame(art.tray, QStringLiteral("clock-"), img.copy(m.left(), 0, img.width() - m.left(), H), QMargins(0, m.top(), m.right(), m.bottom()),
              tilePeriod(r, tray));
        for (const auto &[prefix, sub] : {std::pair("expand-", "traynotifyhoriz"), std::pair("collapse-", "traynotifyhorizopen")}) {
            static const char *const states[] = {"normal", "hover", "pressed"};
            for (int s = 0; s < 3; ++s) {
                const PartRef ref{QStringLiteral("button"), 1, s + 1, QLatin1String(sub)};
                const QImage img = r.stateImage(ref);
                if (!img.isNull())
                    art.tray.append({QLatin1String(prefix) + QLatin1String(states[s]), img});
            }
        }
    }
}

// Windows Classic (measured, Windows Standard): the 30px bar is the dialog
// face under a raised top edge (Light3D, then the highlight); task buttons
// are 22px push buttons 4px down, the active one pushed in over a dither of
// the highlight and the face.
void classicArt(const Look &look, PanelArt &art)
{
    const ClassicScheme &s = look.system;
    const QPalette pal = s.palette();
    const int H = art.height;
    QImage bar(64, H, QImage::Format_ARGB32_Premultiplied);
    bar.fill(pal.color(QPalette::Button));
    {
        QPainter p(&bar);
        p.fillRect(QRect(0, 0, 64, 1), pal.color(QPalette::Midlight));
        p.fillRect(QRect(0, 1, 64, 1), pal.color(QPalette::Light));
    }
    for (const char *prefix : {"south-", ""})
        frame(art.panel, QLatin1String(prefix), bar, QMargins(0, 0, 2, 1), 0);
    art.panelColour = pal.color(QPalette::Button);
    art.textColour = s.colour(u"ButtonText");

    for (const auto &t : kTaskStates) {
        QImage img(160, H, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            const QRect button(0, 4, 160, 22);
            const bool down = t.state == 5;
            classic::pushEdge(&p, button, pal, down);
            if (down)
                for (int y = button.top() + 2; y < button.bottom() - 1; ++y)
                    for (int x = button.left() + 2; x < button.right() - 1; ++x)
                        if ((x + y) % 2 == 0)
                            p.fillRect(x, y, 1, 1, pal.color(QPalette::Light));
        }
        for (const char *location : {"south-", ""})
            frame(art.tasks, QLatin1String(location) + QLatin1String(t.prefix), img, QMargins(4, 4, 6, 6),
                  t.state == 5 ? 2 : 0, QMargins(4, 0, 4, 0));
    }
    QImage flash(160, H, QImage::Format_ARGB32_Premultiplied);
    flash.fill(Qt::transparent);
    {
        QPainter p(&flash);
        const QRect button(0, 4, 160, 22);
        classic::pushEdge(&p, button, pal, false);
        p.fillRect(button.adjusted(2, 2, -2, -2), s.colour(u"Hilight"));
    }
    for (const char *location : {"south-", ""})
        frame(art.tasks, QLatin1String(location) + QLatin1String("attention-"), flash, QMargins(4, 4, 6, 6), 0, QMargins(4, 0, 4, 0));
}

QByteArray colors(const Look &look, const PanelArt &art)
{
    auto rgb = [](const QColor &c) { return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue()); };
    const ClassicScheme &s = look.system;
    QString out;
    for (const char *group : {"Window", "Button", "View", "Header", "Complementary"}) {
        out += QStringLiteral("[Colors:%1]\n").arg(QLatin1String(group));
        out += QStringLiteral("BackgroundNormal=%1\nBackgroundAlternate=%1\n").arg(rgb(art.panelColour));
        out += QStringLiteral("ForegroundNormal=%1\nForegroundActive=%1\nForegroundInactive=%1\n").arg(rgb(art.textColour));
        out += QStringLiteral("DecorationFocus=%1\nDecorationHover=%1\n\n").arg(rgb(s.colour(u"Hilight")));
    }
    out += QStringLiteral("[Colors:Selection]\nBackgroundNormal=%1\nForegroundNormal=%2\n\n")
               .arg(rgb(s.colour(u"Hilight")), rgb(s.colour(u"HilightText")));
    out += QStringLiteral("[Colors:Tooltip]\nBackgroundNormal=%1\nForegroundNormal=%2\n\n")
               .arg(rgb(s.colour(u"InfoWindow")), rgb(s.colour(u"InfoText")));
    out += QStringLiteral("[General]\nName=%1\nColorScheme=%1\n").arg(look.name);
    return out.toUtf8();
}

QByteArray svg(const QList<std::pair<QString, QImage>> &elements)
{
    SvgSheet sheet;
    for (const auto &[id, image] : elements) {
        if (image.isNull())
            sheet.hint(id);
        else
            sheet.image(id, image);
    }
    return sheet.document();
}

} // namespace

PanelArt panelArt(const Look &look)
{
    PanelArt art;
    if (look.classic())
        classicArt(look, art);
    else
        styleArt(look, art);
    return art;
}

QString writePanelTheme(const Look &look, const QString &directory, QString *error)
{
    const PanelArt art = panelArt(look);
    const QString id = look.id + QLatin1String("-taskbar");
    Package pkg(QDir(directory).filePath(id));
    const QByteArray panel = svg(art.panel);
    pkg.write(QStringLiteral("widgets/panel-background.svg"), panel);
    pkg.write(QStringLiteral("solid/widgets/panel-background.svg"), panel);
    pkg.write(QStringLiteral("widgets/tasks.svg"), svg(art.tasks));
    if (!art.tray.isEmpty())
        pkg.write(QStringLiteral("widgets/xp-tray.svg"), svg(art.tray));
    pkg.write(QStringLiteral("colors"), colors(look, art));
    QJsonObject meta = pluginMetadata(id, look.name + QLatin1String(" (taskbar)"),
                                      QStringLiteral("The taskbar of the Windows XP look \"%1\", made by XPLasma").arg(look.name));
    meta.insert(QStringLiteral("X-Plasma-API-Minimum-Version"), QStringLiteral("6.0"));
    meta.insert(QStringLiteral("X-XPLasma-PanelHeight"), art.height);
    pkg.writeJson(QStringLiteral("metadata.json"), meta);
    if (!pkg.ok()) {
        if (error)
            *error = pkg.error();
        return QString();
    }
    return pkg.directory();
}

} // namespace xpl
