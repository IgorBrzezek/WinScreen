#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include "renderer.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----- helpers ----- */

static int visible_len(const char *s)
{
    int len = 0;
    while (*s) {
        if (*s == '\x1b' && *(s+1) == '[') {
            s += 2;
            while (*s && *s != 'm') s++;
            if (*s) s++;
        } else {
            len++;
            s++;
        }
    }
    return len;
}

static void print_utf8_to_buf(char *buf, int *pos, int size, wchar_t wch)
{
    if (*pos >= size - 8) return;
    if (wch < 0x80) {
        buf[(*pos)++] = (char)wch;
    } else if (wch < 0x800) {
        buf[(*pos)++] = (char)(0xC0 | (wch >> 6));
        buf[(*pos)++] = (char)(0x80 | (wch & 0x3F));
    } else {
        buf[(*pos)++] = (char)(0xE0 | (wch >> 12));
        buf[(*pos)++] = (char)(0x80 | ((wch >> 6) & 0x3F));
        buf[(*pos)++] = (char)(0x80 | (wch & 0x3F));
    }
}

/* ----- client terminal init / cleanup ----- */

void render_init(void)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);

    printf("\x1b[?1049h");
    fflush(stdout);
}

void render_cleanup(void)
{
    printf("\x1b[?25h");
    printf("\x1b[?1049l");
    printf("\x1b[0m");
    printf("\x1b[2J");
    printf("\x1b[1;1H");
    fflush(stdout);
}

/* ----- server-side: render buffer line to ANSI string ----- */

static int render_cells_to_string(char *buf, int buf_size, Cell *cells, int cols)
{
    int pos = 0;
    uint8_t last_fg = 255;
    uint8_t last_bg = 255;
    bool last_bold = false;

    for (int x = 0; x < cols; x++) {
        Cell *c = &cells[x];

        if (c->fg_color != last_fg || c->bg_color != last_bg || c->bold != last_bold) {
            int n = snprintf(buf + pos, buf_size - pos,
                            "\x1b[0;%s38;5;%d;48;5;%dm",
                            c->bold ? "1;" : "",
                            c->fg_color, c->bg_color);
            if (n > 0) pos += n;
            if (pos >= buf_size) return pos;
            last_fg = c->fg_color;
            last_bg = c->bg_color;
            last_bold = c->bold;
        }
        print_utf8_to_buf(buf, &pos, buf_size, c->ch);
        if (pos >= buf_size) return pos;
    }

    if (last_fg != 255 || last_bg != 0 || last_bold) {
        int n = snprintf(buf + pos, buf_size - pos, "\x1b[0m");
        if (n > 0) pos += n;
    }
    return pos;
}

static int render_line_to_string(char *buf, int buf_size, TerminalBuffer *tb, int row)
{
    return render_cells_to_string(buf, buf_size, tb->grid[row], tb->cols);
}

/* ----- server-side: status bar content (no cursor positioning) ----- */

static int render_status_bar_content(char *buf, int buf_size, bool clock_update)
{
    (void)clock_update;
    int pos = 0;
    int cols = app.terminal_cols;
    if (cols < 10) cols = 80;

    /* ---- left part ---- */
    char left[2048];
    int lpos = 0;

    /* open background */
    lpos += snprintf(left + lpos, sizeof(left) - lpos,
                     "\x1b[38;5;%d;48;5;%dm", app.sb_fg, app.sb_bg);

    /* hostname */
    if (app.show_hostname) {
        char hostname[64] = "";
        gethostname(hostname, sizeof(hostname));
        lpos += snprintf(left + lpos, sizeof(left) - lpos,
                        "\x1b[38;5;%dm %s \x1b[38;5;%dm|",
                        app.hostname_fg, hostname, app.separator_fg);
    }

    /* clock (left position) */
    char clock_str[32] = "";
    if (app.show_clock) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        if (strcmp(app.clock_format, "HH:MM:SS") == 0)
            snprintf(clock_str, sizeof(clock_str), "%02d:%02d:%02d",
                     st.wHour, st.wMinute, st.wSecond);
        else
            snprintf(clock_str, sizeof(clock_str), "%02d:%02d",
                     st.wHour, st.wMinute);
        if (strcmp(app.clock_position, "left") == 0) {
            lpos += snprintf(left + lpos, sizeof(left) - lpos,
                            "\x1b[38;5;%dm | \x1b[38;5;%dm%s ",
                            app.separator_fg, app.clock_fg, clock_str);
        }
    }

    /* window list */
    int act_id = app.active_id;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!app.windows[i]) continue;
        bool active = (i == act_id);
        uint8_t br_fg = active ? app.active_bracket_fg : app.inactive_bracket_fg;
        uint8_t br_bg = active ? app.active_bracket_bg : app.inactive_bracket_bg;
        uint8_t w_fg  = active ? app.active_window_fg  : app.inactive_window_fg;
        uint8_t w_bg  = active ? app.active_window_bg  : app.inactive_window_bg;

        char lb = '[', rb = ']';
        if (strcmp(app.window_brackets, "parens") == 0) { lb = '('; rb = ')'; }
        else if (strcmp(app.window_brackets, "angles") == 0) { lb = '<'; rb = '>'; }
        else if (strcmp(app.window_brackets, "braces") == 0) { lb = '{'; rb = '}'; }

        lpos += snprintf(left + lpos, sizeof(left) - lpos,
                        "\x1b[38;5;%d;48;5;%dm%c"
                        "\x1b[38;5;%d;48;5;%dm%s %d"
                        "\x1b[38;5;%d;48;5;%dm%c ",
                        br_fg, br_bg, lb,
                        w_fg, w_bg, app.windows[i]->name, i,
                        br_fg, br_bg, rb);
    }

    /* state label */
    if (app.state_label[0]) {
        lpos += snprintf(left + lpos, sizeof(left) - lpos,
                        "\x1b[38;5;%d;48;5;%dm [%s]\x1b[38;5;%d;48;5;%dm",
                        app.rename_fg, app.rename_bg, app.state_label,
                        app.sb_fg, app.sb_bg);
    }

    /* ---- right part ---- */
    char right[512] = "";
    int rpos = 0;

    if (app.show_clock && strcmp(app.clock_position, "right") == 0) {
        rpos += snprintf(right + rpos, sizeof(right) - rpos,
                        "\x1b[38;5;%dm | \x1b[38;5;%dm%s ",
                        app.separator_fg, app.clock_fg, clock_str);
    }

    /* calculate padding */
    int l_vis = visible_len(left);
    int r_vis = visible_len(right);
    int pad = cols - l_vis - r_vis;
    if (pad < 1) pad = 1;

    /* assemble final string */
    pos += snprintf(buf + pos, buf_size - pos, "%s", left);
    /* padding with background */
    for (int i = 0; i < pad && pos < buf_size - 10; i++) {
        buf[pos++] = ' ';
    }
    if (pos < buf_size - 10) {
        pos += snprintf(buf + pos, buf_size - pos, "%s", right);
    }
    pos += snprintf(buf + pos, buf_size - pos, "\x1b[0m");

    return pos;
}

