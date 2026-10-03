# The port's targets, included by the generated build with
# -DWIIKIT_EXTRA=<this file>: the game's own layer linked into wiiboot.
set(GE_PORT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(GE_ROOT ${GE_PORT}/.. ABSOLUTE)
target_sources(wiiboot PRIVATE
    ${GE_PORT}/goldeneye.cpp ${GE_PORT}/framerate.cpp ${GE_PORT}/input.cpp ${GE_PORT}/fov.cpp
    ${GE_PORT}/gecko.cpp ${GE_PORT}/net.cpp ${GE_PORT}/overlay.cpp ${GE_PORT}/dmw.cpp
    ${GE_PORT}/diagnostics.cpp ${GE_PORT}/debug.cpp ${GE_PORT}/native.cpp ${GE_PORT}/crashdump.cpp)
# files found wherever the game is started from
target_compile_definitions(wiiboot PRIVATE
    GE_ROOT="${GE_ROOT}"
    GE_HOSTS_DEFAULT="${GE_ROOT}/config/hosts.txt"
    GE_GECKO_DEFAULT="${GE_ROOT}/build/patched/gecko.txt")
# the debug overlay: Dear ImGui's sources (tools/get_imgui.sh), compiled in when they are there
set(GE_IMGUI ${GE_ROOT}/build/deps/imgui)
if(EXISTS ${GE_IMGUI}/imgui.cpp)
  target_sources(wiiboot PRIVATE
      ${GE_IMGUI}/imgui.cpp ${GE_IMGUI}/imgui_draw.cpp ${GE_IMGUI}/imgui_tables.cpp ${GE_IMGUI}/imgui_widgets.cpp
      ${GE_IMGUI}/backends/imgui_impl_sdl3.cpp ${GE_IMGUI}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(wiiboot PRIVATE ${GE_IMGUI} ${GE_IMGUI}/backends)
  target_compile_definitions(wiiboot PRIVATE GE_IMGUI=1)
  target_link_libraries(wiiboot ${CMAKE_DL_LIBS})
endif()
