@echo off
rem Builds dwmapi.dll (A Bit Better Traversal Movement). Regenerate the proxy sources with tools\gen_dwmapi_proxy.py only if
rem Windows' dwmapi.dll exports change.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
rc /nologo /fo version.res version.rc || exit /b 1
ml64 /nologo /c /Fo dwmapi_proxy.obj dwmapi_proxy.asm || exit /b 1
cl /nologo /LD /O2 /MT /EHa /std:c++17 /W3 /Zi /D_CRT_SECURE_NO_WARNINGS TraversalSpeed.cpp dwmapi_proxy.obj version.res /Fe:dwmapi.dll /link /DLL /DEF:dwmapi.def /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:%%_PDB%% kernel32.lib user32.lib || exit /b 1
