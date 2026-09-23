// Unit tests for the core's parsing and lookup rules. No test framework:
// each CHECK prints on failure and the exit code counts failures.
#include "core/ini.h"
#include "core/pe.h"
#include "core/theme.h"

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

// Test vectors live in native/tests/vectors; build trees sit in native/build-*/.
static QString vectors;

int main(int, char **argv)
{
    vectors = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath() + QLatin1String("/../tests/vectors");

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
        "[Button.PushButton]\nImageCount=6\n"));    // duplicate section: merges, last key wins

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

    QTextStream(stdout) << (failures ? "FAILED " : "ok ") << failures << " failure(s)\n";
    return failures;
}
