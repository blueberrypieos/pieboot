/* SPDX-License-Identifier: BSD-2-Clause */
#include "rkdisplay.h"
#include "dwhdmi.h"
#include "vop2ovl.h"

#define CRU_CLKSEL(n)                   (0x0100 + 4 * (n))
#define  CLKSEL35_CLK_ISP               (3u << 6)
#define  CLKSEL35_CLK_ISP_HPLL          (2u << 6)
#define  CLKSEL38_ACLK_VOP_PRE          (3u << 6)
#define  CLKSEL38_ACLK_VOP_PRE_HPLL     (2u << 6)
#define  DCLK_VOP_SEL                   (3u << 10)
#define  DCLK_VOP_SEL_HPLL              (0u << 10)
#define  DCLK_VOP_DIV                   0xffu
#define CRU_GATE(n)                     (0x0300 + 4 * (n))
#define  GATE19_CLK_ISP                 (1u << 2)
#define  GATE20_VO                      0x03feu
#define  GATE20_DCLK_VOP(n)             (1u << (10 + (n)))
#define  GATE21_HDMI                    ((1u << 4) | (1u << 3))
#define CRU_SOFTRST(n)                  (0x0400 + 4 * (n))
#define  SOFTRST16_HDMI                 ((1u << 15) | (1u << 14))
#define  SOFTRST16_VO                   0x007fu

#define PMUCRU_HPLL_CON(n)              (0x0040 + 4 * (n))
#define  HPLL_CON0_BYPASS               (1u << 15)
#define  HPLL_CON0_POSTDIV1             (7u << 12)
#define  HPLL_CON0_FBDIV                0xfffu
#define  HPLL_CON0_MASK                 (HPLL_CON0_BYPASS | HPLL_CON0_POSTDIV1 | HPLL_CON0_FBDIV)
#define  HPLL_CON1_PD                   ((1u << 14) | (1u << 13))
#define  HPLL_CON1_DSMPD                (1u << 12)
#define  HPLL_CON1_LOCK                 (1u << 10)
#define  HPLL_CON1_POSTDIV2             (7u << 6)
#define  HPLL_CON1_REFDIV               0x3fu
#define  HPLL_CON1_MASK                 (HPLL_CON1_PD | HPLL_CON1_DSMPD | HPLL_CON1_POSTDIV2 | HPLL_CON1_REFDIV)
#define PMUCRU_MODE_CON00               0x0080
#define  MODE_HPLL                      (3u << 2)
#define  MODE_HPLL_XIN24M               (0u << 2)
#define  MODE_HPLL_PLL                  (1u << 2)
#define PMUCRU_CLKSEL(n)                (0x0100 + 4 * (n))
#define  CLKSEL8_HDMIPHY_REF_HPLL_PH0   (1u << 7)

#define GRF_VO_CON0                     0x0360
#define  VO_CON0_HDMIPHY_I2C            (1u << 15)

#define PMU_BUS_IDLE_SFTCON0            0x0050
#define PMU_BUS_IDLE_ACK                0x0060
#define PMU_BUS_IDLE_ST                 0x0068
#define  BUS_IDLE_VO                    (1u << 4)
#define PMU_PWR_DWN_ST                  0x0098
#define PMU_PWR_GATE_SFTCON             0x00a0
#define  PWR_PD_VO                      (1u << 7)
#define PMU_PLLPD_SFTCON                0x00c4

#define VOP2_REG_CFG_DONE               0x0000
#define  CFG_DONE_VP0                   ((1u << 3) | (1u << 0))
#define  CFG_DONE_VP1                   ((1u << 4) | (1u << 1))
#define  CFG_DONE_GLOBAL_EN             (1u << 15)
#define VOP2_VERSION_INFO               0x0004
#define VOP2_AUTO_GATING                0x0008
#define  AUTO_GATING_EN                 (1u << 31)
#define VOP2_AXI_CTRL0                  0x0010
#define VOP2_DSP_INFACE_EN              0x0028
#define  INFACE_EN_HDMI_MUX             (3u << 10)
#define  INFACE_EN_HDMI                 (1u << 1)
#define VOP2_DSP_INFACE_CTRL            0x002c
#define  INFACE_CTRL_REGDONE_IMD        (1u << 28)
#define VOP2_DSP_INFACE_POL             0x0030
#define  INFACE_POL_REGDONE_IMD         (1u << 28)
#define  INFACE_POL_HDMI                (0xfu << 4)
#define  INFACE_POL_HDMI_DCLK           (1u << 7)
#define  INFACE_POL_HDMI_VSYNC          (1u << 5)
#define  INFACE_POL_HDMI_HSYNC          (1u << 4)
#define VOP2_OTP_WIN_EN                 0x0050
#define VOP2_STATUS(n)                  (0x0060 + 4 * (n))
#define VOP2_SYS0_INTR_EN               0x0080
#define VOP2_SYS0_INTR_CLR              0x0084
#define VOP2_SYS0_INTR_RAW              0x008c
#define  SYS0_INTR_ALL                  0x00beu
#define VOP2_SYS1_INTR_CLR              0x0094
#define VOP2_SYS1_INTR_RAW              0x009c
#define  SYS1_INTR_ALL                  0x0086u
#define VOP2_PORT_INTR_CLR(n)           (0x00a4 + 0x10 * (n))
#define VOP2_PORT_INTR_RAW(n)           (0x00ac + 0x10 * (n))
#define  PORT_INTR_ALL                  0x02fdu
#define VOP2_OVL_CTRL                   0x0600
#define  OVL_CTRL_REGDONE_IMD           (1u << 28)
#define  OVL_CTRL_VP0_YUV               (1u << 0)
#define VOP2_OVL_LAYER_SEL              0x0604
#define VOP2_OVL_PORT_SEL               0x0608
#define VOP2_OVL_DP0_BG_MIX             0x06e0
#define VOP2_OVL_BG_MIX(n)              (0x06e0 + 4 * (n))
#define  DP_BG_DLY                      38u
#define  DP_BG_DLY_SDR                  (DP_BG_DLY << 24)
#define VOP2_VP(n)                      (0x0c00 + 0x100 * (n))
#define  VP_DSP_CTRL                    0x00
#define   DSP_CTRL_STANDBY              (1u << 31)
#define   DSP_CTRL_WIN_BYPASS           (1u << 15)
#define   DSP_CTRL_OUT_RGB30            15u
#define   DSP_CTRL_OUT_P888             0u
#define   DSP_CTRL_OUT_MODE             0xfu
#define  VP_COLOR_CTRL                  0x08
#define   COLOR_BAR_EN                  (1u << 0)
#define  VP_3D_LUT_CTRL                 0x10
#define  VP_DSP_BG                      0x2c
#define  VP_PRE_SCAN_HTIMING            0x30
#define  VP_POST_HACT_INFO              0x34
#define  VP_POST_VACT_INFO              0x38
#define  VP_POST_SCL_FACTOR             0x3c
#define   POST_SCL_FACTOR_1             0x10001000u
#define  VP_POST_SCL_CTRL               0x40
#define  VP_HTOTAL_HS_END               0x48
#define  VP_HACT_ST_END                 0x4c
#define  VP_VTOTAL_VS_END               0x50
#define  VP_VACT_ST_END                 0x54
#define  VP_BCSH_CTRL                   0x60
#define VOP2_MMU(n)                     (0x3e00 + 0x100 * (n))
#define  MMU_STATUS                     0x04
#define   MMU_PAGING                    (1u << 0)

