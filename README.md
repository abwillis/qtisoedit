# bkiso-qt

A Qt Widgets editor for ISO 9660 images built on **bkisofs**.

## Features

- Open ISO and NRG images. Rock Ridge is tried first, then Joliet, then plain ISO 9660.
- New image from scratch. Dropping files on an empty window also starts one.
- Add files and folders with the menu or by drag-and-drop from a file manager. If a name already exists, choose Replace, Replace All, Add with New Name, Skip or Skip All.
- New folder, rename, delete, extract (several items at once) and Rock Ridge permissions.
- Image properties: volume name, publisher, namespaces, item count and estimated size.
- El Torito: use a file in the image as a no-emulation boot file, add a hidden floppy or no-emulation boot record, extract the boot record, or remove it.
- Save As with optional Rock Ridge and Joliet. ISO 9660 is always written.
- Opening, adding, extracting and saving run on a worker thread with a progress dialog and a Cancel button. bkisofs "continue after this failure?" prompts appear as dialogs.

## Build (openSUSE Tumbleweed)

```bash
sudo zypper install cmake gcc-c++ qt6-widgets-devel qt6-concurrent-devel
```

Expected layout (override with `-DBKISOFS_SOURCE_DIR=...`):

```
work/
├── bkisofs/     <- bk.h, bk*.c
└── bkiso-qt/    <- this project
```

```bash
cd bkiso-qt
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure   # Qt-free IsoSession test
./build/bkiso-qt [image.iso]
```

By default the bkisofs sources are compiled into the executable as a static library.

### Testing against a prebuilt libbk.so

```bash
cmake -S . -B build-so \
      -DBKISOFS_SOURCE_DIR=$HOME/src/bkisofs \
      -DBKISOFS_LIBRARY=$HOME/src/bkisofs/libbk.so
cmake --build build-so
```

CMake sets the build RPATH, so the test binary finds the library in place without `LD_LIBRARY_PATH`. The upstream Makefile links `libbk.so` without a SONAME, so the binary records the full build path. Add `-Wl,-soname,libbk.so.1` (or similar) to the `libbk.so` rule before installing it system-wide.

Qt 5.12+ also works. CMake picks Qt 6 first and falls back to Qt 5.

## Layout

| File | Purpose |
|---|---|
| `src/isosession.*` | Qt-free wrapper around one `VolInfo`; owns callbacks, path rules and bkisofs workarounds |
| `src/operationrunner.*` | Modal progress dialog; runs one operation on a worker thread |
| `src/mainwindow.*` | Menus, tree, actions |
| `src/isotreewidget.*` | Tree items, sorting, file drop support |
| `src/dialogs.*` | Image properties and permissions dialogs |
| `tests/session_test.cpp` | Builds, reopens, edits, extracts and re-saves images through `IsoSession` |

## bkisofs behaviour handled by the wrapper

- `bk_write_image()` refuses to write over the image being read, so editing is always Save As.
- `bk_write_image()` calls its progress callback unconditionally, so a callback is always passed.
- `bk_add_as()` checks the destination for the *source* name instead of the new name. `IsoSession::addAs()` checks the new name and temporarily renames a clashing item.
- When a warning callback is installed, bkisofs reports `BKWARNING_OPER_PARTLY_FAILED` (or success, if the user chooses Continue) instead of the original error. Extraction therefore checks for existing names before it starts.
- `bk_get_error_string()` has no text for some codes (for example `BKERROR_NOT_REG_FILE_FOR_BR`); those are mapped explicitly.
- `bk_get_permissions()` and `bk_set_permissions()` do not handle `/`; the GUI never passes the root.
- `bk_add()` accepts only names made of ASCII 32–126, so files with non-ASCII names are reported as "invalid character" errors.

## License
**BSD 3-Clause License**.

    Copyright (c) 2026, abwillis

See [`LICENSE`](LICENSE) for the full text.

bkisofs is GPL v2. 

