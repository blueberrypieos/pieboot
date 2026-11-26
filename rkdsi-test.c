/* SPDX-License-Identifier: BSD-2-Clause */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#undef strchr
#define printf(...) panel_printf(__VA_ARGS__)
#include "rkdsi.h"

static uint32_t panel_test_read(uint64_t addr);
static void panel_test_write(uint64_t addr, uint32_t val);
static uint64_t test_counter(void);

#define RKDSI_TEST
#define read_cntpct() test_counter()
#define read_cntfrq() 1000000ull
#define read_isr() 0ull
#define dsb_sy() ((void)0)
#define dcache_clean_invalidate(start, end) ((void)0)
#include "rkdsi.c"
#include "fdt.c"
#undef printf

#define VOP 0xfe040000ull
#define CRU 0xfdd20000ull
#define PMUCRU 0xfdd00000ull
#define PMU 0xfdd90000ull
#define GRF 0xfdc60000ull
#define DSI 0xfe060000ull
#define FB_SIZE 0x12c000ull

static const struct {
	uint64_t gate, reset;
	unsigned gate_bit, reset_bit;
} gpio_model[5] = {
	{ PMUCRU + PMUCRU_GATE(1), PMUCRU + PMUCRU_SOFTRST(0), 9, 9 },
	{ CRU + CRU_GATE(31), CRU + CRU_SOFTRST(21), 2, 10 },
	{ CRU + CRU_GATE(31), CRU + CRU_SOFTRST(21), 4, 12 },
	{ CRU + CRU_GATE(31), CRU + CRU_SOFTRST(21), 6, 14 },
	{ CRU + CRU_GATE(31), CRU + CRU_SOFTRST(22), 8, 0 },
};

static int failed;
static _Alignas(16) uint8_t arena[65536];
static size_t arena_used;
static char messages[16384];
static size_t messages_used;

static struct {
	struct {
		uint64_t addr;
		uint32_t val, initial;
	} regs[512];
	unsigned nregs, reads, writes, fail_gate, reset_after_start, gpio_writes[5];
	uint64_t ticks, start, stop, backlight, progress, delay;
	bool fail_power, fail_idle, fail_phy, no_id, fail_init, fail_pll, stalled, noise, rx, id_returned, pins_changed, bad_gpio;
} sim;

static struct {
	struct fdt fdt;
	struct fdt_node *host, *phy, *panel, *entry;
} tree;

void *
balloc(size_t size)
{
	size = ALIGN_UP(size, 16);
	if (size > sizeof(arena) - arena_used) {
		abort();
	}
	void *p = arena + arena_used;
	arena_used += size;
	memset(p, 0, size);
	return p;
}

void
panic(const char *fmt, ...)
{
	(void)fmt;
	abort();
}

void
panel_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(messages + messages_used, sizeof(messages) - messages_used, fmt, ap);
	va_end(ap);
	if (n > 0) {
		messages_used += MIN((size_t)n, sizeof(messages) - messages_used - 1);
	}
}

static unsigned
reg_index(uint64_t addr)
{
	for (unsigned i = 0; i < sim.nregs; i++) {
		if (sim.regs[i].addr == addr) {
			return i;
		}
	}
	if (sim.nregs == sizeof(sim.regs) / sizeof(sim.regs[0])) {
		abort();
	}
	sim.regs[sim.nregs].addr = addr;
	return sim.nregs++;
}

static uint32_t
value(uint64_t addr)
{
	return sim.regs[reg_index(addr)].val;
}

static void
seed(uint64_t addr, uint32_t val)
{
	unsigned i = reg_index(addr);
	sim.regs[i].val = sim.regs[i].initial = val;
}

static uint64_t
test_counter(void)
{
	return ++sim.ticks;
}

static uint32_t
panel_test_read(uint64_t addr)
{
	sim.reads++;
	uint32_t val = value(addr);
	for (unsigned b = 0; b < 5; b++) {
		if (sim.start && (sim.reset_after_start & (1u << b)) && addr == gpio_model[b].reset) {
			val |= 1u << gpio_model[b].reset_bit;
		}
	}
	if (addr == DSI + DSI_PHY_STATUS) {
		return sim.fail_phy ? 0 : PHY_LOCK | PHY_STOP_CLK | (1u << 4) | (1u << 7) | (1u << 9) | (1u << 11);
	}
	if (addr == DSI + DSI_CMD_PKT_STATUS) {
		return PKT_GEN_CMD_EMPTY | PKT_GEN_PLD_W_EMPTY | (sim.rx ? 0 : PKT_GEN_PLD_R_EMPTY) |
		    (sim.fail_init && sim.id_returned ? PKT_GEN_CMD_FULL : 0);
	}
	if (addr == DSI + DSI_GEN_PLD_DATA) {
		sim.rx = false;
		sim.id_returned = true;
		return 0x563412;
	}
	if (addr == CRU + CRU_VPLL_CON(1) && sim.fail_pll &&
	    value(CRU + CRU_VPLL_CON(0)) != sim.regs[reg_index(CRU + CRU_VPLL_CON(0))].initial) {
		return val & ~VPLL_CON1_LOCK;
	}
	if (addr == VOP + VOP2_STATUS(1)) {
		if (sim.start && !sim.stalled && sim.ticks - sim.start >= sim.delay &&
		    !(value(VOP + VOP2_VP(1) + VP_DSP_CTRL) & DSP_CTRL_STANDBY)) {
			val = (uint32_t)((sim.ticks - sim.start) / 32 % 525) << 16;
			if (!sim.progress) {
				sim.progress = sim.ticks;
			}
		}
		return val | (sim.noise ? (uint32_t)sim.ticks & 0xffff : 0);
	}
	return val;
}