#define WIN_CTRL0                       0x00
#define WIN_CTRL1                       0x04
#define  CTRL1_YMIR                     (1u << 31)
#define WIN_REGION_MST_CTL(r)           (0x10 + 0x30 * (r))
#define  MST_CTL_ARGB8888               (0u << 1)
#define  MST_CTL_EN                     (1u << 0)
#define WIN_REGION0_MST_YRGB            0x14
#define WIN_REGION0_VIR                 0x1c
#define WIN_REGION0_ACT_INFO            0x20
#define WIN_REGION0_DSP_INFO            0x24
#define WIN_REGION0_DSP_OFFSET          0x28
#define WIN_REGION0_SCL_CTRL            0x30
#define CLUSTER_WIN_CTRL0(n)            (0x80 * (n))
#define  CLUSTER_WIN_EN                 (1u << 0)
#define CLUSTER_CTRL                    0x100
#define  CLUSTER_EN                     (1u << 0)

#define TIMEOUT_US                      10000
#define HPD_WAIT_US                     20000
#define LOG_MAX                         128

#define ISR_SERROR                      (1u << 8)

#define PROBE_LOW_BUF                   0x10000000ULL
#define STOCK_LAYER_SEL                 0x54763210u
#define STOCK_PORT_SEL                  0x94000742u
#define BG_RED(v)                       ((uint32_t)(v) << 20)
#define BG_GREEN(v)                     ((uint32_t)(v) << 10)
#define BG_BLUE(v)                      ((uint32_t)(v))

struct rk_mode {
	const char *name;
	struct dwhdmi_mode m;
	uint8_t refdiv, postdiv1, postdiv2;
	uint16_t fbdiv;
};

static const struct rk_mode rk_modes[] = {
	{ "640x480", { 25175, 640, 656, 752, 800, 480, 490, 492, 525, false, false, false, 1, 1 }, 5, 7, 7, 257 },
	{ "1280x720", { 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, true, true, false, 4, 2 }, 2, 4, 4, 99 },
};

struct rk_regs {
	uint64_t hdmi, vop, cru, pmucru, pmu, grf;
	bool rk3566;
};

struct rk_state {
	uint32_t gate20, gate21, softrst16, idle_req;
	bool pd_vo_was_off;
	uint8_t phy_conf0;
};

/* HPLL and the clocks pixel_clock() sets, as found. */
struct rk_clocks {
	uint32_t hpll_con0, hpll_con1, hpll_con2, mode, clksel39, pmuclksel8, vo_con0, gate20;
};

/* The VOP2's settings as they were before their first write while the log records, to put back. */
struct rk_log {
	uint64_t vop;
	unsigned n;
	bool lost;
	uint64_t addr[LOG_MAX];
	uint32_t val[LOG_MAX];
};

static struct rk_log *recording;

static uint32_t
rd(uint64_t addr)
{
	return *(volatile uint32_t *)(uintptr_t)addr;
}

/*
 * The VOP2's settings: the system registers between REG_CFG_DONE and the
 * interrupt ones, then the overlay, the video ports and the windows.
 */
static bool
vop2_setting(uint64_t off)
{
	return (off > VOP2_REG_CFG_DONE && off < VOP2_SYS0_INTR_EN) || (off >= VOP2_OVL_CTRL && off < VOP2_MMU(0));
}

/* addr's value before its first write, in l, when addr is one of the VOP2's settings. */
static void __attribute__((noinline))
note(struct rk_log *l, uint64_t addr)
{
	if (addr < l->vop || !vop2_setting(addr - l->vop)) {
		return;
	}
	unsigned i = 0;
	while (i < l->n && l->addr[i] != addr) {
		i++;
	}
	if (i < l->n) {
		return;
	}
	if (i == LOG_MAX) {
		l->lost = true;
		return;
	}
	l->addr[i] = addr;
	l->val[i] = rd(addr);
	l->n++;
}

static void
wr(uint64_t addr, uint32_t val)
{
	if (recording) {
		note(recording, addr);
	}
	*(volatile uint32_t *)(uintptr_t)addr = val;
}

static void
record(struct rk_log *l, uint64_t vop)
{
	l->vop = vop;
	l->n = 0;
	l->lost = false;
	recording = l;
}

/* What l recorded back, the last write first; it records no more. */
static void
put_back(struct rk_log *l)
{
	if (recording == l) {
		recording = NULL;
	}
	if (l->lost) {
		printf("pieboot: HDMI: more than %u VOP2 registers changed; not all go back\n", LOG_MAX);
	}
	while (l->n > 0) {
		l->n--;
		wr(l->addr[l->n], l->val[l->n]);
	}
}

static void
wr_masked(uint64_t addr, uint32_t mask, uint32_t val)
{
	wr(addr, mask << 16 | (val & mask));
}

static bool trace;

/* With -v, a line naming the step about to start, in memory before the step's first access. */
static void __attribute__((format(printf, 1, 2)))
step(const char *fmt, ...)
{
	if (!trace) {
		return;
	}
	char line[224];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	printf("pieboot: HDMI: %s%s\n", line, (read_isr() & ISR_SERROR) ? "; an SError is pending" : "");
	dsb_sy();
}

static void
step_hdmi(const char *what)
{
	step("%s", what);
}

/* With -v, the VO blocks' power, bus, clock and reset state, from the PMU, CRU and PMUCRU, which are always on. */
static void
step_state(const struct rk_regs *r, const char *when)
{
	if (!trace) {
		return;
	}
	step("%s: power 0x%x 0x%x, VO bus 0x%x 0x%x 0x%x, PLL power 0x%x; gates 0x%04x 0x%04x, resets 0x%04x", when,
	    rd(r->pmu + PMU_PWR_DWN_ST), rd(r->pmu + PMU_PWR_GATE_SFTCON), rd(r->pmu + PMU_BUS_IDLE_SFTCON0),
	    rd(r->pmu + PMU_BUS_IDLE_ACK), rd(r->pmu + PMU_BUS_IDLE_ST), rd(r->pmu + PMU_PLLPD_SFTCON),
	    rd(r->cru + CRU_GATE(20)) & 0xffff, rd(r->cru + CRU_GATE(21)) & 0xffff,
	    rd(r->cru + CRU_SOFTRST(16)) & 0xffff);
	step("%s: clocks 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x; HPLL 0x%04x 0x%04x 0x%08x, mode 0x%04x, PHY reference 0x%04x",
	    when, rd(r->cru + CRU_CLKSEL(37)) & 0xffff, rd(r->cru + CRU_CLKSEL(38)) & 0xffff,
	    rd(r->cru + CRU_CLKSEL(39)) & 0xffff, rd(r->cru + CRU_CLKSEL(40)) & 0xffff,
	    rd(r->cru + CRU_CLKSEL(41)) & 0xffff, rd(r->pmucru + PMUCRU_HPLL_CON(0)) & 0xffff,
	    rd(r->pmucru + PMUCRU_HPLL_CON(1)) & 0xffff, rd(r->pmucru + PMUCRU_HPLL_CON(2)),
	    rd(r->pmucru + PMUCRU_MODE_CON00) & 0xffff, rd(r->pmucru + PMUCRU_CLKSEL(8)) & 0xffff);
}

static void
udelay(uint64_t us)
{
	for (uint64_t until = read_cntpct() + read_cntfrq() * us / 1000000; read_cntpct() < until;) {
	}
}

static bool
wait_bits(uint64_t addr, uint32_t mask, uint32_t want)
{
	for (uint64_t until = read_cntpct() + read_cntfrq() * TIMEOUT_US / 1000000;;) {
		if ((rd(addr) & mask) == want) {
			return true;
		}
		if (read_cntpct() > until) {
			return false;
		}
	}
}

static void vo_restore(const struct rk_regs *r, const struct rk_state *s);

static bool
enabled(const struct fdt_node *n)
{
	const char *status = fdt_prop_str(n, "status");
	return !status || !strcmp(status, "okay") || !strcmp(status, "ok");
}

