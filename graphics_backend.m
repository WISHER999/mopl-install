#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mopl_graphics.h"

/*
 * MOPL# native Cocoa graphics backend.
 *
 * Implements the complete ABI declared by Graphics_pipeline.h.
 * The assembly bridge forwards to these functions.
 */

#define MOPL_MAX_KEYS 512

@interface MoplGraphicsView : NSView
@end

static NSWindow *g_window = nil;
static MoplGraphicsView *g_view = nil;
static CGContextRef g_context = NULL;
static uint8_t *g_pixels = NULL;
static size_t g_width = 0;
static size_t g_height = 0;

static int64_t g_should_close = 0;
static int64_t g_mouse_down = 0;
static int64_t g_mouse_clicked = 0;
static int64_t g_mouse_x = 0;
static int64_t g_mouse_y = 0;

static uint8_t g_keys[MOPL_MAX_KEYS];
static uint8_t g_key_pressed[MOPL_MAX_KEYS];

static void mopl_set_pixel_raw(int64_t x, int64_t y, mopl_color_t color)
{
    if (!g_pixels || x < 0 || y < 0 ||
        (size_t)x >= g_width || (size_t)y >= g_height) {
        return;
    }

    size_t offset = ((size_t)y * g_width + (size_t)x) * 4;

    uint8_t a = (uint8_t)((color >> 24) & 0xFF);
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    /*
     * Bitmap is stored RGBA, premultiplied.
     */
    g_pixels[offset + 0] = (uint8_t)((r * a + 127) / 255);
    g_pixels[offset + 1] = (uint8_t)((g * a + 127) / 255);
    g_pixels[offset + 2] = (uint8_t)((b * a + 127) / 255);
    g_pixels[offset + 3] = a;
}

static void mopl_fill_rect_raw(int64_t x, int64_t y,
                               int64_t w, int64_t h,
                               mopl_color_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    int64_t x0 = x < 0 ? 0 : x;
    int64_t y0 = y < 0 ? 0 : y;
    int64_t x1 = x + w;
    int64_t y1 = y + h;

    if (x1 > (int64_t)g_width) {
        x1 = (int64_t)g_width;
    }

    if (y1 > (int64_t)g_height) {
        y1 = (int64_t)g_height;
    }

    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    uint8_t a = (uint8_t)((color >> 24) & 0xFF);
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    uint8_t pr = (uint8_t)((r * a + 127) / 255);
    uint8_t pg = (uint8_t)((g * a + 127) / 255);
    uint8_t pb = (uint8_t)((b * a + 127) / 255);

    for (int64_t yy = y0; yy < y1; yy++) {
        for (int64_t xx = x0; xx < x1; xx++) {
            size_t offset =
                ((size_t)yy * g_width + (size_t)xx) * 4;

            g_pixels[offset + 0] = pr;
            g_pixels[offset + 1] = pg;
            g_pixels[offset + 2] = pb;
            g_pixels[offset + 3] = a;
        }
    }
}

@implementation MoplGraphicsView

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)isFlipped
{
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;

    if (!g_context || !g_pixels) {
        return;
    }

    CGImageRef image = CGBitmapContextCreateImage(g_context);

    if (image) {
        CGContextRef ctx =
            [[NSGraphicsContext currentContext] CGContext];

        if (ctx) {
            CGContextSaveGState(ctx);

            /*
             * Our bitmap uses top-left coordinates. Flip the image
             * when drawing into the normal Cocoa coordinate system.
             */
            CGContextTranslateCTM(ctx, 0.0, self.bounds.size.height);
            CGContextScaleCTM(ctx, 1.0, -1.0);

            CGContextDrawImage(
                ctx,
                CGRectMake(0, 0,
                           (CGFloat)g_width,
                           (CGFloat)g_height),
                image
            );

            CGContextRestoreGState(ctx);
        }

        CGImageRelease(image);
    }
}

- (void)mouseDown:(NSEvent *)event
{
    NSPoint p = [self convertPoint:event.locationInWindow
                           fromView:nil];

    g_mouse_down = 1;
    g_mouse_x = (int64_t)p.x;
    g_mouse_y = (int64_t)p.y;
}

- (void)mouseUp:(NSEvent *)event
{
    NSPoint p = [self convertPoint:event.locationInWindow
                           fromView:nil];

    g_mouse_down = 0;
    g_mouse_clicked = 1;
    g_mouse_x = (int64_t)p.x;
    g_mouse_y = (int64_t)p.y;
}

- (void)mouseDragged:(NSEvent *)event
{
    NSPoint p = [self convertPoint:event.locationInWindow
                           fromView:nil];

    g_mouse_x = (int64_t)p.x;
    g_mouse_y = (int64_t)p.y;
}

- (void)mouseMoved:(NSEvent *)event
{
    NSPoint p = [self convertPoint:event.locationInWindow
                           fromView:nil];

    g_mouse_x = (int64_t)p.x;
    g_mouse_y = (int64_t)p.y;
}

- (void)keyDown:(NSEvent *)event
{
    NSUInteger key = event.keyCode;

    if (key < MOPL_MAX_KEYS) {
        if (!g_keys[key]) {
            g_key_pressed[key] = 1;
        }

        g_keys[key] = 1;
    }
}

