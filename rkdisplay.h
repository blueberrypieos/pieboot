/* SPDX-License-Identifier: BSD-2-Clause */
/* HDMI on RK3566 and RK3568 boards, for a boot loader that sets no display up (rkdisplay.c). */
#ifndef PIEBOOT_RKDISPLAY_H
#define PIEBOOT_RKDISPLAY_H

#include "fdt.h"
#include "place.h"

/* Whether the device tree has an RK3566's or RK3568's HDMI controller, VOP2, CRU, PMUCRU, PMU and GRF, all enabled. */
bool rk_hdmi_board(const struct fdt *fdt);

/*
 * With a sink on the HDMI port of an RK3566 or RK3568 board, bring HDMI up
 * at a fixed mode, scanning out a 32 bpp framebuffer that ends at end, and
 * add a simple-framebuffer node for it under /chosen. The framebuffer lies
 * clear of the n busy ranges and is cleared. opts is the value of
 * pieboot.hdmi=, or NULL. With verbose, each step is printed before it
 * starts, with the state of the clocks, resets and power domain it needs.
 * *base and *size say where the framebuffer is. False on other boards and
 * without a sink, having changed nothing. False also, with a message, when
 * HDMI does not come up.
 */
bool rk_hdmi(struct fdt *fdt, const char *opts, bool verbose, uint64_t end, const struct place_range *busy, unsigned n,
    uint64_t *base, uint64_t *size);

#endif
