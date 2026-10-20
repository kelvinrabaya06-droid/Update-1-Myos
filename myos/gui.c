/* gui.c - MyOS 0.3 graphical desktop.
 * 800x600x32 framebuffer via the Bochs/QEMU VBE interface, PS/2 mouse + keyboard,
 * a Windows-9x-style window manager, a setup wizard and a few apps.            */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef int            s32;

#include "font.h"

/* ================= hardware access ================= */
static inline void outb(u16 p, u8 v)  { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)         { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline u16  inw(u16 p)         { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline u32  inl(u16 p)         { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* ================= screen ================= */
#define SW 800
#define SH 600
#define TASKH 30
static u32 *fb;                       /* linear framebuffer (set by gui_init)   */
static u32 back[SW * SH];             /* off-screen buffer: draw here, then copy */
static int clx0 = 0, cly0 = 0, clx1 = SW, cly1 = SH;

#define C_BLACK 0x000000
#define C_WHITE 0xFFFFFF
#define C_GRAY  0xC0C0C0
#define C_LGRAY 0xDFDFDF
#define C_DGRAY 0x808080
#define C_NAVY  0x000080
#define C_TEAL  0x008080

static void set_clip(int x0, int y0, int x1, int y1) {
    clx0 = x0 < 0 ? 0 : x0;  cly0 = y0 < 0 ? 0 : y0;
    clx1 = x1 > SW ? SW : x1; cly1 = y1 > SH ? SH : y1;
}
static void clip_all(void) { set_clip(0, 0, SW, SH); }

static inline void px(int x, int y, u32 c) {
    if (x >= clx0 && x < clx1 && y >= cly0 && y < cly1) back[y * SW + x] = c;
}
static void rect(int x, int y, int w, int h, u32 c) {
    int x0 = x > clx0 ? x : clx0,        y0 = y > cly0 ? y : cly0;
    int x1 = x + w < clx1 ? x + w : clx1, y1 = y + h < cly1 ? y + h : cly1;
    for (int j = y0; j < y1; j++) { u32 *p = &back[j * SW]; for (int i = x0; i < x1; i++) p[i] = c; }
}
static u32 mix(u32 a, u32 b, int n, int d) {
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return ((ar + (br - ar) * n / d) << 16) | ((ag + (bg - ag) * n / d) << 8) | (ab + (bb - ab) * n / d);
}
static void vgrad(int x, int y, int w, int h, u32 a, u32 b) {
    for (int j = 0; j < h; j++) rect(x, y + j, w, 1, mix(a, b, j, h > 1 ? h - 1 : 1));
}

/* Windows-9x style 3D border */
static void bevel(int x, int y, int w, int h, int raised) {
    u32 o1 = raised ? C_WHITE : C_DGRAY, o2 = raised ? C_BLACK : C_WHITE;
    u32 i1 = raised ? C_LGRAY : C_BLACK, i2 = raised ? C_DGRAY : C_LGRAY;
    rect(x, y, w - 1, 1, o1);   rect(x, y, 1, h - 1, o1);
    rect(x, y + h - 1, w, 1, o2); rect(x + w - 1, y, 1, h, o2);
    rect(x + 1, y + 1, w - 3, 1, i1); rect(x + 1, y + 1, 1, h - 3, i1);
    rect(x + 1, y + h - 2, w - 2, 1, i2); rect(x + w - 2, y + 1, 1, h - 2, i2);
}
static void panel(int x, int y, int w, int h, int raised) { rect(x, y, w, h, C_GRAY); bevel(x, y, w, h, raised); }
static void sunken_white(int x, int y, int w, int h) { rect(x, y, w, h, C_WHITE); bevel(x, y, w, h, 0); }

/* ================= text ================= */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int sequ(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int sstart(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static void itoa_(int v, char *o) {
    char t[12]; int n = 0, k = 0; unsigned u = v < 0 ? -(unsigned)v : (unsigned)v;
    if (!u) t[n++] = '0';
    while (u) { t[n++] = '0' + u % 10; u /= 10; }
    if (v < 0) o[k++] = '-';
    while (n) o[k++] = t[--n];
    o[k] = 0;
}
static void glyph_s(int x, int y, char ch, u32 c, int s) {
    if (ch < 32 || ch > 126) return;
    const unsigned char *g = font8x16[ch - 32];
    for (int r = 0; r < 16; r++) {
        if (!g[r]) continue;
        for (int b = 0; b < 8; b++) if (g[r] & (0x80 >> b)) {
            if (s == 1) px(x + b, y + r, c); else rect(x + b * s, y + r * s, s, s, c);
        }
    }
}
static void text(int x, int y, const char *s, u32 c) { while (*s) { glyph_s(x, y, *s++, c, 1); x += 8; } }
static void text_bold(int x, int y, const char *s, u32 c) { text(x, y, s, c); text(x + 1, y, s, c); }
static void text_s(int x, int y, const char *s, u32 c, int sc) { while (*s) { glyph_s(x, y, *s++, c, sc); x += 8 * sc; } }
static void text_c(int x, int w, int y, const char *s, u32 c) { text(x + (w - slen(s) * 8) / 2, y, s, c); }

/* ================= PIT timer: a millisecond clock without interrupts ================= */
static u32 ms = 0, tacc = 0;
static u16 plast = 0;
static void pit_init(void) {
    outb(0x43, 0x34); outb(0x40, 0); outb(0x40, 0);     /* channel 0, rate generator, reload 65536 */
    outb(0x43, 0x00); u16 lo = inb(0x40); u16 hi = inb(0x40); plast = (hi << 8) | lo;
}
static void time_update(void) {
    outb(0x43, 0x00); u16 lo = inb(0x40); u16 hi = inb(0x40);
    u16 now = (hi << 8) | lo;
    u16 d = (u16)(plast - now); plast = now;
    static int stall = 0;                                /* safety net: if the timer never ticks, fake time passing */
    if (d == 0) { if (++stall > 3000) { stall = 0; ms += 16; } } else stall = 0;
    tacc += d;
    while (tacc >= 1193) { tacc -= 1193; ms++; }
}

/* ================= real-time clock ================= */
typedef struct { int h, m, s, d, mo, y; } Time;
static Time now_t;
static u8 cmos(u8 r) { outb(0x70, r); return inb(0x71); }
static int bcd(int v) { return (v & 15) + (v >> 4) * 10; }
static void rtc_read(Time *t) {
    while (cmos(0x0A) & 0x80) ;
    int s = cmos(0), m = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9);
    int b = cmos(0x0B);
    int pm = h & 0x80; h &= 0x7F;
    if (!(b & 4)) { s = bcd(s); m = bcd(m); h = bcd(h); d = bcd(d); mo = bcd(mo); y = bcd(y); }
    if (!(b & 2)) { h %= 12; if (pm) h += 12; }
    t->s = s; t->m = m; t->h = h; t->d = d; t->mo = mo; t->y = y;
}
static void two(char *o, int n) { o[0] = '0' + n / 10; o[1] = '0' + n % 10; }
static void fmt_hm(char *o, const Time *t) { two(o, t->h); o[2] = ':'; two(o + 3, t->m); o[5] = 0; }
static void fmt_time(char *o, const Time *t) { two(o, t->h); o[2] = ':'; two(o + 3, t->m); o[5] = ':'; two(o + 6, t->s); o[8] = 0; }
static void fmt_date(char *o, const Time *t) {
    int y = 2000 + t->y;
    o[0] = '0' + y / 1000; o[1] = '0' + (y / 100) % 10; o[2] = '0' + (y / 10) % 10; o[3] = '0' + y % 10;
    o[4] = '-'; two(o + 5, t->mo); o[7] = '-'; two(o + 8, t->d); o[10] = 0;
}

static void reboot(void) {
    for (int t = 0; t < 100000 && (inb(0x64) & 2); t++) ;
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}

/* ================= input state ================= */
#define K_ESC   27
#define K_UP    0x101
#define K_DOWN  0x102
#define K_LEFT  0x103
#define K_RIGHT 0x104

static int mx = SW / 2, my = SH / 2, mb = 0;      /* mouse position and buttons       */
static int dnx = 0, dny = 0;                      /* where the last button went down  */
static int dirty = 1, curdirty = 1;               /* full redraw / cursor-only redraw */

static int inrect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && y >= ry && x < rx + rw && y < ry + rh; }
/* a click counts when the button went down AND came up inside the same rectangle */
static int HITR(int x, int y, int w, int h) { return inrect(mx, my, x, y, w, h) && inrect(dnx, dny, x, y, w, h); }
static int PRESS(int x, int y, int w, int h) { return (mb & 1) && HITR(x, y, w, h); }

static void button(int x, int y, int w, int h, const char *label, int disabled) {
    int p = !disabled && PRESS(x, y, w, h);
    rect(x, y, w, h, C_GRAY);
    bevel(x, y, w, h, !p);
    int tx = x + (w - slen(label) * 8) / 2 + (p ? 1 : 0), ty = y + (h - 16) / 2 + (p ? 1 : 0);
    if (disabled) { text(tx + 1, ty + 1, label, C_WHITE); text(tx, ty, label, C_DGRAY); }
    else text(tx, ty, label, C_BLACK);
}
static void checkbox(int x, int y, int on) {
    sunken_white(x, y, 13, 13);
    if (on) {                                        /* check mark */
        for (int k = 0; k < 3; k++) { px(x + 3 + k, y + 5 + k, C_BLACK); px(x + 3 + k, y + 6 + k, C_BLACK); }
        for (int k = 0; k < 5; k++) { px(x + 5 + k, y + 7 - k, C_BLACK); px(x + 5 + k, y + 8 - k, C_BLACK); }
    }
}
static void progress(int x, int y, int w, int h, int pct) {
    sunken_white(x, y, w, h);
    int blocks = (w - 6) / 10, n = pct * blocks / 100;
    for (int i = 0; i < n; i++) rect(x + 3 + i * 10, y + 3, 8, h - 6, C_NAVY);
}

/* ================= apps and windows ================= */
enum { A_NOTE, A_CALC, A_PAINT, A_TERM, A_SNAKE, A_STORE, A_ABOUT, A_POWER, A_COUNT };
#define NINST 5                                    /* A_NOTE..A_SNAKE can be installed/removed */
static const char *pname[NINST] = { "Notepad", "Calculator", "Paint", "Terminal", "Snake" };
static const char *pdesc[NINST] = { "Simple text editor", "Four-function calculator", "Draw with the mouse",
                                    "Command-line shell", "Classic arcade game" };
static const int   psize[NINST] = { 120, 64, 310, 150, 88 };
static int pstate[NINST];                          /* 0 = not installed, 1 = installing, 2 = installed */
static u32 pstart[NINST];

static const char *apptitle(int t) {
    static const char *n[A_COUNT] = { "Notepad", "Calculator", "Paint", "Terminal", "Snake",
                                      "Software Center", "About MyOS", "Shut Down MyOS" };
    return n[t];
}

#define BORDER 3
#define TITLEH 18
#define MAXW   8
typedef struct { int used, type, x, y, w, h, min; } Win;
static Win wins[MAXW];
static int order[MAXW], nord = 0;                  /* z-order: order[nord-1] is on top */
static int focus = -1, drag = -1, ddx = 0, ddy = 0, cap = -1, dwin = -1, close_req = -1;
static char uname_[20] = "User";

static int cox(const Win *w) { return w->x + BORDER; }
static int coy(const Win *w) { return w->y + BORDER + TITLEH; }
static int cw_(const Win *w) { return w->w - 2 * BORDER; }
static int ch_(const Win *w) { return w->h - 2 * BORDER - TITLEH; }

/* ---- small icons (32x32), drawn with rectangles ---- */
static void app_icon(int t, int x, int y) {
    switch (t) {
    case A_NOTE:
        rect(x + 4, y + 1, 24, 30, C_BLACK); rect(x + 5, y + 2, 22, 28, C_WHITE);
        for (int i = 0; i < 5; i++) rect(x + 8, y + 7 + i * 5, 16, 2, 0x4060C0);
        break;
    case A_CALC:
        rect(x + 4, y + 1, 24, 30, C_BLACK); rect(x + 5, y + 2, 22, 28, C_GRAY);
        rect(x + 8, y + 5, 16, 7, 0x206020);
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) rect(x + 8 + c * 6, y + 15 + r * 5, 4, 3, C_BLACK);
        break;
    case A_PAINT:
        rect(x + 2, y + 3, 28, 26, C_BLACK); rect(x + 3, y + 4, 26, 24, C_WHITE);
        rect(x + 6, y + 8, 7, 7, 0xE02020); rect(x + 15, y + 8, 7, 7, 0x2060E0);
        rect(x + 6, y + 17, 7, 7, 0x20B020); rect(x + 15, y + 17, 7, 7, 0xE0C020);
        break;
    case A_TERM:
        rect(x + 1, y + 4, 30, 24, C_DGRAY); rect(x + 2, y + 5, 28, 22, C_BLACK);
        text(x + 4, y + 8, ">_", 0x40FF40);
        break;
    case A_SNAKE:
        rect(x + 1, y + 1, 30, 30, 0x103010);
        for (int i = 0; i < 5; i++) rect(x + 4 + i * 4, y + 20, 4, 4, 0x30D030);
        rect(x + 20, y + 12, 4, 8, 0x30D030); rect(x + 20, y + 8, 4, 4, 0x80FF80);
        rect(x + 8, y + 7, 5, 5, 0xE02020);
        break;
    case A_STORE:
        rect(x + 2, y + 10, 28, 20, 0x805020); rect(x + 2, y + 10, 28, 4, 0xB08040);
        rect(x + 14, y + 10, 4, 20, 0xE0C020); rect(x + 6, y + 3, 20, 7, 0x2080E0);
        text(x + 12, y + 1, "+", C_WHITE);
        break;
    case A_ABOUT:
        rect(x + 2, y + 2, 28, 28, C_NAVY); rect(x + 4, y + 4, 24, 24, 0x2080E0);
        text_s(x + 8, y + 3, "i", C_WHITE, 2);
        break;
    default:
        rect(x + 2, y + 2, 28, 28, 0x800000); rect(x + 4, y + 4, 24, 24, 0xE02020);
        text_s(x + 8, y + 3, "O", C_WHITE, 2);
        break;
    }
}

/* ---- Notepad ---- */
#define NR 19
#define NC 58
static char nl[NR][NC + 1];
static int nrow = 0, ncol = 0;

static void note_draw(int ox, int oy, int cw, int ch) {
    rect(ox, oy, cw, ch, C_WHITE);
    for (int r = 0; r < NR; r++) text(ox + 4, oy + 4 + r * 16, nl[r], C_BLACK);
    if ((ms / 500) & 1) rect(ox + 4 + ncol * 8, oy + 5 + nrow * 16, 2, 14, C_BLACK);
}
static void note_key(int k) {
    char *l = nl[nrow]; int len = slen(l);
    if (k == K_LEFT)       { if (ncol > 0) ncol--; else if (nrow > 0) { nrow--; ncol = slen(nl[nrow]); } }
    else if (k == K_RIGHT) { if (ncol < len) ncol++; else if (nrow < NR - 1) { nrow++; ncol = 0; } }
    else if (k == K_UP)    { if (nrow > 0) { nrow--; if (ncol > slen(nl[nrow])) ncol = slen(nl[nrow]); } }
    else if (k == K_DOWN)  { if (nrow < NR - 1) { nrow++; if (ncol > slen(nl[nrow])) ncol = slen(nl[nrow]); } }
    else if (k == '\n') {
        if (nrow < NR - 1) {
            for (int r = NR - 1; r > nrow + 1; r--) scpy(nl[r], nl[r - 1], NC + 1);
            scpy(nl[nrow + 1], l + ncol, NC + 1); l[ncol] = 0; nrow++; ncol = 0;
        }
    }
    else if (k == '\b') {
        if (ncol > 0) { for (int i = ncol - 1; i < len; i++) l[i] = l[i + 1]; ncol--; }
        else if (nrow > 0) {
            char *p = nl[nrow - 1]; int pl = slen(p);
            if (pl + len <= NC) {
                for (int i = 0; i <= len; i++) p[pl + i] = l[i];
                for (int r = nrow; r < NR - 1; r++) scpy(nl[r], nl[r + 1], NC + 1);
                nl[NR - 1][0] = 0; nrow--; ncol = pl;
            }
        }
    }
    else if (k >= 32 && k < 127) { if (len < NC) { for (int i = len; i >= ncol; i--) l[i + 1] = l[i]; l[ncol++] = k; } }
}
static void note_click(int ox, int oy) {
    int r = (my - oy - 4) / 16, c = (mx - ox - 4 + 4) / 8;
    if (r < 0) r = 0; if (r >= NR) r = NR - 1;
    int len = slen(nl[r]); if (c < 0) c = 0; if (c > len) c = len;
    nrow = r; ncol = c;
}

/* ---- Calculator ---- */
static s32 c_acc = 0, c_cur = 0; static int c_op = 0, c_new = 1, c_err = 0;
static const char calc_keys[4][4] = { {'7','8','9','/'}, {'4','5','6','*'}, {'1','2','3','-'}, {'0','C','=','+'} };
static s32 c_apply(s32 a, s32 b, int op) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': if (b == 0) { c_err = 1; return 0; } if (b == -1) return -a; return a / b;
    }
    return b;
}
static void calc_press(int ch) {
    if (c_err && ch != 'C') return;
    if (ch >= '0' && ch <= '9') { if (c_new) { c_cur = 0; c_new = 0; } if (c_cur < 100000000) c_cur = c_cur * 10 + (ch - '0'); }
    else if (ch == 'C') { c_acc = c_cur = 0; c_op = 0; c_new = 1; c_err = 0; }
    else if (ch == '=') { if (c_op) { c_cur = c_apply(c_acc, c_cur, c_op); c_op = 0; } c_new = 1; }
    else { if (c_op && !c_new) c_cur = c_apply(c_acc, c_cur, c_op); c_acc = c_cur; c_op = ch; c_new = 1; }
}
static void calc_draw(int ox, int oy, int cw, int ch) {
    rect(ox, oy, cw, ch, C_GRAY);
    sunken_white(ox + 8, oy + 8, cw - 16, 40);
    char b[16]; if (c_err) scpy(b, "Error", 16); else itoa_(c_cur, b);
    text_s(ox + cw - 16 - slen(b) * 16 - 4, oy + 16, b, C_BLACK, 2);
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        char l[2] = { calc_keys[r][c], 0 };
        button(ox + 8 + c * 48, oy + 56 + r * 40, 44, 36, l, 0);
    }
}
static void calc_click(int ox, int oy) {
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
        if (HITR(ox + 8 + c * 48, oy + 56 + r * 40, 44, 36)) calc_press(calc_keys[r][c]);
}

