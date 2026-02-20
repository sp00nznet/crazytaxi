/**
 * SH-4 CPU State Implementation
 *
 * Manages CPU state and memory access for the statically recompiled game.
 * Memory accesses are routed through this layer to handle the Dreamcast
 * memory map, including mirrored regions and hardware register access.
 */

#include "recompiler/sh4_cpu.h"
#include "hal/dc_hardware.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* External hardware reference (set during init) */
static DCHardware *g_hardware = NULL;

void sh4_set_hardware(DCHardware *hw) {
    g_hardware = hw;
}

void sh4_init(SH4CPU *cpu) {
    memset(cpu, 0, sizeof(SH4CPU));

    /* Allocate memory regions */
    cpu->ram = (uint8_t *)calloc(1, DC_RAM_SIZE);
    cpu->vram = (uint8_t *)calloc(1, DC_VRAM_SIZE);
    cpu->aica_ram = (uint8_t *)calloc(1, DC_AICA_SIZE);

    if (!cpu->ram || !cpu->vram || !cpu->aica_ram) {
        fprintf(stderr, "FATAL: Failed to allocate Dreamcast memory\n");
        exit(1);
    }

    sh4_reset(cpu);
}

void sh4_reset(SH4CPU *cpu) {
    /* Reset registers to power-on defaults */
    memset(cpu->r, 0, sizeof(cpu->r));
    memset(cpu->r_bank, 0, sizeof(cpu->r_bank));

    cpu->sr = 0x700000F0;   /* MD=1, RB=1, BL=1, IMASK=0xF */
    cpu->gbr = 0;
    cpu->vbr = 0;
    cpu->mach = 0;
    cpu->macl = 0;
    cpu->pr = 0;
    cpu->pc = GAME_LOAD_ADDR;

    /* FPU defaults */
    cpu->fpscr = 0x00040001; /* DN=1, RM=nearest */
    cpu->fpul = 0;
    memset(cpu->fr, 0, sizeof(cpu->fr));
    memset(cpu->xf, 0, sizeof(cpu->xf));

    /* Stack pointer - Dreamcast default */
    cpu->r[15] = 0x8C00F400;

    cpu->running = true;
    cpu->cycles = 0;
    cpu->delay_slot = 0;
}

void sh4_destroy(SH4CPU *cpu) {
    free(cpu->ram);
    free(cpu->vram);
    free(cpu->aica_ram);
    cpu->ram = NULL;
    cpu->vram = NULL;
    cpu->aica_ram = NULL;
}

/* ========== Memory Access ========== */

/* Translate SH-4 virtual address to physical address */
static uint32_t translate_addr(uint32_t addr) {
    /* P0/U0 area (0x00000000-0x7FFFFFFF): user space, cached */
    /* P1 area (0x80000000-0x9FFFFFFF): kernel, cached */
    /* P2 area (0xA0000000-0xBFFFFFFF): kernel, uncached */
    /* P3 area (0xC0000000-0xDFFFFFFF): kernel, cached (TLB) */
    /* P4 area (0xE0000000-0xFFFFFFFF): control registers */

    /* Strip P1/P2 cache bits to get physical address */
    return addr & ADDR_MASK_P1;
}

/* Check if address is a hardware register */
static bool is_hw_register(uint32_t phys_addr) {
    /* System Board registers: 0x005F6800 - 0x005F69FF */
    /* Maple registers: 0x005F6C00 - 0x005F6CFF */
    /* GD-ROM registers: 0x005F7000 - 0x005F70FF */
    /* PVR registers: 0x005F8000 - 0x005F9FFF */
    /* TA FIFO: 0x10000000 - 0x107FFFFF */
    if (phys_addr >= 0x005F6800 && phys_addr < 0x005FA000)
        return true;
    if (phys_addr >= 0x10000000 && phys_addr < 0x10800000)
        return true;
    if (phys_addr >= 0x10800000 && phys_addr < 0x11000000)
        return true;
    return false;
}

