// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/decoration.h"

#include "app/package.h"
#include "core/schema.h"

#include <QDir>
#include <QPainter>

namespace xpl {

namespace {

int part(const char *name)
{
    return schema::partId(u"window", QString::fromLatin1(name));
}

// An image `width` wide holding `art` at its left (or right) edge, the rest
// transparent: a FrameSvg side element as wide as the caption's end caps,
// painting only the frame's own columns (the rest lies under the client).
QImage padded(const QImage &art, int width, bool alignRight)
{
    QImage out(qMax(width, art.width()), art.height(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.drawImage(alignRight ? out.width() - art.width() : 0, 0, art);
    return out;
}

QString rgba(const QColor &c)
{
    return QStringLiteral("%1,%2,%3,255").arg(c.red()).arg(c.green()).arg(c.blue());
}

struct Rc
{
    QList<std::pair<QString, QList<std::pair<QString, QString>>>> groups;
    void set(const QString &group, const QString &key, const QString &value)
    {
        for (auto &[g, keys] : groups)
            if (g == group) {
                keys.append({key, value});
                return;
            }
        groups.append({group, {{key, value}}});
    }
    void set(const QString &group, const QString &key, int value) { set(group, key, QString::number(value)); }
    QByteArray text() const
    {
        QString out;
        for (const auto &[g, keys] : groups) {
            out += QLatin1Char('[') + g + QLatin1String("]\n");
            for (const auto &[k, v] : keys)
                out += k + QLatin1Char('=') + v + QLatin1Char('\n');
            out += QLatin1Char('\n');
        }
        return out.toUtf8();
    }
};

const char *const kButtonStates[] = {"active", "hover", "pressed", "deactivated"};

// --- a visual style -------------------------------------------------------------------

// A part drawn across a window's width, cut for FrameSvg: its ends as wide
// as the side elements (`left`, `right`), and a middle KSvg stretches or
// tiles. XP stretches the source's middle to each window's width, so the
// pieces come from a window of typical width -- exact there, and as close
// as a fixed image gets at others -- unless the part tiles, when the middle
// is one whole tile starting where the left piece ends, in XP's phase.
struct Slices
{
    QImage left, middle, right;
};
Slices horizontal(const Renderer &r, const PartRef &ref, int height, int left, int right)
{
    constexpr int kTypical = 800;
    const bool tile = r.property(ref, u"sizingtype").value_or(QString()).compare(QLatin1String("tile"), Qt::CaseInsensitive) == 0;
    const QMargins m = r.margins(ref, u"sizingmargins");
    const int period = qMax(1, r.stateImage(ref).width() - m.left() - m.right());
    const QImage wide = r.background(ref, QSize(kTypical, height));
    Slices s;
    s.left = wide.copy(0, 0, left, height);
    s.right = wide.copy(kTypical - right, 0, right, height);
    s.middle = tile ? wide.copy(left, 0, period, height) : wide.copy(left, 0, kTypical - left - right, height);
    return s;
}

struct Sheet
{
    QList<std::pair<QString, QImage>> *out;
    void image(const QString &id, const QImage &img) { out->append({id, img}); }
    void hint(const QString &id) { out->append({id, QImage()}); }
};

void styleArt(const Look &look, const DecorationLayout &l, DecorationArt &art)
{
    const Renderer &r = *look.renderer;
    Sheet svg{&art.frame};
    const int F = l.frame, H = l.frame + l.caption;
    const int wpCaption = part("caption"), wpMaxCaption = part("maxcaption");
    const int wpLeft = part("frameleft"), wpRight = part("frameright"), wpBottom = part("framebottom");

    // Wide enough for the caption's fixed ends, and at least its own art.
    const PartRef caption{QStringLiteral("window"), wpCaption, 1, {}};
    const QMargins cm = r.margins(caption, u"sizingmargins");
    const int Lc = qMax(F, cm.left()), Rc_ = qMax(F, cm.right());
    const QString capSizing = r.property(caption, u"sizingtype").value_or(QStringLiteral("stretch")).toLower();
    const bool stretch = capSizing != QLatin1String("tile");

    for (const bool active : {true, false}) {
        const QString prefix = active ? QStringLiteral("decoration") : QStringLiteral("decoration-inactive");
        const int state = active ? 1 : 2;   // CS_ACTIVE / FS_ACTIVE, CS_INACTIVE / FS_INACTIVE
        const Slices cap = horizontal(r, {QStringLiteral("window"), wpCaption, state, {}}, H, Lc, Rc_);
        svg.image(prefix + QLatin1String("-topleft"), cap.left);
        svg.image(prefix + QLatin1String("-top"), cap.middle);
        svg.image(prefix + QLatin1String("-topright"), cap.right);

        // (Stretched along a typical window's height, as the caption and
        // bottom are along its width: Luna's sides are gradients.)
        auto side = [&](int wp) {
            const PartRef ref{QStringLiteral("window"), wp, state, {}};
            const bool tile = r.property(ref, u"sizingtype").value_or(QString()).compare(QLatin1String("tile"), Qt::CaseInsensitive) == 0;
            return r.background(ref, QSize(F, tile ? qMax(1, r.stateImage(ref).height()) : 600 - H - F));
        };
        svg.image(prefix + QLatin1String("-left"), padded(side(wpLeft), Lc, false));
        svg.image(prefix + QLatin1String("-right"), padded(side(wpRight), Rc_, true));

        const Slices bottom = horizontal(r, {QStringLiteral("window"), wpBottom, state, {}}, F, Lc, Rc_);
        svg.image(prefix + QLatin1String("-bottomleft"), bottom.left);
        svg.image(prefix + QLatin1String("-bottom"), bottom.middle);
        svg.image(prefix + QLatin1String("-bottomright"), bottom.right);
        svg.hint(prefix + QLatin1String("-center"));
        if (stretch)
            svg.hint(prefix + QLatin1String("-hint-stretch-borders"));

        // Maximized, XP's frame is off the screen: what shows is the caption
        // below it (MaxCaption where the style has one), which Aurorae
        // stretches across the title bar.
        const bool hasMaxCaption = r.hasPart({QStringLiteral("window"), wpMaxCaption, state, {}});
        const int maxPart = hasMaxCaption ? wpMaxCaption : wpCaption;
        const PartRef maxRef{QStringLiteral("window"), maxPart, state, {}};
        const QMargins mm = r.margins(maxRef, u"sizingmargins");
        const int mw = qMax(r.stateImage(maxRef).width(), mm.left() + mm.right() + 1);
        const QImage max = r.background(maxRef, QSize(mw, hasMaxCaption ? l.caption : H)).copy(mm.left(), hasMaxCaption ? 0 : F, mw - mm.left() - mm.right(), l.caption);
        svg.image(QStringLiteral("decoration-maximized%1-center").arg(active ? QString() : QStringLiteral("-inactive")), max);
    }

    // The buttons, at their size in XP: normal, hot, pushed, disabled; an
    // inactive window's four frames after them.
    const struct { const char *part; const char *file; } buttons[] = {
        {"closebutton", "close"}, {"maxbutton", "maximize"}, {"restorebutton", "restore"},
        {"minbutton", "minimize"}, {"helpbutton", "help"},
    };
    for (const auto &b : buttons) {
        const int wp = part(b.part);
        if (!r.hasPart({QStringLiteral("window"), wp, 1, {}}))
            continue;
        QList<std::pair<QString, QImage>> states;
        for (int s = 0; s < 4; ++s) {
            const QString name = QLatin1String(kButtonStates[s]);
            states.append({name + QLatin1String("-center"), r.background({QStringLiteral("window"), wp, s + 1, {}}, l.button)});
            states.append({(s == 0 ? QStringLiteral("inactive") : name + QLatin1String("-inactive")) + QLatin1String("-center"),
                           r.background({QStringLiteral("window"), wp, s + 5, {}}, l.button)});
        }
        art.buttons.append({QLatin1String(b.file), states});
    }
}

// The caption text's shadow, if the style gives it one.
void styleShadow(const Renderer &r, Rc &rc)
{
    const int wpCaption = part("caption");
    const PartRef activeCaption{QStringLiteral("window"), wpCaption, 1, {}};
    const bool shadow = r.property(activeCaption, u"textshadowtype").value_or(QString()).compare(QLatin1String("single"), Qt::CaseInsensitive) == 0;
    rc.set(QStringLiteral("General"), QStringLiteral("UseTextShadow"), shadow ? QStringLiteral("true") : QStringLiteral("false"));
    if (shadow) {
        QRgb c = qRgb(0, 0, 0);
        parseColour(r.property(activeCaption, u"textshadowcolor").value_or(QString()), &c);
        const QStringList xy = r.property(activeCaption, u"textshadowoffset").value_or(QStringLiteral("1, 1")).split(QLatin1Char(','));
        rc.set(QStringLiteral("General"), QStringLiteral("ActiveTextShadowColor"), rgba(QColor(c)));
        QRgb ci = c;
        parseColour(r.property({QStringLiteral("window"), wpCaption, 2, {}}, u"textshadowcolor").value_or(QString()), &ci);
        rc.set(QStringLiteral("General"), QStringLiteral("InactiveTextShadowColor"), rgba(QColor(ci)));
        rc.set(QStringLiteral("General"), QStringLiteral("TextShadowOffsetX"), xy.value(0).trimmed().toInt());
        rc.set(QStringLiteral("General"), QStringLiteral("TextShadowOffsetY"), xy.value(1, xy.value(0)).trimmed().toInt());
    }
}

// --- Windows Classic ------------------------------------------------------------------

void classicArt(const Look &look, const DecorationLayout &l, DecorationArt &art)
{
    const ClassicScheme &s = look.system;
    const QPalette pal = s.palette();
    Sheet svg{&art.frame};
    const int F = l.frame, H = l.frame + l.caption;
    const int bar = s.metrics.captionHeight;   // the gradient band; a face row under it
    // XP's caption gradient runs from past the icon to short of the buttons,
    // the start colour before it and the end colour after: those ends are
    // fixed, the gradient between them stretches with the window.
    const int Lc = F + 2 + 16 + 2;
    const int Rc_ = F + 2 + 3 * l.button.width() + l.buttonGap + 2;
    const int W = Lc + 64 + Rc_;

    for (const bool active : {true, false}) {
        const QString prefix = active ? QStringLiteral("decoration") : QStringLiteral("decoration-inactive");
        const QColor from = s.colour(active ? u"ActiveTitle" : u"InactiveTitle");
        const QColor to = s.colour(active ? u"GradientActiveTitle" : u"GradientInactiveTitle");
        // A whole window, from which the pieces are cut: the bevel's joins
        // are then right.
        QImage win(W, H + 64 + F, QImage::Format_ARGB32_Premultiplied);
        win.fill(pal.color(QPalette::Button));
        {
            QPainter p(&win);
            classic::raisedEdge(&p, win.rect(), pal);
            p.fillRect(QRect(F, F, Lc - F, bar), from);
            classic::gradientFill(&p, QRect(Lc, F, W - Lc - Rc_, bar), from, to);
            p.fillRect(QRect(W - Rc_, F, Rc_ - F, bar), to);
        }
        const int h = win.height();
        svg.image(prefix + QLatin1String("-topleft"), win.copy(0, 0, Lc, H));
        svg.image(prefix + QLatin1String("-top"), win.copy(Lc, 0, W - Lc - Rc_, H));
        svg.image(prefix + QLatin1String("-topright"), win.copy(W - Rc_, 0, Rc_, H));
        svg.image(prefix + QLatin1String("-left"), padded(win.copy(0, H, F, 8), Lc, false));
        svg.image(prefix + QLatin1String("-right"), padded(win.copy(W - F, H, F, 8), Rc_, true));
        svg.image(prefix + QLatin1String("-bottomleft"), win.copy(0, h - F, Lc, F));
        svg.image(prefix + QLatin1String("-bottom"), win.copy(Lc, h - F, 8, F));
        svg.image(prefix + QLatin1String("-bottomright"), win.copy(W - Rc_, h - F, Rc_, F));
        svg.hint(prefix + QLatin1String("-center"));
        svg.hint(prefix + QLatin1String("-hint-stretch-borders"));

        QImage max(W, l.caption, QImage::Format_ARGB32_Premultiplied);
        max.fill(pal.color(QPalette::Button));
        {
            QPainter p(&max);
            classic::gradientFill(&p, QRect(0, 0, W, bar), from, to);
        }
        svg.image(QStringLiteral("decoration-maximized%1-center").arg(active ? QString() : QStringLiteral("-inactive")), max);
    }

    const struct { classic::CaptionGlyph glyph; const char *file; } buttons[] = {
        {classic::CaptionGlyph::Close, "close"}, {classic::CaptionGlyph::Maximize, "maximize"},
        {classic::CaptionGlyph::Restore, "restore"}, {classic::CaptionGlyph::Minimize, "minimize"},
    };
    for (const auto &b : buttons) {
        QList<std::pair<QString, QImage>> states;
        for (int st = 0; st < 4; ++st) {
            QImage img(l.button, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            {
                QPainter p(&img);
                classic::captionButton(&p, img.rect(), b.glyph, pal, st == 2, st == 3);
            }
            // (A Classic caption button looks the same in an inactive window.)
            const QString name = QLatin1String(kButtonStates[st]);
            states.append({name + QLatin1String("-center"), img});
            states.append({(st == 0 ? QStringLiteral("inactive") : name + QLatin1String("-inactive")) + QLatin1String("-center"), img});
        }
        art.buttons.append({QLatin1String(b.file), states});
    }
}

} // namespace

QImage DecorationArt::element(const QString &id) const
{
    for (const auto &[name, image] : frame)
        if (name == id)
            return image;
    return QImage();
}

DecorationArt decorationArt(const Look &look)
{
    DecorationArt art;
    const DecorationLayout l = decorationLayout(look);
    if (look.classic())
        classicArt(look, l, art);
    else
        styleArt(look, l, art);
    return art;
}

DecorationLayout decorationLayout(const Look &look)
{
    const NonClientMetrics &m = look.system.metrics;
    DecorationLayout l;
    l.frame = m.borderWidth + 3;
    l.caption = m.captionHeight + 1;
    if (look.classic()) {
        // SM_CXSIZE - 2 by SM_CYSIZE - 4; min and max touch, close 2px off.
        l.button = QSize(qMax(8, m.captionWidth - 2), qMax(8, m.captionHeight - 4));
    } else {
        // Square, SM_CYSIZE - 4, 2px apart (measured, Luna).
        l.button = QSize(qMax(8, m.captionHeight - 4), qMax(8, m.captionHeight - 4));
    }
    return l;
}

QString writeDecoration(const Look &look, const QString &directory, QString *error)
{
    const DecorationLayout l = decorationLayout(look);
    const QString id = look.id;
    Package pkg(QDir(directory).filePath(id));
    Rc rc;
    const ClassicScheme &s = look.system;
    rc.set(QStringLiteral("General"), QStringLiteral("TitleAlignment"), QStringLiteral("Left"));
    rc.set(QStringLiteral("General"), QStringLiteral("TitleVerticalAlignment"), QStringLiteral("Center"));
    rc.set(QStringLiteral("General"), QStringLiteral("LeftButtons"), QStringLiteral("M"));
    // Classic's minimize and maximize touch, with a gap before close.
    rc.set(QStringLiteral("General"), QStringLiteral("RightButtons"), look.classic() ? QStringLiteral("HIA_X") : QStringLiteral("HIAX"));
    rc.set(QStringLiteral("General"), QStringLiteral("Shadow"), QStringLiteral("false"));
    rc.set(QStringLiteral("General"), QStringLiteral("ActiveTextColor"), rgba(s.colour(u"TitleText")));
    rc.set(QStringLiteral("General"), QStringLiteral("InactiveTextColor"), rgba(s.colour(u"InactiveTitleText")));

    const DecorationArt art = decorationArt(look);
    SvgSheet svg;
    for (const auto &[id_, image] : art.frame) {
        if (image.isNull())
            svg.hint(id_);
        else
            svg.image(id_, image);
    }
    pkg.write(QStringLiteral("decoration.svg"), svg.document());
    for (const auto &[file, states] : art.buttons) {
        SvgSheet button;
        for (const auto &[state, image] : states)
            button.image(state, image);
        pkg.write(file + QLatin1String(".svg"), button.document());
    }
    if (!look.classic())
        styleShadow(*look.renderer, rc);

    // The geometry: buttons 2px below the frame and 2px in from it; the
    // icon 2px in, the title after it.
    const int F = l.frame, top = F + 2;
    const QString layout = QStringLiteral("Layout");
    rc.set(layout, QStringLiteral("BorderLeft"), F);
    rc.set(layout, QStringLiteral("BorderRight"), F);
    rc.set(layout, QStringLiteral("BorderBottom"), F);
    rc.set(layout, QStringLiteral("BorderTop"), 0);
    rc.set(layout, QStringLiteral("TitleEdgeTop"), top);
    rc.set(layout, QStringLiteral("TitleEdgeBottom"), F + l.caption - top - l.button.height());
    rc.set(layout, QStringLiteral("TitleEdgeLeft"), F + 2);
    rc.set(layout, QStringLiteral("TitleEdgeRight"), F + 2);
    rc.set(layout, QStringLiteral("TitleEdgeTopMaximized"), 2);
    rc.set(layout, QStringLiteral("TitleEdgeBottomMaximized"), l.caption - 2 - l.button.height());
    rc.set(layout, QStringLiteral("TitleEdgeLeftMaximized"), 2);
    rc.set(layout, QStringLiteral("TitleEdgeRightMaximized"), 2);
    rc.set(layout, QStringLiteral("TitleBorderLeft"), look.classic() ? 2 : 4);
    rc.set(layout, QStringLiteral("TitleBorderRight"), 2);
    rc.set(layout, QStringLiteral("TitleHeight"), l.button.height());
    rc.set(layout, QStringLiteral("ButtonWidth"), l.button.width());
    rc.set(layout, QStringLiteral("ButtonHeight"), l.button.height());
    rc.set(layout, QStringLiteral("ButtonWidthMenu"), 16);
    rc.set(layout, QStringLiteral("ButtonSpacing"), look.classic() ? 0 : l.buttonGap);
    rc.set(layout, QStringLiteral("ExplicitButtonSpacer"), l.buttonGap);
    rc.set(layout, QStringLiteral("ButtonMarginTop"), 0);
    rc.set(QLatin1String(kGeneratedKey), QStringLiteral("Look"), look.name);
    pkg.write(id + QLatin1String("rc"), rc.text());

    QJsonObject meta = pluginMetadata(id, look.name, QStringLiteral("Window borders from the Windows XP look \"%1\", made by XPlasma").arg(look.name));
    meta.insert(QStringLiteral("KPackageStructure"), QStringLiteral("Aurorae"));   // as KWin lists them
    pkg.writeJson(QStringLiteral("metadata.json"), meta);
    pkg.write(QStringLiteral("metadata.desktop"),
              QStringLiteral("[Desktop Entry]\nName=%1\nComment=Window borders from the Windows XP look \"%1\", made by XPlasma\n"
                             "X-KDE-PluginInfo-Name=%2\nX-KDE-PluginInfo-Author=XPlasma\nX-KDE-PluginInfo-Version=1.0\n"
                             "X-KDE-PluginInfo-License=Unknown\n%3=true\n")
                  .arg(look.name, id, QLatin1String(kGeneratedKey)).toUtf8());
    if (!pkg.ok()) {
        if (error)
            *error = pkg.error();
        return QString();
    }
    return pkg.directory();
}

} // namespace xpl