/* ---- Paint ---- */
#define PW 432
#define PH 262
static u8 canvas[PW * PH];
static int pcol = 0, psz = 3, plx = -1, ply = -1, pcur = 0;
static const u32 pal[16] = { 0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xC0C0C0,
                             0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF };
static const int psizes[3] = { 1, 3, 7 };

static void paint_clear(void) { for (int i = 0; i < PW * PH; i++) canvas[i] = 15; }
static void dab(int x, int y, u8 c) {
    int a = -(psz / 2);
    for (int j = 0; j < psz; j++) for (int i = 0; i < psz; i++) {
        int xx = x + a + i, yy = y + a + j;
        if (xx >= 0 && yy >= 0 && xx < PW && yy < PH) canvas[yy * PW + xx] = c;
    }
}
static void pline(int x0, int y0, int x1, int y1, u8 c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
        dab(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}
static void paint_draw(int ox, int oy, int cw, int ch) {
    rect(ox, oy, cw, ch, C_GRAY);
    bevel(ox + 2, oy + 2, PW + 4, PH + 4, 0);
    for (int y = 0; y < PH; y++) {
        int sy = oy + 4 + y; if (sy < cly0 || sy >= cly1) continue;
        for (int x = 0; x < PW; x++) px(ox + 4 + x, sy, pal[canvas[y * PW + x]]);
    }
    for (int i = 0; i < 16; i++) {
        int x = ox + 6 + i * 22, y = oy + 276;
        rect(x - 1, y - 1, 22, 22, i == pcol ? C_BLACK : C_GRAY);
        rect(x, y, 20, 20, pal[i]);
    }
    for (int i = 0; i < 3; i++) {
        char l[4]; itoa_(psizes[i], l);
        int x = ox + 372 + i * 22, y = oy + 276;
        button(x, y, 20, 20, l, 0);
        if (psz == psizes[i]) rect(x + 3, y + 17, 14, 2, C_NAVY);
    }
    button(ox + 6, oy + 302, 60, 22, "Clear", 0);
    text(ox + 80, oy + 305, "Left: draw   Right: erase", C_DGRAY);
}
static void paint_mouse(int ox, int oy, int ev) {
    int cx = mx - (ox + 4), cy = my - (oy + 4);
    if (ev == 1) {
        if (inrect(cx, cy, 0, 0, PW, PH) && (mb & 3)) { pcur = (mb & 2) ? 15 : pcol; dab(cx, cy, pcur); plx = cx; ply = cy; }
        else plx = -1;
    } else if (ev == 0) {
        if ((mb & 3) && plx >= 0) { pline(plx, ply, cx, cy, pcur); plx = cx; ply = cy; }
    } else plx = -1;
}
static void paint_click(int ox, int oy) {
    for (int i = 0; i < 16; i++) if (HITR(ox + 6 + i * 22, oy + 276, 20, 20)) pcol = i;
    for (int i = 0; i < 3; i++) if (HITR(ox + 372 + i * 22, oy + 276, 20, 20)) psz = psizes[i];
    if (HITR(ox + 6, oy + 302, 60, 22)) paint_clear();
}

/* ---- Terminal ---- */
#define TCOLS 64
#define TROWS 20
static char tg[TROWS][TCOLS + 1];
static int tcx = 0, tcy = 0, tlen = 0;
static char tin[60];

static void t_clear(void) {
    for (int r = 0; r < TROWS; r++) { for (int c = 0; c < TCOLS; c++) tg[r][c] = ' '; tg[r][TCOLS] = 0; }
    tcx = tcy = 0;
}
static void t_scroll(void) {
    for (int r = 1; r < TROWS; r++) for (int c = 0; c < TCOLS; c++) tg[r - 1][c] = tg[r][c];
    for (int c = 0; c < TCOLS; c++) tg[TROWS - 1][c] = ' ';
    tcy = TROWS - 1;
}
static void t_putc(char c) {
    if (c == '\n') { tcx = 0; tcy++; }
    else if (c == '\b') { if (tcx > 0) { tcx--; tg[tcy][tcx] = ' '; } }
    else { tg[tcy][tcx] = c; if (++tcx >= TCOLS) { tcx = 0; tcy++; } }
    if (tcy >= TROWS) t_scroll();
}
static void t_puts(const char *s) { while (*s) t_putc(*s++); }
static void t_prompt(void) { t_puts("myos> "); tlen = 0; }

static void t_exec(const char *c) {
    if (!*c) return;
    if (sequ(c, "help"))
        t_puts("Commands: help clear echo ver time cpu apps\n          exit reboot shutdown\n");
    else if (sequ(c, "clear")) t_clear();
    else if (sstart(c, "echo ")) { t_puts(c + 5); t_putc('\n'); }
    else if (sequ(c, "ver")) t_puts("MyOS 0.3\n");
    else if (sequ(c, "time")) {
        Time t; char a[16], b[16]; rtc_read(&t); fmt_date(a, &t); fmt_time(b, &t);
        t_puts(a); t_putc(' '); t_puts(b); t_putc('\n');
    }
    else if (sequ(c, "cpu")) {
        u32 b, cc, d; __asm__ volatile("cpuid" : "=b"(b), "=c"(cc), "=d"(d) : "a"(0));
        char v[13]; *(u32 *)&v[0] = b; *(u32 *)&v[4] = d; *(u32 *)&v[8] = cc; v[12] = 0;
        t_puts("CPU vendor: "); t_puts(v); t_putc('\n');
    }
    else if (sequ(c, "apps")) {
        t_puts("Installed:");
        for (int i = 0; i < NINST; i++) if (pstate[i] == 2) { t_putc(' '); t_puts(pname[i]); }
        t_putc('\n');
    }
    else if (sequ(c, "exit")) close_req = A_TERM;
    else if (sequ(c, "reboot")) reboot();
    else if (sequ(c, "shutdown")) { extern void power_off(void); power_off(); }
    else { t_puts("unknown command: "); t_puts(c); t_putc('\n'); }
}
static void term_key(int k) {
    if (k == '\n') { t_putc('\n'); tin[tlen] = 0; t_exec(tin); if (close_req < 0) t_prompt(); }
    else if (k == '\b') { if (tlen > 0) { tlen--; t_putc('\b'); } }
    else if (k >= 32 && k < 127 && tlen < 50) { tin[tlen++] = k; t_putc(k); }
}
static void term_draw(int ox, int oy, int cw, int ch) {
    rect(ox, oy, cw, ch, C_BLACK);
    for (int r = 0; r < TROWS; r++) text(ox + 4, oy + 4 + r * 16, tg[r], 0xC0C0C0);
    if ((ms / 500) & 1) rect(ox + 4 + tcx * 8, oy + 4 + tcy * 16 + 13, 8, 2, 0xC0C0C0);
}

/* ---- Snake ---- */
#define GW 24
#define GH 16
static u8 sx[GW * GH], sy[GW * GH];
static int snl = 0, sdir = 1, sndir = 1, sfx = 0, sfy = 0, sscore = 0, salive = 0;
static u32 sstep = 0, rng = 12345;
static const int sdx[4] = { 0, 1, 0, -1 }, sdy[4] = { -1, 0, 1, 0 };
static int rnd(void) { rng = rng * 1103515245u + 12345u; return (rng >> 16) & 0x7FFF; }
static void snake_food(void) {
    if (snl >= GW * GH) { salive = 0; return; }
    for (;;) {
        int x = rnd() % GW, y = rnd() % GH, ok = 1;
        for (int i = 0; i < snl; i++) if (sx[i] == x && sy[i] == y) ok = 0;
        if (ok) { sfx = x; sfy = y; return; }
    }
}
static void snake_reset(void) {
    snl = 3; for (int i = 0; i < 3; i++) { sx[i] = 12 - i; sy[i] = 8; }
    sdir = sndir = 1; sscore = 0; salive = 1; sstep = ms; rng += ms; snake_food();
}
static void snake_step(void) {
    sdir = sndir;
    int nx = sx[0] + sdx[sdir], ny = sy[0] + sdy[sdir];
    if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) { salive = 0; return; }
    int grow = (nx == sfx && ny == sfy), check = snl - (grow ? 0 : 1);
    for (int i = 0; i < check; i++) if (sx[i] == nx && sy[i] == ny) { salive = 0; return; }
    if (grow && snl < GW * GH) snl++;
    for (int i = snl - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
    sx[0] = nx; sy[0] = ny;
    if (grow) { sscore += 10; snake_food(); }
}
static void snake_key(int k) {
    int d = -1;
    if (k == K_UP) d = 0; else if (k == K_RIGHT) d = 1; else if (k == K_DOWN) d = 2; else if (k == K_LEFT) d = 3;
    if (d >= 0 && (d + 2) % 4 != sdir) sndir = d;
    if (!salive && (k == ' ' || k == '\n')) snake_reset();
}
static void snake_draw(int ox, int oy, int cw, int ch) {
    rect(ox, oy, cw, ch, 0x101810);
    rect(ox, oy, cw, 24, C_GRAY);
    char b[24]; scpy(b, "Score: ", 24); itoa_(sscore, b + 7);
    text(ox + 8, oy + 4, b, C_BLACK);
    if (!salive) text(ox + cw - 8 - 30 * 8, oy + 4, "Game over - press Space", 0x800000);
    int gy = oy + 24;
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++)
        if (((x + y) & 1) == 0) rect(ox + x * 16, gy + y * 16, 16, 16, 0x142014);
    rect(ox + sfx * 16 + 2, gy + sfy * 16 + 2, 12, 12, 0xE02020);
    for (int i = snl - 1; i >= 0; i--) rect(ox + sx[i] * 16 + 1, gy + sy[i] * 16 + 1, 14, 14, i == 0 ? 0x90FF90 : 0x30C030);
}