static void
panel_test_write(uint64_t addr, uint32_t val)
{
	sim.writes++;
	unsigned i = reg_index(addr);
	bool masked = (addr >= CRU && addr < CRU + 0x500) || (addr >= PMUCRU && addr < PMUCRU + 0x210) ||
	    (addr >= GRF && addr < GRF + 0x400) || (addr >= 0xfdc20000 && addr < 0xfdc20020) ||
	    addr == PMU + PMU_PWR_GATE_SFTCON || addr == PMU + PMU_BUS_IDLE_SFTCON0 ||
	    addr == VOP + VOP2_REG_CFG_DONE || addr == VOP + VOP2_PORT1_INTR_CLR;
	for (unsigned b = 0; b < 5; b++) {
		if (addr >= gpio_banks[b].base && addr < gpio_banks[b].base + 0x10) {
			masked = sim.pins_changed = true;
			sim.gpio_writes[b]++;
			if ((value(gpio_model[b].gate) & (1u << gpio_model[b].gate_bit)) ||
			    (panel_test_read(gpio_model[b].reset) & (1u << gpio_model[b].reset_bit))) {
				sim.bad_gpio = true;
			}
		}
	}
	if (masked) {
		uint32_t mask = val >> 16;
		for (unsigned b = 0; b < 5; b++) {
			if ((sim.fail_gate & (1u << b)) && addr == gpio_model[b].gate) {
				mask &= ~(1u << gpio_model[b].gate_bit);
			}
		}
		sim.regs[i].val = (sim.regs[i].val & ~mask) | (val & mask);
	} else {
		sim.regs[i].val = val;
	}
	if (addr == PMU + PMU_PWR_GATE_SFTCON && (!sim.fail_power || (val & PWR_PD_VO))) {
		sim.regs[reg_index(PMU + PMU_PWR_DWN_ST)].val = value(addr) & PWR_PD_VO;
	}
	if (addr == PMU + PMU_BUS_IDLE_SFTCON0 && !sim.fail_idle) {
		sim.regs[reg_index(PMU + PMU_BUS_IDLE_ACK)].val = value(addr) & BUS_IDLE_VO;
		sim.regs[reg_index(PMU + PMU_BUS_IDLE_ST)].val = value(addr) & BUS_IDLE_VO;
	}
	if (addr == DSI + DSI_GEN_HDR && (val & 0xff) == DT_DCS_READ_0) {
		sim.rx = !sim.no_id;
	}
	if (addr == VOP + VOP2_VP(1) + VP_DSP_CTRL && !(val & DSP_CTRL_STANDBY)) {
		sim.start = sim.ticks;
	}
	if (addr == VOP + VOP2_VP(1) + VP_DSP_CTRL && (val & DSP_CTRL_STANDBY) && sim.start && !sim.stop) {
		sim.stop = sim.ticks;
	}
	if (addr == PWM1_BASE + PWM_CTRL && (val & PWM_CTRL_EN)) {
		sim.backlight = sim.ticks;
	}
}

static void
report(const char *name, bool pass)
{
	printf("%s: %s\n", pass ? "ok" : "FAILED", name);
	if (!pass) {
		printf("%s", messages);
		failed = 1;
	}
}

static void
cells(struct fdt_node *n, const char *name, const uint32_t *v, unsigned count)
{
	uint8_t buf[36];
	for (unsigned i = 0; i < count; i++) {
		buf[4 * i] = (uint8_t)(v[i] >> 24);
		buf[4 * i + 1] = (uint8_t)(v[i] >> 16);
		buf[4 * i + 2] = (uint8_t)(v[i] >> 8);
		buf[4 * i + 3] = (uint8_t)v[i];
	}
	struct fdt_prop *p = fdt_prop(n, name);
	if (p) {
		uint8_t *data = balloc(4 * count);
		memcpy(data, buf, 4 * count);
		p->val = data;
		p->len = 4 * count;
	} else {
		fdt_add_prop(n, name, buf, 4 * count);
	}
}

#define CELLS(n, name, ...) cells(n, name, (uint32_t[]){ __VA_ARGS__ }, \
    sizeof((uint32_t[]){ __VA_ARGS__ }) / sizeof(uint32_t))

static struct fdt_node *
device(const char *name, const char *compat, uint32_t base)
{
	struct fdt_node *n = fdt_add_node(tree.fdt.root, name);
	fdt_add_prop(n, "compatible", compat, (uint32_t)strlen(compat) + 1);
	CELLS(n, "reg", 0, base, 0x10000);
	return n;
}

static void
connect_host(struct fdt_node *host)
{
	struct fdt_node *ports = fdt_add_node(host, "ports");
	struct fdt_node *port = fdt_add_node(ports, "port@0");
	fdt_add_prop_u32(port, "reg", 0);
	fdt_add_prop_u32(fdt_add_node(port, "endpoint"), "remote-endpoint", 10);
}

