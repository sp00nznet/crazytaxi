/**
 * Crazy Taxi - Static Recompilation
 *
 * Main entry point for the statically recompiled version of
 * Crazy Taxi (Dreamcast, 1999).
 * Uses dcrecomp framework for SH-4 CPU, PVR2 GPU, and hardware abstraction.
 *
 * Architecture:
 *   1. Initialize SH-4 CPU state (via dcrecomp)
 *   2. Load game data into emulated RAM
 *   3. Initialize Dreamcast hardware abstraction (via dcrecomp)
 *   4. Execute recompiled game code
 *   5. Route hardware accesses through dcrecomp HAL -> SDL2/OpenGL
 */

#include "recompiler/sh4_cpu.h"
#include "hal/dc_hardware.h"
#include "hal/pvr2.h"
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

/* Game info */
#define GAME_TITLE "Crazy Taxi"
#define GAME_INIT_FUNC 0x8C148962

/* Indirect dispatch (implemented in dispatch_table.c) */
void sh4_call_indirect(SH4CPU *cpu);
void sh4_jump_indirect(SH4CPU *cpu);

/* Global state */
static SH4CPU g_cpu;
static DCHardware *g_hw = NULL;

/* VBlank/IRQ entry.
 *
 * On hardware the SH-4 takes interrupts through VBR+0x600. Crazy Taxi sets
 * VBR = 0x8C00F400 (ldc r0,VBR at 0x8C010110), so the vector sits at
 * 0x8C00FA00 - below the 0x8C010000 load address, in a trampoline the game
 * copies into low RAM at boot. The recompiler never saw it, so the dispatcher
 * behind it was dead code.
 *
 * The real entry is 0x8C16A590: seven register pushes falling through into the
 * dispatcher at 0x8C16A59E, which reads SB_ISTNRM and calls func_8C169F40 for
 * VBlank. That ISR increments the frame counter at 0x0C2E7E90 that every
 * "wait N frames" loop in init spins on. Reproduce the pushes here rather than
 * hand-editing generated code.
 */
static void ct_irq_handler(SH4CPU *cpu) {
    for (int i = 14; i >= 8; i--) {
        cpu->r[15] -= 4;
        sh4_write32(cpu, cpu->r[15], cpu->r[i]);
    }
    func_8C16A59E(cpu);
}

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
    uint32_t load_offset = GAME_LOAD_ADDR & cpu->ram_mask;
    if ((uint32_t)(load_offset + size) > cpu->ram_size) {
        fprintf(stderr, "ERROR: Binary too large (%ld bytes)\n", size);
        fclose(f);
        return -1;
    }

    size_t read = fread(cpu->ram + load_offset, 1, size, f);
    fclose(f);

    printf("[BOOT] Loaded %s: %zu bytes at 0x%08X\n", path, read, GAME_LOAD_ADDR);
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
            printf("[BOOT] Loaded %s: %ld bytes into AICA RAM\n", path, size);
        }
        fclose(f);
    }

    return 0;
}

/**
 * Initialize Dreamcast BIOS-like state.
 * Sets up CPU registers and memory patterns that the game expects.
 */
static void dc_bios_init(SH4CPU *cpu) {
    /* Fill 0x8C00C000-0x8C00F400 with "SEGA" pattern (normally done by BIOS) */
    uint32_t bios_start = 0x8C00C000 & cpu->ram_mask;
    uint32_t bios_end = 0x8C00F400 & cpu->ram_mask;
    uint32_t sega = 0x41474553u; /* "SEGA" little-endian */
    for (uint32_t off = bios_start; off < bios_end; off += 4) {
        memcpy(cpu->ram + off, &sega, 4);
    }

    /* Set initial CPU state - bypass BIOS bootstrap, call game init directly.
     * The original bootstrap at 0x8C010000 copies code to 0x8C004000 and
     * reads BIOS dispatch tables we don't have. */
    cpu->pc = GAME_INIT_FUNC;
    cpu->r[15] = 0x8C00FC00u;   /* Stack pointer */
    cpu->vbr = 0x8C00F400u;     /* Vector Base Register */
    cpu->r[2] = 0x8C000000u;    /* r2 needs to point to readable memory */
    cpu->sr = 0x700000F0u;      /* Supervisor mode, interrupts masked */
    cpu->fpscr = 0x00040001;    /* DN=1, RM=nearest */
    cpu->pr = 0;                /* No return address (top-level) */

    printf("[BOOT] Dreamcast BIOS state initialized\n");
    printf("[BOOT]   SR=0x%08X VBR=0x%08X SP=0x%08X FPSCR=0x%08X\n",
           cpu->sr, cpu->vbr, cpu->r[15], cpu->fpscr);
}

