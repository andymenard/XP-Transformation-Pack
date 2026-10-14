// SPDX-License-Identifier: GPL-3.0-or-later
// libFuzzer targets for everything that parses untrusted bytes.
//
// Built once per target, -DFUZZ_TARGET_<NAME>=1: theme, dib, ini, scheme, icon
// (see fuzz.sh).
// The plugin is loaded into plasmashell and every Qt app, so a crash here is
// a crashed desktop: nothing a .msstyles contains may crash, hang or read out
// of bounds. AddressSanitizer and UBSan run underneath.
#include "core/classic.h"
#include "core/dib.h"
#include "core/icons.h"
#include "core/ini.h"
#include "core/theme.h"
#include "core/themefile.h"

#include <QByteArray>

using namespace xpl;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QByteArray bytes(reinterpret_cast<const char *>(data), qsizetype(size));
#if FUZZ_TARGET_THEME
    QString error;
    if (auto theme = Theme::fromData(bytes, QString(), &error)) {
        for (const QString &name : theme->bitmapNames())
            (void)theme->bitmap(name);
        for (const QString &variant : theme->variants().iniNames)
            (void)Theme::fromData(bytes, variant, &error);
        (void)theme->property(u"Button", u"PushButton", u"Hot", u"ImageFile");
        (void)theme->property(u"Toolbar", u"Button", u"Pressed", u"TextColor", u"Media");
    }
#elif FUZZ_TARGET_DIB
    (void)decodeDib(bytes);
#elif FUZZ_TARGET_INI
    const Ini ini = Ini::parse(bytes);
    for (auto it = ini.sections().cbegin(); it != ini.sections().cend(); ++it)
        for (auto p = it.value().cbegin(); p != it.value().cend(); ++p)
            (void)ini.lookup(it.key().sub, it.key().cls, it.key().part, it.key().state, p.key());
#elif FUZZ_TARGET_SCHEME
    // .theme files (the style reads the Classic scheme's) and SCHEMEDATA.
    const ThemeFile file = ThemeFile::fromBytes(bytes);
    const ClassicScheme scheme = ClassicScheme::fromThemeFile(file);
    ThemeFile out;
    scheme.writeTo(out);
    if (!(ClassicScheme::fromThemeFile(ThemeFile::fromBytes(out.toBytes())).colours == scheme.colours))
        __builtin_trap();   // what we write must read back
    (void)ThemeFile::fromBytes(file.toBytes());
    (void)ClassicScheme::fromSchemeData(bytes);
    (void)NonClientMetrics::fromA(bytes);
    (void)NonClientMetrics::fromW(bytes);
    (void)IconMetrics::fromA(bytes);
#elif FUZZ_TARGET_ICON
    // An RT_ICON payload, and a PE's icon groups.
    (void)decodeIconImage(bytes);
    if (auto pe = PeResources::parse(bytes))
        for (const Resource *group : pe->ofType(u"14"))
            (void)iconGroup(*pe, group->name);
#else
#error define FUZZ_TARGET_THEME, FUZZ_TARGET_DIB, FUZZ_TARGET_INI, FUZZ_TARGET_SCHEME or FUZZ_TARGET_ICON
#endif
    return 0;
}
