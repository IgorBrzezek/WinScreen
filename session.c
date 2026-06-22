#define _WIN32_WINNT 0x0A00
#include "session.h"
#include "lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int session_refcount = 0;

static bool ensure_winsock(void)
{
    if (session_refcount == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return false;
    }
    session_refcount++;
    return true;
}

static void release_winsock(void)
{
    session_refcount--;
    if (session_refcount <= 0) {
        WSACleanup();
        session_refcount = 0;
    }
}

bool session_init(void) { return ensure_winsock(); }
void session_cleanup(void) { release_winsock(); }

static void build_session_path(const char *name, char *path, int path_size)
{
    char appdata[260];
    DWORD len = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    if (len == 0 || len >= sizeof(appdata))
        snprintf(appdata, sizeof(appdata), "C:\\Users\\Public");
    snprintf(path, path_size, "%s\\WinScreen", appdata);
    CreateDirectoryA(path, NULL);
    int plen = (int)strlen(path);
    snprintf(path + plen, path_size - plen, "\\sessions");
    CreateDirectoryA(path, NULL);
    plen = (int)strlen(path);
    snprintf(path + plen, path_size - plen, "\\%s.txt", name);
}

SOCKET session_start_server(const char *name, int *out_port)
{
    (void)name;
    if (!ensure_winsock()) return INVALID_SOCKET;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { release_winsock(); return INVALID_SOCKET; }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* system assigns */

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s); release_winsock(); return INVALID_SOCKET;
    }

    if (listen(s, 1) == SOCKET_ERROR) {
        closesocket(s); release_winsock(); return INVALID_SOCKET;
    }

    /* get assigned port */
    struct sockaddr_in bound;
    int bound_len = sizeof(bound);
    if (getsockname(s, (struct sockaddr *)&bound, &bound_len) == SOCKET_ERROR) {
        closesocket(s); release_winsock(); return INVALID_SOCKET;
    }
    if (out_port) *out_port = ntohs(bound.sin_port);
    return s;
}

SOCKET session_connect_to_server(const char *name)
{
    if (!ensure_winsock()) return INVALID_SOCKET;

    int port = 0;
    DWORD pid = 0;
    int windows = 0;
    char name_buf[64] = {0};

    if (!session_load_info(name, &port, &pid, &windows, name_buf, sizeof(name_buf))) {
        release_winsock();
        return INVALID_SOCKET;
    }
    if (port <= 0) { release_winsock(); return INVALID_SOCKET; }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { release_winsock(); return INVALID_SOCKET; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s); release_winsock(); return INVALID_SOCKET;
    }

    return s;
}

static int recv_all(SOCKET s, char *buf, int len)
{
    while (len > 0) {
        int r = recv(s, buf, len, 0);
        if (r <= 0) return -1;
        buf += r;
        len -= r;
    }
    return 0;
}

bool session_send_msg(SOCKET sock, uint8_t cmd, const char *data, int len)
{
    if (len < 0) len = 0;
    uint8_t header[5];
    header[0] = (uint8_t)((len >> 24) & 0xFF);
    header[1] = (uint8_t)((len >> 16) & 0xFF);
    header[2] = (uint8_t)((len >> 8) & 0xFF);
    header[3] = (uint8_t)(len & 0xFF);
    header[4] = cmd;

    if (send(sock, (const char *)header, 5, 0) == SOCKET_ERROR)
        return false;
    if (len > 0 && data) {
        if (send(sock, data, len, 0) == SOCKET_ERROR)
            return false;
    }
    return true;
}

int session_recv_msg(SOCKET sock, uint8_t *cmd, char *buf, int buf_size)
{
    uint8_t header[5];
    if (recv_all(sock, (char *)header, 5) != 0)
        return -1;

    int len = ((int)header[0] << 24) |
              ((int)header[1] << 16) |
              ((int)header[2] << 8)  |
              ((int)header[3]);
    *cmd = header[4];

    if (len < 0 || len > buf_size)
        return -2;

    if (len > 0) {
        if (recv_all(sock, buf, len) != 0)
            return -1;
    }
    return len;
}

bool session_save_info(const char *name, DWORD pid, int port, int windows)
{
    char path[520];
    build_session_path(name, path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "name=%s\n", name);
    fprintf(f, "pid=%lu\n", (unsigned long)pid);
    fprintf(f, "port=%d\n", port);
    fprintf(f, "windows=%d\n", windows);
    fprintf(f, "created=%lld\n", (long long)time(NULL));
    fprintf(f, "encoding=utf-8\n");
    fclose(f);
    return true;
}

