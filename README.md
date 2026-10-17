# Taskbar Drop Pins

[![Release](https://img.shields.io/github/v/release/muhammetozeski/TaskbarDropPins)](https://github.com/muhammetozeski/TaskbarDropPins/releases/latest)
[![Windows 11](https://img.shields.io/badge/Windows-11-0078D4)](https://github.com/muhammetozeski/TaskbarDropPins)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Pin files, folders and `.lnk` shortcuts by dragging them from File Explorer or the desktop onto the **native Windows 11 taskbar**.

Drop an item among your application icons, click it to open, and drag its taskbar button to reorder it. The mod keeps the Windows 11 taskbar in place.

## Features

- Files open with their associated Windows application; folders open in File Explorer.
- `.lnk` targets, arguments, working directories and saved window settings are preserved.
- Shortcuts to the same executable can remain separate taskbar items.
- Item names stay readable; internal identifiers are stored separately.
- Icons preserve their original colors and transparency.
- Original files and folders stay where they are.
- Existing pins continue to open after Windhawk is closed. Their native buttons can still be reordered.
- Hold **Shift** to retain ordinary Windows file dragging instead of adding a pin.

## Requirements

- Windows 11, x64.
- [Windhawk](https://windhawk.net/).

Verified with Windhawk 1.7.3 on Windows 11 26H2, build 26300.9711. Dragging from File Explorer and the desktop is the verified source workflow. The mod uses private Windows Shell interfaces for pinning and positioning.

## Install

1. Download `taskbar-drop-pins.wh.cpp` from the [latest release](https://github.com/muhammetozeski/TaskbarDropPins/releases/latest), or extract it from `TaskbarDropPins-v1.0.0.zip`.
2. Create a local mod in Windhawk.
3. Replace its template with the entire downloaded file, then compile and enable it.
4. Drag a file, folder or `.lnk` shortcut onto the taskbar.

The release file is self-contained: it includes the small native launcher's bytes, so it does not download another program. Windhawk compiles the mod for its own installed engine. No .NET runtime is required.

The executables are digitally signed. To let Windows verify the signature, run `Install-Certificate.cmd` from `SignatureTrust.zip` once. The programs run without it; only the signature stays unverified. The signed executable is the launcher embedded inside the release source.

## How it works

The mod wraps Explorer's `DoDragDrop` source. When a filesystem item is released over the taskbar, it cancels the original file transfer and submits a separate pin request. This keeps the source file in place.

An item-specific `.lnk` targets a small native launcher, which opens the saved target using `ShellExecuteExW` and exits. Each item has a stable `AppUserModel.ID`. A copy of a dropped `.lnk` is retained so Windows can apply the original shortcut's launch settings.

Shell PinManager inserts the genuine taskbar pin. A read-only accessibility query supplies the existing buttons' positions; native taskbar behavior handles later manual reordering. The mod also retains an OLE target adapter; Explorer's source hook is the verified path on the tested Windows build.

Icons are written as lossless 32-bit image frames in `.ico` containers, retaining transparency. Runtime errors are recorded with Windhawk's logger and diagnostic values. An original drag is never replayed after it has begun.

## Storage and removal

The launcher, generated links, copied `.lnk` targets and icons live in the permanent directory returned by Windhawk's `Wh_GetModStoragePath`. They are outside this source repository and outside temporary folders.

- `links/<identity>/<item name>.lnk`: generated links.
- `targets/<identity>.lnk`: preserved input shortcuts.
- `icons/<identity>-v2.ico`: full-color icons.
- `TaskbarDropPins.Launcher-v1.exe`: the shared launcher.

Disable the mod in Windhawk to stop new drag conversions. Unpin individual items using the normal Windows taskbar menu. Existing pins need their launcher and saved target/icon files, so disabling the mod does not remove that data.

## Build

Use PowerShell 7 and an installed Windhawk compiler:

```powershell
.\Build.ps1 -WindhawkPath 'C:\Program Files\Windhawk'
```

`dist/taskbar-drop-pins.wh.cpp` is the self-contained mod source; `dist/taskbar-drop-pins.dll` is for the matching local Windhawk engine. The original launcher code is in [src/launcher.cpp](src/launcher.cpp).

For a signed build, pass `-SigningScript <absolute path to your signing script> -RequireSignature`. The script must accept the executable path as its first argument. Signing happens **before** the launcher is embedded. No signing certificate or private key is included in the repository.

`Install.ps1 -WindhawkPath <absolute path>` installs a compiled build into **portable Windhawk**. Standard installed Windhawk users should use the local-mod workflow above.

`Test.ps1 -WindhawkPath <absolute path> -TestDirectory <empty temporary directory>` verifies real Shell data objects, Unicode and spaced paths, `.lnk` preservation, stable identities, full-color icon pixel equality, and drag exception handling without replaying an operation. The desktop/taskbar integration was also manually verified.

`Publish.ps1 -TrustDirectory <absolute path to the public certificate bundle>` packages the signed build and writes SHA-256 checksums. It includes only explicitly selected distribution files.

## Source layout

- [src/taskbar-drop-pins.wh.cpp](src/taskbar-drop-pins.wh.cpp): mod lifecycle, Shell data conversion, links and icons.
- [src/source-drop.h](src/source-drop.h): Explorer drag source, operation boundary and worker.
- [src/shell-pins.h](src/shell-pins.h): private Shell adapter and taskbar position reading.
- [src/launcher.cpp](src/launcher.cpp): short-lived native launcher.
- [tests/shell-contract.cpp](tests/shell-contract.cpp): Shell and drag contract checks.

## License

[MIT](LICENSE).