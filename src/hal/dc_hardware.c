/**
 * Dreamcast Hardware Abstraction Layer Implementation
 *
 * Maps Dreamcast hardware registers and subsystems to modern equivalents.
 * GPU rendering is handled via OpenGL, input via SDL2, sound via SDL2 audio.
 */

#include "hal/dc_hardware.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Internal hardware state */
struct DCHardware {
    /* Hardware registers (mapped 0x005F6800 - 0x005FA000) */
    uint32_t hw_regs[0x3800 / 4];

    /* System Board state */
    uint32_t sb_istnrm;    /* Normal interrupt status */
    uint32_t sb_istext;    /* External interrupt status */
    uint32_t sb_isterr;    /* Error interrupt status */

    /* PVR state */
    uint32_t pvr_fb_addr1;
    uint32_t pvr_fb_addr2;
    uint32_t pvr_render_addr;
    bool pvr_rendering;
    int frame_count;

    /* Maple (controller) state */
    MapleController controllers[4];
    bool controller_connected[4];

    /* AICA state */
    bool aica_arm_running;

    /* GD-ROM state */
    int gdrom_status;

    /* TA (Tile Accelerator) FIFO buffer */
    uint32_t ta_fifo[32];
    int ta_fifo_pos;

    /* Timing */
    uint64_t vblank_count;
};

/* Register offset calculation */
static inline uint32_t hw_reg_idx(uint32_t addr) {
    if (addr >= 0x005F6800 && addr < 0x005FA000)
        return (addr - 0x005F6800) / 4;
    return 0;
}

DCHardware* dc_hw_init(void) {
    DCHardware *hw = (DCHardware *)calloc(1, sizeof(DCHardware));
    if (!hw) return NULL;

    /* Set PVR ID (Holly chip) */
    hw->hw_regs[hw_reg_idx(PVR_ID)] = 0x17FD11DB;

    /* Default framebuffer setup (640x480) */
    hw->pvr_fb_addr1 = 0x00000000;
    hw->pvr_fb_addr2 = 0x00000000;

    /* Connect controller in port 0 */
    hw->controller_connected[0] = true;
    hw->controllers[0].buttons = 0xFFFF; /* All buttons released (active low) */
    hw->controllers[0].ltrig = 0;
    hw->controllers[0].rtrig = 0;
    hw->controllers[0].joyx = 0;
    hw->controllers[0].joyy = 0;

    printf("[HAL] Dreamcast hardware initialized\n");
    return hw;
}

void dc_hw_destroy(DCHardware *hw) {
    if (hw) {
        printf("[HAL] Hardware destroyed (frames rendered: %d)\n", hw->frame_count);
        free(hw);
    }
}

uint32_t dc_hw_read32(DCHardware *hw, uint32_t addr) {
    if (!hw) return 0;

    /* Strip P2 area bits if present */
    uint32_t phys = addr & 0x1FFFFFFF;

    switch (phys) {
    case PVR_ID:
        return 0x17FD11DB;

    case PVR_REVISION:
        return 0x00000011;  /* Revision 1.1 */

    case SB_ISTNRM:
        return hw->sb_istnrm;

    case SB_ISTEXT:
        return hw->sb_istext;

    case SB_ISTERR:
        return hw->sb_isterr;

    case PVR_FB_ADDR1:
        return hw->pvr_fb_addr1;

    case PVR_FB_ADDR2:
        return hw->pvr_fb_addr2;

    default:
        if (phys >= 0x005F6800 && phys < 0x005FA000) {
            return hw->hw_regs[hw_reg_idx(phys)];
        }
        break;
    }

    return 0;
}

