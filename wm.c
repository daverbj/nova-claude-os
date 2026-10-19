/* Nova Claude OS window manager + desktop shell + kernel entry point. */
#include "os.h"

#define TB_H 40

window_t wins[MAX_WIN];
static int zorder[MAX_WIN], nz;
static int focus_id = -1;
u32 accent = RGB(0x2D, 0x7D, 0xD2);
int redraws;
static int dirty = 1, start_open;
static rtc_t now;

static const char *short_names[APP_COUNT] = { "term", "files", "notepad", "calc", "paint", "snake", "sysmon", "settings" };
static const char *desk_labels[APP_COUNT] = { "Terminal", "Files", "Notepad", "Calculator", "Paint", "Snake", "Monitor", "Settings" };
static const char *months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

void wm_dirty(void){ dirty = 1; }
int  wm_focused(void){ return focus_id; }
int  wm_count(void){ int n = 0; for (int i = 0; i < MAX_WIN; i++) if (wins[i].used) n++; return n; }
void wm_set_title(window_t *w, const char *t){ scopy(w->title, t, sizeof w->title); dirty = 1; }

int wm_open_name(const char *name, const char *arg){
    for (int i = 0; i < APP_COUNT; i++) if (!strcmp(name, short_names[i])) return wm_open(i, arg);
    if (!strcmp(name, "terminal") || !strcmp(name, "shell")) return wm_open(APP_TERM, arg);
    if (!strcmp(name, "fm") || !strcmp(name, "filemanager")) return wm_open(APP_FILES, arg);
    if (!strcmp(name, "edit") || !strcmp(name, "editor")) return wm_open(APP_NOTE, arg);
    if (!strcmp(name, "calculator")) return wm_open(APP_CALC, arg);
    if (!strcmp(name, "monitor") || !strcmp(name, "taskmgr")) return wm_open(APP_SYSMON, arg);
    return -2;
}

static void focus_win(int id){
    int k = -1;
    for (int i = 0; i < nz; i++) if (zorder[i] == id) k = i;
    if (k >= 0) { for (int i = k; i < nz - 1; i++) zorder[i] = zorder[i + 1]; nz--; }
    zorder[nz++] = id;
    wins[id].minimized = 0; focus_id = id; dirty = 1;
}
static void focus_top(void){
    focus_id = -1;
    for (int i = nz - 1; i >= 0; i--) if (!wins[zorder[i]].minimized) { focus_id = zorder[i]; break; }
}

int wm_open(int app, const char *arg){
    int id = -1;
    for (int i = 0; i < MAX_WIN; i++) if (!wins[i].used) { id = i; break; }
    if (id < 0) return -1;
    app_def_t *a = &app_table[app];
    window_t *w = &wins[id];
    static int casc;
    memset(w, 0, sizeof *w);
    w->used = 1; w->id = id; w->app = app;
    w->w = a->cw + 2; w->h = a->ch + TITLE_H + 1;
    int per = (SH - TB_H - 16) / 88; if (per < 1) per = 1;
    int cols = (APP_COUNT + per - 1) / per;
    w->x = 24 + cols * 96 + casc * 28; w->y = 30 + casc * 28; casc = (casc + 1) % 8;
    if (w->x + w->w > SW) w->x = SW - w->w;
    if (w->y + w->h > SH - TB_H) w->y = SH - TB_H - w->h;
    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;
    scopy(w->title, a->name, sizeof w->title);
    w->state = a->create(w, arg);
    if (!w->state) { w->used = 0; return -1; }
    focus_win(id);
    return id;
}
void wm_close(int id){
    window_t *w = &wins[id];
    if (!w->used) return;
    if (app_table[w->app].destroy) app_table[w->app].destroy(w);
    kfree(w->state);
    w->used = 0; w->state = 0;
    int k = 0;
    for (int i = 0; i < nz; i++) if (zorder[i] != id) zorder[k++] = zorder[i];
    nz = k;
    if (focus_id == id) focus_top();
    dirty = 1;
}

/* ---------------- geometry ---------------- */
static int cl_x(window_t *w){ return w->x + w->w - 1 - 34; }
static int mxb_x(window_t *w){ return cl_x(w) - 34; }
static int mn_x(window_t *w){ return app_table[w->app].resizable ? mxb_x(w) - 34 : cl_x(w) - 34; }

