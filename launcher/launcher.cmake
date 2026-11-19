# The launcher: settings and options into the environment, the disc extracted on a first start,
# then the game. Included by port/goldeneye.cmake. A package (GE_PORTABLE) keeps everything next
# to the executable, a checkout keeps it in build/ (launch.sh starts it there).
set(GE_LAUNCHER ${CMAKE_CURRENT_LIST_DIR})
set(GE_CODECS ${GE_ROOT}/build/deps/codecs)
if(NOT EXISTS ${GE_CODECS}/zstd/lib/zstd.h)
  message(FATAL_ERROR "no codecs in ${GE_CODECS}: sh tools/get_codecs.sh build/deps")
endif()
if(NOT EXISTS ${GE_IMGUI}/imgui.cpp)
  message(FATAL_ERROR "the launcher's window needs Dear ImGui: sh tools/get_imgui.sh build/deps")
endif()

# the decompressors for .rvz and .wia: zstd's decoder, bzip2, the LZMA SDK's two decoders
enable_language(C)
file(GLOB GE_ZSTD ${GE_CODECS}/zstd/lib/common/*.c ${GE_CODECS}/zstd/lib/decompress/*.c)
add_library(ge_codecs STATIC ${GE_ZSTD}
    ${GE_CODECS}/bzip2/blocksort.c ${GE_CODECS}/bzip2/huffman.c ${GE_CODECS}/bzip2/crctable.c
    ${GE_CODECS}/bzip2/randtable.c ${GE_CODECS}/bzip2/compress.c ${GE_CODECS}/bzip2/decompress.c
    ${GE_CODECS}/bzip2/bzlib.c
    ${GE_CODECS}/lzma/C/LzmaDec.c ${GE_CODECS}/lzma/C/Lzma2Dec.c)
target_include_directories(ge_codecs PUBLIC ${GE_CODECS}/zstd/lib ${GE_CODECS}/bzip2 ${GE_CODECS}/lzma/C)
target_compile_definitions(ge_codecs PRIVATE ZSTD_DISABLE_ASM)
set_target_properties(ge_codecs PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
target_compile_options(ge_codecs PRIVATE -w)

target_sources(wiiboot PRIVATE
    ${GE_LAUNCHER}/launch.cpp ${GE_LAUNCHER}/window.cpp ${GE_LAUNCHER}/disc.cpp ${GE_LAUNCHER}/extract.cpp ${GE_LAUNCHER}/update.cpp)
target_link_libraries(wiiboot ge_codecs)
if(GE_PORTABLE)
  target_compile_definitions(wiiboot PRIVATE GE_LAUNCH_PORTABLE=1 GE_LAUNCH_STATE="." GE_LAUNCH_SYMBOLS="config/symbols.tsv" GE_LAUNCH_FONTS="")
else()
  # GE_FONTS: the Makefile's FONTS, Dolphin's Sys/GC when it's installed
  target_compile_definitions(wiiboot PRIVATE GE_LAUNCH_STATE="build" GE_LAUNCH_SYMBOLS="build/recomp/symbols.tsv" GE_LAUNCH_FONTS="${GE_FONTS}")
endif()
if(WIN32 AND GE_PORTABLE)
  # no console window behind the game: the log is run.log, --extract finds the one it was started from
  set_target_properties(wiiboot PROPERTIES WIN32_EXECUTABLE ON)
endif()
