/* Nova Claude OS apps: Calculator, Paint, Snake, System Monitor, Settings. */
#include "os.h"

app_def_t app_table[APP_COUNT];
extern app_def_t app_term_def, app_note_def, app_files_def;
extern app_def_t app_calc_def, app_paint_def, app_snake_def, app_sysmon_def, app_settings_def;
extern void fmt_uptime(char *o, u32 tk);

/* ====================================================================== */
/* Calculator                                                             */
/* ====================================================================== */
typedef struct { char disp[32]; double acc; char op; int fresh, err; char expr[48]; } calc_t;
static const char *calc_lbl[5][4] = { { "C", "+/-", "%", "/" }, { "7", "8", "9", "*" }, { "4", "5", "6", "-" }, { "1", "2", "3", "+" }, { "0", ".", "<", "=" } };

static double parse_d(const char *s){
    int neg = 0; double v = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    if (*s == '.') { s++; double f = 0.1; while (*s >= '0' && *s <= '9') { v += (*s++ - '0') * f; f /= 10; } }
    return neg ? -v : v;
}
static void fmt_d(char *o, double v){
    if (v != v) { scopy(o, "Error", 32); return; }
    int neg = v < 0; if (neg) v = -v;
    if (v >= 999999999.0) { scopy(o, "Overflow", 32); return; }
    u32 ip = (u32)v;
    u32 f = (u32)((v - (double)ip) * 1e8 + 0.5);
    if (f >= 100000000u) { ip++; f -= 100000000u; }
    char fr[12] = "";
    if (f) { ksnprintf(fr, sizeof fr, "%08u", f); int n = 8; while (n > 0 && fr[n - 1] == '0') n--; fr[n] = 0; }
    if (fr[0]) ksnprintf(o, 32, "%s%u.%s", neg && (ip || f) ? "-" : "", ip, fr);
    else ksnprintf(o, 32, "%s%u", neg && ip ? "-" : "", ip);
}
static void calc_press(calc_t *c, int k){
    if (c->err && k != 'C') return;
    if (k >= '0' && k <= '9') {
        if (c->fresh || !strcmp(c->disp, "0")) { c->disp[0] = (char)k; c->disp[1] = 0; c->fresh = 0; }
        else if (strlen(c->disp) < 14) { int n = (int)strlen(c->disp); c->disp[n] = (char)k; c->disp[n + 1] = 0; }
    } else if (k == '.') {
        if (c->fresh) { scopy(c->disp, "0.", 32); c->fresh = 0; }
        else { int has = 0; for (char *p = c->disp; *p; p++) if (*p == '.') has = 1; if (!has && strlen(c->disp) < 14) { int n = (int)strlen(c->disp); c->disp[n] = '.'; c->disp[n + 1] = 0; } }
    } else if (k == 'C') { scopy(c->disp, "0", 32); c->acc = 0; c->op = 0; c->fresh = 0; c->err = 0; c->expr[0] = 0; }
    else if (k == '<') { int n = (int)strlen(c->disp); if (c->fresh) return; if (n > 1) c->disp[n - 1] = 0; else scopy(c->disp, "0", 32); if (!strcmp(c->disp, "-")) scopy(c->disp, "0", 32); }
    else if (k == 'N') { if (strcmp(c->disp, "0")) { if (c->disp[0] == '-') memmove(c->disp, c->disp + 1, strlen(c->disp)); else if (strlen(c->disp) < 14) { memmove(c->disp + 1, c->disp, strlen(c->disp) + 1); c->disp[0] = '-'; } } }
    else if (k == '%') { fmt_d(c->disp, parse_d(c->disp) / 100.0); c->fresh = 1; }
    else if (k == '+' || k == '-' || k == '*' || k == '/' || k == '=') {
        double v = parse_d(c->disp);
        if (c->op && !c->fresh) {
            switch (c->op) {
            case '+': c->acc += v; break; case '-': c->acc -= v; break; case '*': c->acc *= v; break;
            case '/': if (v == 0) { c->err = 1; scopy(c->disp, "Error", 32); c->op = 0; c->expr[0] = 0; return; } c->acc /= v; break;
            }
        } else if (!c->op) c->acc = v;
        fmt_d(c->disp, c->acc);
        if (!strcmp(c->disp, "Overflow")) { c->err = 1; c->op = 0; c->expr[0] = 0; return; }
        if (k == '=') { c->op = 0; c->expr[0] = 0; }
        else { c->op = (char)k; ksnprintf(c->expr, sizeof c->expr, "%s %c", c->disp, k); }
        c->fresh = 1;
    }
}
static void *calc_create(window_t *w, const char *a){ (void)w; (void)a; calc_t *c = kmalloc(sizeof(calc_t)); if (c) scopy(c->disp, "0", 32); return c; }
static void calc_draw(window_t *w, int cx, int cy, int cw, int ch){
    calc_t *c = w->state; (void)ch;
    rfill(cx + 8, cy + 8, cw - 16, 82, 8, RGB(0x1E, 0x24, 0x30));
    draw_text(cx + 18, cy + 16, c->expr, RGB(0x8A, 0x94, 0xA6));
    int tw = (int)strlen(c->disp) * 16;
    draw_text_s(cx + cw - 20 - tw, cy + 44, c->disp, c->err ? RGB(0xFF, 0x7B, 0x72) : C_WHITE, 2);
    for (int r = 0; r < 5; r++) for (int k = 0; k < 4; k++) {
        const char *l = calc_lbl[r][k];
        int op = (k == 3), eq = (r == 4 && k == 3), fn = (r == 0 && k < 3);
        int x = cx + 8 + k * 72, y = cy + 100 + r * 60;
        u32 bg = eq ? accent : op ? blend(C_WIN_BG, accent, 90) : fn ? RGB(0xD5, 0xDA, 0xE2) : C_WHITE;
        rfill(x, y, 64, 52, 8, C_BORDER); rfill(x + 1, y + 1, 62, 50, 7, bg);
        draw_text_s(x + 32 - (int)strlen(l) * 8, y + 10, l, eq ? C_WHITE : C_TEXT, 2);
    }
}
static void calc_key(window_t *w, int key, int mods){
    (void)mods; calc_t *c = w->state;
    if (key == '\n') key = '=';
    else if (key == '\b') key = '<';
    else if (key == 27 || key == 'c' || key == 'C') key = 'C';
    if ((key >= '0' && key <= '9') || key == '+' || key == '-' || key == '*' || key == '/' || key == '=' || key == '.' || key == '%' || key == 'C' || key == '<') calc_press(c, key);
}
static int calc_mouse(window_t *w, int type, int lx, int ly, int btn){
    calc_t *c = w->state;
    if (type != EV_DOWN || btn != 1 || ly < 100) return 0;
    int col = (lx - 8) / 72, row = (ly - 100) / 60;
    if (col < 0 || col > 3 || row < 0 || row > 4) return 0;
    if ((lx - 8) % 72 > 64 || (ly - 100) % 60 > 52) return 0;
    const char *l = calc_lbl[row][col];
    int k = l[0];
    if (!strcmp(l, "+/-")) k = 'N';
    calc_press(c, k);
    return 1;
}
app_def_t app_calc_def = { "Calculator", APP_CALC, 296, 400, 0, calc_create, 0, calc_draw, calc_key, calc_mouse, 0 };