static void
setup(void)
{
	memset(&sim, 0, sizeof(sim));
	memset(&tree, 0, sizeof(tree));
	arena_used = messages_used = 0;
	messages[0] = 0;
	recording = NULL;
	tree.fdt.root = balloc(sizeof(*tree.fdt.root));
	tree.fdt.root->name = "";
	fdt_add_node(tree.fdt.root, "chosen");
	struct fdt_node *vop = device("vop", "rockchip,rk3566-vop", VOP);
	struct fdt_node *vp = fdt_add_node(fdt_add_node(vop, "ports"), "port@1");
	fdt_add_prop_u32(vp, "reg", 1);
	fdt_add_prop_u32(fdt_add_node(vp, "endpoint"), "phandle", 10);
	device("cru", "rockchip,rk3568-cru", CRU);
	device("pmucru", "rockchip,rk3568-pmucru", PMUCRU);
	device("pmu", "rockchip,rk3568-pmu", PMU);
	device("grf", "rockchip,rk3568-grf", GRF);
	device("pmugrf", "rockchip,rk3568-pmugrf", 0xfdc20000);
	tree.phy = device("phy", "rockchip,rk3568-dsi-dphy", 0xfe850000);
	fdt_add_prop_u32(tree.phy, "phandle", 11);
	tree.host = device("dsi", "rockchip,rk3568-mipi-dsi", DSI);
	fdt_add_prop_u32(tree.host, "phys", 11);
	connect_host(tree.host);
	tree.panel = fdt_add_node(tree.host, "panel@0");
	fdt_add_prop_u32(tree.panel, "reg", 0);
	tree.entry = fdt_add_node(tree.panel, "pie,panel-test");
	static const uint8_t id[] = { 0x12, 0x34 }, init[] = { 0x05, 1, 0x20 };
	fdt_add_prop(tree.entry, "pie,id", id, sizeof(id));
	fdt_add_prop(tree.entry, "pie,init", init, sizeof(init));
	CELLS(tree.entry, "pie,modes", 25175, 640, 656, 752, 800, 480, 490, 492, 525);
	fdt_add_prop_u32(tree.entry, "pie,dsi-lanes", 4);
	fdt_add_prop(tree.entry, "pie,dsi-format", "rgb888", 7);
	static const char flags[] = "video\0burst\0lp-commands";
	fdt_add_prop(tree.entry, "pie,dsi-flags", flags, sizeof(flags));
	for (unsigned b = 0; b < 5; b++) {
		char name[16];
		snprintf(name, sizeof(name), "gpio%u", b);
		struct fdt_node *gpio = device(name, "", gpio_banks[b].base);
		fdt_add_prop(gpio, "gpio-controller", "", 0);
		fdt_add_prop_u32(gpio, "#gpio-cells", 2);
		fdt_add_prop_u32(gpio, "phandle", 100 + b);
	}
	CELLS(tree.panel, "reset-gpios", 101, 1, 1);
	struct fdt_node *power = device("power", "regulator-fixed", 0);
	fdt_add_prop_u32(power, "phandle", 23);
	fdt_add_prop(power, "enable-active-high", "", 0);
	CELLS(power, "gpio", 102, 2, 0);
	fdt_add_prop_u32(tree.panel, "vdd-supply", 23);
	struct fdt_node *pwm = device("pwm", "rockchip,rk3328-pwm", PWM1_BASE);
	fdt_add_prop_u32(pwm, "phandle", 20);
	fdt_add_prop_u32(pwm, "pinctrl-0", 21);
	struct fdt_node *pins = fdt_add_node(tree.fdt.root, "pwm-pins");
	fdt_add_prop_u32(pins, "phandle", 21);
	CELLS(pins, "rockchip,pins", 0, 4, 1, 0);
	struct fdt_node *bl = device("backlight", "pwm-backlight", 0);
	fdt_add_prop_u32(bl, "phandle", 22);
	CELLS(bl, "pwms", 20, 0, 20000, 0);
	CELLS(bl, "enable-gpios", 104, 3, 0);
	fdt_add_prop_u32(tree.panel, "backlight", 22);
	seed(CRU + CRU_GATE(20), 0xffff);
	seed(CRU + CRU_GATE(21), 0xffff);
	seed(CRU + CRU_GATE(31), 0xffff);
	seed(CRU + CRU_GATE(33), 0xffff);
	seed(CRU + CRU_SOFTRST(16), 0xa5bf);
	seed(CRU + CRU_SOFTRST(17), 0x5a01);
	seed(CRU + CRU_SOFTRST(23), 0x5a03);
	seed(CRU + CRU_SOFTRST(27), 0x1800);
	seed(PMUCRU + PMUCRU_GATE(1), 0xffff);
	seed(PMUCRU + PMUCRU_GATE(2), 0xffff);
	seed(PMUCRU + PMUCRU_CLKSEL(8), 0xa5f8);
	seed(GRF + GRF_VO_CON0, 0xa5a7);
	seed(GRF + GRF_VO_CON2, 0x5af7);
	seed(CRU + CRU_VPLL_CON(0), 0x307d);
	seed(CRU + CRU_VPLL_CON(1), 0x1442);
	seed(CRU + CRU_MODE_CON00, 0x5a5a);
	seed(CRU + CRU_CLKSEL(40), 0x0c5a);
	seed(CRU + CRU_CLKSEL(72), 0xa7ff);
	seed(PWM1_BASE + PWM_CTRL, PWM_CTRL_CONTINUOUS | PWM_CTRL_DUTY_POSITIVE);
	seed(PWM1_BASE + PWM_PERIOD, 0x123);
	seed(PWM1_BASE + PWM_DUTY, 0x45);
	seed(VOP + VOP2_VP(1) + VP_DSP_CTRL, DSP_CTRL_STANDBY | 0x104);
	seed(VOP + VOP2_DSP_INFACE_EN, 0x12340010);
	seed(VOP + VOP2_OVL_LAYER_SEL, 0x54763210);
	seed(VOP + VOP2_OVL_PORT_SEL, 0x84000743);
	seed(VOP + VOP2_AUTO_GATING, AUTO_GATING_EN | 0x102);
	seed(VOP + VOP2_REG_CFG_DONE, 0x8000);
}

