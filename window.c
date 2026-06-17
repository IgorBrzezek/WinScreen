#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "window.h"

AppState app;

/* ---------- TerminalBuffer ---------- */

TerminalBuffer *tb_create(int cols, int rows)
{
    TerminalBuffer *tb = (TerminalBuffer *)malloc(sizeof(TerminalBuffer));
    if (!tb) return NULL;

    tb->cols = cols;
    tb->rows = rows;
    tb->cursor_x = 0;
    tb->cursor_y = 0;
    tb->save_cursor_x = 0;
    tb->save_cursor_y = 0;
    tb->state = STATE_GROUND;
    tb->num_params = 0;
    tb->params[0] = 0;
    tb->current_fg = 7;
    tb->current_bg = 0;
    tb->current_bold = false;
    tb->utf8_remaining = 0;
    tb->utf8_partial = 0;

    tb->grid = (Cell **)malloc((size_t)rows * sizeof(Cell *));
    if (!tb->grid) { free(tb); return NULL; }
    tb->row_dirty = (bool *)calloc((size_t)rows, sizeof(bool));
    if (!tb->row_dirty) { free(tb->grid); free(tb); return NULL; }
    tb->all_dirty = true;
    tb->scrollback_max = 1024;
    tb->scrollback_len = 0;
    tb->scroll_pos = 0;
    tb->scrollback = (Cell **)calloc((size_t)tb->scrollback_max, sizeof(Cell *));
    if (!tb->scrollback) { free(tb->grid); free(tb->row_dirty); free(tb); return NULL; }
    for (int y = 0; y < rows; y++) {
        tb->grid[y] = (Cell *)calloc((size_t)cols, sizeof(Cell));
        if (!tb->grid[y]) {
            for (int i = 0; i < y; i++) free(tb->grid[i]);
            free(tb->grid); free(tb->row_dirty); free(tb); return NULL;
        }
        for (int x = 0; x < cols; x++) {
            tb->grid[y][x].ch = L' ';
            tb->grid[y][x].fg_color = 7;
            tb->grid[y][x].bg_color = 0;
        }
    }
    return tb;
}

void tb_free(TerminalBuffer *tb)
{
    if (!tb) return;
    if (tb->grid) {
        for (int y = 0; y < tb->rows; y++)
            if (tb->grid[y]) free(tb->grid[y]);
        free(tb->grid);
    }
    free(tb->row_dirty);
    if (tb->scrollback) {
        for (int i = 0; i < tb->scrollback_len; i++)
            if (tb->scrollback[i]) free(tb->scrollback[i]);
        free(tb->scrollback);
    }
    free(tb);
}

void tb_clear(TerminalBuffer *tb)
{
    for (int y = 0; y < tb->rows; y++) {
        tb->row_dirty[y] = true;
        for (int x = 0; x < tb->cols; x++) {
            tb->grid[y][x].ch = L' ';
            tb->grid[y][x].fg_color = tb->current_fg;
            tb->grid[y][x].bg_color = tb->current_bg;
            tb->grid[y][x].bold = tb->current_bold;
            tb->grid[y][x].underline = false;
            tb->grid[y][x].reverse = false;
        }
    }
    for (int i = 0; i < tb->scrollback_len; i++) {
        free(tb->scrollback[i]);
        tb->scrollback[i] = NULL;
    }
    tb->scrollback_len = 0;
    tb->scroll_pos = 0;
    tb->all_dirty = true;
    tb->cursor_x = 0;
    tb->cursor_y = 0;
}

