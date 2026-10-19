/* Nova Claude OS kernel core: libc basics, heap, GDT/IDT/PIC/PIT, PS/2 keyboard+mouse,
   RTC, power control, and an in-memory filesystem. */
#include "os.h"

/* ====================================================================== */
/* libc basics                                                            */
/* ====================================================================== */
void *memset(void *d, int c, size_t n){ void *r = d; __asm__ volatile("rep stosb":"+D"(d),"+c"(n):"a"(c):"memory"); return r; }
void *memcpy(void *d, const void *s, size_t n){ void *r = d; __asm__ volatile("rep movsb":"+D"(d),"+S"(s),"+c"(n)::"memory"); return r; }
void *memmove(void *d, const void *s, size_t n){
    u8 *dd = d; const u8 *ss = s;
    if (dd <= ss || dd >= ss + n) return memcpy(d, s, n);
    while (n--) dd[n] = ss[n];
    return d;
}
int memcmp(const void *a, const void *b, size_t n){
    const u8 *x = a, *y = b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}
size_t strlen(const char *s){ size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b){ while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
int strncmp(const char *a, const char *b, size_t n){
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (u8)*a - (u8)*b : 0;
}
void scopy(char *dst, const char *src, int max){
    int i = 0;
    if (max <= 0) return;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
int katoi(const char *s){
    int neg = 0, v = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}
static u32 rng_state = 0x2545F491u;
u32 krand(void){ u32 x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }

static int utoa_b(u32 v, int base, char *out, int upper){
    char t[16]; int n = 0;
    if (!v) t[n++] = '0';
    while (v) { int d = v % base; t[n++] = d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10; v /= base; }
    for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
    out[n] = 0;
    return n;
}
int ksnprintf(char *buf, int n, const char *fmt, ...){
    va_list ap; va_start(ap, fmt);
    int o = 0;
#define PUT(ch) do { if (o < n - 1) buf[o] = (ch); o++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        char tmp[20]; const char *s = tmp; int neg = 0;
        switch (*fmt) {
        case 'd': { int v = va_arg(ap, int); u32 u = v < 0 ? (u32)(-v) : (u32)v; neg = v < 0; utoa_b(u, 10, tmp, 0); break; }
        case 'u': utoa_b(va_arg(ap, u32), 10, tmp, 0); break;
        case 'x': utoa_b(va_arg(ap, u32), 16, tmp, 0); break;
        case 'X': utoa_b(va_arg(ap, u32), 16, tmp, 1); break;
        case 'c': tmp[0] = (char)va_arg(ap, int); tmp[1] = 0; break;
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; break;
        case '%': tmp[0] = '%'; tmp[1] = 0; break;
        default: tmp[0] = '?'; tmp[1] = 0; break;
        }
        int len = (int)strlen(s) + neg;
        int pad = width > len ? width - len : 0;
        if (!left && !zero) while (pad--) PUT(' ');
        if (neg) PUT('-');
        if (!left && zero) while (pad--) PUT('0');
        for (const char *q = s; *q; q++) PUT(*q);
        if (left) while (pad--) PUT(' ');
    }
    if (n > 0) buf[o < n ? o : n - 1] = 0;
    va_end(ap);
    return o;
#undef PUT
}

/* ====================================================================== */
/* Heap                                                                   */
/* ====================================================================== */
typedef struct blk { u32 size, used; struct blk *next; u32 pad; } blk_t;
#define HEAP_SZ (12 * 1024 * 1024)
static u8 heap_mem[HEAP_SZ] __attribute__((aligned(16)));
static blk_t *heap_head;
static void heap_init(void){
    heap_head = (blk_t *)heap_mem;
    heap_head->size = HEAP_SZ - sizeof(blk_t); heap_head->used = 0; heap_head->next = 0;
}
void *kmalloc(size_t n){
    n = (n + 15) & ~15u;
    for (blk_t *b = heap_head; b; b = b->next) {
        if (!b->used && b->size >= n) {
            if (b->size >= n + sizeof(blk_t) + 32) {
                blk_t *nb = (blk_t *)((u8 *)(b + 1) + n);
                nb->size = b->size - n - sizeof(blk_t); nb->used = 0; nb->next = b->next;
                b->next = nb; b->size = n;
            }
            b->used = 1;
            memset(b + 1, 0, b->size);
            return b + 1;
        }
    }
    return 0;
}
void kfree(void *p){
    if (!p) return;
    ((blk_t *)p - 1)->used = 0;
    for (blk_t *c = heap_head; c; c = c->next)
        while (!c->used && c->next && !c->next->used) { c->size += sizeof(blk_t) + c->next->size; c->next = c->next->next; }
}
u32 heap_used(void){ u32 s = 0; for (blk_t *b = heap_head; b; b = b->next) if (b->used) s += b->size + sizeof(blk_t); return s; }
u32 heap_total(void){ return HEAP_SZ; }

/* ====================================================================== */
/* RTC                                                                    */
/* ====================================================================== */
static u8 cmos(u8 r){ outb(0x70, r); return inb(0x71); }
void rtc_read(rtc_t *t){
    int spin = 100000;
    while ((cmos(0x0A) & 0x80) && spin--) ;
    int s = cmos(0), mi = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9), b = cmos(0x0B);
    int pm = h & 0x80; h &= 0x7F;
    if (!(b & 4)) {
#define BCD(x) (((x) & 15) + ((x) >> 4) * 10)
        s = BCD(s); mi = BCD(mi); h = BCD(h); d = BCD(d); mo = BCD(mo); y = BCD(y);
#undef BCD
    }
    if (!(b & 2)) { h %= 12; if (pm) h += 12; }
    t->s = s; t->mi = mi; t->h = h; t->d = d; t->mo = mo; t->y = 2000 + y;
}