static struct fdt_node *
find(const struct fdt *fdt, const char *compat, uint64_t *base)
{
	struct fdt_node *n = fdt_find_compatible(fdt->root, compat);
	uint64_t size;
	return n && enabled(n) && fdt_reg(n, 0, base, &size) ? n : NULL;
}

static bool
word(const char *p, size_t len, const char *w)
{
	return strlen(w) == len && !strncmp(p, w, len);
}

static const struct {
	uint16_t base, cfg_done;
	const char *name;
} rk_windows[8] = {
	[VOP2_CLUSTER0] = { 0x1000, 1u << 6, "cluster0" },
	[VOP2_CLUSTER1] = { 0x1200, 1u << 7, "cluster1" },
	[VOP2_ESMART0] = { 0x1800, 1u << 10, "esmart0" },
	[VOP2_SMART0] = { 0x1c00, 1u << 12, "smart0" },
	[VOP2_ESMART1] = { 0x1a00, 1u << 11, "esmart1" },
	[VOP2_SMART1] = { 0x1e00, 1u << 13, "smart1" },
};

static bool
win_cluster(unsigned win)
{
	return win == VOP2_CLUSTER0 || win == VOP2_CLUSTER1;
}

static bool
win_on(uint64_t vop, unsigned win)
{
	uint64_t w = vop + rk_windows[win].base;
	if (win_cluster(win)) {
		return (rd(w + CLUSTER_CTRL) & CLUSTER_EN) || (rd(w + CLUSTER_WIN_CTRL0(0)) & CLUSTER_WIN_EN) ||
		       (rd(w + CLUSTER_WIN_CTRL0(1)) & CLUSTER_WIN_EN);
	}
	for (unsigned r = 0; r < 4; r++) {
		if (rd(w + WIN_REGION_MST_CTL(r)) & MST_CTL_EN) {
			return true;
		}
	}
	return false;
}

static void
win_off(uint64_t vop, unsigned win)
{
	uint64_t w = vop + rk_windows[win].base;
	if (win_cluster(win)) {
		for (unsigned i = 0; i < 2; i++) {
			wr(w + CLUSTER_WIN_CTRL0(i), rd(w + CLUSTER_WIN_CTRL0(i)) & ~CLUSTER_WIN_EN);
		}
		wr(w + CLUSTER_CTRL, rd(w + CLUSTER_CTRL) & ~CLUSTER_EN);
		return;
	}
	for (unsigned r = 0; r < 4; r++) {
		wr(w + WIN_REGION_MST_CTL(r), rd(w + WIN_REGION_MST_CTL(r)) & ~MST_CTL_EN);
	}
}

/*
 * With -v, what the VOP2 and the HDMI controller report once running: video
 * port vp's line counter, control and raw interrupt status over 50 ms, the
 * buses', the two AXI ports' DMA stop and IOMMU idle bits, the register-done
 * flags, the overlay, the window win and the interface.
 */
static void
step_status(const struct rk_regs *r, unsigned win, unsigned vp, const char *when)
{
	if (!trace) {
		return;
	}
	wr(r->vop + VOP2_PORT_INTR_CLR(vp), PORT_INTR_ALL << 16 | PORT_INTR_ALL);
	wr(r->vop + VOP2_SYS0_INTR_CLR, SYS0_INTR_ALL << 16 | SYS0_INTR_ALL);
	wr(r->vop + VOP2_SYS1_INTR_CLR, SYS1_INTR_ALL << 16 | SYS1_INTR_ALL);
	udelay(50000);
	uint64_t w = r->vop + rk_windows[win].base;
	step("%s: video port %u line %u, control 0x%08x, raw status 0x%03x, buses 0x%02x 0x%02x, AXI stop and idle 0x%x "
	    "0x%x; done 0x%04x, OTP_WIN_EN %u, gating 0x%08x, AXI 0x%04x", when, vp,
	    (rd(r->vop + VOP2_STATUS(vp)) >> 16) & 0x1fff, rd(r->vop + VOP2_VP(vp) + VP_DSP_CTRL),
	    rd(r->vop + VOP2_PORT_INTR_RAW(vp)) & 0xffff, rd(r->vop + VOP2_SYS0_INTR_RAW) & 0xff,
	    rd(r->vop + VOP2_SYS1_INTR_RAW) & 0xff, rd(r->vop + VOP2_STATUS(0)) & 3, rd(r->vop + VOP2_STATUS(1)) & 3,
	    rd(r->vop + VOP2_REG_CFG_DONE) & 0xffff, rd(r->vop + VOP2_OTP_WIN_EN) & 1, rd(r->vop + VOP2_AUTO_GATING),
	    rd(r->vop + VOP2_AXI_CTRL0) & 0xffff);
	step("%s: layers 0x%08x, ports 0x%08x, overlay 0x%08x, background delay %u; %s 0x%x at 0x%08x, 0x%08x; "
	    "interface 0x%x 0x%08x", when, rd(r->vop + VOP2_OVL_LAYER_SEL), rd(r->vop + VOP2_OVL_PORT_SEL),
	    rd(r->vop + VOP2_OVL_CTRL), rd(r->vop + VOP2_OVL_BG_MIX(vp)) >> 24, rk_windows[win].name,
	    rd(w + WIN_REGION_MST_CTL(0)), rd(w + WIN_REGION0_MST_YRGB), rd(w + WIN_REGION0_ACT_INFO),
	    rd(r->vop + VOP2_DSP_INFACE_EN), rd(r->vop + VOP2_DSP_INFACE_POL));
	step("%s: HDMI clocks locked 0x%02x, PHY 0x%02x status 0x%02x, video 0x%02x, packetizer 0x%02x, interrupts 0x%02x "
	    "0x%02x 0x%02x 0x%02x", when, dwhdmi_peek(r->hdmi, DWHDMI_MC_LOCKONCLOCK),
	    dwhdmi_peek(r->hdmi, DWHDMI_PHY_CONF0), dwhdmi_phy_status(r->hdmi), dwhdmi_peek(r->hdmi, DWHDMI_FC_INVIDCONF),
	    dwhdmi_peek(r->hdmi, DWHDMI_VP_STATUS), dwhdmi_peek(r->hdmi, DWHDMI_IH_FC_STAT0),
	    dwhdmi_peek(r->hdmi, DWHDMI_IH_FC_STAT0 + 1), dwhdmi_peek(r->hdmi, DWHDMI_IH_FC_STAT0 + 2),
	    dwhdmi_peek(r->hdmi, DWHDMI_IH_PHY_STAT0));
}

/* PD_VO on, its bus out of idle, the VOP2's and the HDMI controller's bus clocks on and out of reset. */
static bool
vo_on(const struct rk_regs *r, struct rk_state *s)
{
	s->gate20 = rd(r->cru + CRU_GATE(20)) & 0xffff;
	s->gate21 = rd(r->cru + CRU_GATE(21)) & 0xffff;
	s->softrst16 = rd(r->cru + CRU_SOFTRST(16)) & 0xffff;
	s->idle_req = rd(r->pmu + PMU_BUS_IDLE_SFTCON0) & BUS_IDLE_VO;
	s->pd_vo_was_off = (rd(r->pmu + PMU_PWR_DWN_ST) & PWR_PD_VO) != 0;
	if (s->pd_vo_was_off) {
		wr_masked(r->pmu + PMU_PWR_GATE_SFTCON, PWR_PD_VO, 0);
		if (!wait_bits(r->pmu + PMU_PWR_DWN_ST, PWR_PD_VO, 0)) {
			wr_masked(r->pmu + PMU_PWR_GATE_SFTCON, PWR_PD_VO, PWR_PD_VO);
			printf("pieboot: HDMI: PD_VO does not power up\n");
			return false;
		}
	}
	wr_masked(r->cru + CRU_GATE(20), GATE20_VO, 0);
	wr_masked(r->cru + CRU_GATE(21), GATE21_HDMI, 0);
	if ((rd(r->pmu + PMU_BUS_IDLE_ST) | rd(r->pmu + PMU_BUS_IDLE_ACK)) & BUS_IDLE_VO) {
		wr_masked(r->pmu + PMU_BUS_IDLE_SFTCON0, BUS_IDLE_VO, 0);
		if (!wait_bits(r->pmu + PMU_BUS_IDLE_ACK, BUS_IDLE_VO, 0) ||
		    !wait_bits(r->pmu + PMU_BUS_IDLE_ST, BUS_IDLE_VO, 0)) {
			vo_restore(r, s);
			printf("pieboot: HDMI: the VO bus stays idle\n");
			return false;
		}
	}
	wr_masked(r->cru + CRU_SOFTRST(16), SOFTRST16_VO | SOFTRST16_HDMI, 0);
	udelay(10);
	return true;
}

