/*
 * MiniCalc — спільне ядро. Чистий C99, без залежностей, крім libc.
 */
#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <direct.h>
#  include <wchar.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#endif

#define HIST_MAX 15
#define EXPR_MAX 120
#define RES_MAX  40

const int core_font_px[FONT_COUNT] = { 15, 20, 36 };

/* ------------------------------------------------------------------ */
/*  Стан                                                               */
/* ------------------------------------------------------------------ */
typedef struct { char expr[EXPR_MAX]; char res[RES_MAX]; } HistItem;

static char     g_expr[EXPR_MAX];   /* поточний вираз (ASCII: 0-9 . + - * / % ( ) ) */
static char     g_res[RES_MAX];     /* результат або попередній перегляд           */
static int      g_done;             /* 1 = щойно натиснули "="                      */
static int      g_error;            /* код помилки: 0 немає, 1 синтаксис, 2 /0, 3 переповнення */
static int      g_en;               /* 1 = англійський інтерфейс */
static HistItem g_hist[HIST_MAX];   /* [0] — найновіший                             */
static int      g_hist_n;
static int      g_pressed = -1;     /* id натиснутого елемента                      */
static float    g_scale = 1.0f;
static int      g_w = CORE_WIN_W, g_h = CORE_WIN_H;
#ifndef _WIN32
static char     g_hist_path[1024];
#endif

/* ------------------------------------------------------------------ */
/*  Парсер виразів (рекурсивний спуск, не залежить від локалі)         */
/* ------------------------------------------------------------------ */
static const char *P;
static int         PERR; /* 0 ok, 1 синтаксис, 2 ділення на нуль */

static double parse_sum(int depth);

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static double parse_number(void)
{
    double v = 0.0, frac = 0.1;
    int any = 0;
    while (is_digit(*P)) { v = v * 10.0 + (*P - '0'); P++; any = 1; }
    if (*P == '.') {
        P++;
        while (is_digit(*P)) { v += (*P - '0') * frac; frac *= 0.1; P++; any = 1; }
    }
    if (!any) { PERR = 1; return 0.0; }
    if (*P == 'e' || *P == 'E') {              /* експонента з результатів типу 1e+20 */
        int sign = 1, e = 0;
        P++;
        if (*P == '+') P++; else if (*P == '-') { sign = -1; P++; }
        if (!is_digit(*P)) { PERR = 1; return 0.0; }
        while (is_digit(*P)) { if (e < 1000) e = e * 10 + (*P - '0'); P++; }
        {   /* без libm: множимо/ділимо на 10 */
            double m = 1.0;
            while (e-- > 0 && m < 1e308) m *= 10.0;
            v = (sign > 0) ? v * m : v / m;
        }
    }
    return v;
}

static double parse_unary(int depth)
{
    if (depth > 200) { PERR = 1; return 0.0; }
    if (*P == '-') { P++; return -parse_unary(depth + 1); }
    if (*P == '+') { P++; return  parse_unary(depth + 1); }
    if (*P == '(') {
        double v;
        P++;
        v = parse_sum(depth + 1);
        if (*P == ')') P++;                    /* незакриту дужку прощаємо */
        else if (*P != '\0') PERR = 1;
        return v;
    }
    return parse_number();
}

static double parse_postfix(int depth)
{
    double v = parse_unary(depth);
    while (*P == '%') { v /= 100.0; P++; }
    return v;
}

static double parse_prod(int depth)
{
    double v = parse_postfix(depth);
    while (!PERR && (*P == '*' || *P == '/')) {
        char op = *P++;
        double r = parse_postfix(depth);
        if (op == '*') v *= r;
        else { if (r == 0.0) { PERR = 2; return 0.0; } v /= r; }
    }
    return v;
}

static double parse_sum(int depth)
{
    double v = parse_prod(depth);
    while (!PERR && (*P == '+' || *P == '-')) {
        char op = *P++;
        double r = parse_prod(depth);
        v = (op == '+') ? v + r : v - r;
    }
    return v;
}

