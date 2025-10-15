/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  Copyright (C) 2006-2016 XNeur Team
 *
 */

#include <X11/XKBlib.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "xneur.h"

#include "window.h"
#include "keymap.h"

#include "types.h"
#include "utils.h"
#include "log.h"

#include "switchlang.h"

extern struct _window *main_window;

// Try to switch GNOME input source using gdbus/gsettings. Returns non-zero on success.
static int try_switch_via_gnome(int layout_group)
{
    const char *js_patterns[] = {
        "imports.ui.status.keyboard.getInputSourceManager().inputSources[%d].activate()", // GNOME 3.x/40
        "global.get_input_source_manager().inputSources[%d].activate()",                 // GNOME 42+
        "Main.inputMethod.inputSources[%d].activate()"                                   // Some downstream variants
    };

    char cmd[512];
    for (size_t i = 0; i < sizeof(js_patterns) / sizeof(js_patterns[0]); i++) {
        char js[256];
        snprintf(js, sizeof(js), js_patterns[i], layout_group);
        snprintf(cmd, sizeof(cmd),
                 "gdbus call --session --dest org.gnome.Shell --object-path /org/gnome/Shell --method org.gnome.Shell.Eval \"%s\"",
                 js);
        log_message(DEBUG, cmd);
        if (system(cmd) == 0) {
            return 1;
        }
    }

    // Fallback to gsettings (supported by GNOME)
    snprintf(cmd, sizeof(cmd),
             "gsettings set org.gnome.desktop.input-sources current %d",
             layout_group);
    log_message(DEBUG, cmd);
    if (system(cmd) == 0) {
        return 1;
    }

    return 0;
}

int get_curr_keyboard_group(void)
{
    const char *sess = getenv("XDG_SESSION_TYPE");
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    int prefer_gsettings = (desktop && strstr(desktop, "GNOME") != NULL);

    if (prefer_gsettings || (sess && strcmp(sess, "wayland") == 0)) {
        FILE *fp = popen("gsettings get org.gnome.desktop.input-sources current", "r");
        if (fp) {
            char out[64] = {0};
            if (fgets(out, sizeof(out), fp) != NULL) {
                int idx = 0;
                for (char *p = out; *p; ++p) {
                    if (*p >= '0' && *p <= '9') { idx = (int)strtol(p, NULL, 10); break; }
                }
                pclose(fp);
                return idx;
            }
            pclose(fp);
        }
    }

    // Fallback to XKB state
    XkbStateRec xkbState;
    XkbGetState(main_window->display, XkbUseCoreKbd, &xkbState);
    return xkbState.group;
}

void set_keyboard_group(int layout_group)
{
    const char *sess = getenv("XDG_SESSION_TYPE");
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    int is_x11 = (!sess || strcmp(sess, "x11") == 0);
    int is_gnome = (desktop && strstr(desktop, "GNOME") != NULL);

    // Always attempt GNOME switch if GNOME is present (works on both Wayland/Xorg)
    if (is_gnome) {
        try_switch_via_gnome(layout_group);
    }

    // Also try native XKB to affect X11/XWayland clients
    XkbLockGroup(main_window->display, XkbUseCoreKbd, layout_group);
}

void set_next_keyboard_group(struct _xneur_handle *handle)
{
    int total = (handle && handle->total_languages > 0) ? handle->total_languages : 2;
    int current = get_curr_keyboard_group();
    int new_layout_group = (current + 1) % total;
    log_message (DEBUG, "handle->total_languages = %d", handle ? handle->total_languages : -1);

    set_keyboard_group(new_layout_group);
}

void set_prev_keyboard_group(struct _xneur_handle *handle)
{
    int total = (handle && handle->total_languages > 0) ? handle->total_languages : 2;
    int current = get_curr_keyboard_group();
    int new_layout_group = (current - 1 + total) % total;
    log_message (DEBUG, "handle->total_languages = %d", handle ? handle->total_languages : -1);

    set_keyboard_group(new_layout_group);
}
