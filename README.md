# Corbata PDF

A small, fast desktop app for page-level PDF editing, for Windows and macOS.
Pages are copied losslessly (nothing is re-rendered), so output quality is identical to the input.

## Features

| | |
|---|---|
| **Open** | File ▸ Open, Open Recent, drag PDFs onto the window, or pass files on the command line / "Open with" |
| **Merge** | File ▸ Merge PDFs adds files to the end; drag a PDF *between* two thumbnails to insert it right there; File ▸ Insert PDF After Selection |
| **Delete** | Select pages, press Delete |
| **Reorder** | Drag thumbnails (multi-select works), or Alt+← / Alt+→; Edit ▸ Reverse Order |
| **Rotate** | Ctrl+L / Ctrl+R for selected pages, Ctrl+Shift+L / Ctrl+Shift+R for all pages, 180° in the Pages menu |
| **Export** | Ctrl+E saves just the selected pages as a new PDF |
| **Split** | File ▸ Split into Files: one file per page, or every N pages |
| **Duplicate / Blank** | Ctrl+D duplicates selected pages, Ctrl+B inserts a blank page (same size as its neighbour) |
| **Undo / Redo** | Ctrl+Z / Ctrl+Y for every change until you close the document |
| **Default app** | File ▸ Make Default PDF App (also offered once on first launch). Windows sends you to Settings to confirm; macOS asks for confirmation |
| **Preview** | Double-click or Space; arrow keys step through pages |

Ctrl+mouse wheel (or the slider in the status bar) changes the thumbnail size. On macOS, Ctrl is ⌘.
Password-protected PDFs ask for the password; saved output is not encrypted.

## How it's built

* C++17, **Qt 6.8** (Widgets + Qt PDF for thumbnails), **qpdf** for all PDF writing, CMake.
* `src/PdfWriter.cpp` – assembles the output PDF from a list of page references (the only code that writes PDFs).
* `src/MainWindow.cpp` – actions, menus, undo (each change stores a snapshot of the page list).
* `src/PageGrid.cpp` – thumbnail grid, drag-and-drop, drop indicator.
* `src/Library.cpp` – opened files (kept in memory, so the original can be overwritten on save) and thumbnail rendering.
* `src/DefaultApp_*.cpp/.mm` – registering as the PDF handler (Windows registry under HKCU / macOS LaunchServices).
* `resources/make_icon.py` – draws the icon (white calligraphy "C" on red 179,11,0).
* `tests/test_writer.cpp` – checks merge/reorder/rotate/duplicate/blank/export output with qpdf.

## Building

### Windows
Prerequisites (one-time): Visual Studio 2022 Build Tools (C++), CMake, Python, then

```powershell
pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtpdf -O C:\Qt
git clone https://github.com/microsoft/vcpkg C:\vcpkg; C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install qpdf:x64-windows-static-md
```

Build, test and package (result in `dist\Corbata PDF\` and `dist\Corbata-PDF-windows.zip`):

```powershell
.\build-windows.ps1
```

### macOS
Run `./build-mac.sh` on a Mac with Qt 6.10.3 (`~/Qt/6.10.3/macos`) and vcpkg (`~/vcpkg`, with
`vcpkg install qpdf:arm64-osx --overlay-triplets=cmake/triplets`). The app needs macOS 13 or newer.
Or let GitHub build it: pushing to `main` runs `.github/workflows/build.yml`, which builds and tests both platforms and uploads
`Corbata-PDF-windows.zip` and `Corbata-PDF-macos.dmg` as run artifacts. Pushing a tag like `v1.0.0` also creates a GitHub Release.

The Mac app is ad-hoc signed, not notarized. The `.dmg` contains the app, an Applications shortcut and
[Install Corbata PDF.txt](resources/mac/Install%20Corbata%20PDF.txt), which explains how to get past Gatekeeper the first time.

### Windows Smart App Control
If Smart App Control is on, Windows blocks any unsigned program, including one you just built yourself.
Either turn it off (Windows Security ▸ App & browser control ▸ Smart App Control), or sign the exe with a
certificate Windows trusts (e.g. Azure Trusted Signing).