/* Back as vo_on found it. */
static void
vo_restore(const struct rk_regs *r, const struct rk_state *s)
{
	wr_masked(r->cru + CRU_SOFTRST(16), SOFTRST16_VO | SOFTRST16_HDMI, s->softrst16);
	if (s->pd_vo_was_off || s->idle_req) {
		wr_masked(r->pmu + PMU_BUS_IDLE_SFTCON0, BUS_IDLE_VO, BUS_IDLE_VO);
		bool idle = wait_bits(r->pmu + PMU_BUS_IDLE_ACK, BUS_IDLE_VO, BUS_IDLE_VO) &&
		    wait_bits(r->pmu + PMU_BUS_IDLE_ST, BUS_IDLE_VO, BUS_IDLE_VO);
		if (s->pd_vo_was_off && idle) {
			wr_masked(r->pmu + PMU_PWR_GATE_SFTCON, PWR_PD_VO, PWR_PD_VO);
			wait_bits(r->pmu + PMU_PWR_DWN_ST, PWR_PD_VO, PWR_PD_VO);
		}
	}
	wr_masked(r->cru + CRU_GATE(20), GATE20_VO, s->gate20);
	wr_masked(r->cru + CRU_GATE(21), GATE21_HDMI, s->gate21);
}

/* The HDMI controller reset, as from power-on. */
static void
hdmi_reset(const struct rk_regs *r)
{
	wr_masked(r->cru + CRU_SOFTRST(16), SOFTRST16_HDMI, SOFTRST16_HDMI);
	udelay(10);
	wr_masked(r->cru + CRU_SOFTRST(16), SOFTRST16_HDMI, 0);
	udelay(10);
}

/* HPLL as c has it: slow mode while it changes, then its mode once it locks. */
static void
hpll_restore(const struct rk_regs *r, const struct rk_clocks *c)
{
	wr_masked(r->pmucru + PMUCRU_MODE_CON00, MODE_HPLL, MODE_HPLL_XIN24M);
	wr_masked(r->pmucru + PMUCRU_HPLL_CON(0), HPLL_CON0_MASK, c->hpll_con0);
	wr(r->pmucru + PMUCRU_HPLL_CON(2), c->hpll_con2);
	wr_masked(r->pmucru + PMUCRU_HPLL_CON(1), HPLL_CON1_MASK, c->hpll_con1);
	if ((c->mode & MODE_HPLL) == MODE_HPLL_PLL) {
		wait_bits(r->pmucru + PMUCRU_HPLL_CON(1), HPLL_CON1_LOCK, HPLL_CON1_LOCK);
	}
	wr_masked(r->pmucru + PMUCRU_MODE_CON00, MODE_HPLL, c->mode);
}

/*
 * HPLL, DCLK_VOP0 and the HDMI PHY's reference at the pixel clock, with what
 * they were in c; HPLL as it was if it does not lock.
 */
static bool
pixel_clock(const struct rk_regs *r, const struct rk_mode *mode, struct rk_clocks *c)
{
	c->hpll_con0 = rd(r->pmucru + PMUCRU_HPLL_CON(0)) & 0xffff;
	c->hpll_con1 = rd(r->pmucru + PMUCRU_HPLL_CON(1)) & 0xffff;
	c->hpll_con2 = rd(r->pmucru + PMUCRU_HPLL_CON(2));
	c->mode = rd(r->pmucru + PMUCRU_MODE_CON00) & 0xffff;
	c->clksel39 = rd(r->cru + CRU_CLKSEL(39)) & 0xffff;
	c->pmuclksel8 = rd(r->pmucru + PMUCRU_CLKSEL(8)) & 0xffff;
	c->vo_con0 = rd(r->grf + GRF_VO_CON0) & 0xffff;
	c->gate20 = rd(r->cru + CRU_GATE(20)) & 0xffff;

	wr_masked(r->pmucru + PMUCRU_MODE_CON00, MODE_HPLL, MODE_HPLL_XIN24M);
	wr_masked(r->pmucru + PMUCRU_HPLL_CON(0), HPLL_CON0_MASK, (uint32_t)mode->postdiv1 << 12 | mode->fbdiv);
	wr(r->pmucru + PMUCRU_HPLL_CON(2), 0);
	wr_masked(r->pmucru + PMUCRU_HPLL_CON(1), HPLL_CON1_MASK,
	    HPLL_CON1_DSMPD | (uint32_t)mode->postdiv2 << 6 | mode->refdiv);
	if (!wait_bits(r->pmucru + PMUCRU_HPLL_CON(1), HPLL_CON1_LOCK, HPLL_CON1_LOCK)) {
		hpll_restore(r, c);
		printf("pieboot: HDMI: HPLL does not lock\n");
		return false;
	}
	wr_masked(r->pmucru + PMUCRU_MODE_CON00, MODE_HPLL, MODE_HPLL_PLL);
	wr_masked(r->cru + CRU_CLKSEL(39), DCLK_VOP_SEL | DCLK_VOP_DIV, DCLK_VOP_SEL_HPLL);
	wr_masked(r->pmucru + PMUCRU_CLKSEL(8), CLKSEL8_HDMIPHY_REF_HPLL_PH0, 0);
	wr_masked(r->grf + GRF_VO_CON0, VO_CON0_HDMIPHY_I2C, VO_CON0_HDMIPHY_I2C);
	wr_masked(r->cru + CRU_GATE(20), GATE20_DCLK_VOP(0), 0);
	return true;
}

/* What pixel_clock() changed, back as c has it. */
static void
clocks_restore(const struct rk_regs *r, const struct rk_clocks *c)
{
	wr_masked(r->cru + CRU_GATE(20), GATE20_DCLK_VOP(0), c->gate20);
	wr_masked(r->cru + CRU_CLKSEL(39), DCLK_VOP_SEL | DCLK_VOP_DIV, c->clksel39);
	wr_masked(r->pmucru + PMUCRU_CLKSEL(8), CLKSEL8_HDMIPHY_REF_HPLL_PH0, c->pmuclksel8);
	wr_masked(r->grf + GRF_VO_CON0, VO_CON0_HDMIPHY_I2C, c->vo_con0);
	hpll_restore(r, c);
}

/* What runs from HPLL besides video port 0, or NULL. */
static const char *
hpll_user(const struct rk_regs *r)
{
	if ((rd(r->cru + CRU_CLKSEL(38)) & CLKSEL38_ACLK_VOP_PRE) == CLKSEL38_ACLK_VOP_PRE_HPLL) {
		return "HPLL drives the VOP2's bus clock";
	}
	if ((rd(r->cru + CRU_CLKSEL(35)) & CLKSEL35_CLK_ISP) == CLKSEL35_CLK_ISP_HPLL &&
	    !(rd(r->cru + CRU_GATE(19)) & GATE19_CLK_ISP)) {
		return "HPLL drives the ISP's clock";
	}
	uint32_t gate20 = rd(r->cru + CRU_GATE(20));
	for (unsigned vp = 1; vp <= 2; vp++) {
		if ((rd(r->cru + CRU_CLKSEL(39 + vp)) & DCLK_VOP_SEL) == DCLK_VOP_SEL_HPLL &&
		    !(gate20 & GATE20_DCLK_VOP(vp)) && !(rd(r->vop + VOP2_VP(vp) + VP_DSP_CTRL) & DSP_CTRL_STANDBY)) {
			return vp == 1 ? "HPLL drives video port 1" : "HPLL drives video port 2";
		}
	}
	return NULL;
}