/* ---- Software Center ---- */
#define INST_MS 2400
static void store_draw(int ox, int oy, int cw, int ch) {
    rect(ox, oy, cw, ch, C_GRAY);
    text_bold(ox + 10, oy + 8, "Software Center", C_BLACK);
    text(ox + 10, oy + 26, "Install or remove programs.", C_DGRAY);
    int lx = ox + 8, ly = oy + 48, lw = cw - 16;
    sunken_white(lx, ly, lw, 5 * 52 + 4);
    for (int i = 0; i < NINST; i++) {
        int ry = ly + 2 + i * 52;
        if (i) rect(lx + 2, ry, lw - 4, 1, C_LGRAY);
        app_icon(i, lx + 10, ry + 10);
        text_bold(lx + 54, ry + 6, pname[i], C_BLACK);
        char sz[16]; itoa_(psize[i], sz); int n = slen(sz); sz[n] = ' '; sz[n + 1] = 'K'; sz[n + 2] = 'B'; sz[n + 3] = 0;
        text(lx + 54 + (slen(pname[i]) + 1) * 8 + 8, ry + 6, sz, C_DGRAY);
        text(lx + 54, ry + 26, pdesc[i], C_DGRAY);
        int bx = lx + lw - 90;
        if (pstate[i] == 0) button(bx, ry + 14, 78, 24, "Install", 0);
        else if (pstate[i] == 1) {
            int pct = (int)((ms - pstart[i]) * 100 / (INST_MS + i * 300)); if (pct > 100) pct = 100;
            progress(bx - 86, ry + 17, 164, 18, pct);
        } else { button(bx - 86, ry + 14, 78, 24, "Open", 0); button(bx, ry + 14, 78, 24, "Remove", 0); }
    }
    text(ox + 10, oy + ch - 20, "Programs live in memory until a disk driver exists.", C_DGRAY);
}