/* 0 = ok, 1 = синтаксис, 2 = ділення на нуль, 3 = переповнення */
static int evaluate(const char *s, double *out)
{
    double v;
    if (!*s) return 1;
    P = s; PERR = 0;
    v = parse_sum(0);
    if (!PERR && *P != '\0') PERR = 1;
    if (PERR) return PERR;
    if (v != v || v > 1e300 || v < -1e300) return 3;
    *out = v;
    return 0;
}

static void format_number_p(double v, char *buf, size_t n, int prec)
{
    char *c;
    if (v < 1e-15 && v > -1e-15) v = 0.0;               /* прибираємо -0 і шум */
    snprintf(buf, n, "%.*g", prec, v);
    for (c = buf; *c; c++) if (*c == ',') *c = '.'; /* на випадок чужої локалі */
}

static void format_number(double v, char *buf, size_t n) { format_number_p(v, buf, n, 15); }

/* ------------------------------------------------------------------ */
/*  Історія на диску                                                   */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
/* Windows: шлях у UTF-16, щоб працювало з кириличним ім'ям користувача */
static wchar_t g_hist_wpath[1024];

static void init_hist_path(void)
{
    const wchar_t *base = _wgetenv(L"APPDATA");
    wchar_t dir[900];
    if (!base || !*base) base = L".";
    _snwprintf(dir, 899, L"%ls\\MiniCalc", base); dir[899] = 0;
    _wmkdir(dir);
    _snwprintf(g_hist_wpath, 1023, L"%ls\\history.txt", dir); g_hist_wpath[1023] = 0;
}
static FILE *hist_open(const char *mode)
{
    wchar_t wm[4] = { 0 };
    int i;
    for (i = 0; i < 3 && mode[i]; i++) wm[i] = (wchar_t)mode[i];
    return _wfopen(g_hist_wpath, wm);
}
#else
static void make_dir(const char *p) { mkdir(p, 0700); }

static void init_hist_path(void)
{
    char dir[900];
    const char *base;
#if defined(__APPLE__)
    base = getenv("HOME");
    if (!base || !*base) base = ".";
    snprintf(dir, sizeof dir, "%s/Library/Application Support/MiniCalc", base);
    make_dir(dir);
#else
    base = getenv("XDG_DATA_HOME");
    if (base && *base) {
        snprintf(dir, sizeof dir, "%s/minicalc", base);
    } else {
        char tmp[900];
        base = getenv("HOME");
        if (!base || !*base) base = ".";
        snprintf(tmp, sizeof tmp, "%s/.local", base);        make_dir(tmp);
        snprintf(tmp, sizeof tmp, "%s/.local/share", base);  make_dir(tmp);
        snprintf(dir, sizeof dir, "%s/.local/share/minicalc", base);
    }
    make_dir(dir);
#endif
    snprintf(g_hist_path, sizeof g_hist_path, "%s/history.txt", dir);
}
static FILE *hist_open(const char *mode) { return fopen(g_hist_path, mode); }
#endif

static int valid_expr_chars(const char *s)
{
    for (; *s; s++)
        if (!strchr("0123456789.+-*/%()eE", *s)) return 0;
    return 1;
}

static void hist_load(void)
{
    char line[256];
    FILE *f = hist_open("r");
    g_hist_n = 0;
    if (!f) return;
    while (g_hist_n < HIST_MAX && fgets(line, sizeof line, f)) {
        char *tab = strchr(line, '\t'), *nl;
        if (!tab) continue;
        *tab = '\0';
        if ((nl = strpbrk(tab + 1, "\r\n")) != NULL) *nl = '\0';
        if (!*line || !tab[1] || strlen(line) >= EXPR_MAX || strlen(tab + 1) >= RES_MAX) continue;
        if (!valid_expr_chars(line) || !valid_expr_chars(tab + 1)) continue;
        strcpy(g_hist[g_hist_n].expr, line);
        strcpy(g_hist[g_hist_n].res, tab + 1);
        g_hist_n++;
    }
    fclose(f);
}

