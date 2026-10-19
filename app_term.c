/* Nova Claude OS Terminal: shell with filesystem, process and system commands. */
#include "os.h"

#define TL 300
#define TC 100
#define TWRAP 96
typedef struct {
    char lines[TL][TC]; u32 col[TL]; int nl, scroll;
    char in[128]; int inlen;
    char hist[10][128]; int hn, hpos;
    int cwd, blink, quit;
} term_t;

#define C_OUT   RGB(0xD8, 0xDE, 0xEA)
#define C_ERR   RGB(0xFF, 0x7B, 0x72)
#define C_DIR   RGB(0x6C, 0xB6, 0xFF)
#define C_OK    RGB(0x7E, 0xE7, 0x87)
#define C_INFO  RGB(0x79, 0xD6, 0xE8)

void fmt_uptime(char *o, u32 tk){
    u32 s = tk / 100, h = s / 3600, m = (s / 60) % 60;
    if (h) ksnprintf(o, 32, "%uh %02um %02us", h, m, s % 60);
    else if (m) ksnprintf(o, 32, "%um %02us", m, s % 60);
    else ksnprintf(o, 32, "%us", s);
}

static void addline(term_t *t, u32 c, const char *s){
    int i = t->nl % TL;
    scopy(t->lines[i], s, TC); t->col[i] = c; t->nl++;
}
static void tp(term_t *t, u32 c, const char *s){
    char seg[TC];
    int n = 0;
    for (;; s++) {
        if (*s == '\n' || !*s || n >= TWRAP) {
            seg[n] = 0; addline(t, c, seg); n = 0;
            if (!*s) break;
            if (*s == '\n') continue;
        }
        if (*s == '\t') { for (int k = 0; k < 4 && n < TWRAP; k++) seg[n++] = ' '; continue; }
        seg[n++] = *s;
    }
}
#define TP(t, c, ...) do { char _b[300]; ksnprintf(_b, sizeof _b, __VA_ARGS__); tp(t, c, _b); } while (0)

static void abs_path(term_t *t, const char *p, char *out){
    if (p[0] == '/') { scopy(out, p, 128); return; }
    char cw[128]; fs_path(t->cwd, cw);
    if (!strcmp(cw, "/")) ksnprintf(out, 128, "/%s", p); else ksnprintf(out, 128, "%s/%s", cw, p);
}
static void prompt_parts(term_t *t, char *path){ fs_path(t->cwd, path); }

static void split_path(term_t *t, const char *path, int *par, char *leaf){
    const char *sl = 0; char pp[128];
    for (const char *q = path; *q; q++) if (*q == '/') sl = q;
    if (!sl) { *par = t->cwd; scopy(leaf, path, FS_NAME); return; }
    int n = (int)(sl - path); if (n > 126) n = 126;
    memcpy(pp, path, n); pp[n] = 0;
    if (!n) { pp[0] = '/'; pp[1] = 0; }
    *par = fs_resolve(pp, t->cwd);
    scopy(leaf, sl + 1, FS_NAME);
}
static int rm_rec(int id){
    if (fs_nodes[id].dir) for (int i = 1; i < FS_MAX; i++) if (fs_nodes[i].used && fs_nodes[i].parent == id) rm_rec(i);
    return fs_remove(id);
}

static const char *help_lines[] = {
    "Nova Claude OS shell commands", "",
    "  help               show this help",
    "  ls [path]          list directory        cd [path]   change directory",
    "  pwd                print directory       cat <file>  show file contents",
    "  mkdir <dir>        make directory        touch <f>   create empty file",
    "  rm [-r] <path>     remove                cp <a> <b>  copy file",
    "  mv <a> <b>         move / rename         echo <txt> [> file | >> file]",
    "  edit <file>        open in Notepad       open <file|app>",
    "  run <app>          apps: term files notepad calc paint snake sysmon settings",
    "  ps / kill <pid>    list / end windows    history     command history",
    "  date, uptime       time info             mem, df     memory / disk usage",
    "  neofetch, cpuinfo  system information    ver, uname  version",
    "  wallpaper <0-3>    change wallpaper      beep [hz ms]",
    "  clear              clear screen          exit        close terminal",
    "  disk, sync         disk status / save    format yes  initialise attached disk",
    "  reboot, shutdown   power control         panic       crash the kernel (demo)",
    "", "Tab completes names, Up/Down browse history, PgUp/PgDn scroll.", 0 };

