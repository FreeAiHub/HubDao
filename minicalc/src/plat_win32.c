/*
 * MiniCalc — платформний шар для Windows (чистий Win32 + GDI).
 * Використовує лише системні DLL (user32, gdi32, kernel32, msvcrt).
 */
#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core.h"

static HDC   g_dc;                 /* поточний DC (буфер) під час малювання */
static HFONT g_font[FONT_COUNT];
static float g_scale = 1.0f;

static int utf8_to_w(const char *s, WCHAR *out, int max)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, max);
    if (n <= 0) { out[0] = 0; return 0; }
    return n - 1;
}

void plat_fill(int x, int y, int w, int h, unsigned rgb)
{
    RECT r;
    HBRUSH b;
    if (w <= 0 || h <= 0) return;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    b = CreateSolidBrush(RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
    FillRect(g_dc, &r, b);
    DeleteObject(b);
}

void plat_text(int x, int y, int w, int h, const char *s, unsigned rgb, int font, int align)
{
    WCHAR ws[512];
    RECT r;
    UINT fmt = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
    int n = utf8_to_w(s, ws, 512);
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    fmt |= (align == ALIGN_CENTER) ? DT_CENTER : (align == ALIGN_RIGHT) ? DT_RIGHT : DT_LEFT;
    SelectObject(g_dc, g_font[font]);
    SetTextColor(g_dc, RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
    SetBkMode(g_dc, TRANSPARENT);
    DrawTextW(g_dc, ws, n, &r, fmt);
}

int plat_text_width(const char *s, int font)
{
    WCHAR ws[512];
    SIZE sz;
    int n = utf8_to_w(s, ws, 512);
    HDC dc = g_dc;
    int own = 0;
    if (!dc) { dc = GetDC(NULL); own = 1; }
    SelectObject(dc, g_font[font]);
    if (!GetTextExtentPoint32W(dc, ws, n, &sz)) sz.cx = 0;
    if (own) ReleaseDC(NULL, dc);
    return sz.cx;
}

static void make_fonts(void)
{
    int i;
    for (i = 0; i < FONT_COUNT; i++) {
        if (g_font[i]) DeleteObject(g_font[i]);
        g_font[i] = CreateFontW(-(int)(core_font_px[i] * g_scale + 0.5f), 0, 0, 0,
                                i == FONT_LARGE ? FW_SEMIBOLD : FW_NORMAL,
                                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                                L"Segoe UI");
    }
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc = BeginPaint(hwnd, &ps), mem;
    HBITMAP bmp, old;
    GetClientRect(hwnd, &rc);
    if (rc.right > 0 && rc.bottom > 0) {
        /* подвійна буферизація — без мерехтіння */
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        old = (HBITMAP)SelectObject(mem, bmp);
        g_dc = mem;
        core_render(rc.right, rc.bottom);
        g_dc = NULL;
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int redraw = 0;
    switch (msg) {
    case WM_PAINT:      paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:       redraw = 1; break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        RECT r = { 0, 0, 0, 0 };
        r.right = (LONG)(CORE_MIN_W * g_scale); r.bottom = (LONG)(CORE_MIN_H * g_scale);
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        mm->ptMinTrackSize.x = r.right - r.left;
        mm->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetCapture(hwnd);
        redraw = core_mouse_down((short)LOWORD(lp), (short)HIWORD(lp));
        break;
    case WM_LBUTTONUP:
        ReleaseCapture();
        redraw = core_mouse_up((short)LOWORD(lp), (short)HIWORD(lp));
        break;
    case WM_CHAR:
        if (wp < 128) redraw = core_key((int)wp);
        break;
    case WM_KEYDOWN:
        if (wp == VK_DELETE) redraw = core_key(127);
        break;
    case WM_DPICHANGED: {
        RECT *nr = (RECT *)lp;
        core_set_scale(g_scale = HIWORD(wp) / 96.0f);
        make_fonts();
        SetWindowPos(hwnd, NULL, nr->left, nr->top, nr->right - nr->left, nr->bottom - nr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        redraw = 1;
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    if (redraw) { InvalidateRect(hwnd, NULL, FALSE); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void enable_dpi_awareness(void)
{
    /* динамічно, щоб працювало і на Windows 7 */
    typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
    typedef BOOL (WINAPI *SetAwareFn)(void);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    SetCtxFn ctx = (SetCtxFn)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (ctx && ctx((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */)) return;
    {
        SetAwareFn aw = (SetAwareFn)(void *)GetProcAddress(u, "SetProcessDPIAware");
        if (aw) aw();
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    WNDCLASSW wc;
    HWND hwnd;
    MSG m;
    RECT r;
    HDC sdc;
    (void)prev; (void)cmd;

    enable_dpi_awareness();
    sdc = GetDC(NULL);
    g_scale = GetDeviceCaps(sdc, LOGPIXELSY) / 96.0f;
    ReleaseDC(NULL, sdc);
    core_set_scale(g_scale);
    make_fonts();
    core_init();

    ZeroMemory(&wc, sizeof wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"MiniCalcWnd";
    RegisterClassW(&wc);

    r.left = 0; r.top = 0;
    r.right = (LONG)(CORE_WIN_W * g_scale); r.bottom = (LONG)(CORE_WIN_H * g_scale);
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowW(L"MiniCalcWnd", L"MiniCalc", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                         NULL, NULL, inst, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
