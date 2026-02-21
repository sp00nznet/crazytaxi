/**
 * SH-4 CPU State Implementation
 *
 * Manages CPU state and memory access for the statically recompiled game.
 * Memory accesses are routed through this layer to handle the Dreamcast
 * memory map, including mirrored regions and hardware register access.
 */

#include "recompiler/sh4_cpu.h"
#include "hal/dc_hardware.h"
#include "hal/pvr2.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* External hardware reference (set during init) */
static DCHardware *g_hardware = NULL;
static SH4CPU *g_cpu_ref = NULL;

void sh4_set_hardware(DCHardware *hw) {
    g_hardware = hw;
}

void sh4_set_cpu_ref(SH4CPU *cpu) {
    g_cpu_ref = cpu;
}

/* Get pointer to CPU RAM (for DMA transfers) */
uint8_t *sh4_get_ram_ptr(void) {
    return g_cpu_ref ? g_cpu_ref->ram : NULL;
}

/* Get pointer to CPU VRAM (for DMA transfers) */
uint8_t *sh4_get_vram_ptr(void) {
    return g_cpu_ref ? g_cpu_ref->vram : NULL;
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

    /* QACR defaults: route SQ0/SQ1 to TA FIFO area (0x10000000) */
    cpu->qacr[0] = 0x10;  /* bits[4:2]=4 → addr[28:26]=4 → 0x10000000 */
    cpu->qacr[1] = 0x10;
    cpu->fpul = 0;
    memset(cpu->fr, 0, sizeof(cpu->fr));
    memset(cpu->xf, 0, sizeof(cpu->xf));

    /* Stack pointer - Dreamcast default */
    cpu->r[15] = 0x8C00F400;

    cpu->running = true;
    cpu->cycles = 0;
    cpu->delay_slot = 0;

    /* MMU starts disabled - the heuristic in translate_addr_cpu handles
     * P0/P3 addresses that would need TLB mapping by redirecting them to RAM.
     * The game may enable the MMU and program TLB entries later. */
    cpu->mmucr = 0;

    printf("[MMU] Initialized (heuristic P0/P3 → RAM mapping active)\n");
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
static uint32_t translate_addr_cpu(SH4CPU *cpu, uint32_t addr) {
    /* P2 area (0xA0000000-0xBFFFFFFF): kernel, uncached - always direct map.
     * P2 writes go directly to hardware/memory without caching. */
    if (addr >= 0xA0000000 && addr < 0xC0000000) {
        return addr & ADDR_MASK_P1;
    }

    /* P1 area (0x80000000-0x9FFFFFFF): kernel, cached.
     * On real SH-4, P1 accesses go through the operand cache.
     * For addresses mapping to real memory (RAM/VRAM/AICA), direct map works.
     * For non-memory regions (TA FIFO at 0x10000000), the cache acts as scratch
     * RAM ("cache-as-RAM"). Redirect these to system RAM. */
    if (addr >= 0x80000000 && addr < 0xA0000000) {
        uint32_t phys = addr & ADDR_MASK_P1;
        /* Check if physical address is a real memory region */
        if (phys < 0x01000000) return phys;  /* Boot ROM, Flash, AICA */
        if (phys >= DC_VRAM_BASE && phys < DC_VRAM_BASE + DC_VRAM_SIZE) return phys;
        if (phys >= 0x05000000 && phys < 0x05800000) return phys;
        if (phys >= DC_RAM_BASE && phys < DC_RAM_BASE + DC_RAM_SIZE) return phys;
        if (phys >= 0x005F6800 && phys < 0x005FA000) return phys;  /* HW regs */
        /* Non-memory P1 address: cache-as-RAM, redirect to system RAM */
        return DC_RAM_BASE + (addr & DC_RAM_MASK);
    }

    /* P4 area (0xE0000000-0xFFFFFFFF): on-chip resources, NOT physical memory.
     * Includes Store Queue (0xE0-0xE3), cache arrays (0xF0-0xF5),
     * TLB arrays (0xF6-0xF7), control regs (0xFF).
     * Return unmapped sentinel - callers handle P4 before reaching memory logic. */
    if (addr >= 0xE0000000) {
        return 0xFFFFFFFF;  /* Unmapped - will not match any DC memory region */
    }

    /* P0/U0 (0x00000000-0x7FFFFFFF) and P3 (0xC0000000-0xDFFFFFFF):
     * Use TLB if MMU is enabled, otherwise apply heuristic mapping */
    if (cpu && (cpu->mmucr & 1)) {
        /* MMU enabled - search UTLB for matching entry */
        for (int i = 0; i < 64; i++) {
            uint32_t entry_addr = cpu->utlb_addr[i];
            if (!(entry_addr & 0x100)) continue; /* V (Valid) bit at bit 8 */

            uint32_t entry_data = cpu->utlb_data1[i];
            int sz = ((entry_data >> 6) & 2) | ((entry_data >> 4) & 1);
            uint32_t page_mask;
            switch (sz) {
            case 0: page_mask = 0xFFFFFC00; break; /* 1KB */
            case 1: page_mask = 0xFFFFF000; break; /* 4KB */
            case 2: page_mask = 0xFFFF0000; break; /* 64KB */
            case 3: page_mask = 0xFFF00000; break; /* 1MB */
            default: page_mask = 0xFFF00000; break;
            }

            uint32_t vpn = entry_addr & page_mask;
            if ((addr & page_mask) == vpn) {
                uint32_t ppn = entry_data & 0x1FFFFC00;
                uint32_t offset = addr & ~page_mask;
                return ppn | offset;
            }
        }
    }

    /* Direct map (MMU off or TLB miss) with Dreamcast heuristics.
     * On a real Dreamcast, P0/P3 addresses that don't correspond to any
     * physical memory region would go through TLB. Since we can't know
     * the exact TLB state, apply safe heuristics for common patterns. */
    uint32_t raw_phys = addr & ADDR_MASK_P1;

    /* If physical address lands in a valid DC region, use it directly */
    if (raw_phys < 0x01000000) return raw_phys;  /* Boot ROM, Flash, AICA */
    if (raw_phys >= DC_VRAM_BASE && raw_phys < DC_VRAM_BASE + DC_VRAM_SIZE)
        return raw_phys;  /* VRAM 64-bit */
    if (raw_phys >= 0x05000000 && raw_phys < 0x05800000)
        return raw_phys;  /* VRAM 32-bit */
    if (raw_phys >= DC_RAM_BASE && raw_phys < DC_RAM_BASE + DC_RAM_SIZE)
        return raw_phys;  /* System RAM */

    /* Non-memory physical address: redirect to system RAM.
     * This catches P0/P3 addresses that would normally be TLB-mapped to RAM
     * on a real Dreamcast (e.g., 0x7032CDxx → 0x0C32CD58). */
    return DC_RAM_BASE + (addr & DC_RAM_MASK);
}

/* Wrapper for backward compatibility */
static uint32_t translate_addr(uint32_t addr) {
    return translate_addr_cpu(g_cpu_ref, addr);
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
    /* P4 control registers (0xFF000000+) - check before translate_addr strips bits */
    if (addr >= 0xFF000000) {
        switch (addr) {
        case 0xFF000010: return cpu->mmucr;    /* MMUCR */
        case 0xFF000038: return cpu->qacr[0];  /* QACR0 */
        case 0xFF00003C: return cpu->qacr[1];  /* QACR1 */
        }
        /* DMAC registers: 0xFFA00000-0xFFA00040 */
        if (addr >= 0xFFA00000 && addr <= 0xFFA00040) {
            uint32_t idx = (addr - 0xFFA00000) / 4;
            if (idx < 17) return cpu->dmac_regs[idx];
        }
        /* TMU registers: 0xFFD80000-0xFFD8002F */
        if (addr >= 0xFFD80000 && addr <= 0xFFD8002F) {
            uint32_t idx = (addr - 0xFFD80000) / 4;
            if (idx < 12) return cpu->tmu_regs[idx];
        }
        return 0;
    }

    /* UTLB Address Array: 0xF6000000-0xF6FFFFFF */
    if (addr >= 0xF6000000 && addr < 0xF7000000) {
        int entry = (addr >> 8) & 63;
        return cpu->utlb_addr[entry];
    }
    /* UTLB Data Array 1: 0xF7000000-0xF77FFFFF */
    if (addr >= 0xF7000000 && addr < 0xF7800000) {
        int entry = (addr >> 8) & 63;
        return cpu->utlb_data1[entry];
    }
    /* UTLB Data Array 2: 0xF7800000-0xF7FFFFFF */
    if (addr >= 0xF7800000 && addr < 0xF8000000) {
        int entry = (addr >> 8) & 63;
        return cpu->utlb_data2[entry];
    }

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

uint32_t g_write_seq = 0;

void sh4_write32(SH4CPU *cpu, uint32_t addr, uint32_t val) {
    g_write_seq++;

    /* Handle ALL P4 area (0xE0000000+) BEFORE address translation.
     * P4 addresses are on-chip resources, NOT physical memory. */
    if (addr >= 0xE0000000) {
        /* Store Queue writes (0xE0000000-0xE3FFFFFF) */
        if (addr <= 0xE3FFFFFF) {
            int sq_idx = (addr >> 5) & 1;
            int word_idx = (addr >> 2) & 7;
            cpu->sq[sq_idx][word_idx] = val;
            return;
        }

        /* UTLB Address Array writes: 0xF6000000-0xF6FFFFFF */
        if (addr >= 0xF6000000 && addr < 0xF7000000) {
            int entry = (addr >> 8) & 63;
            cpu->utlb_addr[entry] = val;
            static int utlb_log = 0;
            if (utlb_log < 20) {
                utlb_log++;
                printf("[UTLB] addr[%d] = 0x%08X (VPN=0x%08X V=%d)\n",
                       entry, val, val & 0xFFFFFC00, (val >> 8) & 1);
            }
            return;
        }
        /* UTLB Data Array 1 writes: 0xF7000000-0xF77FFFFF */
        if (addr >= 0xF7000000 && addr < 0xF7800000) {
            int entry = (addr >> 8) & 63;
            cpu->utlb_data1[entry] = val;
            static int utlb_d1_log = 0;
            if (utlb_d1_log < 20) {
                utlb_d1_log++;
                int sz = ((val >> 6) & 2) | ((val >> 4) & 1);
                static const char *sz_names[] = {"1KB", "4KB", "64KB", "1MB"};
                printf("[UTLB] data1[%d] = 0x%08X (PPN=0x%08X sz=%s)\n",
                       entry, val, val & 0x1FFFFC00, sz_names[sz]);
            }
            return;
        }
        /* UTLB Data Array 2 writes: 0xF7800000-0xF7FFFFFF */
        if (addr >= 0xF7800000 && addr < 0xF8000000) {
            int entry = (addr >> 8) & 63;
            cpu->utlb_data2[entry] = val;
            return;
        }

        /* P4 control registers (0xFF000000+) */
        if (addr >= 0xFF000000) {
            switch (addr) {
            case 0xFF000010: {
                int old_at = cpu->mmucr & 1;
                int new_at = val & 1;
                cpu->mmucr = val;
                if (val & 4) {
                    for (int i = 0; i < 64; i++)
                        cpu->utlb_addr[i] &= ~0x100;
                    cpu->mmucr &= ~4;
                    printf("[MMU] TLB invalidated\n");
                }
                if (old_at != new_at) {
                    printf("[MMU] Address translation %s (MMUCR=0x%08X seq=%u)\n",
                           new_at ? "ENABLED" : "DISABLED", cpu->mmucr, g_write_seq);
                }
                return;
            }
            case 0xFF000038:
                cpu->qacr[0] = val;
                return;
            case 0xFF00003C:
                cpu->qacr[1] = val;
                return;
            }
            if (addr >= 0xFFA00000 && addr <= 0xFFA00040) {
                uint32_t idx = (addr - 0xFFA00000) / 4;
                if (idx < 17) cpu->dmac_regs[idx] = val;
                return;
            }
            if (addr >= 0xFFD80000 && addr <= 0xFFD8002F) {
                uint32_t idx = (addr - 0xFFD80000) / 4;
                if (idx < 12) cpu->tmu_regs[idx] = val;
                return;
            }
            return;
        }

        /* Other P4 ranges (cache arrays at 0xF0-0xF5, etc.) - ignore */
        return;
    }

    /* Normal address translation for non-P4 addresses */
    uint32_t phys = translate_addr(addr);

    /* Trace TA-range writes */
    if (phys >= 0x10000000 && phys < 0x10800000) {
        static int ta_trace = 0;
        ta_trace++;
        if (ta_trace <= 10) {
            printf("[TA-TRACE] seq=%u virt=0x%08X phys=0x%08X val=0x%08X pr=0x%08X\n",
                   g_write_seq, addr, phys, val, cpu->pr);
        }
    }

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

    if (is_hw_register(phys) && g_hardware) {
        dc_hw_write32(g_hardware, phys, val);
    }
}

void sh4_write_float(SH4CPU *cpu, uint32_t addr, float val) {
    union { float f; uint32_t u; } conv;
    conv.f = val;
    sh4_write32(cpu, addr, conv.u);
}

/* ========== Store Queue Prefetch ========== */

static int sq_log_count = 0;

void sh4_sq_prefetch(SH4CPU *cpu, uint32_t addr) {
    /* Only process Store Queue addresses */
    if (addr < 0xE0000000 || addr > 0xE3FFFFFF) return;

    if (sq_log_count < 5) {
        sq_log_count++;
        printf("[SQ] prefetch addr=0x%08X qacr0=0x%X qacr1=0x%X\n",
               addr, cpu->qacr[0], cpu->qacr[1]);
    }

    int sq_idx = (addr >> 5) & 1;
    uint32_t qacr = cpu->qacr[sq_idx];

    /* Compute destination: QACR bits [4:2] → address bits [28:26] */
    uint32_t dest = (((qacr >> 2) & 7) << 26) | (addr & 0x03FFFFE0);

    if (dest >= 0x10000000 && dest < 0x10800000) {
        /* TA FIFO — send 32 bytes to the Tile Accelerator */
        static int sq_ta_count = 0;
        sq_ta_count++;
        if (sq_ta_count <= 5) {
            printf("[TA-SQ] SQ%d → TA packet #%d (dest=0x%08X)\n",
                   sq_idx, sq_ta_count, dest);
        }
        pvr2_ta_write(cpu->sq[sq_idx]);
    } else if (dest >= DC_VRAM_BASE && dest < DC_VRAM_BASE + DC_VRAM_SIZE) {
        /* VRAM DMA (texture uploads etc.) */
        uint32_t offset = dest - DC_VRAM_BASE;
        if (offset + 32 <= DC_VRAM_SIZE) {
            memcpy(cpu->vram + offset, cpu->sq[sq_idx], 32);
        }
    } else {
        /* Other destinations — generic word-by-word write */
        for (int i = 0; i < 8; i++) {
            sh4_write32(cpu, dest + i * 4, cpu->sq[sq_idx][i]);
        }
    }
}
