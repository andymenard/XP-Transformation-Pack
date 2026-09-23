/* uxdump -- what real uxtheme draws, for XPLasma's pixel oracle.
 *
 *   uxdump.exe <job.txt> <outdir>
 *
 * job.txt, one directive per line ('#' starts a comment):
 *
 *   style  C:\path\Royale.msstyles  NormalColor  NormalSize
 *          -- load a visual style that need not be applied (see below);
 *             colour and size are COLORNAMES/SIZENAMES entries, or "-" for
 *             the style's default
 *   style  -   -- use whatever style is applied to the session
 *   part   <Class> <partId> <stateId> <width> <height>
 *          -- draw it and record uxtheme's metrics for it
 *   bitmap C:\path\file.bmp
 *          -- GDI as the oracle for DIB decoding: load the file, blit it into
 *             a 32bpp DIB as-is (bitmap_<name>_plain), then through
 *             TransparentBlt with the magenta key over black and white
 *             (bitmap_<name>_keyed_black / _white) -- how uxtheme keys
 *
 * For every `part` line the output directory gets
 *   s<N>_<Class>_<part>_<state>_<w>x<h>_black.bmp   drawn over opaque black
 *   s<N>_<Class>_<part>_<state>_<w>x<h>_white.bmp   drawn over opaque white
 * where N counts `style` directives (s0 = before the first one), so the same
 * part from two styles never collides.
 * and one line in metrics.txt. Two backgrounds recover exact per-pixel alpha
 * on the host: alpha = 255 - (white - black), colour = black / (alpha/255).
 *
 * Non-applied styles use uxtheme's undocumented file-level entry points --
 * the ones XP's own Display Properties uses to preview a style before Apply:
 *   ordinal  2  OpenThemeFile(file, colourName, sizeName, &hThemeFile, 0)
 *   ordinal  3  CloseThemeFile(hThemeFile)
 *   ordinal 16  OpenThemeDataFromFile(hThemeFile, hwnd, classList, 0)
 * (ordinals and signatures as Wine's uxtheme.spec documents XP's). If they
 * are missing or refuse the file, uxdump says so and stops rather than
 * silently measuring the applied style instead.
 *
 * No C runtime at all -- only kernel32, user32, gdi32, shell32 and uxtheme --
 * because today's mingw-w64 links the UCRT, which XP doesn't have. Job files
 * are ASCII.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <shellapi.h>
#include <uxtheme.h>

WINGDIAPI BOOL WINAPI GdiTransparentBlt(HDC, int, int, int, int, HDC, int, int, int, int, UINT);

#ifndef TMT_SIZINGMARGINS
#define TMT_SIZINGMARGINS 3601
#define TMT_CONTENTMARGINS 3602
#endif

typedef HANDLE HTHEMEFILE;
typedef HRESULT (WINAPI *OpenThemeFile_t)(LPCWSTR, LPCWSTR, LPCWSTR, HTHEMEFILE *, DWORD);
typedef HRESULT (WINAPI *CloseThemeFile_t)(HTHEMEFILE);
typedef HTHEME  (WINAPI *OpenThemeDataFromFile_t)(HTHEMEFILE, HWND, LPCWSTR, DWORD);

static OpenThemeFile_t pOpenThemeFile;
static CloseThemeFile_t pCloseThemeFile;
static OpenThemeDataFromFile_t pOpenThemeDataFromFile;
static HTHEMEFILE themeFile;        /* NULL: use the applied style */
static HWND hwnd;
static HANDLE metrics = INVALID_HANDLE_VALUE;
static int styleIndex;          /* bumped by every `style` directive */

