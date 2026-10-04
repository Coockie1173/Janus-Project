#!/usr/bin/env python3
"""The window before the game: settings and key bindings, then Play.

    python3 tools/launcher.py            (./launch.sh opens it)
    python3 tools/launcher.py --check    load and save everything once, no window: a self-test

Settings are kept in build/settings.env (read by launch.sh), the bindings in
build/keys.txt (the runtime's key file: only the [Classic Controller] lines
listed here are rewritten, the rest of the file is kept as it is).

"Extract disc..." fills the game folder from an .rvz/.iso/.wbfs image (extract_disc.py).

In a package (Windows: Play.bat, Linux: Play.sh) this file sits in launcher/ beside the game's
executable and does launch.sh's part as well: the same window, then it starts the game with
what was chosen. There the settings are settings.env and keys.txt in the package's folder, and
the options are launch.sh's: --no-gui, --fps N, --fov N, --mouse N, --mouse-mode M, --fullscreen,
--fast-start, --cheats, --offline, --extract IMAGE (Linux also --no-crash-dumps, --crash-mem);
anything else goes to the game.
"""
import os
import shutil
import subprocess
import sys

import extract_disc

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
WINDOWS = os.name == "nt"
GAME_EXE = os.path.join(ROOT, "GoldenEye.exe" if WINDOWS else "GoldenEye")
PORTABLE = os.path.isfile(GAME_EXE) #a package: no build folder, no launch.sh
PLAY = "Play.bat" if WINDOWS else "./Play.sh"
CRASHES = "crashes" if PORTABLE else "build/crashes"
STATE = ROOT if PORTABLE else os.path.join(ROOT, "build")
KEYS = os.path.join(STATE, "keys.txt")
KEYS_DEFAULT = os.path.join(ROOT, "config", "keys.txt")
SETTINGS = os.path.join(STATE, "settings.env")
#the frame rates the slider stops at (the game's own settings menu, port/settings.cpp, has the same)
FPS_STOPS = [30, 40, 50, 60, 75, 90, 100, 120, 144, 165, 180, 200, 240, 275]
FPS_MAX = FPS_STOPS[-1]
CHEATS = os.path.join(ROOT, "config", "gecko", "cheats.txt")
CHEATS_ON = os.path.join(STATE, "cheats.txt")
GAME = os.path.join(ROOT, "game")
RUN_LOG = os.path.join(STATE, "run.log")
IMAGE_TYPES = [("Wii disc images", "*.rvz *.iso *.wbfs *.wia"), ("All files", "*")]

#what the game does with each Classic Controller button (its default layout), and the key file's name for it
ACTIONS = [
    ("Fire", "ZR"), ("Aim down the sight", "ZL"), ("Melee attack", "L"), ("Throw grenade  (doubles as R button)", "R"),
    ("Reload", "Y"), ("Crouch  (menus: select)", "A"), ("Action  (menus: back)", "B"), ("Sprint", "Extra 1"),
    ("Cycle grenades", "X"), ("Next weapon", "Right"), ("Previous weapon", "Left"),
    ("Firing mode", "Up"), ("Smartphone", "Down"), ("Night vision / scoreboard", "Minus"),
    ("Pause", "Plus"), ("Home menu", "Home"),
    ("Move forward", "Left Stick Up"), ("Move back", "Left Stick Down"),
    ("Move left", "Left Stick Left"), ("Move right", "Left Stick Right"),
]
DEFAULTS = {"fps": "auto", "fov": "49", "mouse": "1.0", "online": "1", "fullscreen": "0", "faststart": "0", "mousemode": "auto",
            "mouseinvert": "0", "adsfov": "22.5", "adssens": "0.5", "aimlock": "0", "mouseeverywhere": "0", "skipcutscenes": "0", "skipmovies": "0", "notutorials": "0", "leveltimer": "1", "autoqte": "0", "autostart": "0", "extrasmenu": "0", "unlockall": "0",
            "crashdump": "1", "crashmem": "0"}

#Tk's key names -> SDL's (the key file's)
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


