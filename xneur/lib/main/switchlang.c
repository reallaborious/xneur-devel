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
#include <X11/extensions/XTest.h>

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

// Extract first shortcut string from GNOME keybinding array, e.g. "['<Super>space']"
static int get_gnome_switch_shortcut(char *out, size_t outlen)
{
    FILE *fp = popen("gsettings get org.gnome.desktop.wm.keybindings switch-input-source", "r");
    if (!fp) return 0;
    char buf[256] = {0};
    if (!fgets(buf, sizeof(buf), fp)) { pclose(fp); return 0; }
    pclose(fp);
    // Find first quoted entry
    char *start = strchr(buf, '\'');
    if (!start) return 0;
    start++;
    char *end = strchr(start, '\'');
    if (!end || end <= start) return 0;
    size_t len = (size_t)(end - start);
    if (len >= outlen) len = outlen - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

// Send a shortcut like "<Super>space" or "Super+space"
static void send_shortcut(Display *dpy, const char *accel)
{
    // Collect modifiers
    int mod_super=0, mod_shift=0, mod_alt=0, mod_ctrl=0;
    const char *p = accel;
    while (p && (*p=='<' || *p=='S' || *p=='s' || *p=='A' || *p=='a' || *p=='C' || *p=='c' || *p=='M' || *p=='m')) {
        const char *gt = strchr(p, '>');
        const char *plus = strchr(p, '+');
        size_t tok_len = 0; const char *tok_end = NULL;
        if (p[0] == '<' && gt) { tok_end = gt; tok_len = (size_t)(gt - (p+1)); p = gt + 1; }
        else if (plus) { tok_end = plus; tok_len = (size_t)(plus - p); p = plus + 1; }
        else break;
        if (tok_len == 0) break;
        char tok[16]={0}; if (tok_len > sizeof(tok)-1) tok_len = sizeof(tok)-1; memcpy(tok, (tok_end==gt? (tok_end - tok_len): (tok_end - tok_len)), tok_len);
        // Normalize
        for (size_t i=0;i<tok_len;i++){ if (tok[i]>='A'&&tok[i]<='Z') tok[i]= (char)(tok[i]-'A'+'a'); }
        if (strstr(tok, "super")) mod_super=1; else if (strstr(tok, "shift")) mod_shift=1; else if (strstr(tok, "alt")) mod_alt=1; else if (strstr(tok, "ctrl")||strstr(tok, "control")) mod_ctrl=1;
    }
    // Remaining part is key name
    const char *keyname = p ? p : accel;
    // Normalize key
    char key[32]={0}; size_t kl = strlen(keyname); if (kl>sizeof(key)-1) kl=sizeof(key)-1; memcpy(key, keyname, kl);
    for (size_t i=0;i<kl;i++){ if (key[i]>='A'&&key[i]<='Z') key[i]=(char)(key[i]-'A'+'a'); }
    KeySym ks = XK_VoidSymbol;
    if (strcmp(key, "space")==0) ks = XK_space; else if (strcmp(key, "tab")==0) ks = XK_Tab; else if (strcmp(key, "grave")==0) ks = XK_grave; else if (strlen(key)==1) ks = (KeySym)key[0];
    KeyCode kc = (ks==XK_VoidSymbol)? 0 : XKeysymToKeycode(dpy, ks);
    KeyCode kc_super = XKeysymToKeycode(dpy, XK_Super_L);
    KeyCode kc_shift = XKeysymToKeycode(dpy, XK_Shift_L);
    KeyCode kc_alt   = XKeysymToKeycode(dpy, XK_Alt_L);
    KeyCode kc_ctrl  = XKeysymToKeycode(dpy, XK_Control_L);
    if (mod_super) XTestFakeKeyEvent(dpy, kc_super, True, 0);
    if (mod_shift) XTestFakeKeyEvent(dpy, kc_shift, True, 0);
    if (mod_alt)   XTestFakeKeyEvent(dpy, kc_alt,   True, 0);
    if (mod_ctrl)  XTestFakeKeyEvent(dpy, kc_ctrl,  True, 0);
    if (kc) { XTestFakeKeyEvent(dpy, kc, True, 0); XTestFakeKeyEvent(dpy, kc, False, 0); }
    if (mod_ctrl)  XTestFakeKeyEvent(dpy, kc_ctrl,  False, 0);
    if (mod_alt)   XTestFakeKeyEvent(dpy, kc_alt,   False, 0);
    if (mod_shift) XTestFakeKeyEvent(dpy, kc_shift, False, 0);
    if (mod_super) XTestFakeKeyEvent(dpy, kc_super, False, 0);
    XFlush(dpy);
    usleep(50000);
}

static void try_reach_group_with_shortcuts(int desired)
{
    Display *dpy = main_window->display;
    // 1) Use configured GNOME shortcut if available
    char accel[128]={0};
    if (get_gnome_switch_shortcut(accel, sizeof(accel))) {
        for (int i=0;i<6;i++){ if (get_curr_keyboard_group()==desired) return; send_shortcut(dpy, accel); }
    }
    // 2) Try Super+space (GNOME default)
    for (int i=0;i<6;i++){ if (get_curr_keyboard_group()==desired) return; send_shortcut(dpy, "<Super>space"); }
    // 3) Try Alt+Shift (popular XKB toggle)
    for (int i=0;i<6;i++){ if (get_curr_keyboard_group()==desired) return; send_shortcut(dpy, "Alt+Shift"); }
}

void set_keyboard_group(int layout_group)
{
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    int is_gnome = (desktop && strstr(desktop, "GNOME") != NULL);

    // 1) GNOME activation (works on Xorg/Wayland)
    if (is_gnome) {
        try_switch_via_gnome(layout_group);
    }
    // 2) XKB force
    XkbLockGroup(main_window->display, XkbUseCoreKbd, layout_group);
    // 3) Verify and drive with shortcuts if needed
    if (get_curr_keyboard_group() != layout_group) {
        try_reach_group_with_shortcuts(layout_group);
    }
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
