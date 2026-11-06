/*
 * MiniCalc — платформний шар для Linux / BSD (чистий Xlib).
 * Залежність одна: libX11, яка є в кожному десктопному Linux
 * (на Wayland працює через XWayland).
 */
#include "core.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __GLIBC__
#  include <malloc.h>
#endif

static Display     *dpy;
static Window       win;
static Pixmap       buf;
static GC           gc;
static int          W = CORE_WIN_W, H = CORE_WIN_H, BW, BH;
static Visual      *vis;
static Colormap     cmap;
static int          depth;

/* ---------------- кольори ---------------- */
static int mask_shift(unsigned long m) { int s = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; s++; } return s; }
static int mask_bits(unsigned long m)  { int b = 0; while (m) { b += (int)(m & 1); m >>= 1; } return b; }

static unsigned long pixel_of(unsigned rgb)
{
    unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    if (vis->class == TrueColor || vis->class == DirectColor) {
        unsigned long px = 0;
        px |= ((unsigned long)(r >> (8 - mask_bits(vis->red_mask)))   << mask_shift(vis->red_mask));
        px |= ((unsigned long)(g >> (8 - mask_bits(vis->green_mask))) << mask_shift(vis->green_mask));
        px |= ((unsigned long)(b >> (8 - mask_bits(vis->blue_mask)))  << mask_shift(vis->blue_mask));
        return px;
    } else {
        /* рідкісні 8-бітні дисплеї: невеликий кеш */
        static unsigned keys[64]; static unsigned long vals[64]; static int n;
        int i; XColor c;
        for (i = 0; i < n; i++) if (keys[i] == rgb) return vals[i];
        c.red = (unsigned short)(r * 257); c.green = (unsigned short)(g * 257); c.blue = (unsigned short)(b * 257);
        c.flags = DoRed | DoGreen | DoBlue;
        if (!XAllocColor(dpy, cmap, &c)) return (r + g + b > 380) ? WhitePixel(dpy, DefaultScreen(dpy)) : BlackPixel(dpy, DefaultScreen(dpy));
        if (n < 64) { keys[n] = rgb; vals[n] = c.pixel; n++; }
        return c.pixel;
    }
}

/* ---------------- шрифти ----------------
 * XLoadQueryFont для Unicode-шрифту тримає метрики на 65536 символів
 * (~800 КБ на шрифт). Щоб вкластися в 5 МБ RAM, ми копіюємо ширини лише
 * потрібних символів (ASCII, кирилиця, кілька знаків) і звільняємо решту.
 */
#define NSLOT 229
typedef struct {
    Font  fid;
    int   ascent, descent;
    int   wide;                /* 2-байтовий (Unicode) шрифт */
    short width[NSLOT];        /* 0 = гліфа немає */
} MiniFont;
static MiniFont fonts[FONT_COUNT];

static int slot_of(unsigned cp)
{
    if (cp < 128) return (int)cp;
    if (cp >= 0x400 && cp < 0x460) return 128 + (int)(cp - 0x400);
    switch (cp) {
    case 0xD7:   return 224;   /* × */
    case 0xF7:   return 225;   /* ÷ */
    case 0x2212: return 226;   /* − */
    case 0x2190: return 227;   /* ← */
    case 0x2026: return 228;   /* … */
    }
    return -1;
}
static unsigned cp_of_slot(int i)
{
    static const unsigned extra[] = { 0xD7, 0xF7, 0x2212, 0x2190, 0x2026 };
    if (i < 128) return (unsigned)i;
    if (i < 224) return 0x400u + (unsigned)(i - 128);
    return extra[i - 224];
}

static int glyph_width(const XFontStruct *f, unsigned cp)
{
    unsigned b1 = cp >> 8, b2 = cp & 255;
    const XCharStruct *c;
    if (b1 < f->min_byte1 || b1 > f->max_byte1 ||
        b2 < f->min_char_or_byte2 || b2 > f->max_char_or_byte2) return 0;
    if (!f->per_char) return f->max_bounds.width;
    c = &f->per_char[(b1 - f->min_byte1) * (f->max_char_or_byte2 - f->min_char_or_byte2 + 1)
                     + (b2 - f->min_char_or_byte2)];
    if (!c->width && !c->ascent && !c->descent && !c->lbearing && !c->rbearing) return 0;
    return c->width;
}

static void adopt(MiniFont *m, XFontStruct *f)
{
    int i;
    m->fid = f->fid; m->ascent = f->ascent; m->descent = f->descent;
    m->wide = f->max_byte1 > 0;
    for (i = 0; i < NSLOT; i++) m->width[i] = (short)glyph_width(f, cp_of_slot(i));
    XFreeFontInfo(NULL, f, 1);         /* метрики більше не потрібні, шрифт лишається на сервері */
}

static int has_cyrillic(const XFontStruct *f)
{
    return glyph_width(f, 0x406) > 0 && glyph_width(f, 0x44F) > 0 && glyph_width(f, 0x457) > 0;
}

