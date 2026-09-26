// SPDX-License-Identifier: GPL-3.0-or-later
/* xput -- send every file in a directory to the host over HTTP.
 *
 *   xput.exe <dir> [host] [port]       (defaults: 10.0.2.2 8765)
 *   xput.exe -started [host] [port]    just `PUT /.started`: the job is running
 *
 * QEMU's user networking puts the host at 10.0.2.2, so this is how results
 * leave the XP VM: one `PUT /<name>` per file, HTTP/1.0, to the receiver
 * refharness/oracle.py runs. Then `PUT /.done` with the file count, so the
 * host knows the batch is complete. Like uxdump, it needs no C runtime.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>

void *memset(void *d, int c, unsigned n)
{
    volatile unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

static unsigned long host_addr;
static unsigned short host_port;

static int send_all(SOCKET s, const char *buf, int n)
{
    while (n > 0) {
        int k = send(s, buf, n, 0);
        if (k <= 0) return 0;
        buf += k; n -= k;
    }
    return 1;
}

/* PUT one body to /<name>; returns 1 on an HTTP 2xx. */
static int put(const char *name, const char *body, DWORD size)
{
    struct sockaddr_in sa;
    char head[512], reply[64];
    int n, ok = 0;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return 0;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = host_addr;
    sa.sin_port = htons(host_port);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) == 0) {
        n = wsprintfA(head, "PUT /%s HTTP/1.0\r\nContent-Length: %lu\r\n\r\n", name, (unsigned long)size);
        if (send_all(s, head, n) && send_all(s, body, (int)size)) {
            n = recv(s, reply, sizeof reply - 1, 0);
            if (n > 12) {
                reply[n] = 0;
                ok = reply[9] == '2';        /* "HTTP/1.x 2.." */
            }
        }
    }
    closesocket(s);
    return ok;
}

static int put_file(const wchar_t *dir, const wchar_t *file)
{
    wchar_t path[MAX_PATH];
    char name[MAX_PATH];
    HANDLE f;
    DWORD size, got;
    char *buf;
    int ok;
    wsprintfW(path, L"%s\\%s", dir, file);
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(f, NULL);
    buf = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    ReadFile(f, buf, size, &got, NULL);
    CloseHandle(f);
    WideCharToMultiByte(CP_ACP, 0, file, -1, name, sizeof name, NULL, NULL);
    ok = got == size && put(name, buf, size);
    HeapFree(GetProcessHeap(), 0, buf);
    return ok;
}

static int run(int argc, wchar_t **argv)
{
    WSADATA wsa;
    WIN32_FIND_DATAW fd;
    wchar_t pattern[MAX_PATH];
    char host[64] = "10.0.2.2", count[32];
    HANDLE h;
    int sent = 0, failed = 0;

    if (argc < 2) return 2;
    if (argc > 2) WideCharToMultiByte(CP_ACP, 0, argv[2], -1, host, sizeof host, NULL, NULL);
    host_port = 8765;
    if (argc > 3) {
        const wchar_t *p = argv[3];
        host_port = 0;
        while (*p >= L'0' && *p <= L'9') host_port = host_port * 10 + (*p++ - L'0');
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 3;
    host_addr = inet_addr(host);

    if (!lstrcmpW(argv[1], L"-started")) {
        int ok = put(".started", "1", 1);
        WSACleanup();
        return ok ? 0 : 1;
    }
    wsprintfW(pattern, L"%s\\*", argv[1]);
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (put_file(argv[1], fd.cFileName)) sent++; else failed++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    wsprintfA(count, "%d %d", sent, failed);
    put(".done", count, lstrlenA(count));
    WSACleanup();
    return failed ? 1 : 0;
}

void __stdcall entry(void)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    ExitProcess(run(argc, argv));
}
