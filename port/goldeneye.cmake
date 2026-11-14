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
# every .cpp under port/, a folder each concern (ge.h lists them). includes go from port/
file(GLOB_RECURSE GE_SOURCES CONFIGURE_DEPENDS ${GE_PORT}/*.cpp)
list(FILTER GE_SOURCES EXCLUDE REGEX "/debug/dev\\.cpp$")
target_sources(wiiboot PRIVATE ${GE_SOURCES})
# -iquote, not -I: port/debug/debug.h would shadow libstdc++'s <debug/debug.h>
target_compile_options(wiiboot PRIVATE "SHELL:-iquote ${GE_PORT}")
# dev only, never committed: port/debug/dev.cpp when it is there (F1 > Dev)
if(EXISTS ${GE_PORT}/debug/dev.cpp)
  target_sources(wiiboot PRIVATE ${GE_PORT}/debug/dev.cpp)
  target_compile_definitions(wiiboot PRIVATE GE_DEV=1)
endif()
# half-life's crouch while bunnyhopping (make HL_CROUCH=ON), left out by default
if(GE_HL_CROUCH)
  target_compile_definitions(wiiboot PRIVATE GE_HL_CROUCH=1)
endif()
# files found wherever the game is started from
target_compile_definitions(wiiboot PRIVATE
    GE_ROOT="${GE_HOME}"
    GE_HOSTS_DEFAULT="${GE_HOME}/config/hosts.txt"
    GE_GECKO_DEFAULT="${GE_GECKO_FILE}"
    GE_SETTINGS_FILE="${GE_SETTINGS_FILE}")
# the window's icon (port/package/icon.bmp, from icon.tga) as an array in the executable, and on
# Windows the executable's own icon as a resource (port/package/win/icon.rc)
file(READ ${GE_PORT}/package/icon.bmp GE_ICON_HEX HEX)
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
  target_sources(wiiboot PRIVATE ${GE_PORT}/package/win/icon.rc)
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
# loads TASLib at run time (port/tas/), dlopen on linux
target_link_libraries(wiiboot ${CMAKE_DL_LIBS})
# TASLib, the private submodule: built as its own shared library beside wiiboot when it's checked
# out, never linked in, an empty TASLib/ just leaves it out
set(GE_TASLIB_DIR ${GE_ROOT}/TASLib)
if(EXISTS ${GE_TASLIB_DIR}/CMakeLists.txt)
  set(GE_TAS_API_DIR ${GE_PORT}/tas)
  add_subdirectory(${GE_TASLIB_DIR} ${CMAKE_BINARY_DIR}/TASLib)
  set_target_properties(TASLib PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
      CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  if(WIN32)
    set_target_properties(TASLib PROPERTIES PREFIX "")
  endif()
  add_dependencies(wiiboot TASLib)
endif()
# what starts the game without a game folder on the command line (launcher/)
include(${GE_ROOT}/launcher/launcher.cmake)
