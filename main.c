#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "window.h"
#include "manager.h"
#include "renderer.h"
#include "input.h"
#include "config.h"
#include "session.h"
#include "lang.h"


/* ---- helpers ---- */

static void print_help(void)
{
    printf("=== %s ===\n", tr("cli_header", "WinScreen - Windows terminal multiplexer"));
    printf("    %s: %s (%s: %s)\n\n",
           tr("cli_author", "Author"), AUTHOR,
           tr("cli_ver", "ver"), VERSION);
    printf("%s\n\n", tr("cli_usage", "Usage: winscreen.exe [options]"));
    printf("%s\n", tr("cli_options", "Options:"));
    printf("%s\n", tr("cli_help_opt", "  -h              Show this help message"));
    printf("%s\n", tr("cli_config_opt", "  -c FILE         Config file path"));
    printf("%s\n", tr("cli_session_opt", "  -S NAME         Session name (default: \"default\")"));
    printf("%s\n", tr("cli_reattach_opt", "  -r NAME         Reattach to existing session"));
    printf("%s\n", tr("cli_shell_opt", "  -e CMD          Shell command for first window"));
    printf("%s\n", tr("cli_name_opt", "  -n NAME         Window name for first window"));
    printf("%s\n", tr("cli_encoding_opt", "  --encoding ENC  Terminal encoding (utf-8, cp1250, etc.)"));
    printf("%s\n", tr("cli_linebuf_opt", "  --linebuf N     Scrollback lines (default: 256)"));
    printf("%s\n", tr("cli_list_opt", "  -ls             List active sessions"));
    printf("%s\n", tr("cli_version_opt", "  --version       Show version"));
    printf("%s\n", tr("cli_lang_opt", "  --lang FILE     Language file for translations"));
    printf("%s\n", tr("cli_clean_opt", "  --CLEAN_ALL_SESSIONS  Kill all server processes and clear session files"));
    printf("\n%s\n", tr("cli_prefix", "Prefix: Ctrl+A."));
    printf("%s\n", tr("cli_shortcuts", " Help: Ctrl+A h, Info: Ctrl+A i, List: Ctrl+A w"));
    printf("%s\n", tr("cli_shortcuts2", " Detach: Ctrl+A d, New: Ctrl+A c, Close: Ctrl+A k"));
    printf("%s\n", tr("cli_shortcuts3", " Next: Ctrl+A n, Prev: Ctrl+A p"));
}

/* ---- server loop ---- */

