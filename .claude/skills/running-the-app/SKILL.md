---
name: running-the-app
description: Use when launching, driving, or screenshotting the Obscura-Safe-Vault GUI (osv) — verifying a UI change visually, reproducing a screen, or checking a dialog. Covers headless launch on a Wayland dev box, synthetic input, and the file-dialog hazard.
---

# Running Obscura-Safe-Vault

SDL3 desktop app. Launching it headlessly needs four non-obvious settings; each
one below cost a failed attempt to discover. The binary is `build/bin/Debug/osv`.

## ⚠️ File dialogs escape the sandbox — read this first

`SDL_ShowOpenFileDialog` does **not** draw a dialog. It calls the **XDG desktop
portal over D-Bus**, a *session* service. The app window renders to your virtual
display, but the dialog is created by the developer's **real desktop session and
appears on their actual screen.**

**Never press a key that opens a file dialog while driving headlessly.** On the
vault manager that is `N` (new vault) and `O` (open other); in the gallery it is
`I`, `Z`, `O` (imports) and `X` (export).

Consequence: any flow that needs a file dialog — creating a vault, opening one,
importing an archive — **cannot be driven headlessly here.** To reach a vault
screen, pre-seed a vault file plus `vaults.list` under the isolated config dir
instead of clicking through. If a dialog does appear on the user's screen, kill
the app immediately (`pkill -x osv`) and tell them.

## Launch

```bash
# 1. Sanitizer runs leave the binary instrumented — rebuild plain first, or the
#    app aborts on a libgallium data race that has nothing to do with your change.
scripts/gen.sh && scripts/build.sh

# 2. Virtual display. Wait for it — querying too early looks like a dead display.
Xvfb :77 -screen 0 1400x900x24 -nolisten tcp &
until DISPLAY=:77 xdotool getdisplaygeometry >/dev/null 2>&1; do sleep 0.5; done

# 3. Launch. All four env vars are load-bearing (see below).
RUN=/tmp/osvrun; mkdir -p "$RUN/cfg" "$RUN/data"
DISPLAY=:77 XDG_CONFIG_HOME="$RUN/cfg" XDG_DATA_HOME="$RUN/data" \
  SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software LIBGL_ALWAYS_SOFTWARE=1 \
  ./build/bin/Debug/osv > "$RUN/app.log" 2>&1 &

# 4. Wait for the window to be MAPPED. Screenshotting before this is the single
#    most common way to get a black frame and misdiagnose it as a launch failure.
for i in $(seq 1 20); do
  DISPLAY=:77 xdotool search --all --name '' 2>/dev/null | while read w; do
    DISPLAY=:77 xdotool getwindowpid $w >/dev/null 2>&1 && echo ready; done | grep -q ready && break
  sleep 0.5
done
```

| Setting | Why — omit it and you get |
|---|---|
| `SDL_VIDEODRIVER=x11` | Dev box is Wayland; SDL3 picks the Wayland backend, ignores `DISPLAY=:77`, and never appears on your virtual display |
| `SDL_RENDER_DRIVER=software` + `LIBGL_ALWAYS_SOFTWARE=1` | `amdgpu_query_info(ACCEL_WORKING) failed (-13)` under Xvfb → no window is ever mapped, and a root-window screenshot is pure black |
| `XDG_CONFIG_HOME` / `XDG_DATA_HOME` | The app writes to the developer's real vault registry (`vaults.list`) and prefs |

## Find the window and screenshot

```bash
export DISPLAY=:77
W=$(for w in $(xdotool search --all --name '' 2>/dev/null); do
      xdotool getwindowpid $w >/dev/null 2>&1 && echo $w; done | head -1)
xdotool windowfocus $W
import -window $W /tmp/osvrun/shot.png
```

Find the window by **having a PID**, not by name: Xvfb's root window matches a
name search, has no PID, and screenshots black. A black frame means the app never
mapped a window — that is a launch failure, not a capture problem.

## Send input

```bash
xdotool windowfocus $W          # no WM runs, so focus must be set explicitly
xdotool key --clearmodifiers n  # XTEST — note: NO --window
xdotool type --delay 40 'text'
```

`xdotool key --window $W` sends **synthetic** events (`send_event=true`), which
SDL filters — the keypress silently does nothing. XTEST (omitting `--window`)
injects real input and works.

## Quick reference

| Task | Command |
|---|---|
| Check display is up | `DISPLAY=:77 xdotool getdisplaygeometry` — **`xdpyinfo` is not installed**, so a readiness check using it always fails |
| Kill the app | `pkill -x osv` — **never** `pkill -f osv`: `-f` matches your own shell command line and kills the tool call |
| Kill the display | `pkill -x Xvfb` |
| App log | empty on a clean run; SDL/driver errors land here |
| Prove the shot isn't black | `identify -format '%k unique colours\n' shot.png` — a real screen has hundreds (743 for the vault manager); **1** means a black frame |

## Common mistakes

- **Screenshot is black** → app never mapped a window. Check the env vars above,
  and confirm you captured a window with a PID rather than the Xvfb root.
- **Keys do nothing** → you used `--window`. Drop it; use XTEST plus
  `xdotool windowfocus`.
- **App aborts with a ThreadSanitizer data race in libgallium** → the binary is
  still instrumented from `scripts/test.sh --tsan|--asan`. Rebuild plain.
- **A dialog appears on the developer's screen** → you pressed a dialog key. Kill
  the app and say so; do not retry.
