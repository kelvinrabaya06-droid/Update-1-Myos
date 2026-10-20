/* MyOS 0.2 - a tiny bare-metal x86 kernel with a text-mode desktop. */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ---------- port I/O ---------- */
static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port)); }
static inline u8 inb(u16 port) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }

/* ---------- VGA text-mode drawing primitives ---------- */
#define VGA ((volatile u16 *)0xB8000)
#define COLS 80
#define ROWS 25

static void cell(int x, int y, u8 c, u8 a) {
    if (x < 0 || y < 0 || x >= COLS || y >= ROWS) return;
    VGA[y * COLS + x] = ((u16)a << 8) | c;
}
static void fill(int x, int y, int w, int h, u8 c, u8 a) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) cell(x + i, y + j, c, a);
}
static int kstrlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void text(int x, int y, const char *s, u8 a) { while (*s) cell(x++, y, (u8)*s++, a); }
static void text_c(int x, int w, int y, const char *s, u8 a) { text(x + (w - kstrlen(s)) / 2, y, s, a); }

/* double-line box (code page 437 box characters), interior filled with spaces */
static void box(int x, int y, int w, int h, u8 a) {
    fill(x, y, w, h, ' ', a);
    for (int i = 1; i < w - 1; i++) { cell(x + i, y, 0xCD, a); cell(x + i, y + h - 1, 0xCD, a); }
    for (int j = 1; j < h - 1; j++) { cell(x, y + j, 0xBA, a); cell(x + w - 1, y + j, 0xBA, a); }
    cell(x, y, 0xC9, a);         cell(x + w - 1, y, 0xBB, a);
    cell(x, y + h - 1, 0xC8, a); cell(x + w - 1, y + h - 1, 0xBC, a);
}

static void dim(int x, int y) {
    if (x < 0 || y < 0 || x >= COLS || y >= ROWS) return;
    VGA[y * COLS + x] = (VGA[y * COLS + x] & 0x00FF) | (0x08 << 8);
}
static void shadow(int x, int y, int w, int h) {
    for (int j = 1; j <= h; j++) { dim(x + w, y + j); dim(x + w + 1, y + j); }
    for (int i = 2; i <= w + 1; i++) dim(x + i, y + h);
}

static void hw_cursor(int x, int y) {
    u16 pos = y * COLS + x;
    outb(0x3D4, 14); outb(0x3D5, pos >> 8);
    outb(0x3D4, 15); outb(0x3D5, pos & 0xFF);
}
static void cursor_show(int on) {
    outb(0x3D4, 0x0A); outb(0x3D5, on ? 0x0E : 0x20);
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);
}

/* ---------- themes ---------- */
typedef struct { const char *name; u8 desk, bar, win, title, sel, edit, term; } Theme;
static const Theme themes[] = {
    { "Classic Blue", 0x19, 0x70, 0x70, 0x1F, 0x2F, 0x1F, 0x0F },
    { "Matrix",       0x02, 0x20, 0x0A, 0x2F, 0x20, 0x0A, 0x0A },
    { "Sunset",       0x4C, 0x6F, 0x70, 0x4F, 0x6F, 0x6F, 0x0E },
    { "Mono",         0x07, 0x70, 0x07, 0x70, 0x70, 0x07, 0x07 },
};
#define NTHEMES 4
static int theme = 0;
#define TH (&themes[theme])

/* ---------- keyboard (PS/2, polling, scancode set 1, US layout) ---------- */
#define K_ESC   27
#define K_UP    0x101
#define K_DOWN  0x102
#define K_LEFT  0x103
#define K_RIGHT 0x104

static const char map[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};
static const char smap[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};
static int shift = 0, ext = 0;

/* returns a key code, or -1 if nothing (useful) is waiting */
static int poll_key(void) {
    if (!(inb(0x64) & 1)) return -1;
    u8 sc = inb(0x60);
    if (sc == 0xE0) { ext = 1; return -1; }
    int e = ext; ext = 0;
    if (sc & 0x80) {                                   /* key release */
        u8 r = sc & 0x7F;
        if (!e && (r == 0x2A || r == 0x36)) shift = 0;
        return -1;
    }
    if (e) {
        switch (sc) {
            case 0x48: return K_UP;
            case 0x50: return K_DOWN;
            case 0x4B: return K_LEFT;
            case 0x4D: return K_RIGHT;
            default:   return -1;
        }
    }
    if (sc == 0x2A || sc == 0x36) { shift = 1; return -1; }
    char c = shift ? smap[sc] : map[sc];
    return c ? (u8)c : -1;
}
static int get_key(void) {
    int k;
    while ((k = poll_key()) < 0) __asm__ volatile("pause");
    return k;
}