static void server_main(const char *session_name, const char *shell_cmd,
                        const char *win_name, const char *config_path)
{
    /* headless init */
    manager_init_headless(80, 24);
    config_load(config_path);

    strncpy(app.session_name, session_name, sizeof(app.session_name) - 1);
    if (shell_cmd)
        strncpy(app.shell_cmd, shell_cmd, sizeof(app.shell_cmd) - 1);

    /* create first window */
    manager_create_window();
    if (win_name && win_name[0])
        strncpy(app.windows[app.active_id]->name, win_name,
                sizeof(app.windows[app.active_id]->name) - 1);

    manager_resize_all(80, 24);

    int server_port = 0;
    SOCKET listener = session_start_server(session_name, &server_port);
    if (listener == INVALID_SOCKET) {
        manager_cleanup();
        return;
    }
    app.server_port = server_port;
    session_save_info(session_name, GetCurrentProcessId(),
                      server_port, app.window_count);

    /* accept loop */
    int last_wc = -1;
    while (1) {
        /* update session file when window count changes */
        int wc = app.window_count;
        if (wc != last_wc) {
            last_wc = wc;
            manager_update_session_info();
        }

        /* accept with timeout using select */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listener, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; /* 500ms */

        if (select(0, &readfds, NULL, NULL, &tv) <= 0)
            goto cleanup_check;

        SOCKET conn = accept(listener, NULL, NULL);
        if (conn == INVALID_SOCKET)
            goto cleanup_check;

        /* ---- handle one client ---- */
        {
            bool overlay = false;
            bool force_redraw = true;
            DWORD last_clock = 0;
            DWORD last_render = 0;

            /* recv timeout: 50ms for low-latency echo feedback */
            DWORD rcv_timeout = 50;
            setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO,
                      (const char *)&rcv_timeout, sizeof(rcv_timeout));

            while (1) {
                uint8_t cmd;
                char msg_buf[65536];
                int rlen = session_recv_msg(conn, &cmd, msg_buf, sizeof(msg_buf));

                if (rlen == -1) {
                    int err = WSAGetLastError();
                    if (err != 0 && err != WSAETIMEDOUT)
                        break; /* real error */
                }

                if (rlen >= 0) {
                    if (cmd == CMD_KBD) {
                        if (overlay) {
                            overlay = false;
                            force_redraw = true;
                            if (rlen == 1 && msg_buf[0] >= '0' && msg_buf[0] <= '9') {
                                char act[2];
                                act[0] = (char)ACT_SWITCH_WINDOW;
                                act[1] = msg_buf[0] - '0';
                                handle_server_action(conn, act, 2);
                            }
                        } else {
                            VtWindow *win = app.windows[app.active_id];
                            if (win && win->is_alive)
                                window_write_input(win, msg_buf, (DWORD)rlen);
                        }
                    } else if (cmd == CMD_DET) {
                        break;
                    } else if (cmd == CMD_QUIT) {
                        break;
                    } else if (cmd == CMD_ACT) {
                        bool was_overlay = handle_server_action(conn, msg_buf, rlen);
                        if (was_overlay) {
                            overlay = true;
                        } else {
                            force_redraw = true;
                        }
                    } else if (cmd == CMD_RST) {
                        /* ignore */
                    }
                }

                 /* cleanup dead windows */
                for (int i = 0; i < MAX_WINDOWS; i++) {
                    VtWindow *w = app.windows[i];
                    if (w && !w->is_alive) {
                        manager_close_window(i);
                    }
                }

                if (app.window_count == 0)
                    break;

                /* check if we need to render */
                DWORD now = GetTickCount();
                bool clock_tick = (now - last_clock >= 1000);
                if (!overlay) {
                    VtWindow *win = app.windows[app.active_id];
                    bool needs_content = force_redraw || (win && win->is_dirty);
                    bool needs_sb = app.sb_dirty || clock_tick;

                    /* render immediately for sb-only (rename), batch content at 16ms */
                    if (needs_content || needs_sb) {
                        bool sb_only = needs_sb && !needs_content;
                        if (force_redraw || sb_only || (now - last_render >= 16)) {
                            last_render = now;

                            /* combine content, status bar, and cursor positioning into one message */
                            char out_buf[131072 + 8192];
                            int out_pos = 0;

                            if (needs_content) {
                                char screen[131072];
                                bool full = force_redraw || (win && win->buffer && win->buffer->all_dirty);
                                int slen = render_content_to_string(screen, sizeof(screen), full);
                                if (slen > 0) {
                                    memcpy(out_buf + out_pos, screen, slen);
                                    out_pos += slen;
                                }
                                if (win) win->is_dirty = false;
                                force_redraw = false;
                            }

                            if (needs_sb) {
                            char sb[8192];
                            int slen = render_status_bar_to_string(sb, sizeof(sb), false);
                            if (slen > 0) {
                                memcpy(out_buf + out_pos, sb, slen);
                                out_pos += slen;
                            }
                            app.sb_dirty = false;
                            last_clock = now;
                        }

                        /* position cursor at shell cursor position (overrides status bar cursor) */
                        if (out_pos > 0 && win && win->is_alive) {
                            int cx = 0, cy = 0;
                            bool skip_cursor = false;
                            EnterCriticalSection(&win->lock);
                            if (win->buffer) {
                                cx = win->buffer->cursor_x;
                                cy = win->buffer->cursor_y;
                                skip_cursor = (win->buffer->scroll_pos != 0);
                            }
                            LeaveCriticalSection(&win->lock);
                            if (!skip_cursor) {
                                int n = snprintf(out_buf + out_pos, sizeof(out_buf) - out_pos,
                                                "\x1b[%d;%dH", cy + 1, cx + 1);
                                if (n > 0) out_pos += n;
                            }
                        }

                        if (out_pos > 0) {
                            session_send_msg(conn, CMD_SCR, out_buf, out_pos);
                        }
                    }
                }
            }
        }

            closesocket(conn);
        }

cleanup_check:
        /* check for dead windows */
        for (int i = 0; i < MAX_WINDOWS; i++) {
            VtWindow *w = app.windows[i];
            if (w && !w->is_alive) {
                manager_close_window(i);
            }
        }
        if (app.window_count == 0)
            break;
    }

    closesocket(listener);
    manager_cleanup();
    session_remove_info(session_name);
}

