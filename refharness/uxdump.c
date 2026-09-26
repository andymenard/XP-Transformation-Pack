// SPDX-License-Identifier: GPL-3.0-or-later
/* uxdump -- what real uxtheme draws, for XPlasma's pixel oracle.
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
 *   control <kind> <w> <h> <state> [label ...]
 *          -- a real common control (comctl32 v6, so themed by the *applied*
 *             style), at w x h on a button-face background, captured with
 *             WM_PRINT: control_<kind>_<state>_<w>x<h>.bmp. Kinds: push,
 *             default, check, check-on, check-mixed, radio, radio-on, group,
 *             edit, combo, combo-edit, progress:<0-100>, trackbar:<0-100>,
 *             tabs, listview, treeview, listbox, hscroll, vscroll, spin,
 *             status, toolbar, tooltip, menu, menubar. States: normal,
 *             disabled, focused, pressed (BM_SETSTATE), hot (menus: the
 *             first item highlighted). Font: the theme's message font.
 *             Menus and tooltips are top-level windows at the capture's
 *             origin, so their drop shadows land on the button face.
 *   apply <file> <colour> <size>
 *          -- make a style the session's visual style (uxtheme ordinal 65,
 *             SetSystemVisualStyle -- what Display Properties does), so
 *             later `control` lines are drawn by it. The VM runs from a
 *             snapshot: nothing outlives the session.
 *   classic
 *          -- turn visual styles off for the session (EnableTheming(FALSE),
 *             flat menus off): the Windows Classic look, in whatever system
 *             colours are set -- `syscolor` lines set a scheme's.
 *   syscolor <COLOR_ index> <r> <g> <b>
 *          -- SetSysColors for one colour.
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
#include <commctrl.h>
#include <commdlg.h>

/* tmschema.h's system font ids (not in mingw's uxtheme.h). */
#ifndef TMT_CAPTIONFONT
#define TMT_CAPTIONFONT 801
#define TMT_SMALLCAPTIONFONT 802
#define TMT_MENUFONT 803
#define TMT_STATUSFONT 804
#define TMT_MSGBOXFONT 805
#endif

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
typedef HRESULT (WINAPI *SetSystemVisualStyle_t)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD);
typedef HRESULT (WINAPI *EnableTheming_t)(BOOL);
static EnableTheming_t pEnableTheming;
static SetSystemVisualStyle_t pSetSystemVisualStyle;
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

static void do_part(const wchar_t *spec, int part, int state, int w, int h, const wchar_t *outdir)
{
    /* SUB::CLASS is drawn as an application draws it: SetWindowTheme(hwnd,
       SUB, NULL), then the plain class opened on that window (uxtheme doesn't
       take "Sub::Class" as a class name). Files name it SUB--CLASS. */
    wchar_t sub[64], cls_[64], name[132];
    const wchar_t *cls = spec, *p;
    HTHEME theme;
    wchar_t path[MAX_PATH];
    SIZE smin, strue, sdraw;
    RECT rc, content;
    MARGINS sizing, contentm;
    HDC screen;
    int drawn;

    sub[0] = 0;
    lstrcpynW(name, spec, 132);
    for (p = spec; *p; p++)
        if (p[0] == L':' && p[1] == L':' && p - spec < 63) {
            lstrcpynW(sub, spec, (int)(p - spec) + 1);
            lstrcpynW(cls_, p + 2, 64);
            cls = cls_;
            wsprintfW(name, L"%s--%s", sub, cls_);
            break;
        }
    if (sub[0])
        SetWindowTheme(hwnd, sub, NULL);
    theme = themeFile ? pOpenThemeDataFromFile(themeFile, hwnd, cls, 0) : OpenThemeData(hwnd, cls);
    if (!theme) {
        out(L"%s %d %d %d %d error=no-theme-data\r\n", spec, part, state, w, h);
        if (sub[0])
            SetWindowTheme(hwnd, NULL, NULL);
        return;
    }
    ZeroMemory(&smin, sizeof smin); ZeroMemory(&strue, sizeof strue); ZeroMemory(&sdraw, sizeof sdraw);
    ZeroMemory(&content, sizeof content); ZeroMemory(&sizing, sizeof sizing); ZeroMemory(&contentm, sizeof contentm);
    SetRect(&rc, 0, 0, w, h);
    wsprintfW(path, L"%s\\s%d_%s_%d_%d_%dx%d_black.bmp", outdir, styleIndex, name, part, state, w, h);
    drawn = render(theme, part, state, w, h, RGB(0, 0, 0), path);
    wsprintfW(path, L"%s\\s%d_%s_%d_%d_%dx%d_white.bmp", outdir, styleIndex, name, part, state, w, h);
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
        styleIndex, spec, part, state, w, h, drawn, smin.cx, smin.cy, strue.cx, strue.cy, sdraw.cx, sdraw.cy,
        content.left, content.top, content.right, content.bottom,
        sizing.cxLeftWidth, sizing.cxRightWidth, sizing.cyTopHeight, sizing.cyBottomHeight,
        contentm.cxLeftWidth, contentm.cxRightWidth, contentm.cyTopHeight, contentm.cyBottomHeight);
    CloseThemeData(theme);
    if (sub[0])
        SetWindowTheme(hwnd, NULL, NULL);
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

static LRESULT CALLBACK host_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    /* A scroll bar control only reports; its owner moves it, as apps do. */
    wchar_t cls[16];
    if ((m == WM_HSCROLL || m == WM_VSCROLL) && lp && GetClassNameW((HWND)lp, cls, 16) && !lstrcmpiW(cls, L"ScrollBar")) {
        SCROLLINFO si;
        int pos;
        si.cbSize = sizeof si;
        si.fMask = SIF_ALL;
        GetScrollInfo((HWND)lp, SB_CTL, &si);
        pos = si.nPos;
        switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= 1; break;
        case SB_LINEDOWN: pos += 1; break;
        case SB_PAGEUP: pos -= (int)si.nPage; break;
        case SB_PAGEDOWN: pos += (int)si.nPage; break;
        case SB_THUMBTRACK: pos = si.nTrackPos; break;
        default: return 0;
        }
        si.fMask = SIF_POS;
        si.nPos = pos;
        SetScrollInfo((HWND)lp, SB_CTL, &si, TRUE);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

/* Set NONCLIENTMETRICSA from hex (a `metrics` dump, or a .theme's
 * NonclientMetrics); TRUE on success. */
