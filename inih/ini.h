#ifndef INI_H
#define INI_H

#define INI_STOP (0)
#define INI_SUCCESS (1)

int ini_parse(const char *filename,
              int (*handler)(void *user, const char *section,
                             const char *name, const char *value),
              void *user);

int ini_parse_string(const char *string,
                     int (*handler)(void *user, const char *section,
                                    const char *name, const char *value),
                     void *user);

#endif
