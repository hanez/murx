/*
 * This file is part of murx - An ultra-minimalist, single-file, terminal
 * multiplexer: https://codeberg.org/armin/murx
 *
 * Copyright 2026 Armin Jenewein <a@bsd.pm>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

/* Feature macros must come before any system header. */
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
/*
 * The BSDs expose flock(), usleep(), cfmakeraw() and SIGWINCH only when
 * __BSD_VISIBLE is set, which defining _XOPEN_SOURCE would disable.  Their
 * default environment already includes everything we use.
 */
#else
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

/*
 * murx - a very small terminal multiplexer
 *
 * One binary, no config file. Shortcuts and colours are hard-coded.
 *
 *   murx [session]     attach to session (default "0"), or start it
 *   murx -h|--help     show usage and all Ctrl-A shortcut chains
 *
 * Pure C99 + POSIX (macOS / Linux / FreeBSD).
 */

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

/* GCC 16 miscompiles grid_resize() at -O2 when it specializes the function
   interprocedurally: grid_init() gets a hard-coded 1x1 size, so pane grids
   collapse to one cell and shell prompts render one character at a time.
   Keep that function out of interprocedural optimization. Clang ignores it. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__has_attribute)
#if __has_attribute(noipa)
#define MURX_NOIPA __attribute__((noipa))
#else
#define MURX_NOIPA
#endif
#else
#define MURX_NOIPA
#endif

#define DEFAULT_PREFIX 0x01

/* client -> server */
enum { C_ATTACH = 1, C_INPUT = 2, C_RESIZE = 3 };
/* server -> client */
enum { S_DRAW = 4, S_DETACH = 5 };

typedef struct {
    uint32_t type;
    uint32_t len;
} MsgHdr;

static int send_msg(int fd, uint32_t type, const void *data, size_t len);

/* colour palette */
#define C_BLACK   0
#define C_CYAN    6
#define C_WHITE   7
#define C_GREY    8
#define C_RED     9
#define C_YELLOW  11
#define C_BLUE    12
#define C_CYAN2   14
#define C_BWHITE  15

/* Pane borders: thin box-drawing lines in a muted grey, drawn over the
   terminal's own background. */
#define C_BORDER  C_GREY

/* Status line palette, mirroring ~/.tmux.conf:
   status fg colour12, accent colour14, current window colour11,
   message style fg=colour6,bg=colour8. */
#define P_STATUS  C_BLUE
#define P_ACCENT  C_CYAN2
#define P_CURRENT C_YELLOW
#define P_MSG     C_CYAN
#define P_MSG_BG  C_GREY

#define DEFAULT_COLOR (-1)
#define RGB_FLAG 0x01000000
#define WIDE_CONT 0xFFFFFFFFu

#define MAX_DIMENSION 1000    /* resource policy, not a C type limit */
#define MAX_DRAW_BYTES (8u * 1024u * 1024u)

#define MIN_COLS 20
#define MIN_ROWS 5
#define MSG_TIME 3000
#define STATUS_INTERVAL 1
#define SIZE_CHECK_MS 250
#define NAME_CHECK_MS 1000

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */

static void range_error(void) {
    fputs("murx: numeric limit exceeded\n", stderr);
    exit(1);
}

static int count_add(int a, int b) {
    if (a < 0 || b < 0 || b > INT_MAX - a) range_error();
    return a + b;
}

static size_t size_add(size_t a, size_t b) {
    if (b > SIZE_MAX - a) range_error();
    return a + b;
}

static size_t size_mul(size_t a, size_t b) {
    if (a && b > SIZE_MAX / a) range_error();
    return a * b;
}

static size_t buffer_capacity(size_t cap, size_t need) {
    while (cap < need) {
        if (cap > SIZE_MAX / 2) return need;
        cap *= 2;
    }
    return cap;
}

static int clamp_int(int value, int lo, int hi) {
    return value < lo ? lo : value > hi ? hi : value;
}

/* Check the destination type before narrowing terminal dimensions. */
static unsigned short tty_dimension(int value) {
    if (value < 1) return 1;
    if ((unsigned int)value > USHRT_MAX) return USHRT_MAX;
    return (unsigned short)value;
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) {
        fputs("murx: out of memory\n", stderr);
        exit(1);
    }
    return p;
}

static void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(1, size_mul(n, sz));
    if (!p) {
        fputs("murx: out of memory\n", stderr);
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) {
        fputs("murx: out of memory\n", stderr);
        exit(1);
    }
    return q;
}

/* Parse unsigned decimal fields without scanf's out-of-range conversions. */
static int parse_ulong_field(const char **text, unsigned long *out) {
    const unsigned char *p = (const unsigned char *)*text;
    while (isspace(*p)) p++;
    if (*p < '0' || *p > '9') return 0;
    unsigned long value = 0;
    do {
        unsigned int digit = *p++ - '0';
        if (value > (ULONG_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    } while (*p >= '0' && *p <= '9');
    if (*p && !isspace(*p)) return 0;
    *out = value;
    *text = (const char *)p;
    return 1;
}

static long long now_ms(void) {
    struct timespec ts = { 0, 0 };
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        perror("murx: clock_gettime");
        exit(1);
    }
    if (ts.tv_sec < 0 || (uintmax_t)ts.tv_sec > (uintmax_t)(LLONG_MAX - MSG_TIME) / 1000 - 1)
        range_error();
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int unicode_scalar(uint32_t cp) {
    return cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
}

static int utf8_decode(const unsigned char *s, size_t n, uint32_t *cp) {
    if (!n) { *cp = 0xFFFD; return 0; }
    unsigned char b = s[0];
    if (b < 0x80) { *cp = b; return 1; }
    int len = b >= 0xC2 && b <= 0xDF ? 2 :
              b >= 0xE0 && b <= 0xEF ? 3 :
              b >= 0xF0 && b <= 0xF4 ? 4 : 0;
    if (!len || n < (size_t)len) { *cp = 0xFFFD; return 1; }
    uint32_t value = b & (0x7F >> len);
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        value = (value << 6) | (s[i] & 0x3F);
    }
    if (!unicode_scalar(value) || (len == 3 && value < 0x800) ||
        (len == 4 && value < 0x10000)) { *cp = 0xFFFD; return 1; }
    *cp = value;
    return len;
}

static int cp_width(uint32_t cp) {
    int w;
    if (cp == 0 || cp == WIDE_CONT) return 1;
    if (!unicode_scalar(cp) || (uintmax_t)cp > (uintmax_t)WCHAR_MAX) return 1;
    w = wcwidth((wchar_t)cp);
    if (w < 0) return 1;
    return w;
}

/* ------------------------------------------------------------------ */
/* cells and screens                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t ch;
    int32_t fg;
    int32_t bg;
    uint8_t attr;
} Cell;

#define A_BOLD   0x01
#define A_DIM    0x02
#define A_ITALIC 0x04
#define A_ULINE  0x08
#define A_REV    0x10
/* Internal marker: cell belongs to a murx border, not pane content.
   Never emitted to the terminal. */

static Cell blank_cell(void) {
    Cell c;
    c.ch = ' ';
    c.fg = DEFAULT_COLOR;
    c.bg = DEFAULT_COLOR;
    c.attr = 0;
    return c;
}

typedef struct {
    int w, h;
    Cell *c;
} Grid;

static void grid_init(Grid *g, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    /* Existing row offsets use int; prove their products fit as well. */
    if (w > INT_MAX / h) range_error();
    size_t bytes = size_mul(size_mul((size_t)w, (size_t)h), sizeof(Cell));
    g->w = w;
    g->h = h;
    g->c = xmalloc(bytes);
    for (int i = 0; i < w * h; i++) g->c[i] = blank_cell();
}

static void grid_free(Grid *g) {
    free(g->c);
    g->c = NULL;
    g->w = g->h = 0;
}

static Cell *grid_at(Grid *g, int x, int y) {
    if (x < 0 || y < 0 || x >= g->w || y >= g->h) return NULL;
    return &g->c[y * g->w + x];
}

static void grid_clear(Grid *g, int32_t bg) {
    for (int i = 0; i < g->w * g->h; i++) {
        g->c[i] = blank_cell();
        g->c[i].bg = bg;
    }
}

static void grid_clear_line(Grid *g, int y, int32_t bg) {
    if (y < 0 || y >= g->h) return;
    for (int x = 0; x < g->w; x++) {
        Cell *c = grid_at(g, x, y);
        *c = blank_cell();
        c->bg = bg;
    }
}

static void MURX_NOIPA grid_resize(Grid *g, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == g->w && h == g->h) return;
    Grid n;
    grid_init(&n, w, h);
    int cw = w < g->w ? w : g->w;
    int ch = h < g->h ? h : g->h;
    for (int y = 0; y < ch; y++) {
        memcpy(&n.c[y * w], &g->c[y * g->w], (size_t)cw * sizeof(Cell));
    }
    grid_free(g);
    *g = n;
}

typedef struct {
    int x, y;
} CursorPos;

/* ------------------------------------------------------------------ */
/* terminal state                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    int32_t fg, bg;
    uint8_t attr;
} Pen;

typedef enum {
    PS_GROUND = 0,
    PS_ESC,
    PS_CSI,
    PS_OSC,
    PS_OSC_ESC,
    PS_CHARSET,
    PS_DCS,
    PS_DCS_ESC
} ParseState;

#define HISTORY_LINES 2000

typedef struct Pane Pane;

struct Pane {
    int fd;
    pid_t pid;
    int alive;

    /* layout / screen placement (content area, border row is y-1) */
    int x, y, w, h;

    Grid main_grid, alt_grid;
    Grid *grid;
    int on_alt;
    Grid *history;         /* ring of rows, retaining their original widths */
    int history_start, history_count;

    int cx, cy;
    int wrap_pending;
    int scroll_top, scroll_bot;
    int cursor_visible;
    int app_cursor;        /* DEC private mode 1: application cursor keys */
    int insert_mode;

    Pen pen;
    int saved_x, saved_y;
    Pen saved_pen;
    int main_x, main_y;
    Pen main_pen;

    ParseState pstate;
    char seq[PATH_MAX * 3 + 256];
    int seqlen;

    int u8_need, u8_have;
    uint32_t u8_cp;

    char title[128];
    pid_t title_pgrp;       /* foreground job that supplied the OSC title */
    char remote_title[128];
    pid_t remote_title_pgrp;
    char reported_cwd[PATH_MAX];
    pid_t reported_cwd_pgrp;
    int reported_root;
    pid_t reported_root_pgrp;
    int id;
    int tty_fd;

    Pane *next;
};

static void pane_write(Pane *p, const void *buf, size_t len) {
    const char *s = buf;
    while (len > 0 && p->fd >= 0) {
        ssize_t n = write(p->fd, s, len > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : len);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd;
                pfd.fd = p->fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                if (poll(&pfd, 1, 20) > 0) continue;
            }
            return;
        }
        if (n == 0) return;
        s += n;
        len -= (size_t)n;
    }
}

/* forward declarations for the emulator */
static void em_scroll_up(Pane *p, int n);
static void em_scroll_down(Pane *p, int n);
static void em_set_alt(Pane *p, int on);

static void pane_lf(Pane *p) {
    p->wrap_pending = 0;
    if (p->cy == p->scroll_bot) {
        em_scroll_up(p, 1);
    } else if (p->cy < p->grid->h - 1) {
        p->cy++;
    }
}

static void pane_put_cp(Pane *p, uint32_t cp) {
    if (!unicode_scalar(cp)) cp = 0xFFFD;
    int w = cp_width(cp);
    Grid *g = p->grid;
    if (w == 0) return;                /* combining mark: ignore */
    if (w > 2) w = 1;
    if (p->wrap_pending) {
        p->cx = 0;
        pane_lf(p);
    }
    if (p->cx + w > g->w) {
        p->cx = 0;
        pane_lf(p);
    }
    if (p->insert_mode && p->cx + w < g->w) {
        int shift = g->w - p->cx - w;
        Cell *row = &g->c[p->cy * g->w];
        memmove(&row[p->cx + w], &row[p->cx], (size_t)shift * sizeof(Cell));
    }
    Cell c;
    c.ch = cp;
    c.fg = p->pen.fg;
    c.bg = p->pen.bg;
    c.attr = p->pen.attr;
    Cell *at = grid_at(g, p->cx, p->cy);
    if (at) *at = c;
    if (w == 2) {
        Cell *cont = grid_at(g, p->cx + 1, p->cy);
        if (cont) {
            cont->ch = WIDE_CONT;
            cont->fg = c.fg;
            cont->bg = c.bg;
            cont->attr = c.attr;
        }
    }
    p->cx += w;
    if (p->cx >= g->w) {
        p->cx = g->w - 1;
        p->wrap_pending = 1;
    }
}

static void pane_cr(Pane *p) {
    p->cx = 0;
    p->wrap_pending = 0;
}

static void pane_bs(Pane *p) {
    if (p->cx > 0) p->cx--;
    p->wrap_pending = 0;
}

static void pane_tab(Pane *p) {
    int nx = (p->cx / 8 + 1) * 8;
    if (nx >= p->grid->w) nx = p->grid->w - 1;
    p->cx = nx;
    p->wrap_pending = 0;
}

static void pane_erase_cell(Pane *p, int x, int y) {
    Cell *c = grid_at(p->grid, x, y);
    if (!c) return;
    *c = blank_cell();
    c->bg = p->pen.bg;
}

static void em_scroll_up(Pane *p, int n) {
    Grid *g = p->grid;
    if (n < 1) n = 1;
    if (n > p->scroll_bot - p->scroll_top + 1) n = p->scroll_bot - p->scroll_top + 1;
    /* Only normal full-screen scrolling contributes shell history. */
    if (!p->on_alt && p->scroll_top == 0 && p->scroll_bot == g->h - 1) {
        if (!p->history) p->history = xcalloc(HISTORY_LINES, sizeof(Grid));
        for (int y = 0; y < n; y++) {
            int slot = (p->history_start + p->history_count) % HISTORY_LINES;
            if (p->history_count == HISTORY_LINES) {
                grid_free(&p->history[slot]);
                p->history_start = (p->history_start + 1) % HISTORY_LINES;
            } else p->history_count++;
            grid_init(&p->history[slot], g->w, 1);
            memcpy(p->history[slot].c, &g->c[y * g->w], (size_t)g->w * sizeof(Cell));
        }
    }
    for (int y = p->scroll_top; y <= p->scroll_bot - n; y++) {
        memcpy(&g->c[y * g->w], &g->c[(y + n) * g->w], (size_t)g->w * sizeof(Cell));
    }
    for (int y = p->scroll_bot - n + 1; y <= p->scroll_bot; y++) {
        grid_clear_line(g, y, p->pen.bg);
    }
}

static void em_scroll_down(Pane *p, int n) {
    Grid *g = p->grid;
    if (n < 1) n = 1;
    if (n > p->scroll_bot - p->scroll_top + 1) n = p->scroll_bot - p->scroll_top + 1;
    for (int y = p->scroll_bot; y >= p->scroll_top + n; y--) {
        memcpy(&g->c[y * g->w], &g->c[(y - n) * g->w], (size_t)g->w * sizeof(Cell));
    }
    for (int y = p->scroll_top; y < p->scroll_top + n; y++) {
        grid_clear_line(g, y, p->pen.bg);
    }
}

static void em_set_alt(Pane *p, int on) {
    if (on && !p->on_alt) {
        p->main_x = p->cx;
        p->main_y = p->cy;
        p->main_pen = p->pen;
        p->on_alt = 1;
        p->grid = &p->alt_grid;
        grid_clear(&p->alt_grid, DEFAULT_COLOR);
        p->cx = p->cy = 0;
        p->wrap_pending = 0;
        p->scroll_top = 0;
        p->scroll_bot = p->grid->h - 1;
    } else if (!on && p->on_alt) {
        p->on_alt = 0;
        p->grid = &p->main_grid;
        p->cx = p->main_x;
        p->cy = p->main_y;
        p->pen = p->main_pen;
        p->wrap_pending = 0;
        p->scroll_top = 0;
        p->scroll_bot = p->grid->h - 1;
        if (p->cx >= p->grid->w) p->cx = p->grid->w - 1;
        if (p->cy >= p->grid->h) p->cy = p->grid->h - 1;
    }
}

static void em_reset(Pane *p) {
    p->on_alt = 0;
    p->grid = &p->main_grid;
    grid_clear(&p->main_grid, DEFAULT_COLOR);
    grid_clear(&p->alt_grid, DEFAULT_COLOR);
    p->cx = p->cy = 0;
    p->wrap_pending = 0;
    p->scroll_top = 0;
    p->scroll_bot = p->grid->h - 1;
    p->pen.fg = DEFAULT_COLOR;
    p->pen.bg = DEFAULT_COLOR;
    p->pen.attr = 0;
    p->cursor_visible = 1;
    p->insert_mode = 0;
    p->app_cursor = 0;
    p->pstate = PS_GROUND;
}

/* ------------------------------------------------------------------ */
/* escape sequence handling                                           */
/* ------------------------------------------------------------------ */

static int parse_params(const char *s, int len, int *out, int max) {
    int n = 0;
    int val = -1;
    int any = 0;
    for (int i = 0; i < len && n < max; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == ';' || ch == ':') {
            out[n++] = any ? val : -1;
            val = -1;
            any = 0;
        } else if (ch >= '0' && ch <= '9') {
            if (!any) val = 0;
            int digit = ch - '0';
            val = val > (INT_MAX - digit) / 10 ? INT_MAX : val * 10 + digit;
            any = 1;
        } else {
            /* private markers / intermediates: ignore */
        }
    }
    if (n < max) out[n++] = any ? val : -1;
    return n;
}

static int param(int *p, int n, int i, int def) {
    if (i >= n || p[i] < 0) return def;
    return p[i];
}