static BOOL set_metrics_hex(const wchar_t *p)
{
    static BYTE ncm[sizeof(NONCLIENTMETRICSA)];
    unsigned len = 0;
    ZeroMemory(ncm, sizeof ncm);
    while (p[0] && p[1] && len < sizeof ncm) {
        int hi = p[0] <= L'9' ? p[0] - L'0' : (p[0] | 0x20) - L'a' + 10;
        int lo = p[1] <= L'9' ? p[1] - L'0' : (p[1] | 0x20) - L'a' + 10;
        ncm[len++] = (BYTE)(hi * 16 + lo);
        p += 2;
    }
    ((NONCLIENTMETRICSA *)ncm)->cbSize = sizeof(NONCLIENTMETRICSA);
    return SystemParametersInfoA(SPI_SETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSA), ncm, SPIF_SENDCHANGE);
}

/* comctl32 caches the system metrics (scroll bar sizes: an up-down's width,
 * a trackbar's thumb) at load, refreshing them on a control's
 * WM_SETTINGCHANGE: refresh them before any control of ours is made. */
static void refresh_comctl32(void)
{
    INITCOMMONCONTROLSEX icc;
    HWND probe;
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    probe = CreateWindowExW(0, UPDOWN_CLASSW, NULL, WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    SendMessageW(probe, WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, 0);
    DestroyWindow(probe);
}

static INT_PTR CALLBACK page_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    return FALSE;
}

static int to_int(const wchar_t *s);
static int wcsncmp_(const wchar_t *s, const wchar_t *prefix);

/* Grab w x h of the screen at the capture origin (40,40) and save it. */
static void grab(const wchar_t *kind, const wchar_t *state, int w, int h, const wchar_t *outdir)
{
    wchar_t path[MAX_PATH];
    void *bits;
    HBITMAP bmp;
    HDC dc = canvas(w, h, GetSysColor(COLOR_BTNFACE), &bits, &bmp), screen;
    int i;
    screen = GetDC(NULL);
    BitBlt(dc, 0, 0, w, h, screen, 40, 40, SRCCOPY | CAPTUREBLT);
    ReleaseDC(NULL, screen);
    GdiFlush();
    wsprintfW(path, L"%s\\control_%s_%s_%dx%d.bmp", outdir, kind, state, w, h);
    for (i = 0; path[i]; i++) if (path[i] == L':' && i > 2) path[i] = L'-';
    write_bmp(path, (const DWORD *)bits, w, h);
    out(L"control %s %s %d %d face=%lu\r\n", kind, state, w, h, (unsigned long)GetSysColor(COLOR_BTNFACE));
    DeleteDC(dc);
    DeleteObject(bmp);
}

/* Let a window finish painting (and any animation), pumping messages. */
static void settle(DWORD ms)
{
    DWORD until = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < until) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    }
}

static struct { const wchar_t *kind, *state, *outdir; int w, h; } pendingMenu, pendingPress;

/* pressed-X-Y: grabbed with the button still down -- from a timer, since a
 * scroll bar tracks the press in a modal loop of its own -- then released. */
static void CALLBACK press_timer(HWND w, UINT m, UINT_PTR id, DWORD t)
{
    KillTimer(w, id);
    grab(pendingPress.kind, pendingPress.state, pendingPress.w, pendingPress.h, pendingPress.outdir);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    pendingPress.kind = NULL;
}

/* Runs inside TrackPopupMenu's modal loop, once the menu has shown. */
static void CALLBACK menu_timer(HWND w, UINT m, UINT_PTR id, DWORD t)
{
    HWND popup = FindWindowW(L"#32768", NULL);
    RECT r;
    KillTimer(w, id);
    grab(pendingMenu.kind, pendingMenu.state, pendingMenu.w, pendingMenu.h, pendingMenu.outdir);
    if (popup && GetWindowRect(popup, &r))
        out(L"geometry %s window=%d,%d,%d,%d\r\n", pendingMenu.kind, r.left - 40, r.top - 40, r.right - 40, r.bottom - 40);
    EndMenu();
}

/* The menus both sides build: no mnemonics (XP hides the underlines of a
 * menu opened with the mouse). */
static HMENU build_menu(void)
{
    HMENU menu = CreatePopupMenu(), sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING, 20, L"Thumbnails");
    AppendMenuW(menu, MF_STRING, 1, L"Open\tCtrl+O");
    AppendMenuW(menu, MF_STRING, 2, L"Save As...");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | MF_CHECKED, 3, L"Status Bar");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, L"View");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 4, L"Print");
    AppendMenuW(menu, MF_STRING, 5, L"Exit");
    return menu;
}

/* An ordinary overlapped window (caption, sizing frame) with menu bar
 * `bar`, placed so that the menu bar's top-left is the capture origin and
 * its menu bar and client together are w x h. */
static HWND framed_host(int w, int h, HMENU bar)
{
    RECT wr, mr;
    HWND host;
    SetRect(&wr, 40, 40, 40 + w, 40 + h);
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);   /* (the menu bar comes out of the h) */
    host = CreateWindowExW(WS_EX_TOPMOST, L"UxdumpHost", L"Frame", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                           wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, NULL, bar, NULL, NULL);
    GetMenuItemRect(host, bar, 0, &mr);
    SetWindowPos(host, NULL, wr.left + (40 - mr.left), wr.top + (40 - mr.top), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    return host;
}

static void do_menu(const wchar_t *kind, const wchar_t *state, int w, int h, const wchar_t *outdir)
{
    HWND host;
    HMENU bar = NULL, menu = NULL;
    const int menubar = !wcsncmp_(kind, L"menubar");
    if (menubar) {
        bar = CreateMenu();
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)build_menu(), L"File");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)CreatePopupMenu(), L"Edit");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)CreatePopupMenu(), L"View");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)CreatePopupMenu(), L"Help");
    }
    if (menubar && kind[7] == L':') {
        /* menubar:1 -- in an ordinary overlapped window: the line under the
         * bar is its own (a bare popup's differs). */
        host = framed_host(w, h, bar);
    } else {
        host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"UxdumpHost", L"", WS_POPUP | WS_VISIBLE, 40, 40, w, h,
                               NULL, bar, NULL, NULL);
    }
    SetCursorPos(700, 560);
    if (menubar) {
        RECT item;
        DrawMenuBar(host);
        GetMenuItemRect(host, bar, 0, &item);
        if (!lstrcmpW(state, L"hot")) {
            /* The pointer over the first title: hot tracking (raised in
             * Classic). HiliteMenuItem would show it open (sunken). */
            SetForegroundWindow(host);
            settle(100);
            SetCursorPos((item.left + item.right) / 2, (item.top + item.bottom) / 2);
            settle(50);
            SetCursorPos((item.left + item.right) / 2 + 1, (item.top + item.bottom) / 2);
        }
        settle(250);
        grab(kind, state, w, h, outdir);
        out(L"geometry %s item0=%d,%d,%d,%d\r\n", kind, item.left - 40, item.top - 40, item.right - 40, item.bottom - 40);
    } else {
        menu = build_menu();
        if (!lstrcmpW(state, L"hot")) {
            MENUITEMINFOW mi;
            ZeroMemory(&mi, sizeof mi);
            mi.cbSize = sizeof mi;
            mi.fMask = MIIM_STATE;
            mi.fState = MFS_HILITE;
            SetMenuItemInfoW(menu, 0, TRUE, &mi);
        }
        settle(100);
        pendingMenu.kind = kind; pendingMenu.state = state; pendingMenu.outdir = outdir;
        pendingMenu.w = w; pendingMenu.h = h;
        SetForegroundWindow(host);
        SetTimer(host, 7, 600, menu_timer);
        TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, 40, 40, 0, host, NULL);
        DestroyMenu(menu);
    }
    DestroyWindow(host);
    if (bar) DestroyMenu(bar);
}

