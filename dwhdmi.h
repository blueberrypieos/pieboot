/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * A DesignWare HDMI TX controller with its HDMI 2.0 TX PHY, as on the
 * RK3568, at one fixed mode (dwhdmi.c). base is the controller's register
 * space, its 8-bit registers 4 bytes apart.
 */
#ifndef PIEBOOT_DWHDMI_H
#define PIEBOOT_DWHDMI_H

#include "lib.h"

struct dwhdmi_mode {
	uint32_t clock_khz;
	uint16_t hdisplay, hsync_start, hsync_end, htotal;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal;
	bool hsync_high, vsync_high;
	bool hdmi;                      /* HDMI with an AVI InfoFrame; else DVI */
	uint8_t vic;                    /* CEA-861 video code, for the InfoFrame */
	uint8_t aspect;                 /* the InfoFrame's picture aspect: 1 4:3, 2 16:9 */
};

#define DWHDMI_PHY_HDMI20       0xf3    /* dwhdmi_phy_type() of the HDMI 2.0 TX PHY */

/* The controller's design and revision, 0xDDRR. */
uint32_t dwhdmi_version(uint64_t base);
/* The PHY type the controller reports. */
uint8_t dwhdmi_phy_type(uint64_t base);
/* The PHY's status: HPD in bit 1, PLL lock in bit 0, RX sense of the four TMDS channels in bits 7:4. */
uint8_t dwhdmi_phy_status(uint64_t base);
/* The controller's 8-bit register reg, for diagnostics, such as these. */
uint8_t dwhdmi_peek(uint64_t base, uint32_t reg);
#define DWHDMI_IH_FC_STAT0      0x0100  /* the frame composer's interrupts, then STAT1 and STAT2 */
#define DWHDMI_IH_PHY_STAT0     0x0104
#define DWHDMI_VP_STATUS        0x0800
#define DWHDMI_FC_INVIDCONF     0x1000
#define DWHDMI_PHY_CONF0        0x3000
#define DWHDMI_MC_LOCKONCLOCK   0x4006
/* Turn the PHY's HPD sensing on; PHY_CONF0 as it was, for dwhdmi_hpd_sense_restore(). */
uint8_t dwhdmi_hpd_sense(uint64_t base);
void dwhdmi_hpd_sense_restore(uint64_t base, uint8_t conf0);
/* Whether a sink holds HPD high, with sensing on. */
bool dwhdmi_hpd(uint64_t base);
/*
 * Set the PHY and the controller up for mode, the pixel clock already
 * running. False if the PHY's PLL does not lock or its I2C does not answer.
 * step, if not NULL, gets the name of each step before it starts.
 */
bool dwhdmi_enable(uint64_t base, const struct dwhdmi_mode *mode, void (*step)(const char *what));
/* Power the PHY down. */
void dwhdmi_disable(uint64_t base);

#endif