static bool
shared_restored(void)
{
	bool pass = true;
	for (unsigned i = 0; i < sim.nregs; i++) {
		uint64_t addr = sim.regs[i].addr;
		uint32_t want = sim.regs[i].initial;
		bool shared = (addr >= CRU && addr < CRU + 0x500) || (addr >= PMUCRU && addr < PMUCRU + 0x210) ||
		    (addr >= PMU && addr < PMU + 0x100) || addr == GRF + GRF_VO_CON0 || addr == GRF + GRF_VO_CON2 ||
		    addr == PWM1_BASE + PWM_CTRL || addr == PWM1_BASE + PWM_PERIOD || addr == PWM1_BASE + PWM_DUTY ||
		    (addr >= VOP && addr < VOP + 0x3e00 && addr != VOP + VOP2_REG_CFG_DONE &&
		    addr != VOP + VOP2_STATUS(1) && addr != VOP + VOP2_PORT1_INTR_CLR);
		if (sim.pins_changed && addr == VOP + VOP2_VP(1) + VP_DSP_CTRL) {
			want |= DSP_CTRL_STANDBY;
		} else if (sim.pins_changed && addr == VOP + VOP2_DSP_INFACE_EN) {
			want &= ~INFACE_EN_MIPI0;
		}
		if (shared && sim.regs[i].val != want) {
			printf("register 0x%llx: 0x%x, want 0x%x\n", (unsigned long long)addr, sim.regs[i].val, want);
			pass = false;
		}
	}
	return pass;
}

static struct fdt_node *
copy_entry(const char *name)
{
	struct fdt_node *entry = fdt_add_node(tree.panel, name);
	for (struct fdt_prop *p = tree.entry->props; p; p = p->next) {
		fdt_add_prop(entry, p->name, p->val, p->len);
	}
	return entry;
}

static void
running_panel(void)
{
	seed(CRU + CRU_GATE(20), 0xffff & ~GATE20_VO);
	seed(CRU + CRU_GATE(21), 0xffff & ~GATE21_DSITX0);
	seed(CRU + CRU_SOFTRST(16), 0xa5bf & ~SOFTRST16_VO);
	seed(CRU + CRU_SOFTRST(17), 0x5a01 & ~SOFTRST17_DSITX0);
	seed(VOP + VOP2_VP(1) + VP_DSP_CTRL, 0x104);
	seed(DSI + DSI_PWR_UP, 1);
	seed(DSI + DSI_PHY_RSTZ, 0xf);
	seed(gpio_banks[0].base + GPIO_SWPORT_DR(4), 1u << 4);
	seed(gpio_banks[1].base + GPIO_SWPORT_DR(1), 1u << 1);
	seed(gpio_banks[2].base + GPIO_SWPORT_DR(2), 1u << 2);
	seed(gpio_banks[4].base + GPIO_SWPORT_DR(3), 1u << 3);
}

static bool
run_panel(const char *opts, struct place_range ram, uint64_t *base, uint64_t *size)
{
	struct place_range busy[] = { { 0, ram.base } };
	return rk_panel(&tree.fdt, opts, false, ram, busy, 1, base, size);
}

static void
discovery_cases(void)
{
	setup();
	report("DSI0 and D-PHY0 are accepted", rk_panel_board(&tree.fdt));
	CELLS(tree.host, "reg", 0, 0xfe070000, 0x10000);
	report("DSI1 is refused with a message", !rk_panel_board(&tree.fdt) && strstr(messages, "not DSI0") && !sim.writes);
	setup();
	CELLS(tree.phy, "reg", 0, 0xfe860000, 0x10000);
	report("D-PHY1 is refused with a message", !rk_panel_board(&tree.fdt) && strstr(messages, "not D-PHY0") && !sim.writes);
	setup();
	struct fdt_node *other = device("dsi1", "rockchip,rk3568-mipi-dsi", 0xfe070000);
	connect_host(other);
	report("DSI1 does not hide DSI0", rk_panel_board(&tree.fdt));
	static const struct { const char *status; unsigned len; bool enabled; } cases[] = {
		{ NULL, 0, true }, { "okay", 5, true }, { "ok", 3, true }, { "disabled", 9, false },
		{ "reserved", 9, false }, { "fail", 5, false }, { "", 0, false }, { "okay", 4, false },
		{ "okay\0disabled", 14, false },
	};
	for (unsigned target = 0; target < 2; target++) {
		for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
			setup();
			if (cases[i].status) {
				fdt_add_prop(target ? tree.entry : tree.panel, "status", cases[i].status, cases[i].len);
			}
			struct panel_entry entries[MAX_PANELS];
			bool accepted = target ? panel_entries(tree.panel, entries, MAX_PANELS) == 1 : rk_panel_board(&tree.fdt);
			char name[80];
			snprintf(name, sizeof(name), "%s status %s, %u bytes", target ? "panel data" : "panel",
			    cases[i].status ? cases[i].status : "absent", cases[i].len);
			report(name, accepted == cases[i].enabled && !sim.writes);
		}
	}
	setup();
	struct fdt_node *disabled = fdt_add_node(tree.panel, "pie,panel-disabled");
	fdt_add_prop(disabled, "status", "disabled", 9);
	struct panel_entry entry;
	report("disabled panel data does not consume a slot", panel_entries(tree.panel, &entry, 1) == 1 &&
	    !strcmp(entry.name, "pie,panel-test") && !messages_used);
}