static struct { const wchar_t *kind, *state, *outdir; int w, h; } pendingDialog;

/* The Open dialog's hook: once it's up, move it to the capture origin, let
 * it paint, grab it and cancel it. */
static UINT_PTR CALLBACK dialog_hook(HWND dlg, UINT m, WPARAM wp, LPARAM lp)
{
    HWND top = GetParent(dlg);
    if (m == WM_NOTIFY && ((OFNOTIFYW *)lp)->hdr.code == CDN_INITDONE) {
        SetWindowPos(top, HWND_TOPMOST, 40, 40, pendingDialog.w, pendingDialog.h, SWP_SHOWWINDOW);
        SetCursorPos(700, 560);
        SetTimer(dlg, 9, 2000, NULL);
    } else if (m == WM_TIMER && wp == 9) {
        RECT r;
        KillTimer(dlg, 9);
        grab(pendingDialog.kind, pendingDialog.state, pendingDialog.w, pendingDialog.h, pendingDialog.outdir);
        GetWindowRect(top, &r);
        out(L"geometry %s window=%d,%d,%d,%d\r\n", pendingDialog.kind, r.left - 40, r.top - 40, r.right - 40, r.bottom - 40);
        PostMessageW(top, WM_COMMAND, IDCANCEL, 0);
    }
    return 0;
}

/* filedialog: comdlg32's Open dialog (the Places bar, the shell view). */
static void do_filedialog(const wchar_t *kind, const wchar_t *state, int w, int h, const wchar_t *outdir)
{
    OPENFILENAMEW ofn;
    wchar_t file[MAX_PATH];
    file[0] = 0;
    ZeroMemory(&ofn, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.lpstrInitialDir = L"C:\\WINDOWS";
    ofn.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLESIZING;
    ofn.lpfnHook = dialog_hook;
    pendingDialog.kind = kind; pendingDialog.state = state; pendingDialog.outdir = outdir;
    pendingDialog.w = w; pendingDialog.h = h;
    GetOpenFileNameW(&ofn);
}

static LRESULT CALLBACK doc_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    return DefMDIChildProcW(w, m, wp, lp);
}

/* mdi: an MDI client (with its client edge, as apps create it) and one
 * document child at 10,10 200x120 captioned LABEL; `inactive` deactivates
 * its caption, `maximized` maximizes it. */
static void do_mdi(const wchar_t *kind, const wchar_t *state, int w, int h, const wchar_t *label,
                   const wchar_t *outdir)
{
    static int registered;
    CLIENTCREATESTRUCT ccs;
    MDICREATESTRUCTW mcs;
    HWND host, client, child;
    HMENU bar = NULL;
    RECT r, cr;
    const int maximized = !wcsncmp_(state, L"maximized");
    if (!registered) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof wc);
        wc.lpfnWndProc = doc_proc;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"UxdumpDoc";
        RegisterClassW(&wc);
        registered = 1;
    }
    if (maximized) {
        /* A maximized child's icon and buttons move into the frame's menu
         * bar: give the frame one. */
        bar = CreateMenu();
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)CreatePopupMenu(), L"File");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)CreatePopupMenu(), L"Edit");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)CreatePopupMenu(), L"View");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)CreatePopupMenu(), L"Help");
    }
    host = maximized ? framed_host(w, h, bar)
                     : CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"UxdumpHost", L"",
                                       WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, 40, 40, w, h, NULL, bar, NULL, NULL);
    GetClientRect(host, &cr);
    ccs.hWindowMenu = NULL;
    ccs.idFirstChild = 1000;
    client = CreateWindowExW(WS_EX_CLIENTEDGE, L"MDICLIENT", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0,
                             cr.right, cr.bottom, host, (HMENU)1, NULL, &ccs);
    ZeroMemory(&mcs, sizeof mcs);
    mcs.szClass = L"UxdumpDoc";
    mcs.szTitle = label[0] ? label : L"Document";
    mcs.x = 10; mcs.y = 10; mcs.cx = 200; mcs.cy = 120;
    mcs.style = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS;
    child = (HWND)SendMessageW(client, WM_MDICREATE, 0, (LPARAM)&mcs);
    SendMessageW(host, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    SetForegroundWindow(host);
    settle(100);
    if (!lstrcmpW(state, L"inactive"))
        SendMessageW(child, WM_NCACTIVATE, FALSE, 0);
    if (maximized) {
        SendMessageW(client, WM_MDIMAXIMIZE, (WPARAM)child, 0);
        DrawMenuBar(host);
    }
    SetCursorPos(700, 560);
    settle(400);
    grab(kind, state, w, h, outdir);
    GetWindowRect(child, &r);
    out(L"geometry %s child=%d,%d,%d,%d caption=%d frame=%d,%d\r\n", kind, r.left - 40, r.top - 40, r.right - 40,
        r.bottom - 40, GetSystemMetrics(SM_CYCAPTION), GetSystemMetrics(SM_CXSIZEFRAME), GetSystemMetrics(SM_CYSIZEFRAME));
    DestroyWindow(host);
    if (bar) DestroyMenu(bar);
}

static void do_tooltip(const wchar_t *kind, const wchar_t *state, int w, int h, const wchar_t *label,
                       const wchar_t *outdir)
{
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"UxdumpHost", L"", WS_POPUP | WS_VISIBLE,
                                40, 40, w, h, NULL, NULL, NULL, NULL);
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                               0, 0, 0, 0, host, NULL, NULL, NULL);
    TOOLINFOW ti;
    RECT r;
    ZeroMemory(&ti, sizeof ti);
    ti.cbSize = sizeof ti;
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    ti.hwnd = host;
    ti.lpszText = (wchar_t *)label;
    SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    SendMessageW(tip, TTM_TRACKPOSITION, 0, MAKELPARAM(40, 40));
    SendMessageW(tip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    SetCursorPos(700, 560);
    settle(600);
    grab(kind, state, w, h, outdir);
    GetWindowRect(tip, &r);
    out(L"geometry %s window=%d,%d,%d,%d\r\n", kind, r.left - 40, r.top - 40, r.right - 40, r.bottom - 40);
    DestroyWindow(tip);
    DestroyWindow(host);
}