- (void)keyUp:(NSEvent *)event
{
    NSUInteger key = event.keyCode;

    if (key < MOPL_MAX_KEYS) {
        g_keys[key] = 0;
    }
}

@end

@interface MoplWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation MoplWindowDelegate

- (void)windowWillClose:(NSNotification *)notification
{
    (void)notification;
    g_should_close = 1;
}

@end

static MoplWindowDelegate *g_delegate = nil;

int64_t mopl_gfx_init(const char *title,
                      int64_t width,
                      int64_t height)
{
    if (width <= 0 || height <= 0) {
        return 0;
    }

    @autoreleasepool {
        if (!NSApp) {
            [NSApplication sharedApplication];
        }

        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        g_width = (size_t)width;
        g_height = (size_t)height;

        size_t bytes_per_row = g_width * 4;
        size_t bytes = bytes_per_row * g_height;

        g_pixels = calloc(1, bytes);

        if (!g_pixels) {
            g_width = 0;
            g_height = 0;
            return 0;
        }

        g_context = CGBitmapContextCreate(
            g_pixels,
            g_width,
            g_height,
            8,
            bytes_per_row,
            CGColorSpaceCreateDeviceRGB(),
            (CGBitmapInfo)kCGImageAlphaPremultipliedLast
        );

        if (!g_context) {
            free(g_pixels);
            g_pixels = NULL;
            g_width = 0;
            g_height = 0;
            return 0;
        }

        NSRect frame =
            NSMakeRect(0, 0, (CGFloat)width, (CGFloat)height);

        NSUInteger style =
            NSWindowStyleMaskTitled |
            NSWindowStyleMaskClosable |
            NSWindowStyleMaskMiniaturizable |
            NSWindowStyleMaskResizable;

        g_window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:style
                        backing:NSBackingStoreBuffered
                          defer:NO];

        if (!g_window) {
            CGContextRelease(g_context);
            g_context = NULL;
            free(g_pixels);
            g_pixels = NULL;
            return 0;
        }

        NSString *nsTitle;

        if (title) {
            nsTitle = [[NSString alloc]
                initWithUTF8String:title];
        } else {
            nsTitle = @"MOPL#";
        }

        if (!nsTitle) {
            nsTitle = @"MOPL#";
        }

        [g_window setTitle:nsTitle];

        g_view = [[MoplGraphicsView alloc]
            initWithFrame:frame];

        [g_window setContentView:g_view];
        [g_window setDelegate:g_delegate =
            [[MoplWindowDelegate alloc] init]];

        [g_window makeFirstResponder:g_view];
        [g_window makeKeyAndOrderFront:nil];

        [NSApp activateIgnoringOtherApps:YES];

        g_should_close = 0;
        g_mouse_down = 0;
        g_mouse_clicked = 0;
        g_mouse_x = 0;
        g_mouse_y = 0;

        memset(g_keys, 0, sizeof(g_keys));
        memset(g_key_pressed, 0, sizeof(g_key_pressed));

        return 1;
    }
}

int64_t mopl_gfx_is_active(void)
{
    return (g_window != nil) ? 1 : 0;
}

void mopl_gfx_shutdown(void)
{
    @autoreleasepool {
        g_should_close = 1;

        if (g_window) {
            [g_window orderOut:nil];
            [g_window close];
            g_window = nil;
        }

        g_view = nil;
        g_delegate = nil;

        if (g_context) {
            CGContextRelease(g_context);
            g_context = NULL;
        }

        free(g_pixels);
        g_pixels = NULL;

        g_width = 0;
        g_height = 0;

        memset(g_keys, 0, sizeof(g_keys));
        memset(g_key_pressed, 0, sizeof(g_key_pressed));
    }
}

int64_t mopl_gfx_should_close(void)
{
    return g_should_close ? 0 : 1;
}

int64_t mopl_gfx_poll_events(void)
{
    if (!NSApp || !g_window) {
        return 0;
    }

    /*
     * Key-pressed and mouse-clicked represent edges occurring
     * during this polling interval.
     */
    memset(g_key_pressed, 0, sizeof(g_key_pressed));
    g_mouse_clicked = 0;

    int64_t count = 0;

    for (;;) {
        NSEvent *event =
            [NSApp nextEventMatchingMask:NSEventMaskAny
                               untilDate:[NSDate dateWithTimeIntervalSinceNow:0.0]
                                  inMode:NSDefaultRunLoopMode
                                 dequeue:YES];

        if (!event) {
            break;
        }

        [NSApp sendEvent:event];
        count++;
    }

    return count;
}

void mopl_gfx_clear_screen(mopl_color_t color)
{
    mopl_fill_rect_raw(
        0,
        0,
        (int64_t)g_width,
        (int64_t)g_height,
        color
    );
}

void mopl_gfx_present(void)
{
    if (!g_view || !g_window) {
        return;
    }

    [g_view setNeedsDisplay:YES];
    [g_view displayIfNeeded];

    /*
     * Keep Cocoa responsive without blocking the interpreter.
     */
    [NSApp updateWindows];
}

