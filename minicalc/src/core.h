/*
 * MiniCalc — спільне ядро (логіка, історія, розкладка, малювання).
 * Платформний шар (Win32 / X11 / Cocoa) лише надає 3 функції малювання
 * і передає події миші/клавіатури у функції core_*.
 */
#ifndef MINICALC_CORE_H
#define MINICALC_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Шрифти, які має надати платформа */
enum { FONT_SMALL = 0, FONT_MEDIUM = 1, FONT_LARGE = 2, FONT_COUNT = 3 };
/* Вирівнювання тексту */
enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

/* Розміри шрифтів у логічних пікселях (платформа множить на свій DPI-масштаб) */
extern const int core_font_px[FONT_COUNT];

/* Початковий розмір вікна (логічні пікселі) */
#define CORE_WIN_W 600
#define CORE_WIN_H 460
#define CORE_MIN_W 480
#define CORE_MIN_H 380

/* ---- Реалізує платформа ---- */
void plat_fill(int x, int y, int w, int h, unsigned rgb);
/* Текст у UTF-8, вертикально по центру прямокутника */
void plat_text(int x, int y, int w, int h, const char *utf8,
               unsigned rgb, int font, int align);
int  plat_text_width(const char *utf8, int font);

/* ---- Реалізує ядро ---- */
void core_init(void);              /* завантажує історію з диска      */
void core_set_scale(float scale);  /* DPI-масштаб (1.0 = 96 dpi)       */
void core_set_english(int on);     /* 1 = англ. написи (немає кирилиці) */
void core_render(int w, int h);    /* малює все вікно                  */
int  core_mouse_down(int x, int y);/* 1 = потрібно перемалювати        */
int  core_mouse_up(int x, int y);
/* Символ клавіатури: цифри, + - * / % ( ) . , = ;
 * '\r' або '=' — обчислити, '\b' — стерти, 27 (Esc) або 127 (Delete) — очистити */
int  core_key(int ch);

#ifdef __cplusplus
}
#endif
#endif
