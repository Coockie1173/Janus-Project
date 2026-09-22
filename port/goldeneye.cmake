# The port's targets, included by the generated build with
# -DWIIKIT_EXTRA=<this file>: the game's own layer linked into wiiboot.
target_sources(wiiboot PRIVATE ${CMAKE_CURRENT_LIST_DIR}/goldeneye.cpp ${CMAKE_CURRENT_LIST_DIR}/debug.cpp)
# the hosts file, found wherever the game is started from
target_compile_definitions(wiiboot PRIVATE GE_HOSTS_DEFAULT="${CMAKE_CURRENT_LIST_DIR}/../config/hosts.txt")
target_compile_definitions(wiiboot PRIVATE GE_GECKO_DEFAULT="${CMAKE_CURRENT_LIST_DIR}/../build/patched/gecko.ram")
