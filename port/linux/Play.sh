#!/bin/sh
# GoldenEye 007 (Wii): the settings window, then the game.
#     ./Play.sh --no-gui               straight to the game, with the saved settings
#     ./Play.sh --extract GAME.rvz     take the game's files out of a disc image (.rvz .iso .wbfs) into "game"
#     ./Play.sh --fps 120 --fov 70 --mouse 1.5 --fullscreen --fast-start --cheats
#     ./Play.sh --offline              no hosts file: the game does not find its servers
#     ./Play.sh --always-mouse         the cheat "Always Enable Mouse": mouse look in every player state
#     ./Play.sh --skip-cutscenes       a mission's opening cutscene is skipped
#     ./Play.sh --skip-zukovsky        the meeting with Zukovsky in Nightclub is skipped
#     ./Play.sh --skip-briggs          the meeting with Sky Briggs in Carrier is skipped
#     ./Play.sh --no-skip-fade         a skipped cutscene is fast forwarded in sight instead of behind a fade
#     ./Play.sh --skip-movies          briefings, the movies between missions and the logos are skipped
#     ./Play.sh --no-tutorials         the tutorial boxes in the missions aren't shown
#     ./Play.sh --no-colour-filter     the tint some levels lay over the picture isn't drawn
#     ./Play.sh --no-level-timer       the hud without the level's time (it is on by default)
#     ./Play.sh --auto-qte             quick time events press their own buttons
#     ./Play.sh --auto-start           a loaded mission starts without "Continue" being pressed
#     ./Play.sh --extras-menu          the cheat "Unused Extras Menu"
#     ./Play.sh --unlock-all           the cheat "Unlock Everything": every mission and difficulty
#     ./Play.sh --no-crash-dumps       no dump in crashes/ when the game dies or stalls (--crash-mem adds
#                                      the game's memory to each dump, 88 MB)
# Any option skips the window; what the launcher does not know goes to the game (--window 1920x1080...).
cd "$(dirname "$0")" || exit 1
[ -x ./GoldenEye ] || { echo "GoldenEye is missing or not executable: unzip the whole package (unzip keeps the permissions)"; exit 1; }
command -v python3 >/dev/null || { echo "python3 is needed for the launcher (with Tk for its window): install it from your distribution"; exit 1; }
# through X11 when there is one: a present on Wayland costs the renderer 2 ms, 0.3 there
# (SDL_VIDEODRIVER in the environment overrides)
if [ -z "$SDL_VIDEODRIVER" ] && [ -n "$DISPLAY" ]; then SDL_VIDEODRIVER=x11,wayland; export SDL_VIDEODRIVER; fi
exec python3 launcher/launcher.py "$@"
