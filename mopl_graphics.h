#ifndef MOPL_GRAPHICS_H
#define MOPL_GRAPHICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t mopl_color_t;

int64_t mopl_gfx_init(const char *title, int64_t width, int64_t height);
void mopl_gfx_shutdown(void);
int64_t mopl_gfx_should_close(void);
int64_t mopl_gfx_is_active(void);
int64_t mopl_gfx_poll_events(void);

void mopl_gfx_clear_screen(mopl_color_t color);
void mopl_gfx_present(void);
void mopl_gfx_flip(void);

void mopl_gfx_set_pixel(int64_t x, int64_t y, mopl_color_t color);
void mopl_gfx_draw_rect(int64_t x, int64_t y, int64_t w, int64_t h, mopl_color_t color);
void mopl_gfx_draw_rect_outline(int64_t x, int64_t y, int64_t w, int64_t h,
                                int64_t thickness, mopl_color_t color);
void mopl_gfx_draw_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                        mopl_color_t color);
void mopl_gfx_draw_circle(int64_t cx, int64_t cy, int64_t radius,
                          mopl_color_t color);
void mopl_gfx_draw_text(const char *utf8_text, int64_t x, int64_t y,
                        int64_t size, mopl_color_t color);

int64_t mopl_gfx_mouse_down(void);
int64_t mopl_gfx_mouse_clicked(void);
int64_t mopl_gfx_mouse_x(void);
int64_t mopl_gfx_mouse_y(void);

int64_t mopl_gfx_key_down(int64_t virtual_keycode);
int64_t mopl_gfx_key_pressed(int64_t virtual_keycode);

#ifdef __cplusplus
}
#endif

#endif
