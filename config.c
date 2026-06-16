#include "config.h"
#include "window.h"
#include "inih/ini.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static int config_handler(void *user, const char *section,
                          const char *name, const char *value)
{
    (void)user;

    if (strcmp(section, "statusbar") == 0) {
        if (strcmp(name, "background_color") == 0)
            app.sb_bg = (uint8_t)atoi(value);
        else if (strcmp(name, "foreground_color") == 0)
            app.sb_fg = (uint8_t)atoi(value);
        else if (strcmp(name, "position") == 0)
            strncpy(app.sb_position, value, sizeof(app.sb_position) - 1);
        else if (strcmp(name, "show_clock") == 0)
            app.show_clock = (strcmp(value, "yes") == 0 ||
                             strcmp(value, "true") == 0 ||
                             strcmp(value, "1") == 0);
        else if (strcmp(name, "clock_format") == 0)
            strncpy(app.clock_format, value, sizeof(app.clock_format) - 1);
        else if (strcmp(name, "clock_position") == 0)
            strncpy(app.clock_position, value, sizeof(app.clock_position) - 1);
        else if (strcmp(name, "window_brackets") == 0)
            strncpy(app.window_brackets, value, sizeof(app.window_brackets) - 1);
        else if (strcmp(name, "active_symbol") == 0)
            strncpy(app.active_symbol, value, sizeof(app.active_symbol) - 1);
        else if (strcmp(name, "show_hostname") == 0 || strcmp(name, "hostname") == 0)
            app.show_hostname = (strcmp(value, "yes") == 0 ||
                                strcmp(value, "true") == 0 ||
                                strcmp(value, "1") == 0 ||
                                strcmp(value, "ON") == 0 ||
                                strcmp(value, "on") == 0);
        else if (strcmp(name, "hostname_fg") == 0)
            app.hostname_fg = (uint8_t)atoi(value);
        else if (strcmp(name, "clock_fg") == 0)
            app.clock_fg = (uint8_t)atoi(value);
        else if (strcmp(name, "separator_fg") == 0)
            app.separator_fg = (uint8_t)atoi(value);
        else if (strcmp(name, "rename_fg") == 0)
            app.rename_fg = (uint8_t)atoi(value);
        else if (strcmp(name, "rename_bg") == 0)
            app.rename_bg = (uint8_t)atoi(value);
        else if (strcmp(name, "active_bracket_fg") == 0)
            app.active_bracket_fg = (uint8_t)atoi(value);
        else if (strcmp(name, "active_bracket_bg") == 0)
            app.active_bracket_bg = (uint8_t)atoi(value);
        else if (strcmp(name, "inactive_bracket_fg") == 0)
            app.inactive_bracket_fg = (uint8_t)atoi(value);
        else if (strcmp(name, "inactive_bracket_bg") == 0)
            app.inactive_bracket_bg = (uint8_t)atoi(value);
        else if (strcmp(name, "active_window_fg") == 0)
            app.active_window_fg = (uint8_t)atoi(value);
        else if (strcmp(name, "active_window_bg") == 0)
            app.active_window_bg = (uint8_t)atoi(value);
        else if (strcmp(name, "inactive_window_fg") == 0)
            app.inactive_window_fg = (uint8_t)atoi(value);
        else if (strcmp(name, "inactive_window_bg") == 0)
            app.inactive_window_bg = (uint8_t)atoi(value);

    } else if (strcmp(section, "shell") == 0) {
        if (strcmp(name, "command") == 0)
            strncpy(app.shell_cmd, value, sizeof(app.shell_cmd) - 1);
        else if (strcmp(name, "default_name") == 0)
            strncpy(app.default_name, value, sizeof(app.default_name) - 1);

    } else if (strcmp(section, "keybindings") == 0) {
        if (strcmp(name, "prefix") == 0 && strlen(value) > 0) {
            char c = (char)toupper((unsigned char)value[strlen(value) - 1]);
            app.prefix_key = c;
        }
    }

    return 1;
}

int config_load(const char *path)
{
    if (!path) return -1;
    return ini_parse(path, config_handler, NULL);
}