/* ====================================================================== */
/* Interrupts: IDT, PIC, PIT                                              */
/* ====================================================================== */
struct idt_entry { u16 lo, sel; u8 zero, flags; u16 hi; } __attribute__((packed));
struct idt_ptr { u16 limit; u32 base; } __attribute__((packed));
static struct idt_entry idt[256];
static struct idt_ptr idtp;
extern u32 isr_table[48];
extern void idt_load(void *);

volatile u32 ticks;

#define EQ 256
static event_t eq[EQ];
static volatile int eq_h, eq_t;
static void ev_push(event_t e){ int n = (eq_h + 1) % EQ; if (n == eq_t) return; eq[eq_h] = e; eq_h = n; }
int event_pending(void){ return eq_t != eq_h; }
int event_poll(event_t *e){ if (eq_t == eq_h) return 0; *e = eq[eq_t]; eq_t = (eq_t + 1) % EQ; return 1; }

/* ---- keyboard (scancode set 1) ---- */
static int kb_shift, kb_ctrl, kb_alt, kb_caps, kb_e0;
static const char sc_norm[] = "\0" "\033" "1234567890-=" "\b" "\t" "qwertyuiop[]" "\n" "\0" "asdfghjkl;'`" "\0" "\\" "zxcvbnm,./" "\0" "*" "\0" " ";
static const char sc_shift[] = "\0" "\033" "!@#$%^&*()_+" "\b" "\t" "QWERTYUIOP{}" "\n" "\0" "ASDFGHJKL:\"~" "\0" "|" "ZXCVBNM<>?" "\0" "*" "\0" " ";
static const char sc_pad[] = "789-456+1230.";

static void kbd_irq(void){
    u8 sc = inb(0x60);
    if (sc == 0xE0) { kb_e0 = 1; return; }
    int rel = sc & 0x80; sc &= 0x7F;
    int e0 = kb_e0; kb_e0 = 0;
    if (sc == 0x1D) { kb_ctrl = !rel; return; }
    if (sc == 0x38) { kb_alt = !rel; return; }
    if (!e0 && (sc == 0x2A || sc == 0x36)) { kb_shift = !rel; return; }
    if (rel) return;
    if (!e0 && sc == 0x3A) { kb_caps ^= 1; return; }
    int key = 0;
    if (e0) {
        switch (sc) {
        case 0x48: key = K_UP; break;   case 0x50: key = K_DOWN; break;
        case 0x4B: key = K_LEFT; break; case 0x4D: key = K_RIGHT; break;
        case 0x47: key = K_HOME; break; case 0x4F: key = K_END; break;
        case 0x49: key = K_PGUP; break; case 0x51: key = K_PGDN; break;
        case 0x53: key = K_DEL; break;  case 0x52: key = K_INS; break;
        case 0x1C: key = '\n'; break;   case 0x35: key = '/'; break;
        }
    } else if (sc >= 0x3B && sc <= 0x44) key = K_F1 + (sc - 0x3B);
    else if (sc == 0x57) key = K_F11;
    else if (sc == 0x58) key = K_F12;
    else if (sc < sizeof(sc_norm) - 1) {
        char c = kb_shift ? sc_shift[sc] : sc_norm[sc];
        if (kb_caps) { if (c >= 'a' && c <= 'z') c -= 32; else if (c >= 'A' && c <= 'Z') c += 32; }
        key = (u8)c;
    } else if (sc >= 0x47 && sc <= 0x53) key = sc_pad[sc - 0x47];
    if (!key) return;
    int mods = (kb_shift ? M_SHIFT : 0) | (kb_ctrl ? M_CTRL : 0) | (kb_alt ? M_ALT : 0);
    if (kb_ctrl && key >= 'A' && key <= 'Z') key += 32;
    event_t e = { EV_KEY, 0, 0, 0, key, mods };
    ev_push(e);
}