static void
entry_cases(struct place_range ram)
{
	static const struct { const char *name, *reason; uint32_t clock; } cases[] = {
		{ "a mode above the lane bandwidth", "the link cannot drive the mode", 60000 },
		{ "a timing above the register limit", "the link cannot drive the mode", 1000 },
		{ "a pixel clock with no exact VPLL", "no VPLL setting gives the pixel clock", 24151 },
	};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		setup();
		running_panel();
		CELLS(tree.entry, "pie,modes", cases[i].clock, 640, 656, 752, 800, 480, 490, 492, 525);
		uint64_t base = 0x55, size = 0x66;
		bool ok = run_panel(NULL, ram, &base, &size);
		report(cases[i].name, !ok && base == 0x55 && size == 0x66 && !sim.reads && !sim.writes &&
		    strstr(messages, cases[i].reason) && strstr(messages, "left out") && strstr(messages, "no usable"));
		rk_panel_off(&tree.fdt, NULL, true);
		report("HDMI leaves an unusable mode as found", !sim.reads && !sim.writes);
	}
	setup();
	struct fdt_node *bad = copy_entry("pie,panel-bad");
	CELLS(bad, "pie,modes", 60000, 640, 656, 752, 800, 480, 490, 492, 525);
	struct panel_entry entry;
	report("an unusable mode does not consume a slot", panel_entries(tree.panel, &entry, 1) == 1 &&
	    !strcmp(entry.name, tree.entry->name) && strstr(messages, "pie,panel-bad") &&
	    !strstr(messages, "limit of") && !sim.reads && !sim.writes);

	static const char *const names[] = { "pie,panel-0", "pie,panel-1", "pie,panel-2", "pie,panel-3" };
	setup();
	for (unsigned i = 0; i < MAX_PANELS; i++) {
		copy_entry(names[i]);
	}
	struct { struct panel_entry entries[MAX_PANELS]; uint32_t guard; } result = { .guard = 0x12345678 };
	unsigned count = panel_entries(tree.panel, result.entries, MAX_PANELS);
	bool order = count == MAX_PANELS;
	for (unsigned i = 0; i < count; i++) {
		order = order && !strcmp(result.entries[i].name, names[MAX_PANELS - 1 - i]);
	}
	report("extra usable nodes are named without exceeding the limit", order && result.guard == 0x12345678 &&
	    strstr(messages, "pie,panel-test: limit of 4 usable panel nodes; left out") && !sim.reads && !sim.writes);
	fdt_add_prop(tree.entry, "status", "disabled", 9);
	messages_used = 0;
	messages[0] = 0;
	report("disabled nodes beyond the limit need no limit message", panel_entries(tree.panel, result.entries,
	    MAX_PANELS) == MAX_PANELS && !messages_used && result.guard == 0x12345678 && !sim.reads && !sim.writes);
}