static void cmd_ls(term_t *t, const char *path){
    int d = path ? fs_resolve(path, t->cwd) : t->cwd;
    if (d < 0) { TP(t, C_ERR, "ls: cannot access '%s': no such file or directory", path); return; }
    if (!fs_nodes[d].dir) { TP(t, C_OUT, "%7u  %s", fs_nodes[d].size, fs_nodes[d].name); return; }
    int ids[FS_MAX]; int n = fs_list(d, ids);
    if (!n) { tp(t, C_MUTED, "(empty)"); return; }
    for (int i = 0; i < n; i++) {
        fsnode_t *f = &fs_nodes[ids[i]];
        if (f->dir) TP(t, C_DIR, "  <DIR>  %s/", f->name); else TP(t, C_OUT, "%7u  %s", f->size, f->name);
    }
}
static void cmd_neofetch(term_t *t){
    char cpu[64], up[32]; cpu_brand(cpu); fmt_uptime(up, ticks);
    u32 ram = mem_total_kb() / 1024;
    TP(t, accent | 0x404040, "  ##     ##   user@nova");
    TP(t, accent | 0x404040, "  ###    ##   -----------");
    TP(t, accent | 0x404040, "  ## ##  ##   OS: %s %s (x86, protected mode)", OS_NAME, OS_VER);
    TP(t, accent | 0x404040, "  ##  ## ##   Uptime: %s", up);
    TP(t, accent | 0x404040, "  ##   ###   Shell: nsh");
    TP(t, accent | 0x404040, "  ##    ##   Resolution: %dx%d", SW, SH);
    TP(t, accent | 0x404040, "              CPU: %s", cpu);
    TP(t, accent | 0x404040, "              Memory: %u MB total, heap %u/%u KB", ram, heap_used() / 1024, heap_total() / 1024);
}