/* ---- About ---- */
static void about_draw(int ox, int oy, int cw, int ch) {
    rect(ox, oy, cw, ch, C_GRAY);
    app_icon(A_ABOUT, ox + 16, oy + 16);
    text_s(ox + 64, oy + 12, "MyOS", C_NAVY, 2);
    text(ox + 64, oy + 46, "Version 0.3", C_BLACK);
    rect(ox + 10, oy + 74, cw - 20, 2, C_DGRAY); rect(ox + 10, oy + 76, cw - 20, 1, C_WHITE);
    text(ox + 16, oy + 86, "A tiny x86 OS written from scratch.", C_BLACK);
    char b[40]; scpy(b, "Registered to: ", 40); scpy(b + 15, uname_, 20);
    text(ox + 16, oy + 110, b, C_BLACK);
    text(ox + 16, oy + 130, "Display: 800 x 600, 32-bit color", C_BLACK);
    char u[32]; scpy(u, "Uptime: ", 32); itoa_((int)(ms / 1000), u + 8); int n = slen(u); u[n] = 's'; u[n + 1] = 0;
    text(ox + 16, oy + 150, u, C_BLACK);
}

/* ---- Power ---- */
static void power_draw(int ox, int oy, int cw, int ch) {
    rect(ox, oy, cw, ch, C_GRAY);
    app_icon(A_POWER, ox + 16, oy + 16);
    text(ox + 64, oy + 18, "What do you want the", C_BLACK);
    text(ox + 64, oy + 36, "computer to do?", C_BLACK);
    button(ox + 14, oy + ch - 38, 90, 26, "Restart", 0);
    button(ox + 112, oy + ch - 38, 90, 26, "Shut down", 0);
    button(ox + 210, oy + ch - 38, 90, 26, "Cancel", 0);
}
void power_off(void) {
    clip_all(); rect(0, 0, SW, SH, C_BLACK);
    text_c(0, SW, SH / 2 - 8, "It is now safe to turn off your computer.", 0xFF8000);
    { u32 *d = fb; const u32 *s = back; u32 n = SW * SH; __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(n) :: "memory"); }
    for (;;) __asm__ volatile("cli; hlt");
}

/* ================= window manager ================= */
static int find_win(int t) { for (int i = 0; i < MAXW; i++) if (wins[i].used && wins[i].type == t) return i; return -1; }
static void order_remove(int i) { int k = 0; for (int j = 0; j < nord; j++) if (order[j] != i) order[k++] = order[j]; nord = k; }
static int top_visible(void) { for (int j = nord - 1; j >= 0; j--) if (!wins[order[j]].min) return order[j]; return -1; }
static void focus_win(int i) { order_remove(i); order[nord++] = i; wins[i].min = 0; focus = i; dirty = 1; }
static void close_win(int i) {
    order_remove(i); wins[i].used = 0;
    if (cap == i) cap = -1; if (drag == i) drag = -1; if (dwin == i) dwin = -1;
    focus = top_visible(); dirty = 1;
}
static void minimize_win(int i) { wins[i].min = 1; if (focus == i) focus = top_visible(); if (cap == i) cap = -1; dirty = 1; }

