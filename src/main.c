/**
 * Crazy Taxi - Static Recompilation
 *
 * Main entry point for the statically recompiled version of
 * Crazy Taxi (Dreamcast, 1999).
 *
 * Architecture:
 *   1. Initialize SH-4 CPU state
 *   2. Load game data into emulated RAM
 *   3. Initialize Dreamcast hardware abstraction
 *   4. Execute recompiled game code
 *   5. Route hardware accesses through HAL -> SDL2/OpenGL
 */

#include "recompiler/sh4_cpu.h"
#include "hal/dc_hardware.h"
#include "platform/platform.h"
#include "game/game_functions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dreamcast display resolution */
#define DC_SCREEN_WIDTH  640
#define DC_SCREEN_HEIGHT 480

/* Game data directory */
#define GAME_DATA_DIR "disc_extract"

/* External: set hardware reference in CPU module */
extern void sh4_set_hardware(DCHardware *hw);

static int load_game_binary(SH4CPU *cpu, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot open %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Load at 0x8C010000 (offset 0x010000 in RAM) */
    uint32_t load_offset = GAME_LOAD_ADDR - DC_RAM_BASE;
    if (load_offset + size > DC_RAM_SIZE) {
        fprintf(stderr, "ERROR: Binary too large (%ld bytes)\n", size);
        fclose(f);
        return -1;
    }

    size_t read = fread(cpu->ram + load_offset, 1, size, f);
    fclose(f);

    printf("Loaded %s: %zu bytes at 0x%08X\n", path, read, GAME_LOAD_ADDR);
    return 0;
}

static int load_game_data(SH4CPU *cpu, const char *datadir) {
    char path[512];

    /* Load main executable */
    snprintf(path, sizeof(path), "%s/1ST_READ.BIN", datadir);
    if (load_game_binary(cpu, path) < 0) return -1;

    /* Load AICA sound driver into sound RAM */
    snprintf(path, sizeof(path), "%s/AICADRV.BIN", datadir);
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size <= DC_AICA_SIZE) {
            fread(cpu->aica_ram, 1, size, f);
            printf("Loaded %s: %ld bytes into AICA RAM\n", path, size);
        }
        fclose(f);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    const char *datadir = GAME_DATA_DIR;

    printf("===========================================\n");
    printf("  Crazy Taxi - Static Recompilation\n");
    printf("  Original: Sega (1999, Dreamcast)\n");
    printf("===========================================\n\n");

    if (argc > 1) {
        datadir = argv[1];
    }

    /* Initialize CPU */
    SH4CPU cpu;
    sh4_init(&cpu);
    printf("SH-4 CPU initialized (16MB RAM, 8MB VRAM)\n");

    /* Initialize hardware */
    DCHardware *hw = dc_hw_init();
    if (!hw) {
        fprintf(stderr, "FATAL: Hardware init failed\n");
        sh4_destroy(&cpu);
        return 1;
    }
    sh4_set_hardware(hw);

    /* Initialize hardware subsystems */
    dc_pvr_init(hw);
    dc_maple_init(hw);
    dc_aica_init(hw);
    dc_gdrom_init(hw);

    /* Load game data */
    printf("\nLoading game data from %s/...\n", datadir);
    if (load_game_data(&cpu, datadir) < 0) {
        fprintf(stderr, "FATAL: Failed to load game data\n");
        dc_hw_destroy(hw);
        sh4_destroy(&cpu);
        return 1;
    }

    /* Initialize platform (window + input) */
    if (platform_init(DC_SCREEN_WIDTH, DC_SCREEN_HEIGHT, "Crazy Taxi") < 0) {
        fprintf(stderr, "FATAL: Platform init failed\n");
        dc_hw_destroy(hw);
        sh4_destroy(&cpu);
        return 1;
    }

    /* Set initial CPU state */
    cpu.pc = GAME_LOAD_ADDR;
    cpu.r[15] = 0x8C00F400; /* Stack pointer */

    printf("\nStarting game execution at 0x%08X...\n\n", cpu.pc);

    /* Execute the game entry point */
    func_8C010000(&cpu);

    /* Main game loop */
    uint64_t last_time = platform_get_ticks_ms();
    int frames = 0;

    while (cpu.running && platform_poll_events(hw)) {
        /* The game's main loop is driven by vblank interrupts.
         * In the recompiled version, we simulate this by:
         * 1. Calling the game's frame update function
         * 2. Presenting the rendered frame
         * 3. Waiting for vsync timing
         */

        /* Signal VBlank to the game */
        dc_pvr_wait_vblank(hw);

        /* Swap buffers */
        platform_swap_buffers();

        /* Frame timing */
        frames++;
        uint64_t now = platform_get_ticks_ms();
        if (now - last_time >= 1000) {
            char title[128];
            snprintf(title, sizeof(title), "Crazy Taxi [%d FPS]", frames);
            platform_set_title(title);
            frames = 0;
            last_time = now;
        }

        /* Target ~60fps (16.67ms per frame) */
        uint64_t frame_time = platform_get_ticks_ms() - now;
        if (frame_time < 16) {
            platform_sleep_ms(16 - (uint32_t)frame_time);
        }
    }

    printf("\nShutting down...\n");
    platform_shutdown();
    dc_hw_destroy(hw);
    sh4_destroy(&cpu);

    printf("Goodbye!\n");
    return 0;
}