static void hist_save(void)
{
    int i;
    FILE *f = hist_open("w");
    if (!f) return;
    for (i = 0; i < g_hist_n; i++)
        fprintf(f, "%s\t%s\n", g_hist[i].expr, g_hist[i].res);
    fclose(f);
}

static void hist_push(const char *expr, const char *res)
{
    if (g_hist_n == HIST_MAX) g_hist_n--;      /* найстаріший випадає */
    memmove(&g_hist[1], &g_hist[0], sizeof(HistItem) * (size_t)g_hist_n);
    snprintf(g_hist[0].expr, EXPR_MAX, "%s", expr);
    snprintf(g_hist[0].res, RES_MAX, "%s", res);
    g_hist_n++;
    hist_save();
}

/* ------------------------------------------------------------------ */
/*  Логіка введення                                                    */
/* ------------------------------------------------------------------ */
static int  is_binop(char c) { return c == '+' || c == '-' || c == '*' || c == '/'; }
static char last_char(void)  { size_t n = strlen(g_expr); return n ? g_expr[n - 1] : '\0'; }

static void append(char c)
{
    size_t n = strlen(g_expr);
    if (n + 1 < EXPR_MAX) { g_expr[n] = c; g_expr[n + 1] = '\0'; }
}

static void append_str(const char *s) { while (*s) append(*s++); }

static void update_preview(void)
{
    double v;
    g_error = 0;
    if (g_expr[0] && evaluate(g_expr, &v) == 0) format_number(v, g_res, sizeof g_res);
    else g_res[0] = '\0';
}

static void clear_all(void) { g_expr[0] = '\0'; g_res[0] = '\0'; g_done = 0; g_error = 0; }

/* після "=" цифра починає новий вираз, а оператор продовжує від результату */
static void continue_from_result(void)
{
    if (!g_done) return;
    g_done = 0;
    if (g_error || !g_res[0]) { g_expr[0] = '\0'; return; }
    snprintf(g_expr, EXPR_MAX, "%s", g_res);
}

static void input(char c)
{
    if (c == ',') c = '.';
    if (c == 'x' || c == 'X') c = '*';
    if (c == ':') c = '/';

    if ((is_digit(c) || c == '.') && g_done) { clear_all(); }

    if (is_digit(c)) {
        char l = last_char();
        if (l == ')' || l == '%') append('*');
        append(c);
    } else if (c == '.') {
        /* одна крапка на число */
        int i = (int)strlen(g_expr) - 1;
        while (i >= 0 && (is_digit(g_expr[i]) || g_expr[i] == '.')) {
            if (g_expr[i] == '.') return;
            i--;
        }
        if (!is_digit(last_char())) { char l = last_char(); if (l == ')' || l == '%') append('*'); append('0'); }
        append('.');
    } else if (is_binop(c)) {
        char l;
        continue_from_result();
        l = last_char();
        if (l == '\0' || l == '(') {
            if (c == '-') append('-');         /* унарний мінус */
            return;
        }
        if (is_binop(l)) {
            if (c == '-' && (l == '*' || l == '/')) { append('-'); return; } /* 5*-3 */
            g_expr[strlen(g_expr) - 1] = c;    /* замінюємо оператор */
            /* "5*-" + "+" -> прибираємо обидва */
            if (strlen(g_expr) >= 2 && is_binop(g_expr[strlen(g_expr) - 2])) {
                size_t n = strlen(g_expr);
                g_expr[n - 2] = c; g_expr[n - 1] = '\0';
            }
            /* "(-" + "*" -> "(" : бінарний оператор після "(" не дозволений */
            {
                size_t n = strlen(g_expr);
                if (c != '-' && (n == 1 || g_expr[n - 2] == '(')) g_expr[n - 1] = '\0';
            }
            update_preview();
            return;
        }
        if (l == '.') append('0');
        append(c);
    } else if (c == '%') {
        char l;
        continue_from_result();
        l = last_char();
        if (is_digit(l) || l == ')' || l == '.') append('%');
    } else if (c == '(') {
        char l;
        if (g_done) clear_all();
        l = last_char();
        if (is_digit(l) || l == ')' || l == '%' || l == '.') append('*');
        append('(');
    } else if (c == ')') {
        int open = 0; const char *s; char l;
        continue_from_result();
        for (s = g_expr; *s; s++) open += (*s == '(') - (*s == ')');
        l = last_char();
        if (open > 0 && l != '(' && !is_binop(l)) append(')');
    } else {
        return;
    }
    update_preview();
}

