#ifndef MANAGER_H
#define MANAGER_H

#include <winsock2.h>
#include <stdbool.h>

void manager_init(void);
void manager_init_headless(int cols, int rows);
void manager_create_window(void);
void manager_next_window(void);
void manager_prev_window(void);
void manager_switch_window(int id);
void manager_close_window(int id);
void manager_resize_all(int cols, int rows);
void manager_cleanup(void);
void manager_update_session_info(void);

/* Server-side: handle an action from client over TCP.
   Returns true if overlay mode was entered (CMD_SCR was sent for overlay).
   For overlays, the next CMD_KBD from client should dismiss it. */
bool handle_server_action(SOCKET conn, const char *data, int len);

#endif