/* Freestanding: GCC may still emit calls to these. */
void *memset(void *d, int c, unsigned n)
{
    volatile unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned n)
{
    volatile unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

/* Append a wsprintf-formatted line to metrics.txt, as ASCII. */
static void out(const wchar_t *fmt, ...)
{
    wchar_t w[1024];
    char a[1024];
    DWORD n, done;
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(w, fmt, ap);
    va_end(ap);
    n = WideCharToMultiByte(CP_ACP, 0, w, -1, a, sizeof a, NULL, NULL);
    if (n > 0)
        WriteFile(metrics, a, n - 1, &done, NULL);
}

static int write_file(const wchar_t *path, const void *a, DWORD na, const void *b, DWORD nb)
{
    DWORD done;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    int ok;
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(f, a, na, &done, NULL) && done == na && WriteFile(f, b, nb, &done, NULL) && done == nb;
    CloseHandle(f);
    return ok;
}

static int write_bmp(const wchar_t *path, const DWORD *bits, int w, int h)
{
    struct {
        BITMAPFILEHEADER fh;
        BITMAPINFOHEADER ih;
    } __attribute__((packed)) hdr;
    DWORD size = (DWORD)w * h * 4;
    ZeroMemory(&hdr, sizeof hdr);
    hdr.fh.bfType = 0x4D42;
    hdr.fh.bfOffBits = sizeof hdr;
    hdr.fh.bfSize = sizeof hdr + size;
    hdr.ih.biSize = sizeof hdr.ih;
    hdr.ih.biWidth = w;
    hdr.ih.biHeight = -h;            /* top-down, like the DIB section */
    hdr.ih.biPlanes = 1;
    hdr.ih.biBitCount = 32;
    hdr.ih.biCompression = BI_RGB;
    hdr.ih.biSizeImage = size;
    return write_file(path, &hdr, sizeof hdr, bits, size);
}

/* Draw one part over a solid background and save it. */
static int render(HTHEME theme, int part, int state, int w, int h, COLORREF bg, const wchar_t *path)
{
    BITMAPINFO bi;
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp, old;
    HBRUSH brush;
    RECT rc;
    HRESULT hr;
    int ok;

    SetRect(&rc, 0, 0, w, h);
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) {
        DeleteDC(dc);
        return 0;
    }
    old = (HBITMAP)SelectObject(dc, bmp);
    brush = CreateSolidBrush(bg);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
    GdiFlush();
    hr = DrawThemeBackground(theme, dc, part, state, &rc, NULL);
    GdiFlush();
    ok = SUCCEEDED(hr) && write_bmp(path, (const DWORD *)bits, w, h);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    return ok;
}

static void do_part(const wchar_t *cls, int part, int state, int w, int h, const wchar_t *outdir)
{
    HTHEME theme = themeFile ? pOpenThemeDataFromFile(themeFile, hwnd, cls, 0)
                             : OpenThemeData(hwnd, cls);
    wchar_t path[MAX_PATH];
    SIZE smin, strue, sdraw;
    RECT rc, content;
    MARGINS sizing, contentm;
    HDC screen;
    int drawn;

    if (!theme) {
        out(L"%s %d %d %d %d error=no-theme-data\r\n", cls, part, state, w, h);
        return;
    }
    ZeroMemory(&smin, sizeof smin); ZeroMemory(&strue, sizeof strue); ZeroMemory(&sdraw, sizeof sdraw);
    ZeroMemory(&content, sizeof content); ZeroMemory(&sizing, sizeof sizing); ZeroMemory(&contentm, sizeof contentm);
    SetRect(&rc, 0, 0, w, h);
    wsprintfW(path, L"%s\\s%d_%s_%d_%d_%dx%d_black.bmp", outdir, styleIndex, cls, part, state, w, h);
    drawn = render(theme, part, state, w, h, RGB(0, 0, 0), path);
    wsprintfW(path, L"%s\\s%d_%s_%d_%d_%dx%d_white.bmp", outdir, styleIndex, cls, part, state, w, h);
    drawn = render(theme, part, state, w, h, RGB(255, 255, 255), path) && drawn;

    screen = GetDC(NULL);
    GetThemePartSize(theme, screen, part, state, NULL, TS_MIN, &smin);
    GetThemePartSize(theme, screen, part, state, NULL, TS_TRUE, &strue);
    GetThemePartSize(theme, screen, part, state, &rc, TS_DRAW, &sdraw);
    GetThemeBackgroundContentRect(theme, screen, part, state, &rc, &content);
    GetThemeMargins(theme, screen, part, state, TMT_SIZINGMARGINS, NULL, &sizing);
    GetThemeMargins(theme, screen, part, state, TMT_CONTENTMARGINS, NULL, &contentm);
    ReleaseDC(NULL, screen);
    out(L"s%d %s %d %d %d %d drawn=%d min=%d,%d true=%d,%d draw=%d,%d content=%d,%d,%d,%d "
        L"sizing=%d,%d,%d,%d contentmargins=%d,%d,%d,%d\r\n",
        styleIndex, cls, part, state, w, h, drawn, smin.cx, smin.cy, strue.cx, strue.cy, sdraw.cx, sdraw.cy,
        content.left, content.top, content.right, content.bottom,
        sizing.cxLeftWidth, sizing.cxRightWidth, sizing.cyTopHeight, sizing.cyBottomHeight,
        contentm.cxLeftWidth, contentm.cxRightWidth, contentm.cyTopHeight, contentm.cyBottomHeight);
    CloseThemeData(theme);
}