void dc_hw_write32(DCHardware *hw, uint32_t addr, uint32_t val) {
    if (!hw) return;

    uint32_t phys = addr & 0x1FFFFFFF;

    switch (phys) {
    case PVR_SOFTRESET:
        if (val & 1) {
            printf("[PVR] Soft reset\n");
        }
        break;

    case PVR_STARTRENDER:
        hw->pvr_rendering = true;
        hw->frame_count++;
        /* Signal render complete via interrupt */
        hw->sb_istnrm |= (1 << 2); /* Render complete */
        break;

    case PVR_FB_ADDR1:
        hw->pvr_fb_addr1 = val;
        break;

    case PVR_FB_ADDR2:
        hw->pvr_fb_addr2 = val;
        break;

    case PVR_FB_RENDER:
        hw->pvr_render_addr = val;
        break;

    case SB_ISTNRM:
        /* Writing 1 bits clears them */
        hw->sb_istnrm &= ~val;
        break;

    case SB_ISTEXT:
        hw->sb_istext &= ~val;
        break;

    case SB_ISTERR:
        hw->sb_isterr &= ~val;
        break;

    case AICA_ARM_RESET:
        hw->aica_arm_running = !(val & 1);
        printf("[AICA] ARM %s\n", hw->aica_arm_running ? "started" : "reset");
        break;

    case TA_LIST_INIT:
        hw->ta_fifo_pos = 0;
        break;

    case MAPLE_DMA_START:
        if (val & 1) {
            /* Maple DMA transfer - handle controller polling */
            dc_maple_poll(hw);
        }
        break;

    default:
        if (phys >= 0x005F6800 && phys < 0x005FA000) {
            hw->hw_regs[hw_reg_idx(phys)] = val;
        }
        /* TA FIFO writes (0x10000000 - 0x107FFFFF) */
        if (phys >= 0x10000000 && phys < 0x10800000) {
            hw->ta_fifo[hw->ta_fifo_pos & 31] = val;
            hw->ta_fifo_pos++;
        }
        break;
    }
}

/* ========== PVR GPU ========== */

void dc_pvr_init(DCHardware *hw) {
    printf("[PVR] PowerVR2 initialized\n");
    hw->pvr_rendering = false;
    hw->frame_count = 0;
}

void dc_pvr_start_render(DCHardware *hw) {
    hw->pvr_rendering = true;
    hw->frame_count++;
}

void dc_pvr_submit_vertex(DCHardware *hw, const PVRVertex *vtx) {
    (void)hw;
    (void)vtx;
    /* TODO: Buffer vertex for rendering via OpenGL */
}

void dc_pvr_begin_list(DCHardware *hw, PVRListType type) {
    (void)hw;
    (void)type;
    /* TODO: Begin polygon list submission */
}

void dc_pvr_end_list(DCHardware *hw) {
    (void)hw;
    /* TODO: End polygon list */
}

void dc_pvr_wait_vblank(DCHardware *hw) {
    hw->vblank_count++;
    /* Set VBLANK interrupt */
    hw->sb_istnrm |= (1 << 3); /* VBlank-IN */
}

/* ========== Maple (Controllers) ========== */

void dc_maple_init(DCHardware *hw) {
    printf("[MAPLE] Controller bus initialized\n");
    hw->controller_connected[0] = true;
}

void dc_maple_poll(DCHardware *hw) {
    /* TODO: Read from SDL2 game controller */
    /* For now, controllers maintain their current state */
    (void)hw;
}

MapleController* dc_maple_get_controller(DCHardware *hw, int port) {
    if (port < 0 || port >= 4) return NULL;
    if (!hw->controller_connected[port]) return NULL;
    return &hw->controllers[port];
}

/* ========== AICA Sound ========== */

void dc_aica_init(DCHardware *hw) {
    printf("[AICA] Sound processor initialized\n");
    hw->aica_arm_running = false;
}

void dc_aica_write_channel(DCHardware *hw, int ch, uint32_t offset, uint32_t val) {
    (void)hw;
    (void)ch;
    (void)offset;
    (void)val;
    /* TODO: Implement AICA channel control */
}

void dc_aica_update(DCHardware *hw) {
    (void)hw;
    /* TODO: Mix audio channels and output via SDL2 */
}

/* ========== GD-ROM ========== */

void dc_gdrom_init(DCHardware *hw) {
    printf("[GDROM] Drive initialized\n");
    hw->gdrom_status = 0;
}

int dc_gdrom_read_sectors(DCHardware *hw, uint32_t lba, uint32_t count, void *buf) {
    (void)hw;
    (void)lba;
    (void)count;
    (void)buf;
    /* TODO: Read from extracted disc files */
    return 0;
}
