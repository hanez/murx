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
 * One binary, no config file. Shortcuts and the interface palette have built-in defaults.
 *
 *   murx [session]     attach to session (default "0"), or start it
 *   murx -h|--help     show version, usage and all Ctrl-A shortcut chains
 *   murx -v|--version  show version
 *
 * Pure C99 + POSIX (macOS / Linux / FreeBSD / OpenBSD).
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
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
/* openpty(3) lives in <pty.h> on Linux and glibc, in <libutil.h> on FreeBSD
   and DragonFly, and in <util.h> on the other BSDs and macOS. */
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <libutil.h>
#elif defined(__NetBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
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

#define MURX_VERSION "1.0.5"
#define DEFAULT_PREFIX 0x01

/* client -> server */
enum { C_ATTACH = 1, C_INPUT = 2, C_RESIZE = 3, C_ADOPT = 6 };
/* server -> client */
enum { S_DRAW = 4, S_DETACH = 5, S_HANDOFF = 7 };

/* C_ATTACH session-size mode: 0 preserves the running session's setting. */
enum { SIZE_MODE_KEEP = 0, SIZE_MODE_ON = 1 };

typedef struct {
    uint32_t type;
    uint32_t len;
} MsgHdr;

static int send_msg(int fd, uint32_t type, const void *data, size_t len);

/* xterm-256 palette indices. The terminal defines the ANSI colours. */
#define C_BLACK                    0
#define C_MAROON                   1
#define C_DGREEN                   2
#define C_OLIVE                    3
#define C_NAVY                     4
#define C_PURPLE                   5
#define C_CYAN                     6
#define C_WHITE                    7
#define C_GREY                     8
#define C_RED                      9
#define C_GREEN                   10
#define C_YELLOW                  11
#define C_BLUE                    12
#define C_MAGENTA                 13
#define C_CYAN2                   14
#define C_BWHITE                  15
#define C_GREY_0                  16
#define C_NAVY_BLUE               17
#define C_DARK_BLUE               18
#define C_BLUE_3_19               19
#define C_BLUE_3_20               20
#define C_BLUE_1                  21
#define C_DARK_GREEN              22
#define C_DEEP_SKY_BLUE_4_23      23
#define C_DEEP_SKY_BLUE_4_24      24
#define C_DEEP_SKY_BLUE_4_25      25
#define C_DODGER_BLUE_3           26
#define C_DODGER_BLUE_2           27
#define C_GREEN_4                 28
#define C_SPRING_GREEN_4          29
#define C_TURQUOISE_4             30
#define C_DEEP_SKY_BLUE_3_31      31
#define C_DEEP_SKY_BLUE_3_32      32
#define C_DODGER_BLUE_1           33
#define C_GREEN_3_34              34
#define C_SPRING_GREEN_3_35       35
#define C_DARK_CYAN               36
#define C_LIGHT_SEA_GREEN         37
#define C_DEEP_SKY_BLUE_2         38
#define C_DEEP_SKY_BLUE_1         39
#define C_GREEN_3_40              40
#define C_SPRING_GREEN_3_41       41
#define C_SPRING_GREEN_2_42       42
#define C_CYAN_3                  43
#define C_DARK_TURQUOISE          44
#define C_TURQUOISE_2             45
#define C_GREEN_1                 46
#define C_SPRING_GREEN_2_47       47
#define C_SPRING_GREEN_1          48
#define C_MEDIUM_SPRING_GREEN     49
#define C_CYAN_2                  50
#define C_CYAN_1                  51
#define C_DARK_RED_52             52
#define C_DEEP_PINK_4_53          53
#define C_PURPLE_4_54             54
#define C_PURPLE_4_55             55
#define C_PURPLE_3                56
#define C_BLUE_VIOLET             57
#define C_ORANGE_4_58             58
#define C_GREY_37                 59
#define C_MEDIUM_PURPLE_4         60
#define C_SLATE_BLUE_3_61         61
#define C_SLATE_BLUE_3_62         62
#define C_ROYAL_BLUE_1            63
#define C_CHARTREUSE_4            64
#define C_DARK_SEA_GREEN_4_65     65
#define C_PALE_TURQUOISE_4        66
#define C_STEEL_BLUE              67
#define C_STEEL_BLUE_3            68
#define C_CORNFLOWER_BLUE         69
#define C_CHARTREUSE_3_70         70
#define C_DARK_SEA_GREEN_4_71     71
#define C_CADET_BLUE_72           72
#define C_CADET_BLUE_73           73
#define C_SKY_BLUE_3              74
#define C_STEEL_BLUE_1_75         75
#define C_CHARTREUSE_3_76         76
#define C_PALE_GREEN_3_77         77
#define C_SEA_GREEN_3             78
#define C_AQUAMARINE_3            79
#define C_MEDIUM_TURQUOISE        80
#define C_STEEL_BLUE_1_81         81
#define C_CHARTREUSE_2_82         82
#define C_SEA_GREEN_2             83
#define C_SEA_GREEN_1_84          84
#define C_SEA_GREEN_1_85          85
#define C_AQUAMARINE_1_86         86
#define C_DARK_SLATE_GRAY_2       87
#define C_DARK_RED_88             88
#define C_DEEP_PINK_4_89          89
#define C_DARK_MAGENTA_90         90
#define C_DARK_MAGENTA_91         91
#define C_DARK_VIOLET_92          92
#define C_PURPLE_93               93
#define C_ORANGE_4_94             94
#define C_LIGHT_PINK_4            95
#define C_PLUM_4                  96
#define C_MEDIUM_PURPLE_3_97      97
#define C_MEDIUM_PURPLE_3_98      98
#define C_SLATE_BLUE_1            99
#define C_YELLOW_4_100           100
#define C_WHEAT_4                101
#define C_GREY_53                102
#define C_LIGHT_SLATE_GREY       103
#define C_MEDIUM_PURPLE          104
#define C_LIGHT_SLATE_BLUE       105
#define C_YELLOW_4_106           106
#define C_DARK_OLIVE_GREEN_3_107 107
#define C_DARK_SEA_GREEN         108
#define C_LIGHT_SKY_BLUE_3_109   109
#define C_LIGHT_SKY_BLUE_3_110   110
#define C_SKY_BLUE_2             111
#define C_CHARTREUSE_2_112       112
#define C_DARK_OLIVE_GREEN_3_113 113
#define C_PALE_GREEN_3_114       114
#define C_DARK_SEA_GREEN_3_115   115
#define C_DARK_SLATE_GRAY_3      116
#define C_SKY_BLUE_1             117
#define C_CHARTREUSE_1           118
#define C_LIGHT_GREEN_119        119
#define C_LIGHT_GREEN_120        120
#define C_PALE_GREEN_1_121       121
#define C_AQUAMARINE_1_122       122
#define C_DARK_SLATE_GRAY_1      123
#define C_RED_3_124              124
#define C_DEEP_PINK_4_125        125
#define C_MEDIUM_VIOLET_RED      126
#define C_MAGENTA_3_127          127
#define C_DARK_VIOLET_128        128
#define C_PURPLE_129             129
#define C_DARK_ORANGE_3_130      130
#define C_INDIAN_RED_131         131
#define C_HOT_PINK_3_132         132
#define C_MEDIUM_ORCHID_3        133
#define C_MEDIUM_ORCHID          134
#define C_MEDIUM_PURPLE_2_135    135
#define C_DARK_GOLDENROD         136
#define C_LIGHT_SALMON_3_137     137
#define C_ROSY_BROWN             138
#define C_GREY_63                139
#define C_MEDIUM_PURPLE_2_140    140
#define C_MEDIUM_PURPLE_1        141
#define C_GOLD_3_142             142
#define C_DARK_KHAKI             143
#define C_NAVAJO_WHITE_3         144
#define C_GREY_69                145
#define C_LIGHT_STEEL_BLUE_3     146
#define C_LIGHT_STEEL_BLUE       147
#define C_YELLOW_3_148           148
#define C_DARK_OLIVE_GREEN_3_149 149
#define C_DARK_SEA_GREEN_3_150   150
#define C_DARK_SEA_GREEN_2_151   151
#define C_LIGHT_CYAN_3           152
#define C_LIGHT_SKY_BLUE_1       153
#define C_GREEN_YELLOW           154
#define C_DARK_OLIVE_GREEN_2     155
#define C_PALE_GREEN_1_156       156
#define C_DARK_SEA_GREEN_2_157   157
#define C_DARK_SEA_GREEN_1_158   158
#define C_PALE_TURQUOISE_1       159
#define C_RED_3_160              160
#define C_DEEP_PINK_3_161        161
#define C_DEEP_PINK_3_162        162
#define C_MAGENTA_3_163          163
#define C_MAGENTA_3_164          164
#define C_MAGENTA_2_165          165
#define C_DARK_ORANGE_3_166      166
#define C_INDIAN_RED_167         167
#define C_HOT_PINK_3_168         168
#define C_HOT_PINK_2             169
#define C_ORCHID                 170
#define C_MEDIUM_ORCHID_1_171    171
#define C_ORANGE_3               172
#define C_LIGHT_SALMON_3_173     173
#define C_LIGHT_PINK_3           174
#define C_PINK_3                 175
#define C_PLUM_3                 176
#define C_VIOLET                 177
#define C_GOLD_3_178             178
#define C_LIGHT_GOLDENROD_3      179
#define C_TAN                    180
#define C_MISTY_ROSE_3           181
#define C_THISTLE_3              182
#define C_PLUM_2                 183
#define C_YELLOW_3_184           184
#define C_KHAKI_3                185
#define C_LIGHT_GOLDENROD_2_186  186
#define C_LIGHT_YELLOW_3         187
#define C_GREY_84                188
#define C_LIGHT_STEEL_BLUE_1     189
#define C_YELLOW_2               190
#define C_DARK_OLIVE_GREEN_1_191 191
#define C_DARK_OLIVE_GREEN_1_192 192
#define C_DARK_SEA_GREEN_1_193   193
#define C_HONEYDEW_2             194
#define C_LIGHT_CYAN_1           195
#define C_RED_1                  196
#define C_DEEP_PINK_2            197
#define C_DEEP_PINK_1_198        198
#define C_DEEP_PINK_1_199        199
#define C_MAGENTA_2_200          200
#define C_MAGENTA_1              201
#define C_ORANGE_RED_1           202
#define C_INDIAN_RED_1_203       203
#define C_INDIAN_RED_1_204       204
#define C_HOT_PINK_205           205
#define C_HOT_PINK_206           206
#define C_MEDIUM_ORCHID_1_207    207
#define C_DARK_ORANGE            208
#define C_SALMON_1               209
#define C_LIGHT_CORAL            210
#define C_PALE_VIOLET_RED_1      211
#define C_ORCHID_2               212
#define C_ORCHID_1               213
#define C_ORANGE_1               214
#define C_SANDY_BROWN            215
#define C_LIGHT_SALMON_1         216
#define C_LIGHT_PINK_1           217
#define C_PINK_1                 218
#define C_PLUM_1                 219
#define C_GOLD_1                 220
#define C_LIGHT_GOLDENROD_2_221  221
#define C_LIGHT_GOLDENROD_2_222  222
#define C_NAVAJO_WHITE_1         223
#define C_MISTY_ROSE_1           224
#define C_THISTLE_1              225
#define C_YELLOW_1               226
#define C_LIGHT_GOLDENROD_1      227
#define C_KHAKI_1                228
#define C_WHEAT_1                229
#define C_CORNSILK_1             230
#define C_GREY_100               231
#define C_GREY_3                 232
#define C_GREY_7                 233
#define C_GREY_11                234
#define C_GREY_15                235
#define C_GREY_19                236
#define C_GREY_23                237
#define C_GREY_27                238
#define C_GREY_30                239
#define C_GREY_35                240
#define C_GREY_39                241
#define C_GREY_42                242
#define C_GREY_46                243
#define C_GREY_50                244
#define C_GREY_54                245
#define C_GREY_58                246
#define C_GREY_62                247
#define C_GREY_66                248
#define C_GREY_70                249
#define C_GREY_74                250
#define C_GREY_78                251
#define C_GREY_82                252
#define C_GREY_85                253
#define C_GREY_89                254
#define C_GREY_93                255

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

/* Menu palette: murx blues/cyan/yellow on a hard black background. */
#define MENU_BG     C_BLACK
#define MENU_FRAME  C_BLUE
#define MENU_TITLE  P_CURRENT
#define MENU_ITEM   P_STATUS
#define MENU_SELECT P_CURRENT
#define MENU_ACCENT P_ACCENT
#define MENU_DIM    C_GREY

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
#define MENU_MAX_DEPTH 8
#define DEFAULT_HISTORY_LINES 2000
#define MAX_HISTORY_LINES 100000
#define MAX_HANDOFF_FDS 256   /* SCM_RIGHTS cap for a session handoff */
#define MAX_HANDOFF_BYTES (256u * 1024u * 1024u)
#define ADOPT_TIMEOUT_MS 2000

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
    if (!unicode_scalar(cp)) return 1;
#if WCHAR_MAX < 0x10FFFF
    if (cp > WCHAR_MAX) return 1;