void mopl_gfx_flip(void)
{
    mopl_gfx_present();
}

void mopl_gfx_set_pixel(int64_t x,
                        int64_t y,
                        mopl_color_t color)
{
    mopl_set_pixel_raw(x, y, color);
}

void mopl_gfx_draw_rect(int64_t x,
                        int64_t y,
                        int64_t w,
                        int64_t h,
                        mopl_color_t color)
{
    mopl_fill_rect_raw(x, y, w, h, color);
}

void mopl_gfx_draw_rect_outline(int64_t x,
                                int64_t y,
                                int64_t w,
                                int64_t h,
                                int64_t thickness,
                                mopl_color_t color)
{
    if (thickness <= 0) {
        return;
    }

    if (thickness * 2 > w) {
        thickness = w / 2;
    }

    if (thickness * 2 > h) {
        thickness = h / 2;
    }

    if (w <= 0 || h <= 0) {
        return;
    }

    mopl_fill_rect_raw(
        x, y, w, thickness, color
    );

    mopl_fill_rect_raw(
        x, y + h - thickness, w, thickness, color
    );

    mopl_fill_rect_raw(
        x, y + thickness,
        thickness, h - thickness * 2,
        color
    );

    mopl_fill_rect_raw(
        x + w - thickness, y + thickness,
        thickness, h - thickness * 2,
        color
    );
}

void mopl_gfx_draw_line(int64_t x0,
                        int64_t y0,
                        int64_t x1,
                        int64_t y1,
                        mopl_color_t color)
{
    int64_t dx = llabs(x1 - x0);
    int64_t sx = x0 < x1 ? 1 : -1;
    int64_t dy = -llabs(y1 - y0);
    int64_t sy = y0 < y1 ? 1 : -1;
    int64_t err = dx + dy;

    for (;;) {
        mopl_set_pixel_raw(x0, y0, color);

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int64_t e2 = 2 * err;

        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void mopl_gfx_draw_circle(int64_t cx,
                          int64_t cy,
                          int64_t radius,
                          mopl_color_t color)
{
    if (radius <= 0) {
        if (radius == 0) {
            mopl_set_pixel_raw(cx, cy, color);
        }
        return;
    }

    int64_t r2 = radius * radius;

    for (int64_t y = -radius; y <= radius; y++) {
        int64_t yy = y * y;

        for (int64_t x = -radius; x <= radius; x++) {
            if (x * x + yy <= r2) {
                mopl_set_pixel_raw(
                    cx + x,
                    cy + y,
                    color
                );
            }
        }
    }
}

void mopl_gfx_draw_text(const char *utf8_text,
                        int64_t x,
                        int64_t y,
                        int64_t size,
                        mopl_color_t color)
{
    if (!g_context || !utf8_text || size <= 0) {
        return;
    }

    NSString *text =
        [[NSString alloc] initWithUTF8String:utf8_text];

    if (!text) {
        return;
    }

    uint8_t a = (uint8_t)((color >> 24) & 0xFF);
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    NSColor *nsColor =
        [NSColor colorWithCalibratedRed:(CGFloat)r / 255.0
                                  green:(CGFloat)g / 255.0
                                   blue:(CGFloat)b / 255.0
                                  alpha:(CGFloat)a / 255.0];

    NSFont *font =
        [NSFont systemFontOfSize:(CGFloat)size];

    NSDictionary *attributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: nsColor
    };

    /*
     * Draw into the bitmap using a flipped coordinate system.
     */
    CGContextSaveGState(g_context);

    CGContextTranslateCTM(
        g_context,
        0,
        (CGFloat)g_height
    );

    CGContextScaleCTM(
        g_context,
        1,
        -1
    );

    NSGraphicsContext *graphicsContext =
        [NSGraphicsContext graphicsContextWithCGContext:g_context
                                                   flipped:NO];

    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:graphicsContext];

    [text drawAtPoint:NSMakePoint(
        (CGFloat)x,
        (CGFloat)(g_height - y - size)
    ) withAttributes:attributes];

    [NSGraphicsContext restoreGraphicsState];

    CGContextRestoreGState(g_context);
}

int64_t mopl_gfx_mouse_down(void)
{
    return g_mouse_down;
}

int64_t mopl_gfx_mouse_clicked(void)
{
    return g_mouse_clicked;
}

int64_t mopl_gfx_mouse_x(void)
{
    return g_mouse_x;
}

int64_t mopl_gfx_mouse_y(void)
{
    return g_mouse_y;
}

int64_t mopl_gfx_key_down(int64_t virtual_keycode)
{
    if (virtual_keycode < 0 ||
        virtual_keycode >= MOPL_MAX_KEYS) {
        return 0;
    }

    return g_keys[virtual_keycode] ? 1 : 0;
}

int64_t mopl_gfx_key_pressed(int64_t virtual_keycode)
{
    if (virtual_keycode < 0 ||
        virtual_keycode >= MOPL_MAX_KEYS) {
        return 0;
    }

    return g_key_pressed[virtual_keycode] ? 1 : 0;
}
