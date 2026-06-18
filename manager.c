#define _WIN32_WINNT 0x0A00
#include "manager.h"
#include "window.h"
#include "session.h"
#include "lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- overlay box helper (DOS-style double border + color) ---- */

static int build_overlay_box(char *buf, int buf_size,
                              const char *title,
                              const char *body[], int body_count)
{
    int cols = app.terminal_cols;
    int rows = app.terminal_rows;
    if (cols < 20) cols = 80;
    if (rows < 10) rows = 24;

    const char *footer = tr("footer_key", "  [Press any key]  ");
    int flen = (int)strlen(footer);
    int tlen = (int)strlen(title);

    int max_w = flen;
    if (tlen + 4 > max_w) max_w = tlen + 4;
    for (int i = 0; i < body_count; i++) {
        int l = (int)strlen(body[i]);
        if (l + 2 > max_w) max_w = l + 2;
    }

    int inner = max_w + 2;
    int box_w = inner + 2;
    if (box_w > cols) { box_w = cols; inner = box_w - 2; }
    if (box_w < 10) { box_w = 10; inner = 8; }

    int box_h = body_count + 4;
    if (box_h > rows) box_h = rows;

    int sx = (cols - box_w) / 2 + 1;
    int sy = (rows - box_h) / 2 + 1;

    int pos = 0;
    char ln[4096];
    int p;

    /* ---- top: ╔══ title ══╗ ---- */
    /* ╔=E2 95 94  ═=E2 95 90  ╗=E2 95 97 */
    p = 0;
    p += snprintf(ln + p, sizeof(ln) - p, "\x1b[48;5;236m\x1b[1;36m"); /* grey bg, cyan border */
    ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x94;            /* ╔ */
    ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x90;            /* ═ */
    ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x90;            /* ═ */
    p += snprintf(ln + p, sizeof(ln) - p, "\x1b[1;33m%s\x1b[1;36m", title); /* bold yellow title */
    for (int j = 0; j < inner - 2 - tlen; j++) {
        ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x90;        /* ═ */
    }
    ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x97;            /* ╗ */
    p += snprintf(ln + p, sizeof(ln) - p, "\x1b[0m");
    ln[p] = '\0';
    pos += snprintf(buf + pos, buf_size - pos, "\x1b[%d;%dH%s", sy, sx, ln);

    /* ---- blank separator: ║  ...  ║ ---- */
    p = 0;
    p += snprintf(ln + p, sizeof(ln) - p, "\x1b[48;5;236m\x1b[1;36m");
    ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x91;            /* ║ */
    for (int j = 0; j < inner; j++) ln[p++] = ' ';
    ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x91;            /* ║ */
    p += snprintf(ln + p, sizeof(ln) - p, "\x1b[0m");
    ln[p] = '\0';
    pos += snprintf(buf + pos, buf_size - pos, "\x1b[%d;%dH%s", sy + 1, sx, ln);

    /* ---- body lines ---- */
    for (int b = 0; b < body_count; b++) {
        int blen = (int)strlen(body[b]);
        p = 0;
        p += snprintf(ln + p, sizeof(ln) - p, "\x1b[48;5;236m\x1b[1;36m"); /* bg grey */
        ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x91;        /* ║ */
        p += snprintf(ln + p, sizeof(ln) - p, "\x1b[37m");      /* white fg, keep grey bg */
        ln[p++] = ' ';
        memcpy(ln + p, body[b], (size_t)blen); p += blen;
        for (int j = 0; j < inner - 1 - blen; j++) ln[p++] = ' ';
        p += snprintf(ln + p, sizeof(ln) - p, "\x1b[1;36m");
        ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x91;        /* ║ */
        p += snprintf(ln + p, sizeof(ln) - p, "\x1b[0m");
        ln[p] = '\0';
        pos += snprintf(buf + pos, buf_size - pos, "\x1b[%d;%dH%s", sy + 2 + b, sx, ln);
    }

    /* ---- footer: centered [Press any key] ---- */
    {
        int pad_l = (inner - flen) / 2;
        p = 0;
        p += snprintf(ln + p, sizeof(ln) - p, "\x1b[48;5;236m\x1b[1;36m"); /* bg grey */
        ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x91;        /* ║ */
        p += snprintf(ln + p, sizeof(ln) - p, "\x1b[1;32m");   /* bold green footer */
        for (int j = 0; j < pad_l; j++) ln[p++] = ' ';
        memcpy(ln + p, footer, (size_t)flen); p += flen;
        for (int j = 0; j < inner - pad_l - flen; j++) ln[p++] = ' ';
        p += snprintf(ln + p, sizeof(ln) - p, "\x1b[1;36m");
        ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x91;        /* ║ */
        p += snprintf(ln + p, sizeof(ln) - p, "\x1b[0m");
        ln[p] = '\0';
        pos += snprintf(buf + pos, buf_size - pos, "\x1b[%d;%dH%s", sy + 2 + body_count, sx, ln);
    }

    /* ---- bottom: ╚══ ... ══╝ ---- */
    /* ╚=E2 95 9A  ╝=E2 95 9D */
    p = 0;
    p += snprintf(ln + p, sizeof(ln) - p, "\x1b[48;5;236m\x1b[1;36m");
    ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x9A;            /* ╚ */
    for (int j = 0; j < inner; j++) {
        ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x90;        /* ═ */
    }
    ln[p++] = 0xE2; ln[p++] = 0x95; ln[p++] = 0x9D;            /* ╝ */
    p += snprintf(ln + p, sizeof(ln) - p, "\x1b[0m");
    ln[p] = '\0';
    pos += snprintf(buf + pos, buf_size - pos, "\x1b[%d;%dH%s", sy + 2 + body_count + 1, sx, ln);

    if (pos < buf_size) buf[pos] = '\0';
    return pos;
}

