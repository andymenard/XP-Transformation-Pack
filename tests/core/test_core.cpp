// Unit tests for the core's parsing and lookup rules. No test framework:
// each CHECK prints on failure and the exit code counts failures.
#include "core/classic.h"
#include "core/ini.h"
#include "core/pe.h"
#include "core/schema.h"
#include "core/theme.h"
#include "core/themefile.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "core/dib.h"

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

int main(int, char **argv)
{
    vectors = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath() + QLatin1String("/../../tests/core/vectors");

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
        "[Rebar.Band]; a header with a comment is still a header\nImageFile=Band.bmp\n"));

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

    QTextStream(stdout) << (failures ? "FAILED " : "ok ") << failures << " failure(s)\n";
    return failures;
}
