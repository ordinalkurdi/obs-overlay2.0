# OBS Overlay (native OBS Studio plugin)

The Python/PySide6 "OBS Overlay" app, rebuilt as a C++17 / Qt 6 plugin on top of
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). It runs **inside** OBS, so there is no
WebSocket, no password, no separate process and no installer.

## Install the sources into the template

1. Create your repo from the template (GitHub: *Use this template*), clone it.
2. Copy this folder's `src/`, `tests/`, `data/`, `CMakeLists.txt`, `.clang-format` over the template's files
   (delete the template's `src/plugin-main.c`). Keep the template's `cmake/`, `.github/`, `build-aux/`,
   `CMakePresets.json`, `LICENSE` (GPL-2.0, which is what a libobs plugin must be).
3. In `buildspec.json` change only the metadata: `name` = `obs-overlay`, `displayName` = `OBS Overlay`,
   `version`, `author`, `website`, `email`. Leave `dependencies` and `platformConfig` as the template has them.
4. Build exactly as the template describes, e.g. Windows:
   `cmake --preset windows-x64` then `cmake --build --preset windows-x64`.
   (`CMakeLists.txt` forces `ENABLE_FRONTEND_API` and `ENABLE_QT` on, and turns warnings-as-errors off for this target.)

Requires OBS 31+ (frontend screenshot events, Qt 6). Unit tests of the pure logic need no OBS:
`g++ -std=c++17 -Isrc/core tests/test_core.cpp -o t && ./t` (or `-DBUILD_OVL_TESTS=ON`).

## What maps to what

| Python app | Plugin |
|---|---|
| OBS WebSocket (host, port, password, keyring) | direct libobs + obs-frontend-api calls on the UI thread |
| pynput hotkeys + conflict detection | OBS hotkeys (`obs_hotkey_register_frontend`): rebind in *OBS Settings > Hotkeys*, entries start with "OBS Overlay:". Only the four OBS has no equivalent for are registered (overlay, widgets, click-through, edit layout); record, replay, screenshot and mute use OBS's own hotkeys |
| `core/service.py` worker thread + Qt signals | `Service` (synchronous, no locking: events arrive on the UI thread) |
| `core/layouts.py`, `widget_rows.py`, `audio.py`, `webdocks.py` | `src/core/*.hpp`, pure C++, **unit-tested** |
| qtawesome icons | Lucide-style inline SVG icons (hand-drawn, rendered with QtSvg) |
| psutil CPU/RAM | Win32 / `/proc` / Mach calls, plus OBS process CPU (new "OBS CPU" metric) |
| Screenshots via WebSocket | `obs_frontend_take_screenshot()`, then moved/converted to your folder, format, quality and name pattern |
| Preview (still image on a timer) | removed from the plugin (OBS already has its own preview) |
| Audio meters | real `obs_volmeter` levels, same ballistics (attack/release/peak hold) |
| Tray-only app | Tools menu entry + optional tray icon + OBS hotkeys |

All other features are ported: docked/floating panel with slide animation, monitor edge reveal, pin, resize/move,
notifications in screen centre, record/pause/stream/replay/save/screenshot/virtual camera, scenes, source
show/hide/lock, mixer (mute, volume, activity, mic/desktop roles), gallery (recursive scan, search/filter/sort,
favorites, rename, delete to recycle bin, thumbnails), settings (all sections, import/export/reset), floating widgets
(stats, recording, stream, scene, audio status; move, resize with scaling text, opacity, pin, click-through), layout edit
mode with snap guides and chips, layout presets (save/load/rename/overwrite/delete, modified detection).

## Differences you should know about

* **Web docks**: OBS plugins cannot embed Chromium (QtWebEngine is not shipped with OBS). A "web link" widget therefore
  shows the address with *Open in browser*, *Copy URL* and *Add as OBS dock* (opens OBS's own **Custom Browser Docks**
  dialog with the URL copied). Presets for Twitch chat / Stream Manager are kept. Web zoom/transparency fields are
  stored in presets but have no effect.
* **Video playback**: QtMultimedia is not available either, so videos open in your default player; images open in a viewer.
  Video durations are not shown.
* **Audio list** follows the scene (sources live in the current scene plus OBS's global devices); the eye button hides one by hand, "Show hidden..." restores; "Hide inactive" is a setting.
* **Gallery video thumbnails** are decoded with the FFmpeg that ships in OBS's prebuilt deps (found by CMake); without it Windows falls back to Explorer thumbnails.
* **Dropped %** now means OBS's network-dropped frames of the stream output (what OBS's status bar shows).
* The first Windows build still needs your eyes (see below): UI paths such as flags and click-through are the same Qt
  calls as before, but OBS's own event loop is now the host.

## Verification status (honest)

* `src/core/{layouts,widget_rows,weburl,audio_math}.hpp` and the Linux branch of `sysmetrics.cpp` were compiled with
  `g++ -Wall -Wextra` and the 58 checks in `tests/test_core.cpp` pass (ports of your layout/preset tests, plus rows,
  web URL validation, audio roles/ballistics, snapping).
* Everything that needs Qt or libobs (`live_backend.cpp`, `service`, `settings`, `gallery`, `hotkeys`, all of `src/ui`,
  `app.cpp`, `plugin-main.cpp`, `CMakeLists.txt`) was written against the Qt 6 / libobs 31 APIs **but not compiled**:
  the sandbox had neither SDK. Expect a handful of compile errors or missing includes on the first build; send me the
  compiler output and I will fix them. Runtime behaviour in a real OBS is untested.
