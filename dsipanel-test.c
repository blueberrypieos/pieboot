/* SPDX-License-Identifier: BSD-2-Clause */
/* dsipanel.c on the build host (make test): each case gives what it must parse, refuse or compute. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsipanel.h"

#define PROP(a)         ((struct dsi_prop){ (const uint8_t *)(a), (uint32_t)sizeof(a) })

static int failed;

static void
report(const char *name, bool pass, const char *detail)
{
	printf("%s: %s%s%s\n", pass ? "ok" : "FAILED", name, detail ? ": " : "", detail ? detail : "");
	if (!pass) {
		failed = 1;
	}
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static const uint8_t id[] = { 0x12, 0x34 };
static uint8_t modes[2 * 36], lanes4[4], lanes0[4], lanes5[4];
static const char rgb888[] = "rgb888", rgb666[] = "rgb666", nonul[6] = { 'r', 'g', 'b', '8', '8', '8' };
static const char mode_flags[] = "hsync-low\0vsync-low";
static const char dsi_flags[] = "video\0burst\0no-eot\0lp-commands";
static const char bus_flags[] = "de-low\0pixdata-negedge";
static const uint8_t init[] = {
	0x39, 3, 0xaa, 0x01, 0x02,
	0x15, 2, 0xbb, 0x03,
	0x05, 1, 0x11,
	0x03, 0,
	0x13, 1, 0x44,
	0x23, 2, 0x55, 0x66,
	0x29, 0,
};

static const uint32_t svga[9] = { 40000, 800, 840, 968, 1056, 600, 601, 605, 628 };

static struct dsi_props
good(void)
{
	static const uint32_t twice[9] = { 80000, 800, 840, 968, 1056, 600, 601, 605, 628 };
	for (unsigned i = 0; i < 9; i++) {
		put32(modes + 4 * i, twice[i]);
		put32(modes + 36 + 4 * i, svga[i]);
	}
	put32(lanes4, 4);
	put32(lanes0, 0);
	put32(lanes5, 5);
	return (struct dsi_props){
		.id = PROP(id), .modes = PROP(modes), .mode_flags = PROP(mode_flags), .dsi_lanes = PROP(lanes4),
		.dsi_format = PROP(rgb888), .dsi_flags = PROP(dsi_flags), .bus_flags = PROP(bus_flags), .init = PROP(init),
	};
}

static void
refuse(const char *name, struct dsi_props p)
{
	struct dsi_panel panel;
	const char *why = NULL;
	bool ok = dsi_panel_parse(&p, &panel, &why);
	report(name, !ok && why, ok ? "parsed" : why);
}

static void
parse_cases(void)
{
	struct dsi_props p = good();
	struct dsi_panel panel;
	const char *why = NULL;
	bool ok = dsi_panel_parse(&p, &panel, &why);
	report("a good entry parses", ok, why);
	if (!ok) {
		return;
	}
	report("the mode nearest 60 Hz", panel.mode.clock_khz == 40000 && panel.mode.hdisplay == 800 &&
	    panel.mode.hsync_start == 840 && panel.mode.hsync_end == 968 && panel.mode.htotal == 1056 &&
	    panel.mode.vdisplay == 600 && panel.mode.vsync_start == 601 && panel.mode.vsync_end == 605 &&
	    panel.mode.vtotal == 628, NULL);
	report("sync polarities", panel.mode.hsync_low && panel.mode.vsync_low, NULL);
	report("link flags", panel.lanes == 4 && panel.burst && !panel.sync_pulses && panel.no_eot && panel.lp_commands,
	    NULL);
	report("ID", panel.id_len == 2 && panel.id[0] == 0x12 && panel.id[1] == 0x34, NULL);

	static const uint8_t types[] = { 0x39, 0x15, 0x05, 0x03, 0x13, 0x23, 0x29 };
	static const uint8_t lens[] = { 3, 2, 1, 0, 1, 2, 0 };
	size_t off = 0;
	unsigned n = 0;
	bool order = true;
	struct dsi_packet pkt;
	while (dsi_packet_next(&panel, &off, &pkt)) {
		order = order && n < sizeof(types) && pkt.type == types[n] && pkt.len == lens[n];
		if (n == 0) {
			order = order && pkt.data[0] == 0xaa && pkt.data[2] == 0x02;
		}
		n++;
	}
	report("packets in order", order && n == sizeof(types) && off == sizeof(init), NULL);

	static const uint8_t longer[] = { 0x12, 0x34, 0x56 }, other[] = { 0x12, 0x35, 0x56 }, shorter[] = { 0x12 };
	report("an ID with more bytes matches", dsi_panel_matches(&panel, longer, sizeof(longer)), NULL);
	report("another ID does not match", !dsi_panel_matches(&panel, other, sizeof(other)), NULL);
	report("a shorter ID does not match", !dsi_panel_matches(&panel, shorter, sizeof(shorter)), NULL);

	p = good();
	p.id = PROP(longer);
	ok = dsi_panel_parse(&p, &panel, &why);
	report("a 3-byte pie,id parses", ok && panel.id_len == 3 && dsi_panel_matches(&panel, longer, sizeof(longer)),
	    ok ? NULL : why);

	p = good();
	p.mode_flags.val = NULL;
	p.bus_flags.val = NULL;
	ok = dsi_panel_parse(&p, &panel, &why);
	report("flags may be absent", ok && !panel.mode.hsync_low && !panel.mode.vsync_low, ok ? NULL : why);

	static const char pulses[] = "video\0sync-pulse\0lp-commands";
	p = good();
	p.dsi_flags = PROP(pulses);
	ok = dsi_panel_parse(&p, &panel, &why);
	report("sync pulses", ok && !panel.burst && panel.sync_pulses && !panel.no_eot && panel.lp_commands,
	    ok ? NULL : why);
}

static void
refuse_cases(void)
{
	struct dsi_props p;
	static const uint8_t id4[] = { 1, 2, 3, 4 }, id5[] = { 1, 2, 3, 4, 5 };
	static const char lpm[] = "video\0lpm", novideo[] = "burst", both[] = "video\0burst\0sync-pulse";
	static const char hs[] = "video\0burst\0no-eot";
	static const char empty_word[] = "video\0\0burst", hboth[] = "hsync-low\0hsync-high", bus[] = "de-low\0bus";
	static const uint8_t bad_type[] = { 0x07, 1, 0x00 }, cut[] = { 0x39, 4, 0xaa, 0x01 };
	static const uint8_t short1[] = { 0x15, 1, 0x36 }, dcs0[] = { 0x05, 2, 0x11, 0x00 };
	static const uint8_t long0[] = { 0x39, 0 }, lone[] = { 0x05 };

	p = good();
	p.id.val = NULL;
	refuse("no pie,id", p);
	p = good();
	p.id = PROP(id4);
	refuse("a 4-byte pie,id", p);
	p = good();
	p.id = PROP(id5);
	refuse("a 5-byte pie,id", p);
	p = good();
	p.modes.len = 35;
	refuse("pie,modes not 9 cells a mode", p);
	p = good();
	put32(modes + 36 + 8, 799);
	refuse("sync start before the display's end", p);
	p = good();
	put32(modes + 36 + 16, 968);
	refuse("no back porch", p);
	p = good();
	p.dsi_lanes = PROP(lanes0);
	refuse("0 lanes", p);
	p = good();
	p.dsi_lanes = PROP(lanes5);
	refuse("5 lanes", p);
	p = good();
	p.dsi_lanes.val = NULL;
	refuse("no pie,dsi-lanes", p);
	p = good();
	p.dsi_format = PROP(rgb666);
	refuse("rgb666", p);
	p = good();
	p.dsi_format = PROP(nonul);
	refuse("a format without its NUL", p);
	p = good();
	p.dsi_flags = PROP(lpm);
	refuse("an unknown link flag", p);
	p = good();
	p.dsi_flags = PROP(novideo);
	refuse("no video flag", p);
	p = good();
	p.dsi_flags = PROP(hs);
	refuse("no lp-commands flag", p);
	p = good();
	p.dsi_flags = PROP(both);
	refuse("burst and sync pulses", p);
	p = good();
	p.dsi_flags = PROP(empty_word);
	refuse("an empty word", p);
	p = good();
	p.mode_flags = PROP(hboth);
	refuse("hsync low and high", p);
	p = good();
	p.bus_flags = PROP(bus);
	refuse("an unknown bus flag", p);
	p = good();
	p.init = PROP(bad_type);
	refuse("a packet type other than a write", p);
	p = good();
	p.init = PROP(cut);
	refuse("a cut packet", p);
	p = good();
	p.init = PROP(short1);
	refuse("a DCS short write of one byte with a parameter", p);
	p = good();
	p.init = PROP(dcs0);
	refuse("a DCS short write without parameters of two bytes", p);
	p = good();
	p.init = PROP(long0);
	refuse("a DCS long write without a command", p);
	p = good();
	p.init = PROP(lone);
	refuse("a lone type byte", p);
	p = good();
	p.init.len = 0;
	refuse("an empty init list", p);
}

static void
timing_cases(void)
{
	struct dsi_mode m = { svga[0], (uint16_t)svga[1], (uint16_t)svga[2], (uint16_t)svga[3], (uint16_t)svga[4],
		(uint16_t)svga[5], (uint16_t)svga[6], (uint16_t)svga[7], (uint16_t)svga[8], false, false };
	struct dsi_timing t;
	bool ok = dsi_timing(&m, 4, 600000, &t);
	char d[160];
	snprintf(d, sizeof(d), "hsa %u hbp %u hline %u vsa %u vbp %u vfp %u vactive %u", t.hsa, t.hbp, t.hline, t.vsa,
	    t.vbp, t.vfp, t.vactive);
	report("SVGA on 4 lanes at 600 Mbps", ok && t.hsa == 240 && t.hbp == 165 && t.hline == 1980 && t.vsa == 4 &&
	    t.vbp == 23 && t.vfp == 1 && t.vactive == 600, d);
	ok = dsi_timing(&m, 4, 500000, &t);
	snprintf(d, sizeof(d), "hsa %u hbp %u hline %u", t.hsa, t.hbp, t.hline);
	report("rounding to the nearest cycle", ok && t.hsa == 200 && t.hbp == 138 && t.hline == 1650, d);
	report("too slow for the pixels", !dsi_timing(&m, 1, 500000, &t), NULL);
	report("no lanes", !dsi_timing(&m, 0, 500000, &t), NULL);
}

struct pattern_region {
	const char *name;
	unsigned x0, y0, x1, y1;
	uint32_t colour;
};

static void
pattern_case(unsigned width, unsigned height, const struct pattern_region *regions, size_t count)
{
	size_t n = (size_t)width * height;
	uint32_t *pixels = malloc((n + 2) * sizeof(*pixels));
	char name[80], detail[96];
	snprintf(name, sizeof(name), "%ux%u pattern buffer", width, height);
	if (!pixels) {
		report(name, false, "no memory");
		return;
	}
	for (size_t i = 0; i < n + 2; i++) {
		pixels[i] = 0xa5a5a5a5;
	}
	struct dsi_mode mode = { .hdisplay = (uint16_t)width, .vdisplay = (uint16_t)height };
	dsi_panel_pattern(&mode, pixels + 1);
	for (size_t i = 0; i < count; i++) {
		const struct pattern_region *r = &regions[i];
		bool pass = true;
		for (unsigned y = r->y0; y <= r->y1 && pass; y++) {
			for (unsigned x = r->x0; x <= r->x1 && pass; x++) {
				uint32_t colour = pixels[1 + (size_t)y * width + x];
				if (colour != r->colour) {
					snprintf(detail, sizeof(detail), "(%u,%u) is 0x%08x, expected 0x%08x", x, y, colour,
					    r->colour);
					pass = false;
				}
			}
		}
		snprintf(name, sizeof(name), "%ux%u %s", width, height, r->name);
		report(name, pass, pass ? NULL : detail);
	}
	snprintf(name, sizeof(name), "%ux%u pattern stays in its buffer", width, height);
	report(name, pixels[0] == 0xa5a5a5a5 && pixels[n + 1] == 0xa5a5a5a5, NULL);
	free(pixels);
}

static void
pattern_cases(void)
{
	static const struct pattern_region vga[] = {
		{ "white marker", 0, 0, 95, 95, 0x00ffffff },
		{ "red beside the marker", 96, 0, 319, 95, 0x00ff0000 },
		{ "red below the marker", 0, 96, 319, 239, 0x00ff0000 },
		{ "green quadrant", 320, 0, 639, 239, 0x0000ff00 },
		{ "blue above the band", 0, 240, 319, 431, 0x000000ff },
		{ "yellow above the band", 320, 240, 639, 431, 0x00ffff00 },
		{ "magenta band", 0, 432, 639, 479, 0x00ff00ff },
	};
	static const struct pattern_region scaled[] = {
		{ "white marker", 0, 0, 126, 119, 0x00ffffff },
		{ "red beside the marker", 127, 0, 425, 119, 0x00ff0000 },
		{ "red below the marker", 0, 120, 425, 299, 0x00ff0000 },
		{ "green quadrant", 426, 0, 852, 299, 0x0000ff00 },
		{ "blue above the band", 0, 300, 425, 540, 0x000000ff },
		{ "yellow above the band", 426, 300, 852, 540, 0x00ffff00 },
		{ "magenta band", 0, 541, 852, 600, 0x00ff00ff },
	};
	static const struct pattern_region small[] = {
		{ "white marker", 0, 0, 0, 0, 0x00ffffff },
		{ "red beside the marker", 1, 0, 1, 0, 0x00ff0000 },
		{ "red below the marker", 0, 1, 1, 1, 0x00ff0000 },
		{ "green quadrant", 2, 0, 4, 1, 0x0000ff00 },
		{ "blue above the band", 0, 2, 1, 2, 0x000000ff },
		{ "yellow above the band", 2, 2, 4, 2, 0x00ffff00 },
		{ "magenta band", 0, 3, 4, 3, 0x00ff00ff },
	};
	static const struct pattern_region single[] = {
		{ "magenta band", 0, 0, 0, 0, 0x00ff00ff },
	};
	pattern_case(640, 480, vga, sizeof(vga) / sizeof(vga[0]));
	pattern_case(853, 601, scaled, sizeof(scaled) / sizeof(scaled[0]));
	pattern_case(5, 4, small, sizeof(small) / sizeof(small[0]));
	pattern_case(1, 1, single, sizeof(single) / sizeof(single[0]));
}

static void
pll_case(uint32_t khz)
{
	struct rk_pll pll;
	char name[64], d[160];
	snprintf(name, sizeof(name), "an exact PLL for %u kHz", khz);
	if (!rk_pll_exact(khz, &pll)) {
		report(name, false, "none found");
		return;
	}
	uint64_t vco_num = 24000ull * pll.fbdiv, out_den = (uint64_t)pll.refdiv * pll.postdiv1 * pll.postdiv2 * pll.div;
	bool exact = vco_num == (uint64_t)khz * out_den;
	bool vco = vco_num >= 950000ull * pll.refdiv && vco_num <= 3800000ull * pll.refdiv;
	bool pfd = 24000 / pll.refdiv >= 1000 && pll.refdiv >= 1;
	bool post = pll.postdiv1 >= 1 && pll.postdiv1 <= 7 && pll.postdiv2 >= 1 && pll.postdiv2 <= pll.postdiv1;
	bool out = vco_num <= 600000ull * pll.refdiv * pll.postdiv1 * pll.postdiv2;
	bool ranges = pll.fbdiv >= 16 && pll.fbdiv <= 2500 && pll.div >= 1 && pll.div <= 256;
	snprintf(d, sizeof(d), "refdiv %u fbdiv %u postdiv %u %u div %u", pll.refdiv, pll.fbdiv, pll.postdiv1,
	    pll.postdiv2, pll.div);
	report(name, exact && vco && pfd && post && out && ranges, d);
}

static void
pll_cases(void)
{
	pll_case(25175);
	pll_case(40000);
	pll_case(65000);
	pll_case(148500);
	struct rk_pll pll;
	report("no exact PLL for 24151 kHz", !rk_pll_exact(24151, &pll), NULL);
	report("no PLL above 600 MHz", !rk_pll_exact(600001, &pll), NULL);
	report("no PLL for 0 kHz", !rk_pll_exact(0, &pll), NULL);
}

int
main(void)
{
	parse_cases();
	refuse_cases();
	timing_cases();
	pattern_cases();
	pll_cases();
	printf("dsipanel-test: %s\n", failed ? "FAIL" : "PASS");
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