static void em_sgr(Pane *p, int *ps, int n) {
    for (int i = 0; i < n; i++) {
        int v = ps[i];
        if (v < 0) v = 0;
        if (v == 0) {
            p->pen.fg = DEFAULT_COLOR;
            p->pen.bg = DEFAULT_COLOR;
            p->pen.attr = 0;
        } else if (v == 1) p->pen.attr |= A_BOLD;
        else if (v == 2) p->pen.attr |= A_DIM;
        else if (v == 3) p->pen.attr |= A_ITALIC;
        else if (v == 4) p->pen.attr |= A_ULINE;
        else if (v == 7) p->pen.attr |= A_REV;
        else if (v == 21) p->pen.attr |= A_ULINE;
        else if (v == 22) p->pen.attr &= ~(A_BOLD | A_DIM);
        else if (v == 23) p->pen.attr &= ~A_ITALIC;
        else if (v == 24) p->pen.attr &= ~A_ULINE;
        else if (v == 27) p->pen.attr &= ~A_REV;
        else if (v >= 30 && v <= 37) p->pen.fg = v - 30;
        else if (v == 39) p->pen.fg = DEFAULT_COLOR;
        else if (v >= 40 && v <= 47) p->pen.bg = v - 40;
        else if (v == 49) p->pen.bg = DEFAULT_COLOR;
        else if (v >= 90 && v <= 97) p->pen.fg = v - 90 + 8;
        else if (v >= 100 && v <= 107) p->pen.bg = v - 100 + 8;
        else if ((v == 38 || v == 48) && i + 1 < n) {
            int target = (v == 38) ? 0 : 1;   /* 0 fg, 1 bg */
            if (ps[i + 1] == 5 && i + 2 < n) {
                int idx = ps[i + 2];
                if (idx < 0) idx = 0;
                if (idx > 255) idx = 255;
                if (target == 0) p->pen.fg = idx;
                else p->pen.bg = idx;
                i += 2;
            } else if (ps[i + 1] == 2 && i + 4 < n) {
                int r = ps[i + 2], g = ps[i + 3], b = ps[i + 4];
                if (r < 0) r = 0;
                if (r > 255) r = 255;
                if (g < 0) g = 0;
                if (g > 255) g = 255;
                if (b < 0) b = 0;
                if (b > 255) b = 255;
                int32_t col = RGB_FLAG | (r << 16) | (g << 8) | b;
                if (target == 0) p->pen.fg = col;
                else p->pen.bg = col;
                i += 4;
            } else {
                i += 1;
            }
        }
    }
}

static void em_csi(Pane *p, const char *raw, int len, unsigned char final) {
    int priv = 0;
    int pn = 0;
    int ps[32];
    const char *s = raw;
    int slen = len;
    if (slen > 0 && (s[0] == '?' || s[0] == '>' || s[0] == '!')) {
        if (s[0] == '?') priv = 1;
        s++;
        slen--;
    }
    pn = parse_params(s, slen, ps, 32);
    Grid *g = p->grid;
    int n;

    switch (final) {
    case 'm':
        em_sgr(p, ps, pn);
        break;
    case 'H':
    case 'f':
        p->cy = param(ps, pn, 0, 1) - 1;
        p->cx = param(ps, pn, 1, 1) - 1;
        if (p->cy < 0) p->cy = 0;
        if (p->cy >= g->h) p->cy = g->h - 1;
        if (p->cx < 0) p->cx = 0;
        if (p->cx >= g->w) p->cx = g->w - 1;
        p->wrap_pending = 0;
        break;
    case 'A':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        p->cy -= n;
        if (p->cy < p->scroll_top) p->cy = p->scroll_top;
        p->wrap_pending = 0;
        break;
    case 'B':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        p->cy += n > g->h - 1 - p->cy ? g->h - 1 - p->cy : n;
        if (p->cy > p->scroll_bot) p->cy = p->scroll_bot;
        p->wrap_pending = 0;
        break;
    case 'C':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        p->cx += n > g->w - 1 - p->cx ? g->w - 1 - p->cx : n;
        if (p->cx >= g->w) p->cx = g->w - 1;
        p->wrap_pending = 0;
        break;
    case 'D':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        p->cx -= n;
        if (p->cx < 0) p->cx = 0;
        p->wrap_pending = 0;
        break;
    case 'E':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        p->cx = 0;
        /* Further blank scrolling cannot change the screen or history. */
        if (n > g->h + HISTORY_LINES) n = g->h + HISTORY_LINES;
        for (int i = 0; i < n; i++) pane_lf(p);
        break;
    case 'F':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        p->cx = 0;
        p->cy -= n;
        if (p->cy < p->scroll_top) p->cy = p->scroll_top;
        p->wrap_pending = 0;
        break;
    case 'G':
    case '`':
        p->cx = param(ps, pn, 0, 1) - 1;
        if (p->cx < 0) p->cx = 0;
        if (p->cx >= g->w) p->cx = g->w - 1;
        p->wrap_pending = 0;
        break;
    case 'd':
        p->cy = param(ps, pn, 0, 1) - 1;
        if (p->cy < 0) p->cy = 0;
        if (p->cy >= g->h) p->cy = g->h - 1;
        p->wrap_pending = 0;
        break;
    case 'J': {
        int mode = param(ps, pn, 0, 0);
        if (mode == 0) {
            for (int x = p->cx; x < g->w; x++) pane_erase_cell(p, x, p->cy);
            for (int y = p->cy + 1; y < g->h; y++) grid_clear_line(g, y, p->pen.bg);
        } else if (mode == 1) {
            for (int y = 0; y < p->cy; y++) grid_clear_line(g, y, p->pen.bg);
            for (int x = 0; x <= p->cx && x < g->w; x++) pane_erase_cell(p, x, p->cy);
        } else {
            grid_clear(g, p->pen.bg);
        }
        p->wrap_pending = 0;
        break;
    }
    case 'K': {
        int mode = param(ps, pn, 0, 0);
        if (mode == 0) {
            for (int x = p->cx; x < g->w; x++) pane_erase_cell(p, x, p->cy);
        } else if (mode == 1) {
            for (int x = 0; x <= p->cx && x < g->w; x++) pane_erase_cell(p, x, p->cy);
        } else {
            grid_clear_line(g, p->cy, p->pen.bg);
        }
        p->wrap_pending = 0;
        break;
    }
    case 'L':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        if (p->cy >= p->scroll_top && p->cy <= p->scroll_bot) {
            if (n > p->scroll_bot - p->cy + 1) n = p->scroll_bot - p->cy + 1;
            for (int i = 0; i < n; i++) {
                for (int y = p->scroll_bot; y > p->cy; y--) {
                    memcpy(&g->c[y * g->w], &g->c[(y - 1) * g->w], (size_t)g->w * sizeof(Cell));
                }
                grid_clear_line(g, p->cy, p->pen.bg);
            }
        }
        break;
    case 'M':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        if (p->cy >= p->scroll_top && p->cy <= p->scroll_bot) {
            if (n > p->scroll_bot - p->cy + 1) n = p->scroll_bot - p->cy + 1;
            for (int i = 0; i < n; i++) {
                for (int y = p->cy; y < p->scroll_bot; y++) {
                    memcpy(&g->c[y * g->w], &g->c[(y + 1) * g->w], (size_t)g->w * sizeof(Cell));
                }
                grid_clear_line(g, p->scroll_bot, p->pen.bg);
            }
        }
        break;
    case 'P':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        if (n > g->w - p->cx) n = g->w - p->cx;
        for (int x = p->cx; x < g->w; x++) {
            Cell *dst = grid_at(g, x, p->cy);
            Cell *src = grid_at(g, x + n, p->cy);
            if (dst) *dst = src ? *src : blank_cell();
            if (dst && !src) dst->bg = p->pen.bg;
        }
        break;
    case 'X':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        for (int i = 0; i < n && p->cx + i < g->w; i++) pane_erase_cell(p, p->cx + i, p->cy);
        break;
    case '@':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        if (n > g->w - p->cx) n = g->w - p->cx;
        for (int x = g->w - 1; x >= p->cx + n; x--) {
            Cell *dst = grid_at(g, x, p->cy);
            Cell *src = grid_at(g, x - n, p->cy);
            if (dst) *dst = src ? *src : blank_cell();
        }
        for (int x = p->cx; x < p->cx + n; x++) pane_erase_cell(p, x, p->cy);
        break;
    case 'S':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        em_scroll_up(p, n);
        break;
    case 'T':
        n = param(ps, pn, 0, 1);
        if (n < 1) n = 1;
        em_scroll_down(p, n);
        break;
    case 'r': {
        int top = param(ps, pn, 0, 1) - 1;
        int bot = param(ps, pn, 1, g->h) - 1;
        if (top < 0) top = 0;
        if (bot >= g->h) bot = g->h - 1;
        if (top < bot) {
            p->scroll_top = top;
            p->scroll_bot = bot;
            p->cx = 0;
            p->cy = 0;
        }
        p->wrap_pending = 0;
        break;
    }
    case 's':
        p->saved_x = p->cx;
        p->saved_y = p->cy;
        p->saved_pen = p->pen;
        break;
    case 'u':
        p->cx = p->saved_x;
        p->cy = p->saved_y;
        p->pen = p->saved_pen;
        if (p->cx >= g->w) p->cx = g->w - 1;
        if (p->cy >= g->h) p->cy = g->h - 1;
        break;
    case 'h':
    case 'l': {
        int on = (final == 'h');
        for (int i = 0; i < pn; i++) {
            int v = ps[i];
            if (priv) {
                if (v == 1) p->app_cursor = on;
                else if (v == 25) p->cursor_visible = on;
                else if (v == 47 || v == 1047 || v == 1049) em_set_alt(p, on);
                else if (v == 1048) {
                    if (on) {
                        p->saved_x = p->cx;
                        p->saved_y = p->cy;
                        p->saved_pen = p->pen;
                    } else {
                        p->cy = clamp_int(p->saved_y, 0, g->h - 1);
                        p->cx = clamp_int(p->saved_x, 0, g->w - 1);
                        p->pen = p->saved_pen;
                    }
                }
            } else {
                if (v == 4) p->insert_mode = on;
            }
        }
        break;
    }
    case 'n':
        if (!priv) {
            int v = param(ps, pn, 0, 0);
            char out[64];
            if (v == 6) {
                int l = snprintf(out, sizeof out, "\x1b[%d;%dR", p->cy + 1, p->cx + 1);
                if (l > 0 && (size_t)l < sizeof out) pane_write(p, out, (size_t)l);
            } else if (v == 5) {
                pane_write(p, "\x1b[0n", 4);
            }
        }
        break;
    case 'c': {
        int v = param(ps, pn, 0, 0);
        if (!priv || v == 0) pane_write(p, "\x1b[?1;2c", 8);
        break;
    }
    case 't': {
        int v = param(ps, pn, 0, 0);
        if (!priv && (v == 18 || v == 19)) {
            char out[64];
            int l = snprintf(out, sizeof out, "\x1b[8;%d;%dt", g->h, g->w);
            if (l > 0 && (size_t)l < sizeof out) pane_write(p, out, (size_t)l);
        }
        break;
    }
    default:
        break;
    }
}

static int pane_remote_title(Pane *p, pid_t pid);

/* OSC 7 uses a percent-encoded file URI. The murx-specific form also
   accepts a shell-shortened path so remote $HOME can be displayed as ~. */
static void em_osc_cwd(Pane *p) {
    const char *path = NULL;
    int uri = 0;
    static const char cwd_prefix[] = "777;murx-cwd;";
    if (strncmp(p->seq, "7;file://", 9) == 0) {
        path = strchr(p->seq + 9, '/');
        uri = 1;
    } else if (strncmp(p->seq, cwd_prefix, sizeof cwd_prefix - 1) == 0) {
        path = p->seq + sizeof cwd_prefix - 1;
    }
    if (!path || (*path != '/' &&
        !(*path == '~' && (!path[1] || path[1] == '/')))) return;
    char cwd[PATH_MAX];
    size_t n = 0;
    while (*path) {
        unsigned char ch = (unsigned char)*path++;
        if (uri && ch == '%') {
            if (!path[0] || !path[1]) return;
            const char *digits = "0123456789abcdef";
            const char *hi = strchr(digits, tolower((unsigned char)path[0]));
            const char *lo = strchr(digits, tolower((unsigned char)path[1]));
            if (!hi || !lo) return;
            ch = (unsigned char)((hi - digits) * 16 + lo - digits);
            path += 2;
        }
        if (ch < 32 || ch == 127 || n >= sizeof cwd - 1) return;
        cwd[n++] = (char)ch;
    }
    cwd[n] = '\0';
    pid_t pid = tcgetpgrp(p->fd);
    if (pid <= 0) pid = tcgetpgrp(p->tty_fd);
    if (pid <= 0) return;
    snprintf(p->reported_cwd, sizeof p->reported_cwd, "%s", cwd);
    p->reported_cwd_pgrp = pid;
}

static void em_osc_done(Pane *p) {
    p->seq[p->seqlen] = '\0';
    em_osc_cwd(p);
    if (strcmp(p->seq, "777;murx-root;0") == 0 ||
        strcmp(p->seq, "777;murx-root;1") == 0) {
        pid_t pid = tcgetpgrp(p->fd);
        if (pid <= 0) pid = tcgetpgrp(p->tty_fd);
        if (pid > 0) {
            p->reported_root = p->seq[sizeof "777;murx-root;" - 1] == '1';
            p->reported_root_pgrp = pid;
        }
    }
    if (p->seqlen > 2 && (p->seq[0] == '0' || p->seq[0] == '2') && p->seq[1] == ';') {
        const char *t = p->seq + 2;
        size_t n = strlen(t);
        if (n >= sizeof p->title) n = sizeof p->title - 1;
        memcpy(p->title, t, n);
        p->title[n] = '\0';
        for (size_t i = 0; i < n; i++) {
            if ((unsigned char)p->title[i] < 32) p->title[i] = ' ';
        }
        p->title_pgrp = tcgetpgrp(p->fd);
        if (p->title_pgrp <= 0) p->title_pgrp = tcgetpgrp(p->tty_fd);
        /* Save structured remote names as they arrive, even when a program
           immediately follows them with its own unstructured OSC title. */
        if (p->title_pgrp > 0 && pane_remote_title(p, p->title_pgrp)) {
            snprintf(p->remote_title, sizeof p->remote_title, "%s", p->title);
            p->remote_title_pgrp = p->title_pgrp;
        }
    }
    p->seqlen = 0;
}

static void em_byte(Pane *p, unsigned char b) {
    switch (p->pstate) {
    case PS_GROUND:
        if (b == 0x1b) {
            p->pstate = PS_ESC;
            p->seqlen = 0;
        } else if (b == '\r') {
            pane_cr(p);
        } else if (b == '\n' || b == '\v' || b == '\f') {
            pane_lf(p);
        } else if (b == '\b') {
            pane_bs(p);
        } else if (b == '\t') {
            pane_tab(p);
        } else if (b == 0x07 || b == 0x0e || b == 0x0f || b == 0x7f) {
            /* bell / shifts / delete: ignore */
        } else if (b < 0x20) {
            /* other C0: ignore */
        } else {
            /* utf-8 accumulate */
            if (p->u8_need) {
                if ((b & 0xC0) == 0x80) {
                    p->u8_cp = (p->u8_cp << 6) | (b & 0x3F);
                    p->u8_have++;
                    if (p->u8_have == p->u8_need) {
                        uint32_t min = p->u8_need == 1 ? 0x80 :
                                       p->u8_need == 2 ? 0x800 : 0x10000;
                        pane_put_cp(p, p->u8_cp < min ? 0xFFFD : p->u8_cp);
                        p->u8_need = p->u8_have = 0;
                    }
                    return;
                }
                pane_put_cp(p, 0xFFFD);
                p->u8_need = p->u8_have = 0;
            }
            if (b < 0x80) {
                pane_put_cp(p, b);
            } else if (b >= 0xC2 && b <= 0xDF) {
                p->u8_cp = b & 0x1F;
                p->u8_need = 1;
                p->u8_have = 0;
            } else if (b >= 0xE0 && b <= 0xEF) {
                p->u8_cp = b & 0x0F;
                p->u8_need = 2;
                p->u8_have = 0;
            } else if (b >= 0xF0 && b <= 0xF4) {
                p->u8_cp = b & 0x07;
                p->u8_need = 3;
                p->u8_have = 0;
            } else {
                pane_put_cp(p, 0xFFFD);
            }
        }
        break;

    case PS_ESC:
        p->pstate = PS_GROUND;
        switch (b) {
        case '[':
            p->pstate = PS_CSI;
            p->seqlen = 0;
            break;
        case ']':
            p->pstate = PS_OSC;
            p->seqlen = 0;
            break;
        case 'P':
            p->pstate = PS_DCS;
            p->seqlen = 0;
            break;
        case '(': case ')': case '*': case '+':
            p->pstate = PS_CHARSET;
            break;
        case '7':
            p->saved_x = p->cx;
            p->saved_y = p->cy;
            p->saved_pen = p->pen;
            break;
        case '8':
            p->cx = p->saved_x;
            p->cy = p->saved_y;
            p->pen = p->saved_pen;
            if (p->cx >= p->grid->w) p->cx = p->grid->w - 1;
            if (p->cy >= p->grid->h) p->cy = p->grid->h - 1;
            break;
        case 'D':
            pane_lf(p);
            break;
        case 'E':
            p->cx = 0;
            pane_lf(p);
            break;
        case 'M':
            if (p->cy == p->scroll_top) em_scroll_down(p, 1);
            else if (p->cy > 0) p->cy--;
            p->wrap_pending = 0;
            break;
        case 'c':
            em_reset(p);
            break;
        default:
            break;
        }
        break;

    case PS_CHARSET:
        p->pstate = PS_GROUND;
        break;

    case PS_CSI:
        if (b >= 0x40 && b <= 0x7e) {
            p->pstate = PS_GROUND;
            em_csi(p, p->seq, p->seqlen, b);
        } else if (p->seqlen < (int)sizeof p->seq - 1) {
            p->seq[p->seqlen++] = (char)b;
        }
        break;

    case PS_OSC:
        if (b == 0x07) {
            p->pstate = PS_GROUND;
            em_osc_done(p);
        } else if (b == 0x1b) {
            p->pstate = PS_OSC_ESC;
        } else if (p->seqlen < (int)sizeof p->seq - 1) {
            p->seq[p->seqlen++] = (char)b;
        }
        break;

    case PS_OSC_ESC:
        if (b == '\\') {
            p->pstate = PS_GROUND;
            em_osc_done(p);
        } else {
            p->pstate = PS_OSC;
            p->seqlen = 0;
        }
        break;

    case PS_DCS:
        if (b == 0x1b) p->pstate = PS_DCS_ESC;
        else if (b == 0x07) p->pstate = PS_GROUND;
        break;

    case PS_DCS_ESC:
        p->pstate = (b == '\\') ? PS_GROUND : PS_DCS;
        break;
    }
}

