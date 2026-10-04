#!/bin/sh
# Build OpenSSL for Windows from source into DIR/openssl-win, static, with llvm-mingw
# (tools/get_mingw.sh first): what the network part (wiikit's /dev/net/ssl) links on make win.
#     sh tools/build_openssl.sh build/deps
set -e
DIR=${1:-build/deps}
VER=3.5.4
mkdir -p "$DIR"
cd "$DIR"
DIR=$PWD
[ -d openssl-src ] || {
    curl -sL -o openssl.tar.gz "https://github.com/openssl/openssl/releases/download/openssl-$VER/openssl-$VER.tar.gz"
    mkdir openssl-src
    tar -xf openssl.tar.gz -C openssl-src --strip-components=1
    rm openssl.tar.gz
}
PATH=$DIR/llvm-mingw/bin:$PATH
export PATH
rm -rf openssl-build-win
mkdir openssl-build-win
cd openssl-build-win
# only the libraries: no programs, no tests, no engines or modules to load at run time
../openssl-src/Configure mingw64 --cross-compile-prefix=x86_64-w64-mingw32- --prefix="$DIR/openssl-win" \
    --libdir=lib --openssldir=ssl no-shared no-apps no-tests no-docs no-module no-engine \
    > ../openssl-win-config.log 2>&1
make -j"$(nproc)" > ../openssl-win-build.log 2>&1
make install_sw >> ../openssl-win-build.log 2>&1
echo "OpenSSL $VER for Windows installed in $DIR/openssl-win"