static void backspace(void)
{
    size_t n;
    if (g_done) { g_done = 0; g_error = 0; }
    n = strlen(g_expr);
    if (n) g_expr[n - 1] = '\0';
    update_preview();
}

static void equals(void)
{
    double v;
    int rc, open = 0;
    char full[EXPR_MAX];
    const char *s;

    if (g_done || !g_expr[0]) return;
    /* обрізаємо хвостовий оператор і докриваємо дужки */
    while (g_expr[0] && (is_binop(last_char()) || last_char() == '(' || last_char() == '.'))
        g_expr[strlen(g_expr) - 1] = '\0';
    if (!g_expr[0]) { update_preview(); return; }
    for (s = g_expr; *s; s++) open += (*s == '(') - (*s == ')');
    snprintf(full, sizeof full, "%s", g_expr);
    while (open-- > 0 && strlen(full) + 1 < EXPR_MAX) strcat(full, ")");
    snprintf(g_expr, EXPR_MAX, "%s", full);

    rc = evaluate(g_expr, &v);
    g_done = 1;
    if (rc != 0) {
        g_error = rc;
        g_res[0] = '\0';
        return;
    }
    g_error = 0;
    format_number(v, g_res, sizeof g_res);
    hist_push(g_expr, g_res);
}

/* вставка значення з історії */
static void use_history(int i)
{
    char l;
    if (i < 0 || i >= g_hist_n) return;
    if (g_done) clear_all();
    l = last_char();
    if (l == '\0' || is_binop(l) || l == '(') {
        int neg = g_hist[i].res[0] == '-';
        if (neg && l != '\0' && l != '(') append('(');
        append_str(g_hist[i].res);
        if (neg && l != '\0' && l != '(') append(')');
    } else {
        snprintf(g_expr, EXPR_MAX, "%s", g_hist[i].res);
    }
    update_preview();
}

/* ------------------------------------------------------------------ */
/*  Розкладка                                                          */
/* ------------------------------------------------------------------ */
#define ROWS 6
#define COLS 4
static const char *const BTN_LABEL[] = {
    "C", "\xE2\x86\x90" /* ← */, "(", ")",
    "7", "8", "9", "\xC3\xB7" /* ÷ */,
    "4", "5", "6", "\xC3\x97" /* × */,
    "1", "2", "3", "\xE2\x88\x92" /* − */,
    "0", ".", "%", "+",
    "="
};
static const char BTN_KEY[] = "C\b()789/456*123-0.%+=";
#define BTN_COUNT 21

/* id елементів: 0..20 кнопки, 100..114 рядки історії, 200 — очистити історію */
#define ID_HIST0  100
#define ID_HCLEAR 200

enum { K_DIGIT, K_OP, K_FUNC, K_EQ };
static int btn_kind(int i)
{
    char k = BTN_KEY[i];
    if (k == '=') return K_EQ;
    if (k == '/' || k == '*' || k == '-' || k == '+') return K_OP;
    if (is_digit(k) || k == '.') return K_DIGIT;
    return K_FUNC;
}

#define S(v) ((int)((v) * g_scale + 0.5f))
#define TR(uk, en) (g_en ? (en) : (uk))

typedef struct { int x, y, w, h; } Rect;