static void em_feed(Pane *p, const unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) em_byte(p, buf[i]);
}

/* ------------------------------------------------------------------ */
/* layout tree, windows, session                                      */
/* ------------------------------------------------------------------ */

enum { L_LEAF = 0, L_COLS = 1, L_ROWS = 2 };

typedef struct Layout {
    int type;
    double ratio;
    struct Layout *a, *b, *parent;
    Pane *pane;
    int x, y, w, h;
} Layout;

typedef struct Window {
    char name[128];
    int index;
    int privileged;
    Layout *root;
    Pane *active;
    struct Window *next;
} Window;

typedef struct Client Client;
typedef struct Session Session;

struct Client {
    int fd;
    int cols, rows;
    int attached;
    uint64_t order;        /* attach/resize sequence, newest client wins */
    int truecolor;
    unsigned char in[16384];
    size_t inlen;
    long long escape_since;
    unsigned char net[65536];
    size_t netlen;

    Grid prev;
    int prev_valid;

    int locked;
    int prefix_pending;
    unsigned char prefix;

    int mode;              /* 0 none, 1 prompt, 2 menu, 3 confirm, 4 scrollback */
    int prompt_kind;       /* 1 rename window, 2 choose window */
    char prompt[160];
    int prompt_len;
    int prompt_label_len;

    Grid backscroll;       /* private frozen history and screen snapshot */
    Pane *backscroll_pane;
    int backscroll_offset;

    int menu_sel;
    int confirm_action;    /* 1 kill pane, 2 kill server */

    char msg[256];
    long long msg_until;

    char title[160];

    int dead;
    Client *next;
};

/* Display choices are set when a session starts. */
typedef struct {
    int show_cwd;
    int show_clock;
    int show_murx;
    int show_hostname;
} DisplayOptions;

struct Session {
    char name[64];
    char sock_path[PATH_MAX];
    char cwd[PATH_MAX];
    int listen_fd;

    int w, h;
    int status_bottom;
    DisplayOptions display;
    Window *windows;
    Window *cur, *last;
    int nwin;

    Client *clients;
    uint64_t client_order;
    int running;
    int redraw;            /* a removed client changed the session size */

    long long last_status;
    long long last_name_check;
};

/* Compact sequence numbers on wrap, preserving the newest-client ordering. */
static uint64_t client_next_order(Session *s) {
    if (s->client_order == UINT64_MAX) {
        uint64_t previous = 0, rank = 0;
        for (;;) {
            Client *next = NULL;
            for (Client *c = s->clients; c; c = c->next) {
                if (c->order > previous && (!next || c->order < next->order)) next = c;
            }
            if (!next) break;
            previous = next->order;
            if (rank == UINT64_MAX - 1) range_error();
            next->order = ++rank;
        }
        s->client_order = rank;
    }
    return ++s->client_order;
}

/* Remote jobs are invisible locally. Accept host:program OSC titles from
   the current ssh job; a previous job's title must not name a new connection. */
static int pane_remote_title(Pane *p, pid_t pid) {
    if (p->title_pgrp != pid) return 0;
    const char *colon = strchr(p->title, ':');
    if (!colon || colon == p->title || !colon[1]) return 0;
    for (const char *s = p->title; s < colon; s++) {
        unsigned char c = (unsigned char)*s;
        if (!isalnum(c) && c != '.' && c != '-' && c != '_') return 0;
    }
    for (const char *s = colon + 1; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c <= 32 || c == 127 || c == '/' || c == ':') return 0;
    }
    return 1;
}

/* Inspect the effective UID, so sudo/su descendants stay marked even
   when their executable name changes from the shell to another program. */
static int process_is_root(pid_t pid) {
    unsigned long uid = 1;
#ifdef __linux__
    char path[64], line[256];
    snprintf(path, sizeof path, "/proc/%ld/status", (long)pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "Uid:", 4) != 0) continue;
        const char *fields = line + 4;
        unsigned long real_uid;
        if (!parse_ulong_field(&fields, &real_uid) || !parse_ulong_field(&fields, &uid)) uid = 1;
        break;
    }
    fclose(f);
#else
    char command[96];
    snprintf(command, sizeof command, "ps -p %ld -o uid=", (long)pid);
    FILE *f = popen(command, "r");
    if (!f) return 0;
    char line[256];
    const char *fields = line;
    int ok = fgets(line, sizeof line, f) && parse_ulong_field(&fields, &uid);
    if (pclose(f) != 0 || !ok) return 0;
#endif
    return uid == 0;
}

/* Use the active pane's foreground process group, so returning from a job
   restores the shell name. */
static int window_update_name(Session *s, Window *w) {
    Pane *p = w->active;
    if (!p || !p->alive) return 0;
    /* Linux permits this query on the master even though the server does
       not own the slave as its controlling terminal. */
    pid_t pid = tcgetpgrp(p->fd);
    if (pid <= 0) pid = tcgetpgrp(p->tty_fd);
    if (pid <= 0) return 0;
    char name[256];
    FILE *f = NULL;
#ifdef __linux__
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/comm", (long)pid);
    f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(name, sizeof name, f) != NULL;
    fclose(f);
#else
    /* POSIX fallback for systems without Linux procfs. The only interpolated
       value is the numeric process group ID obtained from the pane's tty. */
    char command[96];
    snprintf(command, sizeof command, "ps -p %ld -o comm=", (long)pid);
    f = popen(command, "r");
    if (!f) return 0;
    int ok = fgets(name, sizeof name, f) != NULL;
    if (pclose(f) != 0) ok = 0;
#endif
    if (!ok) return 0;
    name[strcspn(name, "\r\n")] = '\0';
    char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    while (*base == '-' || *base == ' ' || *base == '\t') base++;
    if (!*base) return 0;
    for (char *s = base; *s; s++) {
        if ((unsigned char)*s < 32 || (unsigned char)*s == 127) *s = '?';
    }
    int privileged = strcmp(base, "su") == 0 || process_is_root(pid);
    if (strcmp(base, "ssh") == 0) {
        /* An SSH process's local UID says nothing about the remote user. */
        privileged = p->reported_root_pgrp == pid && p->reported_root;
        if (p->remote_title_pgrp == pid) {
            const char *program = strchr(p->remote_title, ':');
            if (program && strcmp(program + 1, "su") == 0) privileged = 1;
        }
    }
    int changed = w->privileged != privileged;
    w->privileged = privileged;
    char next[sizeof w->name];
    if (strcmp(base, "ssh") == 0 && p->remote_title_pgrp == pid && p->remote_title[0]) {
        if (s->display.show_hostname) {
            snprintf(next, sizeof next, "%s", p->remote_title);
        } else {
            const char *colon = strchr(p->remote_title, ':');
            snprintf(next, sizeof next, "%s", colon ? colon + 1 : p->remote_title);
        }
    } else if (s->display.show_hostname) {
        char host[256];
        if (gethostname(host, sizeof host) < 0) snprintf(host, sizeof host, "localhost");
        host[sizeof host - 1] = '\0';
        char *dot = strchr(host, '.');
        if (dot) *dot = '\0';
        for (char *h = host; *h; h++) {
            if ((unsigned char)*h < 32 || (unsigned char)*h == 127) *h = '?';
        }
        snprintf(next, sizeof next, "%.63s:%.63s", host, base);
    } else {
        snprintf(next, sizeof next, "%.63s", base);
    }
    if (strcmp(w->name, next) == 0) return changed;
    snprintf(w->name, sizeof w->name, "%s", next);
    return 1;
}

static void pane_set_winsize(Pane *p, int w, int h) {
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = tty_dimension(w);
    ws.ws_row = tty_dimension(h);

    /* macOS rejects TIOCSWINSZ on the master, so always set it on the slave.
       The server keeps a slave fd open for the whole life of the pane: this
       makes resizing work and prevents a startup race where reading the
       master before the child opens the slave would look like EOF. */
    if (p->tty_fd >= 0) {
        ioctl(p->tty_fd, TIOCSWINSZ, &ws);
    } else if (p->fd >= 0) {
        ioctl(p->fd, TIOCSWINSZ, &ws);
    }
}

static void pane_resize(Pane *p, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (p->w == w && p->h == h && p->grid->w == w && p->grid->h == h) return;
    grid_resize(&p->main_grid, w, h);
    grid_resize(&p->alt_grid, w, h);
    p->grid = p->on_alt ? &p->alt_grid : &p->main_grid;
    if (p->cx >= w) p->cx = w - 1;
    if (p->cy >= h) p->cy = h - 1;
    p->scroll_top = 0;
    p->scroll_bot = h - 1;
    p->wrap_pending = 0;
    p->w = w;
    p->h = h;
    pane_set_winsize(p, w, h);
}

static void pane_free(Pane *p) {
    if (!p) return;
    if (p->tty_fd >= 0) close(p->tty_fd);
    if (p->fd >= 0) close(p->fd);
    if (p->pid > 0) kill(p->pid, SIGHUP);
    grid_free(&p->main_grid);
    grid_free(&p->alt_grid);
    if (p->history) {
        for (int i = 0; i < HISTORY_LINES; i++) grid_free(&p->history[i]);
        free(p->history);
    }
    free(p);
}

static Pane *pane_new(const char *cwd, int w, int h, const char *title) {
    int master = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (master < 0) return NULL;
    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        close(master);
        return NULL;
    }
    const char *slave = ptsname(master);
    if (!slave) {
        close(master);
        return NULL;
    }
    char slave_path[PATH_MAX];
    snprintf(slave_path, sizeof slave_path, "%s", slave);

    const char *shell = getenv("SHELL");
    if (!shell || !*shell) {
        if (access("/usr/bin/zsh", X_OK) == 0) shell = "/usr/bin/zsh";
        else shell = "/bin/sh";
    }

    /* Open the slave and give it its size before forking. The server keeps
       this fd for the pane's lifetime: a pty with no slave open would report
       EOF on the master (and on macOS TIOCSWINSZ only works on the slave). */
    struct winsize iws;
    memset(&iws, 0, sizeof iws);
    iws.ws_col = tty_dimension(w > 0 ? w : 80);
    iws.ws_row = tty_dimension(h > 0 ? h : 24);
    int slave_fd = open(slave_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (slave_fd < 0) {
        close(master);
        return NULL;
    }
    ioctl(slave_fd, TIOCSWINSZ, &iws);

    pid_t pid = fork();
    if (pid < 0) {
        close(slave_fd);
        close(master);
        return NULL;
    }
    if (pid == 0) {
        close(slave_fd);
        setsid();
        int fd = open(slave_path, O_RDWR);
        if (fd < 0) _exit(127);
        ioctl(fd, TIOCSCTTY, 0);
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2) close(fd);
        long maxfd = sysconf(_SC_OPEN_MAX);
        if (maxfd < 0 || maxfd > 4096) maxfd = 4096;
        for (int i = 3; i < maxfd; i++) close(i);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        setenv("TERM", "screen-256color", 1);
        if (cwd && *cwd) chdir(cwd);
        const char *base = strrchr(shell, '/');
        base = base ? base + 1 : shell;
        char arg0[PATH_MAX];
        snprintf(arg0, sizeof arg0, "-%s", base);
        char *args[2];
        args[0] = arg0;
        args[1] = NULL;
        execvp(shell, args);
        execl("/bin/sh", "-sh", (char *)NULL);
        _exit(127);
    }

    fcntl(master, F_SETFL, fcntl(master, F_GETFL, 0) | O_NONBLOCK);

    Pane *p = xcalloc(1, sizeof *p);
    p->fd = master;
    p->pid = pid;
    p->alive = 1;
    p->tty_fd = slave_fd;
    p->cursor_visible = 1;
    p->pen.fg = DEFAULT_COLOR;
    p->pen.bg = DEFAULT_COLOR;
    grid_init(&p->main_grid, w, h);
    grid_init(&p->alt_grid, w, h);
    p->grid = &p->main_grid;
    p->scroll_bot = h - 1;
    p->w = 0;
    p->h = 0;
    snprintf(p->title, sizeof p->title, "%s", title ? title : "shell");
    pane_resize(p, w, h);
    return p;
}

/* ------------------------------------------------------------------ */
/* layout operations                                                  */
/* ------------------------------------------------------------------ */

static Layout *layout_leaf(Pane *p) {
    Layout *l = xcalloc(1, sizeof *l);
    l->type = L_LEAF;
    l->pane = p;
    return l;
}

static void layout_free(Layout *l) {
    if (!l) return;
    layout_free(l->a);
    layout_free(l->b);
    free(l);
}

static int layout_pane_count(Layout *l) {
    if (!l) return 0;
    if (l->type == L_LEAF) return 1;
    return count_add(layout_pane_count(l->a), layout_pane_count(l->b));
}

static void layout_apply(Layout *l, int x, int y, int w, int h) {
    if (!l) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    l->x = x;
    l->y = y;
    l->w = w;
    l->h = h;

    if (l->type == L_LEAF) {
        Pane *p = l->pane;
        p->x = x;
        p->y = y;
        pane_resize(p, w, h);
        return;
    }

    if (l->type == L_COLS) {
        int avail = w - 1;
        if (avail < 1) avail = 1;
        int aw = (int)(avail * l->ratio + 0.5);
        if (aw < 1) aw = 1;
        if (aw > avail - 1) aw = avail - 1;
        if (aw < 1) aw = 1;
        if (avail - aw < 1) aw = avail;
        l->ratio = (double)aw / (double)avail;
        layout_apply(l->a, x, y, aw, h);
        layout_apply(l->b, x + aw + 1, y, avail - aw, h);
    } else {
        int avail = h - 1;
        if (avail < 1) avail = 1;
        int ah = (int)(avail * l->ratio + 0.5);
        if (ah < 1) ah = 1;
        if (ah > avail - 1) ah = avail - 1;
        if (ah < 1) ah = 1;
        l->ratio = (double)ah / (double)avail;
        layout_apply(l->a, x, y, w, ah);
        layout_apply(l->b, x, y + ah + 1, w, avail - ah);
    }
}

static Pane *layout_first_pane(Layout *l) {
    if (!l) return NULL;
    while (l->type != L_LEAF) l = l->a;
    return l->pane;
}

static void layout_collect(Layout *l, Pane **out, int *n) {
    if (!l) return;
    if (l->type == L_LEAF) {
        out[(*n)++] = l->pane;
        return;
    }
    layout_collect(l->a, out, n);
    layout_collect(l->b, out, n);
}

static Layout *layout_find_leaf(Layout *l, Pane *p) {
    if (!l) return NULL;
    if (l->type == L_LEAF) return l->pane == p ? l : NULL;
    Layout *r = layout_find_leaf(l->a, p);
    return r ? r : layout_find_leaf(l->b, p);
}

static void window_renumber(Window *list) {
    int i = 0;
    for (Window *w = list; w; w = w->next) {
        w->index = i;
        i = count_add(i, 1);
    }
    /* Pane numbers are absolute across the whole session, not per window, so
       that the [N: title] labels stay stable no matter which window is
       current. */
    int id = 0;
    for (Window *w = list; w; w = w->next) {
        Pane *list2[128];
        int n = 0;
        if (w->root) layout_collect(w->root, list2, &n);
        for (int j = 0; j < n; j++) {
            list2[j]->id = id;
            id = count_add(id, 1);
        }
    }
}

static Window *window_new(Session *s, const char *name) {
    Window *w = xcalloc(1, sizeof *w);
    const char *shell = getenv("SHELL");
    if (!shell || !*shell) {
        if (access("/usr/bin/zsh", X_OK) == 0) shell = "/usr/bin/zsh";
        else shell = "/bin/sh";
    }
    const char *base = strrchr(shell, '/');
    base = base ? base + 1 : shell;
    if (name) {
        snprintf(w->name, sizeof w->name, "%s", name);
    } else if (s->display.show_hostname) {
        char host[256];
        if (gethostname(host, sizeof host) < 0) snprintf(host, sizeof host, "localhost");
        host[sizeof host - 1] = '\0';
        char *dot = strchr(host, '.');
        if (dot) *dot = '\0';
        snprintf(w->name, sizeof w->name, "%.63s:%.63s", host, base);
    } else {
        snprintf(w->name, sizeof w->name, "%s", base);
    }

    char host[256];
    if (gethostname(host, sizeof host) < 0) snprintf(host, sizeof host, "localhost");
    host[sizeof host - 1] = '\0';
    char *dot = strchr(host, '.');
    if (dot) *dot = '\0';

    int pw = s->w > 0 ? s->w : 80;
    int ph = s->h > 1 ? s->h - 1 : 23;
    if (ph < 1) ph = 1;
    Pane *p = pane_new(s->cwd, pw, ph, host);
    if (!p) {
        free(w);
        return NULL;
    }
    w->root = layout_leaf(p);
    w->active = p;

    if (!s->windows) {
        s->windows = w;
    } else {
        Window *tail = s->windows;
        while (tail->next) tail = tail->next;
        tail->next = w;
    }
    s->nwin = count_add(s->nwin, 1);
    window_renumber(s->windows);
    return w;
}

