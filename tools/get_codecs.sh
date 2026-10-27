#!/bin/sh
# The decompressors the launcher's disc reader needs for .rvz and .wia images, into DIR/codecs:
# sources only, compiled with the game (launcher/launcher.cmake). zstd (BSD), bzip2 (its own
# BSD-like license) and the LZMA SDK's decoders (public domain).
#     sh tools/get_codecs.sh build/deps
set -e
DIR=${1:-build/deps}/codecs
ZSTD=1.5.6 BZIP2=1.0.8 LZMA=2409 LZMA_TAG=24.09
mkdir -p "$DIR"
cd "$DIR"
if [ ! -d zstd ]; then
    curl -fsSL "https://github.com/facebook/zstd/releases/download/v$ZSTD/zstd-$ZSTD.tar.gz" | tar xz
    mv "zstd-$ZSTD" zstd
fi
if [ ! -d bzip2 ]; then
    curl -fsSL "https://sourceware.org/pub/bzip2/bzip2-$BZIP2.tar.gz" | tar xz
    mv "bzip2-$BZIP2" bzip2
fi
if [ ! -d lzma ]; then
    curl -fsSL -o lzma.7z "https://github.com/ip7z/7zip/releases/download/$LZMA_TAG/lzma$LZMA.7z"
    mkdir lzma
    (cd lzma && 7z x -y ../lzma.7z C DOC > /dev/null)
    rm lzma.7z
fi
echo "zstd, bzip2 and the LZMA SDK in $DIR"