static void
vp_timing(uint64_t vp, const struct dwhdmi_mode *m, uint32_t delay)
{
	uint32_t hact_st = m->htotal - m->hsync_start, vact_st = m->vtotal - m->vsync_start;
	uint32_t hact = hact_st << 16 | (hact_st + m->hdisplay), vact = vact_st << 16 | (vact_st + m->vdisplay);

	wr(vp + VP_HTOTAL_HS_END, (uint32_t)m->htotal << 16 | (uint32_t)(m->hsync_end - m->hsync_start));
	wr(vp + VP_HACT_ST_END, hact);
	wr(vp + VP_VTOTAL_VS_END, (uint32_t)m->vtotal << 16 | (uint32_t)(m->vsync_end - m->vsync_start));
	wr(vp + VP_VACT_ST_END, vact);
	wr(vp + VP_POST_HACT_INFO, hact);
	wr(vp + VP_POST_VACT_INFO, vact);
	wr(vp + VP_POST_SCL_FACTOR, POST_SCL_FACTOR_1);
	wr(vp + VP_POST_SCL_CTRL, 0);
	wr(vp + VP_PRE_SCAN_HTIMING,
	    (uint32_t)(m->hdisplay / 2 + delay - 1) << 16 | (uint32_t)(m->hsync_end - m->hsync_start));
	wr(vp + VP_DSP_BG, 0);
}

static void
window(uint64_t vop, unsigned win, uint64_t fb, const struct dwhdmi_mode *m, bool on)
{
	uint64_t w = vop + rk_windows[win].base;
	uint32_t size = (uint32_t)(m->vdisplay - 1) << 16 | (uint32_t)(m->hdisplay - 1);

	wr(w + WIN_CTRL0, 0);
	wr(w + WIN_CTRL1, rd(w + WIN_CTRL1) & ~CTRL1_YMIR);
	for (unsigned r = 1; r < 4; r++) {
		wr(w + WIN_REGION_MST_CTL(r), rd(w + WIN_REGION_MST_CTL(r)) & ~MST_CTL_EN);
	}
	wr(w + WIN_REGION0_MST_YRGB, (uint32_t)fb);
	wr(w + WIN_REGION0_VIR, m->hdisplay);
	wr(w + WIN_REGION0_ACT_INFO, size);
	wr(w + WIN_REGION0_DSP_INFO, size);
	wr(w + WIN_REGION0_DSP_OFFSET, 0);
	wr(w + WIN_REGION0_SCL_CTRL, 0);
	wr(w + WIN_REGION_MST_CTL(0), MST_CTL_ARGB8888 | (on ? MST_CTL_EN : 0));
}

static bool
vp0_running(uint64_t vop)
{
	uint32_t a = (rd(vop + VOP2_STATUS(0)) >> 16) & 0x1fff;
	udelay(1000);
	return ((rd(vop + VOP2_STATUS(0)) >> 16) & 0x1fff) != a;
}

/* pieboot.hdmi=probe: test pictures, each for hold seconds, that show where the window path fails. */
enum { OVL_PIEBOOT, OVL_FOUND, OVL_STOCK };

static const struct {
	const char *what;
	uint8_t hold;                   /* 8 s where a sink may lock afresh */
	uint8_t vp;                     /* the video port HDMI takes */
	uint8_t win;
	uint8_t overlay;                /* OVL_PIEBOOT: vop2_overlay()'s for win; OVL_FOUND; OVL_STOCK */
	uint8_t pattern;                /* the window, smaller than the screen, with test pattern n; 0 none */
	bool bars;
	bool low;                       /* the window's buffer in low RAM instead of the framebuffer */
	uint8_t delay;                  /* the background delay, in BG_MIX and PRE_SCAN_HTIMING */
	uint32_t bg;
} probe_stages[] = {
	{ "blue background, no window", 8, 0, VOP2_ESMART0, OVL_PIEBOOT, 0, false, false, DP_BG_DLY, BG_BLUE(0x3ff) },
	{ "esmart0 on video port 0: vertical bands on dim red", 4, 0, VOP2_ESMART0, OVL_PIEBOOT, 1, false, false, DP_BG_DLY,
	  BG_RED(0x180) },
	{ "esmart0 with the overlay as found: horizontal bands on dim red", 4, 0, VOP2_ESMART0, OVL_FOUND, 2, false, false,
	  DP_BG_DLY, BG_RED(0x180) },
	{ "smart0 on video port 0: a checkerboard on dim red", 4, 0, VOP2_SMART0, OVL_PIEBOOT, 3, false, false, DP_BG_DLY,
	  BG_RED(0x180) },
	{ "esmart0 reading low RAM: white on dim red", 4, 0, VOP2_ESMART0, OVL_PIEBOOT, 4, false, true, DP_BG_DLY,
	  BG_RED(0x180) },
	{ "esmart0 with background delay 42: vertical bands on dim red", 4, 0, VOP2_ESMART0, OVL_PIEBOOT, 1, false, false,
	  42, BG_RED(0x180) },
	{ "video port 1 with the stock firmware's overlay, smart0: horizontal bands on dim green", 8, 1, VOP2_SMART0,
	  OVL_STOCK, 2, false, false, 38, BG_GREEN(0x180) },
	{ "colour bars on video port 0", 8, 0, VOP2_ESMART0, OVL_PIEBOOT, 0, true, false, DP_BG_DLY, 0 },
};

/* What the probe needs from the bring-up. */
struct probe_ctx {
	const struct rk_regs *r;
	const struct dwhdmi_mode *m;
	uint64_t fb, low;               /* the framebuffer; a buffer in low RAM, or 0 */
	uint32_t layer_sel, port_sel;   /* as found */
	uint32_t inface_en, pol, dsp_ctrl, clksel40, gate20, vp1_ctrl;
	unsigned win;                   /* the bring-up's window */
};

static void
probe_pattern(uint64_t fb, const struct dwhdmi_mode *m, unsigned kind)
{
	static const uint32_t colours[8] = {
		0xffffffff, 0xffffff00, 0xff00ffff, 0xff00ff00, 0xffff00ff, 0xffff0000, 0xff0000ff, 0xff808080,
	};
	volatile uint32_t *px = (volatile uint32_t *)(uintptr_t)fb;
	for (unsigned y = 0; y < m->vdisplay; y++) {
		for (unsigned x = 0; x < m->hdisplay; x++) {
			uint32_t c = 0xffffffff;
			if (kind == 1) {
				c = colours[x * 8 / m->hdisplay];
			} else if (kind == 2) {
				c = colours[y * 8 / m->vdisplay];
			} else if (kind == 3) {
				c = ((x / 32) ^ (y / 32)) & 1 ? 0xffffffff : 0xff0000ff;
			}
			px[(uint64_t)y * m->hdisplay + x] = c;
		}
	}
}