bool session_load_info(const char *name, int *port, DWORD *pid,
                       int *windows, char *name_out, int name_size)
{
    char path[520];
    build_session_path(name, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    *port = 0;
    *pid = 0;
    *windows = 0;
    if (name_out) name_out[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = '\0';
        char *val = eq;
        /* strip trailing newline */
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r'))
            val[--vlen] = '\0';

        if (strcmp(line, "port") == 0) *port = atoi(val);
        else if (strcmp(line, "pid") == 0) *pid = (DWORD)atol(val);
        else if (strcmp(line, "windows") == 0) *windows = atoi(val);
        else if (strcmp(line, "name") == 0 && name_out)
            strncpy(name_out, val, (size_t)name_size - 1);
    }
    fclose(f);
    return (*port > 0);
}

void session_remove_info(const char *name)
{
    char path[520];
    build_session_path(name, path, sizeof(path));
    DeleteFileA(path);
}

void session_list_sessions(void)
{
    char appdata[260];
    DWORD len = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    if (len == 0 || len >= sizeof(appdata))
        snprintf(appdata, sizeof(appdata), "C:\\Users\\Public");

    char dir[520];
    snprintf(dir, sizeof(dir), "%s\\WinScreen\\sessions", appdata);

    WIN32_FIND_DATAA ffd;
    char pattern[600];
    snprintf(pattern, sizeof(pattern), "%s\\*.txt", dir);

    HANDLE h = FindFirstFileA(pattern, &ffd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("%s\n", tr("sess_no_active", "No active WinScreen sessions."));
        return;
    }

    printf("%s\n", tr("sess_active", "Active WinScreen sessions:"));
    printf(tr("sess_header_fmt", "  %-20s %-10s %-6s %-10s\n"),
           tr("sess_h_name", "Name"), tr("sess_h_pid", "PID"),
           tr("sess_h_windows", "Windows"), tr("sess_h_encoding", "Encoding"));
    printf("  %-20s %-10s %-6s %-10s\n",
           "--------------------", "----------", "------", "----------");

    do {
        char fname[1024];
        snprintf(fname, sizeof(fname), "%s\\%s", dir, ffd.cFileName);
        FILE *f = fopen(fname, "r");
        if (!f) continue;

        char *dot = strchr(ffd.cFileName, '.');
        if (dot) *dot = '\0';

        int windows = 0;
        DWORD pid = 0;
        char name[64] = {0};
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq++ = '\0';
            char *val = eq;
            size_t vlen = strlen(val);
            while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r'))
                val[--vlen] = '\0';
            if (strcmp(line, "name") == 0)
                strncpy(name, val, sizeof(name) - 1);
            else if (strcmp(line, "pid") == 0)
                pid = (DWORD)atol(val);
            else if (strcmp(line, "windows") == 0)
                windows = atoi(val);
        }
        fclose(f);
        if (name[0] == '\0') {
            size_t fnlen = strlen(ffd.cFileName);
            if (fnlen >= sizeof(name)) fnlen = sizeof(name) - 1;
            memcpy(name, ffd.cFileName, fnlen);
            name[fnlen] = '\0';
        }
        printf("  %-20s %-10lu %-6d %-10s\n",
               name, (unsigned long)pid, windows, "utf-8");
    } while (FindNextFileA(h, &ffd) != 0);

    FindClose(h);
}

void session_clean_all(void)
{
    char appdata[260];
    DWORD len = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    if (len == 0 || len >= sizeof(appdata))
        snprintf(appdata, sizeof(appdata), "C:\\Users\\Public");

    char dir[520];
    snprintf(dir, sizeof(dir), "%s\\WinScreen\\sessions", appdata);

    WIN32_FIND_DATAA ffd;
    char pattern[600];
    snprintf(pattern, sizeof(pattern), "%s\\*.txt", dir);

    HANDLE h = FindFirstFileA(pattern, &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char fpath[1024];
            snprintf(fpath, sizeof(fpath), "%s\\%s", dir, ffd.cFileName);
            DeleteFileA(fpath);
        } while (FindNextFileA(h, &ffd) != 0);
        FindClose(h);
    }

    RemoveDirectoryA(dir);

    char self_path[260];
    GetModuleFileNameA(NULL, self_path, sizeof(self_path));
    const char *exe_name = strrchr(self_path, '\\');
    exe_name = exe_name ? exe_name + 1 : self_path;

    char cmd[520];
    snprintf(cmd, sizeof(cmd), "taskkill /f /im %s > nul 2>&1", exe_name);
    system(cmd);
}