/* One 32bpp top-down DIB section over a solid colour; returns its DC. */
static HDC canvas(int w, int h, COLORREF bg, void **bits, HBITMAP *bmp)
{
    BITMAPINFO bi;
    HDC dc = CreateCompatibleDC(NULL);
    RECT rc;
    HBRUSH brush;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    *bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, bits, NULL, 0);
    SelectObject(dc, *bmp);
    SetRect(&rc, 0, 0, w, h);
    brush = CreateSolidBrush(bg);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
    return dc;
}

static void do_bitmap(const wchar_t *file, const wchar_t *outdir)
{
    HBITMAP src = (HBITMAP)LoadImageW(NULL, file, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    BITMAP info;
    HDC from;
    const wchar_t *name = file, *p;
    wchar_t path[MAX_PATH];
    int i;
    static const struct { const wchar_t *suffix; COLORREF bg; int keyed; } passes[] = {
        {L"plain", RGB(0, 0, 0), 0}, {L"keyed_black", RGB(0, 0, 0), 1}, {L"keyed_white", RGB(255, 255, 255), 1},
    };
    for (p = file; *p; p++)
        if (*p == L'\\') name = p + 1;
    if (!src) {
        out(L"bitmap %s error=load-failed(%lu)\r\n", name, GetLastError());
        return;
    }
    GetObjectW(src, sizeof info, &info);
    from = CreateCompatibleDC(NULL);
    SelectObject(from, src);
    for (i = 0; i < 3; i++) {
        void *bits;
        HBITMAP bmp;
        HDC dc = canvas(info.bmWidth, info.bmHeight, passes[i].bg, &bits, &bmp);
        if (passes[i].keyed)
            GdiTransparentBlt(dc, 0, 0, info.bmWidth, info.bmHeight, from, 0, 0, info.bmWidth, info.bmHeight,
                              RGB(255, 0, 255));
        else
            BitBlt(dc, 0, 0, info.bmWidth, info.bmHeight, from, 0, 0, SRCCOPY);
        GdiFlush();
        wsprintfW(path, L"%s\\bitmap_%s_%s.bmp", outdir, name, passes[i].suffix);
        write_bmp(path, (const DWORD *)bits, info.bmWidth, info.bmHeight);
        DeleteDC(dc);
        DeleteObject(bmp);
    }
    out(L"bitmap %s %dx%d bpp=%d\r\n", name, info.bmWidth, info.bmHeight, info.bmBitsPixel);
    DeleteDC(from);
    DeleteObject(src);
}

static int do_style(const wchar_t *file, const wchar_t *colour, const wchar_t *size)
{
    HRESULT hr;
    styleIndex++;
    if (themeFile) {
        pCloseThemeFile(themeFile);
        themeFile = NULL;
    }
    if (lstrcmpW(file, L"-") == 0) {
        out(L"# style s%d: applied (IsThemeActive=%d)\r\n", styleIndex, IsThemeActive());
        return 1;
    }
    if (!pOpenThemeFile || !pOpenThemeDataFromFile || !pCloseThemeFile) {
        out(L"# error: uxtheme has no OpenThemeFile/OpenThemeDataFromFile ordinals\r\n");
        return 0;
    }
    /* "-" asks for the style's own default, as XP passes NULL. */
    hr = pOpenThemeFile(file, lstrcmpW(colour, L"-") ? colour : NULL, lstrcmpW(size, L"-") ? size : NULL,
                        &themeFile, 0);
    out(L"# style s%d: %s %s %s hr=0x%08lx\r\n", styleIndex, file, colour, size, (unsigned long)hr);
    if (FAILED(hr)) {
        themeFile = NULL;
        return 0;
    }
    return 1;
}

/* --- job parsing: whitespace-separated tokens, one directive per line ---- */

static int is_space(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r'; }

/* Split `line` in place into at most `max` tokens; returns the count. */
static int tokens(wchar_t *line, wchar_t **tok, int max)
{
    int n = 0;
    while (*line && n < max) {
        while (is_space(*line)) *line++ = 0;
        if (!*line) break;
        tok[n++] = line;
        while (*line && !is_space(*line)) line++;
    }
    return n;
}

static int to_int(const wchar_t *s)
{
    int v = 0, neg = 0;
    if (*s == L'-') { neg = 1; s++; }
    while (*s >= L'0' && *s <= L'9') v = v * 10 + (*s++ - L'0');
    return neg ? -v : v;
}

static int run(int argc, wchar_t **argv)
{
    HANDLE job;
    DWORD size, got;
    char *raw;
    wchar_t *text, *line, *next, path[MAX_PATH];
    HMODULE ux;
    OSVERSIONINFOW v;

    if (argc < 3)
        return 2;
    CreateDirectoryW(argv[2], NULL);
    wsprintfW(path, L"%s\\metrics.txt", argv[2]);
    metrics = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    job = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (metrics == INVALID_HANDLE_VALUE || job == INVALID_HANDLE_VALUE)
        return 2;
    size = GetFileSize(job, NULL);
    raw = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 1);
    text = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size + 1) * sizeof(wchar_t));
    ReadFile(job, raw, size, &got, NULL);
    CloseHandle(job);
    MultiByteToWideChar(CP_ACP, 0, raw, (int)got, text, (int)size + 1);

    ux = LoadLibraryW(L"uxtheme.dll");
    pOpenThemeFile = (OpenThemeFile_t)GetProcAddress(ux, MAKEINTRESOURCEA(2));
    pCloseThemeFile = (CloseThemeFile_t)GetProcAddress(ux, MAKEINTRESOURCEA(3));
    pOpenThemeDataFromFile = (OpenThemeDataFromFile_t)GetProcAddress(ux, MAKEINTRESOURCEA(16));
    /* Any window will do: uxtheme only wants one to hang theme data off. */
    hwnd = CreateWindowExW(0, L"STATIC", L"uxdump", WS_POPUP, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    v.dwOSVersionInfoSize = sizeof v;
    GetVersionExW(&v);
    out(L"# windows %lu.%lu.%lu %s; ordinals 2/3/16: %d/%d/%d\r\n",
        v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, v.szCSDVersion,
        pOpenThemeFile != NULL, pCloseThemeFile != NULL, pOpenThemeDataFromFile != NULL);

    for (line = text; line && *line; line = next) {
        wchar_t *tok[16];
        int n;
        next = line;
        while (*next && *next != L'\n') next++;
        if (*next) *next++ = 0;
        n = tokens(line, tok, 16);
        if (n == 0 || tok[0][0] == L'#')
            continue;
        if (lstrcmpW(tok[0], L"part") == 0 && n == 6) {
            do_part(tok[1], to_int(tok[2]), to_int(tok[3]), to_int(tok[4]), to_int(tok[5]), argv[2]);
        } else if (lstrcmpW(tok[0], L"bitmap") == 0 && n >= 2) {
            int i;
            for (i = 2; i < n; i++)
                tok[i][-1] = L' ';
            do_bitmap(tok[1], argv[2]);
        } else if (lstrcmpW(tok[0], L"style") == 0 && n == 2 && lstrcmpW(tok[1], L"-") == 0) {
            do_style(L"-", NULL, NULL);
        } else if (lstrcmpW(tok[0], L"style") == 0 && n >= 4) {
            /* The path may contain spaces: it is everything but the last two tokens. */
            int i;
            for (i = 2; i < n - 2; i++)
                tok[i][-1] = L' ';
            if (!do_style(tok[1], tok[n - 2], tok[n - 1]))
                return 1;
        } else {
            out(L"# ignored: %s\r\n", tok[0]);
        }
    }
    out(L"# done\r\n");
    CloseHandle(metrics);
    return 0;
}

void __stdcall entry(void)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    ExitProcess(run(argc, argv));
}