/* повертає 1, якщо шрифт має кирилицю */
static int load_font(MiniFont *out, int px, int bold, int need_cyr)
{
    static const char *const fam[] = {
        "-*-dejavu sans-%s-r-normal--%d-*-*-*-p-*-iso10646-1",
        "-*-liberation sans-%s-r-normal--%d-*-*-*-p-*-iso10646-1",
        "-*-helvetica-%s-r-normal--%d-*-*-*-p-*-iso10646-1",
        "-*-*-%s-r-normal-sans-%d-*-*-*-p-*-iso10646-1",
        "-*-*-%s-r-normal--%d-*-*-*-p-*-iso10646-1",
        "-misc-fixed-%s-r-*--%d-*-*-*-*-*-iso10646-1",
        "-*-*-%s-r-normal--%d-*-*-*-*-*-iso10646-1",
        "-*-helvetica-%s-r-normal--%d-*-*-*-p-*-iso8859-1",
        "-*-*-%s-r-normal--%d-*-*-*-p-*-iso8859-1",
        "-*-*-%s-r-normal--%d-*-*-*-*-*-iso8859-1",
    };
    char name[256];
    unsigned i;
    int d, w;
    XFontStruct *f, *fallback = NULL;

    for (d = 0; d <= 8; d++) {              /* точний розмір, потім ±1, ±2 ... */
        int sz = px + ((d & 1) ? -(d + 1) / 2 : d / 2);
        if (sz < 6) continue;
        for (i = 0; i < sizeof fam / sizeof fam[0]; i++) {
            for (w = 0; w < 2; w++) {
                snprintf(name, sizeof name, fam[i], w == 0 ? (bold ? "bold" : "medium") : "*", sz);
                if ((f = XLoadQueryFont(dpy, name)) == NULL) continue;
                if (!need_cyr || has_cyrillic(f)) {
                    if (fallback) XFreeFont(dpy, fallback);
                    adopt(out, f);
                    return 1;
                }
                if (!fallback) fallback = f; else XFreeFont(dpy, f);
            }
        }
    }
    if (!fallback && px >= 18) fallback = XLoadQueryFont(dpy, "10x20");
    if (!fallback) fallback = XLoadQueryFont(dpy, "fixed");
    if (!fallback) return -1;
    adopt(out, fallback);
    return 0;
}

/* UTF-8 → XChar2b; символи без гліфа замінюємо на ASCII */
static int to_char2b(const char *s, XChar2b *out, int max, const MiniFont *f, int *width)
{
    int n = 0, tw = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < max - 2) {
        unsigned cp;
        int sl;
        if (*p < 0x80)                                 { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0 && p[1])          { cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu); p += 2; }
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2])  { cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu); p += 3; }
        else { p++; while ((*p & 0xC0) == 0x80) p++; cp = '?'; }

        sl = slot_of(cp);
        if (sl < 0 || f->width[sl] == 0) {
            if (cp == 0x2212) cp = '-';
            else if (cp == 0x2190) cp = '<';
            else if (cp == 0x2026) {
                out[n].byte1 = 0; out[n].byte2 = '.'; n++; tw += f->width['.'];
                cp = '.';
            } else if (cp == 0xD7) cp = 'x';
            else if (cp == 0xF7) cp = '/';
            else cp = '?';
            sl = (int)cp;
        }
        out[n].byte1 = (unsigned char)(cp >> 8);
        out[n].byte2 = (unsigned char)(cp & 255);
        tw += f->width[sl];
        n++;
    }
    if (width) *width = tw;
    return n;
}

int plat_text_width(const char *s, int font)
{
    XChar2b tmp[512];
    int w;
    to_char2b(s, tmp, 512, &fonts[font], &w);
    return w;
}

void plat_fill(int x, int y, int w, int h, unsigned rgb)
{
    if (w <= 0 || h <= 0) return;
    XSetForeground(dpy, gc, pixel_of(rgb));
    XFillRectangle(dpy, buf, gc, x, y, (unsigned)w, (unsigned)h);
}

void plat_text(int x, int y, int w, int h, const char *s, unsigned rgb, int font, int align)
{
    XChar2b tmp[512];
    const MiniFont *f = &fonts[font];
    int tw, n = to_char2b(s, tmp, 512, f, &tw);
    int tx = x, ty;
    XRectangle clip;
    if (align == ALIGN_CENTER) tx = x + (w - tw) / 2;
    else if (align == ALIGN_RIGHT) tx = x + w - tw;
    ty = y + (h - (f->ascent + f->descent)) / 2 + f->ascent;
    clip.x = (short)x; clip.y = (short)y;
    clip.width = (unsigned short)(w > 0 ? w : 0); clip.height = (unsigned short)(h > 0 ? h : 0);
    XSetClipRectangles(dpy, gc, 0, 0, &clip, 1, Unsorted);
    XSetForeground(dpy, gc, pixel_of(rgb));
    XSetFont(dpy, gc, f->fid);
    XDrawString16(dpy, buf, gc, tx, ty, tmp, n);
    XSetClipMask(dpy, gc, None);
}

