/* SPDX-License-Identifier: BSD-2-Clause */
#include "vop2ovl.h"

#define LAYERS                  6
#define PORT_SEL_SEL_MASK       0xff0f0000u
#define PORT_SEL_MUX_MASK       0x00000fffu
#define PORT0_NONE              8

static const uint8_t windows[LAYERS] = {
	VOP2_CLUSTER0, VOP2_CLUSTER1, VOP2_ESMART0, VOP2_SMART0, VOP2_ESMART1, VOP2_SMART1,
};

static bool
is_window(unsigned win)
{
	return win <= VOP2_SMART0 || win == VOP2_ESMART1 || win == VOP2_SMART1;
}

static unsigned
port_shift(unsigned win)
{
	switch (win) {
	case VOP2_CLUSTER0:
		return 16;
	case VOP2_CLUSTER1:
		return 18;
	case VOP2_ESMART0:
		return 24;
	case VOP2_ESMART1:
		return 26;
	case VOP2_SMART0:
		return 28;
	default:
		return 30;
	}
}

unsigned
vop2_win_port(uint32_t port_sel, unsigned win)
{
	return (port_sel >> port_shift(win)) & 3;
}

bool
vop2_overlay(uint32_t layer_sel, uint32_t port_sel, unsigned win, struct vop2_ovl *out)
{
	if (!is_window(win)) {
		return false;
	}
	uint8_t order[LAYERS];
	unsigned n = 0;
	uint32_t seen = 0;
	for (unsigned i = 0; i < LAYERS; i++) {
		unsigned w = (layer_sel >> (4 * i)) & 7;
		if (is_window(w) && !(seen & (1u << w))) {
			order[n++] = (uint8_t)w;
			seen |= 1u << w;
		}
	}
	bool distinct = n == LAYERS && !(layer_sel & ~0x00777777u);
	for (unsigned i = 0; i < LAYERS; i++) {
		if (!(seen & (1u << windows[i]))) {
			order[n++] = windows[i];
			seen |= 1u << windows[i];
		}
	}

	unsigned mux0 = port_sel & 0xf;
	unsigned n0 = mux0 == PORT0_NONE ? 0 : mux0 + 1;
	if (distinct && n0 >= 1 && n0 <= LAYERS) {
		bool mine = false, consistent = true;
		uint32_t off = 0;
		for (unsigned i = 0; i < n0; i++) {
			consistent = consistent && vop2_win_port(port_sel, order[i]) == 0;
			if (order[i] == win) {
				mine = true;
			} else {
				off |= 1u << order[i];
			}
		}
		if (mine && consistent) {
			out->layer_sel = layer_sel;
			out->port_sel = port_sel;
			out->off = off;
			return true;
		}
	}

	uint8_t vp1[LAYERS], rest[LAYERS], spare[LAYERS];
	unsigned n1 = 0, n2 = 0, ns = 0;
	uint32_t off = 0;
	for (unsigned i = 0; i < LAYERS; i++) {
		unsigned w = order[i], port = vop2_win_port(port_sel, w);
		if (w == win) {
			continue;
		}
		if (port == 1) {
			vp1[n1++] = (uint8_t)w;
		} else if (port == 2) {
			rest[n2++] = (uint8_t)w;
		} else {
			spare[ns++] = (uint8_t)w;
			off |= 1u << w;
		}
	}
	unsigned first_spare = 0;
	if (n1 == 0) {
		if (ns == 0) {
			return false;
		}
		vp1[n1++] = spare[first_spare++];
	}
	for (unsigned i = first_spare; i < ns; i++) {
		rest[n2++] = spare[i];
	}

	uint32_t layers = win, sel = 0;
	unsigned slot = 1;
	for (unsigned i = 0; i < n1; i++, slot++) {
		layers |= (uint32_t)vp1[i] << (4 * slot);
		sel |= 1u << port_shift(vp1[i]);
	}
	for (unsigned i = 0; i < n2; i++, slot++) {
		layers |= (uint32_t)rest[i] << (4 * slot);
		sel |= 2u << port_shift(rest[i]);
	}
	out->layer_sel = layers | (layer_sel & ~0x00777777u);
	out->port_sel = (port_sel & ~(PORT_SEL_SEL_MASK | PORT_SEL_MUX_MASK)) | sel |
	    ((LAYERS - 1) << 8) | (n1 << 4);
	out->off = off;
	return true;
}