static int keypad_w(void)  { int kw = g_w * 54 / 100; return kw < S(260) ? S(260) : kw; }
static Rect display_rect(void) { Rect r; r.x = S(10); r.y = S(10); r.w = keypad_w() - S(15); r.h = S(96); return r; }

static Rect btn_rect(int i)
{
    Rect r, d = display_rect();
    int gap = S(6);
    int top = d.y + d.h + S(10);
    int areah = g_h - top - S(10);
    int bw = (d.w - gap * (COLS - 1)) / COLS;
    int bh = (areah - gap * (ROWS - 1)) / ROWS;
    int row = i / COLS, col = i % COLS;
    r.x = d.x + col * (bw + gap);
    r.y = top + row * (bh + gap);
    r.w = (i == BTN_COUNT - 1) ? d.w : bw;      /* "=" на всю ширину */
    r.h = bh;
    return r;
}

static Rect hist_panel(void)
{
    Rect r;
    r.x = keypad_w() + S(5); r.y = S(10);
    r.w = g_w - r.x - S(10); r.h = g_h - S(20);
    return r;
}
static int  hist_header_h(void) { return S(40); }
static Rect hist_row(int i)
{
    Rect p = hist_panel(), r;
    int top = p.y + hist_header_h();
    int rh = (p.h - hist_header_h() - S(6)) / HIST_MAX;
    r.x = p.x + S(4); r.w = p.w - S(8);
    r.y = top + i * rh; r.h = rh - S(2);
    return r;
}
static Rect hist_clear_btn(void)
{
    Rect p = hist_panel(), r;
    r.w = S(84); r.h = S(28);
    r.x = p.x + p.w - r.w - S(6); r.y = p.y + S(6);
    return r;
}