/* ---------------- вікно ---------------- */
static void ensure_buffer(void)
{
    if (buf && BW == W && BH == H) return;
    if (buf) XFreePixmap(dpy, buf);
    buf = XCreatePixmap(dpy, win, (unsigned)W, (unsigned)H, (unsigned)depth);
    BW = W; BH = H;
}

static void redraw(void)
{
    ensure_buffer();
    core_render(W, H);
    XCopyArea(dpy, buf, win, gc, 0, 0, (unsigned)W, (unsigned)H, 0, 0);
    XFlush(dpy);
}

int main(void)
{
    int scr;
    XSizeHints *hints;
    XClassHint *cls;
    Atom wm_delete, net_name, utf8;
    XEvent ev;
    const char *title = "MiniCalc";
    int need_redraw = 0;

#ifdef __GLIBC__
    /* великі тимчасові блоки (метрики шрифтів) одразу повертати ОС */
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    mallopt(M_TRIM_THRESHOLD, 64 * 1024);
#endif
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "MiniCalc: не вдалося відкрити X-дисплей\n"); return 1; }
    scr   = DefaultScreen(dpy);
    vis   = DefaultVisual(dpy, scr);
    cmap  = DefaultColormap(dpy, scr);
    depth = DefaultDepth(dpy, scr);

    core_init();
    core_set_scale(1.0f);

    {
        int a = load_font(&fonts[FONT_SMALL],  core_font_px[FONT_SMALL],  0, 1);
        int b = load_font(&fonts[FONT_MEDIUM], core_font_px[FONT_MEDIUM], 0, 1);
        int c = load_font(&fonts[FONT_LARGE],  core_font_px[FONT_LARGE],  1, 0);
        if (a < 0 || b < 0 || c < 0) { fprintf(stderr, "MiniCalc: немає шрифтів X11\n"); return 1; }
        /* немає кириличних шрифтів X11 — показуємо інтерфейс англійською */
        if (a == 0 || b == 0) core_set_english(1);
    }
#ifdef __GLIBC__
    malloc_trim(0);
#endif

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, (unsigned)W, (unsigned)H, 0,
                              BlackPixel(dpy, scr), pixel_of(0x16181D));
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask |
                           ButtonReleaseMask | StructureNotifyMask);

    hints = XAllocSizeHints();
    hints->flags = PMinSize | PSize;
    hints->min_width = CORE_MIN_W; hints->min_height = CORE_MIN_H;
    hints->width = W; hints->height = H;
    XSetWMNormalHints(dpy, win, hints);
    XFree(hints);

    cls = XAllocClassHint();
    cls->res_name = (char *)"minicalc"; cls->res_class = (char *)"MiniCalc";
    XSetClassHint(dpy, win, cls);
    XFree(cls);

    XStoreName(dpy, win, title);
    net_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    utf8     = XInternAtom(dpy, "UTF8_STRING", False);
    XChangeProperty(dpy, win, net_name, utf8, 8, PropModeReplace,
                    (const unsigned char *)title, (int)strlen(title));

    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    gc = XCreateGC(dpy, win, 0, NULL);
    XMapWindow(dpy, win);

    for (;;) {
        int dirty = 0;
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case Expose:
            if (ev.xexpose.count == 0) dirty = 1;
            break;
        case ConfigureNotify:
            if (ev.xconfigure.width != W || ev.xconfigure.height != H) {
                W = ev.xconfigure.width; H = ev.xconfigure.height; dirty = 1;
            }
            break;
        case ButtonPress:
            if (ev.xbutton.button == Button1) dirty = core_mouse_down(ev.xbutton.x, ev.xbutton.y);
            break;
        case ButtonRelease:
            if (ev.xbutton.button == Button1) dirty = core_mouse_up(ev.xbutton.x, ev.xbutton.y);
            break;
        case KeyPress: {
            char txt[8] = {0};
            KeySym ks;
            int n = XLookupString(&ev.xkey, txt, sizeof txt - 1, &ks, NULL);
            if ((ev.xkey.state & ControlMask) && (ks == XK_q || ks == XK_w)) goto quit;
            if (ks == XK_BackSpace)                       dirty = core_key('\b');
            else if (ks == XK_Return || ks == XK_KP_Enter) dirty = core_key('\r');
            else if (ks == XK_Escape || ks == XK_Delete)   dirty = core_key(27);
            else if (n == 1)                               dirty = core_key((unsigned char)txt[0]);
            break;
        }
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == wm_delete) goto quit;
            break;
        default:
            break;
        }
        /* збираємо пачку подій і малюємо один раз */
        if (dirty) need_redraw = 1;
        if (need_redraw && !XPending(dpy)) { redraw(); need_redraw = 0; }
    }
quit:
    if (buf) XFreePixmap(dpy, buf);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