static void open_app(int t) {
    int i = find_win(t);
    if (i >= 0) { focus_win(i); return; }
    for (i = 0; i < MAXW; i++) if (!wins[i].used) break;
    if (i == MAXW) return;
    int cw, ch;
    switch (t) {
        case A_NOTE:  cw = 480; ch = 328; break;
        case A_CALC:  cw = 200; ch = 224; break;
        case A_PAINT: cw = 440; ch = 330; break;
        case A_TERM:  cw = 520; ch = 328; break;
        case A_SNAKE: cw = 384; ch = 280; break;
        case A_STORE: cw = 520; ch = 340; break;
        case A_ABOUT: cw = 340; ch = 190; break;
        default:      cw = 316; ch = 110; break;
    }
    static int cas = 0;
    Win *w = &wins[i];
    w->used = 1; w->type = t; w->min = 0; w->w = cw + 2 * BORDER; w->h = ch + 2 * BORDER + TITLEH;
    w->x = 110 + (cas % 6) * 28; w->y = 16 + (cas % 6) * 28; cas++;
    if (w->x + w->w > SW) w->x = SW - w->w;
    if (w->y + w->h > SH - TASKH) w->y = SH - TASKH - w->h;
    if (w->x < 0) w->x = 0; if (w->y < 0) w->y = 0;
    if (t == A_POWER) { w->x = (SW - w->w) / 2; w->y = (SH - TASKH - w->h) / 2; }
    if (t == A_SNAKE) snake_reset();
    order[nord++] = i; focus = i; dirty = 1;
}

static void app_draw(Win *w) {
    int ox = cox(w), oy = coy(w), cw = cw_(w), ch = ch_(w);
    set_clip(ox, oy, ox + cw, oy + ch);
    switch (w->type) {
        case A_NOTE:  note_draw(ox, oy, cw, ch);  break;
        case A_CALC:  calc_draw(ox, oy, cw, ch);  break;
        case A_PAINT: paint_draw(ox, oy, cw, ch); break;
        case A_TERM:  term_draw(ox, oy, cw, ch);  break;
        case A_SNAKE: snake_draw(ox, oy, cw, ch); break;
        case A_STORE: store_draw(ox, oy, cw, ch); break;
        case A_ABOUT: about_draw(ox, oy, cw, ch); break;
        case A_POWER: power_draw(ox, oy, cw, ch); break;
    }
    clip_all();
}
static void app_key(Win *w, int k) {
    switch (w->type) {
        case A_NOTE:  note_key(k); break;
        case A_TERM:  term_key(k); break;
        case A_SNAKE: snake_key(k); break;
        case A_CALC:
            if ((k >= '0' && k <= '9') || k == '+' || k == '-' || k == '*' || k == '/') calc_press(k);
            else if (k == '=' || k == '\n') calc_press('=');
            else if (k == 'c' || k == 'C' || k == K_ESC) calc_press('C');
            else if (k == '\b' && !c_new) c_cur /= 10;
            break;
    }
}
static void store_click(int ox, int oy, int cw) {
    int lx = ox + 8, ly = oy + 48, lw = cw - 16;
    for (int i = 0; i < NINST; i++) {
        int ry = ly + 2 + i * 52, bx = lx + lw - 90;
        if (pstate[i] == 0) { if (HITR(bx, ry + 14, 78, 24)) { pstate[i] = 1; pstart[i] = ms; } }
        else if (pstate[i] == 2) {
            if (HITR(bx - 86, ry + 14, 78, 24)) open_app(i);
            else if (HITR(bx, ry + 14, 78, 24)) { pstate[i] = 0; int j = find_win(i); if (j >= 0) close_win(j); }
        }
    }
}
static void app_click(Win *w) {
    int ox = cox(w), oy = coy(w), cw = cw_(w), ch = ch_(w);
    switch (w->type) {
        case A_NOTE:  note_click(ox, oy);  break;
        case A_CALC:  calc_click(ox, oy);  break;
        case A_PAINT: paint_click(ox, oy); break;
        case A_STORE: store_click(ox, oy, cw); break;
        case A_POWER:
            if (HITR(ox + 14, oy + ch - 38, 90, 26)) reboot();
            if (HITR(ox + 112, oy + ch - 38, 90, 26)) power_off();
            if (HITR(ox + 210, oy + ch - 38, 90, 26)) close_req = A_POWER;
            break;
    }
}
static void app_mouse(Win *w, int ev) { if (w->type == A_PAINT) paint_mouse(cox(w), coy(w), ev); }

static void draw_frame(int x, int y, int w, int h, const char *title, int active, int btns) {
    panel(x, y, w, h, 1);
    int tx = x + BORDER, ty = y + BORDER, tw_ = w - 2 * BORDER;
    u32 c0 = active ? 0x000080 : 0x808080, c1 = active ? 0x1084D0 : 0xB8B8B8;
    for (int i = 0; i < tw_; i++) rect(tx + i, ty, 1, TITLEH, mix(c0, c1, i, tw_));
    text_bold(tx + 6, ty + 1, title, C_WHITE);
    if (btns) {
        int bx = x + w - BORDER - 18, by = ty + 2;
        panel(bx, by, 16, 14, 1);
        for (int k = 0; k < 8; k++) {
            px(bx + 4 + k, by + 3 + k, C_BLACK); px(bx + 5 + k, by + 3 + k, C_BLACK);
            px(bx + 11 - k, by + 3 + k, C_BLACK); px(bx + 12 - k, by + 3 + k, C_BLACK);
        }
        bx -= 18;
        panel(bx, by, 16, 14, 1); rect(bx + 4, by + 9, 8, 2, C_BLACK);
    }
}

/* ================= setup wizard ================= */
enum { M_SETUP, M_DESK };
static int mode = M_SETUP;
static int spage = 0, sacc = 0, snlen = 4, slastpct = -1;
static int schk[NINST] = { 1, 1, 0, 1, 0 };
static u32 sbegin = 0;
#define SDX 130
#define SDY 110
#define SDW 540
#define SDH 380
#define SCW 534
#define SCH 356
#define SETUP_MS 7000
static const char *sfiles[] = { "SYSTEM/KERNEL.SYS", "SYSTEM/VGA.DRV", "SYSTEM/MOUSE.DRV", "SYSTEM/KEYBOARD.DRV",
                                "SYSTEM/TIMER.DRV", "DESKTOP/SHELL.EXE", "DESKTOP/TASKBAR.DLL", "DESKTOP/WALLPAPER.BMP",
                                "FONTS/MONO8X16.FNT", "PROGRAMS/SETUP.INF", "USERS/PROFILE.DAT", "Finishing up..." };
#define NFILES 12

static int setup_next_en(void) { return spage == 0 || (spage == 1 && sacc) || (spage == 2 && snlen > 0) || spage == 3 || spage == 5; }
static int setup_back_en(void) { return spage >= 1 && spage <= 3; }
static int setup_canc_en(void) { return spage < 4; }

