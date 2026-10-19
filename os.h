#ifndef OS_H
#define OS_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;

#define OS_NAME "Nova Claude OS"
#define OS_VER  "1.0"

#define RGB(r,g,b) ((u32)(((r)<<16)|((g)<<8)|(b)))

/* ---------------- port I/O ---------------- */
static inline void outb(u16 p, u8 v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline u8   inb(u16 p){ u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outw(u16 p, u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }

/* ---------------- libc-ish ---------------- */
void  *memset(void*, int, size_t);
void  *memcpy(void*, const void*, size_t);
void  *memmove(void*, const void*, size_t);
int    memcmp(const void*, const void*, size_t);
size_t strlen(const char*);
int    strcmp(const char*, const char*);
int    strncmp(const char*, const char*, size_t);
void   scopy(char *dst, const char *src, int max);   /* always NUL-terminates */
int    ksnprintf(char *buf, int n, const char *fmt, ...);
int    katoi(const char *s);
u32    krand(void);

/* ---------------- memory ---------------- */
void  *kmalloc(size_t n);
void   kfree(void *p);
u32    heap_used(void);
u32    heap_total(void);

/* ---------------- time ---------------- */
extern volatile u32 ticks;               /* 100 Hz */
typedef struct { int y, mo, d, h, mi, s; } rtc_t;
void rtc_read(rtc_t *t);

/* ---------------- input ---------------- */
enum { K_UP = 0x100, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN, K_DEL, K_INS,
       K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12 };
enum { M_SHIFT = 1, M_CTRL = 2, M_ALT = 4 };
enum { EV_NONE, EV_MOVE, EV_DOWN, EV_UP, EV_KEY };
typedef struct { int type, x, y, btn, key, mods; } event_t;
int  event_poll(event_t *e);
int  event_pending(void);
extern int mx, my;                       /* mouse position */

/* ---------------- system ---------------- */
void sys_reboot(void);
void sys_beep(u32 hz, u32 ms);
void wm_power(int reboot);
void sys_shutdown(void);
void cpu_brand(char *out);               /* >= 49 bytes */
u32  mem_total_kb(void);

/* ---------------- filesystem (RAM) ---------------- */
#define FS_MAX   256
#define FS_NAME  32
typedef struct { char name[FS_NAME]; int used, dir, parent; u8 *data; u32 size, cap; } fsnode_t;
extern fsnode_t fs_nodes[FS_MAX];
int  fs_resolve(const char *path, int cwd);
int  fs_child(int dir, const char *name);
int  fs_create(const char *path, int cwd, int dir);   /* -1 error, -2 exists */
int  fs_remove(int id);                                /* 0 ok */
int  fs_write(int id, const void *data, u32 len);
int  fs_append(int id, const void *data, u32 len);
void fs_path(int id, char *out);
int  fs_list(int dir, int *out);                       /* returns count, sorted dirs first */
int  fs_rename(int id, const char *name);
int  fs_copy(int id, int newdir, const char *name);
int  fs_move(int id, int newdir);
int  fs_count(void);
void fs_init(void);
enum { DISK_NONE, DISK_NOVAFS, DISK_BLANK, DISK_FOREIGN };
extern int disk_state, fs_dirty;
extern u32 disk_sectors;
int  disk_init(void);
int  fs_load(void);
int  fs_save(void);
int  disk_format(void);
void fs_sync(int force);
void disk_info(char *out);

/* ---------------- graphics ---------------- */
extern int SW, SH;
void gfx_init(void *mbi);
void gfx_clip(int x, int y, int w, int h);
void gfx_clip_reset(void);
void px(int x, int y, u32 c);
void fill(int x, int y, int w, int h, u32 c);
void fill_a(int x, int y, int w, int h, u32 c, int a);       /* alpha 0..255 */
void rect(int x, int y, int w, int h, u32 c);
void hline(int x, int y, int w, u32 c);
void vline(int x, int y, int h, u32 c);
void vgrad(int x, int y, int w, int h, u32 c1, u32 c2);
void rfill(int x, int y, int w, int h, int r, u32 c);
void rfill_a(int x, int y, int w, int h, int r, u32 c, int a);
void line(int x0, int y0, int x1, int y1, u32 c);
void circle_fill(int cx, int cy, int r, u32 c);
void draw_char(int x, int y, int ch, u32 c, int scale);
void draw_text(int x, int y, const char *s, u32 c);
void draw_text_s(int x, int y, const char *s, u32 c, int scale);
void draw_text_shadow(int x, int y, const char *s, u32 c);
int  text_w(const char *s);
u32  blend(u32 a, u32 b, int t);                              /* t 0..255 : a->b */
void icon_draw(int id, int x, int y, int s);
extern const u32 wp_stops[4][3];
void wallpaper_make(int id);
void wallpaper_blit(void);
void gfx_flush(void);                                          /* backbuffer -> screen */
void cursor_move(int x, int y);
void cursor_redraw(void);
void gfx_panic_screen(const char *title, const char *msg);
void gfx_fb_fill(u32 c);

/* ---------------- window manager ---------------- */
#define TITLE_H 28
#define MAX_WIN 16
typedef struct window window_t;
typedef struct {
    const char *name;  int icon;  int cw, ch;  int resizable;
    void *(*create)(window_t *w, const char *arg);
    void  (*destroy)(window_t *w);
    void  (*draw)(window_t *w, int cx, int cy, int cw, int ch);   /* screen coords of client */
    void  (*key)(window_t *w, int key, int mods);
    int   (*mouse)(window_t *w, int type, int lx, int ly, int btn); /* returns redraw? */
    int   (*tick)(window_t *w);                                    /* returns redraw? */
} app_def_t;
struct window {
    int used, id, app, x, y, w, h, minimized, maximized, rx, ry, rw, rh;
    char title[48];
    void *state;
};
enum { APP_TERM, APP_FILES, APP_NOTE, APP_CALC, APP_PAINT, APP_SNAKE, APP_SYSMON, APP_SETTINGS, APP_COUNT };
enum { ICON_FOLDER = 20, ICON_FILE, ICON_UP };
extern app_def_t app_table[APP_COUNT];
extern window_t wins[MAX_WIN];
extern u32 accent;
extern int wallpaper_id;
extern int redraws;
int  wm_open(int app, const char *arg);
void wm_close(int id);
int  wm_focused(void);
void wm_set_title(window_t *w, const char *t);
int  wm_open_name(const char *name, const char *arg);   /* by app short name; -1 if unknown */
void wm_dirty(void);
void apps_init(void);
void fmt_uptime(char *o, u32 tk);
int  wm_count(void);

/* panics / interrupts */
typedef struct { u32 edi, esi, ebp, esp, ebx, edx, ecx, eax, int_no, err, eip, cs, eflags; } regs_t;

/* kernel init pieces */
void kernel_hw_init(void *mbi);

/* UI helpers shared by apps */
static inline int in_rect(int px_, int py_, int x, int y, int w, int h){ return px_ >= x && py_ >= y && px_ < x + w && py_ < y + h; }
void ui_button(int x, int y, int w, int h, const char *label, int pressed, int primary);

#define C_WIN_BG   RGB(0xF4,0xF5,0xF7)
#define C_PANEL    RGB(0xE6,0xE9,0xEE)
#define C_TEXT     RGB(0x1E,0x24,0x30)
#define C_MUTED    RGB(0x6B,0x74,0x84)
#define C_BORDER   RGB(0xC5,0xCB,0xD5)
#define C_WHITE    RGB(255,255,255)

#endif
