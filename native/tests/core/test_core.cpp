// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the core's parsing and lookup rules. No test framework:
// each CHECK prints on failure and the exit code counts failures.
#include "core/classic.h"
#include "core/ini.h"
#include "core/pe.h"
#include "core/schema.h"
#include "core/theme.h"
#include "core/themefile.h"

#include <limits>   // before QtEndian (Qt 5.15.2)

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QtEndian>

#include "core/dib.h"
#include "core/cursors.h"
#include "core/icons.h"

using namespace xpl;

static int failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ++failures;                                                                    \
            QTextStream(stderr) << "FAIL " << __LINE__ << ": " << #cond << '\n';           \
        }                                                                                  \
    } while (0)

static QString get(const Ini &ini, const char *sub, const char *cls, const char *part, const char *state,
                   const char *prop)
{
    return ini.lookup(QString::fromLatin1(sub), QString::fromLatin1(cls), QString::fromLatin1(part),
                      QString::fromLatin1(state), QString::fromLatin1(prop))
        .value_or(QStringLiteral("<none>"));
}

// Test vectors live in tests/core/vectors; build trees sit in build/<mode>-qt<N>/.
static QString vectors;

int main(int, char **)
{
    vectors = QFileInfo(QString::fromUtf8(__FILE__)).absolutePath() + QLatin1String("/vectors");

    // Headers: every piece but the class is optional; a trailing state
    // belongs to the part, or to the class when there's no part.
    CHECK(Ini::parseHeader(QStringLiteral("Button")).toString() == QLatin1String("button"));
    CHECK(Ini::parseHeader(QStringLiteral("Button.PushButton(Hot)")).toString() == QLatin1String("button.pushbutton(hot)"));
    CHECK(Ini::parseHeader(QStringLiteral("ComboBox(Disabled)")).toString() == QLatin1String("combobox(disabled)"));
    CHECK(Ini::parseHeader(QStringLiteral("ComboBox(Disabled)")).cls == QLatin1String("combobox"));
    CHECK(Ini::parseHeader(QStringLiteral("ComboBox(Disabled)")).state == QLatin1String("disabled"));
    CHECK(Ini::parseHeader(QStringLiteral("TaskBand::Toolbar.Button")).sub == QLatin1String("taskband"));
    CHECK(Ini::parseHeader(QStringLiteral("TaskBand::Toolbar.Button")).cls == QLatin1String("toolbar"));
    CHECK(Ini::parseHeader(QStringLiteral(" Start::Button ")).toString() == QLatin1String("start::button"));

    const Ini ini = Ini::parseText(QStringLiteral(
        "; comment\n"
        "[Globals]\nTextColor=1 1 1\nFont=Tahoma, 8\n"
        "[Button]\nBorderSize=2\n"
        "[Button.PushButton]\nTextColor=2 2 2 ; inline comment\nImageCount = 5\n"
        "[Button.PushButton(Disabled)]\nTextColor=3 3 3\n"
        "[Button(Disabled)]\nFillColor=9 9 9\n"
        "[Media::Button.PushButton]\nImageFile=Media\\Button.bmp\n"
        "[Button.PushButton]\nImageCount=6\n"    // duplicate section: merges, last key wins
        "[Rebar.Band]; a header with a comment is still a header\nImageFile=Band.bmp\n"
        "[Start::Button]\nImageFile=Start.bmp\n"));

    CHECK(get(ini, "", "Button", "PushButton", "", "textcolor") == QLatin1String("2 2 2"));
    CHECK(get(ini, "", "Button", "PushButton", "", "imagecount") == QLatin1String("6"));
    CHECK(get(ini, "", "Button", "PushButton", "Disabled", "textcolor") == QLatin1String("3 3 3"));
    // (part, 0) before (0, 0): Hot has no section of its own.
    CHECK(get(ini, "", "Button", "PushButton", "Hot", "textcolor") == QLatin1String("2 2 2"));
    // (0, 0): the class section.
    CHECK(get(ini, "", "Button", "PushButton", "", "bordersize") == QLatin1String("2"));
    // Globals end the override chain.
    CHECK(get(ini, "", "Button", "PushButton", "", "font") == QLatin1String("Tahoma, 8"));
    CHECK(get(ini, "", "Edit", "", "", "textcolor") == QLatin1String("1 1 1"));
    // A class-level state section is (0, state): not consulted for a real part.
    CHECK(get(ini, "", "Button", "PushButton", "Disabled", "fillcolor") == QLatin1String("<none>"));
    CHECK(get(ini, "", "Button", "", "Disabled", "fillcolor") == QLatin1String("9 9 9"));
    // Sub::Class overrides Class, which still supplies the rest.
    CHECK(get(ini, "Media", "Button", "PushButton", "", "imagefile") == QLatin1String("Media\\Button.bmp"));
    CHECK(get(ini, "Media", "Button", "PushButton", "", "imagecount") == QLatin1String("6"));
    CHECK(get(ini, "", "Button", "PushButton", "", "imagefile") == QLatin1String("<none>"));
    // ...and all of a Sub::Class comes first, its class-level section too:
    // XP draws Start::Button's push button from [Start::Button].
    CHECK(get(ini, "Start", "Button", "PushButton", "Hot", "imagefile") == QLatin1String("Start.bmp"));
    CHECK(get(ini, "Start", "Button", "PushButton", "", "imagecount") == QLatin1String("6"));
    // A commented header still opens its own section.
    CHECK(get(ini, "", "Rebar", "Band", "", "imagefile") == QLatin1String("Band.bmp"));
    CHECK(get(ini, "", "Button", "PushButton", "", "imagefile") == QLatin1String("<none>"));

    // Lookups are case-insensitive.
    CHECK(get(ini, "MEDIA", "button", "PUSHBUTTON", "", "ImageFile") == QLatin1String("Media\\Button.bmp"));

    CHECK(Theme::resourceName(QStringLiteral("Royale\\FrameCaption.bmp")) == QLatin1String("ROYALE_FRAMECAPTION_BMP"));

    // Garbage never crashes the PE reader.
    QString error;
    CHECK(!PeResources::parse(QByteArray("MZ"), &error));
    QByteArray junk(4096, '\xff');
    junk[0] = 'M';
    junk[1] = 'Z';
    CHECK(!PeResources::parse(junk, &error));
    CHECK(!Theme::fromData(QByteArray(), QString(), &error));

    // XP's schema (tmschema.h): names to numbers and back.
    CHECK(schema::partId(u"Button", u"PushButton") == 1);
    CHECK(schema::stateId(u"Button", u"PushButton", u"Defaulted") == 5);
    CHECK(schema::stateId(u"button", u"checkbox", u"MixedDisabled") == 12);
    CHECK(schema::stateId(u"Scrollbar", u"ArrowBtn", u"RightDisabled") == 16);
    CHECK(schema::partId(u"Tab", u"Body") == 10);
    CHECK(schema::partId(u"Window", u"CloseButton") == 18);
    CHECK(schema::partId(u"Button", u"PushButon") == 0);           // typo: unknown
    CHECK(schema::stateId(u"Button", u"PushButton", u"Hover") == 0);
    CHECK(schema::partName(u"Toolbar", 4) == QLatin1String("splitbuttondropdown"));
    CHECK(schema::stateName(u"Tab", u"TabItem", 3) == QLatin1String("selected"));

    // A tiny RLE file claiming a gigantic image is refused, not decoded
    // (found by fuzz-dib: 217 bytes claiming 26886 x 8229 took 11 s).
    {
        QFile vector(vectors + QLatin1String("/dib-huge-rle4.bin"));
        CHECK(vector.open(QIODevice::ReadOnly));
        QElapsedTimer timer;
        timer.start();
        CHECK(decodeDib(vector.readAll()).image.isNull());
        CHECK(timer.elapsed() < 100);
    }

    // .theme files: XP's ANSI with CRLF, UTF-16 with a BOM, UTF-8; sections
    // and keys case-insensitive, the last duplicate winning.
    {
        const ThemeFile t = ThemeFile::fromBytes("; comment\r\n[Theme]\r\nDisplayName=Caf\xe9 \x80\r\n"
                                                 "[VisualStyles]\r\nPath=%ResourceDir%\\Themes\\x.msstyles\r\n"
                                                 "ColorStyle=NormalColor\r\n[visualstyles]\r\ncolorstyle=Metallic\r\n"
                                                 "junk line\r\n=nokey\r\n");
        CHECK(t.value(u"THEME", u"displayname") == QString::fromUtf8("Caf\xc3\xa9 \xe2\x82\xac"));
        CHECK(t.value(u"VisualStyles", u"ColorStyle") == QLatin1String("Metallic"));
        CHECK(t.value(u"VisualStyles", u"Path") == QLatin1String("%ResourceDir%\\Themes\\x.msstyles"));
        CHECK(t.sections() == QStringList({QStringLiteral("Theme"), QStringLiteral("VisualStyles")}));
        CHECK(!t.contains(u"VisualStyles", u"Size") && t.value(u"Nope", u"x").isEmpty());
        // Written back as XP would, and read back the same.
        CHECK(t.toBytes().startsWith("[Theme]\r\nDisplayName=Caf\xe9 \x80\r\n"));
        CHECK(ThemeFile::fromBytes(t.toBytes()).value(u"theme", u"displayname") == t.value(u"theme", u"displayname"));

        QByteArray utf16("\xff\xfe");
        for (const char c : QByteArray("[Theme]\r\nDisplayName=Zune\r\n"))
            utf16 += QByteArray(1, c) + QByteArray(1, '\0');
        CHECK(ThemeFile::fromBytes(utf16).value(u"Theme", u"DisplayName") == QLatin1String("Zune"));
        CHECK(ThemeFile::fromBytes("[Theme]\nDisplayName=\xe2\x84\xa2\n").value(u"Theme", u"DisplayName") == QString(QChar(0x2122)));

        // A value outside Windows-1252 makes the file UTF-16.
        ThemeFile u;
        u.setValue(u"Theme", u"DisplayName", QString(QChar(0x3042)));
        CHECK(u.toBytes().startsWith("\xff\xfe"));
        CHECK(ThemeFile::fromBytes(u.toBytes()).value(u"Theme", u"DisplayName") == QString(QChar(0x3042)));
        u.remove(u"theme", u"displayname");
        CHECK(!u.contains(u"Theme", u"DisplayName"));
        u.removeSection(u"THEME");
        CHECK(u.sections().isEmpty());
    }

    // Windows Classic schemes: colours by either spelling, the metrics and
    // fonts through NONCLIENTMETRICSA / ICONMETRICSA and back.
    {
        const ClassicScheme standard;
        CHECK(standard.colour(u"ButtonFace") == QColor(212, 208, 200));
        CHECK(standard.colour(u"btnface") == QColor(212, 208, 200));
        CHECK(standard.colour(u"GRADIENTACTIVECAPTION") == QColor(166, 202, 240));
        CHECK(ClassicScheme::colourIndex(u"hottracking") == 26 && ClassicScheme::colourIndex(u"nope") == -1);
        CHECK(standard.metrics.captionFont.points() == 8 && standard.metrics.captionFont.bold());

        ClassicScheme s;
        s.setColour(u"Background", QColor(1, 2, 3));
        s.metrics.borderWidth = 3;
        s.metrics.captionHeight = 25;
        s.metrics.menuFont = LogFont::fromPoints(QStringLiteral("Microsoft Sans Serif"), 10);
        s.metrics.captionFont.italic = true;
        s.icons.horzSpacing = 43;
        ThemeFile file;
        s.writeTo(file);
        CHECK(file.value(u"Control Panel\\Colors", u"Background") == QLatin1String("1 2 3"));
        const QByteArray ncm = bytesFromDecimal(file.value(u"Metrics", u"NonclientMetrics"));
        CHECK(ncm.size() == 340 && ncm[0] == char(84) && ncm[1] == char(1) && ncm[4] == 3);
        CHECK(QByteArray(ncm.constData() + 160 + 28) == "Microsoft Sans Serif");
        ClassicScheme back = ClassicScheme::fromThemeFile(ThemeFile::fromBytes(file.toBytes()));
        back.name = s.name;
        CHECK(back == s);
        CHECK(back.metrics.menuFont.points() == 10);

        // A .theme with only some colours keeps Windows Standard's for the rest;
        // truncated or absurd metrics are ignored, not trusted.
        const ClassicScheme partial = ClassicScheme::fromThemeFile(ThemeFile::fromBytes(
            "[Control Panel\\Colors]\nWindow=9 9 9\nMenu=1 2\nBogus=1 1 1\n[Metrics]\nNonclientMetrics=84 1 0 0 1\n"));
        CHECK(partial.colour(u"Window") == QColor(9, 9, 9));
        CHECK(partial.colour(u"Menu") == standard.colour(u"Menu"));
        CHECK(partial.metrics == standard.metrics);
        QByteArray absurd = ncm;
        absurd[20] = char(0xff);
        absurd[21] = char(0xff);   // caption height 65535
        CHECK(!NonClientMetrics::fromA(absurd));
        CHECK(!ClassicScheme::fromSchemeData(QByteArray(100, '\0')));
    }

    // Icons: a 4bpp DIB with its AND mask, twice its height; a 32bpp one
    // with alpha (the mask ignored); PNG; garbage.
    {
        auto le32 = [](QByteArray &b, int v) { for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xff)); };
        auto le16 = [](QByteArray &b, int v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); };
        auto header = [&](int size, int bpp) {
            QByteArray h;
            le32(h, 40); le32(h, size); le32(h, size * 2); le16(h, 1); le16(h, bpp);
            le32(h, 0); le32(h, 0); le32(h, 0); le32(h, 0); le32(h, 0); le32(h, 0);
            return h;
        };
        // 4bpp 8x8: palette index 1 (red) everywhere, the mask's top-left
        // pixel set (transparent). Rows are bottom-up, 4-byte aligned.
        QByteArray four = header(8, 4);
        for (int i = 0; i < 16; ++i)
            le32(four, i == 1 ? 0x00ff0000 : 0);
        for (int y = 0; y < 8; ++y)
            le32(four, 0x11111111);
        for (int y = 0; y < 8; ++y)
            le32(four, y == 7 ? 0x80 : 0);   // bottom-up: the last row is the top
        const QImage a = decodeIconImage(four);
        CHECK(a.size() == QSize(8, 8));
        CHECK(a.pixel(0, 0) == 0);
        CHECK(a.pixel(1, 0) == qRgb(255, 0, 0));
        CHECK(a.pixel(7, 7) == qRgb(255, 0, 0));
        QByteArray argb = header(2, 32);
        for (int i = 0; i < 4; ++i)
            le32(argb, int(0x80102030u));
        le32(argb, 0); le32(argb, 0);   // mask
        const QImage b = decodeIconImage(argb);
        CHECK(b.size() == QSize(2, 2) && qAlpha(b.pixel(0, 0)) == 0x80);
        CHECK(decodeIconImage(QByteArray(30, 'x')).isNull());
        QByteArray huge = header(8, 4);
        huge.truncate(40);
        CHECK(decodeIconImage(huge).isNull());   // claims pixels it doesn't have

        // Cursors. A 1bpp 8x8 DIB: black (index 0) everywhere, index 1
        // (white) in the top row; the mask set in the top-left 2x2 -- over
        // white that inverts the screen (black, for a cursor; gone, for an
        // icon), over black it's transparent.
        auto mono = [&] {
            QByteArray d = header(8, 1);
            le32(d, 0); le32(d, 0x00ffffff);
            for (int y = 0; y < 8; ++y)
                le32(d, y == 7 ? 0xff : 0);   // bottom-up: the top row is last
            for (int y = 0; y < 8; ++y)
                le32(d, y >= 6 ? 0xc0 : 0);
            return d;
        }();
        CHECK(decodeMaskedImage(mono, true).pixel(0, 0) == qRgb(0, 0, 0));
        CHECK(decodeMaskedImage(mono, false).pixel(0, 0) == 0);
        CHECK(decodeMaskedImage(mono, true).pixel(0, 1) == 0);
        CHECK(decodeMaskedImage(mono, true).pixel(5, 0) == qRgb(255, 255, 255));
        // A .cur holding it, hotspot (3, 4); an .ani of it twice, the second
        // step at 12 jiffies (200 ms), played in reverse by its "seq ".
        QByteArray cur;
        le16(cur, 0); le16(cur, 2); le16(cur, 1);
        cur.append(char(8)); cur.append(char(8)); cur.append(char(0)); cur.append(char(0));
        le16(cur, 3); le16(cur, 4); le32(cur, int(mono.size())); le32(cur, 22);
        cur += mono;
        const auto f = decodeCursorFile(cur);
        CHECK(f && f->image.size() == QSize(8, 8) && f->hotspot == QPoint(3, 4));
        auto chunk = [&](const char *id, const QByteArray &body) {
            QByteArray c(id, 4);
            le32(c, int(body.size()));
            c += body;
            if (body.size() % 2)
                c.append('\0');
            return c;
        };
        QByteArray anih;
        for (int v : {36, 2, 2, 8, 8, 1, 1, 6, 1})
            le32(anih, v);
        QByteArray rate, seq;
        le32(rate, 6); le32(rate, 12);
        le32(seq, 1); le32(seq, 0);
        QByteArray second = cur;
        second[10] = 7;   // hotspot x 7
        const QByteArray body = "ACON" + chunk("anih", anih) + chunk("rate", rate) + chunk("seq ", seq)
                                + chunk("LIST", "fram" + chunk("icon", cur) + chunk("icon", second));
        QByteArray ani = "RIFF";
        le32(ani, int(body.size()));
        ani += body;
        const QList<CursorFrame> frames = decodeCursor(ani);
        CHECK(frames.size() == 2);
        CHECK(frames.size() == 2 && frames.at(0).hotspot.x() == 7 && frames.at(0).delayMs == 100);
        CHECK(frames.size() == 2 && frames.at(1).hotspot.x() == 3 && frames.at(1).delayMs == 200);
        CHECK(decodeCursor("RIFF\x04\0\0\0ACON").isEmpty());
        // Xcursor: header, a TOC entry per frame, then each image chunk.
        const QByteArray xc = xcursor(frames, 32);
        CHECK(xc.startsWith("Xcur"));
        CHECK(xc.size() == 16 + 2 * 12 + 2 * (36 + 8 * 8 * 4));
        CHECK(qFromLittleEndian<quint32>(xc.constData() + 12) == 2);
        const qsizetype first = qFromLittleEndian<quint32>(xc.constData() + 16 + 8);
        CHECK(qFromLittleEndian<quint32>(xc.constData() + first + 24) == 7);    // xhot
        CHECK(qFromLittleEndian<quint32>(xc.constData() + first + 32) == 100);  // delay
    }

    QTextStream(stdout) << (failures ? "FAILED " : "ok ") << failures << " failure(s)\n";
    return failures;
}