/* ---- client loop ---- */

static void client_main(const char *session_name)
{
    SOCKET sock = session_connect_to_server(session_name);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "%s '%s'.\n",
                tr("err_connect", "WinScreen: Cannot connect to session"), session_name);
        return;
    }

    render_init();
    input_init();

    /* show cursor */
    printf("\x1b[?25h");
    fflush(stdout);

    /* send initial terminal size */
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
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

    /* set socket receive timeout (Windows: DWORD milliseconds) */
    DWORD rcv_timeout = 50; /* 50ms */
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               (const char *)&rcv_timeout, sizeof(rcv_timeout));

    /* main loop */
    while (1) {
        /* process keyboard input */
        if (!process_client_input(sock)) {
            /* detach requested */
            break;
        }

        /* receive screen updates from server */
        {
            uint8_t cmd;
            char msg_buf[131072];
            int rlen = session_recv_msg(sock, &cmd, msg_buf, sizeof(msg_buf));

            if (rlen == -1) {
                int err = WSAGetLastError();
                if (err != 0 && err != WSAETIMEDOUT)
                    break; /* connection error */
            } else if (rlen >= 0) {
                if (cmd == CMD_SCR) {
                    /* display screen text */
                    msg_buf[rlen] = '\0';
                    printf("\x1b[0m%s\x1b[0m\x1b[?25h", msg_buf);
                    fflush(stdout);
                } else if (cmd == CMD_DET) {
                    break;
                }
            }
        }
    }

    render_cleanup();
    closesocket(sock);
}

/* ---- list sessions ---- */

static void list_sessions(void)
{
    session_list_sessions();
}

/* ---- main ---- */

