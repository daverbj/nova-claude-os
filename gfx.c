/* Nova Claude OS graphics: double-buffered linear framebuffer, primitives, icons, cursor. */
#include "os.h"
#include "font.h"

#define MAXW 1280
#define MAXH 1024

typedef struct {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4];
    u32 mmap_length, mmap_addr, drives_length, drives_addr, config_table, boot_loader_name, apm_table;
    u32 vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_seg, vbe_off, vbe_len;
    uint64_t fb_addr;
    u32 fb_pitch, fb_width, fb_height;
    u8 fb_bpp, fb_type;
    u8 rp, rs, gp, gs, bp, bs;
} __attribute__((packed)) mbinfo_t;

int SW = 1024, SH = 768;
static int FW = 1024, FH = 768, scale = 1;
static u8 *fb;
static u32 fb_pitch, fb_bpp;
static int fb_std = 1, rp = 16, gp = 8, bp = 0, rs = 8, gs = 8, bs = 8;
static u32 backbuf[MAXW * MAXH];
static u32 wallbuf[MAXW * MAXH];
static int cx0, cy0, cx1, cy1;

static void text_fatal(const char *msg){
    volatile u16 *v = (volatile u16 *)0xB8000;
    for (int i = 0; i < 80 * 25; i++) v[i] = 0x1F20;
    for (int i = 0; msg[i]; i++) v[80 + i] = 0x1F00 | (u8)msg[i];
    for (;;) __asm__ volatile("cli; hlt");
}

void gfx_init(void *mbi){
    mbinfo_t *m = mbi;
    if (!(m->flags & (1u << 12)) || m->fb_type != 1 || (m->fb_bpp != 32 && m->fb_bpp != 24))
        text_fatal("Nova Claude OS needs a 24/32-bit linear framebuffer (GRUB gfx mode). Check VM video settings.");
    fb = (u8 *)(u32)m->fb_addr; fb_pitch = m->fb_pitch; fb_bpp = m->fb_bpp;
    FW = (int)m->fb_width; FH = (int)m->fb_height;
    scale = (FW >= 1600 && FH >= 900) ? 2 : 1;          /* HiDPI: draw at half size, pixel-double on output */
    SW = FW / scale; SH = FH / scale;
    if (SW > MAXW) SW = MAXW;
    if (SH > MAXH) SH = MAXH;
    {   /* color_info is at byte 110 per spec, but GRUB pads it to 112 in practice: accept whichever is valid */
        const u8 *ci = (const u8 *)m + 110;
        if (!(ci[1] && ci[3] && ci[5])) ci += 2;
        if (ci[1] && ci[3] && ci[5]) { rp = ci[0]; rs = ci[1]; gp = ci[2]; gs = ci[3]; bp = ci[4]; bs = ci[5]; }
    }
    fb_std = (rp == 16 && gp == 8 && bp == 0 && rs == 8 && gs == 8 && bs == 8);
    gfx_clip_reset();
}

void gfx_clip(int x, int y, int w, int h){
    cx0 = x < 0 ? 0 : x; cy0 = y < 0 ? 0 : y;
    cx1 = x + w > SW ? SW : x + w; cy1 = y + h > SH ? SH : y + h;
}
void gfx_clip_reset(void){ cx0 = 0; cy0 = 0; cx1 = SW; cy1 = SH; }

static inline u32 mix(u32 d, u32 s, int a){
    u32 rb = (((d & 0xFF00FF) * (256 - a) + (s & 0xFF00FF) * a) >> 8) & 0xFF00FF;
    u32 g  = (((d & 0x00FF00) * (256 - a) + (s & 0x00FF00) * a) >> 8) & 0x00FF00;
    return rb | g;
}
u32 blend(u32 a, u32 b, int t){ if (t < 0) t = 0; if (t > 255) t = 255; return mix(a, b, t + (t >> 7)); }