#---- the files ------------------------------------------------------------------------------------
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
        f.write("# written by the launcher, read when the game is started\n")
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
            head = lines[where[name]].split("=", 1)[0] #the file's own spacing
            lines[where[name]] = (head + "= " + ", ".join(binds[name])).rstrip()
        else:
            lines.append(f"{name} = " + ", ".join(binds[name]))
    with open(KEYS, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def load_cheats(path):
    """every code in a gecko file as (name, its lines, its notes)"""
    cheats = []
    if not os.path.exists(path):
        return cheats
    for line in open(path):
        line = line.strip()
        if line.startswith("$"):
            cheats.append((line[1:].strip(), [], []))
        elif cheats and line.startswith("*"):
            cheats[-1][2].append(line[1:].strip())
        elif cheats and line and not line.startswith("#"):
            cheats[-1][1].append(line)
    return cheats


def save_cheats(cheats, ticked):
    #only the ticked ones end up in the file the game reads, an empty file means no cheats
    os.makedirs(os.path.dirname(CHEATS_ON), exist_ok=True)
    with open(CHEATS_ON, "w") as f:
        for name, code, _ in cheats:
            if name in ticked:
                f.write("$" + name + "\n" + "\n".join(code) + "\n\n")


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


#---- the window -----------------------------------------------------------------------------------
def run_gui():
    import queue
    import threading
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

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

    #settings
    st = ttk.Frame(book)
    book.add(st, text="Settings")

    hz = refresh_rate()
    #the slider goes over the stops, 60 is the default and is saved as "auto"
    now = int(settings["fps"]) if settings["fps"].isdigit() else 60
    fps = tk.DoubleVar(value=min(range(len(FPS_STOPS)), key=lambda i: abs(FPS_STOPS[i] - now)))
    fps_text = tk.StringVar()
    ttk.Label(st, text="Max frame rate").grid(row=0, column=0, sticky="w", **pad)
    fr = ttk.Frame(st)
    fr.grid(row=0, column=1, sticky="w", **pad)

    def fps_value():
        return FPS_STOPS[max(0, min(len(FPS_STOPS) - 1, round(fps.get())))]

    def fps_changed(*_):
        v = fps_value()
        fps_text.set(f"{v}" + ("  (the default)" if v == 60 else "  (the console's)" if v == 30 else f"  (screen: {hz} Hz)"))
    #above 60 the game shows frames as it draws them, heavy levels sit around 90 on a decent machine
    ttk.Scale(fr, from_=0, to=len(FPS_STOPS) - 1, variable=fps, length=260, command=fps_changed).pack(side="left")
    ttk.Label(fr, textvariable=fps_text, width=18).pack(side="left", padx=6)
    fps_changed()

    fov = tk.DoubleVar(value=float(settings["fov"]))
    fov_text = tk.StringVar()
    ttk.Label(st, text="Field of view").grid(row=1, column=0, sticky="w", **pad)
    fr = ttk.Frame(st)
    fr.grid(row=1, column=1, sticky="w", **pad)

    def fov_changed(*_):
        v = round(fov.get())
        fov_text.set(f"{v}°" + ("  (the game's)" if v == 49 else ""))
    ttk.Scale(fr, from_=20, to=120, variable=fov, length=260, command=fov_changed).pack(side="left")
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

    modes = {"auto": "Automatic (direct; through the stick in a game someone else hosts)",
             "direct": "Direct: the mouse turns the view itself",
             "stick": "Right stick: as controller input (capped turning speed)"}
    mousemode = tk.StringVar(value=modes.get(settings["mousemode"], modes["auto"]))
    ttk.Label(st, text="Mouse look").grid(row=6, column=0, sticky="w", **pad)
    ttk.Combobox(st, textvariable=mousemode, values=list(modes.values()), state="readonly", width=58).grid(
        row=6, column=1, sticky="w", **pad)

    online = tk.BooleanVar(value=settings["online"] == "1")
    fullscreen = tk.BooleanVar(value=settings["fullscreen"] == "1")
    ttk.Checkbutton(st, text="Online: use the community server (config/hosts.txt)", variable=online).grid(
        row=3, column=0, columnspan=2, sticky="w", **pad)
    ttk.Checkbutton(st, text="Fullscreen  (F11 or Alt+Enter switches in the game)", variable=fullscreen).grid(
        row=4, column=0, columnspan=2, sticky="w", **pad)
    faststart = tk.BooleanVar(value=settings["faststart"] == "1")
    ttk.Checkbutton(st, text="Quicker start: hurry through the screens before the menu (a Gecko code)",
                    variable=faststart).grid(row=5, column=0, columnspan=2, sticky="w", **pad)
    #crash dumps are on unless someone unticks them, the memory is big so that one's opt in
    crashdump = tk.BooleanVar(value=settings["crashdump"] == "1")
    crashmem = tk.BooleanVar(value=settings["crashmem"] == "1")
    if not WINDOWS:
        ttk.Checkbutton(st, text=f"Crash dumps: write a report into {CRASHES} when the game dies or stalls",
                        variable=crashdump).grid(row=7, column=0, columnspan=2, sticky="w", **pad)
        ttk.Checkbutton(st, text="...with the game's memory in each dump (88 MB)",
                        variable=crashmem).grid(row=8, column=0, columnspan=2, sticky="w", padx=(30, 8))

    #controls
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
        for other in binds: #a key does one thing
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

    #enhancements
    #the F1 menu's Enhancements page has the same ones
    en = ttk.Frame(book)
    book.add(en, text="Enhancements")
    skip_cutscenes = tk.BooleanVar(value=settings["skipcutscenes"] == "1")
    ttk.Checkbutton(en, text="Skip Intro Cutscenes (Experimental)", variable=skip_cutscenes).grid(row=0, column=0, sticky="w", **pad)
    ttk.Label(en, text="A mission's opening cutscene is skipped.",
              foreground="gray", wraplength=520, justify="left").grid(row=1, column=0, sticky="w", padx=(30, 8))
    level_timer = tk.BooleanVar(value=settings["leveltimer"] == "1")
    ttk.Checkbutton(en, text="Level Timer", variable=level_timer).grid(row=2, column=0, sticky="w", **pad)
    ttk.Label(en, text="The hud shows the time played in the level.",
              foreground="gray", wraplength=520, justify="left").grid(row=3, column=0, sticky="w", padx=(30, 8))
    unlock_all = tk.BooleanVar(value=settings["unlockall"] == "1")
    ttk.Checkbutton(ch, text="Unlock Everything", variable=unlock_all).grid(row=4, column=0, sticky="w", **pad)
    ttk.Label(ch, text="Every mission and every difficulty can be picked. A save made with it on may keep them.",
              foreground="gray", wraplength=520, justify="left").grid(row=5, column=0, sticky="w", padx=(30, 8))
    auto_qte = tk.BooleanVar(value=settings["autoqte"] == "1")
    ttk.Checkbutton(en, text="Auto Quick Time Events", variable=auto_qte).grid(row=4, column=0, sticky="w", **pad)
    ttk.Label(en, text="Quick time events press their own buttons.",
              foreground="gray", wraplength=520, justify="left").grid(row=5, column=0, sticky="w", padx=(30, 8))
    auto_start = tk.BooleanVar(value=settings["autostart"] == "1")
    ttk.Checkbutton(en, text="Auto Start Mission", variable=auto_start).grid(row=6, column=0, sticky="w", **pad)
    ttk.Label(en, text="A loaded mission starts by itself, without Continue being pressed.",
              foreground="gray", wraplength=520, justify="left").grid(row=7, column=0, sticky="w", padx=(30, 8))
    skip_movies = tk.BooleanVar(value=settings["skipmovies"] == "1")
    ttk.Checkbutton(en, text="Skip Movies", variable=skip_movies).grid(row=8, column=0, sticky="w", **pad)
    ttk.Label(en, text="Briefings, the movies between missions and the logos are skipped.",
              foreground="gray", wraplength=520, justify="left").grid(row=9, column=0, sticky="w", padx=(30, 8))
    no_tutorials = tk.BooleanVar(value=settings["notutorials"] == "1")
    ttk.Checkbutton(en, text="Disable Tutorials", variable=no_tutorials).grid(row=10, column=0, sticky="w", **pad)
    ttk.Label(en, text="The tutorial boxes in the missions aren't shown.",
              foreground="gray", wraplength=520, justify="left").grid(row=11, column=0, sticky="w", padx=(30, 8))

    #cheats
    #the tab just lists whatever is in config/gecko/cheats.txt, so adding a cheat is adding it to that file
    ch = ttk.Frame(book)
    book.add(ch, text="Cheats")
    cheats = load_cheats(CHEATS)
    already_on = {name for name, _, _ in load_cheats(CHEATS_ON)}
    cheat_vars = {}
    #the port's own cheats (the F1 menu's Cheats page has them too), then the Gecko codes
    always_mouse = tk.BooleanVar(value=settings["mouseeverywhere"] == "1")
    ttk.Checkbutton(ch, text="Always Enable Mouse", variable=always_mouse).grid(row=0, column=0, sticky="w", **pad)
    ttk.Label(ch, text="The mouse turns the view where the game locks it: cutscenes, melee, climbing, death.",
              foreground="gray", wraplength=520, justify="left").grid(row=1, column=0, sticky="w", padx=(30, 8))
    extras_menu = tk.BooleanVar(value=settings["extrasmenu"] == "1")
    ttk.Checkbutton(ch, text="Unused Extras Menu", variable=extras_menu).grid(row=2, column=0, sticky="w", **pad)
    ttk.Label(ch, text="Extras also lists Mission Select, Character Viewer, Movies and the Calibration Menu.",
              foreground="gray", wraplength=520, justify="left").grid(row=3, column=0, sticky="w", padx=(30, 8))
    ttk.Label(ch, text="Gecko codes from config/gecko/cheats.txt. They apply the next time the game starts.",
              justify="left").grid(row=6, column=0, sticky="w", **pad)
    for i, (name, code, notes) in enumerate(cheats):
        cheat_vars[name] = tk.BooleanVar(value=name in already_on)
        ttk.Checkbutton(ch, text=name, variable=cheat_vars[name]).grid(row=7 + 2 * i, column=0, sticky="w", **pad)
        ttk.Label(ch, text="\n".join(notes) or "  ".join(code), foreground="gray", wraplength=520,
                  justify="left").grid(row=8 + 2 * i, column=0, sticky="w", padx=(30, 8))
    if not cheats:
        ttk.Label(ch, text="No codes in there yet.", foreground="gray").grid(row=7, column=0, sticky="w", **pad)

    #play
    result = {"play": False}

    def collect():
        settings["fps"] = "auto" if fps_value() == 60 else str(fps_value())
        settings["fov"] = str(round(fov.get()))
        settings["mouse"] = f"{mouse.get():.2f}"
        settings["online"] = "1" if online.get() else "0"
        settings["fullscreen"] = "1" if fullscreen.get() else "0"
        settings["faststart"] = "1" if faststart.get() else "0"
        settings["crashdump"] = "1" if crashdump.get() else "0"
        settings["crashmem"] = "1" if crashmem.get() else "0"
        settings["mouseeverywhere"] = "1" if always_mouse.get() else "0"
        settings["skipcutscenes"] = "1" if skip_cutscenes.get() else "0"
        settings["leveltimer"] = "1" if level_timer.get() else "0"
        settings["autoqte"] = "1" if auto_qte.get() else "0"
        settings["autostart"] = "1" if auto_start.get() else "0"
        settings["skipmovies"] = "1" if skip_movies.get() else "0"
        settings["notutorials"] = "1" if no_tutorials.get() else "0"
        settings["extrasmenu"] = "1" if extras_menu.get() else "0"
        settings["unlockall"] = "1" if unlock_all.get() else "0"
        settings["mousemode"] = next((k for k, v in modes.items() if v == mousemode.get()), "auto")
        save_settings(settings)
        save_keys(lines, binds)
        save_cheats(cheats, {name for name, var in cheat_vars.items() if var.get()})

    #the game's files
    game_text = tk.StringVar()

    def game_changed():
        game_text.set("" if extract_disc.have_game(GAME) else "The game's files are missing: Extract disc...")

    def extract_disc_clicked():
        image = filedialog.askopenfilename(parent=root, title="GoldenEye 007 disc image (SJBE52)", filetypes=IMAGE_TYPES)
        if not image:
            return
        force = False
        if extract_disc.have_game(GAME):
            if not messagebox.askyesno("Extract disc", "The game folder is already filled. Replace it?\n"
                                       "(The one that is there is kept beside it as game.old.)", parent=root):
                return
            force = True
        box = tk.Toplevel(root)
        box.title("Extracting the disc")
        box.resizable(False, False)
        box.transient(root)
        said = tk.StringVar(value="Reading the image...")
        ttk.Label(box, textvariable=said, width=64, anchor="w").pack(padx=12, pady=(12, 4))
        bar = ttk.Progressbar(box, length=460, maximum=1000)
        bar.pack(padx=12, pady=4)
        stop = threading.Event()
        ttk.Button(box, text="Cancel", command=stop.set).pack(pady=(4, 12))
        box.protocol("WM_DELETE_WINDOW", stop.set)
        box.grab_set()
        news = queue.Queue()

        class Stopped(Exception):
            pass

        def progress(done, total, rel):
            if stop.is_set():
                raise Stopped()
            news.put(("at", done, total, rel))

        def work(): #the disc is read off the window's thread
            try:
                extract_disc.extract(image, GAME, force, progress, lambda text: news.put(("say", text)))
                news.put(("done", None))
            except Stopped:
                news.put(("done", "Stopped: nothing was kept."))
            except extract_disc.ExtractError as e:
                news.put(("done", str(e)))
            except Exception as e:
                news.put(("done", f"The extraction failed: {e}"))

        def poll():
            ended = False
            while not news.empty():
                item = news.get()
                if item[0] == "at":
                    bar["value"] = item[1] * 1000 // max(item[2], 1)
                    said.set(f"{item[1] / 1e9:.2f} of {item[2] / 1e9:.2f} GB   {item[3]}"[:64])
                elif item[0] == "done":
                    ended, problem = True, item[1]
            if not ended:
                root.after(100, poll)
                return
            box.grab_release()
            box.destroy()
            game_changed()
            if problem:
                messagebox.showerror("Extract disc", problem, parent=root)
            else:
                messagebox.showinfo("Extract disc", "The game's files are in place.", parent=root)

        threading.Thread(target=work, daemon=True).start()
        poll()

    def play():
        if not extract_disc.have_game(GAME):
            messagebox.showwarning("GoldenEye 007", "The game's files are not here yet.\n\nExtract disc... takes them out "
                                   "of your own GoldenEye 007 disc image (NTSC-U, SJBE52: .rvz, .iso or .wbfs).", parent=root)
            return
        collect()
        result["play"] = True
        root.destroy()

    def close():
        collect()
        root.destroy()

    ttk.Label(top, text="GoldenEye 007", font=("", 14, "bold")).pack(side="left")
    ttk.Button(top, text="Play", command=play).pack(side="right")
    ttk.Button(top, text="Save and close", command=close).pack(side="right", padx=6)
    ttk.Button(top, text="Extract disc...", command=extract_disc_clicked).pack(side="right")
    ttk.Label(top, textvariable=game_text, foreground="#b00020").pack(side="right", padx=10)
    game_changed()
    root.protocol("WM_DELETE_WINDOW", close)
    root.mainloop()
    return result["play"]


#---- a package: what launch.sh does ----------------------------------------------------------------
def run_game(args):
    """Start the game's executable with the saved settings and the command line over them; its exit code."""
    s = load_settings()
    env = dict(os.environ)
    extra, rest = [], []
    names = {"--fps": "fps", "--fov": "fov", "--mouse": "mouse", "--mouse-mode": "mousemode"}
    i = 0
    while i < len(args):
        a = args[i]
        if a in names and i + 1 < len(args):
            s[names[a]] = args[i + 1]
            i += 1
        elif a == "--fullscreen":
            s["fullscreen"] = "1"
        elif a == "--always-mouse":
            s["mouseeverywhere"] = "1"
        elif a == "--skip-cutscenes":
            s["skipcutscenes"] = "1"
        elif a == "--no-level-timer":
            s["leveltimer"] = "0"
        elif a == "--auto-qte":
            s["autoqte"] = "1"
        elif a == "--auto-start":
            s["autostart"] = "1"
        elif a == "--skip-movies":
            s["skipmovies"] = "1"
        elif a == "--no-tutorials":
            s["notutorials"] = "1"
        elif a == "--extras-menu":
            s["extrasmenu"] = "1"
        elif a == "--unlock-all":
            s["unlockall"] = "1"
        elif a == "--fast-start":
            s["faststart"] = "1"
        elif a == "--cheats":
            shutil.copy(CHEATS, CHEATS_ON)
        elif a == "--offline":
            s["online"] = "0"
        elif a == "--crash-dumps":
            s["crashdump"] = "1"
        elif a == "--no-crash-dumps":
            s["crashdump"] = "0"
        elif a == "--crash-mem":
            s["crashdump"] = s["crashmem"] = "1"
        elif a == "--trace":
            env["GE_TRACE"] = env["GE_TRACELOOK"] = "1"
        elif a != "--no-gui":
            rest.append(a)
        i += 1
    if not extract_disc.have_game(GAME):
        print(f"The game's files are missing ({os.path.join('game', 'sys', 'main.dol')}). "
              f"Start {PLAY} and use Extract disc...,\nor: {PLAY} --extract GoldenEye.rvz")
        return 1
    if s["fps"] != "auto" and s["fps"].isdigit() and int(s["fps"]) > FPS_MAX:
        s["fps"] = str(FPS_MAX)
    for var, key in (("GE_FPS", "fps"), ("GE_FOV", "fov"), ("GE_MOUSE", "mouse"), ("GE_MOUSE_MODE", "mousemode"),
                     ("GE_MOUSE_INVERT", "mouseinvert"), ("GE_ADS_FOV", "adsfov"),
                     ("GE_ADS_SENS", "adssens"), ("GE_AIM_LOCK", "aimlock"), ("GE_MOUSE_EVERYWHERE", "mouseeverywhere"),
                     ("GE_SKIP_CUTSCENES", "skipcutscenes"), ("GE_LEVEL_TIMER", "leveltimer"), ("GE_AUTO_QTE", "autoqte"), ("GE_AUTO_START", "autostart"), ("GE_SKIP_MOVIES", "skipmovies"), ("GE_NO_TUTORIALS", "notutorials"),
                     ("GE_EXTRAS_MENU", "extrasmenu"), ("GE_UNLOCK_ALL", "unlockall")):
        env.setdefault(var, s[key])
    if s["online"] == "0":
        env["GE_HOSTS"] = ""
    if not WINDOWS: #what the Windows build does not have: the crash dumps
        if s["crashdump"] == "0":
            env["GE_NO_CRASHDUMP"] = "1"
        else:
            env.pop("GE_NO_CRASHDUMP", None)
            env.setdefault("GE_CRASH_DIR", CRASHES)
        if s["crashmem"] == "1":
            env["GE_CRASH_MEM"] = "1"
    if s["faststart"] == "1":
        extra.append("config/gecko/faststart.txt")
    if load_cheats(CHEATS_ON):
        extra.append("cheats.txt")
    if env.get("GE_GECKO_EXTRA"):
        extra.append(env["GE_GECKO_EXTRA"])
    if extra:
        env["GE_GECKO_EXTRA"] = ":".join(extra) #paths from the package's folder: no drive letters
    if not os.path.exists(KEYS):
        shutil.copy(KEYS_DEFAULT, KEYS)
    cmd = [GAME_EXE, "game", "--symbols", os.path.join("config", "symbols.tsv"), "--watch", "10"]
    if s["fullscreen"] == "1":
        cmd.append("--fullscreen")
    print("GoldenEye 007 is running.  keys: keys.txt   log: run.log")
    dumps = os.listdir(os.path.join(ROOT, CRASHES)) if os.path.isdir(os.path.join(ROOT, CRASHES)) else []
    with open(RUN_LOG, "w") as log:
        code = subprocess.run(cmd + rest, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
    if not WINDOWS and os.path.isdir(os.path.join(ROOT, CRASHES)) and os.listdir(os.path.join(ROOT, CRASHES)) != dumps:
        print(f"the game left a crash dump: {CRASHES}/{sorted(os.listdir(os.path.join(ROOT, CRASHES)))[-1]}")
    if code != 0:
        print(f"\nThe game stopped with an error ({code}). The end of run.log:\n")
        with open(RUN_LOG, errors="replace") as log:
            print("".join(log.readlines()[-15:]))
    return code


def extract_cli(args):
    at = args.index("--extract")
    if at + 1 >= len(args):
        print("--extract wants the disc image: --extract GoldenEye.rvz")
        return 2
    return subprocess.call([sys.executable, extract_disc.__file__, args[at + 1], GAME]
                           + (["--force"] if "--force" in args else []))


def main():
    if "--extract" in sys.argv:
        return extract_cli(sys.argv[1:])
    if PORTABLE and "--check" not in sys.argv:
        args = sys.argv[1:]
        if not args:
            try:
                if not run_gui():
                    return 0 #closed without Play
            except Exception as e:
                print(f"launcher: no window ({e}); starting with the saved settings", file=sys.stderr)
        return run_game(args)
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
    except Exception as e: #no display, no Tk: the game starts with what is saved
        print(f"launcher: no window ({e}); starting with the saved settings", file=sys.stderr)
        return 0
    return 0 if play else 3 #3: closed without Play


if __name__ == "__main__":
    sys.exit(main())
