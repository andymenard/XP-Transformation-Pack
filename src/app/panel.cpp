#include "app/panel.h"

#include "app/controls.h"

#include "app/media.h"
#include "app/package.h"
#include "core/classic.h"
#include "core/icons.h"
#include "core/pe.h"
#include "core/schema.h"

#include <QDir>
#include <QPainter>

#include <cmath>

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
    art.trayColour = art.panelColour;   // (unless the style has a tray of its own, below)

    // Task buttons, the height of the bar.
    const int button = schema::partId(u"toolbar", u"button");
    for (const auto &t : kTaskStates) {
        const PartRef ref{QStringLiteral("toolbar"), button, t.state, QStringLiteral("taskband")};
        const QImage img = r.background(ref, QSize(160, H));
        for (const char *location : {"south-", ""})
            frame(art.tasks, QLatin1String(location) + QLatin1String(t.prefix), img, r.margins(ref, u"sizingmargins"),
                  tilePeriod(r, ref), r.contentMargins(ref));
    }
    // (The patched task manager lays its buttons out as XP's TaskBand: see
    // overrides.h. Measured on XP, Luna, 800x600.)
    art.tasks.append({QStringLiteral("hint-xplasma-taskband"), QImage()});
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
    // Text on the tray: the clock's colour (Royale's is black on its light
    // tray, where the taskbar's is white), else whatever reads on the tray.
    QRgb clockText = 0;
    if (parseColour(r.property({QStringLiteral("clock"), 0, 0, QStringLiteral("traynotify")}, u"textcolor").value_or(QString()), &clockText))
        art.trayTextColour = QColor(clockText);

    // The notification area: its backdrop (the clock carries on its middle,
    // without the tray's left end), and the button that shows hidden icons.
    const PartRef tray{QStringLiteral("traynotify"), schema::partId(u"traynotify", u"background"), 0, QStringLiteral("traynotifyhoriz")};
    if (r.hasPart(tray)) {
        const QImage img = r.background(tray, QSize(200, H));
        const QMargins m = r.margins(tray, u"sizingmargins");
        art.trayColour = average(img);
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
    art.trayColour = art.panelColour;   // (the static edge is only a line)
    art.trayTextColour = art.textColour;

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

    // The notification area (measured, Windows Standard, 800x600): a static
    // edge -- a sunken 1px box, the shadow top and left, the highlight right
    // and bottom -- from the buttons' top (4px down) to 3px above the bar's
    // bottom, ending 3px in from the right; icons 4px in. Transparent
    // around it: the bar shows through. The clock carries on its middle.
    // (Plasma pads each icon: 1px of content margin puts the first 5px in.)
    {
        const QColor shadow = pal.color(QPalette::Dark), light = pal.color(QPalette::Light);
        const int W = 200, top = 4, bottom = H - 3, right = W - 3;
        QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.fillRect(QRect(0, top, right, 1), shadow);
        p.fillRect(QRect(0, top, 1, bottom - top), shadow);
        p.fillRect(QRect(right, top, 1, bottom - top + 1), light);
        p.fillRect(QRect(0, bottom, right + 1, 1), light);
        p.end();
        // (The tray's own part is open on the right: the clock closes the
        // box, over or under it, and nothing shows between.)
        QImage open = img.copy(0, 0, right, H);
        frame(art.tray, QStringLiteral("tray-"), open, QMargins(1, top + 1, 0, H - bottom), 0, QMargins(1, 0, 0, 0));
        frame(art.tray, QStringLiteral("clock-"), img.copy(1, 0, W - 1, H), QMargins(0, top + 1, W - right, H - bottom), 0);
    }
    // Show hidden icons: explorer.exe's << and >> (bitmaps 172, 173; magenta
    // keyed out) on a flat toolbar button -- raised under the mouse, sunk
    // (the glyph a pixel down and right) while pressed.
    for (const auto &[prefix, id] : {std::pair("expand-", 172), std::pair("collapse-", 173)}) {
        QImage glyph = media::bitmap(QStringLiteral("explorer.exe"), id).value_or(QImage());
        if (!glyph.isNull()) {
            glyph = glyph.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < glyph.height(); ++y)
                for (int x = 0; x < glyph.width(); ++x)
                    if ((glyph.pixel(x, y) & 0xFFFFFF) == 0xFF00FF)
                        glyph.setPixel(x, y, 0);
        } else {
            // (No XP media: the same two chevrons, drawn.)
            glyph = QImage(9, 9, QImage::Format_ARGB32);
            glyph.fill(Qt::transparent);
            QPainter g(&glyph);
            const bool left = id == 172;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 4; ++i) {
                    const int x = left ? 1 + c * 4 + (3 - i) : 1 + c * 4 + i;
                    g.fillRect(QRect(x, 1 + i, 2, 1), s.colour(u"ButtonText"));
                    g.fillRect(QRect(x, 7 - i, 2, 1), s.colour(u"ButtonText"));
                }
        }
        static const char *const states[] = {"normal", "hover", "pressed"};
        for (int st = 0; st < 3; ++st) {
            QImage img(20, 20, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            const QRect r(1, 1, 18, 18);
            if (st > 0) {
                const QColor tl = st == 1 ? pal.color(QPalette::Light) : pal.color(QPalette::Dark);
                const QColor br = st == 1 ? pal.color(QPalette::Dark) : pal.color(QPalette::Light);
                p.fillRect(QRect(r.left(), r.top(), r.width() - 1, 1), tl);
                p.fillRect(QRect(r.left(), r.top(), 1, r.height() - 1), tl);
                p.fillRect(QRect(r.right(), r.top(), 1, r.height()), br);
                p.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), br);
            }
            const int shift = st == 2 ? 1 : 0;
            p.drawImage(QPoint((20 - glyph.width()) / 2 + shift, (20 - glyph.height()) / 2 + shift), glyph);
            p.end();
            art.tray.append({QLatin1String(prefix) + QLatin1String(states[st]), img});
        }
    }
}