/* ---- window list (overlay) ---- */

static void format_window_list(char *buf, int buf_size)
{
    const char *body[16];
    int count = 0;
    char lines[11][96];
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!app.windows[i]) continue;
        char marker = (i == app.active_id) ? '*' : ' ';
        snprintf(lines[count], sizeof(lines[count]),
                 " %d%c  %s", i, marker, app.windows[i]->name);
        body[count] = lines[count];
        count++;
    }
    if (count > 0) { lines[count][0] = '\0'; body[count] = lines[count]; count++; }
    build_overlay_box(buf, buf_size, tr("list_title", " WinScreen - Window List "), body, count);
}

/* ---- init / cleanup ---- */

void manager_init(void)
{
    memset(&app, 0, sizeof(AppState));

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    app.terminal_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    app.terminal_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    if (app.terminal_cols < 80) app.terminal_cols = 80;
    if (app.terminal_rows < 24) app.terminal_rows = 24;

    app.active_id = -1;
    app.last_active_id = -1;
    app.window_count = 0;
    app.prefix_key = 'A';
    app.sb_bg = 236;
    app.sb_fg = 15;
    app.input_state = INPUT_NORMAL;
    strcpy(app.shell_cmd, "cmd.exe");
    strcpy(app.default_name, "shell");
    strcpy(app.sb_position, "bottom");
    app.show_hostname = false;
    app.show_clock = false;
    strcpy(app.clock_format, "HH:MM");
    strcpy(app.clock_position, "right");
    strcpy(app.window_brackets, "brackets");
    strcpy(app.active_symbol, "*");
    app.active_bracket_fg = 11;
    app.active_bracket_bg = 236;
    app.inactive_bracket_fg = 15;
    app.inactive_bracket_bg = 236;
    app.active_window_fg = 15;
    app.active_window_bg = 236;
    app.inactive_window_fg = 244;
    app.inactive_window_bg = 236;
    app.hostname_fg = 11;
    app.clock_fg = 14;
    app.separator_fg = 240;
    app.rename_fg = 15;
    app.rename_bg = 236;
    app.main_background = -1;
    app.sb_dirty = true;
    app.session_name[0] = '\0';
    app.state_label[0] = '\0';

    for (int i = 0; i < MAX_WINDOWS; i++)
        app.windows[i] = NULL;
}