int main(int argc, char *argv[])
{
    const char *config_path = "winscreen.cfg";
    const char *session_name = NULL;
    const char *reattach_name = NULL;
    const char *server_name = NULL;
    const char *execute_cmd = NULL;
    const char *win_name = NULL;
    const char *encoding = "utf-8";
    int linebuf = 256;
    const char *lang_file = NULL;
    bool do_list = false;
    bool show_version = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc) config_path = argv[++i];
            else { fprintf(stderr, "%s\n", tr("err_c_req", "-c requires path")); return 1; }
        } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--session") == 0) {
            if (i + 1 < argc) session_name = argv[++i];
            else { fprintf(stderr, "%s\n", tr("err_S_req", "-S requires name")); return 1; }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--reattach") == 0) {
            if (i + 1 < argc) reattach_name = argv[++i];
            else { fprintf(stderr, "%s\n", tr("err_r_req", "-r requires name")); return 1; }
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--execute") == 0) {
            if (i + 1 < argc) execute_cmd = argv[++i];
            else { fprintf(stderr, "%s\n", tr("err_e_req", "-e requires command")); return 1; }
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--name") == 0) {
            if (i + 1 < argc) win_name = argv[++i];
            else { fprintf(stderr, "%s\n", tr("err_n_req", "-n requires name")); return 1; }
        } else if (strcmp(argv[i], "--encoding") == 0) {
            if (i + 1 < argc) encoding = argv[++i];
            else { fprintf(stderr, "%s\n", tr("err_enc_req", "--encoding requires arg")); return 1; }
        } else if (strcmp(argv[i], "--linebuf") == 0) {
            if (i + 1 < argc) linebuf = atoi(argv[++i]);
            else { fprintf(stderr, "%s\n", tr("err_lbuf_req", "--linebuf requires N")); return 1; }
        } else if (strcmp(argv[i], "--lang") == 0) {
            if (i + 1 < argc) lang_file = argv[++i];
            else { fprintf(stderr, "%s\n", tr("err_lang_req", "--lang requires filename")); return 1; }
        } else if (strcmp(argv[i], "-ls") == 0 || strcmp(argv[i], "--list") == 0) {
            do_list = true;
        } else if (strcmp(argv[i], "--server") == 0) {
            if (i + 1 < argc) server_name = argv[++i];
            else { fprintf(stderr, "%s\n", tr("err_server_req", "--server requires name")); return 1; }
        } else if (strcmp(argv[i], "--version") == 0) {
            show_version = true;
        } else if (strcmp(argv[i], "--CLEAN_ALL_SESSIONS") == 0) {
            session_clean_all();
            return 0;
        } else {
            const char *fmt = tr("err_unknown_opt", "Unknown option: %s");
            fprintf(stderr, fmt, argv[i]);
            fprintf(stderr, "\n");
            fprintf(stderr, "%s\n", tr("err_use_help", "Use -h for help."));
            return 1;
        }
    }

    if (show_version) {
        printf("WinScreen 1.0.0\n");
        return 0;
    }

    (void)encoding;
    (void)linebuf;
    (void)lang_file;

    lang_load(lang_file);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (do_list) {
        list_sessions();
        return 0;
    }

    if (reattach_name) {
        /* direct reattach to existing session */
        client_main(reattach_name);
        return 0;
    }

    if (server_name) {
        /* server mode (spawned by main process) */
        server_main(server_name, execute_cmd, win_name, config_path);
        return 0;
    }

    /* default: spawn server and connect as client */
    char default_session_name[64];
    const char *sname;
    if (session_name) {
        sname = session_name;
    } else {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        strftime(default_session_name, sizeof(default_session_name),
                 "WinScr_%Y%m%d_%H%M%S", t);
        sname = default_session_name;
    }

    /* check if session already exists and is alive */
    {
        int port = 0, windows = 0;
        DWORD pid = 0;
        char sname_buf[64] = {0};
        if (session_load_info(sname, &port, &pid, &windows,
                              sname_buf, sizeof(sname_buf))) {
            /* try to connect to verify it's alive */
            SOCKET test = session_connect_to_server(sname);
            if (test != INVALID_SOCKET) {
                closesocket(test);
                client_main(sname);
                return 0;
            }
            /* stale session file - remove it */
            session_remove_info(sname);
        }
    }

    /* build server process args */
    char self_path[260];
    GetModuleFileNameA(NULL, self_path, sizeof(self_path));

    /* args: --server <name> [--linebuf N] [--encoding ENC] [-e cmd] [-n name] [-c config] */
    char srv_args[16][260];
    int srv_argc = 0;
    srv_args[srv_argc][0] = '\0'; /* argv[0] - filled by CreateProcess */
    srv_argc++;
    snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "--server");
    snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "%s", sname);
    snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "--linebuf");
    snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "%d", linebuf);
    snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "--encoding");
    snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "%s", encoding);
    if (execute_cmd) {
        snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "-e");
        snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "%s", execute_cmd);
    }
    if (win_name) {
        snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "-n");
        snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "%s", win_name);
    }
    snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "-c");
    snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "%s", config_path);
    if (lang_file) {
        snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "--lang");
        snprintf(srv_args[srv_argc++], sizeof(srv_args[0]), "%s", lang_file);
    }

    /* build command line string */
    char cmdline[4096] = "";
    /* argv[0] must be the program name */
    strcat(cmdline, "\"");
    strcat(cmdline, self_path);
    strcat(cmdline, "\"");
    for (int i = 1; i < srv_argc; i++) {
        strcat(cmdline, " ");
        strcat(cmdline, "\"");
        strcat(cmdline, srv_args[i]);
        strcat(cmdline, "\"");
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessA(
        self_path,
        cmdline,
        NULL, NULL, FALSE,
        DETACHED_PROCESS,
        NULL, NULL,
        &si, &pi
    );

    if (!ok) {
        fprintf(stderr, "WinScreen: Cannot start server process.\n");
        return 1;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    /* wait for server to be ready */
    for (int attempts = 0; attempts < 100; attempts++) {
        Sleep(100);
        int port = 0, windows = 0;
        DWORD pid = 0;
        char name_buf[64] = {0};
        if (session_load_info(sname, &port, &pid, &windows,
                              name_buf, sizeof(name_buf))) {
            if (port > 0) {
                client_main(sname);
                return 0;
            }
        }
    }

    fprintf(stderr, "WinScreen: Server did not respond for session '%s'.\n",
            sname);
    return 1;
}
