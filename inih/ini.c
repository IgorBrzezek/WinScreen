#include "ini.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char *ptr;
    size_t len;
    size_t pos;
} ini_string_reader;

static int ini_string_read(void *ctx, char *buf, size_t buf_size) {
    ini_string_reader *r = (ini_string_reader *)ctx;
    size_t available = r->len - r->pos;
    size_t to_read = buf_size - 1;
    if (to_read > available) to_read = available;
    if (to_read == 0) return 0;
    memcpy(buf, r->ptr + r->pos, to_read);
    r->pos += to_read;
    buf[to_read] = '\0';
    return (int)to_read;
}

typedef int (*ini_reader)(void *ctx, char *buf, size_t buf_size);

static int ini_parse_custom(ini_reader reader, void *reader_ctx,
                            int (*handler)(void *user, const char *section,
                                           const char *name, const char *value),
                            void *user) {
    char *line = NULL;
    size_t line_len = 0;
    char current_section[256] = "";
    int lineno = 0;
    int error = 0;

    while (1) {
        char buf[512];
        int n = reader(reader_ctx, buf, sizeof(buf));
        if (n < 0) { error = 1; break; }
        if (n == 0) break;

        char *p = buf;
        while (*p) {
            char *end = strchr(p, '\n');
            if (!end) end = p + strlen(p);
            size_t len = end - p;

            char *old = line;
            size_t old_len = line_len;
            line_len = old_len + len + 1;
            line = (char *)realloc(line, line_len);
            if (old != line && old) { }
            if (!line) return 0;
            if (old_len > 0) memmove(line, old, old_len);
            memcpy(line + old_len, p, len);
            line[old_len + len] = '\0';

            if (*end == '\n') {
                lineno++;
                char *s = line;
                while (*s && isspace((unsigned char)*s)) s++;
                if (*s == ';' || *s == '#' || *s == '\0') {
                    /* comment or blank line */
                } else if (*s == '[') {
                    s++;
                    char *e = strchr(s, ']');
                    if (!e) { error = 1; break; }
                    *e = '\0';
                    size_t sec_len = e - s;
                    if (sec_len >= sizeof(current_section)) sec_len = sizeof(current_section) - 1;
                    memcpy(current_section, s, sec_len);
                    current_section[sec_len] = '\0';
                } else {
                    char *eq = strchr(s, '=');
                    if (!eq) eq = strchr(s, ':');
                    if (!eq) { error = 1; break; }
                    char *name_end = eq - 1;
                    while (name_end >= s && isspace((unsigned char)*name_end)) name_end--;
                    name_end[1] = '\0';
                    char *val = eq + 1;
                    while (*val && isspace((unsigned char)*val)) val++;
                    size_t val_len = strlen(val);
                    while (val_len > 0 && isspace((unsigned char)val[val_len - 1])) val_len--;
                    val[val_len] = '\0';
                    if (handler) {
                        if (!handler(user, current_section, s, val))
                            { error = 1; break; }
                    }
                }
                line_len = 0;
                free(line);
                line = NULL;
            }

            if (*end == '\n') p = end + 1;
            else p = end;
        }
    }

    free(line);
    return error ? 0 : 1;
}

int ini_parse(const char *filename,
              int (*handler)(void *user, const char *section,
                             const char *name, const char *value),
              void *user) {
    FILE *f = fopen(filename, "r");
    if (!f) return 0;

    char buf[4096];
    size_t n;
    int lineno = 0;
    char current_section[256] = "";
    int error = 0;
    char *line = NULL;
    size_t line_len = 0;

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        char *p = buf;
        char *end = buf + n;
        while (p < end) {
            char *nl = memchr(p, '\n', end - p);
            if (!nl) nl = end;
            size_t len = nl - p;

            char *old = line;
            size_t old_len = line_len;
            line_len = old_len + len + 1;
            line = (char *)realloc(line, line_len);
            if (!line) { fclose(f); return 0; }
            if (old != line && old_len > 0)
                memmove(line, old, old_len);
            memcpy(line + old_len, p, len);
            line[old_len + len] = '\0';

            if (nl < end) {
                lineno++;
                char *s = line;
                while (*s && isspace((unsigned char)*s)) s++;
                if (*s == ';' || *s == '#' || *s == '\0') {
                    /* skip */
                } else if (*s == '[') {
                    s++;
                    char *e = strchr(s, ']');
                    if (!e) { error = 1; break; }
                    *e = '\0';
                    size_t sec_len = e - s;
                    if (sec_len >= sizeof(current_section)) sec_len = sizeof(current_section) - 1;
                    memcpy(current_section, s, sec_len);
                    current_section[sec_len] = '\0';
                } else {
                    char *eq = strchr(s, '=');
                    if (!eq) eq = strchr(s, ':');
                    if (!eq) { error = 1; break; }
                    char *name_end = eq - 1;
                    while (name_end >= s && isspace((unsigned char)*name_end)) name_end--;
                    name_end[1] = '\0';
                    char *val = eq + 1;
                    while (*val && isspace((unsigned char)*val)) val++;
                    size_t val_len = strlen(val);
                    while (val_len > 0 && isspace((unsigned char)val[val_len - 1])) val_len--;
                    val[val_len] = '\0';
                    if (handler) {
                        if (!handler(user, current_section, s, val))
                            { error = 1; break; }
                    }
                }
                line_len = 0;
                free(line);
                line = NULL;
            }

            p = nl + 1;
        }
    }

    free(line);
    fclose(f);
    return error ? 0 : 1;
}

int ini_parse_string(const char *string,
                     int (*handler)(void *user, const char *section,
                                    const char *name, const char *value),
                     void *user) {
    ini_string_reader r;
    r.ptr = string;
    r.len = strlen(string);
    r.pos = 0;
    return ini_parse_custom(ini_string_read, &r, handler, user);
}