void tb_resize(TerminalBuffer *tb, int cols, int rows)
{
    if (cols == tb->cols && rows == tb->rows) return;

    Cell **new_grid = (Cell **)malloc((size_t)rows * sizeof(Cell *));
    if (!new_grid) return;
    bool *new_dirty = (bool *)calloc((size_t)rows, sizeof(bool));
    if (!new_dirty) { free(new_grid); return; }
    for (int y = 0; y < rows; y++) {
        new_grid[y] = (Cell *)calloc((size_t)cols, sizeof(Cell));
        if (!new_grid[y]) {
            for (int i = 0; i < y; i++) free(new_grid[i]);
            free(new_grid); free(new_dirty); return;
        }
        for (int x = 0; x < cols; x++) {
            new_grid[y][x].ch = L' ';
            new_grid[y][x].fg_color = 7;
            new_grid[y][x].bg_color = 0;
        }
    }

    int copy_rows = rows < tb->rows ? rows : tb->rows;
    int copy_cols = cols < tb->cols ? cols : tb->cols;
    for (int y = 0; y < copy_rows; y++) {
        for (int x = 0; x < copy_cols; x++) {
            new_grid[y][x] = tb->grid[y][x];
        }
    }

    for (int y = 0; y < tb->rows; y++) free(tb->grid[y]);
    free(tb->grid);
    free(tb->row_dirty);
    for (int i = 0; i < tb->scrollback_len; i++) {
        free(tb->scrollback[i]);
        tb->scrollback[i] = NULL;
    }
    tb->scrollback_len = 0;
    tb->scroll_pos = 0;
    tb->grid = new_grid;
    tb->row_dirty = new_dirty;
    tb->all_dirty = true;
    tb->cols = cols;
    tb->rows = rows;
    if (tb->cursor_x >= cols) tb->cursor_x = cols - 1;
    if (tb->cursor_y >= rows) tb->cursor_y = rows - 1;
}

void tb_put_char(TerminalBuffer *tb, wchar_t ch)
{
    if (ch == L'\0') return;
    if (tb->cursor_x >= tb->cols) tb->cursor_x = 0;
    if (tb->cursor_y >= tb->rows) tb->cursor_y = tb->rows - 1;

    Cell *c = &tb->grid[tb->cursor_y][tb->cursor_x];
    c->ch = ch;
    c->fg_color = tb->current_fg;
    c->bg_color = tb->current_bg;
    c->bold = tb->current_bold;
    c->underline = false;
    c->reverse = false;
    tb->row_dirty[tb->cursor_y] = true;

    tb->cursor_x++;
    if (tb->cursor_x >= tb->cols) {
        tb->cursor_x = 0;
        cursor_down_or_scroll(tb);
    }
}

void cursor_down_or_scroll(TerminalBuffer *tb)
{
    if (tb->cursor_y < tb->rows - 1) {
        tb->cursor_y++;
    } else {
        Cell *saved = (Cell *)malloc((size_t)tb->cols * sizeof(Cell));
        if (saved) {
            memcpy(saved, tb->grid[0], (size_t)tb->cols * sizeof(Cell));
            if (tb->scrollback_len < tb->scrollback_max) {
                tb->scrollback[tb->scrollback_len++] = saved;
            } else {
                free(tb->scrollback[0]);
                memmove(tb->scrollback, tb->scrollback + 1,
                        (size_t)(tb->scrollback_max - 1) * sizeof(Cell *));
                tb->scrollback[tb->scrollback_max - 1] = saved;
            }
        }
        for (int y = 0; y < tb->rows - 1; y++) {
            memcpy(&tb->grid[y][0], &tb->grid[y + 1][0],
                   (size_t)tb->cols * sizeof(Cell));
            tb->row_dirty[y] = true;
        }
        for (int x = 0; x < tb->cols; x++) {
            tb->grid[tb->rows - 1][x].ch = L' ';
            tb->grid[tb->rows - 1][x].fg_color = tb->current_fg;
            tb->grid[tb->rows - 1][x].bg_color = tb->current_bg;
            tb->grid[tb->rows - 1][x].bold = tb->current_bold;
            tb->grid[tb->rows - 1][x].underline = false;
            tb->grid[tb->rows - 1][x].reverse = false;
        }
        tb->row_dirty[tb->rows - 1] = true;
    }
}

/* ---------- VT100 DFA ---------- */