static int win_at(int x, int y){
    for (int i = nz - 1; i >= 0; i--) {
        window_t *w = &wins[zorder[i]];
        if (!w->minimized && in_rect(x, y, w->x, w->y, w->w, w->h)) return zorder[i];
    }
    return -1;
}
/* start menu */
#define MENU_W 250
#define MENU_H 380
static int menu_x(void){ return 6; }
static int menu_y(void){ return SH - TB_H - MENU_H - 6; }
static int menu_hit(int x, int y){
    int mx0 = menu_x(), my0 = menu_y();
    if (!in_rect(x, y, mx0, my0, MENU_W, MENU_H)) return -1;
    for (int i = 0; i < APP_COUNT; i++) if (in_rect(x, y, mx0, my0 + 56 + i * 34, MENU_W, 34)) return i;
    int fy = my0 + 56 + APP_COUNT * 34 + 10;
    if (in_rect(x, y, mx0 + 12, fy, 106, 32)) return 100;
    if (in_rect(x, y, mx0 + MENU_W - 118, fy, 106, 32)) return 101;
    return 99;
}
/* taskbar */
static int tb_ids[MAX_WIN], tb_n, tb_bw;
static void build_tasklist(void){
    tb_n = 0;
    for (int i = 0; i < MAX_WIN; i++) if (wins[i].used) tb_ids[tb_n++] = i;
    int avail = SW - 106 - 120;
    tb_bw = tb_n ? avail / tb_n : 160;
    if (tb_bw > 170) tb_bw = 170;
}
static int tb_hit(int x, int y){
    if (y < SH - TB_H) return -1;
    for (int i = 0; i < tb_n; i++) if (in_rect(x, y, 106 + i * tb_bw, SH - TB_H + 4, tb_bw - 4, 32)) return tb_ids[i];
    return -1;
}
static int in_start_btn(int x, int y){ return in_rect(x, y, 6, SH - TB_H + 4, 92, 32); }
/* desktop icons */
static int icon_pos(int i, int *x, int *y){
    int per = (SH - TB_H - 16) / 88; if (per < 1) per = 1;
    *x = 14 + (i / per) * 96; *y = 16 + (i % per) * 88;
    return 0;
}
static int icon_at(int x, int y){
    for (int i = 0; i < APP_COUNT; i++) { int ix, iy; icon_pos(i, &ix, &iy); if (in_rect(x, y, ix, iy, 88, 84)) return i; }
    return -1;
}
static int sel_icon = -1;

/* ---------------- drawing ---------------- */
static void draw_window(window_t *w, int active){
    int x = w->x, y = w->y;
    rfill_a(x - 4, y - 2, w->w + 8, w->h + 10, 10, 0, 22);
    rfill_a(x - 2, y, w->w + 4, w->h + 5, 8, 0, 34);
    u32 top = active ? accent : RGB(0xDA, 0xDE, 0xE6);
    u32 border = active ? blend(accent, 0, 70) : RGB(0xB8, 0xBE, 0xC9);
    fill(x, y + TITLE_H, w->w, w->h - TITLE_H, border);
    rfill(x, y, w->w, TITLE_H + 8, 7, top);
    fill(x, y + TITLE_H, w->w, 8, border);
    fill_a(x + 1, y + TITLE_H / 2, w->w - 2, TITLE_H / 2, 0, active ? 30 : 8);
    hline(x + 4, y + 1, w->w - 8, blend(top, C_WHITE, 90));
    int tcol = active ? C_WHITE : RGB(0x4A, 0x52, 0x62);
    app_def_t *a = &app_table[w->app];
    icon_draw(a->icon, x + 8, y + 6, 1);
    gfx_clip(x + 30, y, mn_x(w) - x - 34, TITLE_H);
    draw_text(x + 30, y + 6, w->title, tcol);
    gfx_clip_reset();
    /* buttons */
    int by = y + 1, bh = TITLE_H - 1;
    int hx = mx, hy = my;
    int bxs[3] = { mn_x(w), mxb_x(w), cl_x(w) };
    for (int k = 0; k < 3; k++) {
        if (k == 1 && !a->resizable) continue;
        int bx = bxs[k];
        int hov = in_rect(hx, hy, bx, by, 34, bh);
        if (hov) fill_a(bx, by, 34, bh, k == 2 ? RGB(232, 17, 35) : RGB(255, 255, 255), k == 2 ? 255 : 55);
        u32 gc = (k == 2 && hov) ? C_WHITE : tcol;
        int cx = bx + 17, cy = by + bh / 2;
        if (k == 0) hline(cx - 5, cy + 3, 10, gc);
        else if (k == 1) {
            if (w->maximized) { rect(cx - 3, cy - 4, 8, 8, gc); hline(cx - 5, cy - 2, 6, gc); rect(cx - 5, cy - 2, 8, 8, gc); }
            else rect(cx - 5, cy - 5, 10, 10, gc);
        } else { line(cx - 5, cy - 5, cx + 4, cy + 4, gc); line(cx - 5, cy + 4, cx + 4, cy - 5, gc); line(cx - 4, cy - 5, cx + 5, cy + 4, gc); line(cx - 4, cy + 4, cx + 5, cy - 5, gc); }
    }
    /* client */
    int cx = x + 1, cy = y + TITLE_H, cw = w->w - 2, ch = w->h - TITLE_H - 1;
    gfx_clip(cx, cy, cw, ch);
    fill(cx, cy, cw, ch, C_WIN_BG);
    if (a->draw) a->draw(w, cx, cy, cw, ch);
    gfx_clip_reset();
    if (a->resizable && !w->maximized) {
        for (int i = 0; i < 3; i++) line(x + w->w - 5 - i * 4, y + w->h - 2, x + w->w - 2, y + w->h - 5 - i * 4, RGB(0x9A, 0xA3, 0xB2));
    }
}