void manager_init_headless(int cols, int rows)
{
    memset(&app, 0, sizeof(AppState));

    app.terminal_cols = cols;
    app.terminal_rows = rows;
    if (app.terminal_cols < 80) app.terminal_cols = 80;
    if (app.terminal_rows < 24) app.terminal_rows = 24;

    app.active_id = -1;
    app.last_active_id = -1;
    app.window_count = 0;
    app.prefix_key = 'A';
    app.sb_bg = 236;
    app.sb_fg = 15;
    app.input_state = INPUT_NORMAL;
    strcpy(app.shell_cmd, "cmd.exe");
    strcpy(app.default_name, "shell");
    strcpy(app.sb_position, "bottom");
    app.show_hostname = false;
    app.show_clock = false;
    strcpy(app.clock_format, "HH:MM");
    strcpy(app.clock_position, "right");
    strcpy(app.window_brackets, "brackets");
    strcpy(app.active_symbol, "*");
    app.active_bracket_fg = 11;
    app.active_bracket_bg = 236;
    app.inactive_bracket_fg = 15;
    app.inactive_bracket_bg = 236;
    app.active_window_fg = 15;
    app.active_window_bg = 236;
    app.inactive_window_fg = 244;
    app.inactive_window_bg = 236;
    app.hostname_fg = 11;
    app.clock_fg = 14;
    app.separator_fg = 240;
    app.rename_fg = 15;
    app.rename_bg = 236;
    app.main_background = -1;
    app.sb_dirty = true;
    app.session_name[0] = '\0';
    app.state_label[0] = '\0';
    app.start_time = GetTickCount();
    app.detach_start_tick = GetTickCount();
    app.total_detach_ms = 0;

    for (int i = 0; i < MAX_WINDOWS; i++)
        app.windows[i] = NULL;
}

/* ---- window management ---- */

void manager_create_window(void)
{
    int id = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (app.windows[i] == NULL) { id = i; break; }
    }
    if (id < 0) return;

    wchar_t wcmd[260];
    MultiByteToWideChar(CP_UTF8, 0, app.shell_cmd, -1, wcmd, 260);

    VtWindow *win = window_create(id, wcmd);
    if (!win) return;

    snprintf(win->name, sizeof(win->name), "%s", app.default_name);

    app.windows[id] = win;
    app.last_active_id = app.active_id;
    app.active_id = id;
    app.window_count++;
    app.sb_dirty = true;
    manager_update_session_info();
}

void manager_next_window(void)
{
    if (app.window_count <= 1) return;
    int next = (app.active_id + 1) % MAX_WINDOWS;
    while (!app.windows[next]) next = (next + 1) % MAX_WINDOWS;
    app.last_active_id = app.active_id;
    app.active_id = next;
    app.sb_dirty = true;
}

void manager_prev_window(void)
{
    if (app.window_count <= 1) return;
    int prev = (app.active_id - 1 + MAX_WINDOWS) % MAX_WINDOWS;
    while (!app.windows[prev]) prev = (prev - 1 + MAX_WINDOWS) % MAX_WINDOWS;
    app.last_active_id = app.active_id;
    app.active_id = prev;
    app.sb_dirty = true;
}

void manager_switch_window(int id)
{
    if (id < 0 || id >= MAX_WINDOWS || !app.windows[id]) return;
    app.last_active_id = app.active_id;
    app.active_id = id;
    app.sb_dirty = true;
}

void manager_last_window(void)
{
    if (app.last_active_id < 0 || !app.windows[app.last_active_id]) return;
    int tmp = app.active_id;
    app.active_id = app.last_active_id;
    app.last_active_id = tmp;
    app.sb_dirty = true;
}

void manager_close_window(int id)
{
    if (id < 0 || id >= MAX_WINDOWS) return;
    if (!app.windows[id]) return;

    window_destroy(app.windows[id]);
    app.windows[id] = NULL;
    app.window_count--;
    app.sb_dirty = true;
    manager_update_session_info();

    if (app.window_count == 0) {
        app.active_id = -1;
        return;
    }

    if (app.active_id == id) {
        if (app.last_active_id >= 0 && app.windows[app.last_active_id]) {
            app.active_id = app.last_active_id;
        } else {
            int next = (id + 1) % MAX_WINDOWS;
            while (!app.windows[next]) next = (next + 1) % MAX_WINDOWS;
            app.active_id = next;
        }
    }
}

void manager_resize_all(int cols, int rows)
{
    app.terminal_cols = cols;
    app.terminal_rows = rows;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (app.windows[i] && app.windows[i]->is_alive) {
            window_resize(app.windows[i], cols, rows);
        }
    }
    app.sb_dirty = true;
}

void manager_rename_window(int id, const char *new_name)
{
    if (id < 0 || id >= MAX_WINDOWS || !app.windows[id]) return;
    if (!new_name || new_name[0] == '\0') return;
    size_t nlen = strlen(new_name);
    if (nlen >= sizeof(app.windows[id]->name))
        nlen = sizeof(app.windows[id]->name) - 1;
    memcpy(app.windows[id]->name, new_name, nlen);
    app.windows[id]->name[nlen] = '\0';
    app.sb_dirty = true;
}

void manager_update_session_info(void)
{
    if (app.session_name[0] == '\0') return;
    session_save_info(app.session_name, GetCurrentProcessId(),
                      app.server_port, app.window_count);
}