static void exec(window_t *w, term_t *t, const char *line){
    while (*line == ' ') line++;
    if (!*line) return;
    if (!t->hn || strcmp(t->hist[(t->hn - 1) % 10], line)) { scopy(t->hist[t->hn % 10], line, 128); t->hn++; }
    t->hpos = t->hn;
    char buf[128]; scopy(buf, line, 128);
    char *argv[10]; int argc = 0;
    for (char *p = buf; *p;) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        if (argc < 10) argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    const char *rest = line; while (*rest && *rest != ' ') rest++; while (*rest == ' ') rest++;
    const char *c = argv[0];

    if (!strcmp(c, "help")) { for (int i = 0; help_lines[i]; i++) tp(t, i ? C_OUT : C_INFO, help_lines[i]); }
    else if (!strcmp(c, "clear") || !strcmp(c, "cls")) { t->nl = 0; }
    else if (!strcmp(c, "echo")) {
        const char *gt = 0; for (const char *q = rest; *q; q++) if (*q == '>') { gt = q; break; }
        if (!gt) tp(t, C_OUT, rest);
        else {
            char txt[128]; int n = (int)(gt - rest); if (n > 126) n = 126;
            memcpy(txt, rest, n); while (n > 0 && txt[n - 1] == ' ') n--; txt[n] = 0;
            int app = (gt[1] == '>'); const char *f = gt + 1 + app; while (*f == ' ') f++;
            if (!*f) { tp(t, C_ERR, "echo: missing file name"); }
            else {
                int id = fs_resolve(f, t->cwd);
                if (id < 0) id = fs_create(f, t->cwd, 0);
                if (id < 0 || fs_nodes[id].dir) tp(t, C_ERR, "echo: cannot write to that path");
                else { strlen(txt); txt[n] = '\n'; txt[n + 1] = 0; if (app) fs_append(id, txt, n + 1); else fs_write(id, txt, n + 1); }
            }
        }
    }
    else if (!strcmp(c, "ls")) {
        const char *p = 0; for (int i = 1; i < argc; i++) if (argv[i][0] != '-') p = argv[i];
        cmd_ls(t, p);
    }
    else if (!strcmp(c, "pwd")) { char p[128]; fs_path(t->cwd, p); tp(t, C_OUT, p); }
    else if (!strcmp(c, "cd")) {
        int d = argc > 1 ? fs_resolve(argv[1], t->cwd) : fs_resolve("/home", 0);
        if (d < 0) TP(t, C_ERR, "cd: %s: no such directory", argv[1]);
        else if (!fs_nodes[d].dir) TP(t, C_ERR, "cd: %s: not a directory", argv[1]);
        else t->cwd = d;
    }
    else if (!strcmp(c, "cat")) {
        if (argc < 2) tp(t, C_ERR, "usage: cat <file>");
        for (int i = 1; i < argc; i++) {
            int id = fs_resolve(argv[i], t->cwd);
            if (id < 0) TP(t, C_ERR, "cat: %s: no such file", argv[i]);
            else if (fs_nodes[id].dir) TP(t, C_ERR, "cat: %s: is a directory", argv[i]);
            else if (fs_nodes[id].size) tp(t, C_OUT, (const char *)fs_nodes[id].data);
        }
    }
    else if (!strcmp(c, "mkdir") || !strcmp(c, "touch")) {
        int isdir = (c[0] == 'm');
        if (argc < 2) TP(t, C_ERR, "usage: %s <name>", c);
        for (int i = 1; i < argc; i++) {
            int r = fs_create(argv[i], t->cwd, isdir);
            if (r == -2 && !isdir) continue;
            if (r == -2) TP(t, C_ERR, "%s: '%s' already exists", c, argv[i]);
            else if (r < 0) TP(t, C_ERR, "%s: cannot create '%s'", c, argv[i]);
        }
    }
    else if (!strcmp(c, "rm")) {
        int rec = 0, any = 0;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "-rf")) { rec = 1; continue; }
            any = 1;
            int id = fs_resolve(argv[i], t->cwd);
            if (id < 0) { TP(t, C_ERR, "rm: %s: no such file or directory", argv[i]); continue; }
            if (id == 0 || id == t->cwd) { TP(t, C_ERR, "rm: refusing to remove '%s'", argv[i]); continue; }
            int r = rec ? rm_rec(id) : fs_remove(id);
            if (r == -2) TP(t, C_ERR, "rm: %s: directory not empty (use -r)", argv[i]);
        }
        if (!any) tp(t, C_ERR, "usage: rm [-r] <path>");
    }
    else if (!strcmp(c, "cp")) {
        if (argc < 3) tp(t, C_ERR, "usage: cp <src> <dst>");
        else {
            int s = fs_resolve(argv[1], t->cwd), d = fs_resolve(argv[2], t->cwd);
            if (s < 0) TP(t, C_ERR, "cp: %s: no such file", argv[1]);
            else if (fs_nodes[s].dir) tp(t, C_ERR, "cp: directories are not supported");
            else if (d >= 0 && fs_nodes[d].dir) { if (fs_copy(s, d, fs_nodes[s].name) < 0) tp(t, C_ERR, "cp: failed (name exists?)"); }
            else if (d >= 0) fs_write(d, fs_nodes[s].data, fs_nodes[s].size);
            else { int par; char leaf[FS_NAME]; split_path(t, argv[2], &par, leaf);
                   if (par < 0 || !fs_nodes[par].dir || fs_copy(s, par, leaf) < 0) tp(t, C_ERR, "cp: cannot create destination"); }
        }
    }
    else if (!strcmp(c, "mv")) {
        if (argc < 3) tp(t, C_ERR, "usage: mv <src> <dst>");
        else {
            int s = fs_resolve(argv[1], t->cwd), d = fs_resolve(argv[2], t->cwd);
            if (s <= 0) TP(t, C_ERR, "mv: %s: no such file", argv[1]);
            else if (d >= 0 && fs_nodes[d].dir) { if (fs_move(s, d) < 0) tp(t, C_ERR, "mv: cannot move there"); }
            else if (d >= 0) tp(t, C_ERR, "mv: destination exists");
            else { int par; char leaf[FS_NAME]; split_path(t, argv[2], &par, leaf);
                   if (par < 0 || !fs_nodes[par].dir) tp(t, C_ERR, "mv: bad destination");
                   else if (fs_move(s, par) < 0 || fs_rename(s, leaf) < 0) tp(t, C_ERR, "mv: failed"); }
        }
    }
    else if (!strcmp(c, "edit") || !strcmp(c, "notepad")) {
        char p[128]; if (argc > 1) abs_path(t, argv[1], p);
        wm_open(APP_NOTE, argc > 1 ? p : 0);
    }
    else if (!strcmp(c, "open") || !strcmp(c, "run")) {
        if (argc < 2) tp(t, C_ERR, "usage: open <file|app>");
        else {
            int r = wm_open_name(argv[1], argc > 2 ? argv[2] : 0);
            if (r == -2) {
                int id = fs_resolve(argv[1], t->cwd);
                if (id >= 0 && !fs_nodes[id].dir) { char p[128]; fs_path(id, p); wm_open(APP_NOTE, p); }
                else if (id >= 0) { char p[128]; fs_path(id, p); wm_open(APP_FILES, p); }
                else TP(t, C_ERR, "%s: no such app or file", argv[1]);
            } else if (r < 0) tp(t, C_ERR, "too many windows open");
        }
    }
    else if (!strcmp(c, "date")) { rtc_t d; rtc_read(&d); TP(t, C_OUT, "%04d-%02d-%02d %02d:%02d:%02d", d.y, d.mo, d.d, d.h, d.mi, d.s); }
    else if (!strcmp(c, "uptime")) { char up[32]; fmt_uptime(up, ticks); TP(t, C_OUT, "up %s", up); }
    else if (!strcmp(c, "whoami")) tp(t, C_OUT, "user");
    else if (!strcmp(c, "hostname")) tp(t, C_OUT, "nova");
    else if (!strcmp(c, "uname")) TP(t, C_OUT, "%s nova %s i686", OS_NAME, OS_VER);
    else if (!strcmp(c, "ver")) TP(t, C_OUT, "%s version %s (built %s)", OS_NAME, OS_VER, __DATE__);
    else if (!strcmp(c, "mem") || !strcmp(c, "free")) {
        TP(t, C_OUT, "RAM detected : %u MB", mem_total_kb() / 1024);
        TP(t, C_OUT, "Kernel heap  : %u KB used / %u KB total", heap_used() / 1024, heap_total() / 1024);
    }
    else if (!strcmp(c, "df")) {
        u32 b = 0; for (int i = 0; i < FS_MAX; i++) if (fs_nodes[i].used) b += fs_nodes[i].size;
        TP(t, C_OUT, "ramfs: %d/%d nodes, %u bytes of file data", fs_count(), FS_MAX, b);
        { char d[64]; disk_info(d); TP(t, C_OUT, "disk : %s", d); }
    }
    else if (!strcmp(c, "disk")) { char d[64]; disk_info(d); tp(t, C_OUT, d); }
    else if (!strcmp(c, "sync")) {
        if (disk_state == DISK_NONE || disk_state == DISK_FOREIGN) tp(t, C_ERR, "sync: no usable disk");
        else if (fs_save() == 0) tp(t, C_OK, "saved to disk"); else tp(t, C_ERR, "sync: write failed");
    }
    else if (!strcmp(c, "format")) {
        if (disk_state == DISK_NONE) tp(t, C_ERR, "format: no disk attached");
        else if (argc < 2 || strcmp(argv[1], "yes")) { tp(t, C_ERR, "This ERASES the attached disk and writes the current files."); tp(t, C_ERR, "Run 'format yes' to confirm."); }
        else if (disk_format() == 0) tp(t, C_OK, "disk initialised and saved"); else tp(t, C_ERR, "format: write failed");
    }
    else if (!strcmp(c, "cpuinfo")) { char cpu[64]; cpu_brand(cpu); tp(t, C_OUT, cpu); }
    else if (!strcmp(c, "neofetch")) cmd_neofetch(t);
    else if (!strcmp(c, "ps")) {
        tp(t, C_INFO, "PID  APP              TITLE");
        for (int i = 0; i < MAX_WIN; i++) if (wins[i].used)
            TP(t, C_OUT, "%-4d %-16s %s", i + 1, app_table[wins[i].app].name, wins[i].title);
    }
    else if (!strcmp(c, "kill")) {
        int pid = argc > 1 ? katoi(argv[1]) - 1 : -1;
        if (pid < 0 || pid >= MAX_WIN || !wins[pid].used) tp(t, C_ERR, "kill: no such process");
        else if (pid == w->id) t->quit = 1;
        else wm_close(pid);
    }
    else if (!strcmp(c, "history")) { for (int i = t->hn > 10 ? t->hn - 10 : 0; i < t->hn; i++) TP(t, C_OUT, "%4d  %s", i + 1, t->hist[i % 10]); }
    else if (!strcmp(c, "wallpaper")) { if (argc > 1) { wallpaper_make(katoi(argv[1])); wm_dirty(); } else tp(t, C_ERR, "usage: wallpaper <0-3>"); }
    else if (!strcmp(c, "beep")) sys_beep(argc > 1 ? katoi(argv[1]) : 880, argc > 2 ? katoi(argv[2]) : 200);
    else if (!strcmp(c, "exit")) t->quit = 1;
    else if (!strcmp(c, "reboot") || !strcmp(c, "restart")) wm_power(1);
    else if (!strcmp(c, "shutdown") || !strcmp(c, "poweroff") || !strcmp(c, "halt")) wm_power(0);
    else if (!strcmp(c, "panic")) __asm__ volatile("ud2");
    else TP(t, C_ERR, "%s: command not found (try 'help')", c);
}