static void
off_cases(void)
{
	static const char *const opts[] = { "off", "off,pattern", "pattern,off", "dsibars,off,pattern" };
	for (unsigned i = 0; i < sizeof(opts) / sizeof(opts[0]); i++) {
		setup();
		running_panel();
		rk_panel_off(&tree.fdt, opts[i], true);
		char name[96];
		snprintf(name, sizeof(name), "HDMI leaves hardware as found with %s", opts[i]);
		report(name, !sim.reads && !sim.writes && strstr(messages, "off, as pieboot.panel asks"));
	}
	for (unsigned i = 0; i < 4; i++) {
		setup();
		running_panel();
		if (i == 0) {
			tree.panel->child = NULL;
		} else if (i == 1) {
			fdt_add_prop(tree.entry, "status", "disabled", 9);
		} else if (i == 2) {
			static const uint8_t id[] = { 0x12, 0x34, 0x56, 0x78 };
			struct fdt_prop *p = fdt_prop(tree.entry, "pie,id");
			p->val = id;
			p->len = sizeof(id);
		} else {
			static const char hs[] = "video\0burst";
			struct fdt_prop *p = fdt_prop(tree.entry, "pie,dsi-flags");
			p->val = (const uint8_t *)hs;
			p->len = sizeof(hs);
		}
		rk_panel_off(&tree.fdt, NULL, true);
		report("HDMI leaves hardware as found without usable panel data", !sim.reads && !sim.writes &&
		    (i < 2 || strstr(messages, i == 2 ? "pie,id is not 1 to 3 bytes" : "lacks lp-commands")));
	}
	static const uint32_t gates[] = { 0xffff, 0xa5ac, 0 }, pmugates[] = { 0xffff, 0x55ad, 0 };
	for (unsigned clocks = 0; clocks < 3; clocks++) {
		for (unsigned state = 0; state < 3; state++) {
			setup();
			running_panel();
			seed(CRU + CRU_GATE(31), gates[clocks]);
			seed(PMUCRU + PMUCRU_GATE(1), pmugates[clocks]);
			if (state == 1) {
				seed(CRU + CRU_GATE(21), 0xffff);
			} else if (state == 2) {
				seed(PMU + PMU_PWR_DWN_ST, PWR_PD_VO);
			}
			rk_panel_off(&tree.fdt, clocks == 0 ? NULL : clocks == 1 ? "pattern" : "dsibars", true);
			char name[96];
			snprintf(name, sizeof(name), "HDMI darkens the panel and restores GPIO clocks, gates %u, state %u", clocks, state);
			report(name, sim.pins_changed && !sim.bad_gpio && !sim.backlight &&
			    !(value(gpio_banks[0].base + GPIO_SWPORT_DR(4)) & (1u << 4)) &&
			    !(value(gpio_banks[4].base + GPIO_SWPORT_DR(3)) & (1u << 3)) &&
			    !(value(gpio_banks[1].base + GPIO_SWPORT_DR(1)) & (1u << 1)) &&
			    !(value(gpio_banks[2].base + GPIO_SWPORT_DR(2)) & (1u << 2)) &&
			    value(DSI + DSI_PWR_UP) == (state ? 1u : 0) &&
			    value(DSI + DSI_PHY_RSTZ) == (state ? 0xfu : 0) &&
			    value(VOP + VOP2_VP(1) + VP_DSP_CTRL) == (0x104 | (state == 2 ? 0 : DSP_CTRL_STANDBY)) &&
			    value(VOP + VOP2_DSP_INFACE_EN) == (0x12340010u & (state == 2 ? ~0u : ~INFACE_EN_MIPI0)) &&
			    value(CRU + CRU_GATE(31)) == gates[clocks] && value(PMUCRU + PMUCRU_GATE(1)) == pmugates[clocks]);
		}
	}
}

static void
placement_cases(struct place_range ram)
{
	for (unsigned i = 0; i < 2; i++) {
		setup();
		uint64_t base = 0x55, size = 0x66;
		bool ok = run_panel(i ? "pattern" : NULL, (struct place_range){ 0x70000000, 0x7012c000 }, &base, &size);
		report(i ? "pattern refuses the whole last RAM range" : "console refuses the whole last RAM range",
		    !ok && base == 0x55 && size == 0x66 && !sim.writes && strstr(messages, "no room"));
	}
	setup();
	uint64_t base = 0, size = 0;
	ram.end = ram.base + FB_SIZE + 0x4000;
	bool ok = run_panel("pattern", ram, &base, &size);
	report("one RAM page below the framebuffer permits the top cut", ok && base == ram.base + 0x4000 &&
	    size == FB_SIZE && base + size == ram.end && base > ram.base);
	setup();
	ok = run_panel(NULL, (struct place_range){ ram.base, ram.base + FB_SIZE - 0x4000 }, &base, &size);
	report("a framebuffer larger than the last RAM range is refused", !ok && !sim.writes && strstr(messages, "no room"));
	setup();
	ok = run_panel(NULL, (struct place_range){ ram.base, ram.end - 1 }, &base, &size);
	report("an unaligned framebuffer end is refused", !ok && !sim.writes && strstr(messages, "no room"));
	setup();
	struct place_range busy[] = { { ram.end - FB_SIZE, ram.end - FB_SIZE + 1 } };
	ok = rk_panel(&tree.fdt, "pattern", false, ram, busy, 1, &base, &size);
	report("a busy framebuffer is refused", !ok && !sim.writes && strstr(messages, "no room"));
}

static void
failure_case(const char *name, const char *opts, const char *reason, struct place_range ram)
{
	uint64_t base = 0x55, size = 0x66;
	bool ok = run_panel(opts, ram, &base, &size);
	bool dark = !sim.pins_changed || (!(value(gpio_banks[0].base + GPIO_SWPORT_DR(4)) & (1u << 4)) &&
	    !(value(gpio_banks[4].base + GPIO_SWPORT_DR(3)) & (1u << 3)) && !value(DSI + DSI_PWR_UP) &&
	    !value(DSI + DSI_PHY_RSTZ));
	report(name, !ok && base == 0x55 && size == 0x66 && !sim.backlight && !sim.bad_gpio && shared_restored() &&
	    dark && !fdt_find_compatible(tree.fdt.root, "simple-framebuffer") && strstr(messages, reason));
}

static void
set_backlight_bank(bool enable, unsigned bank)
{
	if (enable) {
		CELLS(fdt_find_phandle(tree.fdt.root, 22), "enable-gpios", 100 + bank, 3, 0);
	} else {
		CELLS(fdt_find_phandle(tree.fdt.root, 21), "rockchip,pins", bank, 4, 1, 0);
	}
}