/* ---------- real-time clock (CMOS) ---------- */
typedef struct { int h, m, s, d, mo, y; } Time;
static u8 cmos(u8 r) { outb(0x70, r); return inb(0x71); }
static int bcd(int v) { return (v & 15) + (v >> 4) * 10; }

static void rtc_read(Time *t) {
    while (cmos(0x0A) & 0x80) ;                        /* wait out an update */
    int s = cmos(0), m = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9);
    int b = cmos(0x0B);
    int pm = h & 0x80; h &= 0x7F;
    if (!(b & 4)) { s = bcd(s); m = bcd(m); h = bcd(h); d = bcd(d); mo = bcd(mo); y = bcd(y); }
    if (!(b & 2)) { h %= 12; if (pm) h += 12; }        /* 12-hour mode -> 24-hour */
    t->s = s; t->m = m; t->h = h; t->d = d; t->mo = mo; t->y = y;
}
static void two(char *o, int n) { o[0] = '0' + n / 10; o[1] = '0' + n % 10; }
static void fmt_time(char *o, const Time *t) {
    two(o, t->h); o[2] = ':'; two(o + 3, t->m); o[5] = ':'; two(o + 6, t->s); o[8] = 0;
}
static void fmt_date(char *o, const Time *t) {
    int y = 2000 + t->y;
    o[0] = '0' + y / 1000; o[1] = '0' + (y / 100) % 10; o[2] = '0' + (y / 10) % 10; o[3] = '0' + y % 10;
    o[4] = '-'; two(o + 5, t->mo); o[7] = '-'; two(o + 8, t->d); o[10] = 0;
}

/* ---------- scrolling console confined to a rectangle (used by the terminal) ---------- */
static int rx = 0, ry = 0, rw = COLS, rh = ROWS, cx = 0, cy = 0;
static u8 cattr = 0x07;

static void con_cursor(void) { hw_cursor(rx + cx, ry + cy); }
static void con_clear(void) { fill(rx, ry, rw, rh, ' ', cattr); cx = cy = 0; con_cursor(); }
static void con_scroll(void) {
    for (int y = 1; y < rh; y++)
        for (int x = 0; x < rw; x++)
            VGA[(ry + y - 1) * COLS + rx + x] = VGA[(ry + y) * COLS + rx + x];
    fill(rx, ry + rh - 1, rw, 1, ' ', cattr);
    cy = rh - 1;
}
static void kputc(char c) {
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\b') { if (cx > 0) { cx--; cell(rx + cx, ry + cy, ' ', cattr); } }
    else { cell(rx + cx, ry + cy, (u8)c, cattr); if (++cx >= rw) { cx = 0; cy++; } }
    if (cy >= rh) con_scroll();
    con_cursor();
}
static void kputs(const char *s) { while (*s) kputc(*s++); }

/* ---------- small helpers ---------- */
static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
static int starts(const char *s, const char *p) {
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}
static void reboot(void) {
    while (inb(0x64) & 2) ;
    outb(0x64, 0xFE);                                  /* pulse the CPU reset line */
    for (;;) __asm__ volatile("hlt");
}
static void halt_screen(void) {
    cursor_show(0);
    fill(0, 0, COLS, ROWS, ' ', 0x07);
    text_c(0, COLS, 12, "It is now safe to turn off your computer.", 0x07);
    for (;;) __asm__ volatile("cli; hlt");
}

/* ---------- windows and the desktop ---------- */
static void open_window(int x, int y, int w, int h, const char *title) {
    const Theme *t = TH;
    shadow(x, y, w, h);
    box(x, y, w, h, t->win);
    fill(x, y, w, 1, ' ', t->title);                   /* title bar */
    text(x + 2, y, title, t->title);
    text(x + w - 7, y, "[Esc]", t->title);
}

static const char *app_glyph[] = { ">_", "[=]", "(:)", "#%#", "(i)", "(o)" };
static const char *app_name[]  = { "Terminal", "Notepad", "Clock", "Themes", "About", "Power" };
#define NAPPS 6
static int selicon = 0;

static void draw_icon(int i) {
    const Theme *t = TH;
    int x = 10 + (i % 3) * 22, y = 5 + (i / 3) * 7;
    u8 a = (i == selicon) ? t->sel : t->win;
    shadow(x, y, 16, 4);
    box(x, y, 16, 4, a);
    text_c(x, 16, y + 1, app_glyph[i], a);
    text_c(x, 16, y + 2, app_name[i], a);
}

static void draw_clock(void) {
    Time t; char b[16];
    rtc_read(&t); fmt_time(b + 1, &t);
    b[0] = ' '; b[9] = ' '; b[10] = 0;
    text(COLS - 10, 0, b, TH->bar);
}