/* ====================================================================== */
/* Paint                                                                  */
/* ====================================================================== */
#define PW 608
#define PH 330
#define P_HDR 66
static const u32 pal[16] = { RGB(0,0,0), RGB(255,255,255), RGB(128,128,128), RGB(200,200,200), RGB(229,57,53), RGB(255,138,0), RGB(251,192,45), RGB(139,195,74),
                             RGB(46,160,67), RGB(20,184,166), RGB(77,208,225), RGB(30,136,229), RGB(94,53,177), RGB(216,64,159), RGB(121,85,72), RGB(255,205,178) };
typedef struct { u8 pix[PW * PH]; int color, tool, size, drawing, lx, ly; } paint_t;
static const int p_sizes[4] = { 1, 3, 6, 12 };

static void p_stamp(paint_t *t, int x, int y){
    int sz = p_sizes[t->size], r = sz / 2, col = t->tool == 1 ? 1 : t->color;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        if (dx * dx + dy * dy > r * r + r) continue;
        int px_ = x + dx, py_ = y + dy;
        if (px_ >= 0 && px_ < PW && py_ >= 0 && py_ < PH) t->pix[py_ * PW + px_] = (u8)col;
    }
}
static void p_stroke(paint_t *t, int x0, int y0, int x1, int y1){
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        p_stamp(t, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void p_fill(paint_t *t, int x, int y){
    u8 target = t->pix[y * PW + x], nc = (u8)t->color;
    if (target == nc) return;
    int *stk = kmalloc(PW * PH * sizeof(int));
    if (!stk) return;
    int sp = 0;
    t->pix[y * PW + x] = nc; stk[sp++] = y * PW + x;
    while (sp) {
        int p = stk[--sp], px_ = p % PW, py_ = p / PW;
        int nb[4] = { px_ > 0 ? p - 1 : -1, px_ < PW - 1 ? p + 1 : -1, py_ > 0 ? p - PW : -1, py_ < PH - 1 ? p + PW : -1 };
        for (int k = 0; k < 4; k++) if (nb[k] >= 0 && t->pix[nb[k]] == target) { t->pix[nb[k]] = nc; stk[sp++] = nb[k]; }
    }
    kfree(stk);
}
static void *paint_create(window_t *w, const char *a){
    (void)w; (void)a;
    paint_t *t = kmalloc(sizeof(paint_t));
    if (!t) return 0;
    memset(t->pix, 1, sizeof t->pix); t->color = 0; t->size = 1;
    return t;
}
static void paint_draw(window_t *w, int cx, int cy, int cw, int ch){
    paint_t *t = w->state; (void)cw; (void)ch;
    fill(cx, cy, cw, P_HDR - 2, C_PANEL); hline(cx, cy + P_HDR - 3, cw, C_BORDER);
    for (int i = 0; i < 16; i++) {
        int x = cx + 8 + (i % 8) * 24, y = cy + 6 + (i / 8) * 26;
        rect(x - 1, y - 1, 24, 24, i == t->color ? C_TEXT : C_BORDER);
        fill(x, y, 22, 22, pal[i]);
        if (i == t->color) rect(x, y, 22, 22, i == 0 ? C_WHITE : C_TEXT);
    }
    const char *tl[4] = { "Pen", "Eraser", "Fill", "Clear" };
    for (int i = 0; i < 4; i++) ui_button(cx + 216 + i * 64, cy + 6, 60, 24, tl[i], 0, i < 3 && t->tool == i);
    const char *sl[4] = { "1", "3", "6", "12" };
    draw_text(cx + 216 - 0, cy + 38, "Size", C_MUTED);
    for (int i = 0; i < 4; i++) ui_button(cx + 264 + i * 44, cy + 34, 40, 24, sl[i], 0, t->size == i);
    int ox = cx + 8, oy = cy + P_HDR;
    rect(ox - 1, oy - 1, PW + 2, PH + 2, C_BORDER);
    for (int y = 0; y < PH; y++) for (int x = 0; x < PW; x++) px(ox + x, oy + y, pal[t->pix[y * PW + x]]);
}
static int paint_mouse(window_t *w, int type, int lx, int ly, int btn){
    paint_t *t = w->state;
    int x = lx - 8, y = ly - P_HDR;
    if (type == EV_DOWN && btn == 1) {
        if (ly < P_HDR - 2) {
            for (int i = 0; i < 16; i++) if (in_rect(lx, ly, 8 + (i % 8) * 24, 6 + (i / 8) * 26, 22, 22)) { t->color = i; if (t->tool == 1) t->tool = 0; }
            for (int i = 0; i < 4; i++) if (in_rect(lx, ly, 216 + i * 64, 6, 60, 24)) { if (i == 3) memset(t->pix, 1, sizeof t->pix); else t->tool = i; }
            for (int i = 0; i < 4; i++) if (in_rect(lx, ly, 264 + i * 44, 34, 40, 24)) t->size = i;
            return 1;
        }
        if (x >= 0 && x < PW && y >= 0 && y < PH) {
            if (t->tool == 2) p_fill(t, x, y);
            else { t->drawing = 1; t->lx = x; t->ly = y; p_stamp(t, x, y); }
            return 1;
        }
    } else if (type == EV_MOVE && t->drawing && (btn & 1)) {
        p_stroke(t, t->lx, t->ly, x, y); t->lx = x; t->ly = y; return 1;
    } else if (type == EV_UP) t->drawing = 0;
    return 0;
}
app_def_t app_paint_def = { "Paint", APP_PAINT, 624, 404, 0, paint_create, 0, paint_draw, 0, paint_mouse, 0 };

/* ====================================================================== */
/* Snake                                                                  */
/* ====================================================================== */
#define GW 30
#define GH 20
#define CS 16
typedef struct { int x[GW * GH], y[GW * GH]; int len, dir, ndir, fx, fy, score, best, over, started, paused; u32 last; } snake_t;
static const int sdx[4] = { 0, 1, 0, -1 }, sdy[4] = { -1, 0, 1, 0 };

static void sn_food(snake_t *s){
    for (int tries = 0; tries < 1000; tries++) {
        int fx = krand() % GW, fy = krand() % GH, ok = 1;
        for (int i = 0; i < s->len; i++) if (s->x[i] == fx && s->y[i] == fy) { ok = 0; break; }
        if (ok) { s->fx = fx; s->fy = fy; return; }
    }
}
static void sn_reset(snake_t *s){
    int best = s->best;
    memset(s, 0, sizeof *s); s->best = best;
    s->len = 3; s->dir = s->ndir = 1;
    for (int i = 0; i < 3; i++) { s->x[i] = GW / 2 - i; s->y[i] = GH / 2; }
    sn_food(s);
}
static void *snake_create(window_t *w, const char *a){ (void)w; (void)a; snake_t *s = kmalloc(sizeof(snake_t)); if (s) sn_reset(s); return s; }
static void snake_draw(window_t *w, int cx, int cy, int cw, int ch){
    snake_t *s = w->state; (void)ch;
    fill(cx, cy, cw, 32, RGB(0x1B, 0x20, 0x2C));
    char b[64]; ksnprintf(b, sizeof b, "Score %d   Best %d", s->score, s->best);
    draw_text(cx + 12, cy + 8, b, C_WHITE);
    draw_text(cx + cw - 12 - 22 * 8, cy + 8, "Arrows/WASD  P pause", RGB(0x8A, 0x94, 0xA6));
    int ox = cx, oy = cy + 32;
    for (int j = 0; j < GH; j++) for (int i = 0; i < GW; i++)
        fill(ox + i * CS, oy + j * CS, CS, CS, ((i + j) & 1) ? RGB(0x20, 0x27, 0x36) : RGB(0x1C, 0x22, 0x30));
    circle_fill(ox + s->fx * CS + CS / 2, oy + s->fy * CS + CS / 2, 6, RGB(0xE5, 0x39, 0x35));
    fill(ox + s->fx * CS + 9, oy + s->fy * CS + 4, 2, 2, RGB(255, 200, 200));
    for (int i = s->len - 1; i >= 0; i--) {
        u32 c = blend(RGB(0x6E, 0xE7, 0x8F), RGB(0x1F, 0x7A, 0x45), i * 255 / (s->len + 4));
        rfill(ox + s->x[i] * CS + 1, oy + s->y[i] * CS + 1, CS - 2, CS - 2, 4, c);
    }
    int hx = ox + s->x[0] * CS, hy = oy + s->y[0] * CS;
    fill(hx + 4, hy + 4, 3, 3, RGB(20, 20, 20)); fill(hx + 9, hy + 4, 3, 3, RGB(20, 20, 20));
    if (!s->started || s->over || s->paused) {
        fill_a(ox, oy, GW * CS, GH * CS, 0, 140);
        const char *m1 = s->over ? "GAME OVER" : s->paused ? "PAUSED" : "SNAKE";
        const char *m2 = s->over ? "Press Space to play again" : s->paused ? "Press P to resume" : "Press an arrow key to start";
        draw_text_s(ox + (GW * CS - (int)strlen(m1) * 24) / 2, oy + 120, m1, C_WHITE, 3);
        draw_text(ox + (GW * CS - (int)strlen(m2) * 8) / 2, oy + 180, m2, RGB(0xD8, 0xDE, 0xEA));
    }
}
static void snake_key(window_t *w, int key, int mods){
    snake_t *s = w->state; (void)mods;
    int d = -1;
    if (key == K_UP || key == 'w') d = 0; else if (key == K_RIGHT || key == 'd') d = 1;
    else if (key == K_DOWN || key == 's') d = 2; else if (key == K_LEFT || key == 'a') d = 3;
    if (d >= 0) {
        if (s->over) return;
        if (d != ((s->dir + 2) & 3)) s->ndir = d;
        if (!s->started) { s->started = 1; s->last = ticks; }
    } else if (key == 'p') { if (s->started && !s->over) s->paused = !s->paused; }
    else if ((key == ' ' || key == '\n' || key == 'r') && (s->over || key == 'r')) sn_reset(s);
}
static int snake_tick(window_t *w){
    snake_t *s = w->state;
    if (!s->started || s->over || s->paused) return 0;
    u32 interval = 12 - (s->len / 4 > 7 ? 7 : s->len / 4);
    if (ticks - s->last < interval) return 0;
    s->last = ticks;
    s->dir = s->ndir;
    int nx = s->x[0] + sdx[s->dir], ny = s->y[0] + sdy[s->dir];
    int eat = (nx == s->fx && ny == s->fy);
    int hit = nx < 0 || ny < 0 || nx >= GW || ny >= GH;
    for (int i = 0; i < s->len - (eat ? 0 : 1) && !hit; i++) if (s->x[i] == nx && s->y[i] == ny) hit = 1;
    if (hit) { s->over = 1; if (s->score > s->best) s->best = s->score; return 1; }
    if (eat && s->len < GW * GH) s->len++;
    for (int i = s->len - 1; i > 0; i--) { s->x[i] = s->x[i - 1]; s->y[i] = s->y[i - 1]; }
    s->x[0] = nx; s->y[0] = ny;
    if (eat) { s->score += 10; sn_food(s); }
    return 1;
}
app_def_t app_snake_def = { "Snake", APP_SNAKE, GW * CS, GH * CS + 32, 0, snake_create, 0, snake_draw, snake_key, 0, snake_tick };

/* ====================================================================== */
/* System Monitor                                                         */
/* ====================================================================== */
typedef struct { u32 last_redraws; int fps, hist[60], hn; char cpu[64]; } sm_t;
static void *sm_create(window_t *w, const char *a){
    (void)w; (void)a; sm_t *t = kmalloc(sizeof(sm_t));
    if (t) { cpu_brand(t->cpu); t->last_redraws = redraws; }
    return t;
}
static void bar(int x, int y, int w, int pct, u32 c){
    rfill(x, y, w, 12, 5, RGB(0xD5, 0xDA, 0xE2));
    if (pct > 0) rfill(x, y, (w * pct / 100) < 10 ? 10 : w * pct / 100, 12, 5, c);
}
static void sm_draw(window_t *w, int cx, int cy, int cw, int ch){
    sm_t *t = w->state;
    char b[96], up[32];
    draw_text(cx + 16, cy + 12, "Processor", C_MUTED);
    draw_text(cx + 16, cy + 30, t->cpu, C_TEXT);
    draw_text(cx + 16, cy + 56, "Kernel heap", C_MUTED);
    u32 hu = heap_used(), ht = heap_total();
    ksnprintf(b, sizeof b, "%u KB / %u KB", hu / 1024, ht / 1024);
    draw_text(cx + cw - 16 - text_w(b), cy + 56, b, C_TEXT);
    bar(cx + 16, cy + 76, cw - 32, (int)(hu / (ht / 100)), accent);
    fmt_uptime(up, ticks);
    ksnprintf(b, sizeof b, "Uptime      %s", up); draw_text(cx + 16, cy + 100, b, C_TEXT);
    ksnprintf(b, sizeof b, "Display     %dx%d", SW, SH); draw_text(cx + 16, cy + 118, b, C_TEXT);
    ksnprintf(b, sizeof b, "RAM         %u MB", mem_total_kb() / 1024); draw_text(cx + 270, cy + 100, b, C_TEXT);
    ksnprintf(b, sizeof b, "Files       %d / %d", fs_count(), FS_MAX); draw_text(cx + 270, cy + 118, b, C_TEXT);
    { char d[64]; disk_info(d); ksnprintf(b, sizeof b, "Disk        %s", d); draw_text(cx + 16, cy + 136, b, C_TEXT); }
    draw_text(cx + 16, cy + 160, "Screen redraws per second", C_MUTED);
    int gx = cx + 16, gy = cy + 180, gw = cw - 32, gh = 52;
    rfill(gx, gy, gw, gh, 4, RGB(0x1E, 0x24, 0x30));
    int mx_ = 10; for (int i = 0; i < 60; i++) if (t->hist[i] > mx_) mx_ = t->hist[i];
    for (int i = 0; i < 60; i++) {
        int idx = (t->hn + i) % 60, h = t->hist[idx] * (gh - 8) / mx_;
        fill(gx + 4 + i * (gw - 8) / 60, gy + gh - 4 - h, (gw - 8) / 60 - 1 > 0 ? (gw - 8) / 60 - 1 : 1, h, RGB(0x4D, 0xD0, 0xE1));
    }
    ksnprintf(b, sizeof b, "%d/s", t->fps); draw_text(cx + cw - 16 - text_w(b), cy + 160, b, C_TEXT);
    draw_text(cx + 16, cy + 244, "Running windows", C_MUTED);
    int y = cy + 266, n = 0;
    for (int i = 0; i < MAX_WIN && y + 24 < cy + ch; i++) if (wins[i].used) {
        fill(cx + 12, y, cw - 24, 24, (n & 1) ? C_WIN_BG : RGB(0xEC, 0xEF, 0xF4));
        icon_draw(app_table[wins[i].app].icon, cx + 18, y + 4, 1);
        ksnprintf(b, sizeof b, "%-3d %s", i + 1, wins[i].title);
        gfx_clip(cx + 12, y, cw - 110, 24); draw_text(cx + 42, y + 4, b, C_TEXT); gfx_clip(cx, cy, cw, ch);
        ui_button(cx + cw - 92, y + 1, 72, 22, "End task", 0, 0);
        y += 24; n++;
    }
}
static int sm_mouse(window_t *w, int type, int lx, int ly, int btn){
    if (type != EV_DOWN || btn != 1) return 0;
    int cw = w->w - 2, y = 266;
    for (int i = 0; i < MAX_WIN; i++) if (wins[i].used) {
        if (in_rect(lx, ly, cw - 92, y + 1, 72, 22)) { wm_close(i); return 1; }
        y += 24;
    }
    return 0;
}
static int sm_tick(window_t *w){
    sm_t *t = w->state;
    if (ticks % 100) return 0;
    t->fps = (int)(redraws - t->last_redraws); t->last_redraws = redraws;
    t->hist[t->hn] = t->fps; t->hn = (t->hn + 1) % 60;
    return 1;
}
app_def_t app_sysmon_def = { "System Monitor", APP_SYSMON, 520, 440, 0, sm_create, 0, sm_draw, 0, sm_mouse, sm_tick };

/* ====================================================================== */
/* Settings                                                               */
/* ====================================================================== */
static const u32 accents[8] = { RGB(0x2D,0x7D,0xD2), RGB(0x14,0xA3,0x95), RGB(0x2E,0xA0,0x43), RGB(0xE0,0x82,0x00), RGB(0xE5,0x48,0x4D), RGB(0xD6,0x40,0x9F), RGB(0x7C,0x5C,0xFA), RGB(0x4B,0x55,0x63) };
static void *set_create(window_t *w, const char *a){ (void)w; (void)a; return kmalloc(16); }
static void set_draw(window_t *w, int cx, int cy, int cw, int ch){
    (void)w; (void)ch;
    draw_text_s(cx + 16, cy + 12, "Personalize", C_TEXT, 2);
    draw_text(cx + 16, cy + 52, "Wallpaper", C_MUTED);
    for (int i = 0; i < 4; i++) {
        int x = cx + 16 + i * 104, y = cy + 74;
        rfill(x - 2, y - 2, 100, 64, 6, i == wallpaper_id ? accent : C_BORDER);
        vgrad(x, y, 96, 60, wp_stops[i][0], wp_stops[i][2]);
        fill_a(x, y + 18, 96, 24, wp_stops[i][1], 150);
    }
    draw_text(cx + 16, cy + 164, "Accent color", C_MUTED);
    for (int i = 0; i < 8; i++) {
        int x = cx + 16 + i * 52, y = cy + 186;
        rfill(x - 2, y - 2, 48, 40, 8, accents[i] == accent ? C_TEXT : C_WIN_BG);
        rfill(x, y, 44, 36, 7, accents[i]);
    }
    rfill(cx + 16, cy + 250, cw - 32, 124, 8, C_WHITE); rect(cx + 16, cy + 250, cw - 32, 124, C_BORDER);
    icon_draw(APP_SETTINGS, cx + 30, cy + 264, 2);
    draw_text_s(cx + 76, cy + 262, OS_NAME, C_TEXT, 2);
    char b[80];
    ksnprintf(b, sizeof b, "Version %s  (built %s)", OS_VER, __DATE__); draw_text(cx + 76, cy + 296, b, C_MUTED);
    draw_text(cx + 30, cy + 322, "32-bit protected-mode kernel, GRUB multiboot", C_TEXT);
    ksnprintf(b, sizeof b, "Display %dx%d   RAM %u MB", SW, SH, mem_total_kb() / 1024); draw_text(cx + 30, cy + 342, b, C_TEXT);
}
static int set_mouse(window_t *w, int type, int lx, int ly, int btn){
    (void)w;
    if (type != EV_DOWN || btn != 1) return 0;
    for (int i = 0; i < 4; i++) if (in_rect(lx, ly, 16 + i * 104, 74, 96, 60)) { wallpaper_make(i); return 1; }
    for (int i = 0; i < 8; i++) if (in_rect(lx, ly, 16 + i * 52, 186, 44, 36)) { accent = accents[i]; return 1; }
    return 0;
}
app_def_t app_settings_def = { "Settings", APP_SETTINGS, 440, 390, 0, set_create, 0, set_draw, 0, set_mouse, 0 };

void apps_init(void){
    app_table[APP_TERM] = app_term_def;   app_table[APP_FILES] = app_files_def;
    app_table[APP_NOTE] = app_note_def;   app_table[APP_CALC] = app_calc_def;
    app_table[APP_PAINT] = app_paint_def; app_table[APP_SNAKE] = app_snake_def;
    app_table[APP_SYSMON] = app_sysmon_def; app_table[APP_SETTINGS] = app_settings_def;
}