#endif
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
    PS_DCS_ESC,
    PS_STRING,
    PS_STRING_ESC
} ParseState;

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
    int history_limit, history_start, history_count;

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
    int adopted;

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
    if (p->history_limit > 0 && !p->on_alt &&
        p->scroll_top == 0 && p->scroll_bot == g->h - 1) {
        if (!p->history) p->history = xcalloc(p->history_limit, sizeof(Grid));
        for (int y = 0; y < n; y++) {
            int slot = (p->history_start + p->history_count) % p->history_limit;
            if (p->history_count == p->history_limit) {
                grid_free(&p->history[slot]);
                p->history_start = (p->history_start + 1) % p->history_limit;
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
        if (n > g->h + p->history_limit) n = g->h + p->history_limit;
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
        case 'k': case 'X': case '^': case '_':
            /* Screen/tmux set the window title with ESC k <text> ESC \.
               SOS/PM/APC use ESC X/^/_ with the same ST terminator. Consume
               these strings so their payload is never painted into the grid. */
            p->pstate = PS_STRING;
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

    case PS_STRING:
        /* Terminated by ST (ESC \) or, for compatibility, BEL. */
        if (b == 0x1b) {
            p->pstate = PS_STRING_ESC;
        } else if (b == 0x07) {
            p->pstate = PS_GROUND;
            p->seqlen = 0;
        } else if (p->seqlen < (int)sizeof p->seq - 1) {
            p->seq[p->seqlen++] = (char)b;
        }
        break;

    case PS_STRING_ESC:
        if (b == '\\') {
            p->pstate = PS_GROUND;
        } else {
            /* Not ST: the ESC belonged to the string payload. */
            p->pstate = PS_STRING;
        }
        p->seqlen = 0;
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

typedef struct Scrollback {
    Pane *pane;
    Grid grid;
    int offset;
    struct Scrollback *next;
} Scrollback;

struct Client {
    int fd;
    int cols, rows;
    int attached;
    uint64_t order;        /* attach/resize sequence, newest client wins */
    uint64_t input_order;  /* last input sequence, used by resize-on-input */
    int truecolor;
    unsigned char in[16384];
    size_t inlen;
    int64_t escape_since;
    unsigned char net[65536];
    size_t netlen;

    Grid prev;
    int prev_valid;

    int locked;
    int prefix_pending;
    unsigned char prefix;

    int mode;              /* 0 none, 1 prompt, 2 menu, 3 confirm */
    int prompt_kind;       /* 1 rename window, 2 choose window */
    char prompt[160];
    int prompt_len;
    int prompt_label_len;

    Scrollback *scrollbacks;

    /* Menu navigation. Pages and items live in a static table; only the
       current page, selection and parent stack are per-client state. */
    int menu_page;
    int menu_top;
    int menu_sel;
    int menu_depth;
    int menu_parent[MENU_MAX_DEPTH];
    int menu_parent_sel[MENU_MAX_DEPTH];

    int confirm_action;    /* 1 kill pane, 2 kill server, 3 kill window */

    char msg[256];
    int64_t msg_until;

    char title[160];

    int dead;
    Client *next;
};

/* Display choices start from the session's command-line options and can be
 * changed live from the Configuration menu. */
typedef struct {
    int show_cwd;
    int show_clock;
    int show_murx;
    int show_hostname;
    int show_pane_titles;
} DisplayOptions;

struct Session {
    char name[64];
    char sock_path[PATH_MAX];
    char cwd[PATH_MAX];
    int listen_fd;

    int w, h;
    int status_bottom;
    int resize_on_input;    /* size follows the client that last sent input */
    int32_t status_bg;      /* 0 uses the terminal default */
    DisplayOptions display;
    int history_limit;
    Window *windows;
    Window *cur, *last;
    int nwin;

    Client *clients;
    uint64_t client_order;
    int running;
    int adopted;           /* handed off to a new server; do not clean up fds */
    int redraw;            /* a removed client changed the session size */

    long long last_status;
    long long last_name_check;
};

/* Compact sequence numbers on wrap, preserving both attach/resize and
   input ordering across all clients. */
static uint64_t client_next_order(Session *s) {
    if (s->client_order == UINT64_MAX) {
        uint64_t previous = 0, rank = 0;
        for (;;) {
            Client *next = NULL;
            uint64_t next_value = 0;
            for (Client *c = s->clients; c; c = c->next) {
                uint64_t values[2] = { c->order, c->input_order };
                for (int i = 0; i < 2; i++) {
                    uint64_t value = values[i];
                    if (value > previous && (!next || value < next_value)) {
                        next = c;
                        next_value = value;
                    }
                }
            }
            if (!next) break;
            if (rank == UINT64_MAX - 1) range_error();
            if (next->order == next_value) next->order = rank + 1;
            if (next->input_order == next_value) next->input_order = rank + 1;
            previous = next_value;
            rank++;
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
        for (int i = 0; i < p->history_count; i++) grid_free(&p->history[i]);
        free(p->history);
    }
    free(p);
}

static Pane *pane_new(const char *cwd, int w, int h, const char *title,
                      const char *session_name, int history_limit) {
    struct winsize iws;
    memset(&iws, 0, sizeof iws);
    iws.ws_col = tty_dimension(w > 0 ? w : 80);
    iws.ws_row = tty_dimension(h > 0 ? h : 24);

    /* openpty() works on Linux, macOS and all BSDs. Linux exposes only
       /dev/ptmx, while OpenBSD and friends use /dev/pty*; letting libc pick
       the pty avoids hardcoding either scheme. */
    int master = -1, slave_fd = -1;
    char slave_path[PATH_MAX];
    if (openpty(&master, &slave_fd, slave_path, NULL, &iws) < 0) return NULL;

    const char *shell = getenv("SHELL");
    if (!shell || !*shell) {
        if (access("/usr/bin/zsh", X_OK) == 0) shell = "/usr/bin/zsh";
        else shell = "/bin/sh";
    }

    /* openpty() gave the slave its size. The server keeps this fd for the
       pane's lifetime: a pty with no slave open would report EOF on the
       master. Make it non-blocking so the server can drain it with poll(). */
    int flags = fcntl(slave_fd, F_GETFL, 0);
    if (flags >= 0) fcntl(slave_fd, F_SETFL, flags | O_NONBLOCK);

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
        if (session_name && *session_name) setenv("MURX", session_name, 1);
        if (cwd && *cwd && chdir(cwd) < 0) perror("murx: chdir");
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
    p->history_limit = history_limit;
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

static void layout_apply(Layout *l, int x, int y, int w, int h, int show_titles) {
    if (!l) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    l->x = x;
    l->y = y;
    l->w = w;
    l->h = h;

    if (l->type == L_LEAF) {
        Pane *p = l->pane;
        if (show_titles) {
            int ph = h - 1;
            if (ph < 1) ph = 1;
            p->x = x;
            p->y = y + 1;
            pane_resize(p, w, ph);
        } else {
            p->x = x;
            p->y = y;
            pane_resize(p, w, h);
        }
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
        layout_apply(l->a, x, y, aw, h, show_titles);
        layout_apply(l->b, x + aw + 1, y, avail - aw, h, show_titles);
    } else {
        if (show_titles) {
            int ah = (int)(h * l->ratio + 0.5);
            if (ah < 2) ah = 2;
            if (ah > h - 2) ah = h - 2;
            if (ah < 1) ah = 1;
            l->ratio = (double)ah / (double)h;
            layout_apply(l->a, x, y, w, ah, show_titles);
            layout_apply(l->b, x, y + ah, w, h - ah, show_titles);
        } else {
            int avail = h - 1;
            if (avail < 1) avail = 1;
            int ah = (int)(avail * l->ratio + 0.5);
            if (ah < 1) ah = 1;
            if (ah > avail - 1) ah = avail - 1;
            if (ah < 1) ah = 1;
            l->ratio = (double)ah / (double)avail;
            layout_apply(l->a, x, y, w, ah, show_titles);
            layout_apply(l->b, x, y + ah + 1, w, avail - ah, show_titles);
        }
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
    Pane *p = pane_new(s->cwd, pw, ph, host, s->name, s->history_limit);
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
    int top = s->status_bottom ? 0 : 1;
    int h = s->h - 1;
    if (h < 1) h = 1;
    for (Window *w = s->windows; w; w = w->next) {
        if (w->root) layout_apply(w->root, 0, top, s->w, h, s->display.show_pane_titles);
    }
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

/* limit is the first column that must stay untouched (for clipping labels
 * inside menu boxes). Passing INT_MAX gives the ordinary unbounded behaviour. */
static int draw_text_limited(Grid *g, int x, int y, const char *s, int fg, int bg,
                             int attr, int limit) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp;
        int used = utf8_decode(p, strlen((const char *)p), &cp);
        int w = cp_width(cp);
        if (w < 0) w = 1;
        if (x > INT_MAX - w || x + w > limit) return x;
        if (cp >= 32 && cp != 0x7f) scr_put_wide(g, x, y, cp, fg, bg, attr);
        x += w;
        p += used;
    }
    return x;
}

static int draw_text(Grid *g, int x, int y, const char *s, int fg, int bg, int attr) {
    return draw_text_limited(g, x, y, s, fg, bg, attr, INT_MAX);
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

static void border_collect(uint8_t *m, int w, int h, Layout *l, int show_titles) {
    if (!l || l->w < 1 || l->h < 1) return;
    if (l->type == L_LEAF) {
        if (show_titles) {
            int y = l->y;
            int end = l->x + l->w;
            if (y < 0 || y >= h) return;
            for (int x = l->x; x < end; x++) {
                uint8_t bits = 0;
                if (x > l->x || l->w == 1) bits |= B_LEFT;
                if (x < end - 1 || l->w == 1) bits |= B_RIGHT;
                border_mark(m, w, h, x, y, bits);
            }
        }
        return;
    }
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
    else if (!show_titles) {
        int ay = l->y + l->a->h;
        int end = l->x + l->w;
        for (int x = l->x; x < end; x++) {
            uint8_t bits = 0;
            if (x > l->x || l->w == 1) bits |= B_LEFT;
            if (x < end - 1 || l->w == 1) bits |= B_RIGHT;
            border_mark(m, w, h, x, ay, bits);
        }
    }
    border_collect(m, w, h, l->a, show_titles);
    border_collect(m, w, h, l->b, show_titles);
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

/* A title row belongs to the layout leaf, not to the pane's content grid.
 * Use the leaf geometry so the label stays on the row reserved by
 * layout_apply(), even if a pane's content coordinates are stale. */
static void draw_window_label(Grid *g, Window *win, Layout *l) {
    Pane *p = l->pane;
    int active = (p == win->active);
    int fg1 = active ? C_WHITE : C_GREY;
    int fg2 = active ? C_YELLOW : 3;
    int fg3 = active ? C_BWHITE : C_BLUE;
    int x = l->x;
    int y = l->y;
    int end = l->x + l->w;
    if (y < 0 || y >= g->h) return;
    if (x < end) scr_put(g, x++, y, '[', fg1, DEFAULT_COLOR, 0);
    char num[32];
    snprintf(num, sizeof num, "%d", p->id);
    x = draw_text(g, x, y, num, fg1, DEFAULT_COLOR, 0);
    if (x < end) scr_put(g, x++, y, ':', fg2, DEFAULT_COLOR, 0);
    if (x < end) scr_put(g, x++, y, ' ', fg2, DEFAULT_COLOR, 0);
    const char *title = p->title[0] ? p->title : win->name;
    const unsigned char *s = (const unsigned char *)title;
    while (*s && x < end - 1) {
        uint32_t cp;
        int used = utf8_decode(s, (int)strlen((const char *)s), &cp);
        int w = cp_width(cp);
        if (w < 1) w = 1;
        if (x + w > end - 1) break;
        scr_put_wide(g, x, y, cp, fg3, DEFAULT_COLOR, 0);
        x += w;
        s += used;
    }
    if (x < end) scr_put(g, x, y, ']', fg1, DEFAULT_COLOR, 0);
}

static void draw_labels(Grid *g, Layout *l, Window *win) {
    if (!l || l->w < 1 || l->h < 1) return;
    if (l->type == L_LEAF) {
        draw_window_label(g, win, l);
        return;
    }
    draw_labels(g, l->a, win);
    draw_labels(g, l->b, win);
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

static Scrollback **scrollback_slot(Client *c, Pane *pane) {
    Scrollback **slot = &c->scrollbacks;
    while (*slot && (*slot)->pane != pane) slot = &(*slot)->next;
    return slot;
}

static Scrollback *scrollback_current(Client *c, Session *s) {
    if (c->mode || !s->cur || !s->cur->active) return NULL;
    return *scrollback_slot(c, s->cur->active);
}

static void scrollback_remove(Scrollback **slot) {
    Scrollback *view = *slot;
    if (!view) return;
    *slot = view->next;
    grid_free(&view->grid);
    free(view);
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
    Scrollback *view = scrollback_current(c, s);
    if (view) {
        snprintf(scroll_msg, sizeof scroll_msg, "Scrollback [%d] PgUp/PgDn, q/Esc exits",
                 view->offset);
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

    if (s->status_bg) {
        for (int x = 0; x < s->w; x++) {
            Cell *cell = grid_at(g, x, y);
            if (cell->bg == DEFAULT_COLOR) cell->bg = s->status_bg;
        }
    }
}

/* ------------------------------------------------------------------ */
/* extensible menu                                                    */
/* ------------------------------------------------------------------ */

/* To add a menu entry, append an item to one of the static tables below.
 * New submenus need a page in menu_pages; new commands need a case in
 * menu_action(). Drawing, navigation and key handling are table-driven. */
enum {
    MENU_MAIN = 0,
    MENU_WINDOWS,          /* dynamic: one item per window */
    MENU_SELECT_PANE,
    MENU_RESIZE_PANE,
    MENU_SESSION,
    MENU_CONFIG,
    MENU_CONFIG_STATUS,
    MENU_CONFIG_PREFIX,
    MENU_COUNT
};

enum { MI_ACTION, MI_SUBMENU, MI_TOGGLE, MI_CHOICE, MI_SEPARATOR };

#define MIF_NONE  0
#define MIF_PANES 0x01     /* only meaningful with more than one pane */

enum {
    ACT_NONE = 0,
    ACT_NEW_WINDOW,
    ACT_SPLIT_ROWS,
    ACT_SPLIT_COLS,
    ACT_CLOSE_PANE,
    ACT_CLOSE_WINDOW,
    ACT_RENAME_WINDOW,
    ACT_PANE_LEFT,
    ACT_PANE_DOWN,
    ACT_PANE_UP,
    ACT_PANE_RIGHT,
    ACT_PANE_NEXT,
    ACT_PANE_PREV,
    ACT_RESIZE_LEFT,
    ACT_RESIZE_DOWN,
    ACT_RESIZE_UP,
    ACT_RESIZE_RIGHT,
    ACT_SCROLLBACK,
    ACT_DETACH,
    ACT_LOCK,
    ACT_CLIENTS,
    ACT_FIT_SIZE,
    ACT_KILL_SERVER,
    ACT_RESET_DISPLAY
};

enum {
    SET_NONE = 0,
    SET_STATUS_BOTTOM,
    SET_STATUS_BG,
    SET_SHOW_CWD,
    SET_SHOW_CLOCK,
    SET_SHOW_MURX,
    SET_SHOW_HOSTNAME,
    SET_SHOW_PANE_TITLES,
    SET_RESIZE_ON_INPUT,
    SET_PREFIX
};

typedef struct {
    const char *label;
    int type;              /* MI_* */
    int id;                /* ACT_*, MENU_* or SET_* */
    int arg;               /* setting value */
    int flags;             /* MIF_* */
} MenuItem;

typedef struct {
    const char *title;
    const MenuItem *items;
    int count;
} MenuPage;

#define MENU_ITEMS(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MENU_SEP { "", MI_SEPARATOR, 0, 0, 0 }

static const MenuItem menu_main_items[] = {
    { "New window (tab)",      MI_ACTION, ACT_NEW_WINDOW, 0, 0 },
    { "New pane (h. split)",   MI_ACTION, ACT_SPLIT_ROWS, 0, 0 },
    { "New pane (v. split)",   MI_ACTION, ACT_SPLIT_COLS, 0, 0 },
    MENU_SEP,
    { "Close pane",            MI_ACTION, ACT_CLOSE_PANE, 0, 0 },
    { "Close window (tab)",    MI_ACTION, ACT_CLOSE_WINDOW, 0, 0 },
    { "Rename window",         MI_ACTION, ACT_RENAME_WINDOW, 0, 0 },
    MENU_SEP,
    { "Windows...",            MI_SUBMENU, MENU_WINDOWS, 0, 0 },
    { "Select pane...",        MI_SUBMENU, MENU_SELECT_PANE, 0, MIF_PANES },
    { "Resize pane...",        MI_SUBMENU, MENU_RESIZE_PANE, 0, MIF_PANES },
    MENU_SEP,
    { "Session...",            MI_SUBMENU, MENU_SESSION, 0, 0 },
    { "Configuration...",      MI_SUBMENU, MENU_CONFIG, 0, 0 }
};

static const MenuItem menu_select_items[] = {
    { "Pane left",             MI_ACTION, ACT_PANE_LEFT, 0, MIF_PANES },
    { "Pane down",             MI_ACTION, ACT_PANE_DOWN, 0, MIF_PANES },
    { "Pane up",               MI_ACTION, ACT_PANE_UP, 0, MIF_PANES },
    { "Pane right",            MI_ACTION, ACT_PANE_RIGHT, 0, MIF_PANES },
    MENU_SEP,
    { "Next pane",             MI_ACTION, ACT_PANE_NEXT, 0, MIF_PANES },
    { "Previous pane",         MI_ACTION, ACT_PANE_PREV, 0, MIF_PANES }
};

static const MenuItem menu_resize_items[] = {
    { "Resize left",           MI_ACTION, ACT_RESIZE_LEFT, 0, MIF_PANES },
    { "Resize right",          MI_ACTION, ACT_RESIZE_RIGHT, 0, MIF_PANES },
    MENU_SEP,
    { "Resize up",             MI_ACTION, ACT_RESIZE_UP, 0, MIF_PANES },
    { "Resize down",           MI_ACTION, ACT_RESIZE_DOWN, 0, MIF_PANES }
};

static const MenuItem menu_session_items[] = {
    { "Detach",                MI_ACTION, ACT_DETACH, 0, 0 },
    { "Lock client",           MI_ACTION, ACT_LOCK, 0, 0 },
    { "List attached clients", MI_ACTION, ACT_CLIENTS, 0, 0 },
    { "Resize session to terminal", MI_ACTION, ACT_FIT_SIZE, 0, 0 },
    MENU_SEP,
    { "Enter scrollback",      MI_ACTION, ACT_SCROLLBACK, 0, 0 },
    MENU_SEP,
    { "Kill server",           MI_ACTION, ACT_KILL_SERVER, 0, 0 }
};

static const MenuItem menu_config_items[] = {
    { "Status line...",        MI_SUBMENU, MENU_CONFIG_STATUS, 0, 0 },
    { "Prefix...",             MI_SUBMENU, MENU_CONFIG_PREFIX, 0, 0 },
    MENU_SEP,
    { "Show current directory", MI_TOGGLE, SET_SHOW_CWD, 1, 0 },
    { "Show clock",            MI_TOGGLE, SET_SHOW_CLOCK, 1, 0 },
    { "Show [murx] label",     MI_TOGGLE, SET_SHOW_MURX, 1, 0 },
    { "Show hostname",         MI_TOGGLE, SET_SHOW_HOSTNAME, 1, 0 },
    { "Show pane titles",      MI_TOGGLE, SET_SHOW_PANE_TITLES, 1, 0 },
    { "Resize to last input",  MI_TOGGLE, SET_RESIZE_ON_INPUT, 1, 0 },
    MENU_SEP,
    { "Reset display/status defaults", MI_ACTION, ACT_RESET_DISPLAY, 0, 0 }
};

static const MenuItem menu_config_status_items[] = {
    { "Position: top",         MI_CHOICE, SET_STATUS_BOTTOM, 0, 0 },
    { "Position: bottom",      MI_CHOICE, SET_STATUS_BOTTOM, 1, 0 },
    MENU_SEP,
    { "Background: default",   MI_CHOICE, SET_STATUS_BG, 0, 0 },
    { "Background: black",     MI_CHOICE, SET_STATUS_BG, 1, 0 },
    { "Background: grey",      MI_CHOICE, SET_STATUS_BG, 2, 0 }
};

#define PREFIX_ITEM(ch) { "Ctrl-" #ch, MI_CHOICE, SET_PREFIX, (#ch)[0] - 'a' + 1, 0 }
static const MenuItem menu_config_prefix_items[] = {
    PREFIX_ITEM(a), PREFIX_ITEM(b), PREFIX_ITEM(c), PREFIX_ITEM(d),
    PREFIX_ITEM(e), PREFIX_ITEM(f), PREFIX_ITEM(g), PREFIX_ITEM(h),
    PREFIX_ITEM(i), PREFIX_ITEM(j), PREFIX_ITEM(k), PREFIX_ITEM(l),
    PREFIX_ITEM(m), PREFIX_ITEM(n), PREFIX_ITEM(o), PREFIX_ITEM(p),
    PREFIX_ITEM(q), PREFIX_ITEM(r), PREFIX_ITEM(s), PREFIX_ITEM(t),
    PREFIX_ITEM(u), PREFIX_ITEM(v), PREFIX_ITEM(w), PREFIX_ITEM(x),
    PREFIX_ITEM(y), PREFIX_ITEM(z)
};
#undef PREFIX_ITEM

static const MenuPage menu_pages[MENU_COUNT] = {
    [MENU_MAIN]           = { "murx",             menu_main_items,          MENU_ITEMS(menu_main_items) },
    [MENU_WINDOWS]        = { "Windows",          NULL,                     0 },
    [MENU_SELECT_PANE]    = { "Select pane",      menu_select_items,        MENU_ITEMS(menu_select_items) },
    [MENU_RESIZE_PANE]    = { "Resize pane",      menu_resize_items,        MENU_ITEMS(menu_resize_items) },
    [MENU_SESSION]        = { "Session",          menu_session_items,       MENU_ITEMS(menu_session_items) },
    [MENU_CONFIG]         = { "Configuration",    menu_config_items,        MENU_ITEMS(menu_config_items) },
    [MENU_CONFIG_STATUS]  = { "Status line",      menu_config_status_items, MENU_ITEMS(menu_config_status_items) },
    [MENU_CONFIG_PREFIX]  = { "Client prefix",    menu_config_prefix_items, MENU_ITEMS(menu_config_prefix_items) }
};

static const MenuPage *menu_page_of(Client *c) {
    if (c->menu_page < 0 || c->menu_page >= MENU_COUNT) return NULL;
    return &menu_pages[c->menu_page];
}

static int menu_window_count(Session *s) {
    int n = 0;
    for (Window *w = s->windows; w; w = w->next) n++;
    return n;
}

static int menu_count(Client *c, Session *s) {
    if (c->menu_page == MENU_WINDOWS) return menu_window_count(s);
    const MenuPage *page = menu_page_of(c);
    return page ? page->count : 0;
}

static const MenuItem *menu_item_at(Client *c, int i) {
    const MenuPage *page = menu_page_of(c);
    if (!page || c->menu_page == MENU_WINDOWS || i < 0 || i >= page->count) return NULL;
    return &page->items[i];
}

static const char *menu_item_label(Client *c, Session *s, int i) {
    if (c->menu_page == MENU_WINDOWS) {
        /* Single-threaded server: a scratch buffer is safe for drawing. */
        static char label[192];
        int k = 0;
        for (Window *w = s->windows; w; w = w->next, k++) {
            if (k == i) {
                snprintf(label, sizeof label, "%d: %s", w->index, w->name);
                return label;
            }
        }
        return "";
    }
    const MenuItem *it = menu_item_at(c, i);
    return it ? it->label : "";
}

static int menu_item_enabled(Session *s, const MenuItem *it) {
    if (!it || it->type == MI_SEPARATOR) return 0;
    if (!(it->flags & MIF_PANES)) return 1;
    Window *w = s->cur;
    return w && w->root && layout_pane_count(w->root) >= 2;
}

static int menu_setting_on(Session *s, Client *c, const MenuItem *it) {
    switch (it->id) {
    case SET_STATUS_BOTTOM: return (s->status_bottom ? 1 : 0) == it->arg;
    case SET_STATUS_BG: {
        int32_t want = it->arg == 1 ? (int32_t)RGB_FLAG :
                       it->arg == 2 ? (int32_t)(RGB_FLAG | 0x191a1c) : 0;
        return s->status_bg == want;
    }
    case SET_SHOW_CWD:      return s->display.show_cwd == it->arg;
    case SET_SHOW_CLOCK:    return s->display.show_clock == it->arg;
    case SET_SHOW_MURX:     return s->display.show_murx == it->arg;
    case SET_SHOW_HOSTNAME: return s->display.show_hostname == it->arg;
    case SET_SHOW_PANE_TITLES: return s->display.show_pane_titles == it->arg;
    case SET_RESIZE_ON_INPUT: return s->resize_on_input == it->arg;
    case SET_PREFIX:        return c->prefix == it->arg;
    default: return 0;
    }
}

static void menu_draw(Grid *g, Session *s, Client *c) {
    const MenuPage *page = menu_page_of(c);
    if (!page) { c->mode = 0; return; }
    int count = menu_count(c, s);
    if (count < 1) { c->mode = 0; return; }

    if (c->menu_sel < 0) c->menu_sel = 0;
    if (c->menu_sel >= count) c->menu_sel = count - 1;

    int bottom = s->h - (s->status_bottom ? 1 : 0);
    if (bottom < 1) bottom = 1;
    const char *hint_text = "Up/Down move   Enter select   Esc close";
    int hint = bottom >= 6;
    int maxrows = bottom - 2 - (hint ? 2 : 0);
    if (maxrows < 1) maxrows = 1;
    if (c->menu_top < 0) c->menu_top = 0;
    if (c->menu_top > count - maxrows) c->menu_top = count - maxrows;
    if (c->menu_top < 0) c->menu_top = 0;
    if (c->menu_sel < c->menu_top) c->menu_top = c->menu_sel;
    if (c->menu_sel >= c->menu_top + maxrows) c->menu_top = c->menu_sel - maxrows + 1;
    int shown = count - c->menu_top;
    if (shown > maxrows) shown = maxrows;
    if (shown < 1) shown = 1;

    int width = text_width(page->title) + 8;
    for (int i = 0; i < count; i++) {
        int wl = text_width(menu_item_label(c, s, i)) + 10;
        if (wl > width) width = wl;
    }
    if (hint) {
        int hl = text_width(hint_text) + 4;
        if (hl > width) width = hl;
    }
    if (width > s->w) width = s->w;
    if (width < 8) width = s->w < 8 ? s->w : 8;

    int height = shown + 2 + (hint ? 2 : 0);
    int x0 = (s->w - width) / 2;
    if (x0 < 0) x0 = 0;
    int y0 = (bottom - height) / 2;
    if (y0 < 0) y0 = 0;
    int x1 = x0 + width - 1;

    for (int y = y0; y < y0 + height; y++) {
        for (int x = x0; x <= x1; x++) scr_put(g, x, y, ' ', MENU_ITEM, MENU_BG, 0);
    }

    scr_put(g, x0, y0, 0x256D, MENU_FRAME, MENU_BG, 0);           /* ╭ */
    scr_put(g, x1, y0, 0x256E, MENU_FRAME, MENU_BG, 0);           /* ╮ */
    for (int x = x0 + 1; x < x1; x++) scr_put(g, x, y0, 0x2500, MENU_FRAME, MENU_BG, 0);
    char title[160];
    snprintf(title, sizeof title, " %s ", page->title);
    draw_text_limited(g, x0 + 2, y0, title, MENU_TITLE, MENU_BG, A_BOLD, x1);

    for (int i = 0; i < shown; i++) {
        int idx = c->menu_top + i;
        int y = y0 + 1 + i;
        const MenuItem *it = menu_item_at(c, idx);
        int selected = idx == c->menu_sel;
        if (it && it->type == MI_SEPARATOR) {
            scr_put(g, x0, y, 0x251C, MENU_FRAME, MENU_BG, 0);
            scr_put(g, x1, y, 0x2524, MENU_FRAME, MENU_BG, 0);
            for (int x = x0 + 1; x < x1; x++)
                scr_put(g, x, y, 0x2500, MENU_DIM, MENU_BG, A_DIM);
            continue;
        }
        scr_put(g, x0, y, 0x2502, MENU_FRAME, MENU_BG, 0);
        scr_put(g, x1, y, 0x2502, MENU_FRAME, MENU_BG, 0);
        int enabled = menu_item_enabled(s, it);
        int fg = !enabled ? MENU_DIM : selected ? MENU_SELECT : MENU_ITEM;
        int attr = !enabled ? A_DIM : selected ? A_BOLD : 0;
        scr_put(g, x0 + 2, y, selected ? '>' : ' ', MENU_ACCENT, MENU_BG, A_BOLD);
        draw_text_limited(g, x0 + 4, y, menu_item_label(c, s, idx),
                          fg, MENU_BG, attr, x1 - 5);
        if (it && (it->type == MI_TOGGLE || it->type == MI_CHOICE)) {
            int on = menu_setting_on(s, c, it);
            draw_text_limited(g, x1 - 4, y, on ? "[x]" : "[ ]",
                              on ? MENU_ACCENT : MENU_DIM, MENU_BG, 0, x1);
        } else if (it && it->type == MI_SUBMENU) {
            scr_put(g, x1 - 2, y, 0x25B8, MENU_ACCENT, MENU_BG, 0);  /* ▸ */
        }
        if (c->menu_top > 0 && i == 0)
            scr_put(g, x1 - 1, y, 0x25B2, MENU_ACCENT, MENU_BG, 0); /* ▲ */
        else if (c->menu_top + shown < count && i == shown - 1)
            scr_put(g, x1 - 1, y, 0x25BC, MENU_ACCENT, MENU_BG, 0); /* ▼ */
    }

    if (hint) {
        int y = y0 + 1 + shown;
        scr_put(g, x0, y, 0x251C, MENU_FRAME, MENU_BG, 0);
        scr_put(g, x1, y, 0x2524, MENU_FRAME, MENU_BG, 0);
        for (int x = x0 + 1; x < x1; x++) scr_put(g, x, y, 0x2500, MENU_FRAME, MENU_BG, 0);
        int hx = x0 + (width - text_width(hint_text)) / 2;
        scr_put(g, x0, y + 1, 0x2502, MENU_FRAME, MENU_BG, 0);
        scr_put(g, x1, y + 1, 0x2502, MENU_FRAME, MENU_BG, 0);
        draw_text_limited(g, hx, y + 1, hint_text, MENU_DIM, MENU_BG, A_DIM, x1 - 1);
    }

    int by = y0 + height - 1;
    scr_put(g, x0, by, 0x2570, MENU_FRAME, MENU_BG, 0);           /* ╰ */
    scr_put(g, x1, by, 0x256F, MENU_FRAME, MENU_BG, 0);           /* ╯ */
    for (int x = x0 + 1; x < x1; x++) scr_put(g, x, by, 0x2500, MENU_FRAME, MENU_BG, 0);
}

static void compose(Session *s, Client *c, Grid *out) {
    grid_init(out, s->w, s->h);
    grid_clear(out, DEFAULT_COLOR);
    if (s->cur && s->cur->root) {
        uint8_t *mask = xcalloc((size_t)out->w, (size_t)out->h);
        border_collect(mask, s->w, s->h, s->cur->root, s->display.show_pane_titles);
        border_join(mask, s->w, s->h);
        draw_layout(out, s->cur->root);
        border_render(out, mask);
        if (s->display.show_pane_titles)
            draw_labels(out, s->cur->root, s->cur);
        free(mask);
    }
    Scrollback *view = scrollback_current(c, s);
    if (view) {
        Pane *p = s->cur->active;
        int top = view->grid.h - p->h - view->offset;
        for (int y = 0; y < p->h; y++) {
            for (int x = 0; x < p->w; x++) {
                Cell *src = grid_at(&view->grid, x, top + y);
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
    if (c->mode != 0 || scrollback_current(c, s)) return;
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
    int cube = C_GREY_0 + 36 * ri + 6 * gi + bi;
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

    if (grayd < cubed) return C_GREY_3 + gidx;
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
                     !scrollback_current(c, s) && s->cur && s->cur->active && s->cur->active->app_cursor;
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

static int read_exact(int fd, void *buf, size_t len) {
    unsigned char *p = buf;
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n > 0) {
            p += (size_t)n;
            len -= (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

/* Send a message header with file descriptors attached. The payload follows
   as ordinary stream bytes so callers can pass large handoff blobs. */
static int send_msg_fds(int fd, uint32_t type, const void *data, size_t len,
                        const int *fds, size_t nfds) {
    MsgHdr h;
    if (len > UINT32_MAX || nfds > MAX_HANDOFF_FDS) { errno = EMSGSIZE; return -1; }
    h.type = type;
    h.len = (uint32_t)len;

    char control[CMSG_SPACE(MAX_HANDOFF_FDS * sizeof(int))];
    memset(control, 0, sizeof control);
    struct iovec iov = { &h, sizeof h };
    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (nfds) {
        msg.msg_control = control;
        msg.msg_controllen = CMSG_SPACE(nfds * sizeof(int));
        struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(nfds * sizeof(int));
        memcpy(CMSG_DATA(cm), fds, nfds * sizeof(int));
    }
    ssize_t n;
    do {
        n = sendmsg(fd, &msg, 0);
    } while (n < 0 && errno == EINTR);
    if (n < 0) return -1;
    if (len && write_all(fd, data, len) < 0) return -1;
    return 0;
}

/* Receive exactly one message header, collecting any attached descriptors. */
static int recv_msg_header(int fd, MsgHdr *h, int *outfds, int maxfds, int *nfds) {
    *nfds = 0;
    unsigned char *p = (unsigned char *)h;
    size_t off = 0;
    int got_fds = 0;
    while (off < sizeof *h) {
        char control[CMSG_SPACE(MAX_HANDOFF_FDS * sizeof(int))];
        memset(control, 0, sizeof control);
        struct iovec iov = { p + off, sizeof *h - off };
        struct msghdr msg;
        memset(&msg, 0, sizeof msg);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof control;
        ssize_t n;
        do {
            n = recvmsg(fd, &msg, 0);
        } while (n < 0 && errno == EINTR);
        if (n <= 0) return -1;
        off += (size_t)n;
        if (!got_fds) {
            for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
                if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS) continue;
                size_t count = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                const int *fdp = (const int *)CMSG_DATA(cm);
                for (size_t i = 0; i < count; i++) {
                    if (*nfds < maxfds) outfds[(*nfds)++] = fdp[i];
                    else close(fdp[i]);
                }
                got_fds = 1;
            }
        }
    }
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
    /* Normally the most recently attached or resized client decides the
       session size. This makes a reattach from a larger terminal grow the
       session even if an older, smaller client is still connected. Such
       clients render only the part of the session that fits (see draw_frame).

       With resize-on-input, the last client to send any input decides instead;
       attach/resize order is only the fallback until the first input arrives. */
    Client *best = NULL;
    if (s->resize_on_input) {
        for (Client *c = s->clients; c; c = c->next) {
            if (!c->attached || c->dead || c->fd < 0 || !c->input_order) continue;
            if (!best || c->input_order > best->input_order) best = c;
        }
    }
    if (!best) {
        for (Client *c = s->clients; c; c = c->next) {
            if (!c->attached || c->dead || c->fd < 0) continue;
            if (!best || c->order > best->order) best = c;
        }
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

static void session_pane_free(Session *s, Pane *p) {
    for (Client *c = s->clients; c; c = c->next)
        scrollback_remove(scrollback_slot(c, p));
    pane_free(p);
}

static void window_free(Session *s, Window *w) {
    if (!w) return;
    if (w->root) {
        Pane *list[128];
        int n = 0;
        layout_collect(w->root, list, &n);
        for (int i = 0; i < n; i++) session_pane_free(s, list[i]);
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
    return pane_new(s->cwd, w, h, host, s->name, s->history_limit);
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
        window_free(s, w);
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
        session_pane_free(s, p);
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
    window_free(s, w);
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

static void menu_open_page(Client *c, int page, int sel) {
    c->mode = 2;
    c->menu_page = page;
    c->menu_sel = sel < 0 ? 0 : sel;
    c->menu_top = 0;
    c->menu_depth = 0;
}

static void menu_open(Client *c) {
    menu_open_page(c, MENU_MAIN, 0);
}

/* Ctrl-A w still opens the window list directly; Left returns to the main
 * menu, Escape closes it. */
static void start_menu(Client *c, Session *s) {
    int sel = 0, i = 0;
    for (Window *w = s->windows; w; w = w->next, i++) {
        if (w == s->cur) sel = i;
    }
    menu_open_page(c, MENU_WINDOWS, sel);
    c->menu_parent[0] = MENU_MAIN;
    c->menu_parent_sel[0] = 0;
    c->menu_depth = 1;
}

static void menu_enter(Client *c, int page) {
    if (c->menu_depth >= MENU_MAX_DEPTH || page < 0 || page >= MENU_COUNT) return;
    c->menu_parent[c->menu_depth] = c->menu_page;
    c->menu_parent_sel[c->menu_depth] = c->menu_sel;
    c->menu_depth++;
    c->menu_page = page;
    c->menu_sel = 0;
    c->menu_top = 0;
}

static void menu_back(Client *c) {
    if (c->menu_depth <= 0) {
        c->mode = 0;
        return;
    }
    c->menu_depth--;
    c->menu_page = c->menu_parent[c->menu_depth];
    c->menu_sel = c->menu_parent_sel[c->menu_depth];
    c->menu_top = 0;
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
    Scrollback **slot = scrollback_slot(c, p);
    if (!*slot) {
        *slot = xcalloc(1, sizeof **slot);
        (*slot)->pane = p;
    }
    Scrollback *view = *slot;
    grid_free(&view->grid);
    grid_init(&view->grid, p->grid->w, count + p->grid->h);
    for (int y = 0; y < count + p->grid->h; y++) {
        Grid *row = y < count ? &p->history[(p->history_start + y) % p->history_limit] : p->grid;
        int sy = y < count ? 0 : y - count;
        int width = row->w < view->grid.w ? row->w : view->grid.w;
        memcpy(&view->grid.c[y * view->grid.w], &row->c[sy * row->w],
               (size_t)width * sizeof(Cell));
        Cell *last = grid_at(&view->grid, view->grid.w - 1, y);
        if (cp_width(last->ch) == 2) *last = blank_cell();
    }
    view->offset = page_up ? p->h : 0;
    if (view->offset > count) view->offset = count;
}

static void report_clients(Client *c, Session *s) {
    int n = 0;
    for (Client *it = s->clients; it; it = it->next) if (it->attached) n++;
    client_message(c, "%d client%s attached", n, n == 1 ? "" : "s");
}

static void fit_session_to_client(Client *c, Session *s) {
    /* Make this client's terminal the size source again, even if another
       client attached or resized more recently. */
    c->order = client_next_order(s);
    session_recalc_size(s);
    invalidate_clients(s);
    s->redraw = 1;
    client_message(c, "Session resized to %dx%d", s->w, s->h);
}

static void menu_apply_setting(Client *c, Session *s, const MenuItem *it) {
    int value = it->type == MI_TOGGLE ? !menu_setting_on(s, c, it) : it->arg;
    switch (it->id) {
    case SET_STATUS_BOTTOM:
        s->status_bottom = value;
        session_layout(s);
        s->redraw = 1;
        break;
    case SET_STATUS_BG:
        s->status_bg = value == 1 ? (int32_t)RGB_FLAG :
                       value == 2 ? (int32_t)(RGB_FLAG | 0x191a1c) : 0;
        s->redraw = 1;
        break;
    case SET_SHOW_CWD:      s->display.show_cwd = value; break;
    case SET_SHOW_CLOCK:    s->display.show_clock = value; break;
    case SET_SHOW_MURX:     s->display.show_murx = value; break;
    case SET_SHOW_HOSTNAME: s->display.show_hostname = value; break;
    case SET_SHOW_PANE_TITLES:
        s->display.show_pane_titles = value;
        session_layout(s);
        s->redraw = 1;
        break;
    case SET_RESIZE_ON_INPUT:
        s->resize_on_input = value ? 1 : 0;
        session_recalc_size(s);
        break;
    case SET_PREFIX:        c->prefix = (unsigned char)value; break;
    default: return;
    }
    /* Window names carry the hostname setting, so refresh them at once. */
    s->last_name_check = 0;
    invalidate_clients(s);
}

static void menu_action(Client *c, Session *s, int action) {
    c->mode = 0;
    switch (action) {
    case ACT_NEW_WINDOW:   new_window(s); break;
    case ACT_SPLIT_ROWS:   split_pane(s, L_ROWS); break;
    case ACT_SPLIT_COLS:   split_pane(s, L_COLS); break;
    case ACT_CLOSE_PANE:   start_confirm(c, 1, "kill-pane? (y/n) "); break;
    case ACT_CLOSE_WINDOW: start_confirm(c, 3, "kill-window? (y/n) "); break;
    case ACT_RENAME_WINDOW: start_rename_prompt(c, s); break;
    case ACT_PANE_LEFT:    select_pane_dir(s, -1, 0); break;
    case ACT_PANE_DOWN:    select_pane_dir(s, 0, 1); break;
    case ACT_PANE_UP:      select_pane_dir(s, 0, -1); break;
    case ACT_PANE_RIGHT:   select_pane_dir(s, 1, 0); break;
    case ACT_PANE_NEXT:    cycle_pane(s, 1); break;
    case ACT_PANE_PREV:    cycle_pane(s, -1); break;
    case ACT_RESIZE_LEFT:  resize_pane_dir(s, -1, 0, 8); break;
    case ACT_RESIZE_RIGHT: resize_pane_dir(s, 1, 0, 8); break;
    case ACT_RESIZE_UP:    resize_pane_dir(s, 0, -1, 5); break;
    case ACT_RESIZE_DOWN:  resize_pane_dir(s, 0, 1, 5); break;
    case ACT_SCROLLBACK:   backscroll_start(c, s, 0); break;
    case ACT_DETACH: {
        char m[128];
        snprintf(m, sizeof m, "[detached (from session %s)]", s->name);
        client_detach(c, m);
        break;
    }
    case ACT_LOCK:     client_lock(c); break;
    case ACT_CLIENTS:  report_clients(c, s); break;
    case ACT_FIT_SIZE: fit_session_to_client(c, s); break;
    case ACT_KILL_SERVER: start_confirm(c, 2, "kill-server? (y/n) "); break;
    case ACT_RESET_DISPLAY:
        memset(&s->display, 0, sizeof s->display);
        s->status_bottom = 0;
        s->status_bg = 0;
        session_layout(s);
        s->last_name_check = 0;
        s->redraw = 1;
        invalidate_clients(s);
        client_message(c, "Display and status defaults restored");
        break;
    default: break;
    }
}

static void menu_activate(Client *c, Session *s) {
    int count = menu_count(c, s);
    if (c->menu_sel < 0 || c->menu_sel >= count) return;
    if (c->menu_page == MENU_WINDOWS) {
        int i = 0;
        for (Window *w = s->windows; w; w = w->next, i++) {
            if (i == c->menu_sel) {
                select_window(s, w);
                break;
            }
        }
        c->mode = 0;
        return;
    }
    const MenuItem *it = menu_item_at(c, c->menu_sel);
    if (!it || it->type == MI_SEPARATOR || !menu_item_enabled(s, it)) return;
    if (it->type == MI_ACTION) menu_action(c, s, it->id);
    else if (it->type == MI_SUBMENU) menu_enter(c, it->id);
    else menu_apply_setting(c, s, it);
}

/* Return zero for an incomplete page-key sequence. */
static size_t backscroll_key(Client *c, Scrollback *view, const unsigned char *buf, size_t n) {
    if (buf[0] == 27 && n == 1) return 0;
    if (buf[0] == 'q' || buf[0] == 3 || buf[0] == '\r' || buf[0] == '\n' ||
        (buf[0] == 27 && (n == 1 || buf[1] != '['))) {
        scrollback_remove(scrollback_slot(c, view->pane));
        return 1;
    }
    if (buf[0] == 27 && n >= 2 && buf[1] == '[') {
        size_t used = 2;
        while (used < n && !(buf[used] >= 0x40 && buf[used] <= 0x7e)) used++;
        if (used == n) return 0;
        used++;
        if (used == 4 && buf[3] == '~' && (buf[2] == '5' || buf[2] == '6')) {
            int height = view->pane->h;
            int max = view->grid.h - height;
            if (max < 0) max = 0;
            view->offset += buf[2] == '5' ? height : -height;
            if (view->offset < 0) view->offset = 0;
            if (view->offset > max) view->offset = max;
        }
        return used;
    }
    return 1;
}

static void prefix_command(Client *c, Session *s, int key) {
    if (scrollback_current(c, s) && !(key >= '0' && key <= '9')) {
        switch (key) {
        case 0x03: case 'c':
        case 0x0e: case ' ': case 'n':
        case 0x10: case 'p': case 0x7f:
        case 0x17: case 'w': case '"':
            break;
        default:
            return;
        }
    }
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
    case 'm':
        menu_open(c);
        break;
    case '*':
        report_clients(c, s);
        break;
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
        fit_session_to_client(c, s);
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
    case 'o':
        cycle_pane(s, 1);
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

/* Move the selection, skipping separators and entries that cannot be used
 * in the current state (for example pane commands with a single pane). */
static void menu_move(Client *c, Session *s, int dir) {
    int count = menu_count(c, s);
    if (count < 1) return;
    for (int step = 0; step < count; step++) {
        c->menu_sel += dir;
        if (c->menu_sel < 0) c->menu_sel = count - 1;
        else if (c->menu_sel >= count) c->menu_sel = 0;
        const MenuItem *it = menu_item_at(c, c->menu_sel);
        if (c->menu_page == MENU_WINDOWS || (it && menu_item_enabled(s, it))) return;
    }
}

static void menu_key(Client *c, Session *s, const unsigned char *buf, size_t n, size_t *used) {
    *used = 1;
    if (menu_count(c, s) < 1) {
        c->mode = 0;
        return;
    }
    unsigned char b = buf[0];
    int key = b;
    if (b == 0x1b) {
        if (n < 2) { *used = 0; return; }
        if (buf[1] == '[' || buf[1] == 'O') {
            if (n < 3) { *used = 0; return; }
            if (buf[2] == 'A') key = K_UP;
            else if (buf[2] == 'B') key = K_DOWN;
            else if (buf[2] == 'C') key = K_RIGHT;
            else if (buf[2] == 'D') key = K_LEFT;
            else key = 0;
            *used = 3;
            if (!key) return;
        } else {
            c->mode = 0;
            return;
        }
    }

    switch (key) {
    case K_UP: case 'k':
        menu_move(c, s, -1);
        break;
    case K_DOWN: case 'j':
        menu_move(c, s, 1);
        break;
    case K_LEFT: case 'h':
        menu_back(c);
        break;
    case K_RIGHT: case 'l': {
        const MenuItem *it = menu_item_at(c, c->menu_sel);
        if (it && it->type == MI_SUBMENU && menu_item_enabled(s, it)) menu_enter(c, it->id);
        break;
    }
    case '\r': case '\n':
        menu_activate(c, s);
        break;
    case 'q': case 0x03:
        c->mode = 0;
        break;
    default:
        break;
    }
}

static void client_feed(Client *c, Session *s, const unsigned char *buf, size_t n) {
    if (n > sizeof c->in - c->inlen) n = sizeof c->in - c->inlen;
    if (n == 0 && c->escape_since &&
        now_ms() - c->escape_since >= 100) {
        Scrollback *view = scrollback_current(c, s);
        if (view) scrollback_remove(scrollback_slot(c, view->pane));
        if (c->mode == 2) c->mode = 0;             /* a lone Escape closes menus */
        else if (!view && !c->prefix_pending && s->cur && s->cur->active)
            pane_write(s->cur->active, c->in, c->inlen);
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
        Scrollback *view = scrollback_current(c, s);
        if (view) {
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
            size_t used = backscroll_key(c, view, p, left);
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
        /* screen-256color describes Home and End as CSI 1~ and CSI 4~. */
        if (p[0] == 27) {
            if (left == 1) break;
            if (p[1] == '[' || p[1] == 'O') {
                if (left == 2) break;
                if (p[2] == 'H' || p[2] == 'F') {
                    if (s->cur && s->cur->active)
                        pane_write(s->cur->active, p[2] == 'H' ? "\x1b[1~" : "\x1b[4~", 4);
                    i += 3;
                    continue;
                }
            }
        }
        size_t j = i + 1;
        while (j < c->inlen && c->in[j] != c->prefix && c->in[j] != 27) j++;
        if (s->cur && s->cur->active) pane_write(s->cur->active, c->in + i, j - i);
        i = j;
    }
    if (i > 0) {
        memmove(c->in, c->in + i, c->inlen - i);
        c->inlen -= i;
    }
    if (c->inlen && c->in[0] == 27 &&
        ((!c->mode && !c->locked && !scrollback_current(c, s) && !c->prefix_pending) ||
         (c->inlen == 1 && (c->mode == 2 || scrollback_current(c, s) || c->prefix_pending)))) {
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
    window_free(s, w);
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
            session_pane_free(s, p);
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
    while (c->scrollbacks) scrollback_remove(&c->scrollbacks);
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

/* ------------------------------------------------------------------ */
/* session handoff serialization                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char *p;
    size_t len, cap;
} HBuf;

typedef struct {
    const unsigned char *p;
    size_t len, off;
} HRdBuf;

static void buf_reserve(HBuf *b, size_t need) {
    if (need <= b->cap) return;
    size_t cap = buffer_capacity(b->cap ? b->cap : 65536, need);
    b->p = xrealloc(b->p, cap);
    b->cap = cap;
}

static void buf_put(HBuf *b, const void *data, size_t len) {
    buf_reserve(b, size_add(b->len, len));
    memcpy(b->p + b->len, data, len);
    b->len += len;
}

static void buf_i32(HBuf *b, int32_t v) { buf_put(b, &v, sizeof v); }
static void buf_u32(HBuf *b, uint32_t v) { buf_put(b, &v, sizeof v); }
static void buf_u64(HBuf *b, uint64_t v) { buf_put(b, &v, sizeof v); }
static void buf_i64(HBuf *b, int64_t v) { buf_put(b, &v, sizeof v); }

static void buf_double(HBuf *b, double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    buf_u64(b, bits);
}

static void buf_str(HBuf *b, const char *s, size_t max) {
    size_t n = strlen(s);
    if (n >= max) n = max - 1;
    buf_u32(b, (uint32_t)n);
    buf_put(b, s, n);
}

static int rd_take(HRdBuf *r, void *data, size_t len) {
    if (len > r->len - r->off) return -1;
    memcpy(data, r->p + r->off, len);
    r->off += len;
    return 0;
}

static int rd_i32(HRdBuf *r, int32_t *v) { return rd_take(r, v, sizeof *v); }
static int rd_u32(HRdBuf *r, uint32_t *v) { return rd_take(r, v, sizeof *v); }
static int rd_u64(HRdBuf *r, uint64_t *v) { return rd_take(r, v, sizeof *v); }
static int rd_i64(HRdBuf *r, int64_t *v) { return rd_take(r, v, sizeof *v); }

static int rd_double(HRdBuf *r, double *v) {
    uint64_t bits;
    if (rd_u64(r, &bits) < 0) return -1;
    memcpy(v, &bits, sizeof *v);
    return 0;
}

static int rd_str(HRdBuf *r, char *out, size_t max) {
    uint32_t n;
    if (rd_u32(r, &n) < 0 || n >= max || rd_take(r, out, n) < 0) return -1;
    out[n] = '\0';
    return 0;
}

static void ser_pen(HBuf *b, const Pen *pen) {
    buf_i32(b, pen->fg);
    buf_i32(b, pen->bg);
    buf_u32(b, pen->attr);
}

static int rd_pen(HRdBuf *r, Pen *pen) {
    uint32_t attr;
    if (rd_i32(r, &pen->fg) < 0 || rd_i32(r, &pen->bg) < 0 ||
        rd_u32(r, &attr) < 0) return -1;
    pen->attr = (uint8_t)attr;
    return 0;
}

static void ser_grid(HBuf *b, const Grid *g) {
    buf_i32(b, g->w);
    buf_i32(b, g->h);
    if (g->w > 0 && g->h > 0)
        buf_put(b, g->c, size_mul((size_t)g->w, (size_t)g->h) * sizeof(Cell));
}

static int rd_grid(HRdBuf *r, Grid *g) {
    int32_t w, h;
    if (rd_i32(r, &w) < 0 || rd_i32(r, &h) < 0) return -1;
    if (w < 1 || h < 1 || w > MAX_DIMENSION || h > MAX_DIMENSION) return -1;
    size_t cells = size_mul((size_t)w, (size_t)h);
    if (cells > SIZE_MAX / sizeof(Cell) || cells * sizeof(Cell) > r->len - r->off) return -1;
    grid_free(g);
    grid_init(g, w, h);
    memcpy(g->c, r->p + r->off, cells * sizeof(Cell));
    r->off += cells * sizeof(Cell);
    return 0;
}

static void ser_layout(HBuf *b, const Layout *l) {
    buf_i32(b, l->type);
    buf_double(b, l->ratio);
    if (l->type == L_LEAF) {
        buf_i32(b, l->pane ? l->pane->id : 0);
    } else {
        ser_layout(b, l->a);
        ser_layout(b, l->b);
    }
}

static Layout *rd_layout(HRdBuf *r, Pane **panes, int npane) {
    int32_t type;
    Layout *l = xcalloc(1, sizeof *l);
    if (rd_i32(r, &type) < 0 || rd_double(r, &l->ratio) < 0) { free(l); return NULL; }
    l->type = (int)type;
    if (l->type == L_LEAF) {
        int32_t id;
        if (rd_i32(r, &id) < 0) { free(l); return NULL; }
        for (int i = 0; i < npane; i++) {
            if (panes[i]->id == id) { l->pane = panes[i]; break; }
        }
        if (!l->pane) { free(l); return NULL; }
        return l;
    }
    if (l->type != L_COLS && l->type != L_ROWS) { free(l); return NULL; }
    l->a = rd_layout(r, panes, npane);
    l->b = rd_layout(r, panes, npane);
    if (!l->a || !l->b) {
        layout_free(l);
        return NULL;
    }
    l->a->parent = l;
    l->b->parent = l;
    return l;
}

static Pane *find_pane_id(Pane **panes, int npane, int32_t id) {
    for (int i = 0; i < npane; i++) if (panes[i]->id == id) return panes[i];
    return NULL;
}

static void ser_pane(HBuf *b, const Pane *p, int master_idx, int tty_idx) {
    buf_i32(b, p->id);
    buf_i32(b, master_idx);
    buf_i32(b, tty_idx);
    buf_i32(b, p->pid);
    buf_i32(b, p->alive);
    buf_i32(b, p->x); buf_i32(b, p->y); buf_i32(b, p->w); buf_i32(b, p->h);
    ser_grid(b, &p->main_grid);
    ser_grid(b, &p->alt_grid);
    buf_i32(b, p->on_alt);
    buf_i32(b, p->history_start);
    buf_i32(b, p->history_count);
    for (int i = 0; i < p->history_count; i++) {
        int slot = (p->history_start + i) % p->history_limit;
        ser_grid(b, &p->history[slot]);
    }
    buf_i32(b, p->cx); buf_i32(b, p->cy); buf_i32(b, p->wrap_pending);
    buf_i32(b, p->scroll_top); buf_i32(b, p->scroll_bot);
    buf_i32(b, p->cursor_visible); buf_i32(b, p->app_cursor);
    buf_i32(b, p->insert_mode);
    ser_pen(b, &p->pen);
    buf_i32(b, p->saved_x); buf_i32(b, p->saved_y);
    ser_pen(b, &p->saved_pen);
    buf_i32(b, p->main_x); buf_i32(b, p->main_y);
    ser_pen(b, &p->main_pen);
    buf_i32(b, p->pstate);
    buf_str(b, p->seq, sizeof p->seq);
    buf_i32(b, p->seqlen);
    buf_i32(b, p->u8_need); buf_i32(b, p->u8_have);
    buf_u32(b, p->u8_cp);
    buf_str(b, p->title, sizeof p->title);
    buf_i32(b, p->title_pgrp);
    buf_str(b, p->remote_title, sizeof p->remote_title);
    buf_i32(b, p->remote_title_pgrp);
    buf_str(b, p->reported_cwd, sizeof p->reported_cwd);
    buf_i32(b, p->reported_cwd_pgrp);
    buf_i32(b, p->reported_root);
    buf_i32(b, p->reported_root_pgrp);
}

static int rd_pane(HRdBuf *r, Pane *p, int *fds, int nfds, int history_limit) {
    int32_t id, master_idx, tty_idx;
    if (rd_i32(r, &id) < 0 || rd_i32(r, &master_idx) < 0 ||
        rd_i32(r, &tty_idx) < 0) return -1;
    if (master_idx < 0 || master_idx >= nfds || tty_idx < 0 || tty_idx >= nfds) return -1;
    p->id = id;
    p->fd = fds[master_idx];
    p->tty_fd = fds[tty_idx];
    p->adopted = 1;
    if (rd_i32(r, (int32_t *)&p->pid) < 0 || rd_i32(r, &p->alive) < 0) return -1;
    if (rd_i32(r, &p->x) < 0 || rd_i32(r, &p->y) < 0 ||
        rd_i32(r, &p->w) < 0 || rd_i32(r, &p->h) < 0) return -1;
    if (rd_grid(r, &p->main_grid) < 0 || rd_grid(r, &p->alt_grid) < 0) return -1;
    int32_t on_alt, count, start;
    if (rd_i32(r, &on_alt) < 0 || rd_i32(r, &start) < 0 ||
        rd_i32(r, &count) < 0) return -1;
    p->on_alt = on_alt != 0;
    p->history_limit = history_limit;
    if (count < 0 || count > history_limit || start < 0 ||
        (history_limit > 0 && start >= history_limit)) return -1;
    p->history_start = start;
    p->history_count = count;
    if (count) {
        p->history = xcalloc((size_t)history_limit, sizeof *p->history);
        for (int i = 0; i < count; i++) {
            int slot = (start + i) % history_limit;
            if (rd_grid(r, &p->history[slot]) < 0) return -1;
        }
    }
    if (rd_i32(r, &p->cx) < 0 || rd_i32(r, &p->cy) < 0 ||
        rd_i32(r, &p->wrap_pending) < 0) return -1;
    if (rd_i32(r, &p->scroll_top) < 0 || rd_i32(r, &p->scroll_bot) < 0 ||
        rd_i32(r, &p->cursor_visible) < 0 || rd_i32(r, &p->app_cursor) < 0 ||
        rd_i32(r, &p->insert_mode) < 0) return -1;
    if (rd_pen(r, &p->pen) < 0 || rd_i32(r, &p->saved_x) < 0 ||
        rd_i32(r, &p->saved_y) < 0 || rd_pen(r, &p->saved_pen) < 0 ||
        rd_i32(r, &p->main_x) < 0 || rd_i32(r, &p->main_y) < 0 ||
        rd_pen(r, &p->main_pen) < 0) return -1;
    int32_t pstate, seqlen, u8_need, u8_have, u8cp;
    if (rd_i32(r, &pstate) < 0 || rd_str(r, p->seq, sizeof p->seq) < 0 ||
        rd_i32(r, &seqlen) < 0) return -1;
    p->pstate = (ParseState)pstate;
    p->seqlen = seqlen;
    if (rd_i32(r, &u8_need) < 0 || rd_i32(r, &u8_have) < 0 ||
        rd_i32(r, (int32_t *)&u8cp) < 0) return -1;
    p->u8_need = u8_need;
    p->u8_have = u8_have;
    p->u8_cp = (uint32_t)u8cp;
    if (rd_str(r, p->title, sizeof p->title) < 0 ||
        rd_i32(r, (int32_t *)&p->title_pgrp) < 0 ||
        rd_str(r, p->remote_title, sizeof p->remote_title) < 0 ||
        rd_i32(r, (int32_t *)&p->remote_title_pgrp) < 0 ||
        rd_str(r, p->reported_cwd, sizeof p->reported_cwd) < 0 ||
        rd_i32(r, (int32_t *)&p->reported_cwd_pgrp) < 0 ||
        rd_i32(r, &p->reported_root) < 0 ||
        rd_i32(r, (int32_t *)&p->reported_root_pgrp) < 0) return -1;
    p->grid = p->on_alt ? &p->alt_grid : &p->main_grid;
    return 0;
}

static void ser_client(HBuf *b, const Client *c, int fd_idx,
                       Pane **panes, int npane, size_t skip_net) {
    buf_i32(b, fd_idx);
    buf_i32(b, c->cols); buf_i32(b, c->rows);
    buf_i32(b, c->attached);
    buf_u64(b, c->order);
    buf_u64(b, c->input_order);
    buf_i32(b, c->truecolor);
    buf_u32(b, (uint32_t)c->inlen);
    buf_put(b, c->in, c->inlen);
    buf_i64(b, c->escape_since);
    /* Preserve partially received protocol messages. The invoking client's
       C_ADOPT request is stripped by the caller so it is not replayed in the
       new server. */
    size_t netoff = skip_net <= c->netlen ? skip_net : c->netlen;
    size_t netlen = c->netlen - netoff;
    buf_u32(b, (uint32_t)netlen);
    buf_put(b, c->net + netoff, netlen);
    buf_i32(b, c->locked); buf_i32(b, c->prefix_pending);
    buf_i32(b, c->prefix);
    buf_i32(b, c->mode); buf_i32(b, c->prompt_kind);
    buf_str(b, c->prompt, sizeof c->prompt);
    buf_i32(b, c->prompt_len); buf_i32(b, c->prompt_label_len);
    buf_i32(b, c->menu_page); buf_i32(b, c->menu_top);
    buf_i32(b, c->menu_sel); buf_i32(b, c->menu_depth);
    for (int i = 0; i < MENU_MAX_DEPTH; i++) {
        buf_i32(b, c->menu_parent[i]);
        buf_i32(b, c->menu_parent_sel[i]);
    }
    buf_i32(b, c->confirm_action);
    buf_str(b, c->msg, sizeof c->msg);
    buf_i64(b, c->msg_until);
    buf_str(b, c->title, sizeof c->title);
    buf_i32(b, c->dead);

    uint32_t nscroll = 0;
    for (Scrollback *sb = c->scrollbacks; sb; sb = sb->next) nscroll++;
    buf_u32(b, nscroll);
    for (Scrollback *sb = c->scrollbacks; sb; sb = sb->next) {
        int32_t id = 0;
        for (int i = 0; i < npane; i++) {
            if (panes[i] == sb->pane) { id = panes[i]->id; break; }
        }
        buf_i32(b, id);
        buf_i32(b, sb->offset);
        ser_grid(b, &sb->grid);
    }
}

static int rd_client(HRdBuf *r, Client *c, int *fds, int nfds,
                     Pane **panes, int npane) {
    int32_t fd_idx;
    if (rd_i32(r, &fd_idx) < 0) return -1;
    if (fd_idx < 0 || fd_idx >= nfds) return -1;
    c->fd = fds[fd_idx];
    if (rd_i32(r, &c->cols) < 0 || rd_i32(r, &c->rows) < 0 ||
        rd_i32(r, &c->attached) < 0) return -1;
    if (rd_u64(r, &c->order) < 0 || rd_u64(r, &c->input_order) < 0) return -1;
    if (rd_i32(r, &c->truecolor) < 0) return -1;
    uint32_t inlen;
    if (rd_u32(r, &inlen) < 0 || inlen > sizeof c->in ||
        rd_take(r, c->in, inlen) < 0) return -1;
    c->inlen = inlen;
    if (rd_i64(r, &c->escape_since) < 0) return -1;
    uint32_t netlen;
    if (rd_u32(r, &netlen) < 0 || netlen > sizeof c->net ||
        rd_take(r, c->net, netlen) < 0) return -1;
    c->netlen = netlen;
    int32_t prefix;
    if (rd_i32(r, &c->locked) < 0 || rd_i32(r, &c->prefix_pending) < 0 ||
        rd_i32(r, &prefix) < 0) return -1;
    c->prefix = (unsigned char)prefix;
    if (rd_i32(r, &c->mode) < 0 || rd_i32(r, &c->prompt_kind) < 0 ||
        rd_str(r, c->prompt, sizeof c->prompt) < 0 ||
        rd_i32(r, &c->prompt_len) < 0 ||
        rd_i32(r, &c->prompt_label_len) < 0) return -1;
    if (rd_i32(r, &c->menu_page) < 0 || rd_i32(r, &c->menu_top) < 0 ||
        rd_i32(r, &c->menu_sel) < 0 || rd_i32(r, &c->menu_depth) < 0) return -1;
    for (int i = 0; i < MENU_MAX_DEPTH; i++) {
        if (rd_i32(r, &c->menu_parent[i]) < 0 ||
            rd_i32(r, &c->menu_parent_sel[i]) < 0) return -1;
    }
    if (rd_i32(r, &c->confirm_action) < 0 ||
        rd_str(r, c->msg, sizeof c->msg) < 0 ||
        rd_i64(r, &c->msg_until) < 0 ||
        rd_str(r, c->title, sizeof c->title) < 0) return -1;
    int32_t dead;
    if (rd_i32(r, &dead) < 0) return -1;
    c->dead = dead != 0;

    uint32_t nscroll;
    if (rd_u32(r, &nscroll) < 0 || nscroll > 512) return -1;
    for (uint32_t i = 0; i < nscroll; i++) {
        Scrollback *sb = xcalloc(1, sizeof *sb);
        int32_t id, offset;
        if (rd_i32(r, &id) < 0 || rd_i32(r, &offset) < 0) {
            free(sb);
            return -1;
        }
        sb->pane = find_pane_id(panes, npane, id);
        if (!sb->pane || rd_grid(r, &sb->grid) < 0) {
            free(sb);
            return -1;
        }
        sb->offset = offset;
        sb->next = c->scrollbacks;
        c->scrollbacks = sb;
    }
    return 0;
}

static int session_serialize(Session *s, HBuf *b, int *fds, int *nfds,
                             Client *adopt_client, size_t adopt_skip) {
    Pane *panes[512];
    int npane = 0;
    session_panes(s, panes, 512, &npane);
    int nclient = 0;
    for (Client *c = s->clients; c; c = c->next) nclient++;
    /* listening socket + master/tty per pane + every client socket */
    if (count_add(count_add(1, count_add(npane, npane)), nclient) > MAX_HANDOFF_FDS)
        return -1;

    buf_u32(b, 0x4d48524c);  /* "MHRL" */
    buf_u32(b, 1);
    buf_str(b, s->name, sizeof s->name);
    buf_str(b, s->cwd, sizeof s->cwd);
    buf_str(b, s->sock_path, sizeof s->sock_path);
    buf_i32(b, s->w); buf_i32(b, s->h);
    buf_i32(b, s->status_bottom); buf_i32(b, s->resize_on_input);
    buf_i32(b, s->status_bg);
    buf_i32(b, s->history_limit);
    buf_i32(b, s->display.show_cwd); buf_i32(b, s->display.show_clock);
    buf_i32(b, s->display.show_murx); buf_i32(b, s->display.show_hostname);
    buf_i32(b, s->display.show_pane_titles);
    buf_u64(b, s->client_order);

    int fd_i = 0;
    fds[fd_i++] = s->listen_fd;
    buf_u32(b, (uint32_t)npane);
    for (int i = 0; i < npane; i++) {
        int master = fd_i++;
        int tty = fd_i++;
        fds[master] = panes[i]->fd;
        fds[tty] = panes[i]->tty_fd;
        ser_pane(b, panes[i], master, tty);
    }

    int nwin = 0;
    for (Window *w = s->windows; w; w = w->next) nwin++;
    buf_u32(b, (uint32_t)nwin);
    int cur_idx = -1, last_idx = -1, wi = 0;
    for (Window *w = s->windows; w; w = w->next, wi++) {
        if (w == s->cur) cur_idx = wi;
        if (w == s->last) last_idx = wi;
        buf_str(b, w->name, sizeof w->name);
        buf_i32(b, w->index);
        buf_i32(b, w->privileged);
        buf_i32(b, w->active ? w->active->id : 0);
        ser_layout(b, w->root);
    }
    buf_i32(b, cur_idx);
    buf_i32(b, last_idx);

    buf_u32(b, (uint32_t)nclient);
    for (Client *c = s->clients; c; c = c->next) {
        fds[fd_i++] = c->fd;
        ser_client(b, c, fd_i - 1, panes, npane,
                   c == adopt_client ? adopt_skip : 0);
    }
    *nfds = fd_i;
    return 0;
}

static Session *session_deserialize(HRdBuf *r, int *fds, int nfds) {
    uint32_t magic, version;
    if (rd_u32(r, &magic) < 0 || magic != 0x4d48524c ||
        rd_u32(r, &version) < 0 || version != 1) return NULL;
    Session *s = xcalloc(1, sizeof *s);
    if (rd_str(r, s->name, sizeof s->name) < 0 ||
        rd_str(r, s->cwd, sizeof s->cwd) < 0 ||
        rd_str(r, s->sock_path, sizeof s->sock_path) < 0) goto fail;
    if (rd_i32(r, &s->w) < 0 || rd_i32(r, &s->h) < 0 ||
        rd_i32(r, &s->status_bottom) < 0 ||
        rd_i32(r, &s->resize_on_input) < 0 ||
        rd_i32(r, &s->status_bg) < 0 ||
        rd_i32(r, &s->history_limit) < 0 ||
        s->history_limit < 0 || s->history_limit > MAX_HISTORY_LINES) goto fail;
    if (rd_i32(r, &s->display.show_cwd) < 0 ||
        rd_i32(r, &s->display.show_clock) < 0 ||
        rd_i32(r, &s->display.show_murx) < 0 ||
        rd_i32(r, &s->display.show_hostname) < 0 ||
        rd_i32(r, &s->display.show_pane_titles) < 0) goto fail;
    if (rd_u64(r, &s->client_order) < 0) goto fail;

    s->listen_fd = fds[0];
    uint32_t npane;
    if (rd_u32(r, &npane) < 0 || npane > 512) goto fail;
    Pane **panes = xcalloc(npane ? npane : 1, sizeof *panes);
    for (uint32_t i = 0; i < npane; i++) {
        panes[i] = xcalloc(1, sizeof *panes[i]);
        if (rd_pane(r, panes[i], fds, nfds, s->history_limit) < 0)
            goto fail_panes;
    }

    uint32_t nwin;
    if (rd_u32(r, &nwin) < 0 || nwin > 512) goto fail_panes;
    Window **window_tail = &s->windows;
    for (uint32_t i = 0; i < nwin; i++) {
        Window *w = xcalloc(1, sizeof *w);
        int32_t active_id;
        if (rd_str(r, w->name, sizeof w->name) < 0 ||
            rd_i32(r, &w->index) < 0 || rd_i32(r, &w->privileged) < 0 ||
            rd_i32(r, &active_id) < 0) { free(w); goto fail_panes; }
        w->root = rd_layout(r, panes, (int)npane);
        if (!w->root) { free(w); goto fail_panes; }
        w->active = find_pane_id(panes, (int)npane, active_id);
        *window_tail = w;
        window_tail = &w->next;
    }
    s->nwin = (int)nwin;
    int32_t cur_idx, last_idx;
    if (rd_i32(r, &cur_idx) < 0 || rd_i32(r, &last_idx) < 0) goto fail_panes;
    int wi = 0;
    for (Window *w = s->windows; w; w = w->next, wi++) {
        if (wi == cur_idx) s->cur = w;
        if (wi == last_idx) s->last = w;
    }

    uint32_t nclient;
    if (rd_u32(r, &nclient) < 0 || nclient > MAX_HANDOFF_FDS) goto fail_panes;
    Client **clients = xcalloc(nclient ? nclient : 1, sizeof *clients);
    for (uint32_t i = 0; i < nclient; i++) {
        clients[i] = xcalloc(1, sizeof *clients[i]);
        if (rd_client(r, clients[i], fds, nfds, panes, (int)npane) < 0)
            goto fail_clients;
    }
    for (uint32_t i = nclient; i > 0; i--) {
        clients[i - 1]->next = s->clients;
        s->clients = clients[i - 1];
    }
    free(clients);
    free(panes);
    s->running = 1;
    s->redraw = 1;
    s->last_status = now_ms();
    s->last_name_check = now_ms();
    session_layout(s);
    for (Client *c = s->clients; c; c = c->next) {
        client_invalidate(c);
        client_message(c, "Session adopted by new murx server");
    }
    return s;

fail_clients:
    for (uint32_t i = 0; i < nclient; i++) {
        if (!clients[i]) continue;
        if (clients[i]->prev_valid) grid_free(&clients[i]->prev);
        while (clients[i]->scrollbacks)
            scrollback_remove(&clients[i]->scrollbacks);
        free(clients[i]);
    }
    free(clients);
fail_panes:
    for (uint32_t i = 0; i < npane; i++) {
        if (!panes[i]) continue;
        grid_free(&panes[i]->main_grid);
        grid_free(&panes[i]->alt_grid);
        if (panes[i]->history) {
            for (int k = 0; k < panes[i]->history_count; k++)
                grid_free(&panes[i]->history[k]);
            free(panes[i]->history);
        }
        free(panes[i]);
    }
    free(panes);
fail: {
    /* Panes are freed separately above; free only the window scaffolding. */
    Window *w = s->windows;
    while (w) {
        Window *next = w->next;
        layout_free(w->root);
        free(w);
        w = next;
    }
    free(s);
    return NULL;
}
}

static void server_handoff(Client *c, Session *s, int handoff_fd,
                           uint32_t adopt_len) {
    HBuf b;
    memset(&b, 0, sizeof b);
    int fds[MAX_HANDOFF_FDS];
    int nfds = 0;
    MsgHdr fail = { 0, 0 };
    size_t adopt_skip = size_add(sizeof(MsgHdr), adopt_len);
    if (handoff_fd < 0 ||
        session_serialize(s, &b, fds, &nfds, c, adopt_skip) < 0 ||
        b.len == 0 || b.len > MAX_HANDOFF_BYTES) {
        if (handoff_fd >= 0) (void)write_all(handoff_fd, &fail, sizeof fail);
        free(b.p);
        return;
    }
    if (send_msg_fds(handoff_fd, S_HANDOFF, b.p, b.len, fds, (size_t)nfds) < 0) {
        (void)write_all(handoff_fd, &fail, sizeof fail);
    } else {
        /* The new server owns duplicates of every descriptor. Exit without
           closing our copies, detaching clients or unlinking the socket. */
        s->adopted = 1;
        s->running = 0;
    }
    free(b.p);
}

static void handle_message(Client *c, Session *s, uint32_t type,
                           const unsigned char *data, uint32_t len,
                           int *fds, int nfds) {
    if (type == C_ADOPT) {
        server_handoff(c, s, nfds > 0 ? fds[0] : -1, len);
        return;
    }
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
            uint32_t color = 0;
            if (len >= 24) memcpy(&color, data + 20, 4);
            if (color && (color >> 24) != 1) { c->dead = 1; return; }
            uint32_t size_mode = SIZE_MODE_KEEP;
            if (len >= 28) memcpy(&size_mode, data + 24, 4);
            if (size_mode > SIZE_MODE_ON) { c->dead = 1; return; }
            if (size_mode == SIZE_MODE_ON) s->resize_on_input = 1;
            if (color) s->status_bg = (int32_t)color;
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
        /* Track input even while the mode is off so enabling it later (from
           the menu or a reattach) already knows the last active client. */
        if (len && c->attached) {
            c->input_order = client_next_order(s);
            if (s->resize_on_input) session_recalc_size(s);
        }
        client_feed(c, s, data, len);
    }
}

static void client_read_socket(Client *c, Session *s) {
    unsigned char tmp[16384];
    char control[CMSG_SPACE(MAX_HANDOFF_FDS * sizeof(int))];
    memset(control, 0, sizeof control);
    struct iovec iov = { tmp, sizeof tmp };
    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof control;

    ssize_t n;
    do {
        n = recvmsg(c->fd, &msg, 0);
    } while (n < 0 && errno == EINTR);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        c->dead = 1;
        return;
    }
    int msgfds[MAX_HANDOFF_FDS];
    int nfds = 0;
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
        if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS) continue;
        size_t count = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const int *fdp = (const int *)CMSG_DATA(cm);
        for (size_t i = 0; i < count && nfds < MAX_HANDOFF_FDS; i++)
            msgfds[nfds++] = fdp[i];
    }

    if ((size_t)n > sizeof c->net - c->netlen) {
        c->dead = 1;
        for (int i = 0; i < nfds; i++) close(msgfds[i]);
        return;
    }
    memcpy(c->net + c->netlen, tmp, (size_t)n);
    c->netlen += (size_t)n;

    size_t off = 0;
    int first = 1;
    while (c->netlen - off >= sizeof(MsgHdr)) {
        MsgHdr h;
        memcpy(&h, c->net + off, sizeof h);
        if (h.len > sizeof c->net - sizeof h) { c->dead = 1; break; }
        if (h.len > c->netlen - off - sizeof h) break;
        handle_message(c, s, h.type, c->net + off + sizeof h, h.len,
                       first ? msgfds : NULL, first ? nfds : 0);
        first = 0;
        off += sizeof h + h.len;
    }
    if (off > 0) {
        memmove(c->net, c->net + off, c->netlen - off);
        c->netlen -= off;
    }
    for (int i = 0; i < nfds; i++) close(msgfds[i]);
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

/* Adopted shells are no longer children of the server. */
static int adopted_shell_exited(pid_t pid) {
    if (kill(pid, 0) < 0 && errno == ESRCH) return 1;
#ifdef __linux__
    /* A shell can remain a zombie until its new parent reaps it. */
    char path[64], line[256];
    snprintf(path, sizeof path, "/proc/%ld/status", (long)pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char state = '\0';
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "State: %c", &state) == 1) break;
    }
    fclose(f);
    return state == 'Z' || state == 'X';
#else
    return 0;
#endif
}

static void run_server(Session *s) {
    int maxpanes = count_add(session_pane_count(s), 64);
    Pane **panes = xcalloc((size_t)maxpanes, sizeof(Pane *));
    unsigned char rbuf[65536];
    int dirty = 1;
    long long next_adopted_check = 0;

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
                break;
            }
        }
        long long now = now_ms();
        int check_adopted = now >= next_adopted_check;
        if (check_adopted) next_adopted_check = now + 200;
        for (int i = 0; i < np; i++) {
            Pane *p = panes[i];
            if (check_adopted && p->adopted && p->pid > 0 &&
                adopted_shell_exited(p->pid))
                p->pid = -1;
            if (p->pid != -1) continue;
            for (;;) {
                ssize_t n = read(p->fd, rbuf, sizeof rbuf);
                if (n > 0) em_feed(p, rbuf, (size_t)n);
                else if (n >= 0 || errno != EINTR) break;
            }
            p->alive = 0;
            reaped = 1;
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
        for (Client *c = s->clients; c; c = c->next) {
            if (!c->escape_since) continue;
            long long remaining = 100 - (now_ms() - c->escape_since);
            if (remaining < timeout) timeout = remaining > 0 ? (int)remaining : 0;
        }
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

    if (!s->adopted) {
        char msg[128];
        snprintf(msg, sizeof msg, "[murx: session \"%s\" server exited]", s->name);
        server_notify_all(s, msg);
    }
    free(panes);
}

static void server_cleanup(Session *s) {
    Client *c = s->clients;
    while (c) {
        Client *next = c->next;
        if (c->fd >= 0) close(c->fd);
        if (c->prev_valid) grid_free(&c->prev);
        while (c->scrollbacks) scrollback_remove(&c->scrollbacks);
        free(c);
        c = next;
    }
    s->clients = NULL;
    Window *w = s->windows;
    while (w) {
        Window *next = w->next;
        window_free(s, w);
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

static void server_start(const char *path, const char *name, int readyfd,
                         const DisplayOptions *display, int history_limit,
                         int adopt_fd, int *adopt_fds, int adopt_nfds) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = term_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    Session *s = NULL;
    int lfd = -1;
    if (adopt_fd >= 0) {
        MsgHdr h;
        if (adopt_nfds < 1 || read_exact(adopt_fd, &h, sizeof h) < 0 ||
            h.type != S_HANDOFF || h.len == 0 || h.len > MAX_HANDOFF_BYTES) {
            server_fail(readyfd, "adopt handshake", EPROTO);
            _exit(1);
        }
        unsigned char *payload = xmalloc(h.len ? h.len : 1);
        if (read_exact(adopt_fd, payload, h.len) < 0) {
            free(payload);
            server_fail(readyfd, "adopt handshake", EPROTO);
            _exit(1);
        }
        HRdBuf r = { payload, h.len, 0 };
        s = session_deserialize(&r, adopt_fds, adopt_nfds);
        free(payload);
        if (!s) {
            server_fail(readyfd, "adopt state", EPROTO);
            _exit(1);
        }
        lfd = s->listen_fd;
    } else {
        lfd = socket(AF_UNIX, SOCK_STREAM, 0);
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

        s = xcalloc(1, sizeof *s);
        snprintf(s->name, sizeof s->name, "%s", name);
        snprintf(s->sock_path, sizeof s->sock_path, "%s", path);
        if (!getcwd(s->cwd, sizeof s->cwd)) snprintf(s->cwd, sizeof s->cwd, "/");
        s->listen_fd = lfd;
        s->w = 80;
        s->h = 24;
        s->running = 1;
        s->display = *display;
        s->history_limit = history_limit;
        s->last_status = now_ms();

        Window *w = window_new(s, NULL);
        if (!w) {
            server_fail(readyfd, "cannot create pane (openpty)", errno);
            server_cleanup(s);
            _exit(1);
        }
        s->cur = w;
        session_layout(s);
    }

    if (readyfd >= 0) {
        char ok = '1';
        write_all(readyfd, &ok, 1);
        close(readyfd);
    }

    run_server(s);
    if (s->adopted) _exit(0);
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

/* Result of a successful C_ADOPT exchange. The descriptors were received via
   SCM_RIGHTS and are inherited by the new server across fork(). */
typedef struct {
    int client_fd;         /* existing connection, kept for the attach below */
    MsgHdr h;
    unsigned char *payload;
    int *fds;
    int nfds;
} AdoptHandoff;

static void adopt_handoff_free(AdoptHandoff *a) {
    if (!a) return;
    for (int i = 0; i < a->nfds; i++) {
        if (a->fds[i] >= 0) close(a->fds[i]);
    }
    free(a->fds);
    free(a->payload);
    memset(a, 0, sizeof *a);
}

static void close_fds(int *fds, int nfds) {
    for (int i = 0; i < nfds; i++) {
        if (fds[i] >= 0) close(fds[i]);
    }
}

/* Ask a cooperating server to hand its session to a new process. An older
   server ignores C_ADOPT, so a timeout is a clean "cannot adopt" result. */
static int request_adoption(int client_fd, AdoptHandoff *a) {
    memset(a, 0, sizeof *a);
    a->client_fd = client_fd;

    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) return -1;
    int pass_fd = sp[1];
    if (send_msg_fds(client_fd, C_ADOPT, NULL, 0, &pass_fd, 1) < 0) {
        int err = errno;
        close(sp[0]);
        close(sp[1]);
        errno = err;
        return -1;
    }
    close(sp[1]);

    struct pollfd pfd;
    pfd.fd = sp[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    int pr;
    do {
        pr = poll(&pfd, 1, ADOPT_TIMEOUT_MS);
    } while (pr < 0 && errno == EINTR);
    if (pr == 0) {
        close(sp[0]);
        errno = ETIMEDOUT;
        return -1;
    }
    if (pr < 0) {
        int err = errno;
        close(sp[0]);
        errno = err;
        return -1;
    }

    int fds[MAX_HANDOFF_FDS];
    int nfds = 0;
    MsgHdr h;
    if (recv_msg_header(sp[0], &h, fds, MAX_HANDOFF_FDS, &nfds) < 0 ||
        h.type != S_HANDOFF || h.len == 0 || h.len > MAX_HANDOFF_BYTES ||
        nfds < 1) {
        close_fds(fds, nfds);
        close(sp[0]);
        errno = EPROTO;
        return -1;
    }
    unsigned char *payload = xmalloc(h.len);
    if (read_exact(sp[0], payload, h.len) < 0) {
        int err = errno ? errno : EPROTO;
        close_fds(fds, nfds);
        close(sp[0]);
        free(payload);
        errno = err;
        return -1;
    }
    close(sp[0]);

    a->h = h;
    a->payload = payload;
    a->nfds = nfds;
    a->fds = xmalloc((size_t)nfds * sizeof *a->fds);
    memcpy(a->fds, fds, (size_t)nfds * sizeof *a->fds);
    return 0;
}

static int spawn_server(const char *path, const char *name,
                        const DisplayOptions *display, int history_limit,
                        const AdoptHandoff *adopt) {
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

    int fd = -1;
    if (!adopt) {
        fd = connect_session(path);
        if (fd >= 0) {
            flock(lock, LOCK_UN);
            close(lock);
            return fd;
        }
    }

    int p[2];
    if (pipe(p) < 0) {
        flock(lock, LOCK_UN);
        close(lock);
        return -1;
    }
    int hp[2] = { -1, -1 };
    if (adopt && pipe(hp) < 0) {
        close(p[0]);
        close(p[1]);
        flock(lock, LOCK_UN);
        close(lock);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        if (adopt) {
            close(hp[0]);
            close(hp[1]);
        }
        flock(lock, LOCK_UN);
        close(lock);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        if (adopt) close(hp[1]);
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
        if (adopt) {
            server_start(path, name, p[1], display, history_limit, hp[0],
                         adopt->fds, adopt->nfds);
        } else {
            server_start(path, name, p[1], display, history_limit, -1, NULL, 0);
        }
        _exit(1);
    }
    close(p[1]);

    int handoff_err = 0;
    if (adopt) {
        close(hp[0]);
        if (write_all(hp[1], &adopt->h, sizeof adopt->h) < 0 ||
            write_all(hp[1], adopt->payload, adopt->h.len) < 0)
            handoff_err = errno;
        close(hp[1]);
    }

    char msg[256];
    size_t used = 0;
    ssize_t got;
    for (;;) {
        got = read(p[0], msg + used, sizeof msg - 1 - used);
        if (got > 0) {
            used += (size_t)got;
            if (used >= sizeof msg - 1) break;
        } else if (got < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    close(p[0]);
    msg[used] = '\0';
    waitpid(pid, NULL, 0);
    flock(lock, LOCK_UN);
    close(lock);

    if (handoff_err) {
        errno = handoff_err;
        return -1;
    }
    if (used < 1 || msg[0] != '1') {
        if (used > 0) {
            fprintf(stderr, "murx: server failed to start: %s\n", msg);
            errno = 0;  /* already reported */
        } else {
            errno = EIO;
        }
        return -1;
    }
    if (adopt) return adopt->client_fd;
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

static int run_client(int fd, const char *name, unsigned char prefix, int status,
                      uint32_t color, uint32_t resize_mode) {
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
    uint32_t attach[7] = { (uint32_t)cols, (uint32_t)rows,
                           (uint32_t)terminal_truecolor(), prefix, (uint32_t)status, color,
                           resize_mode };
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

/* Two-column help rows: the left field is padded so descriptions line up. */
static void help_row(FILE *out, const char *left, const char *right) {
    fprintf(out, "  %-24s  %s\n", left, right);
}

static void print_help(FILE *out) {
    fputs("  +-------------------------+\n"
          "  | murx |                  |\n"
          "  +-------------+-----------+\n"
          "  | >_          | ========  |\n"
          "  |             | ====      |\n"
          "  |    terminal +-----------+\n"
          "  | multiplexer |  :|:||:   |\n"
          "  +-------------+-----------+\n"
          "\n", out);
    fprintf(out, "murx %s\n\n", MURX_VERSION);
    fputs("usage: murx [OPTIONS] [session]\n"
          "\n"
          "Attach to a running session, starting a server if needed. The session\n"
          "name defaults to \"0\". Detach with the prefix followed by d.\n"
          "\n"
          "Options:\n", out);

    help_row(out, "-h, --help", "show this help and exit");
    help_row(out, "-v, --version", "print the version and exit");
    help_row(out, "-l, --list-sessions", "list running sessions without attaching");
    help_row(out, "--adopt", "move a session to a new server binary");
    help_row(out, "-p, --prefix LETTER", "client prefix Ctrl-LETTER (a-z, default: a)");
    help_row(out, "-s, --status POSITION", "status line: top or bottom (new sessions: top)");
    help_row(out, "-c, --color RRGGBB", "status bar background, six hex digits (e.g. 000000)");
    char history_help[96];
    snprintf(history_help, sizeof history_help,
             "history lines per pane in new sessions (0-%d, default: %d)",
             MAX_HISTORY_LINES, DEFAULT_HISTORY_LINES);
    help_row(out, "-H, --history LINES", history_help);
    help_row(out, "-d, --cwd", "show current directory in status bar");
    help_row(out, "-C, --clock", "show clock in status bar");
    help_row(out, "-m, --murx", "show [murx] in status bar unless --cwd is set");
    help_row(out, "-n, --hostname", "show hostname: prefix in window titles");
    help_row(out, "-t, --titles", "show the title row above each pane (default: off)");
    help_row(out, "--resize-on-input", "session size follows the last client with input");

    fputs("\n"
          "--adopt asks the running server to hand the session to a new server\n"
          "process, so an upgraded binary can take over without restarting panes.\n"
          "It works with a session name or with the default session, and needs\n"
          "cooperation from the running server; older servers are attached to\n"
          "unchanged. Clients see a short notification when adoption succeeds.\n"
          "\n"
          "Without -s or -c, an existing session keeps its position and color.\n"
          "Without --resize-on-input, a session keeps its current size mode.\n"
          "Display options and the size mode can be changed later\n"
          "from the Configuration menu.\n"
          "\n"
          "Shortcuts (press the prefix, release it, then press the key shown;\n"
          "Ctrl-A is the default prefix):\n"
          "\n", out);

    fputs("Menu\n", out);
    help_row(out, "m", "open the main menu");

    fputs("\nWindows\n", out);
    help_row(out, "c, Ctrl-C", "new window");
    help_row(out, "n, Space, Ctrl-N, Ctrl-@", "next window");
    help_row(out, "p, Ctrl-P, Backspace", "previous window");
    help_row(out, "Ctrl-A", "last window (press prefix twice)");
    help_row(out, "w, Ctrl-W, \"", "window menu");
    help_row(out, "A", "rename window");
    help_row(out, "0 .. 9", "select window by number");
    help_row(out, "&", "kill window (confirm)");

    fputs("\nPanes\n", out);
    help_row(out, "|, %", "split left/right");
    help_row(out, "-", "split top/bottom");
    help_row(out, "h, j, k, l", "select pane left/down/up/right");
    help_row(out, "Up, Down, Left, Right", "select pane");
    help_row(out, "Tab, Shift-Tab, o", "next/previous pane");
    help_row(out, "Ctrl-H, Ctrl-J", "resize pane left/down");
    help_row(out, "Ctrl-K, Ctrl-L", "resize pane up/right");
    help_row(out, "K", "kill pane (confirm)");
    help_row(out, "PageUp, [", "scrollback (PgUp/PgDn; q/Esc exits)");

    fputs("\nClient and session\n", out);
    help_row(out, "a", "send prefix literally to the pane");
    help_row(out, "x, Ctrl-X", "lock the client");
    help_row(out, "*", "list attached clients");
    help_row(out, "r", "show \"Configuration Reloaded!\"");
    help_row(out, "R", "resize session to this terminal");
    help_row(out, "d, Ctrl-D", "detach");
    help_row(out, "\\", "kill server (confirm)");

    fputs("\nMenus: Up/Down or j/k move, Left/Right or h/l enter/leave submenus,\n"
          "Enter selects, q/Escape/Ctrl-C closes. Prompts: Enter accepts,\n"
          "Escape/Ctrl-C cancels, Ctrl-U clears. Window shortcuts work in scrollback.\n"
          "See murx(1) for the full manual.\n", out);
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
    /* The adoption handoff writes to a pipe whose reader is a forked server;
       a failed start must surface as EPIPE, not kill this process. */
    signal(SIGPIPE, SIG_IGN);

    int argi = 1;
    unsigned char prefix = DEFAULT_PREFIX;
    int status = 0;
    uint32_t color = 0;
    DisplayOptions display = {0};
    int history_limit = DEFAULT_HISTORY_LINES;
    uint32_t resize_mode = SIZE_MODE_KEEP;
    int list = 0;
    int adopt = 0;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1]) {
        const char *option = argv[argi++];
        if (strcmp(option, "--") == 0) break;
        if (strcmp(option, "-h") == 0 || strcmp(option, "--help") == 0) {
            print_help(stdout);
            return 0;
        }
        if (strcmp(option, "-v") == 0 || strcmp(option, "--version") == 0) {
            puts("murx " MURX_VERSION);
            return 0;
        }
        if (strcmp(option, "-l") == 0 || strcmp(option, "--list-sessions") == 0) {
            list = 1;
        } else if (strcmp(option, "-H") == 0 || strcmp(option, "--history") == 0) {
            const char *value = argi < argc ? argv[argi] : "";
            char *end;
            long lines = strtol(value, &end, 10);
            if (*value < '0' || *value > '9' || *end || lines > MAX_HISTORY_LINES) {
                fprintf(stderr, "murx: --history requires a line count from 0 to %d\n",
                        MAX_HISTORY_LINES);
                return 2;
            }
            history_limit = (int)lines;
            argi++;
        } else if (strcmp(option, "--adopt") == 0) {
            adopt = 1;
        } else if (strcmp(option, "-d") == 0 || strcmp(option, "--cwd") == 0) {
            display.show_cwd = 1;
        } else if (strcmp(option, "-C") == 0 || strcmp(option, "--clock") == 0) {
            display.show_clock = 1;
        } else if (strcmp(option, "-m") == 0 || strcmp(option, "--murx") == 0) {
            display.show_murx = 1;
        } else if (strcmp(option, "-n") == 0 || strcmp(option, "--hostname") == 0) {
            display.show_hostname = 1;
        } else if (strcmp(option, "-t") == 0 || strcmp(option, "--titles") == 0) {
            display.show_pane_titles = 1;
        } else if (strcmp(option, "--resize-on-input") == 0) {
            resize_mode = SIZE_MODE_ON;
        } else if (strcmp(option, "-p") == 0 || strcmp(option, "--prefix") == 0) {
            if (argi >= argc || !parse_prefix(argv[argi], &prefix)) {
                fprintf(stderr, "murx: %s requires a letter from a through z\n", option);
                return 2;
            }
            argi++;
        } else if (strcmp(option, "-c") == 0 || strcmp(option, "--color") == 0) {
            if (argi >= argc || strlen(argv[argi]) != 6 ||
                strspn(argv[argi], "0123456789abcdefABCDEF") != 6) {
                fprintf(stderr, "murx: %s requires six hexadecimal digits (RRGGBB)\n", option);
                return 2;
            }
            color = RGB_FLAG | (uint32_t)strtoul(argv[argi++], NULL, 16);
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

    const char *in_murx = getenv("MURX");
    if (in_murx && *in_murx) {
        fprintf(stderr,
                "murx: already running inside murx session '%s'\n"
                "  Refusing to nest. Unset MURX to override: env -u MURX murx\n",
                in_murx);
        return 1;
    }

    char path[PATH_MAX];
    make_socket_path(name, path, sizeof path);

    int fd = connect_session(path);
    AdoptHandoff handoff;
    memset(&handoff, 0, sizeof handoff);
    if (fd >= 0 && adopt) {
        if (request_adoption(fd, &handoff) == 0) {
            int adopted_fd = spawn_server(path, name, &display, history_limit,
                                          &handoff);
            adopt_handoff_free(&handoff);
            if (adopted_fd < 0) {
                int err = errno;
                close(fd);
                if (err) {
                    fprintf(stderr, "murx: cannot hand off session '%s': %s\n",
                            name, strerror(err));
                }
                return 1;
            }
            fd = adopted_fd;
        } else {
            int err = errno ? errno : EPROTO;
            if (err == ETIMEDOUT) {
                fprintf(stderr,
                        "murx: session '%s' runs a server without adoption support; "
                        "attaching normally\n", name);
            } else {
                fprintf(stderr, "murx: cannot adopt session '%s': %s; "
                                "attaching normally\n", name, strerror(err));
            }
        }
    }
    if (fd < 0) fd = spawn_server(path, name, &display, history_limit, NULL);
    if (fd < 0) {
        if (errno != 0) {
            fprintf(stderr, "murx: cannot start or attach session '%s': %s\n",
                    name, strerror(errno));
        }
        return 1;
    }
    return run_client(fd, name, prefix, status, color, resize_mode);
}
