/**
 * SH-4 CPU State Structure
 *
 * Represents the complete state of the Hitachi SH-4 CPU as used in the
 * Sega Dreamcast. All recompiled code operates on this structure.
 *
 * Memory map (Dreamcast):
 *   0x00000000 - 0x001FFFFF : Boot ROM (2MB)
 *   0x00200000 - 0x0021FFFF : Flash ROM (128KB)
 *   0x00800000 - 0x009FFFFF : AICA Sound (2MB)
 *   0x04000000 - 0x047FFFFF : VRAM (8MB, 64-bit access)
 *   0x05000000 - 0x057FFFFF : VRAM (8MB, 32-bit access)
 *   0x0C000000 - 0x0CFFFFFF : System RAM (16MB)
 *   0x10000000 - 0x107FFFFF : Tile Accelerator
 *   0x10800000 - 0x10FFFFFF : Hardware Registers
 *   0x8C000000 - 0x8CFFFFFF : System RAM (cached, P1 area)
 *   0xAC000000 - 0xACFFFFFF : System RAM (uncached, P2 area)
 */

#ifndef SH4_CPU_H
#define SH4_CPU_H

#include <stdint.h>
#include <stdbool.h>

/* SH-4 Status Register bits */
#define SR_T    (1 << 0)   /* True/False bit */
#define SR_S    (1 << 1)   /* Saturating operation */
#define SR_IMASK 0x000000F0 /* Interrupt mask */
#define SR_Q    (1 << 8)   /* Quotient bit */
#define SR_M    (1 << 9)   /* M bit for division */
#define SR_FD   (1 << 15)  /* FPU disable */
#define SR_BL   (1 << 28)  /* Block exceptions */
#define SR_RB   (1 << 29)  /* Register bank */
#define SR_MD   (1 << 30)  /* Processor mode */

/* FPSCR bits */
#define FPSCR_RM    0x00000003  /* Rounding mode */
#define FPSCR_DN    (1 << 18)   /* Denormalization mode */
#define FPSCR_PR    (1 << 19)   /* Precision mode (0=single, 1=double) */
#define FPSCR_SZ    (1 << 20)   /* Transfer size mode */
#define FPSCR_FR    (1 << 21)   /* Floating-point register bank */

/* Dreamcast memory regions */
#define DC_RAM_BASE     0x0C000000
#define DC_RAM_SIZE     0x01000000  /* 16 MB */
#define DC_RAM_MASK     0x00FFFFFF
#define DC_VRAM_BASE    0x04000000
#define DC_VRAM_SIZE    0x00800000  /* 8 MB */
#define DC_AICA_BASE    0x00800000
#define DC_AICA_SIZE    0x00200000  /* 2 MB */

/* P1/P2 mirror masks */
#define ADDR_MASK_P1    0x1FFFFFFF  /* Strip P1/P2/P3/P4 area bits */

/* 1ST_READ.BIN load address */
#define GAME_LOAD_ADDR  0x8C010000

typedef union {
    float f[2];
    double d;
    uint32_t u[2];
} FPRegPair;

typedef struct SH4CPU {
    /* General purpose registers (R0-R15) */
    uint32_t r[16];

    /* Banked registers (R0_BANK-R7_BANK) */
    uint32_t r_bank[8];

    /* Control registers */
    uint32_t sr;        /* Status Register */
    uint32_t gbr;       /* Global Base Register */
    uint32_t vbr;       /* Vector Base Register */
    uint32_t ssr;       /* Saved Status Register */
    uint32_t spc;       /* Saved Program Counter */
    uint32_t sgr;       /* Saved GBR */
    uint32_t dbr;       /* Debug Base Register */

    /* System registers */
    uint32_t mach;      /* Multiply-Accumulate High */
    uint32_t macl;      /* Multiply-Accumulate Low */
    uint32_t pr;        /* Procedure Register (return address) */
    uint32_t pc;        /* Program Counter */

    /* Floating point registers (FPR0-FPR15 in two banks) */
    float fr[16];       /* FPR bank 0 (or current bank) */
    float xf[16];       /* FPR bank 1 (or other bank) */
    uint32_t fpscr;     /* Floating-point Status/Control Register */
    uint32_t fpul;      /* Floating-point Communication Register */

    /* Memory */
    uint8_t *ram;       /* Main RAM (16 MB) */
    uint8_t *vram;      /* Video RAM (8 MB) */
    uint8_t *aica_ram;  /* AICA Sound RAM (2 MB) */

    /* Execution state */
    bool running;
    uint64_t cycles;
    uint32_t delay_slot; /* Non-zero if next instruction is in a delay slot */
} SH4CPU;

/* Initialize CPU state */
void sh4_init(SH4CPU *cpu);

/* Reset CPU */
void sh4_reset(SH4CPU *cpu);

/* Destroy CPU (free memory) */
void sh4_destroy(SH4CPU *cpu);

/* Memory access functions */
uint8_t  sh4_read8(SH4CPU *cpu, uint32_t addr);
uint16_t sh4_read16(SH4CPU *cpu, uint32_t addr);
uint32_t sh4_read32(SH4CPU *cpu, uint32_t addr);
float    sh4_read_float(SH4CPU *cpu, uint32_t addr);
void     sh4_write8(SH4CPU *cpu, uint32_t addr, uint8_t val);
void     sh4_write16(SH4CPU *cpu, uint32_t addr, uint16_t val);
void     sh4_write32(SH4CPU *cpu, uint32_t addr, uint32_t val);
void     sh4_write_float(SH4CPU *cpu, uint32_t addr, float val);

/* SR T-bit helpers */
static inline bool sh4_get_t(SH4CPU *cpu) { return (cpu->sr & SR_T) != 0; }
static inline void sh4_set_t(SH4CPU *cpu, bool v) {
    if (v) cpu->sr |= SR_T; else cpu->sr &= ~SR_T;
}

/* FPSCR helpers */
static inline bool sh4_get_sz(SH4CPU *cpu) { return (cpu->fpscr & FPSCR_SZ) != 0; }
static inline bool sh4_get_pr(SH4CPU *cpu) { return (cpu->fpscr & FPSCR_PR) != 0; }
static inline bool sh4_get_fr(SH4CPU *cpu) { return (cpu->fpscr & FPSCR_FR) != 0; }

#endif /* SH4_CPU_H */