static void
backlight_cases(struct place_range ram)
{
	for (unsigned enable = 0; enable < 2; enable++) {
		for (unsigned bank = 0; bank < 5; bank++) {
			char name[96];
			for (unsigned gate = 0; gate < 2; gate++) {
				setup();
				set_backlight_bank(enable, bank);
				if (gate) {
					sim.fail_gate = 1u << bank;
				} else {
					seed(gpio_model[bank].reset, 1u << gpio_model[bank].reset_bit);
				}
				uint64_t base = 0x55, size = 0x66;
				bool ok = run_panel(NULL, ram, &base, &size);
				snprintf(name, sizeof(name), "%s GPIO%u %s is refused before panel reset", enable ? "enable" : "PWM",
				    bank, gate ? "with a stuck gate" : bank == 4 ? "with SOFTRST22 bit 0 set" : "in reset");
				report(name, !ok && base == 0x55 && size == 0x66 && !sim.pins_changed && !sim.backlight &&
				    !sim.start && !sim.bad_gpio && shared_restored() &&
				    !fdt_find_compatible(tree.fdt.root, "simple-framebuffer") &&
				    strstr(messages, "backlight's GPIO bank is gated or in reset"));
			}
			setup();
			set_backlight_bank(enable, bank);
			uint64_t base = 0, size = 0;
			bool ok = run_panel(NULL, ram, &base, &size);
			snprintf(name, sizeof(name), "%s GPIO%u is ungated for bring-up", enable ? "enable" : "PWM", bank);
			report(name, ok && base == ram.end - FB_SIZE && size == FB_SIZE && sim.backlight &&
			    !sim.bad_gpio && !(value(gpio_model[bank].gate) & (1u << gpio_model[bank].gate_bit)));
		}
	}
	setup();
	sim.reset_after_start = 1u << 4;
	uint64_t base = 0x55, size = 0x66;
	bool ok = run_panel(NULL, ram, &base, &size);
	report("backlight enable failure restores PWM, clocks and VOP without publishing",
	    !ok && base == 0x55 && size == 0x66 && sim.backlight && sim.stop > sim.start &&
	    !sim.bad_gpio && shared_restored() && !fdt_find_compatible(tree.fdt.root, "simple-framebuffer") &&
	    sim.gpio_writes[0] == 4 && !(value(gpio_banks[0].base + GPIO_SWPORT_DR(4)) & (1u << 4)) &&
	    !value(DSI + DSI_PWR_UP) && !value(DSI + DSI_PHY_RSTZ) &&
	    strstr(messages, "cannot turn the backlight on") && strstr(messages, "cannot force the backlight off"));
	setup();
	sim.reset_after_start = 1u << 0;
	sim.stalled = true;
	failure_case("PWM GPIO failure during cleanup still restores shared state", NULL, "does not advance", ram);
	report("failure to force the PWM pin off is reported", sim.gpio_writes[4] == 4 &&
	    strstr(messages, "cannot force the backlight off"));
}

static void
panel_bank_cases(struct place_range ram)
{
	for (unsigned power = 0; power < 2; power++) {
		for (unsigned bank = 0; bank < 5; bank++) {
			for (unsigned gate = 0; gate < 2; gate++) {
				setup();
				running_panel();
				set_backlight_bank(true, (bank + 1) % 5);
				set_backlight_bank(false, (bank + 1) % 5);
				if (power) {
					CELLS(fdt_find_phandle(tree.fdt.root, 23), "gpio", 100 + bank, 2, 0);
				} else {
					CELLS(tree.panel, "reset-gpios", 100 + bank, 1, 1);
				}
				if (gate) {
					sim.fail_gate = 1u << bank;
				} else {
					seed(gpio_model[bank].reset, 1u << gpio_model[bank].reset_bit);
				}
				uint64_t base = 0x55, size = 0x66;
				bool ok = run_panel(NULL, ram, &base, &size);
				char name[96];
				snprintf(name, sizeof(name), "%s GPIO%u %s leaves the running panel as found", power ? "power" : "reset",
				    bank, gate ? "with a stuck gate" : "in reset");
				report(name, !ok && base == 0x55 && size == 0x66 && !sim.pins_changed && !sim.start &&
				    !sim.backlight && !sim.bad_gpio && shared_restored() && value(DSI + DSI_PWR_UP) == 1 &&
				    value(DSI + DSI_PHY_RSTZ) == 0xf && !fdt_find_compatible(tree.fdt.root, "simple-framebuffer") &&
				    strstr(messages, "the panel's GPIO bank is gated or in reset"));
			}
		}
	}
	setup();
	struct fdt_node *power = fdt_find_phandle(tree.fdt.root, 23);
	struct fdt_prop **p = &power->props;
	while (*p && strcmp((*p)->name, "gpio")) {
		p = &(*p)->next;
	}
	*p = (*p)->next;
	uint64_t base = 0, size = 0;
	bool ok = run_panel(NULL, ram, &base, &size);
	report("a supply without a GPIO needs no bank check", ok && base == ram.end - FB_SIZE && size == FB_SIZE &&
	    sim.backlight && !sim.bad_gpio && !sim.gpio_writes[2]);
}

