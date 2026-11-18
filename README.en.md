![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Windows](https://img.shields.io/badge/Windows-10%2F11-blue.svg)
![License](https://img.shields.io/badge/license-MIT-brightgreen.svg)

[Russian](README.md) | **English**

# Linkbar

Linkbar is a fast and lightweight quick-launch bar for the Windows desktop.
Written from scratch in C++20: only WinAPI, GDI and the system GDI+, with no
third-party libraries and no VCL.

## Features

- Dock to the left, top, right or bottom edge of the screen.
- Multiple independent panels (each one runs in its own process).
- Any files and folders in the panel's working directory, not just shortcuts.
- Drag files from Explorer: copy or move them (`Shift`).
- Configurable icon size (1–1024), margins, order, alignment and text.
- Global text color plus **per-shortcut settings**: custom color and text
  visibility (right-click → Linkbar).
- Light, dark and accent themes; transparency, glass and AeroGlass.
- Auto-hide on hover, left or right click; global hotkey.
- Explorer context menu and Jump Lists with tasks, pinned and recent items.
- Modern dark color picker: saturation/value field, hue and alpha bars, live
  preview, HEX (`RRGGBB` / `AARRGGBB`). The chosen color is applied immediately
  and saved even if the dialog is closed without OK.
- Panel backdrop: Solid, Acrylic, Mica, Blur or fully transparent, with
  adjustable intensity. An image (PNG/JPEG/BMP/ICO) or an animated GIF can be
  used, with configurable placement (stretch/fit/tile/center) and opacity.
- Optional animations and automatic icon enlargement near the cursor
  (configurable radius).
- Independent panels per monitor: monitor number, edge, size and all other
  settings are stored in a separate `.lbr` profile.
- Settings are stored in a plain `.lbr` profile.
- Localization from `Locales\*.ini` (13 languages).
- Warning-free `/W4` build; Release ≈ 0.5 MB with a static CRT.

## Build

Requires an MSVC toolchain with C++20 support, the Windows SDK and CMake 3.20+.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

The result is `build/Release/linkbar.exe`; the `Locales` folder is copied next
to it automatically.

## Usage

```text
linkbar.exe                  open all discovered panels
linkbar.exe -f <file.lbr>    open a specific profile
linkbar.exe -n               create a new panel
linkbar.exe -ca              close all panels
linkbar.exe -l <locale>      interface language (e.g. ru-RU)
linkbar.exe -d <ms>          startup delay in milliseconds
```

## Settings

Open them from the panel context menu: **Settings…**. Tabs: panel, items,
auto-hide, additional. Text color is global; per-shortcut options are in the
item's **Linkbar** submenu: "Show text", "Text color…", "Default text color".

## Layout

| Path | Purpose |
|---|---|
| `src/` | source code (WinAPI, GDI, GDI+) |
| `Locales/` | localization files |
| `ico/` | application icon |
| `resource.rc`, `linkbar.manifest` | resources and manifest |
| `CMakeLists.txt` | build configuration |

## License

[MIT](LICENSE). Original idea and project: [Asaq / Linkbar](https://github.com/ATGH15102AFMLD/Linkbar).