/* ---- mouse (PS/2) ---- */
int mx = 512, my = 384;
static u8 mpkt[3]; static int mcyc, mbtn;
static void mouse_irq(void){
    u8 d = inb(0x60);
    if (mcyc == 0 && !(d & 8)) return;
    mpkt[mcyc++] = d;
    if (mcyc < 3) return;
    mcyc = 0;
    u8 f = mpkt[0];
    if (f & 0xC0) return;
    int dx = mpkt[1] - ((f << 4) & 0x100);
    int dy = mpkt[2] - ((f << 3) & 0x100);
    mx += dx; my -= dy;
    if (mx < 0) mx = 0; if (my < 0) my = 0;
    if (mx >= SW) mx = SW - 1; if (my >= SH) my = SH - 1;
    if (dx || dy) { event_t e = { EV_MOVE, mx, my, mbtn, 0, 0 }; ev_push(e); }
    int nb = f & 7, ch = nb ^ mbtn;
    for (int b = 1; b <= 4; b <<= 1) if (ch & b) {
        event_t e = { (nb & b) ? EV_DOWN : EV_UP, mx, my, b, 0, 0 };
        mbtn = (nb & b) ? (mbtn | b) : (mbtn & ~b);
        e.btn = b;
        ev_push(e);
    }
}
static void kbc_wait_w(void){ int t = 200000; while ((inb(0x64) & 2) && t--) ; }
static void kbc_wait_r(void){ int t = 200000; while (!(inb(0x64) & 1) && t--) ; }
static void mouse_write(u8 v){ kbc_wait_w(); outb(0x64, 0xD4); kbc_wait_w(); outb(0x60, v); }
static u8 mouse_read(void){ kbc_wait_r(); return inb(0x60); }
static void ps2_init(void){
    int g = 64; while ((inb(0x64) & 1) && g--) inb(0x60);
    kbc_wait_w(); outb(0x64, 0xA8);
    kbc_wait_w(); outb(0x64, 0x20); kbc_wait_r();
    u8 st = inb(0x60); st |= 3; st &= ~0x20;
    kbc_wait_w(); outb(0x64, 0x60); kbc_wait_w(); outb(0x60, st);
    mouse_write(0xF6); mouse_read();
    mouse_write(0xF4); mouse_read();
    g = 64; while ((inb(0x64) & 1) && g--) inb(0x60);
}

/* ---- exceptions ---- */
static const char *exc_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor overrun", "Invalid TSS", "Segment not present", "Stack fault", "General protection fault",
    "Page fault", "Reserved", "x87 FPU error", "Alignment check", "Machine check", "SIMD error",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved" };