static void session_layout(Session *s) {
    if (!s->cur || !s->cur->root) return;
    int top = s->status_bottom ? 0 : 1;
    int h = s->h - 1;
    if (h < 1) h = 1;
    layout_apply(s->cur->root, 0, top, s->w, h);
}

/* ------------------------------------------------------------------ */
/* session helpers                                                    */
/* ------------------------------------------------------------------ */

static void socket_dir_at(const char *base, char *out, size_t n) {
    snprintf(out, n, "%s/murx-%u", base, (unsigned)getuid());
}

static void socket_dir(char *out, size_t n) {
    const char *base = getenv("XDG_RUNTIME_DIR");
    if (!base || !*base) base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    socket_dir_at(base, out, n);
}

static int valid_session_name(const char *s) {
    if (!s || !*s || strlen(s) > 48) return 0;
    for (const char *p = s; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_')) return 0;
    }
    return 1;
}

static void make_socket_path(const char *name, char *out, size_t n) {
    char dir[PATH_MAX];
    socket_dir(dir, sizeof dir);
    /* Select the fallback before formatting, so a long runtime directory
       cannot truncate the socket path. Session names are limited to 48 bytes. */
    if (strlen(dir) + strlen(name) + 6 >= 100) {
        socket_dir_at("/tmp", dir, sizeof dir);
    }
    snprintf(out, n, "%.99s/%.48s.sock", dir, name);
}

static void client_message(Client *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->msg, sizeof c->msg, fmt, ap);
    va_end(ap);
    c->msg_until = now_ms() + MSG_TIME;
}

/* ------------------------------------------------------------------ */
/* drawing into the composed screen                                   */
/* ------------------------------------------------------------------ */

static void scr_put(Grid *g, int x, int y, uint32_t ch, int fg, int bg, int attr) {
    Cell *c = grid_at(g, x, y);
    if (!c) return;
    c->ch = ch;
    c->fg = fg;
    c->bg = bg;
    c->attr = (uint8_t)attr;
}

static void scr_put_wide(Grid *g, int x, int y, uint32_t ch, int fg, int bg, int attr) {
    int w = cp_width(ch);
    if (w < 1) w = 1;
    scr_put(g, x, y, ch, fg, bg, attr);
    if (w == 2) scr_put(g, x + 1, y, WIDE_CONT, fg, bg, attr);
}

static int text_width(const char *s) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp;
        int used = utf8_decode(p, strlen((const char *)p), &cp);
        int w = cp_width(cp);
        if (w < 0) w = 1;
        if (n > INT_MAX - w) return INT_MAX;
        n += w;
        p += used;
    }
    return n;
}

static int draw_text(Grid *g, int x, int y, const char *s, int fg, int bg, int attr) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp;
        int used = utf8_decode(p, strlen((const char *)p), &cp);
        int w = cp_width(cp);
        if (w < 0) w = 1;
        if (x > INT_MAX - w) return INT_MAX;
        if (cp >= 32 && cp != 0x7f) scr_put_wide(g, x, y, cp, fg, bg, attr);
        x += w;
        p += used;
    }
    return x;
}

/* Pane borders are thin box-drawing lines drawn over the terminal's own
 * background. Borders occupy only the separators between panes and are
 * collected into a mask first so that crossing lines get the right junction
 * glyph no matter which pane is visited first. */
#define B_UP    0x1
#define B_DOWN  0x2
#define B_LEFT  0x4
#define B_RIGHT 0x8

static void border_mark(uint8_t *m, int w, int h, int x, int y, uint8_t bits) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    m[y * w + x] |= bits;
}

static void border_collect(uint8_t *m, int w, int h, Layout *l) {
    if (!l || l->w < 1 || l->h < 1) return;
    if (l->type == L_LEAF) return;
    if (l->type == L_COLS) {
        int ax = l->x + l->a->w;
        int end = l->y + l->h;
        for (int y = l->y; y < end; y++) {
            uint8_t bits = 0;
            if (y > l->y || l->h == 1) bits |= B_UP;
            if (y < end - 1 || l->h == 1) bits |= B_DOWN;
            border_mark(m, w, h, ax, y, bits);
        }
    }
    else {
        int ay = l->y + l->a->h;
        int end = l->x + l->w;
        for (int x = l->x; x < end; x++) {
            uint8_t bits = 0;
            if (x > l->x || l->w == 1) bits |= B_LEFT;
            if (x < end - 1 || l->w == 1) bits |= B_RIGHT;
            border_mark(m, w, h, x, ay, bits);
        }
    }
    border_collect(m, w, h, l->a);
    border_collect(m, w, h, l->b);
}

/* Fill in the missing half of a T-junction: a line that ends next to a
 * perpendicular one should meet it instead of leaving a one-cell gap. */
static void border_join(uint8_t *m, int w, int h) {
    if (w < 1 || h < 1) return;
    if (w > INT_MAX / h) range_error();
    uint8_t *add = xcalloc((size_t)w, (size_t)h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t b = m[y * w + x];
            uint8_t extra = 0;
            if (b & (B_UP | B_DOWN)) {
                if (x > 0 && (m[y * w + x - 1] & (B_LEFT | B_RIGHT)))
                    extra |= B_LEFT;
                if (x + 1 < w && (m[y * w + x + 1] & (B_LEFT | B_RIGHT)))
                    extra |= B_RIGHT;
            }
            if (b & (B_LEFT | B_RIGHT)) {
                if (y > 0 && (m[(y - 1) * w + x] & (B_UP | B_DOWN)))
                    extra |= B_UP;
                if (y + 1 < h && (m[(y + 1) * w + x] & (B_UP | B_DOWN)))
                    extra |= B_DOWN;
            }
            add[y * w + x] = extra;
        }
    }
    for (int i = 0; i < w * h; i++) m[i] |= add[i];
    free(add);
}

static uint32_t border_glyph(uint8_t b) {
    switch (b & 0xF) {
    case B_UP | B_DOWN:                    return 0x2502; /* │ */
    case B_LEFT | B_RIGHT:                 return 0x2500; /* ─ */
    case B_DOWN | B_RIGHT:                 return 0x250C; /* ┌ */
    case B_DOWN | B_LEFT:                  return 0x2510; /* ┐ */
    case B_UP | B_RIGHT:                   return 0x2514; /* └ */
    case B_UP | B_LEFT:                    return 0x2518; /* ┘ */
    case B_UP | B_DOWN | B_RIGHT:          return 0x251C; /* ├ */
    case B_UP | B_DOWN | B_LEFT:           return 0x2524; /* ┤ */
    case B_DOWN | B_LEFT | B_RIGHT:        return 0x252C; /* ┬ */
    case B_UP | B_LEFT | B_RIGHT:          return 0x2534; /* ┴ */
    case B_UP | B_DOWN | B_LEFT | B_RIGHT: return 0x253C; /* ┼ */
    case B_UP: case B_DOWN:                return 0x2502;
    case B_LEFT: case B_RIGHT:             return 0x2500;
    default:                               return 0;
    }
}

static void border_render(Grid *g, const uint8_t *m) {
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            uint8_t b = m[y * g->w + x];
            if (!b) continue;
            uint32_t ch = border_glyph(b);
            if (ch) scr_put(g, x, y, ch, C_BORDER, DEFAULT_COLOR, 0);
        }
    }
}

static void draw_layout(Grid *g, Layout *l) {
    if (!l || l->w < 1 || l->h < 1) return;
    if (l->type == L_LEAF) {
        Pane *p = l->pane;
        int cw = p->grid->w;
        int ch = p->grid->h;
        for (int yy = 0; yy < p->h && yy < ch; yy++) {
            for (int xx = 0; xx < p->w && xx < cw; xx++) {
                Cell *src = grid_at(p->grid, xx, yy);
                Cell *dst = grid_at(g, p->x + xx, p->y + yy);
                if (src && dst) *dst = *src;
            }
        }
        return;
    }
    draw_layout(g, l->a);
    draw_layout(g, l->b);
}

static void status_right_draw(Grid *g, int rx, int y, const char *clock) {
    int px = draw_text(g, rx, y, " ", P_STATUS, DEFAULT_COLOR, 0);
    px = draw_text(g, px, y, clock, P_STATUS, DEFAULT_COLOR, 0);
    draw_text(g, px, y, " ", P_STATUS, DEFAULT_COLOR, 0);
}

/* Linux exposes the foreground job's cwd; fall back to the pane shell,
   then the session's starting directory when it cannot be queried. */
static int status_cwd(Session *s, char *out, size_t size) {
    Pane *p = s->cur ? s->cur->active : NULL;
    if (p) {
        pid_t foreground = tcgetpgrp(p->fd);
        if (foreground <= 0) foreground = tcgetpgrp(p->tty_fd);
        if (foreground > 0 && p->reported_cwd_pgrp == foreground &&
            p->reported_cwd[0]) {
            snprintf(out, size, "%s", p->reported_cwd);
            return 1;
        }
#ifdef __linux__
        pid_t pids[2] = { foreground, p->pid };
        for (int i = 0; i < 2; i++) {
            if (pids[i] <= 0) continue;
            char path[64];
            snprintf(path, sizeof path, "/proc/%ld/cwd", (long)pids[i]);
            ssize_t n = readlink(path, out, size - 1);
            if (n > 0 && (size_t)n < size - 1) {
                out[n] = '\0';
                for (char *q = out; *q; q++) {
                    if ((unsigned char)*q < 32 || (unsigned char)*q == 127) *q = '?';
                }
                return 0;
            }
        }
#endif
    }
    snprintf(out, size, "%s", s->cwd);
    return 0;
}

/* Match a complete home-directory component, not a similarly named sibling. */
static void status_cwd_home(char *cwd, size_t size) {
    const char *home = getenv("HOME");
    if (!home || home[0] != '/') return;
    size_t n = strlen(home);
    while (n > 1 && home[n - 1] == '/') n--;
    if (strncmp(cwd, home, n) != 0) return;
    if (cwd[n] && cwd[n] != '/' && n != 1) return;
    const char *suffix = cwd + n;
    if (n == 1 && *suffix) suffix--;
    size_t len = strlen(suffix);
    if (len + 2 > size) return;
    memmove(cwd + 1, suffix, len + 1);
    cwd[0] = '~';
}

/* Fit the directory label between the left edge and the window list. */
static int status_cwd_draw(Grid *g, int y, const char *cwd, int limit) {
    if (limit < 4) return 0;
    int available = limit - 4; /* leading space, brackets, trailing space */
    int truncated = text_width(cwd) > available;
    int dots = truncated ? (available < 3 ? available : 3) : 0;
    int x = draw_text(g, 0, y, " [", P_STATUS, DEFAULT_COLOR, 0);
    const unsigned char *p = (const unsigned char *)cwd;
    int end = x + available - dots;
    while (*p) {
        uint32_t cp;
        int used = utf8_decode(p, strlen((const char *)p), &cp);
        int w = cp_width(cp);
        if (w < 1) w = 1;
        if (x + w > end) break;
        scr_put_wide(g, x, y, cp, P_ACCENT, DEFAULT_COLOR, 0);
        x += w;
        p += used;
    }
    for (int i = 0; i < dots; i++)
        x = draw_text(g, x, y, ".", P_ACCENT, DEFAULT_COLOR, 0);
    return draw_text(g, x, y, "] ", P_STATUS, DEFAULT_COLOR, 0);
}

static int status_y(Session *s) {
    return s->status_bottom ? s->h - 1 : 0;
}

static void status_draw(Grid *g, Session *s, Client *c) {
    if (s->h < 1) return;
    int y = status_y(s);
    for (int x = 0; x < s->w; x++) {
        scr_put(g, x, y, ' ', P_STATUS, DEFAULT_COLOR, 0);
    }

    char clockbuf[32];
    clockbuf[0] = '\0';
    int right_len = 0;
    int rx = s->w;
    if (s->display.show_clock) {
        time_t t = time(NULL);
        struct tm tmv;
        if (localtime_r(&t, &tmv) &&
            !strftime(clockbuf, sizeof clockbuf, "%H:%M:%S", &tmv)) clockbuf[0] = '\0';
        right_len = text_width(clockbuf) + 2;
        if (right_len > s->w) right_len = s->w;
        rx = s->w - right_len;
        if (rx < 0) rx = 0;
        status_right_draw(g, rx, y, clockbuf);
    }

    int total = 0;
    for (Window *w = s->windows; w; w = w->next) {
        char num[32], flags[8];
        snprintf(num, sizeof num, "%d", w->index);
        flags[0] = 0;
        if (w == s->cur) strcat(flags, "*");
        else if (w == s->last) strcat(flags, "-");
        int width = (int)strlen(num) + 1 + text_width(w->name) + (int)strlen(flags) + 1;
        total = count_add(total, width);
    }
    int need = s->w - right_len;
    int start = need - total;
    int left_len = 0;
    {
        char left[PATH_MAX];
        if (s->display.show_cwd) {
            if (!status_cwd(s, left, sizeof left))
                status_cwd_home(left, sizeof left);
            left_len = status_cwd_draw(g, y, left, start);
        } else if (s->display.show_murx) {
            snprintf(left, sizeof left, "murx");
            left_len = status_cwd_draw(g, y, left, start);
        } else {
            left_len = 0;
        }
        if (start < left_len) start = left_len;
    }
    if (start < 0) start = 0;
    int wx = start;
    for (Window *w = s->windows; w; w = w->next) {
        char num[32], flags[8];
        snprintf(num, sizeof num, "%d", w->index);
        flags[0] = 0;
        if (w == s->cur) strcat(flags, "*");
        else if (w == s->last) strcat(flags, "-");
        int fg_name = (w == s->cur) ? (w->privileged ? C_RED : P_CURRENT) : P_STATUS;
        wx = draw_text(g, wx, y, num, P_STATUS, DEFAULT_COLOR, 0);
        wx = draw_text(g, wx, y, ":", P_ACCENT, DEFAULT_COLOR, 0);
        wx = draw_text(g, wx, y, w->name, fg_name, DEFAULT_COLOR, 0);
        wx = draw_text(g, wx, y, flags, P_ACCENT, DEFAULT_COLOR, 0);
        wx = draw_text(g, wx, y, " ", P_ACCENT, DEFAULT_COLOR, 0);
        if (wx >= need) break;
    }

    char scroll_msg[128];
    const char *overlay = NULL;
    if (c->mode == 4) {
        snprintf(scroll_msg, sizeof scroll_msg, "Scrollback [%d] PgUp/PgDn, q/Esc exits",
                 c->backscroll_offset);
        overlay = scroll_msg;
    }
    if (c->mode == 1 || c->mode == 3) overlay = c->prompt;
    else if (c->msg[0] && c->msg_until > now_ms()) overlay = c->msg;
    if (overlay) {
        int limit = s->w - right_len;
        if (limit > s->w) limit = s->w;
        if (limit < 1) limit = 1;
        for (int i = 0; i < limit; i++) scr_put(g, i, y, ' ', P_MSG, P_MSG_BG, 0);
        int ox = 0;
        const unsigned char *p = (const unsigned char *)overlay;
        while (*p && ox < limit) {
            uint32_t cp;
            int used = utf8_decode(p, strlen((const char *)p), &cp);
            int w = cp_width(cp);
            if (w < 1) w = 1;
            if (ox + w > limit) break;
            scr_put_wide(g, ox, y, cp, P_MSG, P_MSG_BG, 0);
            ox += w;
            p += used;
        }
    }

    if (s->display.show_clock) {
        /* keep the clock visible even if a message or window list is long */
        status_right_draw(g, rx, y, clockbuf);
    }
}

static void menu_draw(Grid *g, Session *s, Client *c) {
    Window *list[256];
    int n = 0;
    for (Window *w = s->windows; w && n < 256; w = w->next) list[n++] = w;
    if (n == 0) return;
    if (c->menu_sel < 0) c->menu_sel = 0;
    if (c->menu_sel >= n) c->menu_sel = n - 1;

    int width = 24;
    for (int i = 0; i < n; i++) {
        int wl = text_width(list[i]->name) + 8;
        if (wl > width) width = wl;
    }
    if (width > s->w - 2) width = s->w - 2;
    if (width < 10) width = 10;
    int x0 = 1, y0 = 1;
    int h = n + 2;
    int bottom = s->h - (s->status_bottom ? 1 : 0);
    if (y0 + h > bottom) h = bottom - y0;
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + width; x++) {
            scr_put(g, x, y, ' ', C_BLUE, C_GREY, 0);
        }
    }
    int rows = h - 2;
    for (int i = 0; i < rows && i < n; i++) {
        char line[320];
        snprintf(line, sizeof line, " %d: %s ", list[i]->index, list[i]->name);
        int sel = (i == c->menu_sel);
        int fg = sel ? C_BLACK : C_BLUE;
        int bg = sel ? C_BLUE : C_GREY;
        int x = x0;
        for (const char *p = line; *p && x < x0 + width; p++) {
            scr_put(g, x++, y0 + 1 + i, (unsigned char)*p, fg, bg, 0);
        }
        while (x < x0 + width) scr_put(g, x++, y0 + 1 + i, ' ', fg, bg, 0);
    }
}