static void draw_desktop_icons(void){
    for (int i = 0; i < APP_COUNT; i++) {
        int x, y; icon_pos(i, &x, &y);
        int hov = in_rect(mx, my, x, y, 88, 84);
        if (i == sel_icon) rfill_a(x + 12, y, 64, 64, 10, C_WHITE, 70);
        else if (hov) rfill_a(x + 12, y, 64, 64, 10, C_WHITE, 36);
        else rfill_a(x + 12, y, 64, 64, 10, 0, 40);
        icon_draw(app_table[i].icon, x + 28, y + 16, 2);
        int tw = text_w(desk_labels[i]);
        draw_text_shadow(x + 44 - tw / 2, y + 66, desk_labels[i], C_WHITE);
    }
}

static void draw_taskbar(void){
    int y0 = SH - TB_H;
    fill_a(0, y0, SW, TB_H, RGB(14, 18, 28), 225);
    hline(0, y0, SW, blend(RGB(14, 18, 28), C_WHITE, 50));
    int hov = in_start_btn(mx, my);
    rfill_a(6, y0 + 4, 92, 32, 6, start_open ? accent : C_WHITE, start_open ? 255 : (hov ? 50 : 22));
    fill(16, y0 + 12, 7, 7, RGB(0xFF, 0x6B, 0x6B)); fill(25, y0 + 12, 7, 7, RGB(0x6E, 0xE7, 0x8F));
    fill(16, y0 + 21, 7, 7, RGB(0x4D, 0xD0, 0xE1)); fill(25, y0 + 21, 7, 7, RGB(0xFB, 0xC0, 0x2D));
    draw_text(42, y0 + 12, "Start", C_WHITE);
    build_tasklist();
    for (int i = 0; i < tb_n; i++) {
        window_t *w = &wins[tb_ids[i]];
        int bx = 106 + i * tb_bw, bw = tb_bw - 4;
        int act = (w->id == focus_id && !w->minimized);
        int h2 = in_rect(mx, my, bx, y0 + 4, bw, 32);
        rfill_a(bx, y0 + 4, bw, 32, 6, C_WHITE, act ? 46 : (h2 ? 32 : 14));
        if (!w->minimized) fill(bx + 8, y0 + 34, bw - 16, 2, act ? accent : RGB(120, 130, 150));
        icon_draw(app_table[w->app].icon, bx + 8, y0 + 12, 1);
        gfx_clip(bx + 28, y0, bw - 32, TB_H);
        draw_text(bx + 30, y0 + 12, w->title, w->minimized ? RGB(160, 168, 184) : C_WHITE);
        gfx_clip_reset();
    }
    char t[24];
    ksnprintf(t, sizeof t, "%02d:%02d", now.h, now.mi);
    draw_text(SW - 98 + 22, y0 + 3, t, C_WHITE);
    ksnprintf(t, sizeof t, "%d %s %d", now.d, months[(now.mo + 11) % 12], now.y);
    draw_text(SW - 98, y0 + 20, t, RGB(190, 198, 214));
}