static void complete(term_t *t){
    int s = t->inlen; while (s > 0 && t->in[s - 1] != ' ') s--;
    const char *word = t->in + s;
    const char *sl = 0; for (const char *q = word; *q; q++) if (*q == '/') sl = q;
    int dir = t->cwd; const char *pre = word;
    if (sl) {
        char dp[128]; int n = (int)(sl - word); if (n > 126) return;
        memcpy(dp, word, n); dp[n] = 0; if (!n) { dp[0] = '/'; dp[1] = 0; }
        dir = fs_resolve(dp, t->cwd); pre = sl + 1;
        if (dir < 0 || !fs_nodes[dir].dir) return;
    }
    int pl = (int)strlen(pre), cnt = 0, first = -1, common = 0;
    for (int i = 1; i < FS_MAX; i++) {
        fsnode_t *f = &fs_nodes[i];
        if (!f->used || f->parent != dir || strncmp(f->name, pre, pl)) continue;
        if (!cnt) { first = i; common = (int)strlen(f->name); }
        else { int k = 0; while (k < common && f->name[k] == fs_nodes[first].name[k]) k++; common = k; }
        cnt++;
    }
    if (!cnt) return;
    for (int k = pl; k < common && t->inlen < 120; k++) t->in[t->inlen++] = fs_nodes[first].name[k];
    if (cnt == 1 && t->inlen < 120) t->in[t->inlen++] = fs_nodes[first].dir ? '/' : ' ';
    t->in[t->inlen] = 0;
}