static void setup_next(void) {
    if (spage == 5) { mode = M_DESK; dirty = 1; return; }
    spage++;
    if (spage == 4) { sbegin = ms; slastpct = -1; }
    dirty = 1;
}
static void setup_click(void) {
    int ox = SDX + BORDER, oy = SDY + BORDER + TITLEH, cx0 = ox + 166, cy0 = oy + 16, by = oy + SCH - 34;
    dirty = 1;
    if (setup_back_en() && HITR(ox + SCW - 260, by, 80, 26)) { spage--; return; }
    if (setup_next_en() && HITR(ox + SCW - 174, by, 80, 26)) { setup_next(); return; }
    if (setup_canc_en() && HITR(ox + SCW - 88, by, 80, 26)) reboot();
    if (spage == 1 && HITR(cx0, cy0 + 176, 220, 22)) sacc = !sacc;
    if (spage == 3) for (int i = 0; i < NINST; i++) if (HITR(cx0, cy0 + 56 + i * 32, 356, 32)) schk[i] = !schk[i];
}
static void setup_key(int k) {
    if (spage == 2) {
        if (k == '\b') { if (snlen > 0) uname_[--snlen] = 0; }
        else if (k >= 32 && k < 127 && snlen < 15) { uname_[snlen++] = k; uname_[snlen] = 0; }
        else if (k == '\n' && snlen > 0) setup_next();
        return;
    }
    if (k == '\n' && setup_next_en()) setup_next();
    else if (spage == 1 && k == ' ') sacc = !sacc;
    else if (spage == 3 && k >= '1' && k <= '5') schk[k - '1'] ^= 1;
}
static void setup_draw(void) {
    int ox = SDX + BORDER, oy = SDY + BORDER + TITLEH, cx0 = ox + 166, cy0 = oy + 16, by = oy + SCH - 34;
    vgrad(0, 0, SW, SH, 0x000040, 0x1030A0);
    text_s(20, 14, "MyOS Setup", C_WHITE, 2);
    text(22, 50, "Getting your computer ready", 0xA0B8FF);
    draw_frame(SDX, SDY, SDW, SDH, "MyOS Setup Wizard", 1, 0);
    set_clip(ox, oy, ox + SCW, oy + SCH);
    rect(ox, oy, SCW, SCH, C_GRAY);
    vgrad(ox + 8, oy + 8, 142, SCH - 60, 0x000070, 0x2090E0);
    text_s(ox + 14, oy + 22, "MyOS", C_WHITE, 3);
    text(ox + 16, oy + 76, "version 0.3", 0xC0D8FF);
    rect(ox + 8, oy + SCH - 44, SCW - 16, 1, C_DGRAY); rect(ox + 8, oy + SCH - 43, SCW - 16, 1, C_WHITE);
    button(ox + SCW - 260, by, 80, 26, "< Back", !setup_back_en());
    button(ox + SCW - 174, by, 80, 26, spage == 3 ? "Install" : spage == 5 ? "Finish" : "Next >", !setup_next_en());
    button(ox + SCW - 88, by, 80, 26, "Cancel", !setup_canc_en());

    if (spage == 0) {
        text_bold(cx0, cy0, "Welcome to MyOS Setup", C_BLACK);
        text(cx0, cy0 + 34, "This wizard installs MyOS on this", C_BLACK);
        text(cx0, cy0 + 52, "computer. It sets up the desktop, the", C_BLACK);
        text(cx0, cy0 + 70, "Start menu and the programs you pick.", C_BLACK);
        text(cx0, cy0 + 106, "Close other programs before you go on.", C_BLACK);
        text(cx0, cy0 + 142, "Click Next to continue.", C_BLACK);
    } else if (spage == 1) {
        text_bold(cx0, cy0, "License Agreement", C_BLACK);
        text(cx0, cy0 + 28, "Please read the following agreement:", C_BLACK);
        sunken_white(cx0, cy0 + 52, 356, 112);
        text(cx0 + 6, cy0 + 58,  "1. MyOS is a learning project and comes", C_BLACK);
        text(cx0 + 6, cy0 + 76,  "   with no warranty of any kind.", C_BLACK);
        text(cx0 + 6, cy0 + 96,  "2. You may read, change and share it.", C_BLACK);
        text(cx0 + 6, cy0 + 116, "3. If it crashes, you keep both pieces.", C_BLACK);
        text(cx0 + 6, cy0 + 136, "4. Have fun and keep building things.", C_BLACK);
        checkbox(cx0, cy0 + 180, sacc);
        text(cx0 + 22, cy0 + 178, "I accept the agreement", C_BLACK);
    } else if (spage == 2) {
        text_bold(cx0, cy0, "Personalize MyOS", C_BLACK);
        text(cx0, cy0 + 34, "Who will be using this computer?", C_BLACK);
        text(cx0, cy0 + 76, "Your name:", C_BLACK);
        sunken_white(cx0, cy0 + 96, 220, 24);
        text(cx0 + 5, cy0 + 100, uname_, C_BLACK);
        if ((ms / 500) & 1) rect(cx0 + 5 + snlen * 8, cy0 + 100, 2, 16, C_BLACK);
        text(cx0, cy0 + 136, "Computer name:", C_BLACK);
        sunken_white(cx0, cy0 + 156, 220, 24); text(cx0 + 5, cy0 + 160, "MYOS-PC", C_DGRAY);
        text(cx0, cy0 + 200, "Your name appears in the Start menu.", C_DGRAY);
    } else if (spage == 3) {
        text_bold(cx0, cy0, "Choose programs", C_BLACK);
        text(cx0, cy0 + 28, "Select the programs to install:", C_BLACK);
        int total = 0;
        for (int i = 0; i < NINST; i++) {
            int y = cy0 + 56 + i * 32;
            checkbox(cx0 + 4, y + 9, schk[i]);
            text_bold(cx0 + 30, y, pname[i], C_BLACK);
            text(cx0 + 30, y + 16, pdesc[i], C_DGRAY);
            char sz[16]; itoa_(psize[i], sz); int n = slen(sz); sz[n] = ' '; sz[n + 1] = 'K'; sz[n + 2] = 'B'; sz[n + 3] = 0;
            text(cx0 + 356 - slen(sz) * 8, y + 8, sz, C_BLACK);
            if (schk[i]) total += psize[i];
        }
        char b[40]; scpy(b, "Space required: ", 40); itoa_(total, b + 16); int n = slen(b); b[n] = ' '; b[n + 1] = 'K'; b[n + 2] = 'B'; b[n + 3] = 0;
        text_bold(cx0, cy0 + 224, b, C_BLACK);
        text(cx0, cy0 + 244, "More programs can be added later.", C_DGRAY);
    } else if (spage == 4) {
        text_bold(cx0, cy0, "Installing MyOS", C_BLACK);
        text(cx0, cy0 + 30, "Please wait while Setup copies files.", C_BLACK);
        int pct = (int)((ms - sbegin) * 100 / SETUP_MS); if (pct > 100) pct = 100;
        int fi = pct * NFILES / 100; if (fi >= NFILES) fi = NFILES - 1;
        text(cx0, cy0 + 70, "Copying:", C_DGRAY);
        text(cx0 + 72, cy0 + 70, sfiles[fi], C_BLACK);
        progress(cx0, cy0 + 100, 356, 24, pct);
        char b[8]; itoa_(pct, b); int n = slen(b); b[n] = '%'; b[n + 1] = 0;
        text_c(cx0, 356, cy0 + 132, b, C_BLACK);
    } else {
        text_bold(cx0, cy0, "Setup is complete", C_BLACK);
        text(cx0, cy0 + 34, "MyOS has been installed.", C_BLACK);
        char b[40]; scpy(b, "Welcome, ", 40); scpy(b + 9, uname_, 20); int n = slen(b); b[n] = '!'; b[n + 1] = 0;
        text_bold(cx0, cy0 + 70, b, C_NAVY);
        text(cx0, cy0 + 106, "Click Finish to start your desktop.", C_BLACK);
    }
    clip_all();
}

/* ================= desktop, taskbar, start menu ================= */
static int start_open = 0, sel_icon = -1;
static u32 last_click_ms = 0;
static const u32 acol[A_COUNT] = { 0xFFFFFF, 0x808080, 0xE02020, 0x000000, 0x30C030, 0xE0A020, 0x2080E0, 0xE02020 };

static int icon_list(int *out) {
    int n = 0;
    for (int i = 0; i < NINST; i++) if (pstate[i] == 2) out[n++] = i;
    out[n++] = A_STORE; return n;
}
static void draw_icons(void) {
    int ic[8], n = icon_list(ic);
    for (int k = 0; k < n; k++) {
        int x = 10, y = 12 + k * 80, t = ic[k];
        app_icon(t, x + 24, y + 4);
        const char *nm = t < NINST ? pname[t] : "Software";
        int tw = slen(nm) * 8, tx = x + (80 - tw) / 2;
        if (t == sel_icon) rect(tx - 2, y + 42, tw + 4, 20, C_NAVY);
        text(tx, y + 44, nm, C_WHITE);
    }
}

