#!/usr/bin/env python3
"""The game's files out of a disc image (.rvz, .iso, .wbfs, .wia), into the game folder.

    python3 tools/extract_disc.py IMAGE [OUT]     (./launch.sh --extract IMAGE, before there is a build)
    python3 tools/extract_disc.py IMAGE --info    what the image is, nothing written

OUT is ./game by default. The image has to be GoldenEye 007, NTSC-U (SJBE52): the build is
made from that executable. The files go into OUT.extracting first and take OUT's place when
all of them are there; a game folder that is already filled is kept unless --force is given,
and then it is moved aside (OUT.old), never deleted.

Reading is wiikit's (wiikit/disc.py, rvz.py). An .rvz holds the data decrypted; an .iso or
.wbfs is AES-encrypted, which wants pycryptodome or the system's libcrypto to be quick.
"""
import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path[:0] = [HERE, ROOT] #the package keeps wiikit beside this file

GAME_ID = "SJBE52"
IMAGES = (".rvz", ".iso", ".wbfs", ".wia")


class ExtractError(Exception):
    pass


def have_game(out):
    return os.path.isfile(os.path.join(out, "sys", "main.dol")) and os.path.isdir(os.path.join(out, "files"))


def use_libcrypto():
    """AES through OpenSSL's libcrypto when pycryptodome is not there: wiikit's own is 0.5 MB/s."""
    from wiikit import aes
    if aes._AES is not None:
        return "pycryptodome"
    import ctypes
    import ctypes.util
    name = ctypes.util.find_library("crypto")
    if not name:
        return None
    try:
        lib = ctypes.CDLL(name)
        lib.EVP_CIPHER_CTX_new.restype = ctypes.c_void_p
        lib.EVP_aes_128_cbc.restype = ctypes.c_void_p
        lib.EVP_DecryptInit_ex.argtypes = [ctypes.c_void_p] * 3 + [ctypes.c_char_p] * 2
        lib.EVP_CIPHER_CTX_set_padding.argtypes = [ctypes.c_void_p, ctypes.c_int]
        lib.EVP_DecryptUpdate.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int),
                                          ctypes.c_char_p, ctypes.c_int]
        lib.EVP_CIPHER_CTX_free.argtypes = [ctypes.c_void_p]
    except (OSError, AttributeError):
        return None

    def cbc_decrypt(key, iv, data, pure=False):
        ctx = lib.EVP_CIPHER_CTX_new()
        try:
            out = ctypes.create_string_buffer(len(data) + 16)
            n = ctypes.c_int(0)
            if (lib.EVP_DecryptInit_ex(ctx, lib.EVP_aes_128_cbc(), None, bytes(key), bytes(iv)) != 1
                    or lib.EVP_CIPHER_CTX_set_padding(ctx, 0) != 1
                    or lib.EVP_DecryptUpdate(ctx, out, ctypes.byref(n), bytes(data), len(data)) != 1
                    or n.value != len(data)):
                raise ExtractError("libcrypto could not decrypt the disc")
            return out.raw[:n.value]
        finally:
            lib.EVP_CIPHER_CTX_free(ctx)

    aes.cbc_decrypt = cbc_decrypt
    return "libcrypto"


def empty_folders(out):
    """The disc's folders that hold no file, made as Dolphin's extraction makes them."""
    import struct
    fst = open(os.path.join(out, "sys", "fst.bin"), "rb").read()
    total = struct.unpack_from(">I", fst, 8)[0]
    stack = [(total, "")]
    for i in range(1, total):
        while i >= stack[-1][0]:
            stack.pop()
        word, _, end = struct.unpack_from(">III", fst, i * 12)
        if word >> 24:
            at = total * 12 + (word & 0xFFFFFF)
            path = stack[-1][1] + fst[at:fst.index(b"\0", at)].decode("latin1") + "/"
            os.makedirs(os.path.join(out, "files", *path.split("/")), exist_ok=True)
            stack.append((end, path))


def inspect(image):
    """(game id, title, kind) of an image; ExtractError when it cannot be read."""
    from wiikit import disc, rvz
    try:
        d = disc.open_disc(image)
        h = disc.header(d)
    except SystemExit as e:
        raise ExtractError(str(e))
    except Exception as e:
        raise ExtractError(f"not a disc image this can read ({e})")
    if not h["wii"]:
        raise ExtractError("not a Wii disc image")
    kind = "WBFS" if isinstance(d, disc.Wbfs) else d.kind if isinstance(d, rvz.Rvz) else "ISO"
    return h["game_id"], h["title"], kind


