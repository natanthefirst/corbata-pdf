# Corbata PDF

A small, fast desktop app to read, fill in, sign and organize PDFs, for Windows and macOS.
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

Corbata PDF has three modes, switched with the buttons in the toolbar. Opening a PDF shows it in View mode.

**View** (Ctrl+1): read the document. Nothing on the page can be changed here.

| | |
|---|---|
| **Read** | One page at a time. ← / → (or ◀ ▶, Page Up / Page Down, or the mouse wheel when the whole page fits) turn pages |
| **Zoom** | **Fit Page** (the whole page) or **Fit Width** (as wide as the window, scroll for the rest); both follow the window size. Ctrl+mouse wheel or − / + for any other size |
| **Go to a page** | Type its number in the page box and press Enter |
| **Search** | Ctrl+F or **Search**: matches are highlighted; Enter / Shift+Enter (or F3 / Shift+F3) go to the next / previous one, across pages; Esc closes |
| **Copy text** | Drag over text to select it, Ctrl+C to copy; Ctrl+A selects the whole page |

**Fill & Sign** (Ctrl+2): the same page view, with tools

| | |
|---|---|
| **Fill in forms** | PDFs with fillable fields: click a field to type, tick boxes, pick radio buttons and drop-down values (Tab moves to the next field). Fields stay fillable after saving |
| **Write on any page** | **Text**: click and type. **✓ Check**, **✗ Cross**, **Date**: one click. Size and black / blue ink in the bar |
| **Sign** | **Sign…**: draw, type or import your signature (the last few are remembered), then click where it goes. Clicking a signature box in a form places it there |
| **Change it** | Drag to move, drag the corner to resize, Delete to remove, double-click text to retype. Saving burns these into the page |

**Edit** (Ctrl+3): work with whole pages

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
| **Back to reading** | Double-click a page (or press Space) to open it in View |

**Print** (Ctrl+P or File ▸ Print): all pages, a range, or the current page, with filled-in fields and
Fill & Sign marks; each page is scaled (and turned if needed) to fit the paper.

In every mode: **Undo / Redo** (Ctrl+Z / Ctrl+Y) for every change until you close the document, and
File ▸ Make Default PDF App (also offered once on first launch; Windows sends you to Settings to confirm, macOS asks).

In Edit, Ctrl+mouse wheel (or the slider in the status bar) changes the thumbnail size. On macOS, Ctrl is ⌘.
Password-protected PDFs ask for the password; saved output is not encrypted.

## For developers

Everything below is only needed to change and build the app yourself, not to use it.

### How it's built

* C++17, **Qt 6.8** (Windows) / **Qt 6.10** (Mac) (Widgets + Qt PDF for thumbnails), **qpdf** for all PDF writing, CMake.
* `src/PdfWriter.cpp` – assembles the output PDF from a list of page references (the only code that writes PDFs):
  copies pages, burns in Fill & Sign marks, fills form fields and carries the form (`/AcroForm`) over.
* `src/MainWindow.cpp` – actions, menus, undo (each change stores a snapshot of the pages and form values).
* `src/DocumentView.cpp` – the page view used by View and Fill & Sign (tools and form filling only in Fill & Sign); `src/SignatureDialog.cpp` – draw / type / import a signature. `src/Printing.cpp` – printing.
* `src/Marks.cpp` – text, check marks and signatures, drawn identically on screen and in the PDF.
* `src/FormFields.cpp` – reads fillable form fields with qpdf.
* `src/PageGrid.cpp` – thumbnail grid, drag-and-drop, drop indicator.
* `src/Library.cpp` – opened files (kept in memory, so the original can be overwritten on save) and thumbnail rendering.
* `src/DefaultApp_*.cpp/.mm` – registering as the PDF handler (Windows registry under HKCU / macOS LaunchServices).
* `resources/make_icon.py` – draws the icon (white calligraphy "C" on red 179,11,0).
* `tests/test_writer.cpp` – checks merge/reorder/rotate/duplicate/blank/export output with qpdf.
* `tests/test_fill.cpp` – checks marks, form filling and saving, and (rendering with Qt PDF) that marks land in the right place.

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
`Corbata-PDF-windows.zip` and `Corbata-PDF-macos.dmg` as run artifacts. Pushing a tag like `v2.0.0` also creates a GitHub Release.

The Mac app is ad-hoc signed, not notarized.

### Signing
The Windows exe is unsigned, so Smart App Control blocks it, even when you built it yourself. Signing it with a
certificate Windows trusts (e.g. Azure Trusted Signing) fixes that. Notarizing the Mac app needs an Apple Developer account.