void parse_byte(TerminalBuffer *tb, uint8_t b)
{
    switch (tb->state) {
    case STATE_GROUND:
        if (tb->utf8_remaining > 0) {
            if ((b & 0xC0) == 0x80) {
                tb->utf8_partial = (tb->utf8_partial << 6) | (b & 0x3F);
                tb->utf8_remaining--;
                if (tb->utf8_remaining == 0) {
                    tb_put_char(tb, tb->utf8_partial);
                }
            } else {
                tb->utf8_remaining = 0;
            }
        } else if (b == '\x1B') {
            tb->state = STATE_ESC;
        } else if (b == '\r') {
            tb->cursor_x = 0;
        } else if (b == '\n') {
            tb->cursor_x = 0;
            cursor_down_or_scroll(tb);
        } else if (b == '\b') {
            if (tb->cursor_x > 0) tb->cursor_x--;
        } else if (b == '\t') {
            int stop = (tb->cursor_x / 8 + 1) * 8;
            if (stop >= tb->cols) stop = tb->cols - 1;
            tb->cursor_x = stop;
        } else if (b < 0x20) {
            /* other control character - ignore */
        } else if (b < 0x80) {
            tb_put_char(tb, (wchar_t)b);
        } else if (b < 0xC0) {
            /* unexpected continuation byte - ignore */
        } else if (b < 0xE0) {
            tb->utf8_partial = (wchar_t)(b & 0x1F);
            tb->utf8_remaining = 1;
        } else if (b < 0xF0) {
            tb->utf8_partial = (wchar_t)(b & 0x0F);
            tb->utf8_remaining = 2;
        } else if (b < 0xF8) {
            tb->utf8_partial = (wchar_t)(b & 0x07);
            tb->utf8_remaining = 3;
        }
        break;

    case STATE_ESC:
        if (b == '[') {
            tb->state = STATE_CSI_ENTRY;
            tb->num_params = 0;
            tb->params[0] = 0;
        } else if (b == ']') {
            /* OSC - consume until BEL or ST */
            tb->state = STATE_OSC;
        } else {
            tb->state = STATE_GROUND;
        }
        break;

    case STATE_CSI_ENTRY:
    case STATE_CSI_PARAM:
        if (b >= '0' && b <= '9') {
            tb->state = STATE_CSI_PARAM;
            int idx = tb->num_params;
            tb->params[idx] = (tb->params[idx] * 10) + (b - '0');
        } else if (b == ';') {
            tb->state = STATE_CSI_PARAM;
            tb->num_params++;
            if (tb->num_params >= 16) tb->num_params = 15;
            tb->params[tb->num_params] = 0;
        } else if (b >= 0x40 && b <= 0x7E) {
            tb->num_params++;
            execute_csi_command(tb, b);
            tb->state = STATE_GROUND;
        } else if (b >= 0x3C && b <= 0x3F) {
            /* private parameter prefix (<=>?) - skip and stay in CSI_PARAM */
        } else {
            tb->state = STATE_GROUND;
        }
        break;

    case STATE_OSC:
        /* consume everything until BEL (0x07) or ST (ESC \) */
        if (b == '\x07') {
            tb->state = STATE_GROUND;
        } else if (b == '\x1B') {
            tb->state = STATE_ESC; /* let ESC state handle ST (backslash) */
        }
        break;
    }
}