def extract(image, out, force=False, progress=None, log=print):
    """Fill `out` from `image`. progress(done bytes, total bytes, file name) is called as it goes;
    raising from it stops the extraction (what was written is removed)."""
    from wiikit import disc
    if not os.path.isfile(image):
        raise ExtractError(f"no such file: {image}")
    game_id, title, kind = inspect(image)
    if game_id != GAME_ID:
        raise ExtractError(f"this is {game_id} '{title}', not GoldenEye 007 NTSC-U ({GAME_ID}): "
                           "the build only runs that version")
    if have_game(out) and not force:
        raise ExtractError(f"{out} already holds the game (--force replaces it; the old one is kept as {out}.old)")
    fast = "none needed" if kind in ("RVZ", "WIA") else use_libcrypto()
    if fast is None:
        log("no pycryptodome and no libcrypto: decrypting in Python, which takes hours. An .rvz does not need it.")
    try:
        _, part = disc.open_partition(image)
        sizes = {rel: size for rel, _, size in part.files()}
    except SystemExit as e:
        raise ExtractError(str(e))
    total = sum(sizes.values())
    parent = os.path.dirname(os.path.abspath(out)) or "."
    os.makedirs(parent, exist_ok=True)
    free = shutil.disk_usage(parent).free
    if free < total + (64 << 20):
        raise ExtractError(f"not enough room: the game is {total / 1e9:.1f} GB, {free / 1e9:.1f} GB are free there")
    log(f"{kind}  {game_id}  '{title}': {len(sizes)} files, {total / 1e9:.2f} GB")

    tmp = out.rstrip("/\\") + ".extracting"
    shutil.rmtree(tmp, ignore_errors=True)
    done = [0, 0, ""] #bytes read, bytes at the last report, the file just finished
    kind_of = type(part)
    plain_read = kind_of.read

    def counted_read(self, off, size): #wiikit reads a file in 4 MB pieces: progress inside the big packs
        data = plain_read(self, off, size)
        done[0] = min(done[0] + len(data), total) #the few MB of sys/ are counted too: close enough
        if progress and done[0] - done[1] >= (16 << 20):
            done[1] = done[0]
            progress(done[0], total, done[2])
        return data

    def each(line): #wiikit says "  path" after each file
        done[2] = line.strip()

    try:
        if progress:
            progress(0, total, "")
        kind_of.read = counted_read
        disc.extract(image, tmp, log=each)
        empty_folders(tmp)
        if progress:
            progress(total, total, "")
    except SystemExit as e:
        shutil.rmtree(tmp, ignore_errors=True)
        raise ExtractError(str(e))
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    finally:
        kind_of.read = plain_read
    if not have_game(tmp):
        shutil.rmtree(tmp, ignore_errors=True)
        raise ExtractError("the image gave no sys/main.dol: it is damaged or not a game disc")

    if os.path.isdir(out):
        if have_game(out) or any(os.path.isdir(os.path.join(out, n)) for n in os.listdir(out)):
            old, n = out.rstrip("/\\") + ".old", 1
            while os.path.exists(old):
                n += 1
                old = out.rstrip("/\\") + f".old{n}"
            os.rename(out, old)
            log(f"the folder that was there is now {old}")
        else: #only the package's note in it
            shutil.rmtree(out)
    os.rename(tmp, out)
    log(f"the game's files are in {out}")
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("image")
    ap.add_argument("out", nargs="?", default=os.path.join(ROOT, "game"))
    ap.add_argument("--info", action="store_true")
    ap.add_argument("--force", action="store_true", help="replace a game folder that is already filled")
    a = ap.parse_args()
    try:
        if a.info:
            game_id, title, kind = inspect(a.image)
            print(f"{kind}  {game_id}  '{title}'" + ("" if game_id == GAME_ID else f"  (not {GAME_ID})"))
            return 0
        last = [-1]

        def progress(done, total, rel):
            pct = done * 100 // max(total, 1)
            if pct != last[0]:
                last[0] = pct
                print(f"\r{pct:3d}%  {done / 1e9:.2f} / {total / 1e9:.2f} GB", end="", flush=True)

        def log(text):
            print(("\n" if last[0] >= 0 else "") + text)
            last[0] = -1

        extract(a.image, a.out, a.force, progress, log)
        return 0
    except ExtractError as e:
        print(f"\nextract: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nextract: stopped, nothing kept", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
