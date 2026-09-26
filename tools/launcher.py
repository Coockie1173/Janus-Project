#!/usr/bin/env python3
"""The window before the game: settings and key bindings, then Play.

    python3 tools/launcher.py            (./launch.sh opens it)
    python3 tools/launcher.py --check    load and save everything once, no window: a self-test

Settings are kept in build/settings.env (read by launch.sh), the bindings in
build/keys.txt (the runtime's key file: only the [Classic Controller] lines
listed here are rewritten, the rest of the file is kept as it is).
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
KEYS = os.path.join(ROOT, "build", "keys.txt")
KEYS_DEFAULT = os.path.join(ROOT, "config", "keys.txt")
SETTINGS = os.path.join(ROOT, "build", "settings.env")

# what the game does with each Classic Controller button (its default layout), and the key file's name for it
ACTIONS = [
    ("Fire", "ZR"), ("Aim down the sight", "ZL"), ("Melee attack", "L"), ("Throw grenade", "R"),
    ("Reload", "Y"), ("Crouch  (menus: select)", "A"), ("Action / sprint  (menus: back)", "B"),
    ("Cycle grenades", "X"), ("Next weapon", "Right"), ("Previous weapon", "Left"),
    ("Firing mode", "Up"), ("Smartphone", "Down"), ("Night vision / scoreboard", "Minus"),
    ("Pause", "Plus"), ("Home menu", "Home"),
    ("Move forward", "Left Stick Up"), ("Move back", "Left Stick Down"),
    ("Move left", "Left Stick Left"), ("Move right", "Left Stick Right"),
]
DEFAULTS = {"fps": "auto", "fov": "49", "mouse": "1.0", "online": "1", "fullscreen": "0"}

# Tk's key names -> SDL's (the key file's)
TK_TO_SDL = {
    "Return": "Return", "space": "Space", "Tab": "Tab", "BackSpace": "Backspace", "Delete": "Delete",
    "Shift_L": "Left Shift", "Shift_R": "Right Shift", "Control_L": "Left Ctrl", "Control_R": "Right Ctrl",
    "Alt_L": "Left Alt", "Alt_R": "Right Alt", "Caps_Lock": "CapsLock",
    "Up": "Up", "Down": "Down", "Left": "Left", "Right": "Right",
    "Insert": "Insert", "Home": "Home", "End": "End", "Prior": "PageUp", "Next": "PageDown",
    "KP_Enter": "Keypad Enter", "comma": ",", "period": ".", "slash": "/", "semicolon": ";",
    "apostrophe": "'", "bracketleft": "[", "bracketright": "]", "backslash": "\\", "minus": "-",
    "equal": "=", "grave": "`",
}
MOUSE = {1: "Mouse Left", 2: "Mouse Middle", 3: "Mouse Right", 8: "Mouse X1", 9: "Mouse X2"}


def sdl_name(keysym):
    if keysym in TK_TO_SDL:
        return TK_TO_SDL[keysym]
    if len(keysym) == 1 and keysym.isalnum():
        return keysym.upper()
    if keysym.startswith("F") and keysym[1:].isdigit():
        return keysym
    if keysym.startswith("KP_") and keysym[3:].isdigit():
        return "Keypad " + keysym[3:]
    return None


# ---- the files ------------------------------------------------------------------------------------
def load_settings():
    s = dict(DEFAULTS)
    try:
        for line in open(SETTINGS, encoding="utf-8"):
            if "=" in line and not line.startswith("#"):
                k, v = line.strip().split("=", 1)
                if k.startswith("LAUNCH_"):
                    s[k[7:].lower()] = v.strip('"')
    except OSError:
        pass
    return s


def save_settings(s):
    os.makedirs(os.path.dirname(SETTINGS), exist_ok=True)
    with open(SETTINGS, "w", encoding="utf-8") as f:
        f.write("# written by tools/launcher.py, read by launch.sh\n")
        for k in DEFAULTS:
            f.write(f'LAUNCH_{k.upper()}="{s[k]}"\n')


def classic_lines(lines):
    """{name: line index} of the [Classic Controller] bindings."""
    out, inside = {}, False
    for i, line in enumerate(lines):
        t = line.strip()
        if t.startswith("["):
            inside = t == "[Classic Controller]"
        elif inside and "=" in t and not t.startswith("#"):
            out[t.split("=", 1)[0].strip()] = i
    return out


def load_keys():
    if not os.path.exists(KEYS):
        os.makedirs(os.path.dirname(KEYS), exist_ok=True)
        shutil.copy(KEYS_DEFAULT, KEYS)
    lines = open(KEYS, encoding="utf-8").read().split("\n")
    where = classic_lines(lines)
    binds = {}
    for _, name in ACTIONS:
        if name in where:
            v = lines[where[name]].split("=", 1)[1]
            binds[name] = [x.strip() for x in v.split(",") if x.strip()]
        else:
            binds[name] = []
    return lines, binds


def save_keys(lines, binds):
    where = classic_lines(lines)
    for _, name in ACTIONS:
        if name in where:
            head = lines[where[name]].split("=", 1)[0]       # the file's own spacing
            lines[where[name]] = (head + "= " + ", ".join(binds[name])).rstrip()
        else:
            lines.append(f"{name} = " + ", ".join(binds[name]))
    with open(KEYS, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def refresh_rate():
    try:
        out = subprocess.run(["xrandr"], capture_output=True, text=True, timeout=3).stdout
        for line in out.splitlines():
            if "*" in line:
                for tok in line.split():
                    if "*" in tok:
                        return str(round(float(tok.strip("*+"))))
    except Exception:
        pass
    return "?"


# ---- the window -----------------------------------------------------------------------------------
def run_gui():
    import tkinter as tk
    from tkinter import ttk

    settings = load_settings()
    lines, binds = load_keys()

    root = tk.Tk()
    root.title("GoldenEye 007")
    root.resizable(False, False)
    pad = {"padx": 8, "pady": 4}

    top = ttk.Frame(root)
    top.pack(fill="x", padx=10, pady=(10, 0))
    book = ttk.Notebook(root)
    book.pack(fill="both", expand=True, padx=10, pady=10)

    # -- settings
    st = ttk.Frame(book)
    book.add(st, text="Settings")

    hz = refresh_rate()
    fps_mode = tk.StringVar(value="auto" if settings["fps"] == "auto" else "custom")
    fps_val = tk.StringVar(value="60" if settings["fps"] == "auto" else settings["fps"])
    ttk.Label(st, text="Frame rate").grid(row=0, column=0, sticky="w", **pad)
    fr = ttk.Frame(st)
    fr.grid(row=0, column=1, sticky="w", **pad)
    ttk.Radiobutton(fr, text=f"Screen's refresh rate ({hz} Hz)", variable=fps_mode, value="auto").pack(anchor="w")
    row = ttk.Frame(fr)
    row.pack(anchor="w")
    ttk.Radiobutton(row, text="Limit to", variable=fps_mode, value="custom").pack(side="left")
    ttk.Spinbox(row, from_=30, to=360, width=5, textvariable=fps_val).pack(side="left", padx=4)
    ttk.Label(row, text="(30 is the console's)").pack(side="left")

    fov = tk.DoubleVar(value=float(settings["fov"]))
    fov_text = tk.StringVar()
    ttk.Label(st, text="Field of view").grid(row=1, column=0, sticky="w", **pad)
    fr = ttk.Frame(st)
    fr.grid(row=1, column=1, sticky="w", **pad)

    def fov_changed(*_):
        v = round(fov.get())
        fov_text.set(f"{v}°" + ("  (the game's)" if v == 49 else ""))
    ttk.Scale(fr, from_=40, to=110, variable=fov, length=260, command=fov_changed).pack(side="left")
    ttk.Label(fr, textvariable=fov_text, width=18).pack(side="left", padx=6)
    fov_changed()

    mouse = tk.DoubleVar(value=float(settings["mouse"]))
    mouse_text = tk.StringVar()
    ttk.Label(st, text="Mouse sensitivity").grid(row=2, column=0, sticky="w", **pad)
    fr = ttk.Frame(st)
    fr.grid(row=2, column=1, sticky="w", **pad)

    def mouse_changed(*_):
        mouse_text.set(f"{mouse.get():.2f}" + ("  (mouse look off)" if mouse.get() < 0.05 else ""))
    ttk.Scale(fr, from_=0.0, to=4.0, variable=mouse, length=260, command=mouse_changed).pack(side="left")
    ttk.Label(fr, textvariable=mouse_text, width=18).pack(side="left", padx=6)
    mouse_changed()

    online = tk.BooleanVar(value=settings["online"] == "1")
    fullscreen = tk.BooleanVar(value=settings["fullscreen"] == "1")
    ttk.Checkbutton(st, text="Online: use the community server (config/hosts.txt)", variable=online).grid(
        row=3, column=0, columnspan=2, sticky="w", **pad)
    ttk.Checkbutton(st, text="Fullscreen  (F11 or Alt+Enter switches in the game)", variable=fullscreen).grid(
        row=4, column=0, columnspan=2, sticky="w", **pad)

    # -- controls
    ct = ttk.Frame(book)
    book.add(ct, text="Controls")
    ttk.Label(ct, text="The mouse looks; the wheel changes weapon. In menus the Fire button selects and the Aim "
                       "button goes back.\nSet replaces a binding, Add gives the action another key; then press "
                       "a key or a mouse button (Esc cancels).", justify="left").grid(
        row=0, column=0, columnspan=5, sticky="w", **pad)
    labels, waiting = {}, {"name": None, "add": False}
    status = tk.StringVar()

    def show(name):
        labels[name].set(", ".join(binds[name]) or "—")

    def capture(name, add):
        waiting["name"], waiting["add"] = name, add
        status.set(f"Press a key or a mouse button for: {dict((n, l) for l, n in ACTIONS)[name]}")
        root.focus_set()

    def bound(key):
        name = waiting["name"]
        if not name:
            return
        waiting["name"] = None
        if key is None:
            status.set("Cancelled.")
            return
        for other in binds:                       # a key does one thing
            if other != name and key in binds[other]:
                binds[other].remove(key)
                show(other)
        if waiting["add"]:
            if key not in binds[name]:
                binds[name].append(key)
        else:
            binds[name] = [key]
        show(name)
        status.set(f"{key} bound.")

    def on_key(e):
        if not waiting["name"]:
            return
        if e.keysym == "Escape":
            bound(None)
        else:
            k = sdl_name(e.keysym)
            if k:
                bound(k)
            else:
                status.set(f"That key ({e.keysym}) has no name in the key file; try another.")
        return "break"

    def on_button(e):
        if waiting["name"] and e.num in MOUSE and e.widget.winfo_class() not in ("TButton",):
            bound(MOUSE[e.num])
            return "break"

    root.bind("<KeyPress>", on_key)
    root.bind("<ButtonPress>", on_button)

    half = (len(ACTIONS) + 1) // 2
    for i, (label, name) in enumerate(ACTIONS):
        col, r = (0, i + 1) if i < half else (1, i - half + 1)
        cell = ttk.Frame(ct)
        cell.grid(row=r, column=col, sticky="w", padx=8, pady=1)
        ttk.Label(cell, text=label, width=27).pack(side="left")
        labels[name] = tk.StringVar()
        ttk.Label(cell, textvariable=labels[name], width=24, relief="sunken", anchor="w").pack(side="left", padx=3)
        ttk.Button(cell, text="Set", width=4, command=lambda n=name: capture(n, False)).pack(side="left")
        ttk.Button(cell, text="Add", width=4, command=lambda n=name: capture(n, True)).pack(side="left")
        show(name)

    def reset_keys():
        nonlocal lines
        shutil.copy(KEYS_DEFAULT, KEYS)
        lines, fresh = load_keys()
        binds.clear()
        binds.update(fresh)
        for _, n in ACTIONS:
            show(n)
        status.set("The port's default bindings are back.")

    bottom = ttk.Frame(ct)
    bottom.grid(row=half + 2, column=0, columnspan=2, sticky="we", **pad)
    ttk.Button(bottom, text="Defaults", command=reset_keys).pack(side="left")
    ttk.Label(bottom, textvariable=status).pack(side="left", padx=10)

    # -- play
    result = {"play": False}

    def collect():
        settings["fps"] = "auto" if fps_mode.get() == "auto" else str(max(30, min(1000, int(float(fps_val.get() or 60)))))
        settings["fov"] = str(round(fov.get()))
        settings["mouse"] = f"{mouse.get():.2f}"
        settings["online"] = "1" if online.get() else "0"
        settings["fullscreen"] = "1" if fullscreen.get() else "0"
        save_settings(settings)
        save_keys(lines, binds)

    def play():
        collect()
        result["play"] = True
        root.destroy()

    def close():
        collect()
        root.destroy()

    ttk.Label(top, text="GoldenEye 007", font=("", 14, "bold")).pack(side="left")
    ttk.Button(top, text="Play", command=play).pack(side="right")
    ttk.Button(top, text="Save and close", command=close).pack(side="right", padx=6)
    root.protocol("WM_DELETE_WINDOW", close)
    root.mainloop()
    return result["play"]


def main():
    if "--check" in sys.argv:
        s = load_settings()
        save_settings(s)
        lines, binds = load_keys()
        before = open(KEYS, encoding="utf-8").read()
        save_keys(lines, binds)
        same = open(KEYS, encoding="utf-8").read() == before
        print(f"settings {s}; {sum(len(v) for v in binds.values())} bindings; key file unchanged by a save: {same}")
        return 0 if same else 1
    try:
        play = run_gui()
    except Exception as e:                       # no display, no Tk: the game starts with what is saved
        print(f"launcher: no window ({e}); starting with the saved settings", file=sys.stderr)
        return 0
    return 0 if play else 3                      # 3: closed without Play


if __name__ == "__main__":
    sys.exit(main())