static void *term_create(window_t *w, const char *arg){
    (void)arg;
    term_t *t = kmalloc(sizeof(term_t));
    if (!t) return 0;
    t->cwd = fs_resolve("/home", 0);
    tp(t, C_INFO, "Nova Claude OS " OS_VER " - type 'help' for a list of commands.");
    tp(t, C_MUTED, "");
    (void)w;
    return t;
}

static void term_draw(window_t *w, int cx, int cy, int cw, int ch){
    term_t *t = w->state;
    fill(cx, cy, cw, ch, RGB(13, 17, 26));
    int rows = (ch - 8) / 16; if (rows < 2) rows = 2;
    int total = t->nl < TL ? t->nl : TL;
    int maxs = total > rows - 1 ? total - (rows - 1) : 0;
    if (t->scroll > maxs) t->scroll = maxs;
    int shown = total < rows - 1 ? total : rows - 1;
    for (int r = 0; r < shown; r++) {
        int idx = t->nl - t->scroll - shown + r;
        if (idx < 0 || idx < t->nl - TL) continue;
        draw_text(cx + 6, cy + 4 + r * 16, t->lines[idx % TL], t->col[idx % TL]);
    }
    int py = cy + 4 + (t->scroll ? rows - 1 : shown) * 16, x = cx + 6;
    char path[128]; prompt_parts(t, path);
    draw_text(x, py, "user@nova", C_OK); x += 72;
    draw_text(x, py, ":", C_OUT); x += 8;
    draw_text(x, py, path, C_DIR); x += text_w(path);
    draw_text(x, py, "$ ", C_OUT); x += 16;
    draw_text(x, py, t->in, C_WHITE);
    x += t->inlen * 8;
    if (t->blink && wm_focused() == w->id) fill(x, py + 1, 8, 14, RGB(0xD8, 0xDE, 0xEA));
    if (t->scroll > 0) draw_text(cx + cw - 16 * 8 - 8, cy + 4, "[ scrolled back ]", RGB(255, 200, 80));
}

