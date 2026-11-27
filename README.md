# Corbata PDF

A small, fast desktop app for page-level PDF editing, for Windows and macOS.
Pages are copied losslessly (nothing is re-rendered), so output quality is identical to the input.

## Download and install

Get the latest version from the [Releases page](https://github.com/natanthefirst/corbata-pdf/releases/latest).
You don't need to install anything else: everything the app needs is included.

### Windows (Windows 10 or 11, 64-bit)
1. Download **Corbata-PDF-windows.zip**.
2. Right-click it ▸ **Extract All…**, and put the folder wherever you like (for example `Documents` or `C:\Program Files`).
3. Open the folder and double-click **Corbata PDF.exe**. Keep the other files next to it; the app needs them.

The app isn't signed yet, so Windows may warn you the first time:
* "Windows protected your PC" (SmartScreen): click **More info** ▸ **Run anyway**.
* If **Smart App Control** is on, Windows blocks unsigned apps completely, with no way to run them
  until the app is signed (or Smart App Control is turned off in Windows Security ▸ App & browser control).

To uninstall, delete the folder.

### Mac (macOS 13 or newer, Apple Silicon M1 or later)
1. Download **Corbata-PDF-macos.dmg** and open it.
2. Drag **Corbata PDF** onto the **Applications** shortcut next to it.
3. The first time you open it, macOS blocks it because it isn't notarized by Apple yet. Follow
   [Install Corbata PDF.txt](resources/mac/Install%20Corbata%20PDF.txt) (also inside the `.dmg`) to allow it once.

### Make it your default PDF app
The first time the app starts, it offers to become your default app for PDF files; or choose
**File ▸ Make Default PDF App** later. Windows sends you to Settings to confirm; macOS asks you to confirm.

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

## For developers

Everything below is only needed to change and build the app yourself, not to use it.

### How it's built

* C++17, **Qt 6.8** (Windows) / **Qt 6.10** (Mac) (Widgets + Qt PDF for thumbnails), **qpdf** for all PDF writing, CMake.
* `src/PdfWriter.cpp` – assembles the output PDF from a list of page references (the only code that writes PDFs).
* `src/MainWindow.cpp` – actions, menus, undo (each change stores a snapshot of the page list).
* `src/PageGrid.cpp` – thumbnail grid, drag-and-drop, drop indicator.
* `src/Library.cpp` – opened files (kept in memory, so the original can be overwritten on save) and thumbnail rendering.
* `src/DefaultApp_*.cpp/.mm` – registering as the PDF handler (Windows registry under HKCU / macOS LaunchServices).
* `resources/make_icon.py` – draws the icon (white calligraphy "C" on red 179,11,0).
* `tests/test_writer.cpp` – checks merge/reorder/rotate/duplicate/blank/export output with qpdf.

### Building on Windows
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

### Building on macOS
Run `./build-mac.sh` on a Mac with Qt 6.10.3 (`~/Qt/6.10.3/macos`) and vcpkg (`~/vcpkg`, with
`vcpkg install qpdf:arm64-osx --overlay-triplets=cmake/triplets`). The app needs macOS 13 or newer.
Or let GitHub build it: pushing to `main` runs `.github/workflows/build.yml`, which builds and tests both platforms and uploads
`Corbata-PDF-windows.zip` and `Corbata-PDF-macos.dmg` as run artifacts. Pushing a tag like `v1.0.0` also creates a GitHub Release.

The Mac app is ad-hoc signed, not notarized.

### Signing
The Windows exe is unsigned, so Smart App Control blocks it, even when you built it yourself. Signing it with a
certificate Windows trusts (e.g. Azure Trusted Signing) fixes that. Notarizing the Mac app needs an Apple Developer account.
