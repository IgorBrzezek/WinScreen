#ifndef SESSION_H
#define SESSION_H

#include <winsock2.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#define CMD_KBD  0x01
#define CMD_SCR  0x02
#define CMD_DET  0x03
#define CMD_QUIT 0x04
#define CMD_ACT  0x08
#define CMD_PNG  0x06
#define CMD_RST  0x07

#define ACT_NONE          255
#define ACT_PASSTHROUGH   1
#define ACT_NEW_WINDOW    2
#define ACT_NEXT_WINDOW   3
#define ACT_PREV_WINDOW   4
#define ACT_SWITCH_WINDOW 5
#define ACT_LAST_WINDOW   6
#define ACT_RENAME_START  7
#define ACT_RENAME_CHAR   8
#define ACT_RENAME_CONFIRM 9
#define ACT_RENAME_CANCEL 10
#define ACT_KILL_WINDOW   11
#define ACT_SCROLL_UP     12
#define ACT_SCROLL_DOWN   13
#define ACT_HELP          14
#define ACT_LIST_WINDOWS  15
#define ACT_DETACH        16
#define ACT_RESIZE        17
#define ACT_INFO          18

bool session_init(void);
void session_cleanup(void);

SOCKET session_start_server(const char *name, int *out_port);
SOCKET session_connect_to_server(const char *name);

bool session_send_msg(SOCKET sock, uint8_t cmd, const char *data, int len);
int  session_recv_msg(SOCKET sock, uint8_t *cmd, char *buf, int buf_size);

bool session_save_info(const char *name, DWORD pid, int port, int windows);
bool session_load_info(const char *name, int *port, DWORD *pid,
                       int *windows, char *name_out, int name_size);
void session_remove_info(const char *name);
void session_list_sessions(void);
void session_clean_all(void);

#endif