void px(int x, int y, u32 c){
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) backbuf[y * SW + x] = c;
}
void fill(int x, int y, int w, int h, u32 c){
    int x1 = x + w, y1 = y + h;
    if (x < cx0) x = cx0; if (y < cy0) y = cy0;
    if (x1 > cx1) x1 = cx1; if (y1 > cy1) y1 = cy1;
    if (x1 <= x || y1 <= y) return;
    for (int j = y; j < y1; j++) {
        u32 *p = &backbuf[j * SW + x]; int n = x1 - x;
        __asm__ volatile("rep stosl" : "+D"(p), "+c"(n) : "a"(c) : "memory");
    }
}
void fill_a(int x, int y, int w, int h, u32 c, int a){
    if (a >= 255) { fill(x, y, w, h, c); return; }
    if (a <= 0) return;
    int x1 = x + w, y1 = y + h, al = a + (a >> 7);
    if (x < cx0) x = cx0; if (y < cy0) y = cy0;
    if (x1 > cx1) x1 = cx1; if (y1 > cy1) y1 = cy1;
    for (int j = y; j < y1; j++) {
        u32 *p = &backbuf[j * SW];
        for (int i = x; i < x1; i++) p[i] = mix(p[i], c, al);
    }
}
void hline(int x, int y, int w, u32 c){ fill(x, y, w, 1, c); }
void vline(int x, int y, int h, u32 c){ fill(x, y, 1, h, c); }
void rect(int x, int y, int w, int h, u32 c){
    hline(x, y, w, c); hline(x, y + h - 1, w, c); vline(x, y, h, c); vline(x + w - 1, y, h, c);
}
void vgrad(int x, int y, int w, int h, u32 c1, u32 c2){
    for (int j = 0; j < h; j++) fill(x, y + j, w, 1, blend(c1, c2, h > 1 ? j * 255 / (h - 1) : 0));
}
static int isqrt(int n){ int r = 0; while ((r + 1) * (r + 1) <= n) r++; return r; }
static void rfill_impl(int x, int y, int w, int h, int r, u32 c, int a){
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int i = 0; i < r; i++) {
        int dy2 = 2 * (r - i) - 1;
        int inset = r - (isqrt(4 * r * r - dy2 * dy2) + 1) / 2;
        fill_a(x + inset, y + i, w - 2 * inset, 1, c, a);
        fill_a(x + inset, y + h - 1 - i, w - 2 * inset, 1, c, a);
    }
    fill_a(x, y + r, w, h - 2 * r, c, a);
}
void rfill(int x, int y, int w, int h, int r, u32 c){ rfill_impl(x, y, w, h, r, c, 255); }
void rfill_a(int x, int y, int w, int h, int r, u32 c, int a){ rfill_impl(x, y, w, h, r, c, a); }
void line(int x0, int y0, int x1, int y1, u32 c){
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void circle_fill(int cx, int cy, int r, u32 c){
    for (int j = -r; j <= r; j++) { int d = isqrt(r * r - j * j); fill(cx - d, cy + j, 2 * d + 1, 1, c); }
}

/* ---- text ---- */
void draw_char(int x, int y, int ch, u32 c, int scale){
    if (ch < 32 || ch > 126) ch = ' ';
    const u8 *g = font8x16[ch];
    for (int j = 0; j < 16; j++) {
        u8 b = g[j];
        if (!b) continue;
        for (int i = 0; i < 8; i++) if (b & (0x80 >> i)) {
            if (scale == 1) px(x + i, y + j, c); else fill(x + i * scale, y + j * scale, scale, scale, c);
        }
    }
}
void draw_text_s(int x, int y, const char *s, u32 c, int scale){
    for (; *s; s++, x += 8 * scale) draw_char(x, y, *s, c, scale);
}
void draw_text(int x, int y, const char *s, u32 c){ draw_text_s(x, y, s, c, 1); }
void draw_text_shadow(int x, int y, const char *s, u32 c){
    draw_text(x + 1, y + 1, s, RGB(0, 0, 0)); draw_text(x, y, s, c);
}
int text_w(const char *s){ return (int)strlen(s) * 8; }

void ui_button(int x, int y, int w, int h, const char *label, int pressed, int primary){
    u32 bg = primary ? (pressed ? blend(accent, 0, 60) : accent) : (pressed ? RGB(0xD5, 0xDA, 0xE2) : C_WHITE);
    u32 fg = primary ? C_WHITE : C_TEXT;
    rfill(x, y, w, h, 4, primary ? blend(accent, 0, 80) : C_BORDER);
    rfill(x + 1, y + 1, w - 2, h - 2, 3, bg);
    draw_text(x + (w - text_w(label)) / 2, y + (h - 16) / 2, label, fg);
}

/* ---- icons: designed on a 16x16 grid, scaled by s ---- */
static void ir(int x, int y, int s, int a, int b, int w, int h, u32 c){ fill(x + a * s, y + b * s, w * s, h * s, c); }
void icon_draw(int id, int x, int y, int s){
    switch (id) {
    case APP_TERM:
        rfill(x + s, y + 2 * s, 14 * s, 12 * s, s + 1, RGB(0x1E, 0x24, 0x30));
        ir(x, y, s, 3, 5, 1, 1, RGB(0x6E, 0xE7, 0x8F)); ir(x, y, s, 4, 6, 1, 1, RGB(0x6E, 0xE7, 0x8F));
        ir(x, y, s, 5, 7, 1, 1, RGB(0x6E, 0xE7, 0x8F)); ir(x, y, s, 4, 8, 1, 1, RGB(0x6E, 0xE7, 0x8F));
        ir(x, y, s, 3, 9, 1, 1, RGB(0x6E, 0xE7, 0x8F)); ir(x, y, s, 8, 10, 4, 1, C_WHITE);
        break;
    case APP_FILES: case ICON_FOLDER:
        rfill(x + s, y + 3 * s, 6 * s, 3 * s, s, RGB(0xE0, 0x9F, 0x1F));
        rfill(x + s, y + 4 * s, 14 * s, 10 * s, s + 1, RGB(0xF5, 0xB6, 0x3A));
        ir(x, y, s, 1, 6, 14, 1, RGB(0xFF, 0xD6, 0x7A));
        break;
    case APP_NOTE: case ICON_FILE:
        rfill(x + 3 * s, y + s, 10 * s, 14 * s, s, C_WHITE);
        rect(x + 3 * s, y + s, 10 * s, 14 * s, RGB(0x9A, 0xA3, 0xB2));
        ir(x, y, s, 3, 1, 10, 2, accent);
        for (int i = 0; i < 3; i++) ir(x, y, s, 5, 5 + i * 2, 6, 1, RGB(0x9A, 0xA3, 0xB2));
        ir(x, y, s, 5, 11, 4, 1, RGB(0x9A, 0xA3, 0xB2));
        break;
    case APP_CALC:
        rfill(x + 3 * s, y + s, 10 * s, 14 * s, s + 1, RGB(0x3A, 0x42, 0x52));
        ir(x, y, s, 4, 2, 8, 3, RGB(0xA8, 0xE6, 0xB0));
        for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) ir(x, y, s, 4 + i * 3, 6 + j * 3, 2, 2, j == 2 && i == 2 ? RGB(0xFF, 0x8A, 0x4C) : RGB(0xDD, 0xE2, 0xEA));
        break;
    case APP_PAINT:
        rfill(x + s, y + 2 * s, 14 * s, 11 * s, 3 * s, RGB(0xF0, 0xDE, 0xB8));
        ir(x, y, s, 4, 4, 2, 2, RGB(0xE5, 0x39, 0x35)); ir(x, y, s, 8, 4, 2, 2, RGB(0x43, 0xA0, 0x47));
        ir(x, y, s, 11, 7, 2, 2, RGB(0x1E, 0x88, 0xE5)); ir(x, y, s, 5, 9, 2, 2, RGB(0xFB, 0xC0, 0x2D));
        break;
    case APP_SNAKE:
        ir(x, y, s, 4, 2, 8, 2, RGB(0x4C, 0xC9, 0x6A)); ir(x, y, s, 4, 4, 2, 3, RGB(0x4C, 0xC9, 0x6A));
        ir(x, y, s, 4, 7, 8, 2, RGB(0x4C, 0xC9, 0x6A)); ir(x, y, s, 10, 9, 2, 3, RGB(0x4C, 0xC9, 0x6A));
        ir(x, y, s, 4, 12, 8, 2, RGB(0x4C, 0xC9, 0x6A)); ir(x, y, s, 11, 2, 1, 1, C_WHITE);
        ir(x, y, s, 13, 12, 2, 2, RGB(0xE5, 0x39, 0x35));
        break;
    case APP_SYSMON:
        rfill(x + s, y + 2 * s, 14 * s, 12 * s, s + 1, RGB(0x1E, 0x24, 0x30));
        ir(x, y, s, 3, 9, 2, 4, RGB(0x4D, 0xD0, 0xE1)); ir(x, y, s, 6, 6, 2, 7, RGB(0x6E, 0xE7, 0x8F));
        ir(x, y, s, 9, 8, 2, 5, RGB(0xFB, 0xC0, 0x2D)); ir(x, y, s, 12, 4, 2, 9, RGB(0xFF, 0x8A, 0x65));
        break;
    case APP_SETTINGS:
        ir(x, y, s, 7, 1, 2, 14, RGB(0x8A, 0x94, 0xA6)); ir(x, y, s, 1, 7, 14, 2, RGB(0x8A, 0x94, 0xA6));
        ir(x, y, s, 3, 3, 10, 10, RGB(0xA9, 0xB2, 0xC2)); ir(x, y, s, 4, 2, 8, 12, RGB(0xA9, 0xB2, 0xC2));
        ir(x, y, s, 2, 4, 12, 8, RGB(0xA9, 0xB2, 0xC2));
        ir(x, y, s, 6, 6, 4, 4, RGB(0x4A, 0x52, 0x62));
        break;
    case ICON_UP:
        for (int i = 0; i < 6; i++) ir(x, y, s, 8 - i - 1, 4 + i, 2 * i + 2, 1, C_TEXT);
        ir(x, y, s, 7, 9, 2, 4, C_TEXT);
        break;
    }
}

