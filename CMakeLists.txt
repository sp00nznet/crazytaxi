cmake_minimum_required(VERSION 3.16)
project(CrazyTaxi C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(MSVC)
    add_compile_options(/W3 /fp:fast /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -O2 -ffast-math)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    endif()
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# ========== Find SDL2 (optional) ==========
find_package(SDL2 QUIET CONFIG)
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
endif()
if(SDL2_FOUND)
    message(STATUS "SDL2 found")
    add_compile_definitions(HAS_SDL2)
else()
    message(STATUS "SDL2 not found - building in headless mode")
endif()

# ========== Find OpenGL + GLEW ==========
find_package(OpenGL QUIET)
find_package(GLEW QUIET CONFIG)
if(OPENGL_FOUND AND GLEW_FOUND)
    message(STATUS "OpenGL + GLEW found")
    add_compile_definitions(HAS_OPENGL)
else()
    message(STATUS "OpenGL/GLEW not found - rendering disabled")
endif()

# ========== Core library (CPU + HAL) ==========
add_library(ct_core STATIC
    src/recompiler/sh4_cpu.c
    src/hal/dc_hardware.c
    src/hal/pvr2_ta.c
    src/hal/pvr2_render.c
    src/platform/platform_sdl2.c
)
if(MSVC)
    target_compile_options(ct_core PRIVATE /O2)
endif()

# ct_core needs SDL2/OpenGL headers for platform_sdl2.c
if(SDL2_FOUND)
    if(TARGET SDL2::SDL2)
        target_link_libraries(ct_core PUBLIC SDL2::SDL2)
    else()
        target_include_directories(ct_core PUBLIC ${SDL2_INCLUDE_DIRS})
    endif()
endif()
if(OPENGL_FOUND AND GLEW_FOUND)
    target_link_libraries(ct_core PUBLIC OpenGL::GL GLEW::GLEW)
endif()

# ========== Recompiled game code ==========
file(GLOB GAME_SOURCES src/game/game_code_*.c)
list(APPEND GAME_SOURCES src/game/dispatch_table.c)
list(APPEND GAME_SOURCES src/game/game_stubs.c)

# Split game code into multiple libraries to speed up parallel compilation
set(GAME_LIBS)
set(BATCH_SIZE 6)
list(LENGTH GAME_SOURCES GAME_SOURCE_COUNT)
math(EXPR NUM_BATCHES "(${GAME_SOURCE_COUNT} + ${BATCH_SIZE} - 1) / ${BATCH_SIZE}")

set(idx 0)
foreach(batch_num RANGE 0 ${NUM_BATCHES})
    set(BATCH_SOURCES)
    foreach(i RANGE 1 ${BATCH_SIZE})
        if(idx LESS GAME_SOURCE_COUNT)
            list(GET GAME_SOURCES ${idx} src)
            list(APPEND BATCH_SOURCES ${src})
            math(EXPR idx "${idx} + 1")
        endif()
    endforeach()

    if(BATCH_SOURCES)
        set(lib_name ct_game_${batch_num})
        add_library(${lib_name} STATIC ${BATCH_SOURCES})
        if(MSVC)
            target_compile_options(${lib_name} PRIVATE /O1)
        endif()
        list(APPEND GAME_LIBS ${lib_name})
    endif()
endforeach()

# ========== Main executable ==========
add_executable(crazytaxi src/main.c)
target_link_libraries(crazytaxi ct_core ${GAME_LIBS})

# Large stack for deep recompiled call chains
if(MSVC)
    target_link_options(crazytaxi PRIVATE /STACK:67108864)  # 64MB stack
else()
    target_link_options(crazytaxi PRIVATE -Wl,-z,stacksize=67108864)
endif()

# SDL2main needed for WinMain entry point
if(SDL2_FOUND AND TARGET SDL2::SDL2main)
    target_link_libraries(crazytaxi SDL2::SDL2main)
endif()

# Math library on Unix
if(UNIX)
    target_link_libraries(crazytaxi m)
endif()

# ========== Install ==========
install(TARGETS crazytaxi RUNTIME DESTINATION bin)

# Print summary
message(STATUS "")
message(STATUS "=== Crazy Taxi Static Recompilation ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Game source files: ${GAME_SOURCE_COUNT}")
message(STATUS "SDL2: ${SDL2_FOUND}")
message(STATUS "")
