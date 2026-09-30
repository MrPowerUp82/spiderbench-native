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
find_path(OPENGL_REGISTRY_INCLUDE GL/glcorearb.h REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(spiderbench ${SOURCES})
target_include_directories(spiderbench PRIVATE src ${JSONCPP_INCLUDE} ${OPENGL_REGISTRY_INCLUDE})
if(WIN32)
  target_compile_definitions(spiderbench PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(spiderbench PRIVATE SDL2::SDL2main SDL2::SDL2 ZLIB::ZLIB ${JSONCPP_LIB})
if(WIN32)
  target_link_libraries(spiderbench PRIVATE opengl32)
  # console subsystem in Debug (logs), windowed in Release
  if(MINGW AND CMAKE_BUILD_TYPE STREQUAL "Release")
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
target_compile_definitions(spiderbench PRIVATE SB_SOURCE_SHADERS="${CMAKE_SOURCE_DIR}/build/refshaders")
target_compile_definitions(spiderbench PRIVATE SB_SOURCE_BAKE="${CMAKE_SOURCE_DIR}/build/city-bake")

add_custom_target(verify_baked_shaders
  COMMAND $<TARGET_FILE:spiderbench> --validate-baked-shaders
  DEPENDS spiderbench
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Compiling the captured Three.js programs on the native GPU")
add_custom_target(verify_baked_pools
  COMMAND $<TARGET_FILE:spiderbench> --validate-baked-pools
  DEPENDS spiderbench
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Comparing native pool selection with the original JS")
add_custom_target(verify_baked_csm
  COMMAND $<TARGET_FILE:spiderbench> --validate-baked-csm
  DEPENDS spiderbench
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Comparing native shadow cascade fitting with the original JS")
add_custom_target(verify_baked_tiles
  COMMAND $<TARGET_FILE:spiderbench> --validate-baked-tiles
  DEPENDS spiderbench
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Comparing native tile LOD and hysteresis with the original JS")
add_custom_target(verify_baked_environment
  COMMAND $<TARGET_FILE:spiderbench> --validate-baked-environment
  DEPENDS spiderbench
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Replaying and validating the original sky and PMREM passes on the native GPU")

# Explicit bake step for the faithful JS city. Run with `cmake --build build --target bake_city`.
# Each command builds the original city once; keep them serial to avoid holding
# multiple copies of its ~3 GiB scene in memory at the same time.
find_program(SB_NODE_EXECUTABLE NAMES node)
find_program(SB_PYTHON_EXECUTABLE NAMES python python3)
if(SB_NODE_EXECUTABLE AND SB_PYTHON_EXECUTABLE)
  add_custom_target(bake_city
    COMMAND "${SB_NODE_EXECUTABLE}" --max-old-space-size=8192 tools/ref/bake_collision.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" --max-old-space-size=8192 tools/ref/bake_geometry.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" --max-old-space-size=8192 tools/ref/bake_pools.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" --max-old-space-size=4096 tools/ref/pack_pools.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" --max-old-space-size=4096 tools/ref/verify_pool_data.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" --max-old-space-size=8192 tools/ref/capture_shaders.mjs --city
    COMMAND "${SB_NODE_EXECUTABLE}" tools/ref/capture_environment.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" tools/ref/verify_environment.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" tools/ref/verify_csm.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" --max-old-space-size=8192 tools/ref/verify_tiles.mjs
    COMMAND "${SB_PYTHON_EXECUTABLE}" tools/ref/verify_collision.py
    COMMAND "${SB_NODE_EXECUTABLE}" tools/ref/verify_geometry.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" --max-old-space-size=4096 tools/ref/verify_pools.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" tools/ref/verify_bake_links.mjs
    COMMAND "${SB_NODE_EXECUTABLE}" tools/ref/verify_textures.mjs
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Baking and validating the original Spiderbench city")
endif()