void isr_handler(regs_t *r){
    if (r->int_no < 32) {
        char m[400];
        ksnprintf(m, sizeof m,
            "Stop code: %s (vector %u)\n\nEIP=%08X  CS=%04X  EFLAGS=%08X  ERR=%08X\n"
            "EAX=%08X  EBX=%08X  ECX=%08X  EDX=%08X\nESI=%08X  EDI=%08X  EBP=%08X  ESP=%08X\n\n"
            "The system has been halted to protect your data.\nPlease restart the virtual machine.",
            exc_names[r->int_no], r->int_no, r->eip, r->cs, r->eflags, r->err,
            r->eax, r->ebx, r->ecx, r->edx, r->esi, r->edi, r->ebp, r->esp);
        gfx_panic_screen("Nova Claude OS ran into a problem", m);
    }
    int irq = r->int_no - 32;
    if (irq == 0) ticks++;
    else if (irq == 1) kbd_irq();
    else if (irq == 12) mouse_irq();
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static void idt_set(int n, u32 h){
    idt[n].lo = h & 0xFFFF; idt[n].hi = h >> 16; idt[n].sel = 0x08; idt[n].zero = 0; idt[n].flags = 0x8E;
}

static u32 ram_kb;
u32 mem_total_kb(void){ return ram_kb; }

void kernel_hw_init(void *mbi){
    u32 *m = mbi;
    ram_kb = (m[0] & 1) ? m[2] + 1024 : 0;
    heap_init();
    gfx_init(mbi);
    mx = SW / 2; my = SH / 2;
    for (int i = 0; i < 48; i++) idt_set(i, isr_table[i]);
    idtp.limit = sizeof idt - 1; idtp.base = (u32)idt;
    idt_load(&idtp);
    /* remap PIC */
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 32);   outb(0xA1, 40);
    outb(0x21, 4);    outb(0xA1, 2);
    outb(0x21, 1);    outb(0xA1, 1);
    outb(0x21, 0xF8); outb(0xA1, 0xEF);
    /* PIT: 100 Hz */
    outb(0x43, 0x36); outb(0x40, 11932 & 0xFF); outb(0x40, 11932 >> 8);
    ps2_init();
    rtc_t t; rtc_read(&t);
    rng_state ^= (u32)(t.s * 2654435761u + t.mi * 40503u + t.h * 977u);
    if (!rng_state) rng_state = 1;
    __asm__ volatile("sti");
}

/* ====================================================================== */
/* Power / misc                                                           */
/* ====================================================================== */
void sys_reboot(void){
    __asm__ volatile("cli");
    int t = 100000; while ((inb(0x64) & 2) && t--) ;
    outb(0x64, 0xFE);
    struct idt_ptr bad = { 0, 0 };
    __asm__ volatile("lidt %0; int3" :: "m"(bad));
    for (;;) __asm__ volatile("hlt");
}
void sys_shutdown(void){
    outw(0x604, 0x2000);    /* QEMU */
    outw(0xB004, 0x2000);   /* Bochs / old QEMU */
    outw(0x4004, 0x3400);   /* VirtualBox */
    outw(0x600, 0x34);      /* Cloud Hypervisor / misc */
}
void sys_beep(u32 hz, u32 ms){
    if (hz < 20) hz = 20;
    u32 div = 1193182 / hz;
    outb(0x43, 0xB6); outb(0x42, div & 0xFF); outb(0x42, div >> 8);
    outb(0x61, inb(0x61) | 3);
    u32 end = ticks + ms / 10 + 1;
    while (ticks < end) __asm__ volatile("hlt");
    outb(0x61, inb(0x61) & ~3);
}
static void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d){
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}
void cpu_brand(char *out){
    u32 a, b, c, d;
    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a >= 0x80000004u) {
        u32 *p = (u32 *)out;
        for (int i = 0; i < 3; i++) cpuid(0x80000002u + i, &p[i * 4], &p[i * 4 + 1], &p[i * 4 + 2], &p[i * 4 + 3]);
        out[48] = 0;
        int k = 0; while (out[k] == ' ') k++;
        if (k) memmove(out, out + k, 49 - k);
    } else {
        cpuid(0, &a, &b, &c, &d);
        u32 *p = (u32 *)out; p[0] = b; p[1] = d; p[2] = c; out[12] = 0;
    }
}

/* ====================================================================== */
/* RAM filesystem                                                         */
/* ====================================================================== */
fsnode_t fs_nodes[FS_MAX];
int fs_dirty;
static u32 fs_dirty_t;
static void fs_touch(void){ fs_dirty = 1; fs_dirty_t = ticks; }

