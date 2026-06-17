#ifndef WINDOW_H
#define WINDOW_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#define STATE_GROUND     0
#define STATE_ESC        1
#define STATE_CSI_ENTRY  2
#define STATE_CSI_PARAM  3
#define STATE_OSC        4

#define INPUT_NORMAL 0
#define INPUT_PREFIX 1
#define INPUT_RENAME 2

#define MAX_WINDOWS 10

#define AUTHOR  "Igor Brzezek"
#define VERSION   "0.1"
#define GITHUB "https://github.com/IgorBrzezek"

#ifndef HPCON
typedef HANDLE HPCON;
#endif

#ifndef ProcThreadAttributeValue
#define ProcThreadAttributeValue(Number, Thread, Input, Additive) \
    (((Number) & 0xFFFF) | \
    ((Thread) ? 0x10000 : 0) | \
    ((Input) ? 0x20000 : 0) | \
    ((Additive) ? 0x40000 : 0))
#endif

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE \
    ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)
#endif

__declspec(dllimport) HRESULT WINAPI CreatePseudoConsole(COORD, HANDLE, HANDLE, DWORD, HPCON *);
__declspec(dllimport) HRESULT WINAPI ResizePseudoConsole(HPCON, COORD);
__declspec(dllimport) VOID WINAPI ClosePseudoConsole(HPCON);

typedef struct {
    wchar_t ch;
    uint8_t fg_color;
    uint8_t bg_color;
    bool    bold;
    bool    underline;
    bool    reverse;
} Cell;

typedef struct {
    int      cols;
    int      rows;
    int      cursor_x;
    int      cursor_y;
    int      save_cursor_x;
    int      save_cursor_y;
    Cell   **grid;

    int      state;
    int      params[16];
    int      num_params;

    uint8_t  current_fg;
    uint8_t  current_bg;
    bool     current_bold;

    int      utf8_remaining;
    wchar_t  utf8_partial;

    bool    *row_dirty;
    bool     all_dirty;

    Cell   **scrollback;
    int      scrollback_len;
    int      scrollback_max;
    int      scroll_pos;
} TerminalBuffer;

typedef struct {
    int      id;
    char     name[64];
    bool     is_alive;
    bool     is_dirty;

    HPCON    hPC;
    HANDLE   hProcess;
    HANDLE   hThread;

    HANDLE   hOutPipeRead;
    HANDLE   hInPipeWrite;

    HANDLE   hReaderThread;
    CRITICAL_SECTION lock;

    TerminalBuffer *buffer;
} VtWindow;

typedef struct {
    VtWindow *windows[MAX_WINDOWS];
    int       active_id;
    int       last_active_id;
    int       window_count;

    int       terminal_cols;
    int       terminal_rows;

    /* status bar config */
    uint8_t   sb_bg;
    uint8_t   sb_fg;
    char      sb_position[16];
    bool      show_hostname;
    bool      show_clock;
    char      clock_format[32];
    char      clock_position[16];
    char      window_brackets[16];
    char      active_symbol[8];

    /* window colors */
    uint8_t   active_bracket_fg, active_bracket_bg;
    uint8_t   inactive_bracket_fg, inactive_bracket_bg;
    uint8_t   active_window_fg, active_window_bg;
    uint8_t   inactive_window_fg, inactive_window_bg;

    /* misc colors */
    uint8_t   hostname_fg;
    uint8_t   clock_fg;
    uint8_t   separator_fg;
    uint8_t   rename_fg;
    uint8_t   rename_bg;

    /* shell */
    char shell_cmd[260];
    char default_name[64];

    /* keybinds */
    char      prefix_key;
    int       input_state;

    /* state flags */
    bool      sb_dirty;
    char      session_name[64];
    char      state_label[64];
    int       server_port;
    DWORD     start_time;

    int       main_background;
} AppState;

extern AppState app;

TerminalBuffer *tb_create(int cols, int rows);
void            tb_free(TerminalBuffer *tb);
void            tb_resize(TerminalBuffer *tb, int cols, int rows);
void            tb_clear(TerminalBuffer *tb);
void            tb_put_char(TerminalBuffer *tb, wchar_t ch);
void            parse_byte(TerminalBuffer *tb, uint8_t b);
void            execute_csi_command(TerminalBuffer *tb, uint8_t cmd);
void            cursor_down_or_scroll(TerminalBuffer *tb);

VtWindow       *window_create(int id, const wchar_t *shell);
void            window_destroy(VtWindow *win);
void            window_write_input(VtWindow *win, const char *data, DWORD len);
void            window_resize(VtWindow *win, int cols, int rows);
DWORD WINAPI    WindowReaderThreadProc(LPVOID lpParam);

#endif
