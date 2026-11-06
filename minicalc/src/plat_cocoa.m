/*
 * MiniCalc — платформний шар для macOS (Cocoa, лише системні фреймворки).
 * Збирається з -fobjc-arc.
 */
#import <Cocoa/Cocoa.h>
#include "core.h"

static NSFont *g_fonts[FONT_COUNT];

static NSColor *color_of(unsigned rgb)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 255) / 255.0
                               green:((rgb >> 8) & 255) / 255.0
                                blue:(rgb & 255) / 255.0
                               alpha:1.0];
}

static NSString *str_of(const char *s)
{
    NSString *r = [NSString stringWithUTF8String:s];
    return r ? r : @"";
}

void plat_fill(int x, int y, int w, int h, unsigned rgb)
{
    if (w <= 0 || h <= 0) return;
    [color_of(rgb) setFill];
    NSRectFill(NSMakeRect(x, y, w, h));
}

int plat_text_width(const char *s, int font)
{
    NSDictionary *a = @{ NSFontAttributeName: g_fonts[font] };
    return (int)ceil([str_of(s) sizeWithAttributes:a].width);
}

void plat_text(int x, int y, int w, int h, const char *s, unsigned rgb, int font, int align)
{
    NSDictionary *a = @{ NSFontAttributeName: g_fonts[font],
                         NSForegroundColorAttributeName: color_of(rgb) };
    NSString *t = str_of(s);
    NSSize sz = [t sizeWithAttributes:a];
    CGFloat tx = x;
    if (align == ALIGN_CENTER) tx = x + (w - sz.width) / 2;
    else if (align == ALIGN_RIGHT) tx = x + w - sz.width;
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(x, y, w, h));
    [t drawAtPoint:NSMakePoint(tx, y + (h - sz.height) / 2) withAttributes:a];
    [NSGraphicsContext restoreGraphicsState];
}

@interface CalcView : NSView
@end

@implementation CalcView
- (BOOL)isFlipped { return YES; }               /* y зверху вниз, як у ядрі */
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { (void)e; return YES; }

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    NSSize s = self.bounds.size;
    core_render((int)s.width, (int)s.height);
}

- (NSPoint)pt:(NSEvent *)e { return [self convertPoint:e.locationInWindow fromView:nil]; }

- (void)mouseDown:(NSEvent *)e
{
    NSPoint p = [self pt:e];
    if (core_mouse_down((int)p.x, (int)p.y)) [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent *)e
{
    NSPoint p = [self pt:e];
    if (core_mouse_up((int)p.x, (int)p.y)) [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent *)e
{
    int ch = 0;
    switch (e.keyCode) {
    case 51:  ch = '\b'; break;          /* Backspace      */
    case 36:
    case 76:  ch = '\r'; break;          /* Return / Enter */
    case 53:
    case 117: ch = 27;   break;          /* Esc / Delete   */
    default: {
        NSString *c = e.characters;
        if (c.length == 1 && [c characterAtIndex:0] < 128) ch = [c characterAtIndex:0];
    }
    }
    if (ch && core_key(ch)) [self setNeedsDisplay:YES];
    else if (!ch) [super keyDown:e];
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (strong) NSWindow *window;
@end

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)n
{
    (void)n;
    NSRect r = NSMakeRect(0, 0, CORE_WIN_W, CORE_WIN_H);
    self.window = [[NSWindow alloc] initWithContentRect:r
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"MiniCalc";
    self.window.contentMinSize = NSMakeSize(CORE_MIN_W, CORE_MIN_H);
    CalcView *v = [[CalcView alloc] initWithFrame:r];
    self.window.contentView = v;
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:v];
    [NSApp activateIgnoringOtherApps:YES];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { (void)a; return YES; }
@end

int main(void)
{
    @autoreleasepool {
        int i;
        for (i = 0; i < FONT_COUNT; i++)
            g_fonts[i] = (i == FONT_LARGE)
                ? [NSFont systemFontOfSize:core_font_px[i] weight:NSFontWeightSemibold]
                : [NSFont systemFontOfSize:core_font_px[i]];
        core_set_scale(1.0f);            /* Retina масштабує сама система */
        core_init();

        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        /* мінімальне меню, щоб працював Cmd+Q */
        NSMenu *bar = [NSMenu new];
        NSMenuItem *appItem = [NSMenuItem new];
        [bar addItem:appItem];
        NSMenu *appMenu = [NSMenu new];
        [appMenu addItemWithTitle:@"Quit MiniCalc" action:@selector(terminate:) keyEquivalent:@"q"];
        appItem.submenu = appMenu;
        app.mainMenu = bar;

        AppDelegate *d = [AppDelegate new];
        app.delegate = d;
        [app run];
    }
    return 0;
}