#define MI_SEP 99
static int mi_t[16], mi_y[16], mi_h[16], mi_n, mx0, my0, mw_, mh_;
static void menu_layout(void) {
    mi_n = 0;
    for (int i = 0; i < NINST; i++) if (pstate[i] == 2) mi_t[mi_n++] = i;
    mi_t[mi_n++] = A_STORE; mi_t[mi_n++] = MI_SEP; mi_t[mi_n++] = A_ABOUT; mi_t[mi_n++] = MI_SEP; mi_t[mi_n++] = A_POWER;
    int y = 0;
    for (int k = 0; k < mi_n; k++) { mi_y[k] = 40 + y; mi_h[k] = mi_t[k] == MI_SEP ? 8 : 28; y += mi_h[k]; }
    mw_ = 220; mh_ = 40 + y + 4; mx0 = 2; my0 = SH - TASKH - mh_ - 2;
}
static int menu_item_at(int x, int y) {
    for (int k = 0; k < mi_n; k++)
        if (mi_t[k] != MI_SEP && inrect(x, y, mx0 + 30, my0 + mi_y[k], mw_ - 34, mi_h[k])) return k;
    return -1;
}
static void menu_draw(void) {
    menu_layout();
    panel(mx0, my0, mw_, mh_, 1);
    vgrad(mx0 + 3, my0 + 3, 26, mh_ - 6, 0x000070, 0x2090E0);
    const char *lt = "MyOS";
    for (int i = 0; i < 4; i++) glyph_s(mx0 + 10, my0 + mh_ - 80 + i * 16, lt[i], C_WHITE, 1);
    text(mx0 + 38, my0 + 6, "Welcome,", C_DGRAY);
    text_bold(mx0 + 38, my0 + 20, uname_, C_BLACK);
    rect(mx0 + 32, my0 + 38, mw_ - 36, 1, C_DGRAY);
    int hov = menu_item_at(mx, my);
    for (int k = 0; k < mi_n; k++) {
        int y = my0 + mi_y[k];
        if (mi_t[k] == MI_SEP) { rect(mx0 + 32, y + 3, mw_ - 36, 1, C_DGRAY); rect(mx0 + 32, y + 4, mw_ - 36, 1, C_WHITE); continue; }
        if (k == hov) rect(mx0 + 30, y, mw_ - 34, mi_h[k], C_NAVY);
        u32 tc = k == hov ? C_WHITE : C_BLACK;
        rect(mx0 + 38, y + 8, 12, 12, C_BLACK); rect(mx0 + 39, y + 9, 10, 10, acol[mi_t[k]]);
        text(mx0 + 58, y + 6, mi_t[k] == A_POWER ? "Shut Down..." : apptitle(mi_t[k]), tc);
    }
}

static int tb_i[MAXW], tb_x[MAXW], tb_w, tb_n;
static void tb_layout(void) {
    tb_n = 0; for (int i = 0; i < MAXW; i++) if (wins[i].used) tb_i[tb_n++] = i;
    int avail = SW - 78 - 84; tb_w = tb_n ? avail / tb_n - 2 : 0; if (tb_w > 150) tb_w = 150;
    for (int k = 0; k < tb_n; k++) tb_x[k] = 78 + k * (tb_w + 2);
}
static void taskbar_draw(void) {
    int y = SH - TASKH;
    rect(0, y, SW, TASKH, C_GRAY); rect(0, y, SW, 1, C_LGRAY); rect(0, y + 1, SW, 1, C_WHITE);
    int sp = start_open;
    panel(3, y + 3, 66, 24, !sp);
    rect(9 + sp, y + 9 + sp, 6, 6, 0xE03030); rect(16 + sp, y + 9 + sp, 6, 6, 0x30B030);
    rect(9 + sp, y + 16 + sp, 6, 6, 0x3050E0); rect(16 + sp, y + 16 + sp, 6, 6, 0xE0C020);
    text_bold(27 + sp, y + 7 + sp, "Start", C_BLACK);
    tb_layout();
    for (int k = 0; k < tb_n; k++) {
        Win *w = &wins[tb_i[k]]; int act = (tb_i[k] == focus && !w->min);
        panel(tb_x[k], y + 3, tb_w, 24, !act);
        rect(tb_x[k] + 6 + act, y + 9 + act, 10, 10, acol[w->type]);
        int maxc = (tb_w - 28) / 8; char b[24]; scpy(b, apptitle(w->type), maxc < 22 ? maxc + 1 : 23);
        if (act) text_bold(tb_x[k] + 22, y + 7, b, C_BLACK); else text(tb_x[k] + 22, y + 7, b, C_BLACK);
    }
    panel(SW - 78, y + 3, 72, 24, 0);
    char b[8]; fmt_hm(b, &now_t);
    text_c(SW - 78, 72, y + 7, b, C_BLACK);
}
static void desktop_draw(void) {
    rect(0, 0, SW, SH - TASKH, C_TEAL);
    text_s(SW - 130, SH - TASKH - 56, "MyOS", 0x10A0A0, 3);
    draw_icons();
    for (int k = 0; k < nord; k++) {
        Win *w = &wins[order[k]]; if (w->min) continue;
        draw_frame(w->x, w->y, w->w, w->h, apptitle(w->type), order[k] == focus, 1);
        app_draw(w);
    }
    taskbar_draw();
    if (start_open) menu_draw();
}
static void render(void) { clip_all(); if (mode == M_SETUP) setup_draw(); else desktop_draw(); }

/* ================= mouse and keyboard events ================= */
static int win_at(int x, int y) {
    for (int k = nord - 1; k >= 0; k--) { Win *w = &wins[order[k]]; if (!w->min && inrect(x, y, w->x, w->y, w->w, w->h)) return order[k]; }
    return -1;
}
static void on_down(void) {
    dnx = mx; dny = my; dwin = -1; dirty = 1;
    if (mode == M_SETUP) return;
    menu_layout();
    int in_menu = start_open && inrect(mx, my, mx0, my0, mw_, mh_);
    int in_sbtn = inrect(mx, my, 3, SH - TASKH + 3, 66, 24);
    if (start_open && !in_menu && !in_sbtn) start_open = 0;
    if (in_menu || my >= SH - TASKH) return;
    int i = win_at(mx, my);
    if (i >= 0) {
        Win *w = &wins[i]; dwin = i; focus_win(i);
        if (my >= coy(w)) { cap = i; app_mouse(w, 1); }
        else if (my >= w->y + BORDER && my < w->y + BORDER + TITLEH && mx >= w->x + BORDER && mx < w->x + w->w - BORDER - 36) {
            drag = i; ddx = mx - w->x; ddy = my - w->y;
        }
    } else {
        int ic[8], n = icon_list(ic), hit = -1;
        for (int k = 0; k < n; k++) if (inrect(mx, my, 10, 12 + k * 80, 80, 64)) hit = ic[k];
        if (hit >= 0) {
            if (sel_icon == hit && ms - last_click_ms < 500) { open_app(hit); sel_icon = -1; }
            else { sel_icon = hit; last_click_ms = ms; }
        } else sel_icon = -1;
    }
}
static void on_up(void) {
    dirty = 1;
    if (mode == M_SETUP) { setup_click(); return; }
    drag = -1;
    if (cap >= 0) { app_mouse(&wins[cap], 2); cap = -1; }
    menu_layout();
    if (start_open) {
        int k = menu_item_at(mx, my);
        if (k >= 0 && k == menu_item_at(dnx, dny)) { int t = mi_t[k]; start_open = 0; open_app(t); return; }
    }
    if (HITR(3, SH - TASKH + 3, 66, 24)) { start_open = !start_open; return; }
    tb_layout();
    for (int k = 0; k < tb_n; k++) if (HITR(tb_x[k], SH - TASKH + 3, tb_w, 24)) {
        int i = tb_i[k];
        if (wins[i].min) focus_win(i); else if (focus == i) minimize_win(i); else focus_win(i);
        return;
    }
    if (dwin >= 0 && wins[dwin].used && win_at(mx, my) == dwin) {
        Win *w = &wins[dwin];
        int bx = w->x + w->w - BORDER - 18, by = w->y + BORDER + 2;
        if (HITR(bx, by, 16, 14)) { close_win(dwin); return; }
        if (HITR(bx - 18, by, 16, 14)) { minimize_win(dwin); return; }
        if (my >= coy(w)) app_click(w);
    }
}
static void on_move(void) {
    if (mode == M_SETUP) { if (mb & 1) dirty = 1; return; }
    if (start_open) dirty = 1;
    if (drag >= 0 && (mb & 1)) {
        Win *w = &wins[drag];
        w->x = mx - ddx; w->y = my - ddy;
        if (w->y < 0) w->y = 0; if (w->y > SH - TASKH - 24) w->y = SH - TASKH - 24;
        if (w->x < 80 - w->w) w->x = 80 - w->w; if (w->x > SW - 80) w->x = SW - 80;
        dirty = 1;
    } else if (cap >= 0) { app_mouse(&wins[cap], 0); dirty = 1; }
    else if (mb & 1) dirty = 1;
}
static void gui_mouse(int dx, int dy, int nb) {
    if (dx || dy) {
        mx += dx; my += dy;
        if (mx < 0) mx = 0; if (mx >= SW) mx = SW - 1; if (my < 0) my = 0; if (my >= SH) my = SH - 1;
        curdirty = 1; on_move();
    }
    int old = mb; mb = nb;
    if (nb & ~old) on_down();
    if (old & ~nb) on_up();
}
static void gui_key(int k) {
    dirty = 1;
    if (mode == M_SETUP) { setup_key(k); return; }
    if (start_open) { if (k == K_ESC) start_open = 0; return; }
    if (focus >= 0 && wins[focus].used) app_key(&wins[focus], k);
}