/* ---- wallpaper ---- */
const u32 wp_stops[4][3] = {
    { RGB(8, 18, 56),  RGB(70, 44, 130), RGB(24, 128, 150) },
    { RGB(40, 20, 72), RGB(196, 72, 96), RGB(252, 178, 84) },
    { RGB(8, 36, 40),  RGB(22, 102, 74), RGB(126, 184, 92) },
    { RGB(16, 18, 24), RGB(42, 48, 62),  RGB(78, 88, 108) },
};
static const u32 wp_glow[4] = { RGB(120, 210, 255), RGB(255, 220, 160), RGB(200, 255, 190), RGB(150, 170, 210) };
int wallpaper_id;
void wallpaper_make(int id){
    wallpaper_id = id & 3;
    const u32 *st = wp_stops[wallpaper_id];
    int gx = SW * 3 / 4, gy = SH / 3, R2 = SW * SW / 4;
    for (int y = 0; y < SH; y++) {
        int dy = y - gy;
        for (int x = 0; x < SW; x++) {
            int t = (x * 256 / SW + y * 256 / SH) / 2;
            u32 c = t < 128 ? blend(st[0], st[1], t * 2) : blend(st[1], st[2], (t - 128) * 2);
            int dx = x - gx, d2 = dx * dx + dy * dy;
            if (d2 < R2) { int g = 256 - d2 * 256 / R2; g = g * g >> 8; c = blend(c, wp_glow[wallpaper_id], g * 70 >> 8); }
            wallbuf[y * SW + x] = c;
        }
    }
}
void wallpaper_blit(void){
    u32 *d = backbuf, *s = wallbuf; int n = SW * SH;
    __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
}