static void draw_start_menu(void){
    int x = menu_x(), y = menu_y();
    rfill_a(x - 2, y - 2, MENU_W + 4, MENU_H + 8, 12, 0, 50);
    rfill(x, y, MENU_W, MENU_H, 10, RGB(26, 31, 44));
    rect(x, y, MENU_W, MENU_H, RGB(52, 60, 80));
    draw_text_s(x + 16, y + 12, OS_NAME, C_WHITE, 2);
    draw_text(x + 16, y + 38, "user@nova", RGB(150, 160, 182));
    hline(x + 12, y + 54, MENU_W - 24, RGB(52, 60, 80));
    int hit = menu_hit(mx, my);
    for (int i = 0; i < APP_COUNT; i++) {
        int iy = y + 56 + i * 34;
        if (hit == i) rfill(x + 6, iy + 1, MENU_W - 12, 32, 6, blend(RGB(26, 31, 44), accent, 200));
        icon_draw(app_table[i].icon, x + 18, iy + 9, 1);
        draw_text(x + 48, iy + 9, app_table[i].name, C_WHITE);
    }
    int fy = y + 56 + APP_COUNT * 34 + 10;
    hline(x + 12, fy - 6, MENU_W - 24, RGB(52, 60, 80));
    rfill(x + 12, fy, 106, 32, 6, hit == 100 ? RGB(70, 80, 104) : RGB(40, 47, 64));
    draw_text(x + 12 + (106 - 56) / 2, fy + 8, "Restart", C_WHITE);
    rfill(x + MENU_W - 118, fy, 106, 32, 6, hit == 101 ? RGB(200, 50, 60) : RGB(150, 40, 50));
    draw_text(x + MENU_W - 118 + (106 - 72) / 2, fy + 8, "Shut down", C_WHITE);
}

static void redraw(void){
    gfx_clip_reset();
    wallpaper_blit();
    draw_desktop_icons();
    for (int i = 0; i < nz; i++) {
        window_t *w = &wins[zorder[i]];
        if (!w->minimized) draw_window(w, w->id == focus_id);
    }
    draw_taskbar();
    if (start_open) draw_start_menu();
    gfx_flush();
    redraws++;
}

/* ---------------- power screens ---------------- */
static void wait_ticks(int n){ u32 e = ticks + n; while (ticks < e) __asm__ volatile("hlt"); }
static void centered(const char *s, int y, int scale, u32 c){ draw_text_s((SW - (int)strlen(s) * 8 * scale) / 2, y, s, c, scale); }
void wm_power(int reboot){
    gfx_clip_reset();
    fill(0, 0, SW, SH, RGB(8, 10, 16));
    centered(reboot ? "Restarting..." : "Shutting down...", SH / 2 - 16, 2, C_WHITE);
    gfx_flush();
    fs_sync(1);
    wait_ticks(60);
    if (reboot) sys_reboot();
    sys_shutdown();
    fill(0, 0, SW, SH, RGB(0, 0, 0));
    centered("It is now safe to turn off the virtual machine.", SH / 2 - 8, 1, RGB(255, 170, 60));
    gfx_flush();
    for (;;) __asm__ volatile("cli; hlt");
}

/* ---------------- input handling ---------------- */
static int drag_win = -1, drag_dx, drag_dy;
static int resize_win = -1, rz_dx, rz_dy;
static int capture = -1;
static u32 last_icon_click, last_tb_click; static int last_icon = -1, last_tb_win = -1;
static int last_hover;

static void toggle_max(window_t *w){
    if (!app_table[w->app].resizable) return;
    if (w->maximized) { w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh; w->maximized = 0; }
    else { w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h; w->x = 0; w->y = 0; w->w = SW; w->h = SH - TB_H; w->maximized = 1; }
    dirty = 1;
}
static void fwd_mouse(window_t *w, int type, int x, int y, int btn){
    app_def_t *a = &app_table[w->app];
    if (!a->mouse) return;
    if (a->mouse(w, type, x - (w->x + 1), y - (w->y + TITLE_H), btn)) dirty = 1;
}