static void
probe_window(uint64_t vop, unsigned win, uint64_t buf, const struct dwhdmi_mode *m)
{
	uint64_t w = vop + rk_windows[win].base;
	uint32_t width = m->hdisplay * 3 / 4, height = m->vdisplay * 3 / 4;
	uint32_t size = (height - 1) << 16 | (width - 1);
	wr(w + WIN_CTRL0, 0);
	wr(w + WIN_CTRL1, rd(w + WIN_CTRL1) & ~CTRL1_YMIR);
	wr(w + WIN_REGION0_MST_YRGB, (uint32_t)buf);
	wr(w + WIN_REGION0_VIR, m->hdisplay);
	wr(w + WIN_REGION0_ACT_INFO, size);
	wr(w + WIN_REGION0_DSP_INFO, size);
	wr(w + WIN_REGION0_DSP_OFFSET, (m->vdisplay - height) / 2 << 16 | (m->hdisplay - width) / 2);
	wr(w + WIN_REGION0_SCL_CTRL, 0);
	wr(w + WIN_REGION_MST_CTL(0), MST_CTL_ARGB8888 | MST_CTL_EN);
}

/* Register done for both video ports and every window. */
static uint32_t
probe_done(void)
{
	uint32_t done = CFG_DONE_GLOBAL_EN | CFG_DONE_VP0 | CFG_DONE_VP1;
	for (unsigned w = 0; w < 8; w++) {
		done |= rk_windows[w].cfg_done;
	}
	return done;
}

/*
 * One whole set-up: HDMI from video port vp, every window off but win, which
 * shows test pattern pattern from the framebuffer or the low buffer; the
 * overlay; the port's timing, background delay, background and colour bars.
 * Video port 0 is in standby while video port 1 runs, and video port 1 as
 * found otherwise. The interface's polarities stay as the bring-up set them.
 */
static void
probe_apply(const struct probe_ctx *c, unsigned vp, unsigned win, unsigned overlay, unsigned pattern, bool bars,
    bool low, uint32_t delay, uint32_t bg)
{
	const struct rk_regs *r = c->r;
	uint32_t layers = c->layer_sel, ports = c->port_sel;
	struct vop2_ovl o;
	if (overlay == OVL_PIEBOOT && vop2_overlay(c->layer_sel, c->port_sel, win, &o)) {
		layers = o.layer_sel;
		ports = o.port_sel;
	} else if (overlay == OVL_STOCK) {
		layers = STOCK_LAYER_SEL;
		ports = STOCK_PORT_SEL;
	}
	for (unsigned w = 0; w < 8; w++) {
		if (rk_windows[w].name) {
			win_off(r->vop, w);
		}
	}
	if (pattern) {
		uint64_t buf = low ? c->low : c->fb;
		probe_pattern(buf, c->m, pattern);
		probe_window(r->vop, win, buf, c->m);
	}
	wr(r->vop + VOP2_OVL_CTRL, (rd(r->vop + VOP2_OVL_CTRL) & ~OVL_CTRL_VP0_YUV) | OVL_CTRL_REGDONE_IMD);
	wr(r->vop + VOP2_OVL_LAYER_SEL, layers);
	wr(r->vop + VOP2_OVL_PORT_SEL, ports);
	uint64_t v = r->vop + VOP2_VP(vp), other = r->vop + VOP2_VP(1 - vp);
	if (vp == 1) {
		wr(other + VP_DSP_CTRL, rd(other + VP_DSP_CTRL) | DSP_CTRL_STANDBY);
		wr_masked(r->cru + CRU_CLKSEL(40), DCLK_VOP_SEL | DCLK_VOP_DIV, DCLK_VOP_SEL_HPLL);
		wr_masked(r->cru + CRU_GATE(20), GATE20_DCLK_VOP(1), 0);
	} else {
		wr(other + VP_DSP_CTRL, (rd(other + VP_DSP_CTRL) & ~DSP_CTRL_STANDBY) | (c->vp1_ctrl & DSP_CTRL_STANDBY));
		udelay(50000);
		wr_masked(r->cru + CRU_CLKSEL(40), DCLK_VOP_SEL | DCLK_VOP_DIV, c->clksel40);
		wr_masked(r->cru + CRU_GATE(20), GATE20_DCLK_VOP(1), c->gate20);
	}
	vp_timing(v, c->m, delay);
	wr(r->vop + VOP2_OVL_BG_MIX(vp), delay << 24);
	wr(v + VP_DSP_BG, bg);
	wr(v + VP_COLOR_CTRL, bars ? COLOR_BAR_EN : 0);
	uint32_t out = vp == 0 ? c->dsp_ctrl & DSP_CTRL_OUT_MODE : DSP_CTRL_OUT_P888;
	wr(v + VP_DSP_CTRL, out | (pattern ? 0 : DSP_CTRL_WIN_BYPASS));
	wr(r->vop + VOP2_DSP_INFACE_EN, (c->inface_en & ~INFACE_EN_HDMI_MUX) | INFACE_EN_HDMI | vp << 10);
	wr(r->vop + VOP2_DSP_INFACE_POL, c->pol);
	wr(r->vop + VOP2_REG_CFG_DONE, probe_done() << 16 | probe_done());
}

/* The pictures, then the VOP2 and video port 1's clock back as the bring-up left them, and the framebuffer cleared. */
static void
probe(const struct probe_ctx *c)
{
	static struct rk_log changed;
	record(&changed, c->r->vop);
	unsigned n = sizeof(probe_stages) / sizeof(probe_stages[0]);
	for (unsigned i = 0; i < n; i++) {
		if (probe_stages[i].low && !c->low) {
			step("probe %u of %u: %s: no room in low RAM, skipped", i + 1, n, probe_stages[i].what);
			continue;
		}
		probe_apply(c, probe_stages[i].vp, probe_stages[i].win, probe_stages[i].overlay, probe_stages[i].pattern,
		    probe_stages[i].bars, probe_stages[i].low, probe_stages[i].delay, probe_stages[i].bg);
		step("probe %u of %u: %s", i + 1, n, probe_stages[i].what);
		udelay(probe_stages[i].hold * 1000000ULL);
		step_status(c->r, probe_stages[i].win, probe_stages[i].vp, "probe");
	}
	memset((void *)(uintptr_t)c->fb, 0, ALIGN_UP((uint64_t)c->m->hdisplay * 4 * c->m->vdisplay, 0x4000));
	put_back(&changed);
	wr(c->r->vop + VOP2_REG_CFG_DONE, probe_done() << 16 | probe_done());
	wr_masked(c->r->cru + CRU_CLKSEL(40), DCLK_VOP_SEL | DCLK_VOP_DIV, c->clksel40);
	wr_masked(c->r->cru + CRU_GATE(20), GATE20_DCLK_VOP(1), c->gate20);
	step("probe done; video port 0 and %s as set up", rk_windows[c->win].name);
}

static struct fdt_node *
chosen_node(const struct fdt *fdt)
{
	struct fdt_node *chosen = fdt_path(fdt, "/chosen");
	unsigned ac = fdt_address_cells(chosen), sc = fdt_size_cells(chosen);
	return chosen && ac >= 1 && ac <= 2 && sc >= 1 && sc <= 2 ? chosen : NULL;
}

static void
add_node(struct fdt_node *chosen, uint64_t base, const struct dwhdmi_mode *m)
{
	unsigned ac = fdt_address_cells(chosen), sc = fdt_size_cells(chosen);
	uint64_t cells[2] = { base, (uint64_t)m->hdisplay * 4 * m->vdisplay };
	uint8_t reg[16];
	unsigned len = 0;
	for (unsigned i = 0; i < 2; i++) {
		for (unsigned c = i ? sc : ac; c > 0; c--) {
			uint32_t v = (uint32_t)(cells[i] >> (32 * (c - 1)));
			reg[len++] = (uint8_t)(v >> 24);
			reg[len++] = (uint8_t)(v >> 16);
			reg[len++] = (uint8_t)(v >> 8);
			reg[len++] = (uint8_t)v;
		}
	}
	char name[32];
	snprintf(name, sizeof(name), "framebuffer@%lx", base);
	struct fdt_node *n = fdt_add_node(chosen, name);
	static const char compat[] = "simple-framebuffer", format[] = "x8r8g8b8", okay[] = "okay";
	fdt_add_prop(n, "compatible", compat, sizeof(compat));
	fdt_add_prop(n, "reg", reg, len);
	fdt_add_prop_u32(n, "width", m->hdisplay);
	fdt_add_prop_u32(n, "height", m->vdisplay);
	fdt_add_prop_u32(n, "stride", (uint32_t)m->hdisplay * 4);
	fdt_add_prop(n, "format", format, sizeof(format));
	fdt_add_prop(n, "status", okay, sizeof(okay));
}