static void compose(Session *s, Client *c, Grid *out) {
    if (c->mode == 4 && (!s->cur || s->cur->active != c->backscroll_pane)) {
        c->mode = 0;
        grid_free(&c->backscroll);
    }
    grid_init(out, s->w, s->h);
    grid_clear(out, DEFAULT_COLOR);
    if (s->cur && s->cur->root) {
        uint8_t *mask = xcalloc((size_t)out->w, (size_t)out->h);
        border_collect(mask, s->w, s->h, s->cur->root);
        border_join(mask, s->w, s->h);
        draw_layout(out, s->cur->root);
        border_render(out, mask);
        free(mask);
    }
    if (c->mode == 4 && s->cur && s->cur->active) {
        Pane *p = s->cur->active;
        int top = c->backscroll.h - p->h - c->backscroll_offset;
        for (int y = 0; y < p->h; y++) {
            for (int x = 0; x < p->w; x++) {
                Cell *src = grid_at(&c->backscroll, x, top + y);
                Cell *dst = grid_at(out, p->x + x, p->y + y);
                if (dst) {
                    *dst = src ? *src : blank_cell();
                    if (dst->ch == WIDE_CONT && x == 0) *dst = blank_cell();
                    if (cp_width(dst->ch) == 2 && x == p->w - 1) *dst = blank_cell();
                }
            }
        }
    }
    if (c->mode == 2) menu_draw(out, s, c);
    status_draw(out, s, c);
}

static void cursor_for(Session *s, Client *c, int *x, int *y, int *visible) {
    *visible = 0;
    *x = *y = 0;
    if (c->locked) return;
    if (c->mode == 1) {
        int len = text_width(c->prompt);
        *x = len;
        *y = status_y(s);
        if (*x >= s->w) *x = s->w - 1;
        *visible = 1;
        return;
    }
    if (c->mode != 0) return;
    if (!s->cur || !s->cur->active) return;
    Pane *p = s->cur->active;
    if (!p->cursor_visible) return;
    int px = p->x + p->cx;
    int py = p->y + p->cy;
    if (px < p->x || py < p->y || px >= p->x + p->w || py >= p->y + p->h) return;
    if (px >= s->w || py >= s->h) return;
    *x = px;
    *y = py;
    *visible = 1;
}

/* ------------------------------------------------------------------ */
/* frame output                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    char *p;
    size_t len, cap;
} Buf;

static void buf_add(Buf *b, const void *data, size_t n) {
    size_t need = size_add(size_add(b->len, n), 1);
    if (need > b->cap) {
        size_t cap = buffer_capacity(b->cap ? b->cap : 4096, need);
        b->p = xrealloc(b->p, cap);
        b->cap = cap;
    }
    memcpy(b->p + b->len, data, n);
    b->len += n;
    b->p[b->len] = 0;
}

static void buf_puts(Buf *b, const char *s) {
    buf_add(b, s, strlen(s));
}

static void buf_printf(Buf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof tmp) n = (int)sizeof tmp - 1;
    buf_add(b, tmp, (size_t)n);
}

static int cell_same(Cell *a, Cell *b) {
    return a->ch == b->ch && a->fg == b->fg && a->bg == b->bg && a->attr == b->attr;
}

static int nearest_cube_level(int v, int *idx) {
    static const int levels[6] = { 0, 95, 135, 175, 215, 255 };
    int best = 0, bestd = -1;
    for (int i = 0; i < 6; i++) {
        int d = v - levels[i];
        d *= d;
        if (bestd < 0 || d < bestd) {
            bestd = d;
            best = i;
        }
    }
    *idx = best;
    return levels[best];
}

/* Map a 24-bit colour to the nearest xterm-256 palette entry. */
static int color256_from_rgb(int r, int g, int b) {
    int ri, gi, bi;
    int cr = nearest_cube_level(r, &ri);
    int cg = nearest_cube_level(g, &gi);
    int cb = nearest_cube_level(b, &bi);
    int cube = 16 + 36 * ri + 6 * gi + bi;
    long cubed = (long)(r - cr) * (r - cr) +
                 (long)(g - cg) * (g - cg) +
                 (long)(b - cb) * (b - cb);

    int gray = (r * 299 + g * 587 + b * 114) / 1000;
    int gidx = (gray - 8 + 5) / 10;
    if (gidx < 0) gidx = 0;
    if (gidx > 23) gidx = 23;
    int gv = 8 + gidx * 10;
    long grayd = (long)(r - gv) * (r - gv) +
                 (long)(g - gv) * (g - gv) +
                 (long)(b - gv) * (b - gv);

    if (grayd < cubed) return 232 + gidx;
    return cube;
}

static void emit_color(Buf *b, int32_t col, int bg, int truecolor) {
    if (col == DEFAULT_COLOR) {
        buf_puts(b, bg ? "49" : "39");
        return;
    }
    if (col & RGB_FLAG) {
        int r = (col >> 16) & 0xFF, g = (col >> 8) & 0xFF, bl = col & 0xFF;
        if (truecolor) {
            buf_printf(b, "%d;2;%d;%d;%d", bg ? 48 : 38, r, g, bl);
            return;
        }
        col = color256_from_rgb(r, g, bl);
    }
    int v = col & 0xFF;
    buf_printf(b, "%d;5;%d", bg ? 48 : 38, v);
}

static void emit_sgr(Buf *b, int32_t fg, int32_t bg, int attr, int truecolor) {
    buf_puts(b, "\x1b[0");
    if (attr & A_BOLD) buf_puts(b, ";1");
    if (attr & A_DIM) buf_puts(b, ";2");
    if (attr & A_ITALIC) buf_puts(b, ";3");
    if (attr & A_ULINE) buf_puts(b, ";4");
    if (attr & A_REV) buf_puts(b, ";7");
    buf_puts(b, ";");
    emit_color(b, fg, 0, truecolor);
    buf_puts(b, ";");
    emit_color(b, bg, 1, truecolor);
    buf_puts(b, "m");
}