void execute_csi_command(TerminalBuffer *tb, uint8_t cmd)
{
    int n = tb->params[0];
    if (n == 0) n = 1;

    switch (cmd) {
    case 'H':
    case 'f': {
        int y = tb->num_params >= 1 ? tb->params[0] : 1;
        int x = tb->num_params >= 2 ? tb->params[1] : 1;
        if (y < 1) y = 1;
        if (x < 1) x = 1;
        tb->cursor_y = y - 1;
        tb->cursor_x = x - 1;
        if (tb->cursor_y >= tb->rows) tb->cursor_y = tb->rows - 1;
        if (tb->cursor_x >= tb->cols) tb->cursor_x = tb->cols - 1;
        break;
    }

    case 'A': {
        int count = n;
        tb->cursor_y -= count;
        if (tb->cursor_y < 0) tb->cursor_y = 0;
        break;
    }

    case 'B': {
        int count = n;
        tb->cursor_y += count;
        if (tb->cursor_y >= tb->rows) tb->cursor_y = tb->rows - 1;
        break;
    }

    case 'C': {
        int count = n;
        tb->cursor_x += count;
        if (tb->cursor_x >= tb->cols) tb->cursor_x = tb->cols - 1;
        break;
    }

    case 'D': {
        int count = n;
        tb->cursor_x -= count;
        if (tb->cursor_x < 0) tb->cursor_x = 0;
        break;
    }

    case 'J':
        if (tb->params[0] == 2 || tb->params[0] == 3) {
            tb_clear(tb);
        } else if (tb->params[0] == 0) {
            for (int x = tb->cursor_x; x < tb->cols; x++) {
                tb->grid[tb->cursor_y][x].ch = L' ';
                tb->grid[tb->cursor_y][x].fg_color = tb->current_fg;
                tb->grid[tb->cursor_y][x].bg_color = tb->current_bg;
            }
            tb->row_dirty[tb->cursor_y] = true;
        } else if (tb->params[0] == 1) {
            for (int x = 0; x <= tb->cursor_x; x++) {
                tb->grid[tb->cursor_y][x].ch = L' ';
                tb->grid[tb->cursor_y][x].fg_color = tb->current_fg;
                tb->grid[tb->cursor_y][x].bg_color = tb->current_bg;
            }
            tb->row_dirty[tb->cursor_y] = true;
        }
        break;

    case 'K':
        if (tb->params[0] == 0) {
            for (int x = tb->cursor_x; x < tb->cols; x++) {
                tb->grid[tb->cursor_y][x].ch = L' ';
                tb->grid[tb->cursor_y][x].fg_color = tb->current_fg;
                tb->grid[tb->cursor_y][x].bg_color = tb->current_bg;
            }
            tb->row_dirty[tb->cursor_y] = true;
        } else if (tb->params[0] == 1) {
            for (int x = 0; x <= tb->cursor_x; x++) {
                tb->grid[tb->cursor_y][x].ch = L' ';
                tb->grid[tb->cursor_y][x].fg_color = tb->current_fg;
                tb->grid[tb->cursor_y][x].bg_color = tb->current_bg;
            }
            tb->row_dirty[tb->cursor_y] = true;
        } else if (tb->params[0] == 2) {
            for (int x = 0; x < tb->cols; x++) {
                tb->grid[tb->cursor_y][x].ch = L' ';
                tb->grid[tb->cursor_y][x].fg_color = tb->current_fg;
                tb->grid[tb->cursor_y][x].bg_color = tb->current_bg;
            }
            tb->row_dirty[tb->cursor_y] = true;
        }
        break;

    case 'm': {
        int i = 0;
        while (i < tb->num_params) {
            int p = tb->params[i];
            if (p == 0) {
                tb->current_fg = 7;
                tb->current_bg = 0;
                tb->current_bold = false;
            } else if (p == 1) {
                tb->current_bold = true;
            } else if (p == 22) {
                tb->current_bold = false;
            } else if (p >= 30 && p <= 37) {
                tb->current_fg = (uint8_t)(p - 30);
            } else if (p >= 40 && p <= 47) {
                tb->current_bg = (uint8_t)(p - 40);
            } else if (p >= 90 && p <= 97) {
                tb->current_fg = (uint8_t)(p - 90 + 8);
            } else if (p >= 100 && p <= 107) {
                tb->current_bg = (uint8_t)(p - 100 + 8);
            } else if (p == 38) {
                if (i + 2 < tb->num_params && tb->params[i + 1] == 5) {
                    tb->current_fg = (uint8_t)tb->params[i + 2];
                    i += 2;
                }
            } else if (p == 48) {
                if (i + 2 < tb->num_params && tb->params[i + 1] == 5) {
                    tb->current_bg = (uint8_t)tb->params[i + 2];
                    i += 2;
                }
            }
            i++;
        }
        break;
    }

    case 's':
        tb->save_cursor_x = tb->cursor_x;
        tb->save_cursor_y = tb->cursor_y;
        break;

    case 'u':
        tb->cursor_x = tb->save_cursor_x;
        tb->cursor_y = tb->save_cursor_y;
        break;

    default:
        break;
    }
}

/* ---------- VtWindow ---------- */

