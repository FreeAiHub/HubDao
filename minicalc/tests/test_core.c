/* Юніт-тести ядра MiniCalc: cc -std=c99 tests/test_core.c -lm && ./a.out */
#define _DARWIN_C_SOURCE 1   /* mkdtemp/setenv на macOS */
#define _DEFAULT_SOURCE 1    /* ... і на glibc */
#include <stdlib.h>
#include <unistd.h>
#include "../src/core.c"

/* заглушки платформи: 8 px на символ */
void plat_fill(int x, int y, int w, int h, unsigned c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void plat_text(int x, int y, int w, int h, const char *s, unsigned c, int f, int a)
{ (void)x; (void)y; (void)w; (void)h; (void)s; (void)c; (void)f; (void)a; }
int plat_text_width(const char *s, int f) { int n = 0; (void)f; for (; *s; s++) if ((*s & 0xC0) != 0x80) n++; return n * 8; }

static int fails, total;

static void type(const char *keys) { for (; *keys; keys++) core_key(*keys); }

static void expect_res(const char *keys, const char *want)
{
    clear_all();
    type(keys);
    total++;
    if (strcmp(g_res, want) != 0) { fails++; printf("FAIL  %-22s -> '%s' (очікував '%s')\n", keys, g_res, want); }
}
static void expect_err(const char *keys, int code)
{
    clear_all();
    type(keys);
    total++;
    if (g_error != code) { fails++; printf("FAIL  %-22s -> помилка %d (очікував %d)\n", keys, g_error, code); }
}
static void expect_expr(const char *keys, const char *want)
{
    clear_all();
    type(keys);
    total++;
    if (strcmp(g_expr, want) != 0) { fails++; printf("FAIL  %-22s -> вираз '%s' (очікував '%s')\n", keys, g_expr, want); }
}

int main(void)
{
    char tmpdir[] = "/tmp/minicalc_test_XXXXXX";
    int i;
    if (!mkdtemp(tmpdir)) return 2;
    setenv("XDG_DATA_HOME", tmpdir, 1);
    setenv("HOME", tmpdir, 1);
    core_init();

    /* арифметика і пріоритети */
    expect_res("2+3*4=", "14");
    expect_res("(2+3)*4=", "20");
    expect_res("10/4=", "2.5");
    expect_res("0.1+0.2=", "0.3");
    expect_res("-5*-3=", "15");
    expect_res("2*(3+4", "14");          /* прев'ю з незакритою дужкою */
    expect_res("2*(3+4=", "14");         /* дужка закривається сама */
    expect_res("50%=", "0.5");
    expect_res("200*10%=", "20");
    expect_res("1/3*3=", "1");
    expect_res("7-10=", "-3");
    expect_res("3.5.5=", "3.55");        /* друга крапка ігнорується */
    expect_res(".5+.5=", "1");
    expect_res("2++3=", "5");            /* оператор замінюється */
    expect_res("2*+3=", "5");
    expect_res("123456789*1000=", "123456789000");
    expect_res("9+1\b2=", "11");         /* backspace */
    expect_res("5=*2=", "10");           /* продовження від результату */
    expect_res("5=3=", "3");             /* цифра після = починає новий вираз */
    expect_res("2(3)=", "6");            /* неявне множення */
    expect_res("2+=", "2");              /* хвостовий оператор відкидається */
    expect_err("7/0=", 2);
    expect_err("1/(2-2)=", 2);
    expect_expr("((", "((");
    expect_expr("(*", "(");
    expect_expr(")", "");
    expect_expr("5*-+", "5+");
    expect_expr("1e", "1");              /* 'e' з клавіатури не вводиться */

    /* історія: 15 записів, найновіший перший, переживає перезапуск */
    g_hist_n = 0; hist_save();
    for (i = 1; i <= 20; i++) { char b[16]; clear_all(); snprintf(b, sizeof b, "%d+0=", i); type(b); }
    total++;
    if (g_hist_n != 15 || strcmp(g_hist[0].res, "20") || strcmp(g_hist[14].res, "6")) {
        fails++; printf("FAIL  історія: n=%d first=%s last=%s\n", g_hist_n, g_hist[0].res, g_hist[14].res);
    }
    g_hist_n = 0;
    hist_load();                          /* "перезапуск" */
    total++;
    if (g_hist_n != 15 || strcmp(g_hist[0].expr, "20+0")) { fails++; printf("FAIL  завантаження історії n=%d\n", g_hist_n); }
    expect_err("1/0=", 2);
    total++;
    if (g_hist_n != 15 || strcmp(g_hist[0].res, "20")) { fails++; puts("FAIL  помилка не має потрапляти в історію"); }

    /* клік по запису історії вставляє результат */
    clear_all(); type("1+"); use_history(0);
    total++;
    if (strcmp(g_expr, "1+20")) { fails++; printf("FAIL  вставка з історії: '%s'\n", g_expr); }

    /* рендер не падає на будь-якому розмірі */
    core_render(CORE_WIN_W, CORE_WIN_H);
    core_render(CORE_MIN_W, CORE_MIN_H);
    core_render(2000, 1400);

    printf("%d/%d тестів пройдено\n", total - fails, total);
    return fails ? 1 : 0;
}
