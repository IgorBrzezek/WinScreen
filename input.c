#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include "input.h"
#include "window.h"
#include "session.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HHOOK g_hMouseHook;
static volatile LONG g_mouse_wheel_scroll;

static LRESULT CALLBACK low_level_mouse_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT *info = (MSLLHOOKSTRUCT *)lParam;
        HWND hConsole = GetConsoleWindow();
        RECT rc;
        GetWindowRect(hConsole, &rc);
        if (PtInRect(&rc, info->pt)) {
            if (wParam == WM_RBUTTONDOWN) {
                CONSOLE_SELECTION_INFO sel;
                if (GetConsoleSelectionInfo(&sel) && sel.dwFlags != CONSOLE_NO_SELECTION)
                    return CallNextHookEx(NULL, nCode, wParam, lParam);
                if (OpenClipboard(NULL)) {
                    HANDLE h = GetClipboardData(CF_UNICODETEXT);
                    if (h) {
                        wchar_t *wt = (wchar_t *)GlobalLock(h);
                        if (wt) {
                            int len = lstrlenW(wt);
                            INPUT *inputs = (INPUT *)calloc((size_t)len * 2, sizeof(INPUT));
                            if (inputs) {
                                for (int i = 0; i < len; i++) {
                                    inputs[i*2].type = INPUT_KEYBOARD;
                                    inputs[i*2].ki.dwFlags = KEYEVENTF_UNICODE;
                                    inputs[i*2].ki.wScan = wt[i];
                                    inputs[i*2+1].type = INPUT_KEYBOARD;
                                    inputs[i*2+1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                                    inputs[i*2+1].ki.wScan = wt[i];
                                }
                                SendInput((UINT)len * 2, inputs, sizeof(INPUT));
                                free(inputs);
                            }
                            GlobalUnlock(h);
                        }
                    }
                    CloseClipboard();
                }
                return 1;
            } else if (wParam == WM_MOUSEWHEEL) {
                int delta = (int)(short)HIWORD(info->mouseData);
                if (delta > 0)
                    InterlockedIncrement(&g_mouse_wheel_scroll);
                else if (delta < 0)
                    InterlockedDecrement(&g_mouse_wheel_scroll);
                return 1;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static DWORD WINAPI mouse_hook_thread(LPVOID lpParam)
{
    (void)lpParam;
    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, low_level_mouse_proc, GetModuleHandle(NULL), 0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

static char rename_buf[64];
static int  rename_pos;

void input_init(void)
{
    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hStdIn, &mode);

    mode &= ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT |
              ENABLE_ECHO_INPUT | ENABLE_MOUSE_INPUT);
    mode |= ENABLE_WINDOW_INPUT;
    mode |= ENABLE_EXTENDED_FLAGS;
    mode |= ENABLE_QUICK_EDIT_MODE;

    SetConsoleMode(hStdIn, mode);

    /* low-level mouse hook intercepts right-click for paste */
    CreateThread(NULL, 0, mouse_hook_thread, NULL, 0, NULL);

    /* client-side initialisation (server uses manager_init/manager_init_headless) */
    app.prefix_key = 'A';
    app.input_state = INPUT_NORMAL;
    strcpy(app.sb_position, "bottom");
    app.state_label[0] = '\0';
}

static void utf8_from_wchar(wchar_t wch, char *out, int *out_len)
{
    if (wch < 0x80) {
        out[0] = (char)wch;
        *out_len = 1;
    } else if (wch < 0x800) {
        out[0] = (char)(0xC0 | (wch >> 6));
        out[1] = (char)(0x80 | (wch & 0x3F));
        *out_len = 2;
    } else {
        out[0] = (char)(0xE0 | (wch >> 12));
        out[1] = (char)(0x80 | ((wch >> 6) & 0x3F));
        out[2] = (char)(0x80 | (wch & 0x3F));
        *out_len = 3;
    }
}

static void send_resize(SOCKET sock)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return;
    int cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    if (cols < 10) cols = 80;
    if (rows < 5) rows = 24;

    app.terminal_cols = cols;
    app.terminal_rows = rows;

    char data[5];
    data[0] = (char)ACT_RESIZE;
    data[1] = (char)((cols >> 8) & 0xFF);
    data[2] = (char)(cols & 0xFF);
    data[3] = (char)((rows >> 8) & 0xFF);
    data[4] = (char)(rows & 0xFF);
    session_send_msg(sock, CMD_ACT, data, 5);
}

static bool send_action(SOCKET sock, int action_type)
{
    char data[1];
    data[0] = (char)action_type;
    return session_send_msg(sock, CMD_ACT, data, 1);
}

static bool send_action_with_data(SOCKET sock, int action_type,
                                   const char *extra, int extra_len)
{
    /* allocate buffer: 1 byte action + extra_len */
    char *data = (char *)malloc((size_t)extra_len + 1);
    if (!data) return false;
    data[0] = (char)action_type;
    if (extra_len > 0 && extra)
        memcpy(data + 1, extra, extra_len);
    bool ok = session_send_msg(sock, CMD_ACT, data, extra_len + 1);
    free(data);
    return ok;
}

static void send_kbd(SOCKET sock, wchar_t wch)
{
    unsigned char utf8[8];
    int len = 0;
    if (wch < 0x80) {
        utf8[len++] = (unsigned char)wch;
    } else if (wch < 0x800) {
        utf8[len++] = (unsigned char)(0xC0 | (wch >> 6));
        utf8[len++] = (unsigned char)(0x80 | (wch & 0x3F));
    } else {
        utf8[len++] = (unsigned char)(0xE0 | (wch >> 12));
        utf8[len++] = (unsigned char)(0x80 | ((wch >> 6) & 0x3F));
        utf8[len++] = (unsigned char)(0x80 | (wch & 0x3F));
    }
    session_send_msg(sock, CMD_KBD, (const char *)utf8, len);
}

static void send_vt100_key(SOCKET sock, WORD vk, bool alt)
{
    const char *seq = NULL;
    switch (vk) {
    case VK_UP:     seq = "\x1b[A";  break;
    case VK_DOWN:   seq = "\x1b[B";  break;
    case VK_RIGHT:  seq = "\x1b[C";  break;
    case VK_LEFT:   seq = "\x1b[D";  break;
    case VK_HOME:   seq = "\x1b[H";  break;
    case VK_END:    seq = "\x1b[F";  break;
    case VK_PRIOR:  seq = "\x1b[5~"; break;
    case VK_NEXT:   seq = "\x1b[6~"; break;
    case VK_INSERT: seq = "\x1b[2~"; break;
    case VK_DELETE: seq = "\x1b[3~"; break;
    case VK_F1:     seq = "\x1bOP";  break;
    case VK_F2:     seq = "\x1bOQ";  break;
    case VK_F3:     seq = "\x1bOR";  break;
    case VK_F4:     seq = "\x1bOS";  break;
    case VK_F5:     seq = "\x1b[15~"; break;
    case VK_F6:     seq = "\x1b[17~"; break;
    case VK_F7:     seq = "\x1b[18~"; break;
    case VK_F8:     seq = "\x1b[19~"; break;
    case VK_F9:     seq = "\x1b[20~"; break;
    case VK_F10:    seq = "\x1b[21~"; break;
    case VK_F11:    seq = "\x1b[23~"; break;
    case VK_F12:    seq = "\x1b[24~"; break;
    }
    if (!seq) return;

    char buf[16];
    int len = 0;
    if (alt) { buf[len++] = '\x1b'; }
    int slen = (int)strlen(seq);
    memcpy(buf + len, seq, slen);
    len += slen;
    session_send_msg(sock, CMD_KBD, buf, len);
}

bool process_client_input(SOCKET sock)
{
    LONG wheel = InterlockedExchange(&g_mouse_wheel_scroll, 0);
    if (wheel != 0) {
        if (wheel > 0) {
            while (wheel > 0) {
                uint8_t bat = (wheel > 127) ? 127 : (uint8_t)wheel;
                send_action_with_data(sock, ACT_SCROLL_UP, (const char *)&bat, 1);
                wheel -= bat;
            }
        } else {
            wheel = -wheel;
            while (wheel > 0) {
                uint8_t bat = (wheel > 127) ? 127 : (uint8_t)wheel;
                send_action_with_data(sock, ACT_SCROLL_DOWN, (const char *)&bat, 1);
                wheel -= bat;
            }
        }
    }

    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);

    DWORD count = 0;
    if (!GetNumberOfConsoleInputEvents(hStdIn, &count) || count == 0)
        return true; /* no event, keep going */

    DWORD max = count;
    for (DWORD i = 0; i < max; i++) {
        INPUT_RECORD ir;
        DWORD read;
        if (!ReadConsoleInputW(hStdIn, &ir, 1, &read) || read == 0)
            break;

        /* resize event */
        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            send_resize(sock);
            continue;
        }

        /* only key down events */
        if (ir.EventType != KEY_EVENT) continue;
        if (!ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;
        wchar_t wch = ir.Event.KeyEvent.uChar.UnicodeChar;
        bool is_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

        /* -- RENAME state -- */
        if (app.input_state == INPUT_RENAME) {
            if (vk == VK_RETURN) {
                /* confirm rename */
                send_action_with_data(sock, ACT_RENAME_CONFIRM,
                                      rename_buf, rename_pos);
                app.input_state = INPUT_NORMAL;
                app.state_label[0] = '\0';
            } else if (vk == VK_ESCAPE) {
                /* cancel */
                send_action(sock, ACT_RENAME_CANCEL);
                app.input_state = INPUT_NORMAL;
                app.state_label[0] = '\0';
            } else if (vk == VK_BACK && rename_pos > 0) {
                rename_pos--;
                rename_buf[rename_pos] = '\0';
                send_action_with_data(sock, ACT_RENAME_CHAR, rename_buf, rename_pos);
            } else if (wch >= 32 && wch < 128 && rename_pos < (int)sizeof(rename_buf) - 1) {
                /* printable ascii char */
                char utf8[4];
                int ulen;
                utf8_from_wchar(wch, utf8, &ulen);
                for (int j = 0; j < ulen && rename_pos < (int)sizeof(rename_buf) - 1; j++)
                    rename_buf[rename_pos++] = utf8[j];
                rename_buf[rename_pos] = '\0';
                send_action_with_data(sock, ACT_RENAME_CHAR, rename_buf, rename_pos);
            }
            continue;
        }

        /* -- check for Ctrl+prefix_key (Ctrl+A by default) -- */
        /* ignore RIGHT_ALT (AltGr) so international chars like ą work */
        if (app.input_state == INPUT_NORMAL &&
            is_ctrl && !(ctrl & RIGHT_ALT_PRESSED) &&
            vk == (WORD)(unsigned char)app.prefix_key) {
            app.input_state = INPUT_PREFIX;
            continue;
        }

        /* -- PREFIX state -- */
        if (app.input_state == INPUT_PREFIX) {
            app.input_state = INPUT_NORMAL;

            /* lowercase for mapping */
            char c = (char)(wch >= L'A' && wch <= L'Z' ? (wch - L'A' + L'a') : wch);

            switch (c) {
            case 'c':
                send_action(sock, ACT_NEW_WINDOW);
                break;
            case 'n':
                send_action(sock, ACT_NEXT_WINDOW);
                break;
            case 'p':
                send_action(sock, ACT_PREV_WINDOW);
                break;
            case 'k':
                send_action(sock, ACT_KILL_WINDOW);
                break;
            case 'd':
                session_send_msg(sock, CMD_DET, NULL, 0);
                return false; /* disconnect */
            case 'w':
                send_action(sock, ACT_LIST_WINDOWS);
                break;
            case '/':
            case 'h':
            case '?':
                send_action(sock, ACT_HELP);
                break;
            case 'i':
                send_action(sock, ACT_INFO);
                break;
            case 'a':
                /* start rename */
                app.input_state = INPUT_RENAME;
                rename_pos = 0;
                rename_buf[0] = '\0';
                send_action(sock, ACT_RENAME_START);
                break;
            case 'r':
                /* redraw: send NONE action to force refresh */
                send_action(sock, ACT_NONE);
                break;
            default:
                /* number keys 0-9 for window switch */
                if (c >= '0' && c <= '9') {
                    char win_id = (char)(c - '0');
                    send_action_with_data(sock, ACT_SWITCH_WINDOW, &win_id, 1);
                }
                break;
            }

            /* check for Ctrl+A again (last window) */
            if (is_ctrl && vk == (WORD)(unsigned char)app.prefix_key) {
                send_action(sock, ACT_LAST_WINDOW);
            }

            /* arrow keys for line scroll */
            if (vk == VK_UP) {
                send_action(sock, ACT_SCROLL_UP);
            } else if (vk == VK_DOWN) {
                send_action(sock, ACT_SCROLL_DOWN);
            } else if (vk == VK_PRIOR) {
                uint8_t cnt = (uint8_t)(app.terminal_rows - 1);
                send_action_with_data(sock, ACT_SCROLL_UP, (const char *)&cnt, 1);
            } else if (vk == VK_NEXT) {
                uint8_t cnt = (uint8_t)(app.terminal_rows - 1);
                send_action_with_data(sock, ACT_SCROLL_DOWN, (const char *)&cnt, 1);
            }

            continue;
        }

        /* -- NORMAL state: passthrough to server -- */
        if (app.input_state == INPUT_NORMAL) {
            /* function keys / navigation */
            if (vk >= VK_F1 && vk <= VK_F12) {
                send_vt100_key(sock, vk, false);
            } else if ((vk >= VK_END && vk <= VK_DELETE) ||
                       vk == VK_PRIOR || vk == VK_NEXT) {
                bool alt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
                send_vt100_key(sock, vk, alt);
            } else if (vk == VK_RETURN) {
                session_send_msg(sock, CMD_KBD, "\r", 1);
            } else if (vk == VK_BACK) {
                session_send_msg(sock, CMD_KBD, "\x7f", 1);
            } else if (vk == VK_TAB) {
                session_send_msg(sock, CMD_KBD, "\t", 1);
            } else if (vk == VK_ESCAPE) {
                session_send_msg(sock, CMD_KBD, "\x1b", 1);
            } else if (wch != L'\0') {
                send_kbd(sock, wch);
            }
        }
    }
    return true;
}
