cmake_minimum_required(VERSION 3.20)
project(spiderbench_native CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig QUIET)
find_path(JSONCPP_INCLUDE json/json.h)
find_library(JSONCPP_LIB jsoncpp REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(spiderbench ${SOURCES})
target_include_directories(spiderbench PRIVATE src ${JSONCPP_INCLUDE})
target_link_libraries(spiderbench PRIVATE SDL2::SDL2main SDL2::SDL2 ZLIB::ZLIB ${JSONCPP_LIB})
if(WIN32)
  target_link_libraries(spiderbench PRIVATE opengl32)
  # console subsystem in Debug (logs), windowed in Release
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(spiderbench PRIVATE -mwindows)
  endif()
endif()
if(MSVC)
  target_compile_options(spiderbench PRIVATE /W3)
else()
  target_compile_options(spiderbench PRIVATE -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare -march=native)
endif()

# MinGW: copy the runtime DLLs next to the exe so it starts outside the MSYS2 shell
if(MINGW)
  get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
  file(GLOB RUNTIME_DLLS "${MINGW_BIN}/libgcc_s_seh-1.dll" "${MINGW_BIN}/libwinpthread-1.dll" "${MINGW_BIN}/libstdc++-6.dll"
       "${MINGW_BIN}/SDL2.dll" "${MINGW_BIN}/zlib1.dll" "${MINGW_BIN}/libjsoncpp-*.dll")
  foreach(dll ${RUNTIME_DLLS})
    add_custom_command(TARGET spiderbench POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:spiderbench>")
  endforeach()
endif()

# assets are baked by tools/convert_assets.py into ./assets; the exe finds them next to itself or in the source tree
target_compile_definitions(spiderbench PRIVATE SB_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets")
