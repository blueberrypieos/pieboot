/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * A DSI panel as the device tree's pie,panel-* nodes describe it, and the
 * arithmetic of its link and its pixel clock (dsipanel.c). No hardware:
 * dsipanel-test.c runs it on the build host (make test).
 */
#ifndef PIEBOOT_DSIPANEL_H
#define PIEBOOT_DSIPANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct dsi_mode {
	uint32_t clock_khz;
	uint16_t hdisplay, hsync_start, hsync_end, htotal;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal;
	bool hsync_low, vsync_low;
};

struct dsi_panel {
	uint8_t id[3];
	unsigned id_len;
	struct dsi_mode mode;           /* the panel's mode nearest 60 Hz */
	unsigned lanes;
	bool burst, sync_pulses, no_eot, lp_commands;
	const uint8_t *init;            /* packets: data type, payload length, payload */
	size_t init_len;
};

/* A property's value as the FDT holds it; val is NULL when the property is absent. */
struct dsi_prop {
	const uint8_t *val;
	uint32_t len;
};

/* The properties of one pie,panel-* node, by name without the pie, prefix. */
struct dsi_props {
	struct dsi_prop id, modes, mode_flags, dsi_lanes, dsi_format, dsi_flags, bus_flags, init;
};

/*
 * The panel props describes, in *out, which points into props' values.
 * False, with *why set, when a property is missing or malformed, names
 * something unknown, or the init list holds anything but DCS and generic
 * writes.
 */
bool dsi_panel_parse(const struct dsi_props *props, struct dsi_panel *out, const char **why);

/* Whether the n bytes the panel returned for DCS 0x04 begin with the panel's ID. */
bool dsi_panel_matches(const struct dsi_panel *panel, const uint8_t *id, size_t n);

struct dsi_packet {
	uint8_t type;
	uint8_t len;
	const uint8_t *data;
};

/* The packet at *off of a parsed panel's init list, with *off moved past it; false at the end. */
bool dsi_packet_next(const struct dsi_panel *panel, size_t *off, struct dsi_packet *pkt);

void dsi_panel_pattern(const struct dsi_mode *mode, uint32_t *pixels);

/* The DSI host's video timing: horizontal in lane byte clock cycles, vertical in lines. */
struct dsi_timing {
	uint32_t hsa, hbp, hline;
	uint32_t vsa, vbp, vfp, vactive;
};

/*
 * The timing of mode with lane_kbps on each of lanes lanes. False if the link
 * is too slow for the mode's pixels or a value does not fit its register.
 */
bool dsi_timing(const struct dsi_mode *mode, unsigned lanes, uint32_t lane_kbps, struct dsi_timing *t);

/* A pixel clock from the 24 MHz oscillator: 24 MHz / refdiv * fbdiv / postdiv1 / postdiv2 / div. */
struct rk_pll {
	uint8_t refdiv, postdiv1, postdiv2;
	uint16_t fbdiv, div;
};

/*
 * Settings of an RK3568 PLL in integer mode, and of the divider after it,
 * that give exactly khz: the VCO from 950 MHz to 3.8 GHz, the reference
 * after refdiv at least 1 MHz, the PLL's output at most 600 MHz. False if
 * none does.
 */
bool rk_pll_exact(uint32_t khz, struct rk_pll *out);

#endif