/* ---- PS/2 keyboard decoding ---- */
static const char kmap[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};
static const char ksmap[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};
static int kshift = 0, kext = 0;
static void kbd_byte(u8 sc) {
    if (sc == 0xE0) { kext = 1; return; }
    int e = kext; kext = 0;
    if (sc & 0x80) { u8 r = sc & 0x7F; if (!e && (r == 0x2A || r == 0x36)) kshift = 0; return; }
    if (e) {
        switch (sc) {
            case 0x48: gui_key(K_UP); break;
            case 0x50: gui_key(K_DOWN); break;
            case 0x4B: gui_key(K_LEFT); break;
            case 0x4D: gui_key(K_RIGHT); break;
        }
        return;
    }
    if (sc == 0x2A || sc == 0x36) { kshift = 1; return; }
    char c = kshift ? ksmap[sc] : kmap[sc];
    if (c) gui_key((u8)c);
}

/* ---- PS/2 mouse ---- */
static int mpk[3], mpi = 0;
static void mouse_byte(u8 d) {
    if (mpi == 0 && !(d & 8)) return;                   /* out of sync: wait for a first byte */
    mpk[mpi++] = d;
    if (mpi < 3) return;
    mpi = 0;
    int b = mpk[0];
    if (b & 0xC0) return;                               /* overflow */
    int dx = mpk[1] - ((b << 4) & 0x100), dy = mpk[2] - ((b << 3) & 0x100);
    gui_mouse(dx, -dy, b & 3);
}
static void poll_input(void) {
    u8 st; int guard = 0;
    while (((st = inb(0x64)) & 1) && guard++ < 64) {
        u8 d = inb(0x60);
        if (st & 0x20) mouse_byte(d); else kbd_byte(d);
    }
}
static void kbc_wait_in(void)  { for (int t = 100000; t && (inb(0x64) & 2); t--) ; }
static void kbc_wait_out(void) { for (int t = 100000; t && !(inb(0x64) & 1); t--) ; }
static void mouse_cmd(u8 c) { kbc_wait_in(); outb(0x64, 0xD4); kbc_wait_in(); outb(0x60, c); kbc_wait_out(); if (inb(0x64) & 1) inb(0x60); }
static void mouse_init(void) {
    while (inb(0x64) & 1) inb(0x60);
    kbc_wait_in(); outb(0x64, 0xA8);                    /* enable the second (mouse) port */
    kbc_wait_in(); outb(0x64, 0x20); kbc_wait_out(); u8 cfg = inb(0x60);
    cfg |= 2; cfg &= ~0x20;
    kbc_wait_in(); outb(0x64, 0x60); kbc_wait_in(); outb(0x60, cfg);
    mouse_cmd(0xF6);                                    /* defaults */
    mouse_cmd(0xF4);                                    /* start streaming movement packets */
}

/* ---- mouse pointer ---- */
static const char *curs[19] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
    "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ",
    "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     X..X   ", "      X..X  ", "      X..X  ",
    "       XX   "
};
static void present(void) {
    u32 *d = fb; const u32 *s = back; u32 n = SW * SH;
    __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
}
static void draw_cursor(void) {
    for (int r = 0; r < 19; r++) for (int c = 0; c < 12; c++) {
        char ch = curs[r][c]; int x = mx + c, y = my + r;
        if ((ch == 'X' || ch == '.') && x < SW && y < SH) fb[y * SW + x] = ch == 'X' ? 0x000000 : 0xFFFFFF;
    }
}

/* ================= main loop ================= */
static int lastraw = -1, lastblink = -1;
static void gui_frame(void) {
    time_update();
    poll_input();
    int raw = cmos(0);
    if (raw != lastraw) { lastraw = raw; rtc_read(&now_t); dirty = 1; }
    /* installs in progress */
    for (int i = 0; i < NINST; i++) if (pstate[i] == 1) {
        if (ms - pstart[i] >= (u32)(INST_MS + i * 300)) pstate[i] = 2;
        dirty = 1;
    }
    /* snake */
    int sj = find_win(A_SNAKE);
    if (mode == M_DESK && sj >= 0 && !wins[sj].min && salive) {
        int iv = 140 - (sscore / 10) * 4; if (iv < 70) iv = 70;
        if (ms - sstep >= (u32)iv) { sstep = ms; snake_step(); dirty = 1; }
    }
    /* setup progress */
    if (mode == M_SETUP && spage == 4) {
        int pct = (int)((ms - sbegin) * 100 / SETUP_MS); if (pct > 100) pct = 100;
        if (pct != slastpct) { slastpct = pct; dirty = 1; }
        if (pct >= 100) { for (int i = 0; i < NINST; i++) pstate[i] = schk[i] ? 2 : 0; spage = 5; dirty = 1; }
    }
    /* blinking caret */
    int bl = (ms / 500) & 1;
    if (bl != lastblink) {
        lastblink = bl;
        if ((mode == M_SETUP && spage == 2) ||
            (mode == M_DESK && focus >= 0 && (wins[focus].type == A_NOTE || wins[focus].type == A_TERM))) dirty = 1;
    }
    if (close_req >= 0) { int j = find_win(close_req); if (j >= 0) close_win(j); close_req = -1; }
    if (dirty) { render(); present(); draw_cursor(); dirty = 0; curdirty = 0; }
    else if (curdirty) { present(); draw_cursor(); curdirty = 0; }
}
static void gui_boot(void) {
    rtc_read(&now_t);
    paint_clear(); t_clear();
    t_puts("MyOS 0.3 shell. Type 'help' for commands.\n"); t_prompt();
    snlen = slen(uname_); mode = M_SETUP; spage = 0; dirty = 1;
}

int gui_init(void) {
    outw(0x1CE, 0);
    u16 id = inw(0x1CF);
    if (id < 0xB0C0 || id > 0xB0CF) return 0;           /* no Bochs/QEMU VBE adapter: stay in text mode */
    u32 addr = 0;
    for (int slot = 0; slot < 32; slot++) {             /* find the adapter on the PCI bus to get its framebuffer */
        outl(0xCF8, 0x80000000u | ((u32)slot << 11));
        if (inl(0xCFC) == 0x11111234u) {
            outl(0xCF8, 0x80000000u | ((u32)slot << 11) | 0x10);
            addr = inl(0xCFC) & 0xFFFFFFF0u; break;
        }
    }
    if (!addr) return 0;
    outw(0x1CE, 4); outw(0x1CF, 0);                     /* disable while changing mode */
    outw(0x1CE, 1); outw(0x1CF, SW);
    outw(0x1CE, 2); outw(0x1CF, SH);
    outw(0x1CE, 3); outw(0x1CF, 32);
    outw(0x1CE, 4); outw(0x1CF, 0x41);                  /* enabled + linear framebuffer */
    outw(0x1CE, 1); if (inw(0x1CF) != SW) return 0;
    fb = (u32 *)addr;
    pit_init();
    mouse_init();
    return 1;
}
void gui_run(void) {
    gui_boot();
    for (;;) { gui_frame(); __asm__ volatile("pause"); }
}