void manager_cleanup(void)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (app.windows[i]) {
            window_destroy(app.windows[i]);
            app.windows[i] = NULL;
        }
    }
}

/* ---- server-side action handler ---- */

bool handle_server_action(SOCKET conn, const char *data, int len)
{
    if (!data || len < 1) return false;

    int atype = (unsigned char)data[0];

    /* --- overlays (send CMD_SCR with text, return true) --- */

    if (atype == ACT_HELP) {
        char overlay[4096];
        const char *help_lines[] = {
            tr("help_new", "c   - new window"),
            tr("help_next_prev", "n/p - next/previous window"),
            tr("help_close", "k   - close window"),
            tr("help_help", "h   - this help"),
            tr("help_info", "i   - information"),
            tr("help_list", "w   - window list"),
            tr("help_detach", "d   - detach from session"),
            tr("help_rename", "a   - rename window"),
            tr("help_redraw", "r   - redraw screen"),
        };
        int hcount = sizeof(help_lines) / sizeof(help_lines[0]);
        int slen = build_overlay_box(overlay, sizeof(overlay),
                                      tr("help_title", " WinScreen - Keybindings "),
                                      help_lines, hcount);
        session_send_msg(conn, CMD_SCR, overlay, slen);
        return true;
    }

    if (atype == ACT_LIST_WINDOWS) {
        char overlay[4096];
        format_window_list(overlay, sizeof(overlay));
        session_send_msg(conn, CMD_SCR, overlay, (int)strlen(overlay));
        return true;
    }

    if (atype == ACT_INFO) {
        char overlay[4096];
        char lines[12][256];
        const char *body[12];
        int count = 0;

        const char *labels[10];
        char values[10][128];
        int n = 0;

        labels[n] = tr("info_session_label", "Session:");
        snprintf(values[n], sizeof(values[n]), "%s",
                 app.session_name[0] ? app.session_name : tr("info_none", "(none)"));
        n++;

        labels[n] = tr("info_pid_label", "PID:");
        snprintf(values[n], sizeof(values[n]), "%lu",
                 (unsigned long)GetCurrentProcessId());
        n++;

        labels[n] = tr("info_port_label", "Port:");
        snprintf(values[n], sizeof(values[n]), "%d",
                 app.server_port);
        n++;

        labels[n] = tr("info_windows_label", "Windows:");
        snprintf(values[n], sizeof(values[n]), "%d",
                 app.window_count);
        n++;

        labels[n] = tr("info_active_label", "Active:");
        {
            DWORD uptime_ms = GetTickCount() - app.start_time;
            DWORD detach_ms = app.total_detach_ms;
            if (app.detach_start_tick != 0)
                detach_ms += GetTickCount() - app.detach_start_tick;
            DWORD active_ms = uptime_ms > detach_ms ? uptime_ms - detach_ms : 0;
            int hrs = (int)(active_ms / 3600000);
            int min = (int)((active_ms % 3600000) / 60000);
            int sec = (int)((active_ms % 60000) / 1000);
            snprintf(values[n], sizeof(values[n]), "%02d:%02d:%02d", hrs, min, sec);
        }
        n++;

        labels[n] = tr("info_detach_label", "Detach:");
        {
            DWORD detach_ms = app.total_detach_ms;
            if (app.detach_start_tick != 0)
                detach_ms += GetTickCount() - app.detach_start_tick;
            int hrs = (int)(detach_ms / 3600000);
            int min = (int)((detach_ms % 3600000) / 60000);
            int sec = (int)((detach_ms % 60000) / 1000);
            snprintf(values[n], sizeof(values[n]), "%02d:%02d:%02d", hrs, min, sec);
        }
        n++;

        labels[n] = tr("info_total_label", "Total:");
        {
            DWORD uptime_ms = GetTickCount() - app.start_time;
            int hrs = (int)(uptime_ms / 3600000);
            int min = (int)((uptime_ms % 3600000) / 60000);
            int sec = (int)((uptime_ms % 60000) / 1000);
            snprintf(values[n], sizeof(values[n]), "%02d:%02d:%02d", hrs, min, sec);
        }
        n++;

        labels[n] = tr("info_author_label", "Author:");
        snprintf(values[n], sizeof(values[n]), "%s", AUTHOR);
        n++;

        labels[n] = tr("info_version_label", "Version:");
        snprintf(values[n], sizeof(values[n]), "%s", VERSION);
        n++;

        labels[n] = tr("info_github_label", "GitHub:");
        snprintf(values[n], sizeof(values[n]), "%s", GITHUB);
        n++;

        int max_w = 0;
        for (int i = 0; i < n; i++) {
            int l = (int)strlen(labels[i]);
            if (l > max_w) max_w = l;
        }
        int data_col = max_w + 4;

        for (int i = 0; i < 7; i++) {
            snprintf(lines[count], sizeof(lines[count]),
                     "%-*s %s", data_col, labels[i], values[i]);
            body[count] = lines[count]; count++;
        }

        lines[count][0] = '\0'; body[count] = lines[count]; count++;

        for (int i = 7; i < n; i++) {
            snprintf(lines[count], sizeof(lines[count]),
                     "%-*s %s", data_col, labels[i], values[i]);
            body[count] = lines[count]; count++;
        }

        lines[count][0] = '\0'; body[count] = lines[count]; count++;

        int slen = build_overlay_box(overlay, sizeof(overlay),
                                      tr("info_title", " WinScreen - Info "), body, count);
        session_send_msg(conn, CMD_SCR, overlay, slen);
        return true;
    }

    /* --- resize --- */

    if (atype == ACT_RESIZE && len >= 5) {
        int cols = ((unsigned char)data[1] << 8) | (unsigned char)data[2];
        int rows = ((unsigned char)data[3] << 8) | (unsigned char)data[4];
        if (cols < 10) cols = 80;
        if (rows < 5) rows = 24;
        manager_resize_all(cols, rows);
        return false;
    }

    /* --- window management --- */

    if (atype == ACT_NEW_WINDOW) {
        manager_create_window();
    } else if (atype == ACT_NEXT_WINDOW) {
        manager_next_window();
    } else if (atype == ACT_PREV_WINDOW) {
        manager_prev_window();
    } else if (atype == ACT_LAST_WINDOW) {
        manager_last_window();
    } else if (atype == ACT_KILL_WINDOW) {
        manager_close_window(app.active_id);
    } else if (atype == ACT_SWITCH_WINDOW && len >= 2) {
        int id = (unsigned char)data[1];
        manager_switch_window(id);
    } else if (atype == ACT_RENAME_START) {
        snprintf(app.state_label, sizeof(app.state_label), "%s", tr("rename_label_init", "Rename: _"));
        app.sb_dirty = true;
    } else if (atype == ACT_RENAME_CHAR && len > 1) {
        char buf[64];
        int nlen = len - 1;
        if (nlen > 55) nlen = 55;
        memcpy(buf, data + 1, nlen);
        buf[nlen] = '\0';
        snprintf(app.state_label, sizeof(app.state_label),
                 tr("rename_label", "Rename: %s_"), buf);
        app.sb_dirty = true;
    } else if (atype == ACT_RENAME_CANCEL) {
        app.state_label[0] = '\0';
        app.sb_dirty = true;
    } else if (atype == ACT_RENAME_CONFIRM && len > 1) {
        char new_name[64];
        int nlen = len - 1;
        if (nlen > 63) nlen = 63;
        memcpy(new_name, data + 1, nlen);
        new_name[nlen] = '\0';
        manager_rename_window(app.active_id, new_name);
        app.state_label[0] = '\0';
        app.sb_dirty = true;
    } else if (atype == ACT_SCROLL_UP) {
        VtWindow *win = app.windows[app.active_id];
        if (win && win->buffer) {
            TerminalBuffer *tb = win->buffer;
            int count = (len >= 2) ? (unsigned char)data[1] : 1;
            if (count < 1) count = 1;
            EnterCriticalSection(&win->lock);
            if (tb->scroll_pos < tb->scrollback_len) {
                tb->scroll_pos += count;
                if (tb->scroll_pos > tb->scrollback_len)
                    tb->scroll_pos = tb->scrollback_len;
                tb->all_dirty = true;
            }
            LeaveCriticalSection(&win->lock);
        }
        return false;
    } else if (atype == ACT_SCROLL_DOWN) {
        VtWindow *win = app.windows[app.active_id];
        if (win && win->buffer) {
            TerminalBuffer *tb = win->buffer;
            int count = (len >= 2) ? (unsigned char)data[1] : 1;
            if (count < 1) count = 1;
            EnterCriticalSection(&win->lock);
            if (tb->scroll_pos > 0) {
                tb->scroll_pos -= count;
                if (tb->scroll_pos < 0)
                    tb->scroll_pos = 0;
                tb->all_dirty = true;
            }
            LeaveCriticalSection(&win->lock);
        }
        return false;
    }

    return false;
}
