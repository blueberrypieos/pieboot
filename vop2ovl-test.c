/* SPDX-License-Identifier: BSD-2-Clause */
/* vop2_overlay() and vop2_overlay_vp() on the build host (make test): each case gives the selection it must make. */
#include <stdio.h>
#include <stdlib.h>

#include "vop2ovl.h"

#define C0      (1u << VOP2_CLUSTER0)
#define C1      (1u << VOP2_CLUSTER1)
#define E0      (1u << VOP2_ESMART0)
#define S0      (1u << VOP2_SMART0)
#define E1      (1u << VOP2_ESMART1)
#define S1      (1u << VOP2_SMART1)

static int failed;

static unsigned
layer(uint32_t layer_sel, unsigned i)
{
	return (layer_sel >> (4 * i)) & 7;
}

/*
 * What every answer must satisfy: each window once, win among video port
 * vp's layers, the others there to be turned off, and every window not
 * turned off on the port it had.
 */
static bool
sound(uint32_t old_ports, unsigned vp, unsigned win, const struct vop2_ovl *o)
{
	uint32_t seen = 0;
	for (unsigned i = 0; i < 6; i++) {
		seen |= 1u << layer(o->layer_sel, i);
	}
	if (seen != (C0 | C1 | E0 | S0 | E1 | S1) || vop2_win_port(o->port_sel, win) != vp || (o->off & (1u << win))) {
		return false;
	}
	unsigned mux0 = o->port_sel & 0xf, mux1 = (o->port_sel >> 4) & 0xf, n0 = mux0 == 8 ? 0 : mux0 + 1;
	unsigned first = vp == 0 ? 0 : n0, end = vp == 0 ? n0 : mux1 + 1;
	bool found = false;
	for (unsigned i = first; i < end && i < 6; i++) {
		unsigned w = layer(o->layer_sel, i);
		found = found || w == win;
		if (w != win && (vop2_win_port(o->port_sel, w) != vp || !(o->off & (1u << w)))) {
			return false;
		}
	}
	for (unsigned w = 0; w < 8; w++) {
		if ((seen & (1u << w)) && w != win && !(o->off & (1u << w)) &&
		    vop2_win_port(o->port_sel, w) != vop2_win_port(old_ports, w)) {
			return false;
		}
	}
	return found;
}

static void
check_vp(const char *name, uint32_t layer_sel, uint32_t port_sel, unsigned vp, unsigned win, bool want_ok,
    uint32_t want_layers, uint32_t want_ports, uint32_t want_off)
{
	struct vop2_ovl o = { 0, 0, 0 };
	bool ok = vp == 0 ? vop2_overlay(layer_sel, port_sel, win, &o) : vop2_overlay_vp(layer_sel, port_sel, vp, win, &o);
	bool pass = ok == want_ok;
	if (ok && want_ok) {
		pass = pass && o.layer_sel == want_layers && o.port_sel == want_ports && o.off == want_off &&
		    sound(port_sel, vp, win, &o);
	}
	printf("%s: %s: layers 0x%08x ports 0x%08x off 0x%02x\n", pass ? "ok" : "FAILED", name,
	    (unsigned)o.layer_sel, (unsigned)o.port_sel, (unsigned)o.off);
	if (!pass) {
		printf("  want %s, layers 0x%08x ports 0x%08x off 0x%02x\n", want_ok ? "true" : "false",
		    (unsigned)want_layers, (unsigned)want_ports, (unsigned)want_off);
		failed = 1;
	}
}

static void
check(const char *name, uint32_t layer_sel, uint32_t port_sel, unsigned win, bool want_ok,
    uint32_t want_layers, uint32_t want_ports, uint32_t want_off)
{
	check_vp(name, layer_sel, port_sel, 0, win, want_ok, want_layers, want_ports, want_off);
}

