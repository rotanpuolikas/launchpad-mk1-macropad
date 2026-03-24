# launc-macro

Launchpad Mini MK1 GIF display + macro pad. Runs on Linux and Windows. Probably works on newer launchpad models, but expect the colours to be wacky and wrong.

## Prebuilt binaries

You can find two prebuilt binaries in the project root, those being `launc-macro` and `launc-macro.exe`

I still highly recommend building this by yourself, it takes less than a minute, but if you're really lazy you can use the prebuilts. I don't judge.

## Building

### Linux

**Dependencies**

```bash
# Arch
sudo pacman -S alsa-lib cmake git base-devel

# Debian/Ubuntu
sudo apt install libasound2-dev cmake git build-essential
```

`inih` and `stb` are fetched automatically by CMake at configure time.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# binary: build/launc-macro
```

### Windows

**Requirements:** [MinGW-w64](https://www.mingw-w64.org/) (e.g. via [MSYS2](https://www.msys2.org/)) and CMake. No extra libraries needed — WinMM, user32, and shell32 are part of Windows.

From an MSYS2 MinGW64 shell:

```bash
# Install tools (first time only)
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake git

cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# binary: build/launc-macro.exe
```

### GUI version (launc-macro-gui)

A graphical front-end built with [raylib](https://www.raylib.com/) + [raygui](https://github.com/raysan5/raygui). It shows a live visual of the Launchpad grid, lets you inspect button configs, and controls the macro pad / GIF playback without a terminal.

**Additional dependencies** (raylib, raygui, inih, stb are all fetched automatically by CMake):

```bash
# Arch — extra X11/GL libraries for raylib
sudo pacman -S libx11 libxrandr libxinerama libxcursor libxi mesa

# Debian/Ubuntu
sudo apt install libx11-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libgl1-mesa-dev
```

**Linux:**

```bash
cmake -S gui -B build-gui -DCMAKE_BUILD_TYPE=Release
cmake --build build-gui -j$(nproc)
# binary: build-gui/launc-macro-gui
```

**Windows (MSYS2 MinGW64 shell):**

```bash
cmake -S gui -B build-gui -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-gui -j$(nproc)
# binary: build-gui/launc-macro-gui.exe
```

**Cross-compile for Windows from Linux:**

```bash
cmake -S gui -B build-gui-win \
    -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc"
cmake --build build-gui-win -j$(nproc)
# binary: build-gui-win/launc-macro-gui.exe
```

See [BUILD_GUI.txt](BUILD_GUI.txt) for full details and dependency notes.

## Setup

### Linux — uinput access (key/media actions)

Key and media actions require write access to `/dev/uinput`.

```bash
# Option A — add yourself to the input group (relogin after)
sudo usermod -aG input $USER

# Option B — udev rule (no group change needed)
echo 'KERNEL=="uinput", GROUP="input", MODE="0660"' \
    | sudo tee /etc/udev/rules.d/99-uinput.rules
sudo udevadm trigger
```

If uinput is unavailable, the program still runs but key/media actions are silently disabled.

### Windows — key/media actions

`SendInput` is used directly; no extra setup is required. Run as a normal user.

## Usage

```
launc-macro                           # macro-pad mode
launc-macro animation.gif             # GIF animation + macro-pad
launc-macro animation.gif -f 15       # override playback fps
launc-macro animation.gif -m red      # colour filter: full | red | green | yellow
launc-macro -c /path/to/my.conf       # custom config file
launc-macro --edit                    # open config in default app (xdg-open / Explorer)
launc-macro --edit vim                # open config in a specific editor
launc-macro --help                    # show all options
```

Press **Ctrl+C** to exit.

## Config file

The config is an INI file created automatically on first run:

- **Linux:** `~/.config/launchpad-macro/launchpad.conf`
- **Windows:** `%APPDATA%\launchpad-macro\launchpad.conf`

```ini
[settings]
default_color         = black
default_color_pressed = green_low

# Assign a button by its ID: top_N (0–7), side_N (0–7), grid_R_C (0–7 each)
[grid_0_0]
color         = black
color_pressed = red_low
action        = key:ctrl+c

[top_0]
color  = yellow_low
action = media:play_pause

[side_7]
color  = green_med
action = app:firefox
```

**Action prefixes:**

| Prefix | Example | Description |
|--------|---------|-------------|
| `key:` | `key:ctrl+shift+s` | Send a key combo |
| `media:` | `media:volume_up` | Send a media key |
| `app:` | `app:firefox` | Run a shell command (non-blocking) |

**Colours:** `black`, `red_low`, `red_med`, `red_max`, `green_low`, `green_med`, `green_max`, `yellow_low`, `yellow_med`, `yellow_max`

**Media keys:** `play_pause`, `next`, `prev`, `volume_up`, `volume_down`, `mute`

**GIF overlay:** set `gif_overlay = true` on a button to keep it lit and responsive while a GIF plays in the background.

## AI usage

This project has been partially written by Claude, and this README.md has mostly been written by Claude as well. I would never write a README this comprehensive.

The entire GUI part has been written by Claude Code. I take no credit for the coding of that.