static void on_down(int x, int y, int btn){
    if (btn != 1) {
        int wi = win_at(x, y);
        if (wi >= 0 && y >= wins[wi].y + TITLE_H) { focus_win(wi); fwd_mouse(&wins[wi], EV_DOWN, x, y, btn); }
        return;
    }
    if (start_open) {
        int it = menu_hit(x, y);
        start_open = 0; dirty = 1;
        if (it >= 0 && it < APP_COUNT) { wm_open(it, 0); return; }
        if (it == 100) { wm_power(1); return; }
        if (it == 101) { wm_power(0); return; }
        if (it == 99 || in_start_btn(x, y)) return;
    }
    if (y >= SH - TB_H) {
        if (in_start_btn(x, y)) { start_open = 1; dirty = 1; return; }
        int wid = tb_hit(x, y);
        if (wid >= 0) {
            if (wins[wid].minimized || focus_id != wid) focus_win(wid);
            else { wins[wid].minimized = 1; focus_top(); }
            dirty = 1;
        }
        return;
    }
    int wi = win_at(x, y);
    if (wi >= 0) {
        window_t *w = &wins[wi];
        app_def_t *a = &app_table[w->app];
        focus_win(wi);
        if (y < w->y + TITLE_H) {
            if (y > w->y && in_rect(x, y, cl_x(w), w->y + 1, 34, TITLE_H - 1)) { wm_close(wi); return; }
            if (a->resizable && in_rect(x, y, mxb_x(w), w->y + 1, 34, TITLE_H - 1)) { toggle_max(w); return; }
            if (in_rect(x, y, mn_x(w), w->y + 1, 34, TITLE_H - 1)) { w->minimized = 1; focus_top(); dirty = 1; return; }
            if (last_tb_win == wi && ticks - last_tb_click < 40) { toggle_max(w); last_tb_win = -1; return; }
            last_tb_win = wi; last_tb_click = ticks;
            if (!w->maximized) { drag_win = wi; drag_dx = x - w->x; drag_dy = y - w->y; }
        } else if (a->resizable && !w->maximized && x > w->x + w->w - 16 && y > w->y + w->h - 16) {
            resize_win = wi; rz_dx = w->x + w->w - x; rz_dy = w->y + w->h - y;
        } else {
            capture = wi;
            fwd_mouse(w, EV_DOWN, x, y, btn);
        }
        return;
    }
    focus_id = -1; dirty = 1;
    int ic = icon_at(x, y);
    if (ic >= 0) {
        if (ic == last_icon && ticks - last_icon_click < 50) { wm_open(ic, 0); last_icon = -1; sel_icon = -1; }
        else { last_icon = ic; last_icon_click = ticks; sel_icon = ic; }
    } else sel_icon = -1;
}

static void on_move(int x, int y, int btn){
    if (drag_win >= 0) {
        window_t *w = &wins[drag_win];
        w->x = x - drag_dx; w->y = y - drag_dy;
        if (w->y < 0) w->y = 0;
        if (w->y > SH - TB_H - TITLE_H) w->y = SH - TB_H - TITLE_H;
        if (w->x < 60 - w->w) w->x = 60 - w->w;
        if (w->x > SW - 60) w->x = SW - 60;
        dirty = 1;
    } else if (resize_win >= 0) {
        window_t *w = &wins[resize_win];
        int nw = x + rz_dx - w->x, nh = y + rz_dy - w->y;
        if (nw < 240) nw = 240;
        if (nh < 150) nh = 150;
        if (w->x + nw > SW) nw = SW - w->x;
        if (w->y + nh > SH - TB_H) nh = SH - TB_H - w->y;
        w->w = nw; w->h = nh; dirty = 1;
    } else if (capture >= 0 && wins[capture].used) {
        fwd_mouse(&wins[capture], EV_MOVE, x, y, btn);
    } else {
        int wi = win_at(x, y);
        if (wi >= 0 && wi == focus_id && y >= wins[wi].y + TITLE_H) fwd_mouse(&wins[wi], EV_MOVE, x, y, btn);
    }
}

static void on_up(int x, int y, int btn){
    drag_win = -1; resize_win = -1;
    if (capture >= 0) {
        int c = capture; capture = -1;
        if (wins[c].used) fwd_mouse(&wins[c], EV_UP, x, y, btn);
    }
}

static void on_key(int key, int mods){
    if ((mods & M_ALT) && key == '\t') { if (nz > 1) focus_win(zorder[0]); return; }
    if ((mods & M_ALT) && key == K_F4) { if (focus_id >= 0) wm_close(focus_id); return; }
    if ((mods & M_CTRL) && (mods & M_ALT) && key == 't') { wm_open(APP_TERM, 0); return; }
    if (key == 27 && start_open) { start_open = 0; dirty = 1; return; }
    if (focus_id >= 0) {
        window_t *w = &wins[focus_id];
        if (app_table[w->app].key) app_table[w->app].key(w, key, mods);
        dirty = 1;
    }
}