/* ---- framebuffer output ---- */
static inline u32 packc(u32 c){
    if (fb_std) return c;
    return ((((c >> 16) & 255) >> (8 - rs)) << rp) | ((((c >> 8) & 255) >> (8 - gs)) << gp) | (((c & 255) >> (8 - bs)) << bp);
}
static inline void fb_put(int x, int y, u32 c){
    u8 *p = fb + y * fb_pitch + x * (fb_bpp >> 3);
    c = packc(c);
    if (fb_bpp == 32) *(u32 *)p = c; else { p[0] = c; p[1] = c >> 8; p[2] = c >> 16; }
}
static inline u32 fb_get(int x, int y){
    u8 *p = fb + y * fb_pitch + x * (fb_bpp >> 3);
    return fb_bpp == 32 ? *(u32 *)p : (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16);
}
static inline void fb_put_raw(int x, int y, u32 c){
    u8 *p = fb + y * fb_pitch + x * (fb_bpp >> 3);
    if (fb_bpp == 32) *(u32 *)p = c; else { p[0] = c; p[1] = c >> 8; p[2] = c >> 16; }
}

/* cursor sprite */
#define CW 12
#define CH 19
static const char *cur_spr[CH] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
    "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ",
    "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     X..X   ", "      X..X  ", "      X..X  ",
    "       XX   " };