static void draw_desktop(void) {
    const Theme *t = TH;
    fill(0, 1, COLS, ROWS - 2, 0xB0, t->desk);
    fill(0, 0, COLS, 1, ' ', t->bar);
    text(1, 0, "\x04 MyOS", t->bar);
    fill(0, ROWS - 1, COLS, 1, ' ', t->bar);
    text(1, ROWS - 1, "Arrows: select   Enter: open   1-6: quick open", t->bar);
    text(COLS - 2 - kstrlen(t->name), ROWS - 1, t->name, t->bar);
    text(8, 2, " Welcome to MyOS - pick an app below ", t->title);
    for (int i = 0; i < NAPPS; i++) draw_icon(i);
    draw_clock();
}

/* ---------- app: terminal / shell ---------- */
static u8 tcolor = 0;                                  /* 0 = use the theme's terminal color */

static int run(const char *line) {
    if (!*line) return 0;
    if (!kstrcmp(line, "help")) {
        kputs("Commands:\n"
              "  help          show this list\n"
              "  clear         clear the terminal\n"
              "  echo <text>   print text\n"
              "  about         about MyOS\n"
              "  cpu           show CPU vendor\n"
              "  time          show date and time (RTC)\n"
              "  color <0-F>   set text color\n"
              "  exit          back to the desktop\n"
              "  reboot        restart the machine\n"
              "  halt          stop the CPU\n");
    }
    else if (!kstrcmp(line, "clear")) con_clear();
    else if (starts(line, "echo ")) { kputs(line + 5); kputc('\n'); }
    else if (!kstrcmp(line, "about")) kputs("MyOS 0.2 - a 32-bit x86 kernel written from scratch.\n");
    else if (!kstrcmp(line, "cpu")) {
        u32 b, c, d;
        __asm__ volatile("cpuid" : "=b"(b), "=c"(c), "=d"(d) : "a"(0));
        char v[13];
        *(u32 *)&v[0] = b; *(u32 *)&v[4] = d; *(u32 *)&v[8] = c; v[12] = 0;
        kputs("CPU vendor: "); kputs(v); kputc('\n');
    }
    else if (!kstrcmp(line, "time")) {
        Time t; char a[16], b[16];
        rtc_read(&t); fmt_date(a, &t); fmt_time(b, &t);
        kputs(a); kputc(' '); kputs(b); kputc('\n');
    }
    else if (starts(line, "color ")) {
        char h = line[6]; int n = -1;
        if (h >= '0' && h <= '9') n = h - '0';
        else if (h >= 'a' && h <= 'f') n = h - 'a' + 10;
        else if (h >= 'A' && h <= 'F') n = h - 'A' + 10;
        if (n < 0) kputs("usage: color <0-F>\n");
        else { tcolor = n ? n : 7; cattr = tcolor; con_clear(); }
    }
    else if (!kstrcmp(line, "exit")) return 1;
    else if (!kstrcmp(line, "reboot")) reboot();
    else if (!kstrcmp(line, "halt")) halt_screen();
    else { kputs("unknown command: "); kputs(line); kputc('\n'); }
    return 0;
}

static void app_terminal(void) {
    open_window(4, 2, 72, 21, " Terminal ");
    rx = 5; ry = 3; rw = 70; rh = 19;
    cattr = tcolor ? tcolor : TH->term;
    cursor_show(1);
    con_clear();
    kputs("MyOS shell. Type 'help' for commands, 'exit' to return to the desktop.\n\n");
    char buf[128]; int len, quit = 0;
    while (!quit) {
        kputs("myos> "); len = 0;
        for (;;) {
            int c = get_key();
            if (c == K_ESC) { quit = 1; break; }
            if (c == '\n') { kputc('\n'); break; }
            if (c == '\b') { if (len > 0) { len--; kputc('\b'); } continue; }
            if (c >= 32 && c < 127 && len < 60) { buf[len++] = c; kputc(c); }
        }
        if (quit) break;
        buf[len] = 0;
        if (run(buf)) break;
    }
    cursor_show(0);
}

/* ---------- app: notepad (text stays while the OS is running) ---------- */
#define NW 66
#define NH 17
static char note[NH][NW];
static int ncx = 0, ncy = 0, ninit = 0;