static void buf_utf8(Buf *b, uint32_t cp) {
    char tmp[4];
    int n;
    if (cp < 0x80) {
        tmp[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        tmp[0] = (char)(0xF0 | (cp >> 18));
        tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    buf_add(b, tmp, (size_t)n);
}

static void draw_frame(Session *s, Client *c, Grid *scr) {
    Buf b;
    memset(&b, 0, sizeof b);

    /* A client larger than the session gets the screen centred, while a
       smaller client sees the top-left part of the session. */
    int offx = c->cols > scr->w ? (c->cols - scr->w) / 2 : 0;
    int offy = c->rows > scr->h ? (c->rows - scr->h) / 2 : 0;
    int vw = scr->w < c->cols ? scr->w : c->cols;
    int vh = scr->h < c->rows ? scr->h : c->rows;
    if (vw < 1) vw = 1;
    if (vh < 1) vh = 1;

    int invalid = !c->prev_valid || c->prev.w != scr->w || c->prev.h != scr->h;
    if (invalid) {
        if (c->prev_valid) {
            grid_free(&c->prev);
            c->prev_valid = 0;
        }
        grid_init(&c->prev, scr->w, scr->h);
        grid_clear(&c->prev, -2);
        buf_puts(&b, "\x1b[2J\x1b[H");
    }
    {
        char title[160];
        snprintf(title, sizeof title, "[murx] %s", s->cur ? s->cur->name : "murx");
        if (strcmp(title, c->title) != 0) {
            buf_printf(&b, "\x1b]2;%s\x07", title);
            snprintf(c->title, sizeof c->title, "%s", title);
        }
    }

    for (int y = 0; y < vh; y++) {
        int first = -1, last = -1;
        for (int x = 0; x < vw; x++) {
            Cell *a = grid_at(scr, x, y);
            Cell *p = grid_at(&c->prev, x, y);
            if (!cell_same(a, p)) {
                if (first < 0) first = x;
                last = x;
            }
        }
        if (first < 0) continue;
        if (first > 0 && grid_at(scr, first, y)->ch == WIDE_CONT) first--;
        if (last + 1 < vw && grid_at(scr, last + 1, y)->ch == WIDE_CONT) last++;
        buf_printf(&b, "\x1b[%d;%dH", y + 1 + offy, first + 1 + offx);

        int32_t cf = -3, cb = -3;
        int ca = -1;
        for (int x = first; x <= last; x++) {
            Cell *cell = grid_at(scr, x, y);
            if (cell->ch == WIDE_CONT) continue;
            if (cell->fg != cf || cell->bg != cb || cell->attr != ca) {
                emit_sgr(&b, cell->fg, cell->bg, cell->attr, c->truecolor);
                cf = cell->fg;
                cb = cell->bg;
                ca = cell->attr;
            }
            buf_utf8(&b, cell->ch);
        }
        for (int x = first; x <= last; x++) {
            Cell *dst = grid_at(&c->prev, x, y);
            Cell *src = grid_at(scr, x, y);
            *dst = *src;
        }
    }

    int cx, cy, vis;
    cursor_for(s, c, &cx, &cy, &vis);
    /* Match the pane's DECCKM request on every attached terminal. Otherwise
       screen-256color applications such as mc expect ESC O A/B/C/D while
       the outer terminal continues sending ESC [ A/B/C/D. Overlays use
       normal cursor keys; switching panes restores that pane's mode. */
    int app_cursor = !c->locked && c->mode == 0 && !c->prefix_pending &&
                     s->cur && s->cur->active && s->cur->active->app_cursor;
    buf_puts(&b, app_cursor ? "\x1b[?1h" : "\x1b[?1l");
    buf_puts(&b, "\x1b[0m");
    if (vis && (cx >= vw || cy >= vh)) vis = 0;
    if (vis) {
        buf_printf(&b, "\x1b[%d;%dH\x1b[?25h", cy + 1 + offy, cx + 1 + offx);
    } else {
        buf_puts(&b, "\x1b[?25l");
    }
    c->prev_valid = 1;

    for (size_t off = 0; off < b.len;) {
        size_t len = b.len - off;
        if (len > MAX_DRAW_BYTES) len = MAX_DRAW_BYTES;
        if (send_msg(c->fd, S_DRAW, b.p + off, len) < 0) {
            c->dead = 1;
            break;
        }
        off += len;
    }
    free(b.p);
}

/* ------------------------------------------------------------------ */
/* protocol and session plumbing                                      */
/* ------------------------------------------------------------------ */

static int write_all(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len > 0) {
        ssize_t n = write(fd, p, len > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int send_msg(int fd, uint32_t type, const void *data, size_t len) {
    MsgHdr h;
    if (len > UINT32_MAX) { errno = EMSGSIZE; return -1; }
    h.type = type;
    h.len = (uint32_t)len;
    if (write_all(fd, &h, sizeof h) < 0) return -1;
    if (len && write_all(fd, data, len) < 0) return -1;
    return 0;
}

static void short_hostname(char *out, size_t n) {
    if (gethostname(out, n) < 0) snprintf(out, n, "localhost");
    out[n - 1] = '\0';
    char *dot = strchr(out, '.');
    if (dot) *dot = '\0';
}

static void invalidate_clients(Session *s) {
    for (Client *c = s->clients; c; c = c->next) {
        if (c->prev_valid) {
            grid_free(&c->prev);
            c->prev_valid = 0;
        }
    }
}

static void session_recalc_size(Session *s) {
    /* The most recently attached or resized client decides the session size.
       This makes a reattach from a larger terminal grow the session even if an
       older, smaller client is still connected. Such clients render only the
       part of the session that fits (see draw_frame). */
    Client *best = NULL;
    for (Client *c = s->clients; c; c = c->next) {
        if (!c->attached || c->dead || c->fd < 0) continue;
        if (!best || c->order > best->order) best = c;
    }
    if (!best) return;
    int cols = best->cols;
    int rows = best->rows;
    if (cols < MIN_COLS) cols = MIN_COLS;
    if (rows < MIN_ROWS) rows = MIN_ROWS;
    if (cols == s->w && rows == s->h) return;
    s->w = cols;
    s->h = rows;
    s->redraw = 1;
    session_layout(s);
    invalidate_clients(s);
}

static void window_free(Window *w) {
    if (!w) return;
    if (w->root) {
        Pane *list[128];
        int n = 0;
        layout_collect(w->root, list, &n);
        for (int i = 0; i < n; i++) pane_free(list[i]);
        layout_free(w->root);
    }
    free(w);
}

static void select_window(Session *s, Window *w) {
    if (!w || w == s->cur) return;
    s->last = s->cur;
    s->cur = w;
    session_layout(s);
    invalidate_clients(s);
}

static void next_window(Session *s, int dir) {
    if (!s->cur || !s->windows) return;
    Window *w;
    if (dir > 0) {
        w = s->cur->next ? s->cur->next : s->windows;
    } else {
        w = s->windows;
        while (w->next) w = w->next;
        if (s->cur != s->windows) {
            Window *p = s->windows;
            while (p->next && p->next != s->cur) p = p->next;
            w = p;
        }
    }
    select_window(s, w);
}

static void new_window(Session *s) {
    Window *w = window_new(s, NULL);
    if (!w) return;
    s->last = s->cur;
    s->cur = w;
    session_layout(s);
    invalidate_clients(s);
}

static void select_window_index(Session *s, int index) {
    for (Window *w = s->windows; w; w = w->next) {
        if (w->index == index) {
            select_window(s, w);
            return;
        }
    }
}

static void switch_last_window(Session *s) {
    if (!s->last || s->last == s->cur) return;
    Window *t = s->cur;
    s->cur = s->last;
    s->last = t;
    session_layout(s);
    invalidate_clients(s);
}

static Pane *host_pane_new(Session *s, int w, int h) {
    char host[128];
    short_hostname(host, sizeof host);
    return pane_new(s->cwd, w, h, host);
}

static void split_pane(Session *s, int type) {
    Window *w = s->cur;
    if (!w || !w->active) return;
    if (layout_pane_count(w->root) >= 60) return;   /* fixed-size pane lists */
    Pane *old = w->active;
    Layout *leaf = layout_find_leaf(w->root, old);
    if (!leaf) return;
    int nw = old->w / 2;
    int nh = old->h / 2;
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    Pane *np = host_pane_new(s, nw, nh);
    if (!np) return;

    Layout *node = xcalloc(1, sizeof *node);
    Layout *nl = layout_leaf(np);
    node->type = type;
    node->ratio = 0.5;
    node->parent = leaf->parent;
    node->a = leaf;
    node->b = nl;
    leaf->parent = node;
    nl->parent = node;

    if (!node->parent) {
        w->root = node;
    } else if (node->parent->a == leaf) {
        node->parent->a = node;
    } else {
        node->parent->b = node;
    }
    w->active = np;
    window_renumber(s->windows);
    session_layout(s);
    invalidate_clients(s);
}

static void kill_active_pane(Session *s) {
    Window *w = s->cur;
    if (!w || !w->active) return;
    Pane *p = w->active;
    Layout *leaf = layout_find_leaf(w->root, p);
    if (!leaf) return;

    if (leaf == w->root) {
        Window *prev = NULL;
        for (Window *it = s->windows; it; it = it->next) {
            if (it == w) break;
            prev = it;
        }
        Window *next = w->next;
        if (prev) prev->next = next;
        else s->windows = next;
        s->nwin--;
        if (s->cur == w) s->cur = next ? next : prev;
        if (s->last == w) s->last = NULL;
        window_free(w);
        if (!s->windows) s->running = 0;
    } else {
        Layout *par = leaf->parent;
        Layout *sib = (par->a == leaf) ? par->b : par->a;
        if (par->parent) {
            if (par->parent->a == par) par->parent->a = sib;
            else par->parent->b = sib;
        } else {
            w->root = sib;
        }
        sib->parent = par->parent;
        free(leaf);
        free(par);
        pane_free(p);
        w->active = layout_first_pane(w->root);
    }
    window_renumber(s->windows);
    if (s->cur) {
        session_layout(s);
        invalidate_clients(s);
    }
}

static void kill_active_window(Session *s) {
    Window *w = s->cur;
    if (!w) return;
    Window *prev = NULL;
    for (Window *it = s->windows; it; it = it->next) {
        if (it == w) break;
        prev = it;
    }
    Window *next = w->next;
    if (prev) prev->next = next;
    else s->windows = next;
    s->nwin--;
    if (s->cur == w) s->cur = next ? next : prev;
    if (s->last == w) s->last = NULL;
    window_free(w);
    if (!s->windows) s->running = 0;
    window_renumber(s->windows);
    if (s->cur) {
        session_layout(s);
        invalidate_clients(s);
    }
}

static int rect_overlap(int a0, int a1, int b0, int b1) {
    int s = a0 > b0 ? a0 : b0;
    int e = a1 < b1 ? a1 : b1;
    return e - s;
}

/* Gap between two intervals: 0 when they overlap, otherwise the distance
 * between the closest edges. */
static int rect_gap(int a0, int a1, int b0, int b1) {
    if (b0 >= a1) return b0 - a1;
    if (b1 <= a0) return a0 - b1;
    return 0;
}

static void select_pane_dir(Session *s, int dx, int dy) {
    Window *w = s->cur;
    if (!w || !w->active) return;
    Pane *a = w->active;
    Pane *list[128];
    int n = 0;
    layout_collect(w->root, list, &n);

    Pane *best = NULL;
    long best_primary = 0, best_gap = 0, best_center = 0;
    int best_overlap = 0;

    for (int i = 0; i < n; i++) {
        Pane *p = list[i];
        if (p == a) continue;

        int primary, gap, overlap, ac, pc;

        /* Only panes strictly beyond the active pane's edge in the wanted
         * direction are candidates. Comparing centres is not enough: a
         * tall pane beside us extends further down and would look like it
         * is "below", while a wide pane beside us would look like it is
         * "right". */
        if (dx > 0) {
            if (p->x < a->x + a->w) continue;
            primary = p->x - (a->x + a->w);
            gap = rect_gap(a->y, a->y + a->h, p->y, p->y + p->h);
            overlap = rect_overlap(a->y, a->y + a->h, p->y, p->y + p->h);
            ac = a->y * 2 + a->h;
            pc = p->y * 2 + p->h;
        } else if (dx < 0) {
            if (p->x + p->w > a->x) continue;
            primary = a->x - (p->x + p->w);
            gap = rect_gap(a->y, a->y + a->h, p->y, p->y + p->h);
            overlap = rect_overlap(a->y, a->y + a->h, p->y, p->y + p->h);
            ac = a->y * 2 + a->h;
            pc = p->y * 2 + p->h;
        } else if (dy > 0) {
            if (p->y < a->y + a->h) continue;
            primary = p->y - (a->y + a->h);
            gap = rect_gap(a->x, a->x + a->w, p->x, p->x + p->w);
            overlap = rect_overlap(a->x, a->x + a->w, p->x, p->x + p->w);
            ac = a->x * 2 + a->w;
            pc = p->x * 2 + p->w;
        } else if (dy < 0) {
            if (p->y + p->h > a->y) continue;
            primary = a->y - (p->y + p->h);
            gap = rect_gap(a->x, a->x + a->w, p->x, p->x + p->w);
            overlap = rect_overlap(a->x, a->x + a->w, p->x, p->x + p->w);
            ac = a->x * 2 + a->w;
            pc = p->x * 2 + p->w;
        } else {
            continue;
        }

        /* Prefer panes that overlap the active pane on the perpendicular
         * axis, then the closest edge in the wanted direction, then the
         * smallest perpendicular gap and the best centre alignment. */
        int aligned = overlap > 0;
        long center = labs((long)ac - (long)pc);
        int better;
        if (!best) {
            better = 1;
        } else if (aligned != best_overlap) {
            better = aligned > best_overlap;
        } else if (primary != best_primary) {
            better = primary < best_primary;
        } else if (gap != best_gap) {
            better = gap < best_gap;
        } else {
            better = center < best_center;
        }
        if (better) {
            best = p;
            best_overlap = aligned;
            best_primary = primary;
            best_gap = gap;
            best_center = center;
        }
    }

    if (best) {
        w->active = best;
        invalidate_clients(s);
    }
}

static void cycle_pane(Session *s, int dir) {
    Window *w = s->cur;
    if (!w || !w->active) return;
    Pane *list[128];
    int n = 0;
    layout_collect(w->root, list, &n);
    if (n < 2) return;
    int idx = 0;
    for (int i = 0; i < n; i++) if (list[i] == w->active) idx = i;
    idx = (idx + dir + n) % n;
    w->active = list[idx];
    invalidate_clients(s);
}

static void resize_pane_dir(Session *s, int dx, int dy, int amount) {
    Window *w = s->cur;
    if (!w || !w->active) return;
    Layout *leaf = layout_find_leaf(w->root, w->active);
    if (!leaf || !leaf->parent) return;
    int want = (dx != 0) ? L_COLS : L_ROWS;
    Layout *l = leaf->parent;
    while (l && l->type != want) l = l->parent;
    if (!l) return;
    double total = (l->type == L_COLS) ? (double)(l->w - 1) : (double)l->h;
    if (total < 1) return;
    double step = (double)amount / total;
    if (dx < 0 || dy < 0) l->ratio -= step;
    else l->ratio += step;
    if (l->ratio < 0.05) l->ratio = 0.05;
    if (l->ratio > 0.95) l->ratio = 0.95;
    session_layout(s);
    invalidate_clients(s);
}

static void rename_active_window(Session *s, const char *name) {
    if (!s->cur || !name || !*name) return;
    snprintf(s->cur->name, sizeof s->cur->name, "%s", name);
    invalidate_clients(s);
}

/* ------------------------------------------------------------------ */
/* client modes and key bindings                                      */
/* ------------------------------------------------------------------ */

#define K_UP    0x1001
#define K_DOWN  0x1002
#define K_RIGHT 0x1003
#define K_LEFT  0x1004
#define K_BTAB  0x1005
#define K_PGUP  0x1006
#define K_PGDN  0x1007

static void client_invalidate(Client *c) {
    if (c->prev_valid) {
        grid_free(&c->prev);
        c->prev_valid = 0;
    }
}

static void client_detach(Client *c, const char *msg) {
    if (c->fd < 0) return;
    send_msg(c->fd, S_DETACH, msg, size_add(strlen(msg), 1));
    close(c->fd);
    c->fd = -1;
    c->dead = 1;
}

static void client_lock(Client *c) {
    c->locked = 1;
    c->mode = 0;
    c->prefix_pending = 0;
    const char *text = "[murx] locked - press Enter to unlock";
    int tw = text_width(text);
    int x = (c->cols - tw) / 2;
    int y = c->rows / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    char out[512];
    int n = snprintf(out, sizeof out, "\x1b[0m\x1b[2J\x1b[H\x1b[?25l\x1b[%d;%dH%s", y + 1, x + 1, text);
    if (n > 0 && (size_t)n < sizeof out && c->fd >= 0)
        send_msg(c->fd, S_DRAW, out, (size_t)n);
    client_invalidate(c);
}

static void client_unlock(Client *c) {
    c->locked = 0;
    if (c->fd >= 0) {
        const char *seq = "\x1b[2J\x1b[H";
        send_msg(c->fd, S_DRAW, seq, strlen(seq));
    }
    client_invalidate(c);
}

static void start_menu(Client *c, Session *s) {
    c->mode = 2;
    c->menu_sel = 0;
    int i = 0;
    for (Window *w = s->windows; w; w = w->next) {
        if (w == s->cur) c->menu_sel = i;
        i++;
    }
}

static void start_rename_prompt(Client *c, Session *s) {
    if (!s->cur) return;
    c->mode = 1;
    c->prompt_kind = 1;
    snprintf(c->prompt, sizeof c->prompt, "rename-window: %s", s->cur->name);
    c->prompt_len = (int)strlen(c->prompt);
    c->prompt_label_len = (int)strlen("rename-window: ");
}

static void start_confirm(Client *c, int action, const char *text) {
    c->mode = 3;
    c->confirm_action = action;
    snprintf(c->prompt, sizeof c->prompt, "%s", text);
    c->prompt_len = (int)strlen(c->prompt);
    c->prompt_label_len = c->prompt_len;
}

static void apply_prompt(Client *c, Session *s) {
    if (c->prompt_kind == 1) {
        const char *name = c->prompt + c->prompt_label_len;
        if (*name) rename_active_window(s, name);
    } else if (c->prompt_kind == 2) {
        const char *text = c->prompt + c->prompt_label_len;
        char *end;
        errno = 0;
        long value = strtol(text, &end, 10);
        if (errno == ERANGE || end == text || *end || value < 0 || value > INT_MAX) {
            c->mode = 0;
            return;
        }
        int idx = (int)value;
        for (Window *w = s->windows; w; w = w->next) {
            if (w->index == idx) {
                select_window(s, w);
                break;
            }
        }
    }
    c->mode = 0;
}

static void apply_confirm(Client *c, Session *s) {
    int action = c->confirm_action;
    c->mode = 0;
    if (action == 1) {
        kill_active_pane(s);
    } else if (action == 2) {
        s->running = 0;
    } else if (action == 3) {
        kill_active_window(s);
    }
}

static void backscroll_start(Client *c, Session *s, int page_up) {
    Pane *p = s->cur ? s->cur->active : NULL;
    if (!p) return;
    int count = p->on_alt ? 0 : p->history_count;
    grid_free(&c->backscroll);
    grid_init(&c->backscroll, p->grid->w, count + p->grid->h);
    for (int y = 0; y < count + p->grid->h; y++) {
        Grid *row = y < count ? &p->history[(p->history_start + y) % HISTORY_LINES] : p->grid;
        int sy = y < count ? 0 : y - count;
        int width = row->w < c->backscroll.w ? row->w : c->backscroll.w;
        memcpy(&c->backscroll.c[y * c->backscroll.w], &row->c[sy * row->w],
               (size_t)width * sizeof(Cell));
        Cell *last = grid_at(&c->backscroll, c->backscroll.w - 1, y);
        if (cp_width(last->ch) == 2) *last = blank_cell();
    }
    c->backscroll_pane = p;
    c->backscroll_offset = page_up ? p->h : 0;
    if (c->backscroll_offset > count) c->backscroll_offset = count;
    c->mode = 4;
}

/* Return zero for an incomplete page-key sequence. */
static size_t backscroll_key(Client *c, Session *s, const unsigned char *buf, size_t n) {
    if (!s->cur || s->cur->active != c->backscroll_pane) {
        c->mode = 0;
        grid_free(&c->backscroll);
        return 1;
    }
    if (buf[0] == 27 && n == 1) return 0;
    if (buf[0] == 'q' || buf[0] == 3 || buf[0] == '\r' || buf[0] == '\n' ||
        (buf[0] == 27 && (n == 1 || buf[1] != '['))) {
        c->mode = 0;
        grid_free(&c->backscroll);
        return 1;
    }
    if (buf[0] == 27 && n >= 2 && buf[1] == '[') {
        size_t used = 2;
        while (used < n && !(buf[used] >= 0x40 && buf[used] <= 0x7e)) used++;
        if (used == n) return 0;
        used++;
        if (used == 4 && buf[3] == '~' && (buf[2] == '5' || buf[2] == '6')) {
            int height = s->cur->active->h;
            int max = c->backscroll.h - height;
            if (max < 0) max = 0;
            c->backscroll_offset += buf[2] == '5' ? height : -height;
            if (c->backscroll_offset < 0) c->backscroll_offset = 0;
            if (c->backscroll_offset > max) c->backscroll_offset = max;
        }
        return used;
    }
    return 1;
}

static void prefix_command(Client *c, Session *s, int key) {
    switch (key) {
    case '[':
    case K_PGUP:
        backscroll_start(c, s, key == K_PGUP);
        break;
    case 'a':
        if (s->cur && s->cur->active) {
            unsigned char b = c->prefix;
            pane_write(s->cur->active, &b, 1);
        }
        break;
    case 'c':
    case 0x03:
        new_window(s);
        break;
    case 'x':
    case 0x18:
        client_lock(c);
        break;
    case 0x04:
    case 'd': {
        char m[128];
        snprintf(m, sizeof m, "[detached (from session %s)]", s->name);
        client_detach(c, m);
        break;
    }
    case '*': {
        int n = 0;
        for (Client *it = s->clients; it; it = it->next) if (it->attached) n++;
        client_message(c, "%d client%s attached", n, n == 1 ? "" : "s");
        break;
    }
    case 0x00:
    case 0x0e:
    case ' ':
    case 'n':
        next_window(s, 1);
        break;
    case 'A':
        start_rename_prompt(c, s);
        break;
    case 0x10:
    case 'p':
    case 0x7f:
        next_window(s, -1);
        break;
    case 0x17:
    case 'w':
        start_menu(c, s);
        break;
    case '\\':
        start_confirm(c, 2, "kill-server? (y/n) ");
        break;
    case 'K':
        start_confirm(c, 1, "kill-pane? (y/n) ");
        break;
    case '&':
        start_confirm(c, 3, "kill-window? (y/n) ");
        break;
    case 'k':
        select_pane_dir(s, 0, -1);
        break;
    case 'j':
        select_pane_dir(s, 0, 1);
        break;
    case 'h':
        select_pane_dir(s, -1, 0);
        break;
    case 'l':
        select_pane_dir(s, 1, 0);
        break;
    case 0x0b:
        resize_pane_dir(s, 0, -1, 5);
        break;
    case 0x0a:
        resize_pane_dir(s, 0, 1, 5);
        break;
    case 0x08:
        resize_pane_dir(s, -1, 0, 8);
        break;
    case 0x0c:
        resize_pane_dir(s, 1, 0, 8);
        break;
    case 'r':
        client_message(c, "Configuration Reloaded!");
        break;
    case 'R':
        /* Make this client's terminal the size source again, even if another
           client attached or resized more recently. */
        c->order = client_next_order(s);
        session_recalc_size(s);
        invalidate_clients(s);
        s->redraw = 1;
        client_message(c, "Session resized to %dx%d", s->w, s->h);
        break;
    case '-':
        split_pane(s, L_ROWS);
        break;
    case '|':
    case '%':
        split_pane(s, L_COLS);
        break;
    case '\t':
        cycle_pane(s, 1);
        break;
    case K_BTAB:
        cycle_pane(s, -1);
        break;
    case '"':
        start_menu(c, s);
        break;
    case K_UP:
        select_pane_dir(s, 0, -1);
        break;
    case K_DOWN:
        select_pane_dir(s, 0, 1);
        break;
    case K_LEFT:
        select_pane_dir(s, -1, 0);
        break;
    case K_RIGHT:
        select_pane_dir(s, 1, 0);
        break;
    default:
        if (key >= '0' && key <= '9') select_window_index(s, key - '0');
        break;
    }
}

/* returns bytes consumed, 0 = need more input */
static int handle_prefix_key(Client *c, Session *s, const unsigned char *buf, size_t n) {
    if (n == 0) return 0;
    unsigned char b = buf[0];
    if (b == c->prefix) {
        c->prefix_pending = 0;
        switch_last_window(s);
        return 1;
    }
    int key = b;
    int used = 1;
    if (b == 0x1b) {
        if (n < 2) return 0;
        else if (buf[1] == '[' || buf[1] == 'O') {
            if (n < 3) return 0;
            switch (buf[2]) {
            case 'A': key = K_UP; break;
            case 'B': key = K_DOWN; break;
            case 'C': key = K_RIGHT; break;
            case 'D': key = K_LEFT; break;
            case 'Z': key = K_BTAB; break;
            default: key = 0; break;
            }
            used = 3;
            if (buf[1] == '[' && (buf[2] == '5' || buf[2] == '6')) {
                if (n < 4) return 0;
                key = buf[3] == '~' ? (buf[2] == '5' ? K_PGUP : K_PGDN) : 0;
                used = 4;
            }
        } else {
            key = 0;
            used = 1;
        }
    }
    c->prefix_pending = 0;
    if (key) prefix_command(c, s, key);
    return used;
}

static void prompt_key(Client *c, Session *s, const unsigned char *buf, size_t n, size_t *used) {
    *used = 1;
    unsigned char b = buf[0];
    if (b == 0x1b) {
        if (n < 2) {
            c->mode = 0;
            *used = 1;
            return;
        }
        if ((buf[1] == '[' || buf[1] == 'O') && n < 3) {
            *used = 0;
            return;
        }
        c->mode = 0;
        *used = (buf[1] == '[' || buf[1] == 'O') ? 3 : 1;
        return;
    }
    if (b == '\r' || b == '\n') {
        apply_prompt(c, s);
        return;
    }
    if (b == 0x03) {
        c->mode = 0;
        return;
    }
    if (b == 0x7f || b == 0x08) {
        if (c->prompt_len > c->prompt_label_len) c->prompt[--c->prompt_len] = '\0';
        return;
    }
    if (b == 0x15) {
        c->prompt_len = c->prompt_label_len;
        c->prompt[c->prompt_len] = '\0';
        return;
    }
    if (b >= 0x20 && c->prompt_len < (int)sizeof c->prompt - 2) {
        c->prompt[c->prompt_len++] = (char)b;
        c->prompt[c->prompt_len] = '\0';
    }
}

static void confirm_key(Client *c, Session *s, unsigned char b) {
    if (b == 'y' || b == 'Y' || b == '\r' || b == '\n') {
        apply_confirm(c, s);
    } else if (b == 'n' || b == 'N' || b == 0x1b || b == 0x03) {
        c->mode = 0;
    }
}

static void menu_key(Client *c, Session *s, const unsigned char *buf, size_t n, size_t *used) {
    *used = 1;
    unsigned char b = buf[0];
    int count = 0;
    for (Window *w = s->windows; w; w = w->next) count++;
    if (count < 1) {
        c->mode = 0;
        return;
    }

    if (b == 0x1b) {
        int key = 0;
        if (b == 0x1b) {
            if (n < 2) { *used = 0; return; }
            if (buf[1] == '[' || buf[1] == 'O') {
                if (n < 3) { *used = 0; return; }
                if (buf[2] == 'A') key = K_UP;
                else if (buf[2] == 'B') key = K_DOWN;
                *used = 3;
                if (!key) { c->mode = 0; return; }
            } else {
                c->mode = 0;
                *used = 1;
                return;
            }
        } else {
            key = b;
        }
        if (key == K_UP) {
            c->menu_sel--;
            if (c->menu_sel < 0) c->menu_sel = count - 1;
        } else {
            c->menu_sel++;
            if (c->menu_sel >= count) c->menu_sel = 0;
        }
        return;
    }
    if (b == 'k') { c->menu_sel = (c->menu_sel + count - 1) % count; return; }
    if (b == 'j') { c->menu_sel = (c->menu_sel + 1) % count; return; }
    if (b == 'q') { c->mode = 0; return; }
    if (b == '\r' || b == '\n') {
        int i = 0;
        for (Window *w = s->windows; w; w = w->next) {
            if (i == c->menu_sel) {
                select_window(s, w);
                break;
            }
            i++;
        }
        c->mode = 0;
        return;
    }
}

static void client_feed(Client *c, Session *s, const unsigned char *buf, size_t n) {
    if (n > sizeof c->in - c->inlen) n = sizeof c->in - c->inlen;
    if (n == 0 && c->inlen == 1 && c->in[0] == 27 && c->escape_since &&
        now_ms() - c->escape_since >= 100) {
        if (c->mode == 4) {
            c->mode = 0;
            grid_free(&c->backscroll);
        }
        c->prefix_pending = 0;
        c->inlen = 0;
        c->escape_since = 0;
        return;
    }
    if (n == 0) return;
    memcpy(c->in + c->inlen, buf, n);
    c->inlen += n;

    size_t i = 0;
    while (i < c->inlen) {
        unsigned char *p = c->in + i;
        size_t left = c->inlen - i;
        if (c->locked) {
            if (p[0] == '\r' || p[0] == '\n') client_unlock(c);
            i++;
            continue;
        }
        if (c->dead) break;
        if (c->mode == 4) {
            size_t used = backscroll_key(c, s, p, left);
            if (used == 0) break;
            i += used;
            continue;
        }
        if (c->mode == 1) {
            size_t used = 0;
            prompt_key(c, s, p, left, &used);
            if (used == 0) break;
            i += used;
            continue;
        }
        if (c->mode == 2) {
            size_t used = 0;
            menu_key(c, s, p, left, &used);
            if (used == 0) break;
            i += used;
            continue;
        }
        if (c->mode == 3) {
            confirm_key(c, s, p[0]);
            i++;
            continue;
        }
        if (c->prefix_pending) {
            int used = handle_prefix_key(c, s, p, left);
            if (used == 0) break;
            i += (size_t)used;
            continue;
        }
        if (p[0] == c->prefix) {
            c->prefix_pending = 1;
            i++;
            continue;
        }
        size_t j = i;
        while (j < c->inlen && c->in[j] != c->prefix) j++;
        if (s->cur && s->cur->active) pane_write(s->cur->active, c->in + i, j - i);
        i = j;
    }
    if (i > 0) {
        memmove(c->in, c->in + i, c->inlen - i);
        c->inlen -= i;
    }
    if (c->inlen == 1 && c->in[0] == 27 && (c->mode == 4 || c->prefix_pending)) {
        if (!c->escape_since) c->escape_since = now_ms();
    } else c->escape_since = 0;
}

/* ------------------------------------------------------------------ */
/* server                                                             */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_term;

static void term_handler(int sig) {
    (void)sig;
    g_term = 1;
}

static int session_pane_count(Session *s) {
    int n = 0;
    for (Window *w = s->windows; w; w = w->next) n = count_add(n, layout_pane_count(w->root));
    return n;
}

static void session_panes(Session *s, Pane **out, int max, int *n) {
    *n = 0;
    for (Window *w = s->windows; w; w = w->next) {
        Pane *tmp[128];
        int tn = 0;
        layout_collect(w->root, tmp, &tn);
        for (int i = 0; i < tn && *n < max; i++) out[(*n)++] = tmp[i];
    }
}

static void unlink_window(Session *s, Window *w) {
    Window *prev = NULL;
    for (Window *it = s->windows; it; it = it->next) {
        if (it == w) break;
        prev = it;
    }
    Window *next = w->next;
    if (prev) prev->next = next;
    else s->windows = next;
    s->nwin--;
    if (s->cur == w) s->cur = next ? next : prev;
    if (s->last == w) s->last = NULL;
    window_free(w);
    window_renumber(s->windows);
}

static void remove_pane_ptr(Session *s, Pane *p) {
    for (Window *w = s->windows; w; w = w->next) {
        Layout *leaf = layout_find_leaf(w->root, p);
        if (!leaf) continue;
        if (leaf == w->root) {
            unlink_window(s, w);
        } else {
            Layout *par = leaf->parent;
            Layout *sib = (par->a == leaf) ? par->b : par->a;
            if (par->parent) {
                if (par->parent->a == par) par->parent->a = sib;
                else par->parent->b = sib;
            } else {
                w->root = sib;
            }
            sib->parent = par->parent;
            free(leaf);
            free(par);
            pane_free(p);
            w->active = layout_first_pane(w->root);
            window_renumber(s->windows);
        }
        if (!s->windows) s->running = 0;
        if (s->cur) session_layout(s);
        invalidate_clients(s);
        return;
    }
}

static void client_remove(Session *s, Client *c) {
    Client *prev = NULL;
    for (Client *it = s->clients; it; it = it->next) {
        if (it == c) break;
        prev = it;
    }
    if (prev) prev->next = c->next;
    else s->clients = c->next;
    if (c->fd >= 0) close(c->fd);
    if (c->prev_valid) grid_free(&c->prev);
    grid_free(&c->backscroll);
    free(c);
    session_recalc_size(s);
    invalidate_clients(s);
}

static Client *client_new(int fd) {
    Client *c = xcalloc(1, sizeof *c);
    c->fd = fd;
    c->prefix = DEFAULT_PREFIX;
    return c;
}

static void handle_message(Client *c, Session *s, uint32_t type, const unsigned char *data, uint32_t len) {
    if (type == C_ATTACH) {
        if (len >= 8) {
            uint32_t cols, rows, flags = 0;
            memcpy(&cols, data, 4);
            memcpy(&rows, data + 4, 4);
            if (len >= 12) memcpy(&flags, data + 8, 4);
            uint32_t prefix = DEFAULT_PREFIX;
            if (len >= 16) memcpy(&prefix, data + 12, 4);
            if (prefix < 1 || prefix > 26) { c->dead = 1; return; }
            c->prefix = (unsigned char)prefix;
            /* 0 preserves the session position; 1 selects top, 2 bottom. */
            uint32_t status = 0;
            if (len >= 20) memcpy(&status, data + 16, 4);
            if (status > 2) { c->dead = 1; return; }
            if (status) {
                s->status_bottom = status == 2;
                session_layout(s);
            }
            if (cols < 1) cols = 1;
            if (rows < 1) rows = 1;
            if (cols > MAX_DIMENSION) cols = MAX_DIMENSION;
            if (rows > MAX_DIMENSION) rows = MAX_DIMENSION;
            c->cols = (int)cols;
            c->rows = (int)rows;
            c->truecolor = (flags & 1) != 0;
            c->attached = 1;
            c->order = client_next_order(s);
            client_invalidate(c);
            session_recalc_size(s);
            invalidate_clients(s);
        }
    } else if (type == C_RESIZE) {
        if (len >= 8) {
            uint32_t cols, rows;
            memcpy(&cols, data, 4);
            memcpy(&rows, data + 4, 4);
            if (cols < 1) cols = 1;
            if (rows < 1) rows = 1;
            if (cols > MAX_DIMENSION) cols = MAX_DIMENSION;
            if (rows > MAX_DIMENSION) rows = MAX_DIMENSION;
            c->cols = (int)cols;
            c->rows = (int)rows;
            c->order = client_next_order(s);
            client_invalidate(c);
            session_recalc_size(s);
            invalidate_clients(s);
        }
    } else if (type == C_INPUT) {
        client_feed(c, s, data, len);
    }
}

static void client_read_socket(Client *c, Session *s) {
    unsigned char tmp[16384];
    ssize_t n = read(c->fd, tmp, sizeof tmp);
    if (n <= 0) {
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
        c->dead = 1;
        return;
    }
    if ((size_t)n > sizeof c->net - c->netlen) {
        c->dead = 1;
        return;
    }
    memcpy(c->net + c->netlen, tmp, (size_t)n);
    c->netlen += (size_t)n;

    size_t off = 0;
    while (c->netlen - off >= sizeof(MsgHdr)) {
        MsgHdr h;
        memcpy(&h, c->net + off, sizeof h);
        if (h.len > sizeof c->net - sizeof h) { c->dead = 1; return; }
        if (h.len > c->netlen - off - sizeof h) break;
        handle_message(c, s, h.type, c->net + off + sizeof h, h.len);
        off += sizeof h + h.len;
    }
    if (off > 0) {
        memmove(c->net, c->net + off, c->netlen - off);
        c->netlen -= off;
    }
}

static void accept_client(Session *s) {
    int fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0) return;
    Client *c = client_new(fd);
    c->next = s->clients;
    s->clients = c;
}

static void server_notify_all(Session *s, const char *msg) {
    for (Client *c = s->clients; c; c = c->next) {
        if (c->fd >= 0) send_msg(c->fd, S_DETACH, msg, size_add(strlen(msg), 1));
    }
}

static void run_server(Session *s) {
    int maxpanes = count_add(session_pane_count(s), 64);
    Pane **panes = xcalloc((size_t)maxpanes, sizeof(Pane *));
    unsigned char rbuf[65536];
    int dirty = 1;

    while (s->running && !g_term) {
        int need = session_pane_count(s);
        if (need > maxpanes - 8) {
            maxpanes = count_add(need, 64);
            panes = xrealloc(panes, size_mul((size_t)maxpanes, sizeof(Pane *)));
        }
        int np = 0;
        session_panes(s, panes, maxpanes, &np);

        /* reap shells; a background child may hold the pty open */
        int reaped = 0;
        for (;;) {
            int st;
            pid_t r = waitpid(-1, &st, WNOHANG);
            if (r <= 0) break;
            for (int i = 0; i < np; i++) {
                Pane *p = panes[i];
                if (p->pid != r) continue;
                p->pid = -1;
                for (;;) {
                    ssize_t n = read(p->fd, rbuf, sizeof rbuf);
                    if (n > 0) em_feed(p, rbuf, (size_t)n);
                    else break;
                }
                p->alive = 0;
                reaped = 1;
                break;
            }
        }
        if (reaped) {
            for (int i = 0; i < np; i++) {
                if (!panes[i]->alive) remove_pane_ptr(s, panes[i]);
            }
            dirty = 1;
            continue;
        }

        int nclients = 0;
        for (Client *c = s->clients; c; c = c->next) nclients = count_add(nclients, 1);
        int nfds = count_add(count_add(nclients, np), 1);
        struct pollfd *pfds = xcalloc((size_t)nfds, sizeof(struct pollfd));
        Client **cmap = xcalloc((size_t)nclients + 1, sizeof(Client *));
        int idx = 0;
        pfds[idx].fd = s->listen_fd;
        pfds[idx].events = POLLIN;
        idx++;
        int client_poll_count = 0;
        for (Client *c = s->clients; c; c = c->next) {
            if (c->fd >= 0) {
                pfds[idx].fd = c->fd;
                pfds[idx].events = POLLIN;
                cmap[client_poll_count] = c;
                client_poll_count++;
                idx++;
            }
        }
        for (int i = 0; i < np; i++) {
            pfds[idx].fd = panes[i]->fd;
            pfds[idx].events = POLLIN;
            idx++;
        }

        int timeout = 200;
        int pr = poll(pfds, (nfds_t)idx, timeout);
        if (pr < 0) {
            int interrupted = errno == EINTR;
            free(pfds);
            free(cmap);
            if (interrupted) continue;
            break;
        }

        if (pr > 0 && (pfds[0].revents & POLLIN)) {
            accept_client(s);
            dirty = 1;
        }
        for (int i = 0; i < client_poll_count; i++) {
            if (pfds[1 + i].revents & (POLLIN | POLLHUP | POLLERR)) {
                client_read_socket(cmap[i], s);
                dirty = 1;
            }
        }
        for (int i = 0; i < np; i++) {
            struct pollfd *pf = &pfds[1 + client_poll_count + i];
            if (!(pf->revents & (POLLIN | POLLHUP | POLLERR))) continue;
            for (;;) {
                ssize_t n = read(pf->fd, rbuf, sizeof rbuf);
                if (n > 0) {
                    em_feed(panes[i], rbuf, (size_t)n);
                    dirty = 1;
                    if ((size_t)n < sizeof rbuf) break;
                } else if (n < 0 && (errno == EINTR)) {
                    continue;
                } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    break;
                } else {
                    panes[i]->alive = 0;
                    break;
                }
            }
            if (!panes[i]->alive) {
                Pane *dead = panes[i];
                dirty = 1;
                remove_pane_ptr(s, dead);
                break;
            }
        }

        free(pfds);
        free(cmap);

        for (Client *it = s->clients; it; it = it->next) {
            if (it->escape_since && now_ms() - it->escape_since >= 100) {
                client_feed(it, s, NULL, 0);
                dirty = 1;
            }
        }

        /* Reap dead clients before drawing so a removed client's size change
           is reflected in this frame instead of waiting for the next event. */
        Client *c = s->clients;
        while (c) {
            Client *next = c->next;
            if (c->dead) client_remove(s, c);
            c = next;
        }
        if (s->redraw) {
            s->redraw = 0;
            dirty = 1;
        }

        long long t = now_ms();
        if (t - s->last_name_check >= NAME_CHECK_MS) {
            s->last_name_check = t;
            for (Window *w = s->windows; w; w = w->next) {
                if (window_update_name(s, w)) dirty = 1;
            }
        }
        if (t - s->last_status >= STATUS_INTERVAL * 1000) {
            s->last_status = t;
            dirty = 1;
        }

        if (dirty) {
            for (Client *c = s->clients; c; c = c->next) {
                if (!c->attached || c->dead || c->locked || c->fd < 0) continue;
                Grid scr;
                compose(s, c, &scr);
                draw_frame(s, c, &scr);
                grid_free(&scr);
            }
            dirty = 0;
        }
    }

    char msg[128];
    snprintf(msg, sizeof msg, "[murx: session \"%s\" server exited]", s->name);
    server_notify_all(s, msg);
    free(panes);
}

static void server_cleanup(Session *s) {
    Client *c = s->clients;
    while (c) {
        Client *next = c->next;
        if (c->fd >= 0) close(c->fd);
        if (c->prev_valid) grid_free(&c->prev);
        grid_free(&c->backscroll);
        free(c);
        c = next;
    }
    s->clients = NULL;
    Window *w = s->windows;
    while (w) {
        Window *next = w->next;
        window_free(w);
        w = next;
    }
    s->windows = NULL;
    if (s->listen_fd >= 0) close(s->listen_fd);
    unlink(s->sock_path);
}

/* Tell the spawning client why the server could not come up. */
static void server_fail(int readyfd, const char *stage, int err) {
    if (readyfd < 0) return;
    char buf[200];
    int n = snprintf(buf, sizeof buf, "%s: %s", stage, strerror(err));
    if (n > 0) {
        if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
        ssize_t w = write(readyfd, buf, (size_t)n);
        (void)w;
    }
    close(readyfd);
}

static void server_start(const char *path, const char *name, int readyfd, const DisplayOptions *display) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = term_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) {
        server_fail(readyfd, "socket", errno);
        _exit(1);
    }
    struct sockaddr_un saun;
    memset(&saun, 0, sizeof saun);
    saun.sun_family = AF_UNIX;
    size_t path_len = strlen(path);
    if (path_len >= sizeof saun.sun_path) {
        close(lfd);
        server_fail(readyfd, "socket path", ENAMETOOLONG);
        _exit(1);
    }
    memcpy(saun.sun_path, path, path_len + 1);
    unlink(path);
    /* Protect the socket without imposing private file permissions on pane
       shells and their build output (rsync -a preserves those permissions). */
    mode_t saved_umask = umask(077);
    int bound = bind(lfd, (struct sockaddr *)&saun, sizeof saun);
    int bind_err = errno;
    umask(saved_umask);
    if (bound < 0) {
        close(lfd);
        server_fail(readyfd, "bind", bind_err);
        _exit(1);
    }
    if (listen(lfd, 16) < 0) {
        int err = errno;
        close(lfd);
        server_fail(readyfd, "listen", err);
        _exit(1);
    }
    chmod(path, 0600);

    Session *s = xcalloc(1, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name);
    snprintf(s->sock_path, sizeof s->sock_path, "%s", path);
    if (!getcwd(s->cwd, sizeof s->cwd)) snprintf(s->cwd, sizeof s->cwd, "/");
    s->listen_fd = lfd;
    s->w = 80;
    s->h = 24;
    s->running = 1;
    s->display = *display;
    s->last_status = now_ms();

    Window *w = window_new(s, NULL);
    if (!w) {
        server_cleanup(s);
        if (readyfd >= 0) close(readyfd);
        _exit(1);
    }
    s->cur = w;
    session_layout(s);

    if (readyfd >= 0) {
        char ok = '1';
        write_all(readyfd, &ok, 1);
        close(readyfd);
    }

    run_server(s);
    server_cleanup(s);
    free(s);
    _exit(0);
}

