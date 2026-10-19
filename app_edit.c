/* Nova Claude OS Notepad (text editor) and File Manager. */
#include "os.h"

/* ====================================================================== */
/* Notepad                                                                */
/* ====================================================================== */
#define NP_CAP 16384
#define NP_TB 34
#define NP_SB 22
#define NP_GUT 44
typedef struct {
    char buf[NP_CAP]; int len, cur, top, left;
    char path[128]; int has_path, modified;
    int saveas; char sa[128]; int sal;
    char msg[64]; u32 msg_t; int blink;
} np_t;

static void np_dims(window_t *w, int *rows, int *cols){
    int ch = w->h - TITLE_H - 1, cw = w->w - 2;
    *rows = (ch - NP_TB - NP_SB) / 16; if (*rows < 1) *rows = 1;
    *cols = (cw - NP_GUT - 8) / 8; if (*cols < 1) *cols = 1;
}
static int np_line_start(np_t *t, int line){
    int i = 0;
    while (line > 0 && i < t->len) { if (t->buf[i] == '\n') line--; i++; }
    return i;
}
static void np_rc(np_t *t, int pos, int *r, int *c){
    int rr = 0, ls = 0;
    for (int i = 0; i < pos; i++) if (t->buf[i] == '\n') { rr++; ls = i + 1; }
    *r = rr; *c = pos - ls;
}
static int np_pos(np_t *t, int r, int c){
    int s = np_line_start(t, r), e = s;
    while (e < t->len && t->buf[e] != '\n') e++;
    return s + (c < e - s ? c : e - s);
}
static int np_nlines(np_t *t){ int n = 1; for (int i = 0; i < t->len; i++) if (t->buf[i] == '\n') n++; return n; }
static void np_title(window_t *w, np_t *t){
    char b[48]; const char *n = t->has_path ? t->path : "Untitled";
    const char *sl = n; for (const char *q = n; *q; q++) if (*q == '/') sl = q + 1;
    ksnprintf(b, sizeof b, "Notepad - %s%s", sl, t->modified ? " *" : "");
    wm_set_title(w, b);
}
static void np_visible(window_t *w, np_t *t){
    int rows, cols, r, c; np_dims(w, &rows, &cols); np_rc(t, t->cur, &r, &c);
    if (r < t->top) t->top = r;
    if (r >= t->top + rows) t->top = r - rows + 1;
    if (c < t->left) t->left = c;
    if (c >= t->left + cols) t->left = c - cols + 1;
    if (t->left < 0) t->left = 0;
}
static void np_msg(np_t *t, const char *m){ scopy(t->msg, m, 64); t->msg_t = ticks; }
static void np_save(window_t *w, np_t *t){
    if (!t->has_path) { t->saveas = 1; scopy(t->sa, "/home/untitled.txt", 128); t->sal = (int)strlen(t->sa); return; }
    int id = fs_resolve(t->path, 0);
    if (id < 0) id = fs_create(t->path, 0, 0);
    if (id < 0 || fs_nodes[id].dir || fs_write(id, t->buf, t->len) < 0) { np_msg(t, "Cannot save: invalid path"); return; }
    t->modified = 0; np_msg(t, "Saved"); np_title(w, t);
}
static void np_ins(window_t *w, np_t *t, char c){
    if (t->len >= NP_CAP - 1) return;
    memmove(t->buf + t->cur + 1, t->buf + t->cur, t->len - t->cur);
    t->buf[t->cur++] = c; t->len++;
    if (!t->modified) { t->modified = 1; np_title(w, t); }
}
static void np_del(window_t *w, np_t *t, int at){
    if (at < 0 || at >= t->len) return;
    memmove(t->buf + at, t->buf + at + 1, t->len - at - 1);
    t->len--;
    if (!t->modified) { t->modified = 1; np_title(w, t); }
}
static void *np_create(window_t *w, const char *arg){
    np_t *t = kmalloc(sizeof(np_t));
    if (!t) return 0;
    if (arg && *arg) {
        scopy(t->path, arg, 128); t->has_path = 1;
        int id = fs_resolve(arg, 0);
        if (id >= 0 && !fs_nodes[id].dir) {
            int n = (int)fs_nodes[id].size; if (n > NP_CAP - 1) { n = NP_CAP - 1; np_msg(t, "File truncated to 16 KB"); }
            memcpy(t->buf, fs_nodes[id].data, n); t->len = n;
        } else np_msg(t, "New file");
    }
    np_title(w, t);
    return t;
}
static void np_draw(window_t *w, int cx, int cy, int cw, int ch){
    np_t *t = w->state;
    int rows, cols; np_dims(w, &rows, &cols);
    fill(cx, cy, cw, NP_TB, C_PANEL); hline(cx, cy + NP_TB - 1, cw, C_BORDER);
    if (t->saveas) {
        draw_text(cx + 8, cy + 9, "Save as:", C_TEXT);
        int ix = cx + 80, iw = cw - 80 - 140;
        rfill(ix, cy + 4, iw, 26, 4, C_BORDER); rfill(ix + 1, cy + 5, iw - 2, 24, 3, C_WHITE);
        gfx_clip(ix + 2, cy + 4, iw - 4, 26);
        int shift = t->sal * 8 > iw - 16 ? t->sal * 8 - (iw - 16) : 0;
        draw_text(ix + 6 - shift, cy + 9, t->sa, C_TEXT);
        if (t->blink) fill(ix + 6 - shift + t->sal * 8, cy + 8, 2, 18, C_TEXT);
        gfx_clip(cx, cy, cw, ch);
        ui_button(cx + cw - 128, cy + 4, 56, 26, "Save", 0, 1);
        ui_button(cx + cw - 68, cy + 4, 60, 26, "Cancel", 0, 0);
    } else {
        ui_button(cx + 8, cy + 4, 52, 26, "New", 0, 0);
        ui_button(cx + 64, cy + 4, 52, 26, "Save", 0, 1);
        ui_button(cx + 120, cy + 4, 76, 26, "Save As", 0, 0);
        const char *hint = "Ctrl+S to save";
        draw_text(cx + cw - text_w(hint) - 10, cy + 9, hint, C_MUTED);
    }
    int ty = cy + NP_TB, th = ch - NP_TB - NP_SB;
    fill(cx, ty, cw, th, C_WHITE);
    fill(cx, ty, NP_GUT, th, RGB(0xEE, 0xF0, 0xF4)); vline(cx + NP_GUT - 1, ty, th, C_BORDER);
    gfx_clip(cx, ty, cw, th);
    int cr, cc; np_rc(t, t->cur, &cr, &cc);
    int pos = np_line_start(t, t->top);
    for (int r = 0; r < rows; r++) {
        int line = t->top + r, y = ty + r * 16;
        if (pos > t->len) break;
        if (line == cr) fill(cx + NP_GUT, y, cw - NP_GUT, 16, RGB(0xF1, 0xF6, 0xFD));
        char ln[12]; ksnprintf(ln, sizeof ln, "%4d", line + 1);
        draw_text(cx + 4, y, ln, line == cr ? C_TEXT : C_MUTED);
        int e = pos; while (e < t->len && t->buf[e] != '\n') e++;
        char seg[200]; int n = 0;
        for (int i = pos + t->left; i < e && n < cols && n < 199; i++) seg[n++] = t->buf[i];
        seg[n] = 0;
        gfx_clip(cx + NP_GUT, ty, cw - NP_GUT, th);
        draw_text(cx + NP_GUT + 4, y, seg, C_TEXT);
        gfx_clip(cx, ty, cw, th);
        pos = e + 1;
    }
    if (!t->saveas && t->blink && wm_focused() == w->id && cr >= t->top && cr < t->top + rows)
        fill(cx + NP_GUT + 4 + (cc - t->left) * 8, ty + (cr - t->top) * 16, 2, 16, accent);
    gfx_clip(cx, cy, cw, ch);
    int sy = cy + ch - NP_SB;
    fill(cx, sy, cw, NP_SB, C_PANEL); hline(cx, sy, cw, C_BORDER);
    char s[96];
    if (t->msg[0] && ticks - t->msg_t < 300) ksnprintf(s, sizeof s, "%s", t->msg);
    else ksnprintf(s, sizeof s, "Ln %d, Col %d    %d lines    %d bytes", cr + 1, cc + 1, np_nlines(t), t->len);
    draw_text(cx + 8, sy + 3, s, C_MUTED);
    const char *p = t->has_path ? t->path : "Untitled";
    draw_text(cx + cw - text_w(p) - 10, sy + 3, p, C_MUTED);
}
static void np_key(window_t *w, int key, int mods){
    np_t *t = w->state;
    if (t->saveas) {
        if (key == 27) t->saveas = 0;
        else if (key == '\n') { if (t->sal) { scopy(t->path, t->sa, 128); t->has_path = 1; t->saveas = 0; np_save(w, t); } }
        else if (key == '\b') { if (t->sal) t->sa[--t->sal] = 0; }
        else if (key >= 32 && key < 127 && t->sal < 120) { t->sa[t->sal++] = (char)key; t->sa[t->sal] = 0; }
        return;
    }
    int rows, cols, r, c; np_dims(w, &rows, &cols); np_rc(t, t->cur, &r, &c);
    if (mods & M_CTRL) { if (key == 's') np_save(w, t); return; }
    switch (key) {
    case K_LEFT: if (t->cur > 0) t->cur--; break;
    case K_RIGHT: if (t->cur < t->len) t->cur++; break;
    case K_UP: if (r > 0) t->cur = np_pos(t, r - 1, c); break;
    case K_DOWN: t->cur = np_pos(t, r + 1, c); if (r + 1 >= np_nlines(t)) t->cur = np_pos(t, r, 100000); break;
    case K_HOME: t->cur = np_pos(t, r, 0); break;
    case K_END: t->cur = np_pos(t, r, 100000); break;
    case K_PGUP: t->cur = np_pos(t, r > rows ? r - rows : 0, c); break;
    case K_PGDN: { int nr = r + rows; if (nr >= np_nlines(t)) nr = np_nlines(t) - 1; t->cur = np_pos(t, nr, c); break; }
    case K_DEL: np_del(w, t, t->cur); break;
    case '\b': if (t->cur > 0) { np_del(w, t, t->cur - 1); t->cur--; } break;
    case '\n': np_ins(w, t, '\n'); break;
    case '\t': for (int i = 0; i < 4; i++) np_ins(w, t, ' '); break;
    default: if (key >= 32 && key < 127) np_ins(w, t, (char)key); break;
    }
    np_visible(w, t);
}
static int np_mouse(window_t *w, int type, int lx, int ly, int btn){
    np_t *t = w->state;
    if (type != EV_DOWN || btn != 1) return 0;
    int cw = w->w - 2;
    if (ly < NP_TB) {
        if (t->saveas) {
            if (in_rect(lx, ly, cw - 128, 4, 56, 26)) { if (t->sal) { scopy(t->path, t->sa, 128); t->has_path = 1; t->saveas = 0; np_save(w, t); } }
            else if (in_rect(lx, ly, cw - 68, 4, 60, 26)) t->saveas = 0;
        } else {
            if (in_rect(lx, ly, 8, 4, 52, 26)) { t->len = 0; t->cur = 0; t->top = t->left = 0; t->has_path = 0; t->modified = 0; t->path[0] = 0; np_title(w, t); }
            else if (in_rect(lx, ly, 64, 4, 52, 26)) np_save(w, t);
            else if (in_rect(lx, ly, 120, 4, 76, 26)) { t->saveas = 1; scopy(t->sa, t->has_path ? t->path : "/home/untitled.txt", 128); t->sal = (int)strlen(t->sa); }
        }
        return 1;
    }
    int rows, cols; np_dims(w, &rows, &cols);
    if (ly >= NP_TB && ly < NP_TB + rows * 16 && lx >= NP_GUT) {
        int r = t->top + (ly - NP_TB) / 16, c = t->left + (lx - NP_GUT - 4 + 4) / 8;
        if (r >= np_nlines(t)) r = np_nlines(t) - 1;
        t->cur = np_pos(t, r, c < 0 ? 0 : c);
    }
    return 1;
}
static int np_tick(window_t *w){
    np_t *t = w->state; int b = (ticks / 50) & 1;
    if (b != t->blink) { t->blink = b; return wm_focused() == w->id; }
    if (t->msg[0] && ticks - t->msg_t == 300) return 1;
    return 0;
}
app_def_t app_note_def = { "Notepad", APP_NOTE, 640, 420, 1, np_create, 0, np_draw, np_key, np_mouse, np_tick };

