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
    add_compile_options(/W3 /O2 /fp:fast)
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
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    message(STATUS "SDL2 found: ${SDL2_INCLUDE_DIRS}")
    add_compile_definitions(HAS_SDL2)
    include_directories(${SDL2_INCLUDE_DIRS})
else()
    message(STATUS "SDL2 not found - building in headless mode")
endif()

# ========== Core library (CPU + HAL) ==========
add_library(ct_core STATIC
    src/recompiler/sh4_cpu.c
    src/hal/dc_hardware.c
    src/platform/platform_sdl2.c
)

# ========== Recompiled game code ==========
file(GLOB GAME_SOURCES src/game/game_code_*.c)
list(APPEND GAME_SOURCES src/game/dispatch_table.c)

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
        list(APPEND GAME_LIBS ${lib_name})
    endif()
endforeach()

# ========== Main executable ==========
add_executable(crazytaxi src/main.c)
target_link_libraries(crazytaxi ct_core ${GAME_LIBS})

if(SDL2_FOUND)
    target_link_libraries(crazytaxi ${SDL2_LIBRARIES})
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