static int fs_alloc(void){
    for (int i = 1; i < FS_MAX; i++) if (!fs_nodes[i].used) { memset(&fs_nodes[i], 0, sizeof(fsnode_t)); fs_nodes[i].used = 1; return i; }
    return -1;
}
int fs_child(int dir, const char *name){
    for (int i = 1; i < FS_MAX; i++)
        if (fs_nodes[i].used && fs_nodes[i].parent == dir && !strcmp(fs_nodes[i].name, name)) return i;
    return -1;
}
int fs_resolve(const char *p, int cwd){
    int cur = (p[0] == '/') ? 0 : cwd;
    char comp[FS_NAME + 1];
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        int n = 0;
        while (*p && *p != '/') { if (n < FS_NAME) comp[n] = *p; n++; p++; }
        if (n >= FS_NAME) return -1;
        comp[n] = 0;
        if (!strcmp(comp, ".")) continue;
        if (!strcmp(comp, "..")) { cur = fs_nodes[cur].parent; continue; }
        if (!fs_nodes[cur].dir) return -1;
        int c = fs_child(cur, comp);
        if (c < 0) return -1;
        cur = c;
    }
    return cur;
}
static int valid_name(const char *n){
    int L = (int)strlen(n);
    if (L == 0 || L >= FS_NAME || !strcmp(n, ".") || !strcmp(n, "..")) return 0;
    for (int i = 0; i < L; i++) if (n[i] == '/') return 0;
    return 1;
}
int fs_create(const char *path, int cwd, int dir){
    char pp[128]; const char *leaf; int par;
    const char *sl = 0;
    for (const char *q = path; *q; q++) if (*q == '/') sl = q;
    if (!sl) { par = cwd; leaf = path; }
    else {
        int n = (int)(sl - path);
        if (n >= 127) return -1;
        memcpy(pp, path, n); pp[n] = 0;
        if (n == 0) { pp[0] = '/'; pp[1] = 0; }
        par = fs_resolve(pp, cwd); leaf = sl + 1;
    }
    if (par < 0 || !fs_nodes[par].dir) return -1;
    if (!valid_name(leaf)) return -1;
    if (fs_child(par, leaf) >= 0) return -2;
    int id = fs_alloc();
    if (id < 0) return -1;
    scopy(fs_nodes[id].name, leaf, FS_NAME);
    fs_nodes[id].dir = dir; fs_nodes[id].parent = par;
    fs_touch();
    return id;
}
int fs_remove(int id){
    if (id <= 0 || id >= FS_MAX || !fs_nodes[id].used) return -1;
    if (fs_nodes[id].dir) for (int i = 1; i < FS_MAX; i++) if (fs_nodes[i].used && fs_nodes[i].parent == id) return -2;
    kfree(fs_nodes[id].data);
    memset(&fs_nodes[id], 0, sizeof(fsnode_t));
    fs_touch();
    return 0;
}
int fs_write(int id, const void *data, u32 len){
    fsnode_t *n = &fs_nodes[id];
    if (n->dir) return -1;
    if (len + 1 > n->cap) {
        u32 nc = (len + 64) & ~63u;
        u8 *nd = kmalloc(nc);
        if (!nd) return -1;
        kfree(n->data); n->data = nd; n->cap = nc;
    }
    memcpy(n->data, data, len);
    n->data[len] = 0; n->size = len;
    fs_touch();
    return 0;
}
int fs_append(int id, const void *data, u32 len){
    fsnode_t *n = &fs_nodes[id];
    if (n->dir) return -1;
    u32 nl = n->size + len;
    u8 *nd = kmalloc((nl + 64) & ~63u);
    if (!nd) return -1;
    if (n->size) memcpy(nd, n->data, n->size);
    memcpy(nd + n->size, data, len);
    nd[nl] = 0;
    kfree(n->data); n->data = nd; n->cap = (nl + 64) & ~63u; n->size = nl;
    fs_touch();
    return 0;
}
void fs_path(int id, char *out){
    int chain[32], n = 0;
    while (id > 0 && n < 32) { chain[n++] = id; id = fs_nodes[id].parent; }
    int o = 0;
    if (!n) { out[0] = '/'; out[1] = 0; return; }
    while (n--) { out[o++] = '/'; for (const char *q = fs_nodes[chain[n]].name; *q; q++) out[o++] = *q; }
    out[o] = 0;
}
int fs_list(int dir, int *out){
    int n = 0;
    for (int i = 1; i < FS_MAX; i++) if (fs_nodes[i].used && fs_nodes[i].parent == dir) out[n++] = i;
    for (int i = 1; i < n; i++) {
        int v = out[i], j = i - 1;
        while (j >= 0) {
            fsnode_t *a = &fs_nodes[out[j]], *b = &fs_nodes[v];
            int gt = (a->dir != b->dir) ? (b->dir > a->dir) : (strcmp(a->name, b->name) > 0);
            if (!gt) break;
            out[j + 1] = out[j]; j--;
        }
        out[j + 1] = v;
    }
    return n;
}
int fs_rename(int id, const char *name){
    if (id <= 0 || !valid_name(name)) return -1;
    int ex = fs_child(fs_nodes[id].parent, name);
    if (ex >= 0 && ex != id) return -2;
    scopy(fs_nodes[id].name, name, FS_NAME);
    fs_touch();
    return 0;
}
int fs_copy(int id, int newdir, const char *name){
    if (fs_nodes[id].dir || !valid_name(name) || fs_child(newdir, name) >= 0) return -1;
    int nid = fs_alloc();
    if (nid < 0) return -1;
    scopy(fs_nodes[nid].name, name, FS_NAME);
    fs_nodes[nid].parent = newdir;
    if (fs_nodes[id].size) fs_write(nid, fs_nodes[id].data, fs_nodes[id].size);
    return nid;
}
int fs_move(int id, int newdir){
    if (id <= 0 || !fs_nodes[newdir].dir) return -1;
    for (int p = newdir; ; p = fs_nodes[p].parent) { if (p == id) return -1; if (p == 0) break; }
    int ex = fs_child(newdir, fs_nodes[id].name);
    if (ex >= 0 && ex != id) return -2;
    fs_nodes[id].parent = newdir;
    fs_touch();
    return 0;
}
int fs_count(void){ int n = 0; for (int i = 0; i < FS_MAX; i++) if (fs_nodes[i].used) n++; return n; }

