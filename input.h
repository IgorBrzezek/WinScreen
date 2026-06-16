#ifndef INPUT_H
#define INPUT_H

#include <winsock2.h>
#include <stdbool.h>

void input_init(void);

/* Client: read one input event, send keystrokes/actions to server via TCP.
   Returns false if the caller should disconnect (detach or quit). */
bool process_client_input(SOCKET sock);

#endif
