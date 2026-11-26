/* SPDX-License-Identifier: BSD-2-Clause */
/* The built-in DSI panel of RK3566 and RK3568 boards, for a boot loader that sets no display up (rkdsi.c). */
#ifndef PIEBOOT_RKDSI_H
#define PIEBOOT_RKDSI_H

#include "fdt.h"
#include "place.h"

/*
 * Whether the device tree has an RK3566's or RK3568's VOP2, CRU, PMUCRU, PMU,
 * GRF and PMU GRF, and an enabled DSI host on video port 1 with a panel.
 */
bool rk_panel_board(const struct fdt *fdt);

/*
 * Bring the panel up at its mode nearest 60 Hz. By default, scan out a
 * cleared 32 bpp framebuffer that ends at ram.end and starts above
 * ram.base. Add a simple-framebuffer node under /chosen.
 * With opts="off" or no usable panel data, skip bring-up and leave the
 * hardware as found. A panel the boot loader left running stays as it was.
 * With opts="dsibars", show the DSI host's colour bars without a framebuffer.
 * With opts="pattern", fill the test image and keep it without a node.
 * "pattern" is ignored with "dsibars". The init list, mode and link come
 * from the pie,panel-* nodes under the panel's node. The node is used
 * when the panel's reply to DCS 0x04 begins with the node's pie,id.
 * pie,bus-flags is checked but not applied.
 * The framebuffer lies clear of the n busy ranges. opts is a comma-separated
 * list from pieboot.panel=, or NULL.
 * With verbose, each step is printed before it starts. *base and *size say
 * where the framebuffer is. False on other boards, having changed nothing;
 * false also for opts="off" or no usable panel data, as above. A bring-up
 * failure returns false with a message. Cleanup requests a dark panel and
 * restores the clocks, resets and power domain.
 */
bool rk_panel(struct fdt *fdt, const char *opts, bool verbose, struct place_range ram, const struct place_range *busy, unsigned n,
    uint64_t *base, uint64_t *size);

/*
 * Darken the panel while another output has the console: backlight off,
 * video port 1 in standby, panel in reset and off. Leave hardware as found
 * with off in opts or no usable panel data. Restore GPIO bank clock gates.
 */
void rk_panel_off(const struct fdt *fdt, const char *opts, bool verbose);

#endif