static void mkfile(const char *path, const char *text){
    int id = fs_create(path, 0, 0);
    if (id >= 0) fs_write(id, text, (u32)strlen(text));
}
void fs_init(void){
    memset(fs_nodes, 0, sizeof fs_nodes);
    fs_nodes[0].used = 1; fs_nodes[0].dir = 1; fs_nodes[0].parent = 0; fs_nodes[0].name[0] = '/';
    fs_create("/home", 0, 1); fs_create("/docs", 0, 1); fs_create("/etc", 0, 1); fs_create("/tmp", 0, 1);
    mkfile("/readme.txt",
        "Welcome to Nova Claude OS!\n\n"
        "Nova Claude OS is a small hobby operating system written from scratch in C and\n"
        "x86 assembly. It boots with GRUB, drives a linear framebuffer directly,\n"
        "and ships a window manager plus a handful of apps.\n\n"
        "Try this:\n"
        "  - Open the Terminal and type 'help'\n"
        "  - Edit this file in Notepad and press Ctrl+S\n"
        "  - Browse files in the File Manager\n"
        "  - Play Snake, draw in Paint, change the wallpaper in Settings\n\n"
        "Note: the file system lives in RAM, so files vanish on reboot.\n");
    mkfile("/docs/shortcuts.txt",
        "Keyboard shortcuts\n------------------\n"
        "Alt+Tab     switch window\n"
        "Alt+F4      close window\n"
        "Ctrl+S      save (Notepad)\n"
        "F2          rename (File Manager)\n"
        "PgUp/PgDn   scroll (Terminal)\n"
        "Ctrl+L      clear screen (Terminal)\n");
    mkfile("/docs/about.txt", "Nova Claude OS " OS_VER "\nKernel: monolithic, 32-bit protected mode\nDisplay: VESA linear framebuffer via GRUB\n");
    mkfile("/etc/motd", "Have a productive session.\n");
    mkfile("/home/hello.txt", "Hello from Nova Claude OS!\n");
}

/* ====================================================================== */
/* ATA (IDE) PIO disk driver + filesystem persistence                      */
/* ====================================================================== */
int disk_state;                 /* DISK_* */
u32 disk_sectors;
static u16 d_base; static int d_slave;
static u32 last_save_tick; static int save_failed;