static struct fdt_node *
rk_find(const struct fdt *fdt, struct rk_regs *r)
{
	struct fdt_node *hn = find(fdt, "rockchip,rk3568-dw-hdmi", &r->hdmi);
	r->rk3566 = find(fdt, "rockchip,rk3566-vop", &r->vop) != NULL;
	if (!hn || !(r->rk3566 || find(fdt, "rockchip,rk3568-vop", &r->vop)) ||
	    !find(fdt, "rockchip,rk3568-cru", &r->cru) || !find(fdt, "rockchip,rk3568-pmucru", &r->pmucru) ||
	    !find(fdt, "rockchip,rk3568-pmu", &r->pmu) || !find(fdt, "rockchip,rk3568-grf", &r->grf)) {
		return NULL;
	}
	return hn;
}

bool
rk_hdmi_board(const struct fdt *fdt)
{
	struct rk_regs r;
	return rk_find(fdt, &r) != NULL;
}

bool
rk_hdmi(struct fdt *fdt, const char *opts, bool verbose, uint64_t end, const struct place_range *busy, unsigned n,
    uint64_t *base, uint64_t *size)
{
	struct rk_regs r;
	struct fdt_node *hn = rk_find(fdt, &r);
	if (!hn) {
		return false;
	}
	trace = verbose;

	const struct rk_mode *mode = &rk_modes[0];
	bool hdmi = false, bars = false, probing = false;
	for (const char *p = opts; p && *p;) {
		const char *e = p;
		while (*e && *e != ',') {
			e++;
		}
		size_t len = (size_t)(e - p);
		bool known = true;
		if (word(p, len, "off")) {
			printf("pieboot: HDMI: off, as pieboot.hdmi asks\n");
			return false;
		} else if (word(p, len, "dvi")) {
			hdmi = false;
		} else if (word(p, len, "hdmi")) {
			hdmi = true;
		} else if (word(p, len, "bars")) {
			bars = true;
		} else if (word(p, len, "probe")) {
			probing = true;
		} else {
			known = false;
			for (unsigned i = 0; i < sizeof(rk_modes) / sizeof(rk_modes[0]) && !known; i++) {
				if (word(p, len, rk_modes[i].name)) {
					mode = &rk_modes[i];
					known = true;
				}
			}
		}
		if (!known) {
			printf("pieboot: HDMI: pieboot.hdmi takes off, 640x480, 1280x720, dvi, hdmi, bars or probe, not %.*s\n",
			    (int)len, p);
		}
		p = *e ? e + 1 : e;
	}
	if (probing && bars) {
		printf("pieboot: HDMI: probe needs the framebuffer, so not with bars\n");
		probing = false;
	}
	trace = trace || probing;
	uint32_t width = 1;
	fdt_prop_u32(hn, "reg-io-width", &width);
	if (width != 4) {
		printf("pieboot: HDMI: the controller's reg-io-width is %u, not 4; HDMI left off\n", width);
		return false;
	}

	struct rk_state state;
	step_state(&r, "as found");
	if (!vo_on(&r, &state)) {
		return false;
	}
	step_state(&r, "VO on");
	step("reading the HDMI controller's PHY_CONF0");
	state.phy_conf0 = dwhdmi_hpd_sense(r.hdmi);
	step("PHY_CONF0 0x%02x; waiting for hot plug", state.phy_conf0);
	bool hpd = dwhdmi_hpd(r.hdmi);
	for (uint64_t until = read_cntpct() + read_cntfrq() * HPD_WAIT_US / 1000000; !hpd && read_cntpct() < until;) {
		hpd = dwhdmi_hpd(r.hdmi);
	}
	if (!hpd) {
		uint8_t status = dwhdmi_phy_status(r.hdmi);
		dwhdmi_hpd_sense_restore(r.hdmi, state.phy_conf0);
		vo_restore(&r, &state);
		printf("pieboot: HDMI: no display connected, PHY status 0x%02x\n", status);
		return false;
	}

	const struct dwhdmi_mode *m = &mode->m;
	if (trace) {
		step("hot plug, PHY status 0x%02x; reading the controller's IDs", dwhdmi_phy_status(r.hdmi));
	}
	uint32_t version = dwhdmi_version(r.hdmi);
	uint8_t phy = dwhdmi_phy_type(r.hdmi);
	step("controller 0x%04x, PHY type 0x%02x; reading the VOP2's overlay", version, phy);
	uint32_t layer_sel = rd(r.vop + VOP2_OVL_LAYER_SEL), port_sel = rd(r.vop + VOP2_OVL_PORT_SEL);
	if (trace) {
		step("layers 0x%08x, ports 0x%08x, done 0x%04x, OTP_WIN_EN %u, gating 0x%08x, AXI 0x%04x, AXI stop and idle 0x%x "
		    "0x%x; reading esmart0", layer_sel, port_sel, rd(r.vop + VOP2_REG_CFG_DONE) & 0xffff,
		    rd(r.vop + VOP2_OTP_WIN_EN) & 1, rd(r.vop + VOP2_AUTO_GATING), rd(r.vop + VOP2_AXI_CTRL0) & 0xffff,
		    rd(r.vop + VOP2_STATUS(0)) & 3, rd(r.vop + VOP2_STATUS(1)) & 3);
	}
	unsigned win = VOP2_ESMART0;
	if (win_on(r.vop, VOP2_ESMART0) && vop2_win_port(port_sel, VOP2_ESMART0) != 0) {
		win = VOP2_SMART0;
	}
	struct vop2_ovl ovl;
	uint64_t fb_size = ALIGN_UP((uint64_t)m->hdisplay * 4 * m->vdisplay, 0x4000), fb = end - fb_size;
	bool reach = bars || end <= 0x100000000ULL;
	bool room = bars || (end >= fb_size && !(end & 0x3fff));
	for (unsigned i = 0; i < n && room && !bars; i++) {
		room = busy[i].end <= fb || busy[i].base >= end || busy[i].base >= busy[i].end;
	}
	step("%s; reading the VOP2's IOMMU", rk_windows[win].name);
	uint32_t mmu = rd(r.vop + VOP2_MMU(0) + MMU_STATUS) | rd(r.vop + VOP2_MMU(1) + MMU_STATUS);
	step("IOMMU status 0x%x; checking what else runs from HPLL", mmu);
	const char *user = hpll_user(&r);
	step("checking the windows and the overlay");
	const char *why = NULL;
	if (phy != DWHDMI_PHY_HDMI20) {
		why = "the controller reports another PHY type than the HDMI 2.0 TX PHY's";
	} else if (mmu & MMU_PAGING) {
		why = "the VOP2's IOMMU translates its addresses";
	} else if (user) {
		why = user;
	} else if (win == VOP2_SMART0 && win_on(r.vop, VOP2_SMART0) && vop2_win_port(port_sel, VOP2_SMART0) != 0) {
		why = "esmart0 and smart0 both show another display";
	} else if (!vop2_overlay(layer_sel, port_sel, win, &ovl)) {
		why = "the overlay leaves no layer for video port 1";
	} else if (!reach) {
		why = "the top of RAM is above 4 GB, out of the VOP2's reach";
	} else if (!room) {
		why = "no room for the framebuffer below the panic region";
	} else if (!chosen_node(fdt)) {
		why = "/chosen cannot take a framebuffer node";
	}
	if (why) {
		dwhdmi_hpd_sense_restore(r.hdmi, state.phy_conf0);
		vo_restore(&r, &state);
		printf("pieboot: HDMI: %s; HDMI left off (PHY type 0x%02x, layers 0x%08x, ports 0x%08x)\n", why, phy,
		    layer_sel, port_sel);
		return false;
	}
	if (!bars) {
		step("clearing the framebuffer, 0x%lx-0x%lx", fb, end);
		dcache_clean_invalidate(fb, end);
		memset((void *)(uintptr_t)fb, 0, fb_size);
	}

	step("setting HPLL for %u kHz", m->clock_khz);
	struct rk_clocks clocks;
	if (!pixel_clock(&r, mode, &clocks)) {
		dwhdmi_hpd_sense_restore(r.hdmi, state.phy_conf0);
		vo_restore(&r, &state);
		return false;
	}

	static struct rk_log found;
	uint32_t cfg_done = rd(r.vop + VOP2_REG_CFG_DONE) & 0xffff;
	record(&found, r.vop);
	uint64_t vp0 = r.vop + VOP2_VP(0);
	if (trace) {
		step("HPLL 0x%04x 0x%04x, mode 0x%04x; reading video port 0", rd(r.pmucru + PMUCRU_HPLL_CON(0)) & 0xffff,
		    rd(r.pmucru + PMUCRU_HPLL_CON(1)) & 0xffff, rd(r.pmucru + PMUCRU_MODE_CON00) & 0xffff);
	}
	uint32_t ctrl = rd(vp0 + VP_DSP_CTRL);
	step("video port 0 control 0x%08x; standby, and its other windows off", ctrl);
	if (!(ctrl & DSP_CTRL_STANDBY)) {
		wr(vp0 + VP_DSP_CTRL, ctrl | DSP_CTRL_STANDBY);
		udelay(50000);
	}
	uint32_t done = CFG_DONE_GLOBAL_EN | CFG_DONE_VP0 | rk_windows[win].cfg_done;
	for (unsigned w = 0; w < 8; w++) {
		if ((ovl.off & (1u << w)) && win_on(r.vop, w)) {
			win_off(r.vop, w);
			done |= rk_windows[w].cfg_done;
		}
	}
	step("setting up %s, the overlay, the timing and the interface", rk_windows[win].name);
	uint32_t gating = rd(r.vop + VOP2_AUTO_GATING);
	if (r.rk3566) {
		wr(r.vop + VOP2_OTP_WIN_EN, 1);
	}
	wr(r.vop + VOP2_AUTO_GATING, gating & ~AUTO_GATING_EN);
	window(r.vop, win, fb, m, !bars);
	wr(r.vop + VOP2_OVL_CTRL, (rd(r.vop + VOP2_OVL_CTRL) & ~OVL_CTRL_VP0_YUV) | OVL_CTRL_REGDONE_IMD);
	if (ovl.layer_sel != layer_sel) {
		wr(r.vop + VOP2_OVL_LAYER_SEL, ovl.layer_sel);
	}
	if (ovl.port_sel != port_sel) {
		wr(r.vop + VOP2_OVL_PORT_SEL, ovl.port_sel);
	}
	wr(r.vop + VOP2_OVL_DP0_BG_MIX, DP_BG_DLY_SDR);
	vp_timing(vp0, m, DP_BG_DLY);
	wr(vp0 + VP_3D_LUT_CTRL, 0);
	wr(vp0 + VP_BCSH_CTRL, 0);
	wr(vp0 + VP_COLOR_CTRL, bars ? COLOR_BAR_EN : 0);
	uint32_t dsp_ctrl = DSP_CTRL_OUT_RGB30 | (bars ? DSP_CTRL_WIN_BYPASS : 0);
	wr(vp0 + VP_DSP_CTRL, DSP_CTRL_STANDBY | dsp_ctrl);

	wr(r.vop + VOP2_DSP_INFACE_CTRL, rd(r.vop + VOP2_DSP_INFACE_CTRL) | INFACE_CTRL_REGDONE_IMD);
	uint32_t inface_en = rd(r.vop + VOP2_DSP_INFACE_EN);
	wr(r.vop + VOP2_DSP_INFACE_EN, (inface_en & ~INFACE_EN_HDMI_MUX) | INFACE_EN_HDMI);
	uint32_t pol = (rd(r.vop + VOP2_DSP_INFACE_POL) & ~INFACE_POL_HDMI) | INFACE_POL_REGDONE_IMD | INFACE_POL_HDMI_DCLK;
	if (m->vsync_high) {
		pol |= INFACE_POL_HDMI_VSYNC;
	}
	if (m->hsync_high) {
		pol |= INFACE_POL_HDMI_HSYNC;
	}
	wr(r.vop + VOP2_DSP_INFACE_POL, pol);

	step("starting video port 0");
	wr(r.vop + VOP2_REG_CFG_DONE, done << 16 | done);
	wr(vp0 + VP_DSP_CTRL, dsp_ctrl);
	udelay(20000);
	bool running = vp0_running(r.vop);

	step("video port 0 %s; resetting the HDMI controller", running ? "running" : "not running");
	hdmi_reset(&r);
	struct dwhdmi_mode hm = *m;
	hm.hdmi = hdmi;
	step("setting up the PHY and the controller");
	if (!dwhdmi_enable(r.hdmi, &hm, trace ? step_hdmi : NULL)) {
		uint8_t status = dwhdmi_phy_status(r.hdmi);
		dwhdmi_disable(r.hdmi);
		dwhdmi_hpd_sense_restore(r.hdmi, state.phy_conf0);
		put_back(&found);
		wr(r.vop + VOP2_REG_CFG_DONE, (done | CFG_DONE_GLOBAL_EN) << 16 | (done & ~CFG_DONE_GLOBAL_EN) |
		    (cfg_done & CFG_DONE_GLOBAL_EN));
		udelay(50000);
		clocks_restore(&r, &clocks);
		vo_restore(&r, &state);
		printf("pieboot: HDMI: the PHY does not lock, status 0x%02x; HDMI left off\n", status);
		return false;
	}
	recording = NULL;
	step_status(&r, win, 0, "started");
	if (probing) {
		uint64_t low = PROBE_LOW_BUF;
		for (unsigned i = 0; i < n && low; i++) {
			if (!(busy[i].end <= low || busy[i].base >= low + fb_size || busy[i].base >= busy[i].end)) {
				low = 0;
			}
		}
		if (low + fb_size > fb) {
			low = 0;
		}
		struct probe_ctx c = { &r, &hm, fb, low, layer_sel, port_sel, inface_en, pol, dsp_ctrl,
			rd(r.cru + CRU_CLKSEL(40)) & 0xffff, rd(r.cru + CRU_GATE(20)) & 0xffff,
			rd(r.vop + VOP2_VP(1) + VP_DSP_CTRL), win };
		probe(&c);
	}
	printf("pieboot: HDMI: %s at 60 Hz, %s; controller %x.%03x, PHY status 0x%02x; video port 0 %s\n",
	    mode->name, hdmi ? "HDMI" : "DVI", version >> 12, version & 0xfff, dwhdmi_phy_status(r.hdmi),
	    running ? "running" : "not running");
	printf("pieboot: HDMI: VOP2 0x%08x, %s; layers 0x%08x to 0x%08x, ports 0x%08x to 0x%08x\n",
	    rd(r.vop + VOP2_VERSION_INFO), rk_windows[win].name, layer_sel, ovl.layer_sel, port_sel, ovl.port_sel);
	if (bars) {
		printf("pieboot: HDMI: colour bars, as pieboot.hdmi asks; no framebuffer\n");
		return false;
	}
	add_node(chosen_node(fdt), fb, m);
	*base = fb;
	*size = fb_size;
	return true;
}