static void app_notes(void) {
    u8 a = TH->edit;
    if (!ninit) { for (int y = 0; y < NH; y++) for (int x = 0; x < NW; x++) note[y][x] = ' '; ninit = 1; }
    open_window(6, 2, 68, 21, " Notepad ");
    for (int y = 0; y < NH; y++) for (int x = 0; x < NW; x++) cell(7 + x, 3 + y, note[y][x], a);
    text(8, 21, " Type to write.  Arrows move.  Esc closes. ", TH->title);
    cursor_show(1); hw_cursor(7 + ncx, 3 + ncy);
    for (;;) {
        int k = get_key();
        if (k == K_ESC) break;
        if (k == K_LEFT)       { if (ncx > 0) ncx--; }
        else if (k == K_RIGHT) { if (ncx < NW - 1) ncx++; }
        else if (k == K_UP)    { if (ncy > 0) ncy--; }
        else if (k == K_DOWN)  { if (ncy < NH - 1) ncy++; }
        else if (k == '\n')    { ncx = 0; if (ncy < NH - 1) ncy++; }
        else if (k == '\b') {
            if (ncx > 0) ncx--; else if (ncy > 0) { ncy--; ncx = NW - 1; }
            note[ncy][ncx] = ' '; cell(7 + ncx, 3 + ncy, ' ', a);
        }
        else if (k >= 32 && k < 127) {
            note[ncy][ncx] = k; cell(7 + ncx, 3 + ncy, k, a);
            if (ncx < NW - 1) ncx++; else if (ncy < NH - 1) { ncx = 0; ncy++; }
        }
        hw_cursor(7 + ncx, 3 + ncy);
    }
    cursor_show(0);
}

/* ---------- app: clock ---------- */
static void app_clock(void) {
    open_window(20, 8, 40, 9, " Clock ");
    text_c(21, 38, 15, "Esc closes", TH->win);
    int last = -1;
    for (;;) {
        int raw = cmos(0);
        if (raw != last) {
            last = raw;
            Time t; char b[16];
            rtc_read(&t);
            fmt_time(b, &t); text_c(21, 38, 11, b, TH->sel);
            fmt_date(b, &t); text_c(21, 38, 13, b, TH->win);
        }
        if (poll_key() == K_ESC) break;
        __asm__ volatile("pause");
    }
}

/* ---------- app: themes ---------- */
static void app_themes(void) {
    for (;;) {
        draw_desktop();
        open_window(24, 6, 32, 10, " Themes ");
        for (int i = 0; i < NTHEMES; i++) {
            u8 a = (i == theme) ? TH->sel : TH->win;
            fill(26, 8 + i, 28, 1, ' ', a);
            text(27, 8 + i, i == theme ? "> " : "  ", a);
            text(29, 8 + i, themes[i].name, a);
        }
        text(26, 14, "Up/Down: choose   Esc: done", TH->win);
        int k = get_key();
        if (k == K_ESC || k == '\n') break;
        if (k == K_UP)   theme = (theme + NTHEMES - 1) % NTHEMES;
        if (k == K_DOWN) theme = (theme + 1) % NTHEMES;
    }
}

/* ---------- app: about ---------- */
static void app_about(void) {
    open_window(17, 6, 46, 13, " About MyOS ");
    u8 a = TH->win;
    text(20, 8,  "MyOS 0.2", TH->sel);
    text(20, 9,  "A tiny 32-bit x86 operating system,", a);
    text(20, 10, "written from scratch.", a);
    text(20, 12, "  - Text-mode desktop and windows", a);
    text(20, 13, "  - PS/2 keyboard driver", a);
    text(20, 14, "  - Shell, notepad, real-time clock", a);
    text(20, 15, "  - 4 color themes", a);
    text(20, 17, "Press any key to close.", a);
    get_key();
}

/* ---------- app: power ---------- */
static void app_power(void) {
    open_window(20, 8, 40, 8, " Power ");
    text_c(21, 38, 11, "R = Reboot      H = Shut down", TH->win);
    text_c(21, 38, 13, "Esc = Cancel", TH->win);
    for (;;) {
        int k = get_key();
        if (k == 'r' || k == 'R') reboot();
        if (k == 'h' || k == 'H') halt_screen();
        if (k == K_ESC) break;
    }
}

static void launch(int i) {
    switch (i) {
        case 0: app_terminal(); break;
        case 1: app_notes();    break;
        case 2: app_clock();    break;
        case 3: app_themes();   break;
        case 4: app_about();    break;
        case 5: app_power();    break;
    }
}

extern int gui_init(void);
extern void gui_run(void);

void kernel_main(void) {
    if (gui_init()) gui_run();           /* graphical desktop; falls back to text mode if no VBE adapter */
    cursor_show(0);
    for (;;) {
        draw_desktop();
        int last = cmos(0);
        for (;;) {
            int raw = cmos(0);
            if (raw != last) { last = raw; draw_clock(); }
            int k = poll_key();
            if (k < 0) { __asm__ volatile("pause"); continue; }
            int old = selicon;
            int col = selicon % 3;
            if (k == K_LEFT  && col > 0)            selicon--;
            if (k == K_RIGHT && col < 2)            selicon++;
            if (k == K_UP    && selicon >= 3)       selicon -= 3;
            if (k == K_DOWN  && selicon < NAPPS - 3) selicon += 3;
            if (k >= '1' && k <= '0' + NAPPS) { selicon = k - '1'; launch(selicon); break; }
            if (k == '\n') { launch(selicon); break; }
            if (selicon != old) { draw_icon(old); draw_icon(selicon); }
        }
    }
}