static inline void insw_(u16 port, void *buf, int n){ __asm__ volatile("rep insw" : "+D"(buf), "+c"(n) : "d"(port) : "memory"); }
static inline void outsw_(u16 port, const void *buf, int n){ __asm__ volatile("rep outsw" : "+S"(buf), "+c"(n) : "d"(port)); }
static void ata_delay(u16 b){ for (int i = 0; i < 4; i++) inb(b + 0x206); }
static int ata_wait(u16 b, int drq){
    ata_delay(b);
    u8 s; int t = 400000;
    while (((s = inb(b + 7)) & 0x80) && --t) ;
    if (!t || (s & 0x21)) return -1;
    if (drq) { t = 400000; while (!((s = inb(b + 7)) & 8) && --t) if (s & 0x21) return -1; if (!t) return -1; }
    return 0;
}
static int ata_identify(u16 b, int slave, u32 *sectors){
    outb(b + 6, 0xA0 | (slave << 4)); ata_delay(b);
    u8 st = inb(b + 7);
    if (st == 0 || st == 0xFF) return 0;
    outb(b + 2, 0); outb(b + 3, 0); outb(b + 4, 0); outb(b + 5, 0);
    outb(b + 7, 0xEC);
    st = inb(b + 7);
    if (!st) return 0;
    int t = 400000; while (((st = inb(b + 7)) & 0x80) && --t) ;
    if (!t) return 0;
    if (inb(b + 4) || inb(b + 5)) return 0;               /* ATAPI / SATA signature: not a plain ATA disk */
    t = 400000; while (!((st = inb(b + 7)) & 8) && --t) if (st & 1) return 0;
    if (!t) return 0;
    u16 id[256]; insw_(b, id, 256);
    *sectors = id[60] | ((u32)id[61] << 16);
    return *sectors > 0;
}
static int ata_rw(int write, u32 lba, u32 count, void *buf){
    u8 *p = buf;
    while (count) {
        u32 n = count > 64 ? 64 : count;
        outb(d_base + 6, 0xE0 | (d_slave << 4) | ((lba >> 24) & 0xF)); ata_delay(d_base);
        if (ata_wait(d_base, 0) < 0) return -1;
        outb(d_base + 2, n); outb(d_base + 3, lba); outb(d_base + 4, lba >> 8); outb(d_base + 5, lba >> 16);
        outb(d_base + 7, write ? 0x30 : 0x20);
        for (u32 i = 0; i < n; i++) {
            if (ata_wait(d_base, 1) < 0) return -1;
            if (write) outsw_(d_base, p, 256); else insw_(d_base, p, 256);
            p += 512;
        }
        if (write) { outb(d_base + 7, 0xE7); if (ata_wait(d_base, 0) < 0) return -1; }
        lba += n; count -= n;
    }
    return 0;
}

typedef struct { char magic[8]; u32 version, bytes, checksum, pad[4]; } nv_hdr_t;
typedef struct { u8 used, dir; u16 parent; u32 size; char name[FS_NAME]; } __attribute__((packed)) nv_rec_t;
#define NV_MAX_IMG (4u * 1024 * 1024)
static u32 nv_sum(const u8 *p, u32 n){ u32 h = 5381; while (n--) h = h * 33 + *p++; return h; }