uint8_t sh4_read8(SH4CPU *cpu, uint32_t addr) {
    uint32_t phys = translate_addr(addr);

    /* Main RAM */
    if (phys >= DC_RAM_BASE && phys < DC_RAM_BASE + DC_RAM_SIZE) {
        return cpu->ram[phys & DC_RAM_MASK];
    }

    /* VRAM */
    if (phys >= DC_VRAM_BASE && phys < DC_VRAM_BASE + DC_VRAM_SIZE) {
        return cpu->vram[phys - DC_VRAM_BASE];
    }
    /* VRAM 32-bit access area */
    if (phys >= 0x05000000 && phys < 0x05800000) {
        return cpu->vram[phys - 0x05000000];
    }

    /* AICA RAM */
    if (phys >= DC_AICA_BASE && phys < DC_AICA_BASE + DC_AICA_SIZE) {
        return cpu->aica_ram[phys - DC_AICA_BASE];
    }

    /* Hardware register - byte access not common but handle it */
    if (is_hw_register(phys) && g_hardware) {
        uint32_t aligned = phys & ~3;
        uint32_t val = dc_hw_read32(g_hardware, aligned);
        int shift = (phys & 3) * 8;
        return (val >> shift) & 0xFF;
    }

    return 0;
}

uint16_t sh4_read16(SH4CPU *cpu, uint32_t addr) {
    uint32_t phys = translate_addr(addr);

    if (phys >= DC_RAM_BASE && phys < DC_RAM_BASE + DC_RAM_SIZE) {
        uint32_t offset = phys & DC_RAM_MASK;
        return *(uint16_t *)(cpu->ram + offset);
    }

    if (phys >= DC_VRAM_BASE && phys < DC_VRAM_BASE + DC_VRAM_SIZE) {
        return *(uint16_t *)(cpu->vram + (phys - DC_VRAM_BASE));
    }
    if (phys >= 0x05000000 && phys < 0x05800000) {
        return *(uint16_t *)(cpu->vram + (phys - 0x05000000));
    }

    if (phys >= DC_AICA_BASE && phys < DC_AICA_BASE + DC_AICA_SIZE) {
        return *(uint16_t *)(cpu->aica_ram + (phys - DC_AICA_BASE));
    }

    if (is_hw_register(phys) && g_hardware) {
        uint32_t aligned = phys & ~3;
        uint32_t val = dc_hw_read32(g_hardware, aligned);
        int shift = (phys & 2) * 8;
        return (val >> shift) & 0xFFFF;
    }

    return 0;
}

uint32_t sh4_read32(SH4CPU *cpu, uint32_t addr) {
    uint32_t phys = translate_addr(addr);

    if (phys >= DC_RAM_BASE && phys < DC_RAM_BASE + DC_RAM_SIZE) {
        uint32_t offset = phys & DC_RAM_MASK;
        return *(uint32_t *)(cpu->ram + offset);
    }

    if (phys >= DC_VRAM_BASE && phys < DC_VRAM_BASE + DC_VRAM_SIZE) {
        return *(uint32_t *)(cpu->vram + (phys - DC_VRAM_BASE));
    }
    if (phys >= 0x05000000 && phys < 0x05800000) {
        return *(uint32_t *)(cpu->vram + (phys - 0x05000000));
    }

    if (phys >= DC_AICA_BASE && phys < DC_AICA_BASE + DC_AICA_SIZE) {
        return *(uint32_t *)(cpu->aica_ram + (phys - DC_AICA_BASE));
    }

    if (is_hw_register(phys) && g_hardware) {
        return dc_hw_read32(g_hardware, phys);
    }

    return 0;
}

float sh4_read_float(SH4CPU *cpu, uint32_t addr) {
    union { uint32_t u; float f; } conv;
    conv.u = sh4_read32(cpu, addr);
    return conv.f;
}