/* ------------------------------------------------------------------ */
/* client                                                             */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_winch;
static volatile sig_atomic_t g_client_quit;
/* Self-pipe used to wake the client's poll() from the SIGWINCH handler. The
 * handler can arrive in the small window between checking g_winch and entering
 * poll(), which would otherwise leave the resize pending until the next key or
 * pane output. Writing a byte makes that race harmless. */
static int g_winch_pipe[2] = { -1, -1 };

static void winch_handler(int sig) {
    (void)sig;
    int saved = errno;
    g_winch = 1;
    if (g_winch_pipe[1] >= 0) {
        char b = 1;
        ssize_t r = write(g_winch_pipe[1], &b, 1);
        (void)r;
    }
    errno = saved;
}

static void client_quit_handler(int sig) {
    (void)sig;
    g_client_quit = 1;
}

static int connect_session(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    size_t path_len = strlen(path);
    if (path_len >= sizeof sa.sun_path) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(sa.sun_path, path, path_len + 1);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int session_name_compare(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static int list_sessions(void) {
    char dirs[2][PATH_MAX];
    socket_dir(dirs[0], sizeof dirs[0]);
    socket_dir_at("/tmp", dirs[1], sizeof dirs[1]);
    char (*names)[49] = NULL;
    size_t count = 0;
    int result = 0;
    for (int i = 0; i < 2; i++) {
        if (i == 1 && strcmp(dirs[0], dirs[1]) == 0) continue;
        DIR *dir = opendir(dirs[i]);
        if (!dir) {
            if (errno != ENOENT) {
                fprintf(stderr, "murx: cannot list %s: %s\n", dirs[i], strerror(errno));
                result = 1;
            }
            continue;
        }
        struct dirent *entry;
        for (;;) {
            errno = 0;
            entry = readdir(dir);
            if (!entry) {
                if (errno) {
                    fprintf(stderr, "murx: cannot read %s: %s\n", dirs[i], strerror(errno));
                    result = 1;
                }
                break;
            }
            size_t len = strlen(entry->d_name);
            if (len <= 5 || len > 53 || strcmp(entry->d_name + len - 5, ".sock") != 0)
                continue;
            char name[49];
            memcpy(name, entry->d_name, len - 5);
            name[len - 5] = '\0';
            if (!valid_session_name(name)) continue;
            struct sockaddr_un sa;
            memset(&sa, 0, sizeof sa);
            sa.sun_family = AF_UNIX;
            size_t dirlen = strlen(dirs[i]);
            if (dirlen + len + 2 > sizeof sa.sun_path) continue;
            memcpy(sa.sun_path, dirs[i], dirlen);
            sa.sun_path[dirlen] = '/';
            memcpy(sa.sun_path + dirlen + 1, entry->d_name, len + 1);
            struct stat st;
            if (lstat(sa.sun_path, &st) < 0 || !S_ISSOCK(st.st_mode) || st.st_uid != getuid())
                continue;
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) continue;
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                close(fd);
                continue;
            }
            int connected = connect(fd, (struct sockaddr *)&sa, sizeof sa);
            int live = connected == 0 || errno == EINPROGRESS || errno == EAGAIN;
            close(fd);
            if (!live) continue;
            names = xrealloc(names, size_mul(size_add(count, 1), sizeof *names));
            memcpy(names[count++], name, len - 4);
        }
        closedir(dir);
    }
    if (count > 1) qsort(names, count, sizeof *names, session_name_compare);
    for (size_t i = 0; i < count; i++) {
        if (!i || strcmp(names[i], names[i - 1]) != 0) puts(names[i]);
    }
    free(names);
    if (ferror(stdout)) result = 1;
    return result;
}

