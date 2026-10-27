# The port's targets, included by the generated build with
# -DWIIKIT_EXTRA=<this file>: the game's own layer linked into wiiboot.
set(GE_PORT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(GE_ROOT ${GE_PORT}/.. ABSOLUTE)
# where the game looks for its files: this checkout, or (GE_PORTABLE, the Windows package)
# the folder it is started in, laid out by tools/package_win.sh
set(GE_HOME ${GE_ROOT})
set(GE_GECKO_FILE ${GE_ROOT}/build/patched/gecko.txt)
set(GE_SETTINGS_FILE ${GE_ROOT}/build/settings.env)
if(GE_PORTABLE)
  set(GE_HOME .)
  set(GE_GECKO_FILE config/gecko-codes.txt)
  set(GE_SETTINGS_FILE settings.env)
endif()
target_sources(wiiboot PRIVATE
    ${GE_PORT}/goldeneye.cpp ${GE_PORT}/framerate.cpp ${GE_PORT}/input.cpp ${GE_PORT}/fov.cpp
    ${GE_PORT}/gecko.cpp ${GE_PORT}/net.cpp ${GE_PORT}/overlay.cpp ${GE_PORT}/dmw.cpp ${GE_PORT}/settings.cpp
    ${GE_PORT}/diagnostics.cpp ${GE_PORT}/debug.cpp ${GE_PORT}/native.cpp ${GE_PORT}/crashdump.cpp
    ${GE_PORT}/cutscenes.cpp ${GE_PORT}/tweaks.cpp ${GE_PORT}/qte.cpp ${GE_PORT}/movies.cpp
    ${GE_PORT}/tank.cpp ${GE_PORT}/hotkeys.cpp ${GE_PORT}/noclip.cpp ${GE_PORT}/bunnyhop.cpp ${GE_PORT}/stamina.cpp ${GE_PORT}/files.cpp ${GE_PORT}/records.cpp ${GE_PORT}/zones.cpp
    ${GE_PORT}/autosave.cpp ${GE_PORT}/weapons.cpp ${GE_PORT}/collision.cpp ${GE_PORT}/triggers.cpp ${GE_PORT}/prompts.cpp ${GE_PORT}/body.cpp ${GE_PORT}/unload.cpp
    ${GE_PORT}/timeline.cpp ${GE_PORT}/flipbooks.cpp ${GE_PORT}/heap.cpp ${GE_PORT}/sweep.cpp ${GE_PORT}/mods.cpp ${GE_PORT}/modmenu.cpp ${GE_PORT}/pictures.cpp ${GE_PORT}/shake.cpp ${GE_PORT}/render.cpp ${GE_PORT}/sound.cpp ${GE_PORT}/occlusion.cpp ${GE_PORT}/height.cpp ${GE_PORT}/typing.cpp ${GE_PORT}/custom.cpp ${GE_PORT}/pointer.cpp ${GE_PORT}/cheats.cpp ${GE_PORT}/randomizer.cpp ${GE_PORT}/customweapons.cpp ${GE_PORT}/customsound.cpp ${GE_PORT}/icon.cpp ${GE_PORT}/speedrun.cpp ${GE_PORT}/owned.cpp)
# dev only, never committed: port/dev.cpp when it is there (F1 > Dev)
if(EXISTS ${GE_PORT}/dev.cpp)
  target_sources(wiiboot PRIVATE ${GE_PORT}/dev.cpp)
  target_compile_definitions(wiiboot PRIVATE GE_DEV=1)
endif()
# files found wherever the game is started from
target_compile_definitions(wiiboot PRIVATE
    GE_ROOT="${GE_HOME}"
    GE_HOSTS_DEFAULT="${GE_HOME}/config/hosts.txt"
    GE_GECKO_DEFAULT="${GE_GECKO_FILE}"
    GE_SETTINGS_FILE="${GE_SETTINGS_FILE}")
# the window's icon (port/icon.bmp, from icon.tga) as an array in the executable, and on
# Windows the executable's own icon as a resource (port/win/icon.rc)
file(READ ${GE_PORT}/icon.bmp GE_ICON_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," GE_ICON_BYTES "${GE_ICON_HEX}")
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ge_gen/ge_icon.h
     CONTENT "static const unsigned char kIcon[] = {${GE_ICON_BYTES}};\n" @ONLY)
target_include_directories(wiiboot PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ge_gen)
# the port's version, from the last v* tag (v1.0, v1.0-3-g4542d33, or the commit alone before
# any tag), -dirty with uncommitted changes; wiikit's patched submodule doesn't count
execute_process(COMMAND git describe --tags --match "v*" --always WORKING_DIRECTORY ${GE_ROOT}
                OUTPUT_VARIABLE GE_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no --ignore-submodules=all WORKING_DIRECTORY ${GE_ROOT}
                OUTPUT_VARIABLE GE_CHANGES OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT GE_VERSION)
  set(GE_VERSION "unknown")
elseif(GE_CHANGES)
  string(APPEND GE_VERSION "-dirty")
endif()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ge_gen/ge_version.h
     CONTENT "#define GE_VERSION \"${GE_VERSION}\"\n" @ONLY)
if(WIN32)
  enable_language(RC)
  target_sources(wiiboot PRIVATE ${GE_PORT}/win/icon.rc)
endif()
# the debug overlay and the settings menu: Dear ImGui's sources (tools/get_imgui.sh), compiled in when they are there
set(GE_IMGUI ${GE_ROOT}/build/deps/imgui)
if(EXISTS ${GE_IMGUI}/imgui.cpp)
  target_sources(wiiboot PRIVATE
      ${GE_IMGUI}/imgui.cpp ${GE_IMGUI}/imgui_draw.cpp ${GE_IMGUI}/imgui_tables.cpp ${GE_IMGUI}/imgui_widgets.cpp
      ${GE_IMGUI}/backends/imgui_impl_sdl3.cpp ${GE_IMGUI}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(wiiboot PRIVATE ${GE_IMGUI} ${GE_IMGUI}/backends ${CMAKE_CURRENT_BINARY_DIR}/ge_gen)
  # the menu's typeface (Roboto, among Dear ImGui's fonts) as an array in the executable
  file(READ ${GE_IMGUI}/misc/fonts/Roboto-Medium.ttf GE_FONT_HEX HEX)
  string(REGEX REPLACE "(..)" "0x\\1," GE_FONT_BYTES "${GE_FONT_HEX}")
  file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ge_gen/ge_font.h
       CONTENT "static const unsigned char kMenuFont[] = {${GE_FONT_BYTES}};\n" @ONLY)
  target_compile_definitions(wiiboot PRIVATE GE_IMGUI=1)
  target_link_libraries(wiiboot ${CMAKE_DL_LIBS})
endif()