int
main(void)
{
	check("reset values, esmart0", 0x00763210, 0x84000743, VOP2_ESMART0, true,
	    0x00763210, 0x84000743, C0 | C1 | S0);
	check("reset values, smart0", 0x00763210, 0x84000743, VOP2_SMART0, true,
	    0x00763210, 0x84000743, C0 | C1 | E0);
	check("reset values, esmart1 of port 1", 0x00763210, 0x84000743, VOP2_ESMART1, true,
	    0x00321706, 0xa2090510, C0 | C1 | E0 | S0);
	check("reset values, smart1 of port 2", 0x00763210, 0x84000743, VOP2_SMART1, true,
	    0x00321067, 0x260a0510, C0 | C1 | E0 | S0);
	check("port 0 empty, port 1 cluster0 esmart0 smart0, port 2 the rest", 0x00761320, 0x99090528,
	    VOP2_ESMART0, true, 0x00761302, 0x98090520, 0);
	check("port 0 empty, all six on port 1", 0x00763210, 0x55050558, VOP2_ESMART0, true,
	    0x00763102, 0x54050550, 0);
	check("port 0 empty, all six on port 1, smart0", 0x00763210, 0x55050558, VOP2_SMART0, true,
	    0x00762103, 0x45050550, 0);
	check("port 0 has all six", 0x00763210, 0x00000005, VOP2_ESMART0, true,
	    0x00763210, 0x00000005, C0 | C1 | S0 | E1 | S1);
	check("win in port 0's layers but on port 1", 0x00763210, 0x85000743, VOP2_ESMART0, true,
	    0x00310762, 0xa40a0510, C0 | C1 | S0);
	check("no window for port 1: a spare of port 0", 0x00763210, 0x8a080003, VOP2_ESMART0, true,
	    0x00376102, 0xa8090510, C0 | S0);
	check("all but win on port 2", 0x00763210, 0xaa0a0003, VOP2_ESMART0, false, 0, 0, 0);
	check("duplicates and reserved numbers in LAYER_SEL", 0x00444422, 0x84000743, VOP2_ESMART0, true,
	    0x00310762, 0xa40a0510, C0 | C1 | S0);
	check("reserved bits 31:24 set, as an RK3566 reads them", 0x54763210, 0x84000743, VOP2_ESMART0, true,
	    0x54310762, 0xa40a0510, C0 | C1 | S0);
	check("not a window", 0x00763210, 0x84000743, 4, false, 0, 0, 0);

	check_vp("port 1: reset values, smart0", 0x00763210, 0x84000743, 1, VOP2_SMART0, true,
	    0x00673210, 0x98000532, E1);
	check_vp("port 1: smart0 and esmart1 there already", 0x00763210, 0x94000742, 1, VOP2_SMART0, true,
	    0x00763210, 0x94000742, E1);
	check_vp("port 1: the same with reserved bits 31:24 set, as the stock firmware has them", 0x54763210,
	    0x94000742, 1, VOP2_SMART0, true, 0x54673210, 0x98000532, E1);
	check_vp("port 1: port 0 empty, all six on port 1", 0x00763210, 0x55050558, 1, VOP2_SMART0, true,
	    0x00763210, 0x55050558, C0 | C1 | E0 | E1 | S1);
	check_vp("port 1: port 0 empty, a spare of port 1 stays on port 0", 0x00763210, 0xa9050528, 1, VOP2_SMART0,
	    true, 0x00217630, 0x9a080510, C0 | C1 | E0);
	check_vp("port 1: port 0 keeps esmart0 alone", 0x00376102, 0xa8090510, 1, VOP2_SMART0, true,
	    0x00076132, 0x980a0510, C0);
	check_vp("port 1: all on port 2", 0x00763210, 0xaa0a0003, 1, VOP2_SMART0, false, 0, 0, 0);
	check_vp("port 2 is not offered", 0x00763210, 0x84000743, 2, VOP2_SMART0, false, 0, 0, 0);
	check_vp("port 1: not a window", 0x00763210, 0x84000743, 1, 5, false, 0, 0, 0);

	printf("vop2ovl-test: %s\n", failed ? "FAIL" : "PASS");
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
