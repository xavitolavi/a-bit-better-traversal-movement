r"""Build the release folder + zip.
usage: python tools/make_release.py [version]   (default v1.3.0)

The zip mirrors the game folder (End\...), so players copy the "End" folder into the game folder.
Needs: dll/dwmapi.dll, built with dll\build.bat and signed with tools\sign_dll.ps1."""
import os, shutil, subprocess, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = sys.argv[1] if len(sys.argv) > 1 else 'v1.3.0'
REL = os.path.join(ROOT, 'release')
OUT = os.path.join(REL, 'A Bit Better Traversal Movement')
DLL = os.path.join(ROOT, 'dll', 'dwmapi.dll')

# don't ship a DLL without its version metadata or signature
cmd = ("$ErrorActionPreference='Stop'; $f='%s';"
       "Write-Output ([bool]((Get-Item -LiteralPath $f).VersionInfo.ProductName));"
       "Write-Output ([bool]((Get-AuthenticodeSignature -LiteralPath $f).SignerCertificate))" % DLL.replace("'", "''"))
meta = subprocess.run(['powershell', '-NoProfile', '-Command', cmd], capture_output=True, text=True)
assert meta.stdout.split()[:2] == ['True', 'True'], (
    r'dwmapi.dll is missing version metadata or a signature - run dll\build.bat then tools\sign_dll.ps1. (%r)'
    % (meta.stdout + meta.stderr))

if os.path.exists(OUT):
    shutil.rmtree(OUT)
w64 = os.path.join(OUT, 'End', 'Binaries', 'Win64')
os.makedirs(w64)
shutil.copy2(DLL, os.path.join(w64, 'dwmapi.dll'))
shutil.copy2(os.path.join(REL, 'TraversalSpeed.ini'), w64)
shutil.copy2(os.path.join(REL, 'README.txt'), OUT)
shutil.copy2(os.path.join(REL, 'CHANGELOG.txt'), OUT)
shutil.copy2(os.path.join(ROOT, 'LICENSE'), os.path.join(OUT, 'LICENSE.txt'))

zp = os.path.join(REL, 'A_Bit_Better_Traversal_Movement_%s.zip' % VERSION)
with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as z:
    for dp, _, fs in os.walk(OUT):
        for fn in fs:
            full = os.path.join(dp, fn)
            z.write(full, os.path.relpath(full, OUT))
print('zip:', zp, os.path.getsize(zp) // 1024, 'KB')
with zipfile.ZipFile(zp) as z:
    for i in z.infolist(): print('  ', i.filename, i.file_size)
