// libFuzzer targets for everything that parses untrusted bytes.
//
// Built three times with -DFUZZ_TARGET=theme|dib|ini (see fuzz.sh).
// The plugin is loaded into plasmashell and every Qt app, so a crash here is
// a crashed desktop: nothing a .msstyles contains may crash, hang or read out
// of bounds. AddressSanitizer and UBSan run underneath.
#include "core/dib.h"
#include "core/ini.h"
#include "core/theme.h"

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
#else
#error define FUZZ_TARGET_THEME, FUZZ_TARGET_DIB or FUZZ_TARGET_INI
#endif
    return 0;
}
