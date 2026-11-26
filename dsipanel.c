/* SPDX-License-Identifier: BSD-2-Clause */
#include "dsipanel.h"

#define MODE_CELLS              9
#define REFRESH_MHZ             60000u
#define MAX_CLOCK_KHZ           600000u
#define MAX_DISPLAY             4096u
#define MAX_TOTAL               8191u

#define DT_GEN_SHORT_0          0x03
#define DT_GEN_SHORT_1          0x13
#define DT_GEN_SHORT_2          0x23
#define DT_DCS_SHORT_0          0x05
#define DT_DCS_SHORT_1          0x15
#define DT_GEN_LONG             0x29
#define DT_DCS_LONG             0x39

#define PLL_REF_KHZ             24000u
#define PLL_VCO_MIN_KHZ         950000u
#define PLL_VCO_MAX_KHZ         3800000u
#define PLL_OUT_MAX_KHZ         600000u
#define PLL_PFD_MIN_KHZ         1000u
#define PLL_FBDIV_MIN           16u
#define PLL_FBDIV_MAX           2500u
#define PLL_POSTDIV_MAX         7u
#define PLL_DIV_MAX             256u

enum { MODE_HSYNC_LOW, MODE_VSYNC_LOW, MODE_HSYNC_HIGH, MODE_VSYNC_HIGH };
static const char *const mode_words[] = { "hsync-low", "vsync-low", "hsync-high", "vsync-high" };

enum { DSI_VIDEO, DSI_BURST, DSI_SYNC_PULSE, DSI_NO_EOT, DSI_LP_COMMANDS };
static const char *const dsi_words[] = { "video", "burst", "sync-pulse", "no-eot", "lp-commands" };

static const char *const bus_words[] = { "de-low", "de-high", "pixdata-negedge", "pixdata-posedge" };

#define BIT(n)                  (1u << (n))
#define COUNT(a)                (sizeof(a) / sizeof((a)[0]))