VtWindow *window_create(int id, const wchar_t *shell)
{
    VtWindow *win = (VtWindow *)calloc(1, sizeof(VtWindow));
    if (!win) return NULL;

    InitializeCriticalSection(&win->lock);

    win->id = id;
    snprintf(win->name, sizeof(win->name), "win%d", id);
    win->is_alive = true;
    win->is_dirty = true;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hOutRead, hOutWrite, hInRead, hInWrite;
    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) goto fail;
    if (!CreatePipe(&hInRead, &hInWrite, &sa, 0)) {
        CloseHandle(hOutRead); CloseHandle(hOutWrite);
        goto fail;
    }

    COORD size;
    size.X = (short)app.terminal_cols;
    size.Y = (short)(app.terminal_rows - 1);

    HRESULT hr = CreatePseudoConsole(size, hInRead, hOutWrite, 0, &win->hPC);
    if (FAILED(hr)) {
        CloseHandle(hOutRead); CloseHandle(hOutWrite);
        CloseHandle(hInRead); CloseHandle(hInWrite);
        goto fail;
    }

    CloseHandle(hOutWrite);
    CloseHandle(hInRead);

    win->hOutPipeRead = hOutRead;
    win->hInPipeWrite = hInWrite;

    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrListSize);
    STARTUPINFOEXW siex;
    memset(&siex, 0, sizeof(siex));
    siex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    siex.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attrListSize);
    if (!siex.lpAttributeList) goto fail;
    InitializeProcThreadAttributeList(siex.lpAttributeList, 1, 0, &attrListSize);
    UpdateProcThreadAttribute(siex.lpAttributeList, 0,
                              PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                              win->hPC, sizeof(HPCON), NULL, NULL);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL proc_ok = CreateProcessW(NULL, (LPWSTR)shell, NULL, NULL, FALSE,
                                  EXTENDED_STARTUPINFO_PRESENT,
                                  NULL, NULL, &siex.StartupInfo, &pi);

    free(siex.lpAttributeList);

    if (!proc_ok) goto fail;

    win->hProcess = pi.hProcess;
    win->hThread = pi.hThread;

    win->buffer = tb_create(app.terminal_cols, app.terminal_rows - 1);
    if (!win->buffer) goto fail;

    win->hReaderThread = CreateThread(NULL, 0, WindowReaderThreadProc, win, 0, NULL);
    if (!win->hReaderThread) goto fail;

    return win;

fail:
    window_destroy(win);
    return NULL;
}

void window_destroy(VtWindow *win)
{
    if (!win) return;

    if (win->hReaderThread) {
        TerminateThread(win->hReaderThread, 0);
        CloseHandle(win->hReaderThread);
    }

    if (win->hProcess) {
        TerminateProcess(win->hProcess, 0);
        CloseHandle(win->hProcess);
    }
    if (win->hThread) CloseHandle(win->hThread);
    if (win->hPC) ClosePseudoConsole(win->hPC);
    if (win->hOutPipeRead) CloseHandle(win->hOutPipeRead);
    if (win->hInPipeWrite) CloseHandle(win->hInPipeWrite);
    if (win->buffer) tb_free(win->buffer);
    DeleteCriticalSection(&win->lock);
    free(win);
}

void window_write_input(VtWindow *win, const char *data, DWORD len)
{
    if (!win || !win->is_alive) return;
    if (win->buffer) win->buffer->scroll_pos = 0;
    DWORD written;
    WriteFile(win->hInPipeWrite, data, len, &written, NULL);
}

void window_resize(VtWindow *win, int cols, int rows)
{
    if (!win || !win->is_alive) return;

    EnterCriticalSection(&win->lock);

    COORD size;
    size.X = (short)cols;
    size.Y = (short)(rows - 1);

    ResizePseudoConsole(win->hPC, size);
    tb_resize(win->buffer, cols, rows - 1);

    LeaveCriticalSection(&win->lock);
    win->is_dirty = true;
}

/* ---------- Reader Thread ---------- */

static void decode_utf8_and_parse(TerminalBuffer *tb, const uint8_t *buf, DWORD len)
{
    tb->scroll_pos = 0;
    for (DWORD i = 0; i < len; i++) {
        parse_byte(tb, buf[i]);
    }
}

DWORD WINAPI WindowReaderThreadProc(LPVOID lpParam)
{
    VtWindow *win = (VtWindow *)lpParam;
    uint8_t buffer[65536];
    DWORD bytes_read;

    while (1) {
        BOOL ok = ReadFile(win->hOutPipeRead, buffer, sizeof(buffer) - 1,
                           &bytes_read, NULL);
        if (!ok || bytes_read == 0) break;

        EnterCriticalSection(&win->lock);
        if (win->buffer) {
            decode_utf8_and_parse(win->buffer, buffer, bytes_read);
            win->is_dirty = true;
        }
        LeaveCriticalSection(&win->lock);
    }

    win->is_alive = false;
    return 0;
}