int main(int argc, char *argv[]) {
    const char *datadir = GAME_DATA_DIR;

    printf("=== %s ===\n", GAME_TITLE);
    printf("Static Recompilation (Sega Dreamcast / SH-4)\n");
    printf("Powered by dcrecomp framework\n\n");

    if (argc > 1) {
        datadir = argv[1];
    }

    /* Initialize SH-4 CPU with Dreamcast's 16MB RAM */
    sh4_init_ex(&g_cpu, DC_RAM_SIZE_16MB);

    /* Initialize hardware */
    g_hw = dc_hw_init();
    if (!g_hw) {
        fprintf(stderr, "FATAL: Hardware init failed\n");
        sh4_destroy(&g_cpu);
        return 1;
    }

    /* Connect CPU to hardware */
    sh4_set_hardware(g_hw);
    sh4_set_cpu_ref(&g_cpu);

    /* Initialize hardware subsystems */
    dc_pvr_init(g_hw);
    dc_maple_init(g_hw);
    dc_aica_init(g_hw);
    dc_gdrom_init(g_hw);

    /* Load game data */
    printf("[BOOT] Loading game data from %s/...\n", datadir);
    if (load_game_data(&g_cpu, datadir) < 0) {
        printf("\nNo game binary found at '%s/1ST_READ.BIN'.\n", datadir);
        printf("Extract the game disc first:\n");
        printf("  python dcrecomp/tools/extract_gdi.py <game.gdi> disc_extract\n");
        goto cleanup;
    }

    /* Initialize platform (window + input) */
    if (platform_init(DC_SCREEN_WIDTH, DC_SCREEN_HEIGHT, GAME_TITLE) < 0) {
        fprintf(stderr, "FATAL: Platform init failed\n");
        goto cleanup;
    }

    /* Initialize PVR2 TA + renderer */
    pvr2_ta_init();
    if (pvr2_render_init(DC_SCREEN_WIDTH, DC_SCREEN_HEIGHT) < 0) {
        fprintf(stderr, "WARNING: PVR2 renderer init failed (running without rendering)\n");
    }

    /* Set up BIOS state and entry point */
    dc_bios_init(&g_cpu);

    /* Deliver VBlank to the game's own IRQ dispatcher */
    sh4_set_irq_handler(ct_irq_handler);

    printf("[BOOT] Starting game execution at 0x%08X...\n\n", g_cpu.pc);
    fflush(stdout);

    /* Execute the game's main init function */
    func_8C148962(&g_cpu);
    printf("[BOOT] Game init returned, entering main loop\n");
    fflush(stdout);

    /* Main game loop */
    uint64_t last_time = platform_get_ticks_ms();
    int frames = 0;

    while (g_cpu.running && platform_poll_events(g_hw)) {
        /* Signal VBlank to the game */
        dc_pvr_wait_vblank(g_hw);

        /* Swap buffers */
        platform_swap_buffers();

        /* Frame timing */
        frames++;
        uint64_t now = platform_get_ticks_ms();
        if (now - last_time >= 1000) {
            char title[128];
            snprintf(title, sizeof(title), "%s [%d FPS]", GAME_TITLE, frames);
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

cleanup:
    printf("\n[BOOT] Shutting down...\n");
    pvr2_render_destroy();
    pvr2_ta_destroy();
    platform_shutdown();
    dc_hw_destroy(g_hw);
    sh4_destroy(&g_cpu);

    printf("Goodbye!\n");
    return 0;
}