static int cur_x, cur_y, cur_drawn;
static u32 cur_save[CW * CH * 4];

static void cursor_hide(void){
    if (!cur_drawn) return;
    int s = scale;
    for (int j = 0; j < CH * s; j++) for (int i = 0; i < CW * s; i++) {
        int x = cur_x * s + i, y = cur_y * s + j;
        if (x < FW && y < FH && cur_spr[j / s][i / s] != ' ') fb_put_raw(x, y, cur_save[j * CW * s + i]);
    }
    cur_drawn = 0;
}
static void cursor_show(void){
    int s = scale;
    for (int j = 0; j < CH * s; j++) for (int i = 0; i < CW * s; i++) {
        int x = cur_x * s + i, y = cur_y * s + j;
        char c = cur_spr[j / s][i / s];
        if (c == ' ' || x >= FW || y >= FH) continue;
        cur_save[j * CW * s + i] = fb_get(x, y);
        fb_put(x, y, c == 'X' ? RGB(0, 0, 0) : RGB(255, 255, 255));
    }
    cur_drawn = 1;
}
void cursor_move(int x, int y){ cursor_hide(); cur_x = x; cur_y = y; cursor_show(); }
void cursor_redraw(void){ cur_drawn = 0; cursor_show(); }

static void blit_all(void){
    if (fb_bpp == 32 && fb_std) {
        for (int y = 0; y < SH; y++) {
            u32 *src = &backbuf[y * SW];
            if (scale == 1) {
                u32 *d = (u32 *)(fb + y * fb_pitch), *sp = src; int n = SW;
                __asm__ volatile("rep movsl" : "+D"(d), "+S"(sp), "+c"(n) :: "memory");
            } else {
                for (int r = 0; r < 2; r++) {
                    u32 *d = (u32 *)(fb + (y * 2 + r) * fb_pitch);
                    for (int x = 0; x < SW; x++) { u32 c = src[x]; d[2 * x] = c; d[2 * x + 1] = c; }
                }
            }
        }
    } else {
        for (int y = 0; y < SH * scale; y++) for (int x = 0; x < SW * scale; x++) fb_put(x, y, backbuf[(y / scale) * SW + x / scale]);
    }
}
void gfx_flush(void){ blit_all(); cursor_redraw(); }
void gfx_fb_fill(u32 c){ gfx_clip_reset(); fill(0, 0, SW, SH, c); cur_drawn = 0; blit_all(); }

void gfx_panic_screen(const char *title, const char *msg){
    __asm__ volatile("cli");
    gfx_clip_reset();
    fill(0, 0, SW, SH, RGB(0, 84, 166));
    draw_text_s(80, 70, ":(", C_WHITE, 8);
    draw_text_s(80, 230, title, C_WHITE, 2);
    int y = 290;
    char line[128]; int n = 0;
    for (const char *p = msg; ; p++) {
        if (*p == '\n' || !*p) { line[n] = 0; draw_text(80, y, line, C_WHITE); y += 18; n = 0; if (!*p) break; }
        else if (n < 126) line[n++] = *p;
    }
    cur_drawn = 0;
    blit_all();
    for (;;) __asm__ volatile("hlt");
}
