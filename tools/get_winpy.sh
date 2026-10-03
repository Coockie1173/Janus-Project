#!/bin/sh
# A Python for Windows with Tk into DIR/winpy/python: what the Windows package runs the
# launcher and the disc extraction with (nobody has to install Python there). The interpreter
# is python.org's NuGet package, Tk its installer's tcltk.msi (unpacked with Wine's msiexec),
# plus pycryptodome for the AES of .iso and .wbfs images; what the launcher never uses is left out.
#     sh tools/get_winpy.sh build/deps
set -e
DIR=${1:-build/deps}
VER=3.14.8
W=$DIR/winpy
[ -f "$W/python/python.exe" ] && { echo "Python for Windows in $W/python"; exit 0; }
rm -rf "$W"; mkdir -p "$W"; W=$(cd "$W" && pwd); cd "$W"
curl -sfL -o python.zip "https://api.nuget.org/v3-flatcontainer/python/$VER/python.$VER.nupkg"
curl -sfL -o tcltk.msi "https://www.python.org/ftp/python/$VER/amd64/tcltk.msi"
unzip -q python.zip -d nupkg && mv nupkg/tools python && rm -rf nupkg
mkdir tk
WINEDEBUG=-all wine msiexec /a tcltk.msi /qn TARGETDIR="Z:$(echo "$W/tk" | tr / '\\')" > /dev/null 2>&1
[ -f tk/DLLs/_tkinter.pyd ] || { echo "tcltk.msi did not unpack (needs wine)"; exit 1; }
cp -r tk/DLLs/. python/DLLs/ && cp -r tk/Lib/. python/Lib/ && cp -r tk/tcl python/tcl
rm -rf python/Lib/site-packages && mkdir python/Lib/site-packages
python3 -m pip download pycryptodome --platform win_amd64 --only-binary=:all: --python-version "${VER%.*}" \
    --no-deps -q -d wheel
unzip -q wheel/*.whl -d python/Lib/site-packages
# not needed to show a window and read a disc
cd python
rm -rf include libs Lib/test Lib/idlelib Lib/ensurepip Lib/venv Lib/turtledemo Lib/pydoc_data Lib/unittest \
       Lib/lib2to3 Lib/tkinter/test Lib/sqlite3 Lib/asyncio Lib/multiprocessing Lib/xmlrpc Lib/wsgiref \
       Lib/curses Lib/dbm Lib/_pyrepl Lib/__phello__ Lib/site-packages/Crypto/SelfTest \
       tcl/nmake tcl/*.lib tcl/*.sh tcl/dde* tcl/registry* DLLs/sqlite3.dll DLLs/_sqlite3.pyd \
       DLLs/libcrypto*.dll DLLs/libssl*.dll DLLs/_ssl.pyd DLLs/_hashlib.pyd DLLs/_testcapi*.pyd DLLs/_test*.pyd \
       DLLs/_msi.pyd DLLs/_asyncio.pyd DLLs/_multiprocessing.pyd DLLs/_overlapped.pyd DLLs/*.ico DLLs/*.cat
find . -name __pycache__ -prune -exec rm -rf {} +
find . -name '*.pdb' -delete
cd .. && rm -rf tk wheel python.zip tcltk.msi
echo "Python for Windows in $W/python ($(du -sh python | cut -f1))"