static uint32_t
cell(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static bool
same(const uint8_t *s, size_t len, const char *w)
{
	for (size_t i = 0; i < len; i++) {
		if (!w[i] || s[i] != (uint8_t)w[i]) {
			return false;
		}
	}
	return !w[len];
}

static bool
is_string(const struct dsi_prop *p, const char *w)
{
	return p->val && p->len && !p->val[p->len - 1] && same(p->val, p->len - 1, w);
}

static bool
words(const struct dsi_prop *p, const char *const *known, unsigned n, uint32_t *bits)
{
	*bits = 0;
	if (!p->val) {
		return true;
	}
	if (!p->len || p->val[p->len - 1]) {
		return false;
	}
	for (uint32_t i = 0; i < p->len;) {
		uint32_t j = i;
		while (p->val[j]) {
			j++;
		}
		unsigned k = 0;
		while (k < n && !same(p->val + i, j - i, known[k])) {
			k++;
		}
		if (k == n) {
			return false;
		}
		*bits |= BIT(k);
		i = j + 1;
	}
	return true;
}

static bool
mode_at(const uint8_t *p, struct dsi_mode *m)
{
	uint32_t c[MODE_CELLS];
	for (unsigned i = 0; i < MODE_CELLS; i++) {
		c[i] = cell(p + 4 * i);
	}
	if (!c[0] || c[0] > MAX_CLOCK_KHZ || !c[1] || c[1] > MAX_DISPLAY || c[1] > c[2] || c[2] >= c[3] ||
	    c[3] >= c[4] || c[4] > MAX_TOTAL || !c[5] || c[5] > MAX_DISPLAY || c[5] > c[6] || c[6] >= c[7] ||
	    c[7] >= c[8] || c[8] > MAX_TOTAL) {
		return false;
	}
	m->clock_khz = c[0];
	m->hdisplay = (uint16_t)c[1];
	m->hsync_start = (uint16_t)c[2];
	m->hsync_end = (uint16_t)c[3];
	m->htotal = (uint16_t)c[4];
	m->vdisplay = (uint16_t)c[5];
	m->vsync_start = (uint16_t)c[6];
	m->vsync_end = (uint16_t)c[7];
	m->vtotal = (uint16_t)c[8];
	return true;
}

static uint64_t
refresh_mhz(const struct dsi_mode *m)
{
	return (uint64_t)m->clock_khz * 1000000u / ((uint32_t)m->htotal * m->vtotal);
}

static bool
init_ok(const uint8_t *p, size_t len)
{
	if (!len) {
		return false;
	}
	for (size_t off = 0; off < len;) {
		if (len - off < 2 || len - off - 2 < p[off + 1]) {
			return false;
		}
		uint8_t n = p[off + 1];
		bool ok;
		switch (p[off]) {
		case DT_GEN_SHORT_0:
			ok = n == 0;
			break;
		case DT_GEN_SHORT_1:
		case DT_DCS_SHORT_0:
			ok = n == 1;
			break;
		case DT_GEN_SHORT_2:
		case DT_DCS_SHORT_1:
			ok = n == 2;
			break;
		case DT_GEN_LONG:
			ok = true;
			break;
		case DT_DCS_LONG:
			ok = n >= 1;
			break;
		default:
			ok = false;
			break;
		}
		if (!ok) {
			return false;
		}
		off += 2 + (size_t)n;
	}
	return true;
}

bool
dsi_panel_parse(const struct dsi_props *props, struct dsi_panel *out, const char **why)
{
	const struct dsi_prop *id = &props->id, *modes = &props->modes, *lanes = &props->dsi_lanes;
	const struct dsi_prop *format = &props->dsi_format, *init = &props->init;
	uint32_t mode_bits, dsi_bits, bus_bits;

	if (!id->val || !id->len || id->len > sizeof(out->id)) {
		*why = "pie,id is not 1 to 3 bytes";
		return false;
	}
	if (!modes->val || !modes->len || modes->len % (4 * MODE_CELLS)) {
		*why = "pie,modes is not a list of 9 cells per mode";
		return false;
	}
	if (!lanes->val || lanes->len != 4 || !cell(lanes->val) || cell(lanes->val) > 4) {
		*why = "pie,dsi-lanes is not 1 to 4";
		return false;
	}
	if (!is_string(format, "rgb888")) {
		*why = "pie,dsi-format is not rgb888";
		return false;
	}
	if (!words(&props->mode_flags, mode_words, COUNT(mode_words), &mode_bits) ||
	    ((mode_bits & BIT(MODE_HSYNC_LOW)) && (mode_bits & BIT(MODE_HSYNC_HIGH))) ||
	    ((mode_bits & BIT(MODE_VSYNC_LOW)) && (mode_bits & BIT(MODE_VSYNC_HIGH)))) {
		*why = "pie,mode-flags has an unknown or contradictory flag";
		return false;
	}
	if (!words(&props->dsi_flags, dsi_words, COUNT(dsi_words), &dsi_bits) || !(dsi_bits & BIT(DSI_VIDEO)) ||
	    ((dsi_bits & BIT(DSI_BURST)) && (dsi_bits & BIT(DSI_SYNC_PULSE)))) {
		*why = "pie,dsi-flags lacks video, or has an unknown or contradictory flag";
		return false;
	}
	if (!(dsi_bits & BIT(DSI_LP_COMMANDS))) {
		*why = "pie,dsi-flags lacks lp-commands";
		return false;
	}
	if (!words(&props->bus_flags, bus_words, COUNT(bus_words), &bus_bits)) {
		*why = "pie,bus-flags has an unknown flag";
		return false;
	}
	if (!init->val || !init_ok(init->val, init->len)) {
		*why = "pie,init is not a list of DCS and generic writes";
		return false;
	}

	bool found = false;
	uint64_t best = 0;
	for (uint32_t off = 0; off < modes->len; off += 4 * MODE_CELLS) {
		struct dsi_mode m;
		if (!mode_at(modes->val + off, &m)) {
			*why = "pie,modes has a mode out of order or out of range";
			return false;
		}
		uint64_t r = refresh_mhz(&m), d = r > REFRESH_MHZ ? r - REFRESH_MHZ : REFRESH_MHZ - r;
		if (!found || d < best) {
			out->mode = m;
			best = d;
			found = true;
		}
	}
	out->mode.hsync_low = (mode_bits & BIT(MODE_HSYNC_LOW)) != 0;
	out->mode.vsync_low = (mode_bits & BIT(MODE_VSYNC_LOW)) != 0;
	for (unsigned i = 0; i < id->len; i++) {
		out->id[i] = id->val[i];
	}
	out->id_len = id->len;
	out->lanes = cell(lanes->val);
	out->burst = (dsi_bits & BIT(DSI_BURST)) != 0;
	out->sync_pulses = (dsi_bits & BIT(DSI_SYNC_PULSE)) != 0;
	out->no_eot = (dsi_bits & BIT(DSI_NO_EOT)) != 0;
	out->lp_commands = (dsi_bits & BIT(DSI_LP_COMMANDS)) != 0;
	out->init = init->val;
	out->init_len = init->len;
	return true;
}

bool
dsi_panel_matches(const struct dsi_panel *panel, const uint8_t *id, size_t n)
{
	if (n < panel->id_len) {
		return false;
	}
	for (unsigned i = 0; i < panel->id_len; i++) {
		if (id[i] != panel->id[i]) {
			return false;
		}
	}
	return true;
}

bool
dsi_packet_next(const struct dsi_panel *panel, size_t *off, struct dsi_packet *pkt)
{
	if (*off + 2 > panel->init_len) {
		return false;
	}
	pkt->type = panel->init[*off];
	pkt->len = panel->init[*off + 1];
	pkt->data = panel->init + *off + 2;
	*off += 2 + (size_t)pkt->len;
	return true;
}

void
dsi_panel_pattern(const struct dsi_mode *mode, uint32_t *pixels)
{
	unsigned width = mode->hdisplay, height = mode->vdisplay;
	unsigned corner_width = width * 96 / 640, corner_height = height * 96 / 480;
	unsigned band_height = height * 48 / 480;
	corner_width = corner_width ? corner_width : 1;
	corner_height = corner_height ? corner_height : 1;
	band_height = band_height ? band_height : 1;
	for (unsigned y = 0; y < height; y++) {
		for (unsigned x = 0; x < width; x++) {
			uint32_t colour;
			if (y >= height - band_height) {
				colour = 0x00ff00ff;
			} else if (x < corner_width && y < corner_height) {
				colour = 0x00ffffff;
			} else if (y < height / 2) {
				colour = x < width / 2 ? 0x00ff0000 : 0x0000ff00;
			} else {
				colour = x < width / 2 ? 0x000000ff : 0x00ffff00;
			}
			pixels[(size_t)y * width + x] = colour;
		}
	}
}

static uint32_t
byte_clocks(uint32_t pixels, uint32_t lane_kbps, uint32_t clock_khz)
{
	uint64_t den = 8ull * clock_khz;
	return (uint32_t)(((uint64_t)pixels * lane_kbps + den / 2) / den);
}

bool
dsi_timing(const struct dsi_mode *mode, unsigned lanes, uint32_t lane_kbps, struct dsi_timing *t)
{
	if (!lanes || !mode->clock_khz || (uint64_t)lane_kbps * lanes < 24ull * mode->clock_khz) {
		return false;
	}
	t->hsa = byte_clocks(mode->hsync_end - mode->hsync_start, lane_kbps, mode->clock_khz);
	t->hbp = byte_clocks(mode->htotal - mode->hsync_end, lane_kbps, mode->clock_khz);
	t->hline = byte_clocks(mode->htotal, lane_kbps, mode->clock_khz);
	t->vsa = mode->vsync_end - mode->vsync_start;
	t->vbp = mode->vtotal - mode->vsync_end;
	t->vfp = mode->vsync_start - mode->vdisplay;
	t->vactive = mode->vdisplay;
	return t->hsa <= 0xfff && t->hbp <= 0xfff && t->hline <= 0x7fff && t->vsa <= 0x3ff && t->vbp <= 0x3ff &&
	    t->vfp <= 0x3ff && t->vactive <= 0x3fff;
}

bool
rk_pll_exact(uint32_t khz, struct rk_pll *out)
{
	if (!khz || khz > PLL_OUT_MAX_KHZ) {
		return false;
	}
	for (uint32_t refdiv = 1; PLL_REF_KHZ / refdiv >= PLL_PFD_MIN_KHZ; refdiv++) {
		for (uint32_t p1 = 1; p1 <= PLL_POSTDIV_MAX; p1++) {
			for (uint32_t p2 = 1; p2 <= p1; p2++) {
				for (uint32_t div = 1; div <= PLL_DIV_MAX && (uint64_t)khz * div <= PLL_OUT_MAX_KHZ; div++) {
					uint64_t vco = (uint64_t)khz * div * p1 * p2;
					if (vco > PLL_VCO_MAX_KHZ) {
						break;
					}
					if (vco < PLL_VCO_MIN_KHZ || (vco * refdiv) % PLL_REF_KHZ) {
						continue;
					}
					uint64_t fbdiv = vco * refdiv / PLL_REF_KHZ;
					if (fbdiv < PLL_FBDIV_MIN || fbdiv > PLL_FBDIV_MAX) {
						continue;
					}
					out->refdiv = (uint8_t)refdiv;
					out->postdiv1 = (uint8_t)p1;
					out->postdiv2 = (uint8_t)p2;
					out->fbdiv = (uint16_t)fbdiv;
					out->div = (uint16_t)div;
					return true;
				}
			}
		}
	}
	return false;
}
