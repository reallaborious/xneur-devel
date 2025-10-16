# Ubuntu 22.04 GNOME layout switching issue (XNeur)

## Summary
On Ubuntu 22.04.4 (GNOME 42, x11 session, XKB options include `grp:lalt_lshift_toggle`), XNeur converts text but does not switch the system keyboard layout after the hotkey (Pause). The problem is that switching the input source in GNOME is not reliably triggered by XKB-only calls for some apps/shell paths; GNOME’s source remains unchanged (`gsettings get ... current` stays `0`) and XKB also does not flip for the active client.

## Repro
- System: Ubuntu 22.04.4 LTS, GNOME Shell 42.x, session type: x11
- Layouts: us, ru (also a duplicate ru in `setxkbmap -query`)
- XNeur hotkey: Pause -> Convert last word
- Behavior: word converts (e.g., `english` -> `утпдшыр`), but active layout stays US.

## Observations
- `gsettings get org.gnome.desktop.input-sources current` remains `uint32 0` before and after the operation.
- `setxkbmap -query` shows `layout: us,ru,ru` and option `grp:lalt_lshift_toggle`.
- dbus GNOME Shell Eval calls of the form
  `gdbus call ... "imports.ui.status.keyboard.getInputSourceManager().inputSources[idx].activate()"`
  returned `(false, '')` on user’s GNOME 42 session, indicating the eval path is disabled/blocked or API mismatch.
- XKB-only `XkbLockGroup` does not reliably flip for the foreground client in this environment.

## What we changed in branch `fix/ubuntu-22.04-layout-switch`
1) Hardened startup
   - Guarded XOpenDisplay and XKB name queries; exit cleanly if DISPLAY/XKB not available.
   - Robust HOME detection for `~/.xneur` (fallback to passwd if HOME unset).
2) Switching logic (`xneur/lib/main/switchlang.c`)
   - Read current group via GSettings on GNOME/Wayland; fallback to XKB state
   - Attempt GNOME input-source activation via gdbus/gsettings (non-fatal)
   - Always apply XKB `XkbLockGroup` afterward to help X11/XWayland clients
3) Packaging & CI
   - Added GitHub Actions workflows (full deps + minimal Ubuntu 22.04 job)
   - Uploaded a self-contained bundle with local `libxneur.so.20` and `libxnconfig.so.20` and RPATH to avoid ABI mixups
   - Added `scripts/build-local.sh` (relative paths, dependency checks only) producing `./out` bundle
   - Ignored `build-local/` and `out/` in `.gitignore` to prevent pull conflicts

## Results so far
- XNeur runs fine and converts text reliably.
- On the user’s GNOME 42 session, neither gdbus Eval nor XKB calls consistently flip the reported GNOME source or the effective client layout.
- Shortcut synthesis experiments showed that sending raw printable keys is unsafe (caused stray letters). We removed that path.

## Hypotheses
- GNOME Shell 42 disables org.gnome.Shell.Eval by default; input source manager API may be inaccessible.
- The environment’s effective switch binding is configured as `grp:lalt_lshift_toggle` for XKB, but application focus/latched modifiers or timing prevent reliable acceptance of group changes.
- Duplicate `ru` in XKB group chain (us, ru, ru) may create group index inconsistencies.

## Next steps (proposed)
1) Prefer the actual GNOME keybinding for `switch-input-source` when GNOME is present:
   - Parse it from `gsettings get org.gnome.desktop.wm.keybindings switch-input-source`
   - Only synthesize modifier+non-printable keys (e.g., `<Super>space`, `<Alt><Shift>`) using XTest; avoid any printable characters
   - Add small XSync delays and verify active source after each send
2) Normalize layout list in handle creation:
   - Collapse duplicates to ensure stable mapping between group indices and GNOME sources
3) Add an explicit fallback for X11 sessions:
   - Issue one `XkbLockGroup` and one Alt+Shift toggle to cover both state machines
4) Expose a setting in `xneurrc` to choose switch method preference order (GNOME-first vs XKB-first) for affected environments

## How to build & run locally
- Minimal features:
  ```bash
  scripts/build-local.sh
  ./out/xneur
  ```
- Full features (sound/notify/spell):
  ```bash
  scripts/build-local.sh --full
  ./out/xneur
  ```

## Troubleshooting commands
- Session/desktop: `echo $XDG_SESSION_TYPE; echo $XDG_CURRENT_DESKTOP`
- GNOME input source index: `gsettings get org.gnome.desktop.input-sources current`
- XKB: `setxkbmap -query`
- GNOME switch keybinding: `gsettings get org.gnome.desktop.wm.keybindings switch-input-source`

## Status
- Root cause likely tied to GNOME Shell 42 input source handling and disabled Eval API.
- Implemented safer switching order and guards; further work needed to synthesize the configured GNOME switch shortcut without generating stray text and to deduplicate XKB groups.