void sh4_write8(SH4CPU *cpu, uint32_t addr, uint8_t val) {
    uint32_t phys = translate_addr(addr);

    if (phys >= DC_RAM_BASE && phys < DC_RAM_BASE + DC_RAM_SIZE) {
        cpu->ram[phys & DC_RAM_MASK] = val;
        return;
    }

    if (phys >= DC_VRAM_BASE && phys < DC_VRAM_BASE + DC_VRAM_SIZE) {
        cpu->vram[phys - DC_VRAM_BASE] = val;
        return;
    }
    if (phys >= 0x05000000 && phys < 0x05800000) {
        cpu->vram[phys - 0x05000000] = val;
        return;
    }

    if (phys >= DC_AICA_BASE && phys < DC_AICA_BASE + DC_AICA_SIZE) {
        cpu->aica_ram[phys - DC_AICA_BASE] = val;
        return;
    }

    if (is_hw_register(phys) && g_hardware) {
        /* Byte writes to hardware - read-modify-write */
        uint32_t aligned = phys & ~3;
        uint32_t cur = dc_hw_read32(g_hardware, aligned);
        int shift = (phys & 3) * 8;
        cur &= ~(0xFF << shift);
        cur |= (uint32_t)val << shift;
        dc_hw_write32(g_hardware, aligned, cur);
    }
}

void sh4_write16(SH4CPU *cpu, uint32_t addr, uint16_t val) {
    uint32_t phys = translate_addr(addr);

    if (phys >= DC_RAM_BASE && phys < DC_RAM_BASE + DC_RAM_SIZE) {
        *(uint16_t *)(cpu->ram + (phys & DC_RAM_MASK)) = val;
        return;
    }

    if (phys >= DC_VRAM_BASE && phys < DC_VRAM_BASE + DC_VRAM_SIZE) {
        *(uint16_t *)(cpu->vram + (phys - DC_VRAM_BASE)) = val;
        return;
    }
    if (phys >= 0x05000000 && phys < 0x05800000) {
        *(uint16_t *)(cpu->vram + (phys - 0x05000000)) = val;
        return;
    }

    if (phys >= DC_AICA_BASE && phys < DC_AICA_BASE + DC_AICA_SIZE) {
        *(uint16_t *)(cpu->aica_ram + (phys - DC_AICA_BASE)) = val;
        return;
    }

    if (is_hw_register(phys) && g_hardware) {
        uint32_t aligned = phys & ~3;
        uint32_t cur = dc_hw_read32(g_hardware, aligned);
        int shift = (phys & 2) * 8;
        cur &= ~(0xFFFF << shift);
        cur |= (uint32_t)val << shift;
        dc_hw_write32(g_hardware, aligned, cur);
    }
}

void sh4_write32(SH4CPU *cpu, uint32_t addr, uint32_t val) {
    uint32_t phys = translate_addr(addr);

    if (phys >= DC_RAM_BASE && phys < DC_RAM_BASE + DC_RAM_SIZE) {
        *(uint32_t *)(cpu->ram + (phys & DC_RAM_MASK)) = val;
        return;
    }

    if (phys >= DC_VRAM_BASE && phys < DC_VRAM_BASE + DC_VRAM_SIZE) {
        *(uint32_t *)(cpu->vram + (phys - DC_VRAM_BASE)) = val;
        return;
    }
    if (phys >= 0x05000000 && phys < 0x05800000) {
        *(uint32_t *)(cpu->vram + (phys - 0x05000000)) = val;
        return;
    }

    if (phys >= DC_AICA_BASE && phys < DC_AICA_BASE + DC_AICA_SIZE) {
        *(uint32_t *)(cpu->aica_ram + (phys - DC_AICA_BASE)) = val;
        return;
    }

    /* Store Queue (0xE0000000-0xE3FFFFFF) */
    if (addr >= 0xE0000000 && addr <= 0xE3FFFFFF) {
        /* Store queues write to VRAM/TA - handle in HAL */
        if (g_hardware) {
            dc_hw_write32(g_hardware, phys, val);
        }
        return;
    }

    if (is_hw_register(phys) && g_hardware) {
        dc_hw_write32(g_hardware, phys, val);
    }
}

void sh4_write_float(SH4CPU *cpu, uint32_t addr, float val) {
    union { float f; uint32_t u; } conv;
    conv.f = val;
    sh4_write32(cpu, addr, conv.u);
}