static void term_key(window_t *w, int key, int mods){
    term_t *t = w->state;
    int rows = (w->h - TITLE_H - 9) / 16;
    if (mods & M_CTRL) {
        if (key == 'c') { char p[128], l[260]; fs_path(t->cwd, p); ksnprintf(l, sizeof l, "user@nova:%s$ %s^C", p, t->in); addline(t, C_OUT, l); t->inlen = 0; t->in[0] = 0; }
        else if (key == 'l') t->nl = 0;
        return;
    }
    if (key == K_PGUP) { t->scroll += rows / 2; return; }
    if (key == K_PGDN) { t->scroll -= rows / 2; if (t->scroll < 0) t->scroll = 0; return; }
    t->scroll = 0;
    if (key == '\n') {
        char p[128], l[260]; fs_path(t->cwd, p);
        ksnprintf(l, sizeof l, "user@nova:%s$ %s", p, t->in);
        addline(t, C_OUT, l);
        char cmd[128]; scopy(cmd, t->in, 128);
        t->inlen = 0; t->in[0] = 0;
        exec(w, t, cmd);
        if (t->quit) wm_close(w->id);
    }
    else if (key == '\b') { if (t->inlen) t->in[--t->inlen] = 0; }
    else if (key == '\t') complete(t);
    else if (key == K_UP) { if (t->hpos > 0 && t->hpos > t->hn - 10) { t->hpos--; scopy(t->in, t->hist[t->hpos % 10], 128); t->inlen = (int)strlen(t->in); } }
    else if (key == K_DOWN) {
        if (t->hpos < t->hn - 1) { t->hpos++; scopy(t->in, t->hist[t->hpos % 10], 128); t->inlen = (int)strlen(t->in); }
        else { t->hpos = t->hn; t->inlen = 0; t->in[0] = 0; }
    }
    else if (key >= 32 && key < 127 && t->inlen < 120) { t->in[t->inlen++] = (char)key; t->in[t->inlen] = 0; }
}

static int term_mouse(window_t *w, int type, int lx, int ly, int btn){ (void)w; (void)type; (void)lx; (void)ly; (void)btn; return 0; }
static int term_tick(window_t *w){
    term_t *t = w->state;
    int b = (ticks / 50) & 1;
    if (b != t->blink) { t->blink = b; return wm_focused() == w->id; }
    return 0;
}

app_def_t app_term_def = { "Terminal", APP_TERM, 664, 400, 1, term_create, 0, term_draw, term_key, term_mouse, term_tick };