static void
rollback_cases(struct place_range ram)
{
	setup();
	sim.fail_power = true;
	seed(PMU + PMU_PWR_DWN_ST, PWR_PD_VO);
	seed(PMU + PMU_PWR_GATE_SFTCON, PWR_PD_VO);
	seed(PMU + PMU_BUS_IDLE_SFTCON0, BUS_IDLE_VO);
	seed(PMU + PMU_BUS_IDLE_ACK, BUS_IDLE_VO);
	seed(PMU + PMU_BUS_IDLE_ST, BUS_IDLE_VO);
	failure_case("power failure restores shared state", NULL, "does not power up", ram);
	setup();
	sim.fail_idle = true;
	seed(PMU + PMU_BUS_IDLE_SFTCON0, BUS_IDLE_VO);
	seed(PMU + PMU_BUS_IDLE_ACK, BUS_IDLE_VO);
	seed(PMU + PMU_BUS_IDLE_ST, BUS_IDLE_VO);
	failure_case("bus failure restores gates and resets", NULL, "bus stays idle", ram);
	setup();
	seed(CRU + CRU_SOFTRST(21), 1u << 10);
	failure_case("GPIO failure restores GPIO bank gates", NULL, "GPIO bank is gated or in reset", ram);
	setup();
	sim.fail_phy = true;
	failure_case("PHY failure restores routing and reference selection", NULL, "link does not come up", ram);
	setup();
	sim.no_id = true;
	failure_case("ID failure restores routing and GPIO bank gates", NULL, "does not answer", ram);
	setup();
	sim.no_id = true;
	seed(CRU + CRU_GATE(31), 0xa5ac);
	seed(PMUCRU + PMUCRU_GATE(1), 0x55ad);
	seed(gpio_banks[2].base + GPIO_EXT_PORT, 1u << 2);
	seed(gpio_banks[2].base + GPIO_SWPORT_DR(2), 1u << 2);
	failure_case("ID failure preserves mixed GPIO gates and existing panel power", NULL, "does not answer", ram);
	report("existing panel power stays on", (value(gpio_banks[2].base + GPIO_SWPORT_DR(2)) & (1u << 2)) != 0);
	setup();
	sim.fail_init = true;
	failure_case("init failure restores shared state", NULL, "does not send the init list", ram);
	setup();
	sim.fail_pll = true;
	failure_case("VPLL lock failure restores the original PLL", NULL, "VPLL does not lock", ram);
	setup();
	seed(VOP + VOP2_OVL_PORT_SEL, 0xaa0a0003);
	failure_case("overlay failure restores VPLL and DCLK_VOP1", NULL, "cannot take smart0", ram);
	for (unsigned i = 0; i < 2; i++) {
		setup();
		sim.stalled = sim.noise = true;
		failure_case(i ? "stalled pattern restores VOP and shared state" : "stalled console restores VOP and shared state",
		    i ? "pattern" : NULL, "does not advance", ram);
		report("stalled scanout has a bounded wait", sim.stop > sim.start && sim.stop - sim.start < 73000);
	}
	setup();
	sim.stalled = true;
	seed(VOP + VOP2_VP(1) + VP_DSP_CTRL, 0x104);
	seed(VOP + VOP2_OVL_PORT_SEL, 0x55050558);
	for (unsigned w = 0; w < 8; w++) {
		if (!windows[w].base) {
			continue;
		}
		uint64_t win = VOP + windows[w].base;
		if (w == VOP2_CLUSTER0 || w == VOP2_CLUSTER1) {
			seed(win + CLUSTER_CTRL, 0x103);
			seed(win + CLUSTER_WIN_CTRL0(0), 0x21);
			seed(win + CLUSTER_WIN_CTRL0(1), 0x43);
		} else {
			for (unsigned r = 0; r < 4; r++) {
				seed(win + WIN_REGION_MST_CTL(r), 0x21 + 4 * r);
			}
		}
	}
	failure_case("stalled scanout restores all window settings and keeps VP1 in standby", NULL, "does not advance", ram);
	setup();
	sim.delay = 90000;
	failure_case("progress after the timeout is refused", NULL, "does not advance", ram);
}

static void
progress_cases(struct place_range ram)
{
	for (unsigned i = 0; i < 2; i++) {
		setup();
		sim.delay = 40000;
		uint64_t base = 0, size = 0;
		bool ok = run_panel(i ? "pattern" : NULL, ram, &base, &size);
		bool published = fdt_find_compatible(tree.fdt.root, "simple-framebuffer") != NULL;
		report(i ? "pattern waits for progress and keeps its buffer" : "console waits for progress before publication",
		    ok && base == ram.end - FB_SIZE && size == FB_SIZE && published == !i &&
		    sim.progress && sim.backlight >= sim.progress && sim.backlight - sim.start >= sim.delay);
	}
	setup();
	sim.stalled = true;
	uint64_t base = 0x55, size = 0x66;
	bool ok = run_panel("dsibars", ram, &base, &size);
	report("dsibars keeps the backlight on without VP1 progress", !ok && base == 0x55 && size == 0x66 &&
	    sim.backlight && !fdt_find_compatible(tree.fdt.root, "simple-framebuffer") && strstr(messages, "colour bars"));
}

int
main(void)
{
	void *pixels = mmap((void *)0x70000000, 0x400000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pixels == MAP_FAILED || (uintptr_t)pixels + 0x400000 > 0x100000000ull) {
		fprintf(stderr, "rkdsi-test: cannot map a framebuffer below 4 GB\n");
		return EXIT_FAILURE;
	}
	struct place_range ram = { (uintptr_t)pixels, (uintptr_t)pixels + 0x380000 };
	discovery_cases();
	entry_cases(ram);
	off_cases();
	placement_cases(ram);
	backlight_cases(ram);
	panel_bank_cases(ram);
	rollback_cases(ram);
	progress_cases(ram);
	munmap(pixels, 0x400000);
	printf("rkdsi-test: %s\n", failed ? "FAIL" : "PASS");
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
