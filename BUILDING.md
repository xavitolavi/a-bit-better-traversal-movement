# Building A Bit Better Traversal Movement

The mod is a single DLL built from this source. No game files are needed to build it and none are included here.

## Requirements
- Windows 10/11.
- FINAL FANTASY VII REBIRTH, Steam PC build of 28 Oct 2025 (game version 1.0.0.5), to run it. The DLL checks the build at startup and stays idle on any other version.
- Visual Studio 2022 with "Desktop development with C++" (MSVC x64).
- Python 3.11 (standard library only), for the proxy generator and the release script.

Run all commands from the repository root.

## 1. The DLL (`dwmapi.dll`)
```
dll\build.bat
```
This produces `dll\dwmapi.dll`. `build.bat` expects Visual Studio 2022 Community in its default folder; edit the `vcvars64.bat` path if yours is elsewhere. It:
- compiles `dll\version.rc` (the file's name/author/version metadata);
- assembles `dll\dwmapi_proxy.asm`, the forwarding stubs exported through `dll\dwmapi.def`;
- compiles and links the mod (`dll\TraversalSpeed.cpp`).

The game imports `DWMAPI.dll`, so Windows loads this `dwmapi.dll` from the game folder at startup. It forwards every dwmapi call to `System32\dwmapi.dll`, and the game exe is never modified.

The proxy sources are generated and committed. Regenerate them only if Windows' dwmapi.dll exports change:
```
python tools\gen_dwmapi_proxy.py
```

### Sign the DLL (recommended)
```
powershell -ExecutionPolicy Bypass -File tools\sign_dll.ps1
```
This generates a free, self-signed code-signing certificate in your user certificate store the first time (reused afterwards), then signs and timestamps `dll\dwmapi.dll`. The signature is real; because it is self-signed rather than issued by a certificate authority, Windows lists the publisher as unverified. It proves the file has not been altered since it was built and gives it a named publisher, which reduces antivirus false positives on an otherwise unsigned DLL. The public certificate is written to `tools\TraversalMovement-codesign.cer` (safe to share); the private key never leaves your certificate store.

## 2. The release zip
```
python tools\make_release.py v1.3.0
```
Checks that the DLL has its version metadata and a signature, then builds `release\A Bit Better Traversal Movement\` and `release\A_Bit_Better_Traversal_Movement_v1.0.0.zip` with:
- `End\Binaries\Win64\dwmapi.dll`
- `End\Binaries\Win64\TraversalSpeed.ini` (from `release\TraversalSpeed.ini`)
- `README.txt`, `LICENSE.txt`

When no ini exists, the DLL writes the same defaults itself (embedded in `TraversalSpeed.cpp` as `RELEASE_INI`).