static int hover_sig(void){
    int x = mx, y = my;
    if (start_open) { int it = menu_hit(x, y); if (it >= 0) return 1000 + it; }
    if (y >= SH - TB_H) { if (in_start_btn(x, y)) return 1; return 10 + tb_hit(x, y); }
    int wi = win_at(x, y);
    if (wi >= 0) {
        window_t *w = &wins[wi];
        if (y < w->y + TITLE_H) {
            if (in_rect(x, y, cl_x(w), w->y + 1, 34, TITLE_H)) return 100 + wi * 4 + 1;
            if (in_rect(x, y, mxb_x(w), w->y + 1, 34, TITLE_H)) return 100 + wi * 4 + 2;
            if (in_rect(x, y, mn_x(w), w->y + 1, 34, TITLE_H)) return 100 + wi * 4 + 3;
        }
        return 0;
    }
    int ic = icon_at(x, y);
    return ic >= 0 ? 200 + ic : 0;
}

/* ---------------- boot splash + kmain ---------------- */
static void splash(const char *msg, int step, int steps){
    gfx_clip_reset();
    vgrad(0, 0, SW, SH, RGB(8, 12, 24), RGB(26, 34, 66));
    int cx = SW / 2, cy = SH / 2 - 60;
    rfill(cx - 34, cy - 34, 32, 32, 6, RGB(0xFF, 0x6B, 0x6B)); rfill(cx + 2, cy - 34, 32, 32, 6, RGB(0x6E, 0xE7, 0x8F));
    rfill(cx - 34, cy + 2, 32, 32, 6, RGB(0x4D, 0xD0, 0xE1)); rfill(cx + 2, cy + 2, 32, 32, 6, RGB(0xFB, 0xC0, 0x2D));
    centered(OS_NAME, cy + 60, 3, C_WHITE);
    centered(msg, cy + 130, 1, RGB(160, 170, 195));
    int bw = 260, bx = cx - bw / 2, by = cy + 160;
    rfill(bx, by, bw, 6, 3, RGB(40, 48, 72));
    rfill(bx, by, bw * step / steps, 6, 3, RGB(0x4D, 0xD0, 0xE1));
    gfx_flush();
}

void kmain(u32 magic, void *mbi){
    (void)magic;
    apps_init();
    kernel_hw_init(mbi);
    splash("Starting kernel...", 1, 5);   wait_ticks(15);
    wallpaper_make(0);
    splash("Initializing memory", 2, 5);  wait_ticks(15);
    fs_init();
    disk_init();
    int restored = fs_load();
    splash(restored ? "Disk found: files restored" : disk_state == DISK_NONE ? "No disk found: files will not persist" :
           disk_state == DISK_FOREIGN ? "Disk has unknown data: left untouched" : "New disk: will be initialised", 3, 5);
    wait_ticks(restored ? 25 : 40);
    splash("Loading drivers", 4, 5);      wait_ticks(15);
    splash("Starting desktop", 5, 5);     wait_ticks(20);
    event_t e;
    while (event_poll(&e)) ;
    rtc_read(&now);
    wm_open(APP_TERM, 0);
    u32 last_tick = ticks, last_sec = ticks / 100;
    cursor_move(mx, my);
    for (;;) {
        while (event_poll(&e)) {
            switch (e.type) {
            case EV_MOVE: cursor_move(e.x, e.y); on_move(e.x, e.y, e.btn); break;
            case EV_DOWN: cursor_move(e.x, e.y); on_down(e.x, e.y, e.btn); break;
            case EV_UP:   on_up(e.x, e.y, e.btn); break;
            case EV_KEY:  on_key(e.key, e.mods); break;
            }
        }
        int hs = hover_sig();
        if (hs != last_hover) { last_hover = hs; dirty = 1; }
        if (ticks != last_tick) {
            last_tick = ticks;
            for (int i = 0; i < MAX_WIN; i++)
                if (wins[i].used && app_table[wins[i].app].tick && app_table[wins[i].app].tick(&wins[i])) dirty = 1;
            fs_sync(0);
            if (ticks / 100 != last_sec) {
                last_sec = ticks / 100;
                int om = now.mi; rtc_read(&now);
                if (now.mi != om) dirty = 1;
            }
        }
        if (dirty) { dirty = 0; redraw(); }
        __asm__ volatile("cli");
        if (!event_pending() && ticks == last_tick) __asm__ volatile("sti; hlt");
        else __asm__ volatile("sti");
    }
}