/* A control, captured the way it paints itself into a parent: parent
 * background (button face) first, then the control via WM_PRINT. */
static void do_control(wchar_t **tok, int n, const wchar_t *outdir)
{
    static int registered;
    const wchar_t *kind = tok[1];
    const int w = to_int(tok[2]), h = to_int(tok[3]);
    const wchar_t *state = tok[4];
    wchar_t label[256], path[MAX_PATH];
    const wchar_t *cls = L"BUTTON";
    DWORD style = WS_CHILD | WS_VISIBLE;
    int value = -1, i;
    const wchar_t *colon;
    HWND host, ctl;
    NONCLIENTMETRICSW ncm;
    HFONT font;
    void *bits;
    HBITMAP bmp;
    HDC dc;
    RECT rc;

    label[0] = 0;
    for (i = 5; i < n; i++) {
        if (i > 5) lstrcatW(label, L" ");
        lstrcatW(label, tok[i]);
    }
    colon = kind;
    while (*colon && *colon != L':') colon++;
    if (*colon) value = to_int(colon + 1);

    if (!registered) {
        WNDCLASSW wc;
        INITCOMMONCONTROLSEX icc;
        ZeroMemory(&wc, sizeof wc);
        wc.lpfnWndProc = host_proc;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"UxdumpHost";
        RegisterClassW(&wc);
        icc.dwSize = sizeof icc;
        icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS;
        InitCommonControlsEx(&icc);
        registered = 1;
    }
    if (!wcsncmp_(kind, L"push")) style |= BS_PUSHBUTTON;
    else if (!wcsncmp_(kind, L"default")) style |= BS_DEFPUSHBUTTON;
    else if (!wcsncmp_(kind, L"check")) style |= BS_AUTO3STATE;
    else if (!wcsncmp_(kind, L"radio")) style |= BS_AUTORADIOBUTTON;
    else if (!wcsncmp_(kind, L"group")) style |= BS_GROUPBOX;
    else if (!wcsncmp_(kind, L"edit")) { cls = L"EDIT"; style |= ES_AUTOHSCROLL; }
    else if (!wcsncmp_(kind, L"combo-edit")) { cls = L"COMBOBOX"; style |= CBS_DROPDOWN; }
    else if (!wcsncmp_(kind, L"combo")) { cls = L"COMBOBOX"; style |= CBS_DROPDOWNLIST; }
    else if (!wcsncmp_(kind, L"progress")) { cls = PROGRESS_CLASSW; }
    else if (!wcsncmp_(kind, L"trackbar")) { cls = TRACKBAR_CLASSW; style |= TBS_AUTOTICKS | TBS_BOTTOM; }
    else if (!wcsncmp_(kind, L"tabs") || !wcsncmp_(kind, L"propsheet")) { cls = WC_TABCONTROLW; }
    else if (!wcsncmp_(kind, L"listview")) { cls = WC_LISTVIEWW; style |= LVS_REPORT | (value == 3 ? LVS_SHOWSELALWAYS : 0); }
    else if (!wcsncmp_(kind, L"treeview")) {
        cls = WC_TREEVIEWW;
        style |= TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT;
    }
    else if (!wcsncmp_(kind, L"listbox")) { cls = L"LISTBOX"; style |= LBS_NOINTEGRALHEIGHT; }
    else if (!wcsncmp_(kind, L"hscroll")) { cls = L"SCROLLBAR"; style |= SBS_HORZ; }
    else if (!wcsncmp_(kind, L"vscroll")) { cls = L"SCROLLBAR"; style |= SBS_VERT; }
    else if (!wcsncmp_(kind, L"spin")) { cls = L"EDIT"; style |= ES_AUTOHSCROLL; }
    else if (!wcsncmp_(kind, L"link")) { cls = L"SysLink"; }   /* label: text with <a>...</a> */
    else if (!wcsncmp_(kind, L"status")) { cls = STATUSCLASSNAMEW; style |= SBARS_SIZEGRIP; }
    else if (!wcsncmp_(kind, L"toolbar")) { cls = TOOLBARCLASSNAMEW; style |= TBSTYLE_FLAT | TBSTYLE_LIST | CCS_NODIVIDER | CCS_NORESIZE; }
    else if (!wcsncmp_(kind, L"menu")) { do_menu(kind, state, w, h, outdir); return; }
    else if (!wcsncmp_(kind, L"tooltip")) { do_tooltip(kind, state, w, h, label, outdir); return; }
    else if (!wcsncmp_(kind, L"filedialog")) { do_filedialog(kind, state, w, h, outdir); return; }
    else if (!wcsncmp_(kind, L"mdi")) { do_mdi(kind, state, w, h, label, outdir); return; }
    else { out(L"control %s error=unknown-kind\r\n", kind); return; }

    host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"UxdumpHost", L"", WS_POPUP | WS_VISIBLE, 40, 40, w, h,
                           NULL, NULL, NULL, NULL);
    ctl = CreateWindowExW((!lstrcmpW(cls, L"EDIT") || !lstrcmpW(cls, WC_LISTVIEWW) || !lstrcmpW(cls, WC_TREEVIEWW)
                           || !lstrcmpW(cls, L"LISTBOX")) ? WS_EX_CLIENTEDGE : 0, cls,
                          lstrcmpW(cls, L"COMBOBOX") ? (wcsncmp_(kind, L"spin") ? label : L"42") : L"", style, 0, 0, w,
                          !lstrcmpW(cls, L"COMBOBOX") ? h + 100 : h, host, (HMENU)1, NULL, NULL);
    /* comctl32 caches the system colours process-wide and refreshes them
     * on WM_SYSCOLORCHANGE -- which `syscolor` lines, set before any of its
     * windows existed, never delivered. */
    SendMessageW(ctl, WM_SYSCOLORCHANGE, 0, 0);
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    /* The dialog font -- MS Shell Dlg 2, Tahoma 8 on XP -- whatever the
     * style's MessageFont (Zune's is Microsoft Sans Serif). */
    ncm.lfMessageFont.lfHeight = -11;
    ncm.lfMessageFont.lfWeight = FW_NORMAL;
    ncm.lfMessageFont.lfItalic = 0;
    lstrcpyW(ncm.lfMessageFont.lfFaceName, L"Tahoma");
    font = CreateFontIndirectW(&ncm.lfMessageFont);
    SendMessageW(ctl, WM_SETFONT, (WPARAM)font, TRUE);
    if (!lstrcmpW(cls, L"COMBOBOX")) {
        SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)label);
        if (!lstrcmpW(state, L"open")) {   /* open: two more items, for the drop-down list */
            SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)L"Windows Classic style");
            SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)L"Silver");
        }
        SendMessageW(ctl, CB_SETCURSEL, 0, 0);
    }
    if (!wcsncmp_(kind, L"check-on") || !wcsncmp_(kind, L"radio-on")) SendMessageW(ctl, BM_SETCHECK, BST_CHECKED, 0);
    if (!wcsncmp_(kind, L"check-mixed")) SendMessageW(ctl, BM_SETCHECK, BST_INDETERMINATE, 0);
    if (!wcsncmp_(kind, L"progress")) { SendMessageW(ctl, PBM_SETRANGE32, 0, 100); SendMessageW(ctl, PBM_SETPOS, value, 0); }
    if (!wcsncmp_(kind, L"trackbar")) { SendMessageW(ctl, TBM_SETRANGE, TRUE, MAKELONG(0, 100)); SendMessageW(ctl, TBM_SETTICFREQ, 10, 0); SendMessageW(ctl, TBM_SETPOS, TRUE, value); }
    if (!wcsncmp_(kind, L"tabs") || !wcsncmp_(kind, L"propsheet")) {
        TCITEMW item;
        const wchar_t *names[] = {L"General", L"Appearance", L"Advanced"};
        for (i = 0; i < 3; i++) {
            item.mask = TCIF_TEXT;
            item.pszText = (wchar_t *)names[i];
            SendMessageW(ctl, TCM_INSERTITEMW, i, (LPARAM)&item);
        }
        if (value > 0) SendMessageW(ctl, TCM_SETCURSEL, value, 0);   /* tabs:N selects tab N */
    }
    if (!wcsncmp_(kind, L"listview")) {
        LVCOLUMNW col;
        LVITEMW item;
        const wchar_t *heads[] = {L"Name", L"Size", L"Type"};
        const int widths[] = {100, 70, 80};
        wchar_t text[32];
        for (i = 0; i < 3; i++) {
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = (wchar_t *)heads[i];
            col.cx = widths[i];
            SendMessageW(ctl, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
        }
        if (value == 4) {   /* listview:4 -- column 0 sorted ascending (HDF_SORTUP) */
            HWND header = (HWND)SendMessageW(ctl, LVM_GETHEADER, 0, 0);
            HDITEMW hd;
            hd.mask = HDI_FORMAT;
            SendMessageW(header, HDM_GETITEMW, 0, (LPARAM)&hd);
            hd.fmt |= HDF_SORTUP;
            SendMessageW(header, HDM_SETITEMW, 0, (LPARAM)&hd);
        }
        if (value == 1)   /* listview:1 -- LVS_EX_CHECKBOXES, File 0 and 2 ticked */
            SendMessageW(ctl, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_CHECKBOXES, LVS_EX_CHECKBOXES);
        for (i = 0; i < 4; i++) {
            wsprintfW(text, L"File %d", i);
            item.mask = LVIF_TEXT; item.iItem = i; item.iSubItem = 0; item.pszText = text;
            SendMessageW(ctl, LVM_INSERTITEMW, 0, (LPARAM)&item);
        }
        if (value == 2 || value == 3) {   /* listview:2 -- File 1 selected; :3 the same, LVS_SHOWSELALWAYS */
            item.mask = LVIF_STATE; item.iItem = 1; item.iSubItem = 0;
            item.state = LVIS_SELECTED | LVIS_FOCUSED; item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW(ctl, LVM_SETITEMSTATE, 1, (LPARAM)&item);
        }
        if (value == 1)
            for (i = 0; i < 4; i += 2) {
                item.mask = LVIF_STATE; item.iItem = i; item.iSubItem = 0;
                item.state = INDEXTOSTATEIMAGEMASK(2); item.stateMask = LVIS_STATEIMAGEMASK;
                SendMessageW(ctl, LVM_SETITEMSTATE, i, (LPARAM)&item);
            }
    }
    if (!wcsncmp_(kind, L"treeview")) {
        TVINSERTSTRUCTW ins;
        /* treeview:4 -- checkboxes; comctl32 wants TVS_CHECKBOXES set after
         * creation and before the items go in, or the states don't stick. */
        if (value == 4)
            SetWindowLongW(ctl, GWL_STYLE, GetWindowLongW(ctl, GWL_STYLE) | TVS_CHECKBOXES);
        HTREEITEM root;
        ZeroMemory(&ins, sizeof ins);
        ins.hParent = TVI_ROOT; ins.hInsertAfter = TVI_LAST; ins.item.mask = TVIF_TEXT;
        ins.item.pszText = L"Desktop";
        root = (HTREEITEM)SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
        if (value == 2) goto tree_done;   /* treeview:2 -- a lone leaf root */
        ins.hParent = root; ins.item.pszText = L"My Documents";
        {
            HTREEITEM docs = (HTREEITEM)SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
            if (value == 6)   /* treeview:6 -- the same, TVS_SHOWSELALWAYS */
                SetWindowLongW(ctl, GWL_STYLE, GetWindowLongW(ctl, GWL_STYLE) | TVS_SHOWSELALWAYS);
            if (value == 5 || value == 6)   /* treeview:5 -- My Documents selected */
                SendMessageW(ctl, TVM_SELECTITEM, TVGN_CARET, (LPARAM)docs);
            if (value == 4) {   /* tick My Documents */
                TVITEMW tv;
                tv.mask = TVIF_STATE; tv.hItem = docs;
                tv.state = INDEXTOSTATEIMAGEMASK(2); tv.stateMask = TVIS_STATEIMAGEMASK;
                SendMessageW(ctl, TVM_SETITEMW, 0, (LPARAM)&tv);
            }
        }
        ins.item.pszText = L"My Computer";
        {
            HTREEITEM computer = (HTREEITEM)SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
            if (value == 1) {
                /* treeview:1 -- nesting, a collapsed parent, a second root. */
                HTREEITEM second;
                ins.hParent = computer; ins.item.pszText = L"Local Disk (C:)";
                SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
                ins.item.pszText = L"CD Drive (D:)";
                SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
                SendMessageW(ctl, TVM_EXPAND, TVE_EXPAND, (LPARAM)computer);
                ins.hParent = root; ins.item.pszText = L"Network";
                second = (HTREEITEM)SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
                ins.hParent = second; ins.item.pszText = L"Workgroup";
                SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
                ins.hParent = TVI_ROOT; ins.item.pszText = L"Recycle Bin";
                second = (HTREEITEM)SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
                ins.hParent = second; ins.item.pszText = L"Old";
                SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
                ins.hParent = TVI_ROOT; ins.item.pszText = L"Leaf";
                SendMessageW(ctl, TVM_INSERTITEMW, 0, (LPARAM)&ins);
            }
        }
        if (value != 3)   /* treeview:3 -- the same, collapsed */
            SendMessageW(ctl, TVM_EXPAND, TVE_EXPAND, (LPARAM)root);
    tree_done:;
    }
    if (!wcsncmp_(kind, L"propsheet")) {
        /* A property sheet's page: a child dialog over the tab control's
         * display rect with the tab body texture (ETDT_ENABLETAB), and a
         * check box on it drawn over that texture. */
        static WORD tpl[16];
        DLGTEMPLATE *t = (DLGTEMPLATE *)tpl;
        RECT disp;
        HWND page, check;
        ZeroMemory(tpl, sizeof tpl);
        t->style = WS_CHILD | WS_VISIBLE | DS_CONTROL;
        page = CreateDialogIndirectParamW(NULL, t, host, page_proc, 0);
        EnableThemeDialogTexture(page, ETDT_ENABLETAB);
        SetRect(&disp, 0, 0, w, h);
        SendMessageW(ctl, TCM_ADJUSTRECT, FALSE, (LPARAM)&disp);
        SetWindowPos(page, HWND_TOP, disp.left, disp.top, disp.right - disp.left, disp.bottom - disp.top, 0);
        check = CreateWindowExW(0, L"BUTTON", label[0] ? label : L"Show window contents while dragging",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 8, 8, 200, 17, page, (HMENU)3, NULL, NULL);
        SendMessageW(check, WM_SYSCOLORCHANGE, 0, 0);
        SendMessageW(check, WM_SETFONT, (WPARAM)font, TRUE);
        out(L"geometry %s page=%d,%d,%d,%d\r\n", kind, disp.left, disp.top, disp.right, disp.bottom);
    }
    if (!wcsncmp_(kind, L"listbox")) {
        SendMessageW(ctl, LB_ADDSTRING, 0, (LPARAM)L"Ascent");
        SendMessageW(ctl, LB_ADDSTRING, 0, (LPARAM)L"Autumn");
        SendMessageW(ctl, LB_ADDSTRING, 0, (LPARAM)L"Azul");
        if (value == 1) SendMessageW(ctl, LB_SETCURSEL, 1, 0);   /* listbox:1 -- Autumn selected */
    }
    if (!wcsncmp_(kind, L"hscroll") || !wcsncmp_(kind, L"vscroll")) {
        SCROLLINFO si;
        si.cbSize = sizeof si; si.fMask = SIF_ALL; si.nMin = 0; si.nMax = 100; si.nPage = 10; si.nPos = 30; si.nTrackPos = 0;
        SetScrollInfo(ctl, SB_CTL, &si, TRUE);
    }
    if (!wcsncmp_(kind, L"spin")) {
        /* The up-down docks to its buddy's right, overlapping its edge. */
        HWND ud = CreateWindowExW(0, UPDOWN_CLASSW, NULL, WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_SETBUDDYINT
                                  | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0, host, (HMENU)2, NULL, NULL);
        RECT e, u;
        SendMessageW(ud, UDM_SETBUDDY, (WPARAM)ctl, 0);
        SendMessageW(ud, UDM_SETRANGE32, 0, 100);
        SendMessageW(ud, UDM_SETPOS32, 0, 42);
        GetWindowRect(ctl, &e);
        GetWindowRect(ud, &u);
        out(L"geometry %s edit=%d,%d,%d,%d updown=%d,%d,%d,%d\r\n", kind, e.left - 40, e.top - 40, e.right - 40,
            e.bottom - 40, u.left - 40, u.top - 40, u.right - 40, u.bottom - 40);
        if (!lstrcmpW(state, L"disabled")) EnableWindow(ud, FALSE);
    }
    if (!wcsncmp_(kind, L"status")) {
        RECT r;
        SendMessageW(ctl, WM_SIZE, 0, 0);
        GetWindowRect(ctl, &r);
        out(L"geometry %s bar=%d,%d,%d,%d\r\n", kind, r.left - 40, r.top - 40, r.right - 40, r.bottom - 40);
    }
    if (!wcsncmp_(kind, L"toolbar")) {
        TBBUTTON b[4];
        RECT r[4];
        const wchar_t *names[] = {L"Back", L"Forward", NULL, L"Folders"};
        /* toolbar:1 -- drop-down buttons, arrows drawn as Explorer has them
         * (TBSTYLE_EX_DRAWDDARROWS): Views and Back split, Tools whole. */
        const wchar_t *dropNames[] = {L"Views", L"Tools", NULL, L"Back"};
        const BYTE dropStyles[] = {BTNS_DROPDOWN, BTNS_WHOLEDROPDOWN, 0, BTNS_DROPDOWN};
        ZeroMemory(b, sizeof b);
        SendMessageW(ctl, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageW(ctl, TB_SETIMAGELIST, 0, 0);
        if (value == 1) {
            SendMessageW(ctl, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
            for (i = 0; i < 4; i++)
                names[i] = dropNames[i];
        }
        for (i = 0; i < 4; i++) {
            b[i].iBitmap = names[i] ? I_IMAGENONE : 6;
            b[i].idCommand = 100 + i;
            b[i].fsState = TBSTATE_ENABLED | (i == 3 && value != 1 ? TBSTATE_CHECKED : 0);
            b[i].fsStyle = names[i] ? (BTNS_AUTOSIZE | BTNS_SHOWTEXT | (i == 3 && value != 1 ? BTNS_CHECK : 0)
                                       | (value == 1 ? dropStyles[i] : 0))
                                    : BTNS_SEP;
            b[i].iString = (INT_PTR)names[i];
        }
        SendMessageW(ctl, TB_ADDBUTTONSW, 4, (LPARAM)b);
        SendMessageW(ctl, TB_AUTOSIZE, 0, 0);
        for (i = 0; i < 4; i++)
            SendMessageW(ctl, TB_GETITEMRECT, i, (LPARAM)&r[i]);
        out(L"geometry %s b0=%d,%d,%d,%d b1=%d,%d,%d,%d sep=%d,%d,%d,%d b3=%d,%d,%d,%d\r\n", kind,
            r[0].left, r[0].top, r[0].right, r[0].bottom, r[1].left, r[1].top, r[1].right, r[1].bottom,
            r[2].left, r[2].top, r[2].right, r[2].bottom, r[3].left, r[3].top, r[3].right, r[3].bottom);
    }
    /* Focus cues: XP shows or hides a new window's by the kind of the last
     * input (keyboard or mouse), so pin them -- shown for `focused` (focus
     * moved by the keyboard), hidden otherwise (a mouse user's view). */
    SendMessageW(host, WM_CHANGEUISTATE, MAKEWPARAM(!lstrcmpW(state, L"focused") ? UIS_CLEAR : UIS_SET, UISF_HIDEFOCUS), 0);
    if (!lstrcmpW(state, L"disabled")) EnableWindow(ctl, FALSE);
    if (!lstrcmpW(state, L"focused")) SetFocus(ctl);
    if (!lstrcmpW(state, L"pressed")) SendMessageW(ctl, BM_SETSTATE, TRUE, 0);
    if (!lstrcmpW(state, L"open")) {   /* the drop-down list, down (the pointer stays away) */
        SetForegroundWindow(host);
        SendMessageW(ctl, CB_SHOWDROPDOWN, TRUE, 0);
    }
    /* Keep the pointer well away: a hot control isn't a normal one. */
    SetCursorPos(700, 560);
    UpdateWindow(host);
    if (!wcsncmp_(state, L"hot")) {
        /* hot: the pointer over the control's middle; hot-X-Y over X,Y. */
        int hx = w / 2, hy = h / 2;
        if (state[3] == L'-') {
            const wchar_t *p = state + 4;
            hx = to_int(p);
            while (*p && *p != L'-') p++;
            if (*p) hy = to_int(p + 1);
        }
        settle(100);
        SetCursorPos(40 + hx, 40 + hy);
        settle(50);
        SetCursorPos(40 + hx + 1, 40 + hy);
        settle(50);
        SetCursorPos(40 + hx, 40 + hy);
    }
    if (!wcsncmp_(state, L"pressed-")) {
        /* pressed-X-Y: the pointer to X,Y and the left button down there,
         * held while the screen is grabbed. */
        const wchar_t *p = state + 8;
        int px = to_int(p), py;
        while (*p && *p != L'-') p++;
        py = *p ? to_int(p + 1) : h / 2;
        SetForegroundWindow(host);
        settle(100);
        SetCursorPos(40 + px, 40 + py);
        settle(100);
        pendingPress.kind = kind; pendingPress.state = state; pendingPress.outdir = outdir;
        pendingPress.w = w; pendingPress.h = h;
        SetTimer(NULL, 0, 120, press_timer);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        settle(400);
        if (pendingPress.kind) {   /* (never grabbed: let go regardless) */
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            out(L"control %s %s error=press-not-grabbed\r\n", kind, state);
        }
        settle(100);
        SetCursorPos(700, 560);
        DestroyWindow(host);
        DeleteObject(font);
        return;
    }
    {
        /* Let it finish painting (and any animation), pumping messages. */
        DWORD until = GetTickCount() + 250;
        MSG msg;
        while (GetTickCount() < until) {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            Sleep(10);
        }
    }

    /* Grab the screen, not WM_PRINT: WM_PRINT's non-client pass is the
     * classic one (an Edit's border came back 3D), while the screen shows
     * what uxtheme actually painted. */
    dc = canvas(w, h, GetSysColor(COLOR_BTNFACE), &bits, &bmp);
    SetRect(&rc, 0, 0, w, h);
    {
        HDC screen = GetDC(NULL);
        BitBlt(dc, 0, 0, w, h, screen, 40, 40, SRCCOPY | CAPTUREBLT);
        ReleaseDC(NULL, screen);
    }
    GdiFlush();
    wsprintfW(path, L"%s\\control_%s_%s_%dx%d.bmp", outdir, kind, state, w, h);
    for (i = 0; path[i]; i++) if (path[i] == L':' && i > 2) path[i] = L'-';
    write_bmp(path, (const DWORD *)bits, w, h);
    out(L"control %s %s %d %d face=%lu\r\n", kind, state, w, h, (unsigned long)GetSysColor(COLOR_BTNFACE));
    /* The control's own idea of its insides: exact targets for Qt's metrics. */
    if (!lstrcmpW(cls, L"COMBOBOX")) {
        COMBOBOXINFO ci;
        ci.cbSize = sizeof ci;
        if (GetComboBoxInfo(ctl, &ci))
            out(L"geometry %s item=%d,%d,%d,%d button=%d,%d,%d,%d\r\n", kind,
                ci.rcItem.left, ci.rcItem.top, ci.rcItem.right, ci.rcItem.bottom,
                ci.rcButton.left, ci.rcButton.top, ci.rcButton.right, ci.rcButton.bottom);
    } else if (!wcsncmp_(kind, L"trackbar")) {
        RECT ch, th;
        SendMessageW(ctl, TBM_GETCHANNELRECT, 0, (LPARAM)&ch);
        SendMessageW(ctl, TBM_GETTHUMBRECT, 0, (LPARAM)&th);
        out(L"geometry %s channel=%d,%d,%d,%d thumb=%d,%d,%d,%d tic0=%d tic5=%d\r\n", kind,
            ch.left, ch.top, ch.right, ch.bottom, th.left, th.top, th.right, th.bottom,
            (int)SendMessageW(ctl, TBM_GETTICPOS, 0, 0), (int)SendMessageW(ctl, TBM_GETTICPOS, 4, 0));
    } else if (!wcsncmp_(kind, L"tabs") || !wcsncmp_(kind, L"propsheet")) {
        RECT r[3], disp;
        for (i = 0; i < 3; i++)
            SendMessageW(ctl, TCM_GETITEMRECT, i, (LPARAM)&r[i]);
        SetRect(&disp, 0, 0, w, h);
        SendMessageW(ctl, TCM_ADJUSTRECT, FALSE, (LPARAM)&disp);
        out(L"geometry %s tab0=%d,%d,%d,%d tab1=%d,%d,%d,%d tab2=%d,%d,%d,%d display=%d,%d,%d,%d\r\n", kind,
            r[0].left, r[0].top, r[0].right, r[0].bottom, r[1].left, r[1].top, r[1].right, r[1].bottom,
            r[2].left, r[2].top, r[2].right, r[2].bottom, disp.left, disp.top, disp.right, disp.bottom);
    } else if (!lstrcmpW(cls, L"SCROLLBAR")) {
        SCROLLBARINFO sb;
        sb.cbSize = sizeof sb;
        if (GetScrollBarInfo(ctl, OBJID_CLIENT, &sb))
            out(L"geometry %s line=%d thumb=%d,%d\r\n", kind, sb.dxyLineButton, sb.xyThumbTop, sb.xyThumbBottom);
    } else if (!lstrcmpW(cls, WC_LISTVIEWW)) {
        RECT item, hdr;
        HWND header = (HWND)SendMessageW(ctl, LVM_GETHEADER, 0, 0);
        item.left = LVIR_BOUNDS;
        SendMessageW(ctl, LVM_GETITEMRECT, 0, (LPARAM)&item);
        GetClientRect(header, &hdr);
        out(L"geometry %s item0=%d,%d,%d,%d header=%dx%d\r\n", kind, item.left, item.top, item.right, item.bottom,
            hdr.right, hdr.bottom);
    } else if (!lstrcmpW(cls, WC_TREEVIEWW)) {
        out(L"geometry %s itemheight=%d indent=%d\r\n", kind, (int)SendMessageW(ctl, TVM_GETITEMHEIGHT, 0, 0),
            (int)SendMessageW(ctl, TVM_GETINDENT, 0, 0));
    } else if (!lstrcmpW(cls, L"LISTBOX")) {
        out(L"geometry %s itemheight=%d\r\n", kind, (int)SendMessageW(ctl, LB_GETITEMHEIGHT, 0, 0));
    } else if (!lstrcmpW(cls, L"EDIT")) {
        RECT fr;
        SendMessageW(ctl, EM_GETRECT, 0, (LPARAM)&fr);
        out(L"geometry %s format=%d,%d,%d,%d\r\n", kind, fr.left, fr.top, fr.right, fr.bottom);
    }
    DeleteDC(dc);
    DeleteObject(bmp);
    DestroyWindow(host);
    DeleteObject(font);
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

static int wcsncmp_(const wchar_t *s, const wchar_t *prefix)
{
    while (*prefix)
        if (*s++ != *prefix++) return 1;
    return 0;
}

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
    pSetSystemVisualStyle = (SetSystemVisualStyle_t)GetProcAddress(ux, MAKEINTRESOURCEA(65));
    pEnableTheming = (EnableTheming_t)GetProcAddress(ux, "EnableTheming");
    /* Any window will do: uxtheme only wants one to hang theme data off. */
    hwnd = CreateWindowExW(0, L"STATIC", L"uxdump", WS_POPUP, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    v.dwOSVersionInfoSize = sizeof v;
    GetVersionExW(&v);
    out(L"# windows %lu.%lu.%lu %s; ordinals 2/3/16: %d/%d/%d\r\n",
        v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, v.szCSDVersion,
        pOpenThemeFile != NULL, pCloseThemeFile != NULL, pOpenThemeDataFromFile != NULL);

    /* XP's default font smoothing, "Standard" (no smoothing at Tahoma 8),
     * whatever the session has: this VM's image came with ClearType on.
     * (For the session only: -snapshot throws it away.) */
    SystemParametersInfoW(SPI_SETFONTSMOOTHING, TRUE, NULL, SPIF_SENDCHANGE);
    SystemParametersInfoW(SPI_SETFONTSMOOTHINGTYPE, 0, (PVOID)FE_FONTSMOOTHINGSTANDARD, SPIF_SENDCHANGE);
    out(L"# font smoothing: standard\r\n");

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
        } else if (lstrcmpW(tok[0], L"control") == 0 && n >= 5) {
            do_control(tok, n, argv[2]);
        } else if (lstrcmpW(tok[0], L"bitmap") == 0 && n >= 2) {
            int i;
            for (i = 2; i < n; i++)
                tok[i][-1] = L' ';
            do_bitmap(tok[1], argv[2]);
        } else if (lstrcmpW(tok[0], L"apply") == 0 && n >= 4) {
            int i;
            HRESULT hr = E_FAIL;
            for (i = 2; i < n - 2; i++)
                tok[i][-1] = L' ';
            if (pEnableTheming)
                pEnableTheming(TRUE);
            SystemParametersInfoW(SPI_SETFLATMENU, 0, (PVOID)TRUE, SPIF_SENDCHANGE);
            if (pSetSystemVisualStyle)
                hr = pSetSystemVisualStyle(tok[1], tok[n - 2], tok[n - 1], 0);
            settle(1500);
            if (SUCCEEDED(hr)) {
                /* Display Properties also loads the style's system colours
                 * (its [SysMetrics]); SetSystemVisualStyle alone doesn't. */
                HTHEME t = OpenThemeData(hwnd, L"WINDOW");
                if (t) {
                    int ids[COLOR_MENUBAR + 1];
                    COLORREF cols[COLOR_MENUBAR + 1];
                    int k;
                    for (k = 0; k <= COLOR_MENUBAR; k++) {
                        ids[k] = k;
                        cols[k] = GetThemeSysColor(t, k);
                    }
                    SetSysColors(COLOR_MENUBAR + 1, ids, cols);
                    CloseThemeData(t);
                    settle(1000);
                    refresh_comctl32();
                }
            }
            out(L"# apply %s %s %s hr=0x%08lx face=%lu window=%lu highlight=%lu menu=%lu caption=%d scroll=%d\r\n", tok[1],
                tok[n - 2], tok[n - 1], (unsigned long)hr, (unsigned long)GetSysColor(COLOR_BTNFACE),
                (unsigned long)GetSysColor(COLOR_WINDOW), (unsigned long)GetSysColor(COLOR_HIGHLIGHT),
                (unsigned long)GetSysColor(COLOR_MENU), GetSystemMetrics(SM_CYCAPTION), GetSystemMetrics(SM_CXVSCROLL));
        } else if (lstrcmpW(tok[0], L"classic") == 0) {
            HRESULT hr = pEnableTheming ? pEnableTheming(FALSE) : E_FAIL;
            SystemParametersInfoW(SPI_SETFLATMENU, 0, (PVOID)FALSE, SPIF_SENDCHANGE);
            settle(1500);
            out(L"# classic hr=0x%08lx themed=%d\r\n", (unsigned long)hr, IsThemeActive());
        } else if (lstrcmpW(tok[0], L"metrics") == 0) {
            /* The session's NONCLIENTMETRICSA, as `ncmetrics` takes it. */
            NONCLIENTMETRICSA ncm;
            const BYTE *b = (const BYTE *)&ncm;
            wchar_t hexs[2 * sizeof ncm + 1];
            unsigned k;
            ncm.cbSize = sizeof ncm;
            SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
            for (k = 0; k < sizeof ncm; k++) {
                hexs[2 * k] = L"0123456789abcdef"[b[k] >> 4];
                hexs[2 * k + 1] = L"0123456789abcdef"[b[k] & 15];
            }
            hexs[2 * sizeof ncm] = 0;
            out(L"# metrics %s smicon=%d,%d\r\n", hexs, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
        } else if (lstrcmpW(tok[0], L"ncmetrics") == 0 && n == 2) {
            /* A .theme's [Metrics] NonclientMetrics (NONCLIENTMETRICSA, in
             * hex): the frame, caption, scroll and menu sizes and fonts
             * Display Properties applies with a scheme. */
            const BOOL ok = set_metrics_hex(tok[1]);
            settle(1000);
            refresh_comctl32();
            out(L"# ncmetrics ok=%d caption=%d size=%d,%d frame=%d scroll=%d smicon=%d,%d\r\n", ok,
                GetSystemMetrics(SM_CYCAPTION), GetSystemMetrics(SM_CXSIZE), GetSystemMetrics(SM_CYSIZE),
                GetSystemMetrics(SM_CXSIZEFRAME), GetSystemMetrics(SM_CXVSCROLL), GetSystemMetrics(SM_CXSMICON),
                GetSystemMetrics(SM_CYSMICON));
        } else if (lstrcmpW(tok[0], L"syscolor") == 0 && n == 5) {
            int id = to_int(tok[1]);
            COLORREF c = RGB(to_int(tok[2]), to_int(tok[3]), to_int(tok[4]));
            SetSysColors(1, &id, &c);
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