int disk_init(void){
    static const u16 bases[2] = { 0x1F0, 0x170 };
    disk_state = DISK_NONE;
    for (int c = 0; c < 2; c++) for (int s = 0; s < 2; s++) {
        u32 sec;
        if (ata_identify(bases[c], s, &sec)) {
            d_base = bases[c]; d_slave = s; disk_sectors = sec;
            u8 buf[512];
            if (ata_rw(0, 0, 1, buf) < 0) continue;
            nv_hdr_t *h = (nv_hdr_t *)buf;
            if (!memcmp(h->magic, "NOVAFS01", 8)) { disk_state = DISK_NOVAFS; return 1; }
            int blank = 1; for (int i = 0; i < 512; i++) if (buf[i]) { blank = 0; break; }
            disk_state = blank ? DISK_BLANK : DISK_FOREIGN;
            return 1;
        }
    }
    return 0;
}
int fs_load(void){
    if (disk_state != DISK_NOVAFS) return 0;
    u8 sec[512];
    if (ata_rw(0, 0, 1, sec) < 0) return 0;
    nv_hdr_t *h = (nv_hdr_t *)sec;
    u32 hdr_n = FS_MAX * sizeof(nv_rec_t);
    if (h->version != 1 || h->bytes < hdr_n || h->bytes > NV_MAX_IMG || (h->bytes + 511) / 512 + 1 > disk_sectors) return 0;
    u32 secs = (h->bytes + 511) / 512;
    u8 *img = kmalloc(secs * 512);
    if (!img) return 0;
    if (ata_rw(0, 1, secs, img) < 0 || nv_sum(img, h->bytes) != h->checksum) { kfree(img); return 0; }
    nv_rec_t *r = (nv_rec_t *)img;
    u32 off = hdr_n;
    if (!r[0].used || !r[0].dir) { kfree(img); return 0; }
    for (int i = 0; i < FS_MAX; i++) {         /* validate before touching live state */
        if (!r[i].used) continue;
        if (r[i].parent >= FS_MAX || r[i].name[FS_NAME - 1] != 0) { kfree(img); return 0; }
        if (!r[i].dir) { if (off + r[i].size > h->bytes) { kfree(img); return 0; } off += r[i].size; }
    }
    for (int i = 0; i < FS_MAX; i++) { kfree(fs_nodes[i].data); }
    memset(fs_nodes, 0, sizeof fs_nodes);
    off = hdr_n;
    for (int i = 0; i < FS_MAX; i++) {
        if (!r[i].used) continue;
        fsnode_t *n = &fs_nodes[i];
        n->used = 1; n->dir = r[i].dir; n->parent = r[i].parent; scopy(n->name, r[i].name, FS_NAME);
        if (!n->dir) {
            u32 cap = (r[i].size + 64) & ~63u;
            n->data = kmalloc(cap);
            if (n->data) { memcpy(n->data, img + off, r[i].size); n->size = r[i].size; n->cap = cap; }
            off += r[i].size;
        }
    }
    kfree(img);
    fs_dirty = 0;
    return 1;
}
int fs_save(void){
    if (disk_state != DISK_NOVAFS && disk_state != DISK_BLANK) return -1;
    u32 hdr_n = FS_MAX * sizeof(nv_rec_t), total = hdr_n;
    for (int i = 0; i < FS_MAX; i++) if (fs_nodes[i].used && !fs_nodes[i].dir) total += fs_nodes[i].size;
    if (total > NV_MAX_IMG || (total + 511) / 512 + 1 > disk_sectors) return -1;
    u32 secs = (total + 511) / 512;
    u8 *img = kmalloc(secs * 512);
    if (!img) return -1;
    nv_rec_t *r = (nv_rec_t *)img;
    u32 off = hdr_n;
    for (int i = 0; i < FS_MAX; i++) {
        fsnode_t *n = &fs_nodes[i];
        if (!n->used) continue;
        r[i].used = 1; r[i].dir = n->dir; r[i].parent = n->parent; r[i].size = n->dir ? 0 : n->size;
        scopy(r[i].name, n->name, FS_NAME);
        if (!n->dir && n->size) { memcpy(img + off, n->data, n->size); off += n->size; }
    }
    u8 sec[512]; memset(sec, 0, sizeof sec);
    nv_hdr_t *h = (nv_hdr_t *)sec;
    memcpy(h->magic, "NOVAFS01", 8); h->version = 1; h->bytes = total; h->checksum = nv_sum(img, total);
    int rc = ata_rw(1, 1, secs, img);
    if (rc == 0) rc = ata_rw(1, 0, 1, sec);     /* header last: a torn write leaves the old header invalid-checksum, never half-valid */
    kfree(img);
    if (rc == 0) { disk_state = DISK_NOVAFS; fs_dirty = 0; save_failed = 0; }
    else save_failed = 1;
    return rc;
}
int disk_format(void){
    if (disk_state == DISK_NONE) return -1;
    disk_state = DISK_BLANK;
    fs_dirty = 1;
    return fs_save();
}
void fs_sync(int force){
    if (!fs_dirty || (disk_state != DISK_NOVAFS && disk_state != DISK_BLANK)) return;
    if (!force && (ticks - fs_dirty_t < 150 || ticks - last_save_tick < 300)) return;
    last_save_tick = ticks;
    fs_save();
}
void disk_info(char *out){
    char sz[24];
    u32 mb = disk_sectors / 2048;
    ksnprintf(sz, sizeof sz, "%u MB", mb);
    switch (disk_state) {
    case DISK_NONE: scopy(out, "No disk (files live in RAM only)", 64); break;
    case DISK_BLANK: ksnprintf(out, 64, "ATA disk %s, new (will initialise)", sz); break;
    case DISK_NOVAFS: ksnprintf(out, 64, "ATA disk %s, %s", sz, save_failed ? "WRITE ERROR" : fs_dirty ? "unsaved changes" : "all saved"); break;
    default: ksnprintf(out, 64, "ATA disk %s: unknown data, not touched", sz); break;
    }
}