/* ====================================================================== */
/* File Manager                                                           */
/* ====================================================================== */
#define FM_T1 36
#define FM_T2 36
#define FM_HDR 22
#define FM_ROW 24
#define FM_TOP (FM_T1 + FM_T2 + FM_HDR)
#define FM_SB 24
typedef struct {
    int dir, ent[FS_MAX], n, sel, selid, scroll, renaming;
    char rn[FS_NAME]; int rnl;
    u32 lastclick; int lastidx;
    char msg[64]; u32 msg_t; int blink;
} fm_t;

static void fm_title(window_t *w, fm_t *t){
    char p[128], b[48]; fs_path(t->dir, p); ksnprintf(b, sizeof b, "Files - %s", p); wm_set_title(w, b);
}
static void fm_refresh(fm_t *t){
    t->n = fs_list(t->dir, t->ent);
    t->sel = -1;
    for (int i = 0; i < t->n; i++) if (t->ent[i] == t->selid) t->sel = i;
    if (t->sel < 0) t->selid = -1;
}
static void fm_msg(fm_t *t, const char *m){ scopy(t->msg, m, 64); t->msg_t = ticks; }
static void fm_goto(window_t *w, fm_t *t, int dir){
    t->dir = dir; t->selid = -1; t->scroll = 0; t->renaming = 0; fm_refresh(t); fm_title(w, t);
}
static void fm_open(window_t *w, fm_t *t, int idx){
    int id = t->ent[idx];
    if (fs_nodes[id].dir) fm_goto(w, t, id);
    else { char p[128]; fs_path(id, p); wm_open(APP_NOTE, p); }
}
static void fm_visible(window_t *w, fm_t *t){
    int rows = (w->h - TITLE_H - 1 - FM_TOP - FM_SB) / FM_ROW;
    if (rows < 1) rows = 1;
    if (t->sel >= 0) { if (t->sel < t->scroll) t->scroll = t->sel; if (t->sel >= t->scroll + rows) t->scroll = t->sel - rows + 1; }
}
static void fm_begin_rename(fm_t *t){
    if (t->sel < 0) return;
    scopy(t->rn, fs_nodes[t->ent[t->sel]].name, FS_NAME); t->rnl = (int)strlen(t->rn); t->renaming = 1;
}
static void fm_new(window_t *w, fm_t *t, int isdir){
    char name[FS_NAME];
    const char *base = isdir ? "New Folder" : "New File.txt";
    scopy(name, base, FS_NAME);
    for (int k = 2; fs_child(t->dir, name) >= 0 && k < 100; k++) ksnprintf(name, FS_NAME, isdir ? "New Folder %d" : "New File %d.txt", k);
    int id = fs_create(name, t->dir, isdir);
    if (id < 0) { fm_msg(t, "Cannot create (disk full?)"); return; }
    t->selid = id; fm_refresh(t); fm_visible(w, t); fm_begin_rename(t);
}
static void fm_delete(fm_t *t){
    if (t->sel < 0) return;
    int r = fs_remove(t->ent[t->sel]);
    if (r == -2) fm_msg(t, "Folder is not empty"); else if (r < 0) fm_msg(t, "Cannot delete");
    t->selid = -1; fm_refresh(t);
}
static void fm_commit(fm_t *t){
    t->renaming = 0;
    if (t->sel < 0 || !t->rnl) return;
    int r = fs_rename(t->ent[t->sel], t->rn);
    if (r == -2) fm_msg(t, "A file with that name already exists"); else if (r < 0) fm_msg(t, "Invalid name");
    fm_refresh(t);
}
static void *fm_create(window_t *w, const char *arg){
    fm_t *t = kmalloc(sizeof(fm_t));
    if (!t) return 0;
    t->selid = -1; t->lastidx = -1;
    int d = 0;
    if (arg && *arg) {
        int id = fs_resolve(arg, 0);
        if (id >= 0) { if (fs_nodes[id].dir) d = id; else { d = fs_nodes[id].parent; t->selid = id; } }
    } else d = fs_resolve("/home", 0);
    t->dir = d; fm_refresh(t); fm_title(w, t);
    return t;
}
static void fm_draw(window_t *w, int cx, int cy, int cw, int ch){
    fm_t *t = w->state;
    if (!t->renaming) fm_refresh(t);
    fill(cx, cy, cw, FM_T1 + FM_T2, C_PANEL); hline(cx, cy + FM_T1 + FM_T2 - 1, cw, C_BORDER);
    ui_button(cx + 8, cy + 5, 60, 26, "  Up", 0, 0);
    icon_draw(ICON_UP, cx + 14, cy + 10, 1);
    char path[128]; fs_path(t->dir, path);
    rfill(cx + 76, cy + 5, cw - 84, 26, 4, C_BORDER); rfill(cx + 77, cy + 6, cw - 86, 24, 3, C_WHITE);
    draw_text(cx + 86, cy + 10, path, C_TEXT);
    ui_button(cx + 8, cy + FM_T1 + 5, 104, 26, "New Folder", 0, 1);
    ui_button(cx + 118, cy + FM_T1 + 5, 92, 26, "New File", 0, 0);
    ui_button(cx + 216, cy + FM_T1 + 5, 80, 26, "Rename", 0, 0);
    ui_button(cx + 302, cy + FM_T1 + 5, 76, 26, "Delete", 0, 0);
    int hy = cy + FM_T1 + FM_T2;
    fill(cx, hy, cw, FM_HDR, RGB(0xEE, 0xF0, 0xF4)); hline(cx, hy + FM_HDR - 1, cw, C_BORDER);
    draw_text(cx + 34, hy + 3, "Name", C_MUTED); draw_text(cx + cw - 190, hy + 3, "Size", C_MUTED); draw_text(cx + cw - 100, hy + 3, "Type", C_MUTED);
    int ly = cy + FM_TOP, lh = ch - FM_TOP - FM_SB;
    fill(cx, ly, cw, lh, C_WHITE);
    gfx_clip(cx, ly, cw, lh);
    int rows = lh / FM_ROW + 1;
    for (int r = 0; r < rows; r++) {
        int i = t->scroll + r; if (i >= t->n) break;
        fsnode_t *f = &fs_nodes[t->ent[i]];
        int y = ly + r * FM_ROW;
        if (i == t->sel) fill(cx, y, cw, FM_ROW, blend(C_WHITE, accent, 70));
        icon_draw(f->dir ? ICON_FOLDER : ICON_FILE, cx + 10, y + 4, 1);
        if (t->renaming && i == t->sel) {
            rect(cx + 32, y + 1, 220, FM_ROW - 2, accent); fill(cx + 33, y + 2, 218, FM_ROW - 4, C_WHITE);
            draw_text(cx + 36, y + 4, t->rn, C_TEXT);
            if (t->blink) fill(cx + 36 + t->rnl * 8, y + 4, 2, 16, C_TEXT);
        } else draw_text(cx + 34, y + 4, f->name, C_TEXT);
        char s[24];
        if (f->dir) scopy(s, "--", 24); else if (f->size < 1024) ksnprintf(s, 24, "%u B", f->size); else ksnprintf(s, 24, "%u KB", f->size / 1024);
        draw_text(cx + cw - 190, y + 4, s, C_MUTED);
        draw_text(cx + cw - 100, y + 4, f->dir ? "Folder" : "Text file", C_MUTED);
    }
    if (!t->n) draw_text(cx + 34, ly + 8, "This folder is empty.", C_MUTED);
    gfx_clip(cx, cy, cw, ch);
    int sy = cy + ch - FM_SB;
    fill(cx, sy, cw, FM_SB, C_PANEL); hline(cx, sy, cw, C_BORDER);
    char s[96];
    if (t->msg[0] && ticks - t->msg_t < 300) ksnprintf(s, sizeof s, "%s", t->msg);
    else ksnprintf(s, sizeof s, "%d item%s", t->n, t->n == 1 ? "" : "s");
    draw_text(cx + 8, sy + 4, s, C_MUTED);
}
static void fm_key(window_t *w, int key, int mods){
    fm_t *t = w->state; (void)mods;
    if (t->renaming) {
        if (key == 27) t->renaming = 0;
        else if (key == '\n') fm_commit(t);
        else if (key == '\b') { if (t->rnl) t->rn[--t->rnl] = 0; }
        else if (key >= 32 && key < 127 && key != '/' && t->rnl < FS_NAME - 1) { t->rn[t->rnl++] = (char)key; t->rn[t->rnl] = 0; }
        return;
    }
    switch (key) {
    case K_UP: if (t->sel > 0) t->sel--; else if (t->sel < 0 && t->n) t->sel = t->n - 1; break;
    case K_DOWN: if (t->sel < t->n - 1) t->sel++; break;
    case '\n': if (t->sel >= 0) { fm_open(w, t, t->sel); return; } break;
    case '\b': if (t->dir) fm_goto(w, t, fs_nodes[t->dir].parent); return;
    case K_DEL: fm_delete(t); return;
    case K_F2: fm_begin_rename(t); return;
    }
    if (t->sel >= 0) t->selid = t->ent[t->sel];
    fm_visible(w, t);
}
static int fm_mouse(window_t *w, int type, int lx, int ly, int btn){
    fm_t *t = w->state;
    if (type != EV_DOWN || btn != 1) return 0;
    int cw = w->w - 2;
    if (t->renaming) fm_commit(t);
    if (ly < FM_T1) {
        if (in_rect(lx, ly, 8, 5, 60, 26) && t->dir) fm_goto(w, t, fs_nodes[t->dir].parent);
        return 1;
    }
    if (ly < FM_T1 + FM_T2) {
        int by = FM_T1 + 5;
        if (in_rect(lx, ly, 8, by, 104, 26)) fm_new(w, t, 1);
        else if (in_rect(lx, ly, 118, by, 92, 26)) fm_new(w, t, 0);
        else if (in_rect(lx, ly, 216, by, 80, 26)) fm_begin_rename(t);
        else if (in_rect(lx, ly, 302, by, 76, 26)) fm_delete(t);
        return 1;
    }
    (void)cw;
    if (ly >= FM_TOP) {
        int i = t->scroll + (ly - FM_TOP) / FM_ROW;
        if (i < t->n) {
            if (i == t->lastidx && ticks - t->lastclick < 50) { fm_open(w, t, i); t->lastidx = -1; return 1; }
            t->sel = i; t->selid = t->ent[i]; t->lastidx = i; t->lastclick = ticks;
        } else { t->sel = -1; t->selid = -1; t->lastidx = -1; }
    }
    return 1;
}
static int fm_tick(window_t *w){
    fm_t *t = w->state; int b = (ticks / 50) & 1;
    if (b != t->blink) { t->blink = b; return t->renaming && wm_focused() == w->id; }
    if (t->msg[0] && ticks - t->msg_t == 300) return 1;
    return 0;
}
app_def_t app_files_def = { "File Manager", APP_FILES, 600, 420, 1, fm_create, 0, fm_draw, fm_key, fm_mouse, fm_tick };
