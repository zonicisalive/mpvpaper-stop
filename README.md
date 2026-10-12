![Preview](https://github.com/pvtoari/mpvpaper-stop/blob/master/media/preview.gif)
# mpvpaper-stop

> **Note:** This project is an enhanced fork of [pvtoari/mpvpaper-stop](https://github.com/pvtoari/mpvpaper-stop), maintained by [Zonic](https://github.com/ZonicExists). Upgraded with native **Niri** compositor support, **multi-screen independent display control** within a single daemon, **auto-reconnecting lifecycle**, and performance optimizations.

`mpvpaper-stop` is a lightweight, high-performance background utility written in C designed to work with [mpvpaper](https://github.com/GhostNaN/mpvpaper). It automatically pauses `mpvpaper` when any window becomes visible on your active workspace and resumes playback when no windows are displayed.

---

## What's New in this Fork

* **Native Niri Compositor Support**: Full JSON-RPC IPC communication and real-time `"EventStream"` socket integration.
* **Multi-Screen Independent Management (Single Daemon)**: One single `mpvpaper-stop` process can manage multiple displays simultaneously (e.g. `DP-1`, `DP-2`, `HDMI-A-1`). It pauses only the monitor that has windows open, while allowing empty monitors to keep playing!
* **Auto-Reconnecting Lifecycle**: Runs continuously in the background and automatically connects/reconnects whenever `mpvpaper` starts, stops, or restarts without crashing.
* **Performance & Zero CPU Footprint**: Uses persistent domain sockets, fast-path workspace checks, kernel `select()` blocking (0.0% CPU when idle), and compiled with `-O3 -flto`.
* **Automatic Compositor Detection**: Automatically detects whether you are running **Niri** or **Hyprland** at runtime.

---

## Supported Compositors
* **Niri** (via native JSON-RPC IPC & real-time `"EventStream"`)
* **Hyprland** (via native IPC & `.socket2.sock` event streams)
* **Auto-detection**: Automatically detects your active compositor at runtime.

---

## How it works

1. **Real-time Event Listening & Polling**: Connects to the compositor's event stream for zero-delay reaction upon window open/close, focus, or workspace switch.
2. **Per-Monitor Window Count Detection**: Queries the active/focused workspace on each output to check if windows are covering that specific monitor.
3. **Independent Pause/Resume**:
   - **Screen with windows**: Sends a pause command to that monitor's `mpvpaper` socket (`/tmp/mpvsocket-<output>`).
   - **Screen without windows**: Resumes playback on that monitor's socket.
4. **Color Generation**: Optionally triggers color palette extractors (`matugen` or `pywal`) using the paused frame.

---

## Dependencies

* **[mpvpaper](https://github.com/GhostNaN/mpvpaper)**: Running with `--input-ipc-server` enabled.
* **libcjson**: `cJSON` library (required for build).
* **Niri** or **Hyprland**
* *(Optional)*: `matugen` or `pywal` (for dynamic color theming on pause)

---

## Build and Install

```bash
# 1. Setup project with meson
meson setup build

# 2. Compile with optimizations
meson compile -C build

# 3. Install to system or local bin
sudo meson install -C build
# Or copy locally:
cp build/mpvpaper-stop ~/.local/bin/mpvpaper-stop
```

---

## Usage Examples

### 1. Single Monitor (or Single Shared Wallpaper across all screens)

```bash
# Start mpvpaper
mpvpaper -o "--input-ipc-server=/tmp/mpvsocket --loop" ALL /path/to/video.mp4 &

# Start mpvpaper-stop daemon
mpvpaper-stop -f
```

### 2. Multi-Screen with Independent Wallpapers (e.g. 3 Displays)

Start separate `mpvpaper` instances per display with matching socket names:

```bash
# Screen 1 (DP-1)
mpvpaper -o "--input-ipc-server=/tmp/mpvsocket-DP-1 --loop" DP-1 bg1.mp4 &

# Screen 2 (DP-2)
mpvpaper -o "--input-ipc-server=/tmp/mpvsocket-DP-2 --loop" DP-2 bg2.mp4 &

# Screen 3 (HDMI-A-1)
mpvpaper -o "--input-ipc-server=/tmp/mpvsocket-HDMI-A-1 --loop" HDMI-A-1 bg3.mp4 &

# Start ONE single daemon to manage all 3 screens independently:
mpvpaper-stop -f
```

When you open an application on Screen 1, **only Screen 1 pauses**; Screens 2 & 3 **keep playing**!

---

## Configuration & Autostart

### In Niri (`~/.config/niri/config.kdl` or `config.d/50-startup.kdl`):
```kdl
spawn-at-startup "mpvpaper-stop" "-f"
```

### In Hyprland (`~/.config/hypr/hyprland.conf`):
```ini
exec-once = mpvpaper-stop -f
```

---

## Command-Line Options

```
Usage: mpvpaper-stop [options]
Options:
  -v, --verbose              Enables verbose output
  -f, --fork                 Forks the process into the background
  -o, --output NAME          Target specific monitor output (e.g. DP-1, ALL) (default: ALL)
  -p, --socket-path PATH     Path to the mpvpaper socket (default: /tmp/mpvsocket)
  -w, --socket-wait-time TIME Wait time for the socket in milliseconds (default: 5000)
  -t, --period TIME          Polling period in milliseconds (default: 1000)
  -C, --compositor NAME      Compositor to use: niri, hyprland, or auto (default: auto)
  -c <color_backend>         Chooses color backend (pywal or matugen)
  --pywal                    Runs pywal on pause
  --matugen                  Runs matugen on pause
  -h, --help                 Shows this help message
```

---

## Maintainer & Acknowledgments

* **Maintained by**: [Zonic](https://github.com/ZonicExists)
* **Original Creator**: [@pvtoari](https://github.com/pvtoari) at [pvtoari/mpvpaper-stop](https://github.com/pvtoari/mpvpaper-stop).
* **License**: Licensed under the **MIT License**. See `LICENSE` for details.