// Notification balloons and the copy dialog. Balloon colours are the
// tooltip's (the theme's [Colors:Tooltip]: InfoWindow, InfoText); the rest
// is the look's own button, text box and progress bar -- or Classic's
// bevels -- and XP's icons and animations from its files, when the media
// is there.
void notifyArt(const Look &look, PanelArt &art)
{
    auto &out = art.notify;
    const QPalette pal = look.system.palette();
    static const char *const states[] = {"normal", "hover", "pressed"};

    // The balloon's close box: the style's ToolTip::Close, or Classic's
    // caption close button, flat.
    for (int s = 0; s < 3; ++s) {
        QImage img;
        if (!look.classic()) {
            const PartRef close{QStringLiteral("tooltip"), schema::partId(u"tooltip", u"close"), s + 1, {}};
            if (look.renderer->hasPart(close))
                img = look.renderer->stateImage(close);
        }
        if (img.isNull()) {
            img = QImage(16, 14, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            if (s == 0)
                classic::captionGlyph(&p, QPoint(0, 0), classic::CaptionGlyph::Close, pal.color(QPalette::ButtonText));
            else
                classic::captionButton(&p, img.rect(), classic::CaptionGlyph::Close, pal, s == 2, false);
        }
        out.append({QLatin1String("close-") + QLatin1String(states[s]), img});
    }

    // Push buttons: the style's Button::PushButton (normal, hot, pressed),
    // or Classic's bevel.
    for (int s = 0; s < 3; ++s) {
        const PartRef button{QStringLiteral("button"), schema::partId(u"button", u"pushbutton"), s + 1, {}};
        if (!look.classic() && look.renderer->hasPart(button)) {
            frame(out, QLatin1String("button-") + QLatin1String(states[s]) + QLatin1Char('-'), look.renderer->stateImage(button),
                  look.renderer->margins(button, u"sizingmargins"), 0, QMargins(3, 3, 3, 3));
        } else {
            QImage img(24, 23, QImage::Format_ARGB32_Premultiplied);
            img.fill(pal.color(QPalette::Button));
            QPainter p(&img);
            classic::pushEdge(&p, img.rect(), pal, s == 2);
            p.end();
            frame(out, QLatin1String("button-") + QLatin1String(states[s]) + QLatin1Char('-'), img, QMargins(3, 3, 3, 3), 0,
                  QMargins(3, 3, 3, 3));
        }
    }

    // A text box: the style's Edit, or Classic's sunken edge; white inside.
    {
        QImage img(40, 21, QImage::Format_ARGB32_Premultiplied);
        const PartRef edit{QStringLiteral("edit"), schema::partId(u"edit", u"edittext"), 1, {}};
        if (!look.classic() && look.renderer->hasPart(edit)) {
            img = look.renderer->background(edit, img.size());
        } else {
            img.fill(pal.color(QPalette::Base));
            QPainter p(&img);
            classic::sunkenEdge(&p, img.rect(), pal);
        }
        frame(out, QStringLiteral("edit-"), img, QMargins(2, 2, 2, 2), 0, QMargins(3, 3, 3, 3));
    }

    // The progress bar: the style's track and one chunk (its chunk and
    // space sizes as hints), or Classic's sunken box and highlight blocks.
    {
        const int h = 18;
        const PartRef bar{QStringLiteral("progress"), schema::partId(u"progress", u"bar"), 0, {}};
        const PartRef chunk{QStringLiteral("progress"), schema::partId(u"progress", u"chunk"), 0, {}};
        const PartRef base{QStringLiteral("progress"), 0, 0, {}};
        if (!look.classic() && look.renderer->hasPart(bar) && look.renderer->hasPart(chunk)) {
            const QMargins m = look.renderer->margins(bar, u"sizingmargins");
            frame(out, QStringLiteral("progress-track-"), look.renderer->background(bar, QSize(100, h)), m, 0, QMargins(3, 3, 3, 3));
            const int size = look.renderer->property(base, u"progresschunksize").value_or(QStringLiteral("6")).toInt();
            const int space = look.renderer->property(base, u"progressspacesize").value_or(QStringLiteral("2")).toInt();
            out.append({QStringLiteral("progress-chunk"), look.renderer->background(chunk, QSize(qMax(1, size), h - 6))});
            out.append({QStringLiteral("hint-progress-space"), QImage(qMax(1, space), 1, QImage::Format_ARGB32_Premultiplied)});
        } else {
            QImage track(40, h, QImage::Format_ARGB32_Premultiplied);
            track.fill(pal.color(QPalette::Button));
            QPainter p(&track);
            classic::frame(&p, track.rect(), pal.color(QPalette::Dark), pal.color(QPalette::Light));
            p.end();
            frame(out, QStringLiteral("progress-track-"), track, QMargins(1, 1, 1, 1), 0, QMargins(2, 2, 2, 2));
            QImage block((h - 4) * 2 / 3, h - 4, QImage::Format_ARGB32_Premultiplied);
            block.fill(look.system.colour(u"Hilight"));
            out.append({QStringLiteral("progress-chunk"), block});
            QImage gap(2, 1, QImage::Format_ARGB32_Premultiplied);
            gap.fill(Qt::transparent);
            out.append({QStringLiteral("hint-progress-space"), gap});
        }
    }

    // XP's own: the severity icons (user32.dll's information, warning and
    // error, 16px) and shell32.dll's file animations.
    if (const auto user32 = media::systemFile(QStringLiteral("user32.dll")))
        if (const auto pe = PeResources::parse(*user32))
            for (const auto &[name, group] : {std::pair("icon-info", 104), std::pair("icon-warning", 101), std::pair("icon-error", 103)}) {
                const auto images = iconGroup(*pe, QString::number(group));
                if (images.contains(16))
                    out.append({QLatin1String(name), images.value(16).image});
            }
    for (const auto &[name, id] : {std::pair("copy", 161), std::pair("move", 160), std::pair("delete", 162)}) {
        const auto frames = media::animation(QStringLiteral("shell32.dll"), id);
        if (!frames)
            continue;
        for (int i = 0; i < frames->size(); ++i)
            out.append({QStringLiteral("anim-%1-%2").arg(QLatin1String(name)).arg(i), frames->at(i)});
    }
}

// Plasma's popups and tooltips, as XP's. A popup is a flyout: the button
// face in the frame XP gives its menus -- visual styles' flat button-shadow
// line, Classic's raised edge (Light3D and DkShadow outside, Highlight and
// Shadow inside). A tooltip is InfoWindow yellow in a 1px window-frame
// line, as the style draws QToolTip. (Their text is the theme's Window
// colours': XP's window text, black on both.)
void popupArt(const Look &look, PanelArt &art)
{
    const ClassicScheme &s = look.system;
    auto ring = [](QPainter &p, const QRect &q, const QColor &tl, const QColor &br) {
        p.fillRect(QRect(q.left(), q.top(), q.width() - 1, 1), tl);
        p.fillRect(QRect(q.left(), q.top(), 1, q.height() - 1), tl);
        p.fillRect(QRect(q.left(), q.bottom(), q.width(), 1), br);
        p.fillRect(QRect(q.right(), q.top(), 1, q.height()), br);
    };
    const int edge = look.classic() ? 2 : 1;
    QImage popup(24, 24, QImage::Format_ARGB32_Premultiplied);
    popup.fill(s.colour(u"ButtonFace"));
    {
        QPainter p(&popup);
        if (look.classic()) {
            ring(p, popup.rect(), s.colour(u"ButtonLight"), s.colour(u"ButtonDkShadow"));
            ring(p, popup.rect().adjusted(1, 1, -1, -1), s.colour(u"ButtonHilight"), s.colour(u"ButtonShadow"));
        } else {
            ring(p, popup.rect(), s.colour(u"ButtonShadow"), s.colour(u"ButtonShadow"));
        }
    }
    const int pad = edge + 4;
    frame(art.popup, QString(), popup, QMargins(edge, edge, edge, edge), 0, QMargins(pad, pad, pad, pad));

    QImage tip(16, 16, QImage::Format_ARGB32_Premultiplied);
    tip.fill(s.colour(u"InfoWindow"));
    {
        QPainter p(&tip);
        ring(p, tip.rect(), s.colour(u"WindowFrame"), s.colour(u"WindowFrame"));
    }
    frame(art.tooltip, QString(), tip, QMargins(1, 1, 1, 1), 0, QMargins(3, 2, 3, 2));
}

QByteArray colors(const Look &look, const PanelArt &art)
{
    auto rgb = [](const QColor &c) { return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue()); };
    const ClassicScheme &s = look.system;
    QString out;
    // The taskbar's colours are the Complementary set's (XPLasma's taskbar
    // widgets take that set); the rest are XP's, for Plasma's popups: the
    // dialog face and window text, white for lists.
    auto group = [&](const char *name, const QColor &background, const QColor &text, const QColor &inactive) {
        out += QStringLiteral("[Colors:%1]\n").arg(QLatin1String(name));
        out += QStringLiteral("BackgroundNormal=%1\nBackgroundAlternate=%1\n").arg(rgb(background));
        out += QStringLiteral("ForegroundNormal=%1\nForegroundActive=%1\nForegroundInactive=%2\n").arg(rgb(text), rgb(inactive));
        out += QStringLiteral("DecorationFocus=%1\nDecorationHover=%1\n\n").arg(rgb(s.colour(u"Hilight")));
    };
    group("Window", s.colour(u"ButtonFace"), s.colour(u"WindowText"), s.colour(u"GrayText"));
    group("Header", s.colour(u"ButtonFace"), s.colour(u"WindowText"), s.colour(u"GrayText"));
    group("Button", s.colour(u"ButtonFace"), s.colour(u"ButtonText"), s.colour(u"GrayText"));
    group("View", s.colour(u"Window"), s.colour(u"WindowText"), s.colour(u"GrayText"));
    group("Complementary", art.panelColour, art.textColour, art.textColour);
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

void appendFrame(QList<std::pair<QString, QImage>> &out, const QString &prefix, const QImage &image, QMargins margins,
                 int tilePeriod, const QMargins &content)
{
    frame(out, prefix, image, margins, tilePeriod, content);
}

QImage composeFrame(const QList<std::pair<QString, QImage>> &elements, const QString &prefix, QSize size)
{
    auto element = [&](const char *name) {
        for (const auto &[id, image] : elements)
            if (id == prefix + QLatin1String(name))
                return image;
        return QImage();
    };
    const QImage tl = element("topleft"), t = element("top"), tr = element("topright"), l = element("left"),
                 r = element("right"), bl = element("bottomleft"), b = element("bottom"), br = element("bottomright"),
                 c = element("center");
    bool stretch = false;
    for (const auto &e : elements)
        stretch |= e.first == prefix + QLatin1String("hint-stretch-borders");
    const int lw = qMax(l.width(), qMax(tl.width(), bl.width())), rw = qMax(r.width(), qMax(tr.width(), br.width()));
    const int th = qMax(t.height(), qMax(tl.height(), tr.height())), bh = qMax(b.height(), qMax(bl.height(), br.height()));
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    auto edge = [&](const QImage &img, const QRect &to) {
        if (to.isEmpty() || img.isNull())
            return;
        if (stretch) {
            p.drawImage(to, img.scaled(to.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
            return;
        }
        for (int y = to.top(); y <= to.bottom(); y += img.height())
            for (int x = to.left(); x <= to.right(); x += img.width())
                p.drawImage(QPoint(x, y), img, QRect(0, 0, qMin(img.width(), to.right() + 1 - x), qMin(img.height(), to.bottom() + 1 - y)));
    };
    p.drawImage(QRect(0, 0, lw, th), tl);
    p.drawImage(QRect(size.width() - rw, 0, rw, th), tr);
    p.drawImage(QRect(0, size.height() - bh, lw, bh), bl);
    p.drawImage(QRect(size.width() - rw, size.height() - bh, rw, bh), br);
    edge(t, QRect(lw, 0, size.width() - lw - rw, th));
    edge(b, QRect(lw, size.height() - bh, size.width() - lw - rw, bh));
    edge(l, QRect(0, th, lw, size.height() - th - bh));
    edge(r, QRect(size.width() - rw, th, rw, size.height() - th - bh));
    edge(c, QRect(lw, th, size.width() - lw - rw, size.height() - th - bh));
    return out;
}

PanelArt panelArt(const Look &look)
{
    PanelArt art;
    if (look.classic())
        classicArt(look, art);
    else
        styleArt(look, art);
    notifyArt(look, art);
    popupArt(look, art);
    // (marks every XPLasma taskbar: its widgets take the Complementary colours)
    art.tasks.append({QStringLiteral("hint-xplasma-colours"), QImage()});
    if (!art.trayTextColour.isValid()) {
        // Whichever of black and white stands out more from the tray.
        auto luminance = [](const QColor &c) {
            auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
        };
        const double tray = luminance(art.trayColour);
        art.trayTextColour = (1.05 / (tray + 0.05)) > ((tray + 0.05) / 0.05) ? QColor(Qt::white) : QColor(Qt::black);
    }
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
    if (!art.notify.isEmpty())
        pkg.write(QStringLiteral("widgets/xp-notify.svg"), svg(art.notify));
    pkg.write(QStringLiteral("dialogs/background.svg"), svg(art.popup));
    pkg.write(QStringLiteral("widgets/background.svg"), svg(art.popup));
    pkg.write(QStringLiteral("widgets/tooltip.svg"), svg(art.tooltip));
    // The controls in Plasma's popups, as XP's (when XPLasma's style can
    // draw them here; Plasma's own otherwise).
    const auto controls = plasmaControls(look);
    for (auto it = controls.cbegin(); it != controls.cend(); ++it)
        pkg.write(QStringLiteral("widgets/%1.svg").arg(it.key()), svg(it.value()));
    pkg.write(QStringLiteral("colors"), colors(look, art));
    QJsonObject meta = pluginMetadata(id, look.name + QLatin1String(" (taskbar)"),
                                      QStringLiteral("The taskbar of the Windows XP look \"%1\", made by XPLasma").arg(look.name));
    meta.insert(QStringLiteral("X-Plasma-API-Minimum-Version"), QStringLiteral("6.0"));
    // Without it Plasma takes the theme for a Plasma 4 one and gives every
    // colour set but Window the Button colours: the taskbar's text black.
    meta.insert(QStringLiteral("X-Plasma-API"), QStringLiteral("6.0"));
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