/* ----- server-side: status bar with cursor positioning ----- */

int render_status_bar_to_string(char *buf, int buf_size, bool clock_update)
{
    int pos = 0;
    VtWindow *win = app.windows[app.active_id];
    int usable = app.terminal_rows - 1;
    if (usable < 1) usable = 23;
    int sb_row = (strcmp(app.sb_position, "top") == 0) ? 0 : usable;

    char sb[4096];
    int slen = render_status_bar_content(sb, sizeof(sb), clock_update);
    if (slen > 0) {
        int n = snprintf(buf + pos, buf_size - pos,
                        "\x1b[%d;1H%.*s", sb_row + 1, slen, sb);
        if (n > 0) pos += n;
    }

    if (pos < buf_size) buf[pos] = '\0';
    (void)win;
    return pos;
}

/* ----- server-side: render dirty content lines with cursor positioning ----- */

int render_content_to_string(char *buf, int buf_size, bool full_redraw)
{
    int pos = 0;
    VtWindow *win = app.windows[app.active_id];
    int cols = app.terminal_cols;
    int usable = app.terminal_rows - 1;
    if (cols < 10) cols = 80;
    if (usable < 1) usable = 23;

    if (win && win->is_alive) {
        EnterCriticalSection(&win->lock);
        TerminalBuffer *tb = win->buffer;
        if (tb) {
            if (tb->scroll_pos > 0 && tb->scrollback_len > 0) {
                int off = tb->scroll_pos;
                if (off > tb->scrollback_len) off = tb->scrollback_len;
                for (int y = 0; y < usable; y++) {
                    char line[4096];
                    int llen = 0;
                    if (y < off) {
                        int si = tb->scrollback_len - off + y;
                        if (si >= 0 && si < tb->scrollback_len)
                            llen = render_cells_to_string(line, sizeof(line),
                                                          tb->scrollback[si], tb->cols);
                    } else {
                        int gy = y - off;
                        if (gy < tb->rows)
                            llen = render_line_to_string(line, sizeof(line), tb, gy);
                    }
                    if (llen == 0) continue;
                    int n;
                    if (y == 0)
                        n = snprintf(buf + pos, buf_size - pos,
                                     "\x1b[1;1H%.*s", llen, line);
                    else
                        n = snprintf(buf + pos, buf_size - pos,
                                     "\r\n%.*s", llen, line);
                    if (n > 0) pos += n;
                    if (pos >= buf_size) break;
                }
                tb->all_dirty = false;
            } else if (full_redraw) {
                for (int y = 0; y < usable && y < tb->rows; y++) {
                    char line[4096];
                    int llen = render_line_to_string(line, sizeof(line), tb, y);
                    if (llen == 0) continue;
                    int n;
                    if (y == 0)
                        n = snprintf(buf + pos, buf_size - pos,
                                     "\x1b[1;1H%.*s", llen, line);
                    else
                        n = snprintf(buf + pos, buf_size - pos,
                                     "\r\n%.*s", llen, line);
                    if (n > 0) pos += n;
                    if (pos >= buf_size) break;
                    tb->row_dirty[y] = false;
                }
                tb->all_dirty = false;
            } else {
                for (int y = 0; y < usable && y < tb->rows; y++) {
                    if (!tb->row_dirty[y]) continue;
                    char line[4096];
                    int llen = render_line_to_string(line, sizeof(line), tb, y);
                    if (llen == 0) continue;
                    int n = snprintf(buf + pos, buf_size - pos,
                                     "\x1b[%d;1H%.*s", y + 1, llen, line);
                    if (n > 0) pos += n;
                    if (pos >= buf_size) break;
                    tb->row_dirty[y] = false;
                }
                tb->all_dirty = false;
            }

            /* position cursor at shell cursor (when in normal view) */
            if (tb->scroll_pos == 0) {
                int n = snprintf(buf + pos, buf_size - pos,
                                "\x1b[%d;%dH",
                                tb->cursor_y + 1, tb->cursor_x + 1);
                if (n > 0) pos += n;
            }
        }
        LeaveCriticalSection(&win->lock);
    }

    if (pos < buf_size) buf[pos] = '\0';
    return pos;
}