static int in_rect(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static int hit_test(int x, int y)
{
    int i;
    for (i = 0; i < BTN_COUNT; i++) if (in_rect(btn_rect(i), x, y)) return i;
    if (g_hist_n && in_rect(hist_clear_btn(), x, y)) return ID_HCLEAR;
    for (i = 0; i < g_hist_n; i++) if (in_rect(hist_row(i), x, y)) return ID_HIST0 + i;
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Малювання                                                          */
/* ------------------------------------------------------------------ */
#define C_BG      0x16181Du
#define C_PANEL   0x22252Cu
#define C_DIGIT   0x343843u
#define C_FUNC    0x4A4F5Cu
#define C_OP      0xF0952Bu
#define C_EQ      0x2F80EDu
#define C_TEXT    0xF2F3F5u
#define C_DIM     0x8B909Au
#define C_ERR     0xFF6B6Bu
#define C_ROWHOV  0x2C3039u

static unsigned lighten(unsigned c)
{
    unsigned r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    r += (255 - r) / 4; g += (255 - g) / 4; b += (255 - b) / 4;
    return (r << 16) | (g << 8) | b;
}

/* вираз для показу: * → ×, / → ÷, - → − */
static void pretty(const char *src, char *dst, size_t n)
{
    size_t o = 0;
    const char *start = src;
    for (; *src && o + 4 < n; src++) {
        const char *rep = NULL;
        if ((*src == '+' || *src == '-') && src > start && (src[-1] == 'e' || src[-1] == 'E')) {
            dst[o++] = *src; continue;            /* знак експоненти: 1e+20 */
        }
        if (*src == '*') rep = " \xC3\x97 ";
        else if (*src == '/') rep = " \xC3\xB7 ";
        else if (*src == '+') rep = " + ";
        else if (*src == '-') {
            char pv = src > start ? src[-1] : '\0';
            rep = (is_digit(pv) || pv == ')' || pv == '%' || pv == '.') ? " \xE2\x88\x92 " : "\xE2\x88\x92";
        }
        if (rep) {
            size_t l = strlen(rep);
            if (o + l + 1 >= n) break;
            memcpy(dst + o, rep, l); o += l;
        } else dst[o++] = *src;
    }
    dst[o] = '\0';
}

/* текст, що не вміщується, обрізаємо зліва ("…хвіст") або справа ("початок…") */
static void fit_text(Rect r, const char *s, unsigned col, int font, int align, int keep_tail)
{
    char buf[512];
    size_t len;
    snprintf(buf, sizeof buf, "%s", s);
    len = strlen(buf);
    if (plat_text_width(buf, font) > r.w) {
        char tmp[sizeof buf + 4];
        if (keep_tail) {
            const char *t = s;
            while (*t) {
                t++;
                while ((*t & 0xC0) == 0x80) t++;    /* межа UTF-8 символу */
                snprintf(tmp, sizeof tmp, "\xE2\x80\xA6%s", t);
                if (plat_text_width(tmp, font) <= r.w) break;
            }
            memcpy(buf, tmp, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
        } else {
            while (len > 0) {
                len--;
                while (len > 0 && (buf[len] & 0xC0) == 0x80) len--;
                buf[len] = '\0';
                snprintf(tmp, sizeof tmp, "%s\xE2\x80\xA6", buf);
                if (plat_text_width(tmp, font) <= r.w) break;
            }
            memcpy(buf, tmp, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
        }
    }
    plat_text(r.x, r.y, r.w, r.h, buf, col, font, align);
}

/* число, що не вміщується, показуємо з меншою кількістю знаків (а не обрізаємо) */
static void fit_number(Rect r, const char *prefix, const char *num, unsigned col, int font, int align)
{
    char n2[RES_MAX], pe[96], tmp[128];
    double v;
    int prec;
    snprintf(n2, sizeof n2, "%s", num);
    for (prec = 15; ; prec--) {
        pretty(n2, pe, sizeof pe);
        snprintf(tmp, sizeof tmp, "%s%s", prefix, pe);
        if (plat_text_width(tmp, font) <= r.w || prec < 4 || evaluate(num, &v) != 0) break;
        format_number_p(v, n2, sizeof n2, prec - 1);
    }
    fit_text(r, tmp, col, font, align, 0);
}

void core_render(int w, int h)
{
    char tmp[512], pe[400];
    Rect d, r;
    int i;

    g_w = w; g_h = h;
    plat_fill(0, 0, w, h, C_BG);

    /* дисплей */
    d = display_rect();
    plat_fill(d.x, d.y, d.w, d.h, C_PANEL);
    r.x = d.x + S(12); r.w = d.w - S(24);

    pretty(g_expr, pe, sizeof pe);
    if (g_done) snprintf(tmp, sizeof tmp, "%s =", pe);
    else        snprintf(tmp, sizeof tmp, "%s", pe);
    r.y = d.y + S(6); r.h = S(30);
    fit_text(r, tmp[0] ? tmp : "0", C_DIM, FONT_MEDIUM, ALIGN_RIGHT, 1);

    r.y = d.y + S(38); r.h = S(52);
    if (g_done) {
        if (g_error) snprintf(tmp, sizeof tmp, "%s", TR(g_error == 2 ? "Ділення на 0" : g_error == 3 ? "Переповнення" : "Помилка",
                                                    g_error == 2 ? "Division by 0" : g_error == 3 ? "Overflow" : "Error"));
        if (g_error) fit_text(r, tmp, C_ERR, FONT_LARGE, ALIGN_RIGHT, 0);
        else fit_number(r, "", g_res, C_TEXT, FONT_LARGE, ALIGN_RIGHT);
    } else if (g_res[0]) {
        fit_number(r, "= ", g_res, C_DIM, FONT_LARGE, ALIGN_RIGHT);
    }

    /* кнопки */
    for (i = 0; i < BTN_COUNT; i++) {
        unsigned bg;
        r = btn_rect(i);
        switch (btn_kind(i)) {
        case K_OP:    bg = C_OP;    break;
        case K_EQ:    bg = C_EQ;    break;
        case K_FUNC:  bg = C_FUNC;  break;
        default:      bg = C_DIGIT; break;
        }
        if (g_pressed == i) bg = lighten(bg);
        plat_fill(r.x, r.y, r.w, r.h, bg);
        plat_text(r.x, r.y, r.w, r.h, BTN_LABEL[i], C_TEXT, FONT_MEDIUM, ALIGN_CENTER);
    }

    /* історія */
    r = hist_panel();
    plat_fill(r.x, r.y, r.w, r.h, C_PANEL);
    snprintf(tmp, sizeof tmp, "%s  %d/%d", TR("Історія", "History"), g_hist_n, HIST_MAX);
    plat_text(r.x + S(12), r.y + S(6), r.w - S(110), S(28), tmp, C_TEXT, FONT_MEDIUM, ALIGN_LEFT);
    if (g_hist_n) {
        Rect b = hist_clear_btn();
        plat_fill(b.x, b.y, b.w, b.h, g_pressed == ID_HCLEAR ? lighten(C_FUNC) : C_FUNC);
        plat_text(b.x, b.y, b.w, b.h, TR("Очистити", "Clear"), C_TEXT, FONT_SMALL, ALIGN_CENTER);
    } else {
        Rect e = hist_row(1);
        plat_text(e.x + S(8), e.y, e.w - S(16), e.h, TR("Тут будуть останні 15 обчислень", "Your last 15 results appear here"),
                  C_DIM, FONT_SMALL, ALIGN_LEFT);
    }
    for (i = 0; i < g_hist_n; i++) {
        Rect row = hist_row(i), t;
        int resw;
        if (g_pressed == ID_HIST0 + i) plat_fill(row.x, row.y, row.w, row.h, C_ROWHOV);
        pretty(g_hist[i].res, pe, sizeof pe);
        snprintf(tmp, sizeof tmp, "= %s", pe);
        resw = plat_text_width(tmp, FONT_SMALL);
        if (resw > row.w / 2) resw = row.w / 2;
        t.x = row.x + row.w - S(8) - resw; t.y = row.y; t.w = resw; t.h = row.h;
        fit_number(t, "= ", g_hist[i].res, C_TEXT, FONT_SMALL, ALIGN_RIGHT);
        pretty(g_hist[i].expr, pe, sizeof pe);
        t.x = row.x + S(8); t.w = row.w - resw - S(24);
        fit_text(t, pe, C_DIM, FONT_SMALL, ALIGN_LEFT, 1);
    }
}

/* ------------------------------------------------------------------ */
/*  Події                                                              */
/* ------------------------------------------------------------------ */
void core_set_english(int on) { g_en = on; }
void core_set_scale(float s) { g_scale = (s < 0.5f) ? 0.5f : (s > 4.0f ? 4.0f : s); }

void core_init(void)
{
    clear_all();
    init_hist_path();
    hist_load();
}

int core_key(int ch)
{
    if (ch == '\r' || ch == '\n' || ch == '=') { equals(); return 1; }
    if (ch == '\b') { backspace(); return 1; }
    if (ch == 27 || ch == 127 || ch == 'c' || ch == 'C') { clear_all(); return 1; }
    if (ch > 0 && ch < 128 && strchr("0123456789.,+-*/%()xX:", ch)) { input((char)ch); return 1; }
    return 0;
}

int core_mouse_down(int x, int y)
{
    g_pressed = hit_test(x, y);
    return 1;
}

int core_mouse_up(int x, int y)
{
    int id = g_pressed;
    g_pressed = -1;
    if (id < 0 || hit_test(x, y) != id) return 1;
    if (id < BTN_COUNT) {
        char k = BTN_KEY[id];
        if (k == 'C') clear_all();
        else core_key(k);
    } else if (id == ID_HCLEAR) {
        g_hist_n = 0;
        hist_save();
    } else if (id >= ID_HIST0 && id < ID_HIST0 + HIST_MAX) {
        use_history(id - ID_HIST0);
    }
    return 1;
}