static int ensure_socket_dir(void) {
    char dir[PATH_MAX];
    socket_dir(dir, sizeof dir);
    if (mkdir(dir, 0700) < 0 && errno != EEXIST) return -1;
    chmod(dir, 0700);
    char alt[PATH_MAX];
    socket_dir_at("/tmp", alt, sizeof alt);
    if (strcmp(alt, dir) != 0) {
        if (mkdir(alt, 0700) == 0) chmod(alt, 0700);
    }
    return 0;
}

static int spawn_server(const char *path, const char *name, const DisplayOptions *display) {
    if (ensure_socket_dir() < 0) return -1;
    char dir[PATH_MAX], lockpath[PATH_MAX + 64];
    socket_dir(dir, sizeof dir);
    snprintf(lockpath, sizeof lockpath, "%s/.spawn-%s.lock", dir, name);
    int lock = open(lockpath, O_CREAT | O_RDWR, 0600);
    if (lock < 0) return -1;
    if (flock(lock, LOCK_EX) < 0) {
        close(lock);
        return -1;
    }

    int fd = connect_session(path);
    if (fd >= 0) {
        flock(lock, LOCK_UN);
        close(lock);
        return fd;
    }

    int p[2];
    if (pipe(p) < 0) {
        flock(lock, LOCK_UN);
        close(lock);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        flock(lock, LOCK_UN);
        close(lock);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        setsid();
        pid_t pid2 = fork();
        if (pid2 < 0) _exit(1);
        if (pid2 > 0) _exit(0);
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) {
            dup2(dn, 0);
            dup2(dn, 1);
            dup2(dn, 2);
            if (dn > 2) close(dn);
        }
        close(lock);
        server_start(path, name, p[1], display);
        _exit(1);
    }
    close(p[1]);
    char ok = 0;
    ssize_t got;
    do {
        got = read(p[0], &ok, 1);
    } while (got < 0 && errno == EINTR);
    close(p[0]);
    waitpid(pid, NULL, 0);
    flock(lock, LOCK_UN);
    close(lock);

    if (got != 1 || ok != '1') {
        errno = EIO;
        return -1;
    }
    for (int i = 0; i < 300; i++) {
        fd = connect_session(path);
        if (fd >= 0) return fd;
        usleep(10000);
    }
    return -1;
}

static void tty_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

static int tty_raw(struct termios *saved) {
    if (tcgetattr(STDIN_FILENO, saved) < 0) return -1;
    struct termios raw = *saved;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) return -1;
    return 0;
}

static void tty_restore(struct termios *saved) {
    const char *seq = "\x1b[?1l\x1b[0m\x1b[?25h\x1b[?1049l";
    write_all(STDOUT_FILENO, seq, strlen(seq));
    tcsetattr(STDIN_FILENO, TCSAFLUSH, saved);
}

typedef struct {
    unsigned char *p;
    size_t len, cap;
} NetBuf;

static void net_append(NetBuf *nb, const unsigned char *data, size_t n) {
    size_t need = size_add(nb->len, n);
    if (need > nb->cap) {
        size_t cap = buffer_capacity(nb->cap ? nb->cap : 65536, need);
        nb->p = xrealloc(nb->p, cap);
        nb->cap = cap;
    }
    memcpy(nb->p + nb->len, data, n);
    nb->len += n;
}

static void net_consume(NetBuf *nb, size_t n) {
    if (n >= nb->len) {
        nb->len = 0;
        return;
    }
    memmove(nb->p, nb->p + n, nb->len - n);
    nb->len -= n;
}

static int terminal_truecolor(void) {
    const char *ct = getenv("COLORTERM");
    const char *term = getenv("TERM");
    if (!ct || (strcmp(ct, "truecolor") != 0 && strcmp(ct, "24bit") != 0)) return 0;
    /* screen/tmux/linux often advertise COLORTERM without passing RGB
       through; prefer 256 colours there so backgrounds are not dropped. */
    if (term && (!strncmp(term, "screen", 6) || !strncmp(term, "tmux", 4) ||
                 !strncmp(term, "linux", 5) || !strncmp(term, "dumb", 4))) {
        return 0;
    }
    return 1;
}

static int run_client(int fd, const char *name, unsigned char prefix, int status) {
    struct termios saved;
    if (tty_raw(&saved) < 0) {
        fprintf(stderr, "murx: not a terminal\n");
        return 1;
    }

    /* Wake poll() for SIGWINCH without relying on the signal landing while it
       is blocked. Failure is fine: the g_winch check still works, just with
       the old race. */
    if (pipe(g_winch_pipe) == 0) {
        fcntl(g_winch_pipe[0], F_SETFL, fcntl(g_winch_pipe[0], F_GETFL, 0) | O_NONBLOCK);
        fcntl(g_winch_pipe[1], F_SETFL, fcntl(g_winch_pipe[1], F_GETFL, 0) | O_NONBLOCK);
    } else {
        g_winch_pipe[0] = g_winch_pipe[1] = -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = winch_handler;
    sigaction(SIGWINCH, &sa, NULL);
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = client_quit_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    {
        const char *seq = "\x1b[?1049h\x1b[?1l\x1b[?25l\x1b[2J\x1b[H";
        write_all(STDOUT_FILENO, seq, strlen(seq));
    }

    int cols, rows;
    tty_size(&cols, &rows);
    uint32_t attach[5] = { (uint32_t)cols, (uint32_t)rows,
                           (uint32_t)terminal_truecolor(), prefix, (uint32_t)status };
    if (send_msg(fd, C_ATTACH, attach, sizeof attach) < 0) {
        /* errno is the only clue: the server accepted the connection but the
           attach write failed, usually because it closed the socket while
           shutting down. Save it before close()/tcsetattr() clobber it. */
        int err = errno;
        if (g_winch_pipe[0] >= 0) close(g_winch_pipe[0]);
        if (g_winch_pipe[1] >= 0) close(g_winch_pipe[1]);
        g_winch_pipe[0] = g_winch_pipe[1] = -1;
        tty_restore(&saved);
        if (err == EPIPE || err == ECONNRESET) {
            fprintf(stderr, "murx: cannot attach to session '%s': server closed "
                            "the connection (%s)\n", name, strerror(err));
        } else {
            fprintf(stderr, "murx: cannot attach to session '%s': %s\n",
                    name, strerror(err));
        }
        return 1;
    }

    NetBuf nb;
    memset(&nb, 0, sizeof nb);
    char exitmsg[256];
    exitmsg[0] = '\0';
    int done = 0;
    long long last_size_check = now_ms();

    while (!done) {
        /* Drain the self-pipe before blocking. A SIGWINCH that arrived while
           the previous iteration was working has left a byte in the pipe, so
           the resize is applied now instead of waiting for more activity. */
        if (g_winch_pipe[0] >= 0) {
            char junk[64];
            while (read(g_winch_pipe[0], junk, sizeof junk) > 0) g_winch = 1;
        }

        /* Do not rely on SIGWINCH alone: some terminals have been seen to
           update the pty size without delivering a signal, or to deliver it
           before the size is visible. Reconcile against TIOCGWINSZ
           periodically so a missed event cannot leave us at the old size. */
        long long t = now_ms();
        if (g_winch || t - last_size_check >= SIZE_CHECK_MS) {
            g_winch = 0;
            last_size_check = t;
            int ncols, nrows;
            tty_size(&ncols, &nrows);
            if (ncols != cols || nrows != rows) {
                cols = ncols;
                rows = nrows;
                uint32_t sz[2] = { (uint32_t)cols, (uint32_t)rows };
                if (send_msg(fd, C_RESIZE, sz, sizeof sz) < 0) break;
            }
        }

        struct pollfd pfds[3];
        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[1].fd = fd;
        pfds[1].events = POLLIN;
        int nfds = 2;
        if (g_winch_pipe[0] >= 0) {
            pfds[2].fd = g_winch_pipe[0];
            pfds[2].events = POLLIN;
            nfds = 3;
        }
        int pr = poll(pfds, (nfds_t)nfds, SIZE_CHECK_MS);
        if (pr < 0) {
            if (errno == EINTR) {
                if (g_client_quit) break;
                continue;
            } else {
                break;
            }
        }
        /* The resize itself is handled at the top of the loop. */
        if (nfds == 3 && (pfds[2].revents & POLLIN)) continue;

        if (pr > 0 && (pfds[0].revents & (POLLIN | POLLHUP))) {
            unsigned char buf[4096];
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n > 0) {
                if (send_msg(fd, C_INPUT, buf, (size_t)n) < 0) break;
            } else if (n == 0) {
                break;
            }
        }

        if (pr > 0 && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            unsigned char buf[16384];
            ssize_t n = read(fd, buf, sizeof buf);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                break;
            }
            net_append(&nb, buf, (size_t)n);
            size_t off = 0;
            while (nb.len - off >= sizeof(MsgHdr)) {
                MsgHdr h;
                memcpy(&h, nb.p + off, sizeof h);
                if (h.len > MAX_DRAW_BYTES) { done = 1; break; }
                if (h.len > nb.len - off - sizeof h) break;
                unsigned char *payload = nb.p + off + sizeof h;
                if (h.type == S_DRAW) {
                    write_all(STDOUT_FILENO, payload, h.len);
                } else if (h.type == S_DETACH) {
                    size_t m = h.len;
                    if (m >= sizeof exitmsg) m = sizeof exitmsg - 1;
                    if (m) memcpy(exitmsg, payload, m);
                    exitmsg[m] = '\0';
                    done = 1;
                }
                off += sizeof h + h.len;
            }
            if (off) net_consume(&nb, off);
        }
    }

    free(nb.p);
    if (g_winch_pipe[0] >= 0) close(g_winch_pipe[0]);
    if (g_winch_pipe[1] >= 0) close(g_winch_pipe[1]);
    g_winch_pipe[0] = g_winch_pipe[1] = -1;
    close(fd);
    tty_restore(&saved);
    if (exitmsg[0]) printf("%s\n", exitmsg);
    else printf("[detached (from session %s)]\n", name);
    return 0;
}

static void print_help(FILE *out) {
    fprintf(out,
        "usage: murx [-p letter] [-s top|bottom] [--no-cwd] [--no-clock] [--no-murx] [--no-hostname] [session]\n"
        "       murx -l | --list-sessions\n"
        "       murx -h | --help\n"
        "\n"
        "Attach to a running session, or start it if it is not running.\n"
        "The session name defaults to \"0\".\n"
        "\n"
        "Use -l or --list-sessions to list running sessions without attaching.\n"
        "\n"
        "Use -s top|bottom (or --status top|bottom) to position the status line.\n"
        "New sessions default to top; an explicit choice updates all attached clients.\n"
        "Without -s, keep the session position.\n"
        "Use --no-cwd to hide the current directory in the status bar.\n"
        "Use --no-clock to hide the clock in the status bar.\n"
        "Use --no-murx to hide [murx] in the status bar when using --no-cwd.\n"
        "Use --no-hostname to hide the hostname: prefix in window titles.\n"
        "\n"
        "Use -p x (or --prefix x) to choose Ctrl-X as this client's prefix.\n"
        "Letters a through z are supported (case-insensitive); the default is a (Ctrl-A).\n"
        "The shortcuts below use the default Ctrl-A prefix. Replace it with\n"
        "your chosen prefix, including both keys in the last-window shortcut.\n"
        "Prefix then a sends the chosen prefix literally to the pane.\n"
        "\n"
        "Windows\n"
        "  Ctrl-A c              new window\n"
        "  Ctrl-A Ctrl-C         new window\n"
        "  Ctrl-A n              next window\n"
        "  Ctrl-A Space          next window\n"
        "  Ctrl-A Ctrl-N         next window\n"
        "  Ctrl-A Ctrl-@         next window\n"
        "  Ctrl-A p              previous window\n"
        "  Ctrl-A Backspace      previous window\n"
        "  Ctrl-A Ctrl-P         previous window\n"
        "  Ctrl-A Ctrl-A         last window\n"
        "  Ctrl-A w              window menu\n"
        "  Ctrl-A Ctrl-W         window menu\n"
        "  Ctrl-A \"              window menu\n"
        "  Ctrl-A A              rename window\n"
        "  Ctrl-A 0 .. Ctrl-A 9  select window by number\n"
        "  Ctrl-A &              kill window (confirm)\n"
        "\n"
        "Panes\n"
        "  Ctrl-A |              split left/right\n"
        "  Ctrl-A %%              split left/right\n"
        "  Ctrl-A -              split top/bottom\n"
        "  Ctrl-A h              select pane left\n"
        "  Ctrl-A j              select pane down\n"
        "  Ctrl-A k              select pane up\n"
        "  Ctrl-A l              select pane right\n"
        "  Ctrl-A arrow keys     select pane\n"
        "  Ctrl-A Tab            next pane\n"
        "  Ctrl-A Shift-Tab      previous pane\n"
        "  Ctrl-A o              next pane\n"
        "  Ctrl-A Ctrl-H         resize pane left\n"
        "  Ctrl-A Ctrl-J         resize pane down\n"
        "  Ctrl-A Ctrl-K         resize pane up\n"
        "  Ctrl-A Ctrl-L         resize pane right\n"
        "  Ctrl-A K              kill pane (confirm)\n"
        "  Ctrl-A PageUp, [      scrollback (PgUp/PgDn; q/Escape exits)\n"
        "\n"
        "Client and server\n"
        "  Ctrl-A a              send a literal Ctrl-A to the pane\n"
        "  Ctrl-A x              lock the client\n"
        "  Ctrl-A Ctrl-X         lock the client\n"
        "  Ctrl-A *              list attached clients\n"
        "  Ctrl-A r              show \"Configuration Reloaded!\"\n"
        "  Ctrl-A R              resize session to this terminal (force)\n"
        "  Ctrl-A d              detach\n"
        "  Ctrl-A Ctrl-D         detach\n"
        "  Ctrl-A \\              kill server (confirm)\n"
        "\n"
        "Inside menus use Up/Down or j/k and Enter; q or Escape closes.\n"
        "Inside prompts use Enter to accept, Escape to cancel, Ctrl-U to clear.\n");
}

static int parse_prefix(const char *text, unsigned char *prefix) {
    if (!text || strlen(text) != 1) return 0;
    unsigned char letter = (unsigned char)text[0];
    if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
    if (letter < 'a' || letter > 'z') return 0;
    *prefix = letter - 'a' + 1;
    return 1;
}

int main(int argc, char **argv) {
    setlocale(LC_CTYPE, "");

    int argi = 1;
    unsigned char prefix = DEFAULT_PREFIX;
    int status = 0;
    DisplayOptions display = {1, 1, 1, 1};
    int list = 0;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1]) {
        const char *option = argv[argi++];
        if (strcmp(option, "--") == 0) break;
        if (strcmp(option, "-h") == 0 || strcmp(option, "--help") == 0) {
            print_help(stdout);
            return 0;
        }
        if (strcmp(option, "-l") == 0 || strcmp(option, "--list-sessions") == 0) {
            list = 1;
        } else if (strcmp(option, "--no-cwd") == 0) {
            display.show_cwd = 0;
        } else if (strcmp(option, "--no-clock") == 0) {
            display.show_clock = 0;
        } else if (strcmp(option, "--no-murx") == 0) {
            display.show_murx = 0;
        } else if (strcmp(option, "--no-hostname") == 0) {
            display.show_hostname = 0;
        } else if (strcmp(option, "-p") == 0 || strcmp(option, "--prefix") == 0) {
            if (argi >= argc || !parse_prefix(argv[argi], &prefix)) {
                fprintf(stderr, "murx: %s requires a letter from a through z\n", option);
                return 2;
            }
            argi++;
        } else if (strcmp(option, "-s") == 0 || strcmp(option, "--status") == 0) {
            if (argi >= argc || (strcmp(argv[argi], "top") != 0 &&
                                 strcmp(argv[argi], "bottom") != 0)) {
                fprintf(stderr, "murx: %s requires top or bottom\n", option);
                return 2;
            }
            status = strcmp(argv[argi++], "bottom") == 0 ? 2 : 1;
        } else {
            fprintf(stderr, "murx: unknown option '%s'\n", option);
            fprintf(stderr, "Try 'murx --help' for more information.\n");
            return 2;
        }
    }

    if (list) {
        if (argi != argc) {
            fprintf(stderr, "murx: --list-sessions does not take a session name\n");
            return 2;
        }
        return list_sessions();
    }
    if (argc - argi > 1) {
        fprintf(stderr, "murx: too many arguments\n");
        fprintf(stderr, "Try 'murx --help' for more information.\n");
        return 2;
    }
    const char *name = argc - argi > 0 ? argv[argi] : "0";
    if (!valid_session_name(name)) {
        fprintf(stderr, "murx: bad session name '%s'\n", name);
        fprintf(stderr, "Try 'murx --help' for more information.\n");
        return 2;
    }

    char path[PATH_MAX];
    make_socket_path(name, path, sizeof path);

    int fd = connect_session(path);
    if (fd < 0) fd = spawn_server(path, name, &display);
    if (fd < 0) {
        fprintf(stderr, "murx: cannot start or attach session '%s': %s\n",
                name, strerror(errno));
        return 1;
    }
    return run_client(fd, name, prefix, status);
}
