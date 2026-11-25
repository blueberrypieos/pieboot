/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * The RK3568 VOP2 overlay's layer and port selection with one window as
 * video port 0's only layer (vop2ovl.c). Plain arithmetic, no hardware:
 * vop2ovl-test.c runs it on the build host (make test).
 */
#ifndef PIEBOOT_VOP2OVL_H
#define PIEBOOT_VOP2OVL_H

#include <stdbool.h>
#include <stdint.h>

/* Window numbers, as LAYER_SEL names them. */
#define VOP2_CLUSTER0           0
#define VOP2_CLUSTER1           1
#define VOP2_ESMART0            2
#define VOP2_SMART0             3
#define VOP2_ESMART1            6
#define VOP2_SMART1             7

struct vop2_ovl {
	uint32_t layer_sel;     /* LAYER_SEL */
	uint32_t port_sel;      /* PORT_SEL */
	uint32_t off;           /* windows to turn off, each as 1 << its number */
};

/* The video port that PORT_SEL gives window win: 0 to 2, or 3, which is reserved. */
unsigned vop2_win_port(uint32_t port_sel, unsigned win);

/*
 * The overlay for window win on video port 0, from the LAYER_SEL and
 * PORT_SEL in use. If video port 0 already has win among its layers, the
 * selection stays, and the other windows of video port 0 are to be turned
 * off. Otherwise win becomes the one layer of video port 0, at the bottom.
 * The other windows keep their video port and their order. The windows of
 * video port 0, or of the reserved port, move to video port 2 and are to
 * be turned off. False if win is not a window, or if video port 1 would be
 * left without a layer.
 */
bool vop2_overlay(uint32_t layer_sel, uint32_t port_sel, unsigned win, struct vop2_ovl *out);

#endif
