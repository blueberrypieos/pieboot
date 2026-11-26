/* SPDX-License-Identifier: BSD-2-Clause */
#include "rkdsi.h"
#include "dsipanel.h"
#include "vop2ovl.h"

#define CRU_VPLL_CON(n)                 (0x00a0 + 4 * (n))
#define  VPLL_CON0_BYPASS               (1u << 15)
#define  VPLL_CON0_POSTDIV1             (7u << 12)
#define  VPLL_CON0_FBDIV                0xfffu
#define  VPLL_CON1_PD                   ((1u << 14) | (1u << 13))
#define  VPLL_CON1_DSMPD                (1u << 12)
#define  VPLL_CON1_LOCK                 (1u << 10)
#define  VPLL_CON1_POSTDIV2             (7u << 6)
#define  VPLL_CON1_REFDIV               0x3fu
#define CRU_MODE_CON00                  0x00c0
#define  MODE_VPLL                      (3u << 12)
#define  MODE_VPLL_XIN24M               (0u << 12)
#define  MODE_VPLL_PLL                  (1u << 12)
#define CRU_CLKSEL(n)                   (0x0100 + 4 * (n))
#define  CLKSEL38_ACLK_VOP_PRE          (3u << 6)
#define  CLKSEL38_ACLK_VOP_PRE_VPLL     (3u << 6)
#define  DCLK_VOP_SEL                   (3u << 10)
#define  DCLK_VOP_SEL_VPLL              (1u << 10)
#define  DCLK_VOP_DIV                   0xffu
#define  CLKSEL45_RKVENC_CORE           (3u << 14)
#define  CLKSEL48_RKVDEC_CA             (3u << 6)
#define  CLKSEL49_RKVDEC_CORE           (3u << 14)
#define  CLKSEL49_RKVDEC_HEVC_CA        (3u << 6)
#define  CLKSEL72_PWM1                  (3u << 8)
#define  CLKSEL72_PWM1_XIN24M           (1u << 8)
#define CRU_GATE(n)                     (0x0300 + 4 * (n))
#define  GATE20_VO                      0x03feu
#define  GATE20_DCLK_VOP(n)             (1u << (10 + (n)))
#define  GATE21_DSITX0                  (1u << 6)
#define  GATE31_PWM1                    ((1u << 11) | (1u << 10))
#define  GATE33_DSIPHY0                 (1u << 14)
#define CRU_SOFTRST(n)                  (0x0400 + 4 * (n))
#define  SOFTRST16_VO                   0x003fu
#define  SOFTRST16_DCLK_VOP1            (1u << 7)
#define  SOFTRST17_DSITX0               (1u << 0)
#define  SOFTRST23_PWM1                 ((1u << 1) | (1u << 0))
#define  SOFTRST27_DSIPHY0              (1u << 11)

#define PMUCRU_CLKSEL(n)                (0x0100 + 4 * (n))
#define  CLKSEL8_DSIPHY0_REF_OSC        (1u << 2)
#define PMUCRU_GATE(n)                  (0x0180 + 4 * (n))
#define  PMUGATE2_DSIPHY0_REF           (1u << 3)
#define PMUCRU_SOFTRST(n)               (0x0200 + 4 * (n))

#define PMU_BUS_IDLE_SFTCON0            0x0050
#define PMU_BUS_IDLE_ACK                0x0060
#define PMU_BUS_IDLE_ST                 0x0068
#define  BUS_IDLE_VO                    (1u << 4)
#define PMU_PWR_DWN_ST                  0x0098
#define PMU_PWR_GATE_SFTCON             0x00a0
#define  PWR_PD_VO                      (1u << 7)
#define PMU_PLLPD_CON                   0x00c0
#define PMU_PLLPD_SFTCON                0x00c4

#define GRF_IOMUX(bank, pin)            (0x20 * ((bank) - 1) + 4 * ((pin) / 4))
#define PMUGRF_IOMUX(pin)               (4 * ((pin) / 4))
#define  IOMUX_SHIFT(pin)               (4 * ((pin) % 4))
#define  IOMUX_FUNC                     7u
#define GRF_VO_CON0                     0x0360
#define  VO_CON0_DSI0                   0x0007u
#define GRF_VO_CON2                     0x0368
#define  VO_CON2_DSIPHY0                0x00f7u
#define GRF_SOC_STATUS0                 0x0580

#define GPIO_SWPORT_DR(pin)             ((pin) < 16 ? 0x0000u : 0x0004u)
#define GPIO_SWPORT_DDR(pin)            ((pin) < 16 ? 0x0008u : 0x000cu)
#define GPIO_EXT_PORT                   0x0070

#define PWM1_BASE                       0xfe6e0000ull
#define PWM_PERIOD                      0x04
#define PWM_DUTY                        0x08
#define PWM_CTRL                        0x0c
#define  PWM_CTRL_EN                    (1u << 0)
#define  PWM_CTRL_CONTINUOUS            (1u << 1)
#define  PWM_CTRL_DUTY_POSITIVE         (1u << 3)
#define  PWM_CTRL_INACTIVE_POSITIVE     (1u << 4)
#define PWM_CLOCK_KHZ                   24000u
#define BACKLIGHT_PERCENT               60u

#define DSI_PWR_UP                      0x0004
#define DSI_CLKMGR_CFG                  0x0008
#define DSI_DPI_VCID                    0x000c
#define DSI_DPI_COLOR_CODING            0x0010
#define  COLOR_CODING_24BIT             5u
#define DSI_DPI_CFG_POL                 0x0014
#define  CFG_POL_HSYNC_LOW              (1u << 2)
#define  CFG_POL_VSYNC_LOW              (1u << 1)
#define DSI_DPI_LP_CMD_TIM              0x0018
#define DSI_PCKHDL_CFG                  0x002c
#define  PCKHDL_CRC_RX                  (1u << 4)
#define  PCKHDL_ECC_RX                  (1u << 3)
#define  PCKHDL_BTA                     (1u << 2)
#define  PCKHDL_EOTP_TX                 (1u << 0)
#define DSI_GEN_VCID                    0x0030
#define DSI_MODE_CFG                    0x0034
#define  MODE_COMMAND                   1u
#define  MODE_VIDEO                     0u
#define DSI_VID_MODE_CFG                0x0038
#define  VID_VPG_EN                     (1u << 16)
#define  VID_LP_CMD_EN                  (1u << 15)
#define  VID_LP_ALL                     (0x3fu << 8)
#define  VID_SYNC_PULSES                0u
#define  VID_SYNC_EVENTS                1u
#define  VID_BURST                      2u
#define DSI_VID_PKT_SIZE                0x003c
#define DSI_VID_NUM_CHUNKS              0x0040
#define DSI_VID_NULL_SIZE               0x0044
#define DSI_VID_HSA_TIME                0x0048
#define DSI_VID_HBP_TIME                0x004c
#define DSI_VID_HLINE_TIME              0x0050
#define DSI_VID_VSA_LINES               0x0054
#define DSI_VID_VBP_LINES               0x0058
#define DSI_VID_VFP_LINES               0x005c
#define DSI_VID_VACTIVE_LINES           0x0060
#define DSI_CMD_MODE_CFG                0x0068
#define  CMD_MODE_ALL_LP                0x010f7f00u
#define DSI_GEN_HDR                     0x006c
#define DSI_GEN_PLD_DATA                0x0070
#define DSI_CMD_PKT_STATUS              0x0074
#define  PKT_GEN_RD_BUSY                (1u << 6)
#define  PKT_GEN_PLD_R_EMPTY            (1u << 4)
#define  PKT_GEN_PLD_W_FULL             (1u << 3)
#define  PKT_GEN_PLD_W_EMPTY            (1u << 2)
#define  PKT_GEN_CMD_FULL               (1u << 1)
#define  PKT_GEN_CMD_EMPTY              (1u << 0)
#define DSI_TO_CNT_CFG                  0x0078
#define DSI_HS_RD_TO_CNT                0x007c
#define DSI_LP_RD_TO_CNT                0x0080
#define DSI_HS_WR_TO_CNT                0x0084
#define DSI_LP_WR_TO_CNT                0x0088
#define DSI_BTA_TO_CNT                  0x008c
#define DSI_LPCLK_CTRL                  0x0094
#define  LPCLK_TXREQUESTCLKHS           (1u << 0)
#define DSI_PHY_TMR_LPCLK_CFG           0x0098
#define DSI_PHY_TMR_CFG                 0x009c
#define DSI_PHY_RSTZ                    0x00a0
#define  PHY_ENABLECLK                  (1u << 2)
#define  PHY_RSTZ                       (1u << 1)
#define  PHY_SHUTDOWNZ                  (1u << 0)
#define DSI_PHY_IF_CFG                  0x00a4
#define DSI_PHY_STATUS                  0x00b0
#define  PHY_STOP_CLK                   (1u << 2)
#define  PHY_LOCK                       (1u << 0)
#define DSI_INT_ST0                     0x00bc
#define DSI_INT_ST1                     0x00c0
#define DSI_VID_SHADOW_CTRL             0x0100

#define DSI_TO_CLK_DIVISION             10u
#define DSI_ESC_MAX_KHZ                 20000u
#define DSI_LP_CMD_BYTES                4u
#define DSI_HSTX_TO                     1000u
#define DSI_LPRX_TO                     1000u
#define DSI_BTA_TO                      3328u
#define DSI_CLK_HS2LP                   64u
#define DSI_CLK_LP2HS                   64u
#define DSI_HS2LP                       20u
#define DSI_LP2HS                       16u
#define DSI_MAX_RD_TIME                 10000u
#define DSI_STOP_WAIT                   32u

#define DT_GEN_LONG                     0x29
#define DT_DCS_LONG                     0x39
#define DT_DCS_SHORT_0                  0x05
#define DT_DCS_READ_0                   0x06
#define DT_MAX_RETURN_SIZE              0x37
#define DCS_GET_DISPLAY_ID              0x04
#define DCS_EXIT_SLEEP_MODE             0x11
#define DCS_SET_DISPLAY_ON              0x29
#define ID_BYTES                        3u

#define DPHY_REG(n)                     (4u * (n))
#define DPHY_LANES                      0x00
#define  LANES_CLK                      (1u << 6)
#define  LANES_DATA(n)                  (((1u << (n)) - 1) << 2)
#define  LANES_POWER_WORK               (1u << 0)
#define DPHY_PLL                        0x01
#define  PLL_RESERVED                   0xe0u
#define  PLL_SYNRST                     (1u << 2)
#define  PLL_LDO_PD                     (1u << 1)
#define  PLL_PD                         (1u << 0)
#define DPHY_PREDIV                     0x03
#define  PREDIV_RESERVED                0x80u
#define  PREDIV_FBDIV8                  (1u << 5)
#define DPHY_FBDIV                      0x04
#define DPHY_POSTDIV                    0x08
#define  POSTDIV_ON                     0x6eu
#define DPHY_DIGITAL                    0x20
#define  DIGITAL_RESERVED               0x1eu
#define  DIGITAL_RSTN                   (1u << 0)
#define DPHY_ANALOG_FIRST               0x02
#define DPHY_CLOCK_LANE                 0x40
#define DPHY_DATA_LANE(n)               (0x60 + 0x20 * (n))
#define DPHY_PLL_PREDIV                 4u
#define DPHY_PLL_FBDIV                  111u
#define DPHY_LANE_KBPS                  (24000u * DPHY_PLL_FBDIV / DPHY_PLL_PREDIV / 2)
#define DPHY_ANALOG_RESET_US            100

#define VOP2_REG_CFG_DONE               0x0000
#define  CFG_DONE_VP1                   ((1u << 4) | (1u << 1))
#define  CFG_DONE_GLOBAL_EN             (1u << 15)
#define VOP2_VERSION_INFO               0x0004
#define VOP2_AUTO_GATING                0x0008
#define  AUTO_GATING_EN                 (1u << 31)
#define VOP2_DSP_INFACE_EN              0x0028
#define  INFACE_EN_MIPI0_MUX            (3u << 16)
#define  INFACE_EN_MIPI0_MUX_VP1        (1u << 16)
#define  INFACE_EN_MIPI0                (1u << 4)
#define VOP2_DSP_INFACE_CTRL            0x002c
#define  INFACE_CTRL_REGDONE_IMD        (1u << 28)
#define VOP2_DSP_INFACE_POL             0x0030
#define  INFACE_POL_REGDONE_IMD         (1u << 28)
#define  INFACE_POL_MIPI                (0xfu << 16)
#define  INFACE_POL_MIPI_VSYNC          (1u << 17)
#define  INFACE_POL_MIPI_HSYNC          (1u << 16)
#define VOP2_OTP_WIN_EN                 0x0050
#define VOP2_STATUS(vp)                 (0x0060 + 4 * (vp))
#define VOP2_SYS0_INTR_EN               0x0080
#define VOP2_PORT1_INTR_CLR             0x00b4
#define VOP2_PORT1_INTR_RAW             0x00bc
#define  PORT_INTR_ALL                  0x02fdu
#define VOP2_OVL_CTRL                   0x0600
#define  OVL_CTRL_REGDONE_IMD           (1u << 28)
#define  OVL_CTRL_VP1_YUV               (1u << 1)
#define VOP2_OVL_LAYER_SEL              0x0604
#define VOP2_OVL_PORT_SEL               0x0608
#define VOP2_OVL_DP_BG_MIX(vp)          (0x06e0 + 4 * (vp))
#define  DP_BG_DLY                      38u
#define VOP2_VP(n)                      (0x0c00 + 0x100 * (n))
#define  VP_DSP_CTRL                    0x00
#define   DSP_CTRL_STANDBY              (1u << 31)
#define   DSP_CTRL_PRE_DITHER_DOWN      (1u << 16)
#define   DSP_CTRL_WIN_BYPASS           (1u << 15)
#define   DSP_CTRL_OUT_P888             0u
#define  VP_MIPI_CTRL                   0x04
#define  VP_COLOR_CTRL                  0x08
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
#define POWER_ON_US                     20000
#define RESET_US                        20000
#define RESET_DONE_US                   120000
#define SLEEP_OUT_US                    120000
#define DISPLAY_ON_US                   20000
#define MAX_PANELS                      4
#define LOG_MAX                         128

#define ISR_SERROR                      (1u << 8)

struct pin {
	uint64_t gpio;
	uint8_t bank, num;
	bool active_low, present;
};

struct panel_hw {
	uint64_t vop, cru, pmucru, pmu, grf, pmugrf, dsi, dphy, pwm;
	bool rk3566;
	struct fdt_node *panel;
	struct pin reset, power, bl_enable, bl_pin;
	uint8_t bl_func;
	uint32_t pwm_period_ns;
	bool pwm_inverted;
};

struct panel_state {
	uint32_t gate20, gate21, gate33, softrst16, softrst17, softrst27, pmugate2, idle_req;
	bool pd_vo_was_off;
};

struct panel_entry {
	const char *name;
	struct dsi_panel panel;
};

struct panel_clocks {
	uint32_t con0, con1, mode, clksel40;
};

struct panel_log {
	const struct panel_hw *hw;
	unsigned n;
	uint32_t cfg_done, done;
	bool lost;
	struct {
		uint64_t addr;
		uint32_t val, mask;
	} regs[LOG_MAX];
};

static struct panel_log *recording;

static const struct {
	uint32_t base;
	bool pmu;
	uint16_t gate, rst;
	uint8_t gate_bit, rst_bit;
} gpio_banks[5] = {
	{ 0xfdd60000, true, PMUCRU_GATE(1), PMUCRU_SOFTRST(0), 9, 9 },
	{ 0xfe740000, false, CRU_GATE(31), CRU_SOFTRST(21), 2, 10 },
	{ 0xfe750000, false, CRU_GATE(31), CRU_SOFTRST(21), 4, 12 },
	{ 0xfe760000, false, CRU_GATE(31), CRU_SOFTRST(21), 6, 14 },
	{ 0xfe770000, false, CRU_GATE(31), CRU_SOFTRST(22), 8, 0 },
};

static const struct {
	uint16_t base, cfg_done;
} windows[8] = {
	[VOP2_CLUSTER0] = { 0x1000, 1u << 6 },
	[VOP2_CLUSTER1] = { 0x1200, 1u << 7 },
	[VOP2_ESMART0] = { 0x1800, 1u << 10 },
	[VOP2_SMART0] = { 0x1c00, 1u << 12 },
	[VOP2_ESMART1] = { 0x1a00, 1u << 11 },
	[VOP2_SMART1] = { 0x1e00, 1u << 13 },
};

static const uint32_t phy_stop_lane[4] = { 1u << 4, 1u << 7, 1u << 9, 1u << 11 };

static const uint8_t dphy_analog[32] = {
	0x7d, 0xe0, 0xa0, 0x84, 0x6f, 0x08, 0x00, 0x88,
	0x6e, 0xdd, 0x84, 0x5f, 0xf0, 0xc0, 0x36, 0xdb,
	0x03, 0x80, 0x10, 0x00, 0x00, 0x03, 0xff, 0x42,
	0x01, 0xc0, 0xc0, 0x78, 0x43, 0x23, 0x21, 0x00,
};

static const uint8_t dphy_clock_lane[32] = {
	0x4a, 0x05, 0x00, 0x40, 0xb0, 0xc3, 0x7e, 0x19,
	0x04, 0x45, 0x0a, 0x0f, 0x8b, 0xff, 0x81, 0x10,
	0x04, 0x02, 0x05, 0x80, 0x00, 0x00, 0x01, 0x60,
	0x1b, 0x18, 0x7f, 0x4b, 0x00, 0x03, 0x00, 0x00,
};

static const uint8_t dphy_data_lane[32] = {
	0x4a, 0x05, 0x00, 0x40, 0xb0, 0xc3, 0x7e, 0x07,
	0x04, 0x45, 0x0f, 0x0f, 0x8b, 0xff, 0x86, 0x10,
	0x04, 0x02, 0x05, 0x80, 0x00, 0x00, 0x01, 0x60,
	0x1b, 0x18, 0x7f, 0x4b, 0x00, 0x03, 0x00, 0x00,
};

static uint32_t
rd(uint64_t addr)
{
#ifdef RKDSI_TEST
	return panel_test_read(addr);
#else
	return *(volatile uint32_t *)(uintptr_t)addr;
#endif
}

static bool
note(struct panel_log *l, uint64_t addr, uint32_t mask)
{
	for (unsigned i = 0; i < l->n; i++) {
		if (l->regs[i].addr == addr) {
			l->regs[i].mask |= mask;
			return true;
		}
	}
	if (l->n == LOG_MAX) {
		l->lost = true;
		return false;
	}
	l->regs[l->n].addr = addr;
	l->regs[l->n].val = rd(addr);
	l->regs[l->n++].mask = mask;
	return true;
}

static void
wr(uint64_t addr, uint32_t val)
{
	if (recording) {
		const struct panel_hw *h = recording->hw;
		uint64_t off = addr - h->vop;
		if (((off > VOP2_REG_CFG_DONE && off < VOP2_SYS0_INTR_EN) || (off >= VOP2_OVL_CTRL && off < VOP2_MMU(0)) ||
		    addr == h->pwm + PWM_CTRL || addr == h->pwm + PWM_PERIOD || addr == h->pwm + PWM_DUTY) &&
		    !note(recording, addr, 0)) {
			return;
		}
		if (off == VOP2_REG_CFG_DONE) {
			recording->done |= val & 0xffff;
		}
	}
#ifdef RKDSI_TEST
	panel_test_write(addr, val);
#else
	*(volatile uint32_t *)(uintptr_t)addr = val;
#endif
}

static void
wr_masked(uint64_t addr, uint32_t mask, uint32_t val)
{
	if (recording) {
		const struct panel_hw *h = recording->hw;
		if ((addr == h->pmucru + PMUCRU_CLKSEL(8) || addr == h->grf + GRF_VO_CON0 ||
		    addr == h->grf + GRF_VO_CON2 || addr == h->pmucru + PMUCRU_GATE(1) ||
		    addr == h->cru + CRU_GATE(31) || addr == h->cru + CRU_CLKSEL(72) ||
		    addr == h->cru + CRU_SOFTRST(23)) && !note(recording, addr, mask)) {
			return;
		}
	}
	wr(addr, mask << 16 | (val & mask));
}

static void
record(struct panel_log *l, const struct panel_hw *h)
{
	l->hw = h;
	l->n = 0;
	l->done = 0;
	l->lost = false;
	l->cfg_done = rd(h->vop + VOP2_REG_CFG_DONE) & 0xffff;
	recording = l;
}

static bool trace;

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
	printf("pieboot: panel: %s%s\n", line, (read_isr() & ISR_SERROR) ? "; an SError is pending" : "");
	dsb_sy();
}

static void
step_state(const struct panel_hw *h, const char *when)
{
	if (!trace) {
		return;
	}
	step("%s: power 0x%x, VO bus 0x%x 0x%x 0x%x; gates 0x%04x 0x%04x 0x%04x 0x%04x, resets 0x%04x 0x%04x 0x%04x",
	    when, rd(h->pmu + PMU_PWR_DWN_ST), rd(h->pmu + PMU_BUS_IDLE_SFTCON0), rd(h->pmu + PMU_BUS_IDLE_ACK),
	    rd(h->pmu + PMU_BUS_IDLE_ST), rd(h->cru + CRU_GATE(20)) & 0xffff, rd(h->cru + CRU_GATE(21)) & 0xffff,
	    rd(h->cru + CRU_GATE(33)) & 0xffff, rd(h->pmucru + PMUCRU_GATE(2)) & 0xffff,
	    rd(h->cru + CRU_SOFTRST(16)) & 0xffff, rd(h->cru + CRU_SOFTRST(17)) & 0xffff,
	    rd(h->cru + CRU_SOFTRST(27)) & 0xffff);
	step("%s: DCLK_VOP1 0x%04x, VPLL 0x%04x 0x%04x, mode 0x%04x, PWM1 clock 0x%04x, D-PHY reference 0x%04x", when,
	    rd(h->cru + CRU_CLKSEL(40)) & 0xffff, rd(h->cru + CRU_VPLL_CON(0)) & 0xffff,
	    rd(h->cru + CRU_VPLL_CON(1)) & 0xffff, rd(h->cru + CRU_MODE_CON00) & 0xffff,
	    rd(h->cru + CRU_CLKSEL(72)) & 0xffff, rd(h->pmucru + PMUCRU_CLKSEL(8)) & 0xffff);
	step("%s: VO clocks 0x%04x 0x%04x; PLL power 0x%04x 0x%04x, lock status 0x%08x", when,
	    rd(h->cru + CRU_CLKSEL(37)) & 0xffff, rd(h->cru + CRU_CLKSEL(38)) & 0xffff,
	    rd(h->pmu + PMU_PLLPD_CON) & 0xffff, rd(h->pmu + PMU_PLLPD_SFTCON) & 0xffff,
	    rd(h->grf + GRF_SOC_STATUS0));
}

static void
step_errors(const struct panel_hw *h, const char *when)
{
	if (trace) {
		step("%s: DSI errors 0x%06x 0x%05x", when, rd(h->dsi + DSI_INT_ST0), rd(h->dsi + DSI_INT_ST1));
	}
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

static void
put_back(struct panel_log *l)
{
	recording = NULL;
	const struct panel_hw *h = l->hw;
	while (l->n > 0) {
		unsigned i = --l->n;
		uint64_t addr = l->regs[i].addr;
		uint32_t val = l->regs[i].val, mask = l->regs[i].mask;
		if (mask) {
			wr_masked(addr, mask, val);
		} else {
			if (addr == h->vop + VOP2_VP(1) + VP_DSP_CTRL) {
				val |= DSP_CTRL_STANDBY;
			} else if (addr == h->vop + VOP2_DSP_INFACE_EN) {
				val &= ~INFACE_EN_MIPI0;
			}
			wr(addr, val);
		}
	}
	if (l->done) {
		wr(h->vop + VOP2_REG_CFG_DONE, (l->done | CFG_DONE_GLOBAL_EN) << 16 |
		    (l->done & ~CFG_DONE_GLOBAL_EN) | (l->cfg_done & CFG_DONE_GLOBAL_EN));
		udelay(50000);
	}
}

static bool
enabled(const struct fdt_node *n)
{
	struct fdt_prop *status = fdt_prop(n, "status");
	return !status || (status->len == 5 && !memcmp(status->val, "okay", 5)) ||
	    (status->len == 3 && !memcmp(status->val, "ok", 3));
}

static struct fdt_node *
find_enabled(struct fdt_node *n, const char *compat)
{
	if (!n) {
		return NULL;
	}
	if (fdt_is_compatible(n, compat) && enabled(n)) {
		return n;
	}
	for (struct fdt_node *c = n->child; c; c = c->sibling) {
		struct fdt_node *r = find_enabled(c, compat);
		if (r) {
			return r;
		}
	}
	return NULL;
}

static struct fdt_node *
find(const struct fdt *fdt, const char *compat, uint64_t *base)
{
	struct fdt_node *n = find_enabled(fdt->root, compat);
	uint64_t size;
	return n && fdt_reg(n, 0, base, &size) ? n : NULL;
}

static struct fdt_node *
phandle_prop(const struct fdt *fdt, const struct fdt_node *n, const char *name, unsigned index)
{
	struct fdt_prop *p = fdt_prop(n, name);
	if (!p || p->len < 4 * (index + 1)) {
		return NULL;
	}
	return fdt_find_phandle(fdt->root, be32(p->val + 4 * index));
}

static bool
reg_is(const struct fdt_node *n, uint32_t want)
{
	uint32_t r;
	return fdt_prop_u32(n, "reg", &r) && r == want;
}

static struct fdt_node *
child_reg(const struct fdt_node *n, uint32_t reg)
{
	for (struct fdt_node *c = n ? n->child : NULL; c; c = c->sibling) {
		if (reg_is(c, reg)) {
			return c;
		}
	}
	return NULL;
}

static bool
named(const struct fdt_node *n, const char *base)
{
	size_t len = strlen(base);
	return !strncmp(n->name, base, len) && (n->name[len] == 0 || n->name[len] == '@');
}

static unsigned
dsi_video_port(const struct fdt *fdt, const struct fdt_node *dsi, const struct fdt_node *vop)
{
	struct fdt_node *ports = NULL;
	for (struct fdt_node *c = dsi->child; c && !ports; c = c->sibling) {
		if (named(c, "ports")) {
			ports = c;
		}
	}
	struct fdt_node *port = child_reg(ports, 0);
	for (struct fdt_node *ep = port ? port->child : NULL; ep; ep = ep->sibling) {
		struct fdt_node *remote = named(ep, "endpoint") ? phandle_prop(fdt, ep, "remote-endpoint", 0) : NULL;
		struct fdt_node *vp = remote ? remote->parent : NULL;
		uint32_t n;
		if (vp && vp->parent && vp->parent->parent == vop && fdt_prop_u32(vp, "reg", &n) && n <= 2) {
			return n;
		}
	}
	return 3;
}

static struct fdt_node *
find_dsi(const struct fdt *fdt, struct fdt_node *n, const struct fdt_node *vop)
{
	if (!n) {
		return NULL;
	}
	if (fdt_is_compatible(n, "rockchip,rk3568-mipi-dsi") && enabled(n) && dsi_video_port(fdt, n, vop) == 1) {
		uint64_t base, size;
		if (fdt_reg(n, 0, &base, &size)) {
			if (base == 0xfe060000) {
				return n;
			}
			printf("pieboot: panel: DSI host 0x%lx is not DSI0 at 0xfe060000; left out\n", base);
		}
	}
	for (struct fdt_node *c = n->child; c; c = c->sibling) {
		struct fdt_node *r = find_dsi(fdt, c, vop);
		if (r) {
			return r;
		}
	}
	return NULL;
}

static bool
panel_find(const struct fdt *fdt, struct panel_hw *h)
{
	memset(h, 0, sizeof(*h));
	struct fdt_node *vop = find(fdt, "rockchip,rk3566-vop", &h->vop);
	h->rk3566 = vop != NULL;
	if (!vop) {
		vop = find(fdt, "rockchip,rk3568-vop", &h->vop);
	}
	if (!vop || !find(fdt, "rockchip,rk3568-cru", &h->cru) || !find(fdt, "rockchip,rk3568-pmucru", &h->pmucru) ||
	    !find(fdt, "rockchip,rk3568-pmu", &h->pmu) || !find(fdt, "rockchip,rk3568-grf", &h->grf) ||
	    !find(fdt, "rockchip,rk3568-pmugrf", &h->pmugrf)) {
		return false;
	}
	struct fdt_node *dsi = find_dsi(fdt, fdt->root, vop);
	uint64_t size;
	if (!dsi || !fdt_reg(dsi, 0, &h->dsi, &size)) {
		return false;
	}
	for (struct fdt_node *c = dsi->child; c && !h->panel; c = c->sibling) {
		if (reg_is(c, 0) && !named(c, "ports") && enabled(c)) {
			h->panel = c;
		}
	}
	struct fdt_node *phy = phandle_prop(fdt, dsi, "phys", 0);
	if (!h->panel || !phy || !fdt_is_compatible(phy, "rockchip,rk3568-dsi-dphy") || !enabled(phy) ||
	    !fdt_reg(phy, 0, &h->dphy, &size)) {
		return false;
	}
	if (h->dphy != 0xfe850000) {
		printf("pieboot: panel: D-PHY 0x%lx is not D-PHY0 at 0xfe850000; left out\n", h->dphy);
		return false;
	}
	return true;
}

static bool
gpio_spec(const struct fdt *fdt, const struct fdt_node *n, const char *name, struct pin *p)
{
	struct fdt_prop *prop = fdt_prop(n, name);
	p->present = false;
	if (!prop) {
		return true;
	}
	struct fdt_node *g = prop->len == 12 ? fdt_find_phandle(fdt->root, be32(prop->val)) : NULL;
	uint32_t cells, num = prop->len == 12 ? be32(prop->val + 4) : 32;
	uint64_t base, size;
	if (!g || !fdt_prop(g, "gpio-controller") || !fdt_prop_u32(g, "#gpio-cells", &cells) || cells != 2 ||
	    !fdt_reg(g, 0, &base, &size) || num >= 32) {
		return false;
	}
	for (unsigned b = 0; b < sizeof(gpio_banks) / sizeof(gpio_banks[0]); b++) {
		if (base == gpio_banks[b].base) {
			p->gpio = base;
			p->bank = (uint8_t)b;
			p->num = (uint8_t)num;
			p->active_low = (be32(prop->val + 8) & 1) != 0;
			p->present = true;
			return true;
		}
	}
	return false;
}

static const char *
panel_wiring(const struct fdt *fdt, struct panel_hw *h)
{
	if (!gpio_spec(fdt, h->panel, "reset-gpios", &h->reset) || !h->reset.present) {
		return "the panel has no reset GPIO pieboot can drive";
	}
	struct fdt_node *reg = phandle_prop(fdt, h->panel, "vdd-supply", 0);
	if (reg && (!fdt_is_compatible(reg, "regulator-fixed") ||
	    !gpio_spec(fdt, reg, fdt_prop(reg, "gpio") ? "gpio" : "gpios", &h->power))) {
		return "the panel's supply is not a fixed regulator on a GPIO pieboot can drive";
	}
	if (h->power.present) {
		h->power.active_low = !fdt_prop(reg, "enable-active-high");
	}
	struct fdt_node *bl = phandle_prop(fdt, h->panel, "backlight", 0);
	struct fdt_prop *pwms = bl ? fdt_prop(bl, "pwms") : NULL;
	struct fdt_node *pwm = pwms && pwms->len == 16 ? fdt_find_phandle(fdt->root, be32(pwms->val)) : NULL;
	uint64_t size;
	if (!bl || !fdt_is_compatible(bl, "pwm-backlight") || !pwm || !fdt_is_compatible(pwm, "rockchip,rk3328-pwm") ||
	    !fdt_reg(pwm, 0, &h->pwm, &size) || (h->pwm & ~0x30ull) != PWM1_BASE || be32(pwms->val + 4) != 0 ||
	    !be32(pwms->val + 8)) {
		return "the panel's backlight is not a PWM of PWM1";
	}
	h->pwm_period_ns = be32(pwms->val + 8);
	h->pwm_inverted = (be32(pwms->val + 12) & 1) != 0;
	if (!gpio_spec(fdt, bl, "enable-gpios", &h->bl_enable)) {
		return "the backlight's enable GPIO is not one pieboot can drive";
	}
	struct fdt_node *pins = phandle_prop(fdt, pwm, "pinctrl-0", 0);
	struct fdt_prop *rp = pins ? fdt_prop(pins, "rockchip,pins") : NULL;
	if (!rp || rp->len != 16 || be32(rp->val) >= 5 || be32(rp->val + 4) >= 32 || be32(rp->val + 8) > IOMUX_FUNC) {
		return "the backlight's PWM has no single pin in its pinctrl-0";
	}
	h->bl_pin.bank = (uint8_t)be32(rp->val);
	h->bl_pin.num = (uint8_t)be32(rp->val + 4);
	h->bl_pin.gpio = gpio_banks[h->bl_pin.bank].base;
	h->bl_pin.active_low = h->pwm_inverted;
	h->bl_pin.present = true;
	h->bl_func = (uint8_t)be32(rp->val + 8);
	return NULL;
}

static uint64_t
bank_clocks(const struct panel_hw *h, unsigned bank)
{
	return gpio_banks[bank].pmu ? h->pmucru : h->cru;
}

static void
iomux(const struct panel_hw *h, unsigned bank, unsigned pin, unsigned func)
{
	uint64_t a = bank == 0 ? h->pmugrf + PMUGRF_IOMUX(pin) : h->grf + GRF_IOMUX(bank, pin);
	wr_masked(a, IOMUX_FUNC << IOMUX_SHIFT(pin), func << IOMUX_SHIFT(pin));
}

static bool
bank_ready(const struct panel_hw *h, unsigned bank)
{
	uint64_t clk = bank_clocks(h, bank);
	if (rd(clk + gpio_banks[bank].rst) & (1u << gpio_banks[bank].rst_bit)) {
		return false;
	}
	uint32_t mask = 1u << gpio_banks[bank].gate_bit;
	wr_masked(clk + gpio_banks[bank].gate, mask, 0);
	return !(rd(clk + gpio_banks[bank].gate) & mask);
}

static bool
pin_out(const struct panel_hw *h, const struct pin *p, bool active)
{
	if (!p->present) {
		return true;
	}
	if (!bank_ready(h, p->bank)) {
		return false;
	}
	unsigned bit = p->num % 16;
	uint32_t level = active != p->active_low;
	wr(p->gpio + GPIO_SWPORT_DR(p->num), 1u << (bit + 16) | level << bit);
	wr(p->gpio + GPIO_SWPORT_DDR(p->num), 1u << (bit + 16) | 1u << bit);
	iomux(h, p->bank, p->num, 0);
	return true;
}

static bool
pin_active(const struct panel_hw *h, const struct pin *p)
{
	if (!p->present || !bank_ready(h, p->bank)) {
		return false;
	}
	return ((rd(p->gpio + GPIO_EXT_PORT) >> p->num) & 1) != p->active_low;
}

static bool
backlight_off(const struct panel_hw *h)
{
	bool enable = pin_out(h, &h->bl_enable, false);
	bool pwm = pin_out(h, &h->bl_pin, false);
	return enable && pwm;
}

static bool
backlight_on(const struct panel_hw *h)
{
	wr_masked(h->cru + CRU_CLKSEL(72), CLKSEL72_PWM1, CLKSEL72_PWM1_XIN24M);
	wr_masked(h->cru + CRU_GATE(31), GATE31_PWM1, 0);
	wr_masked(h->cru + CRU_SOFTRST(23), SOFTRST23_PWM1, 0);
	uint32_t period = (uint32_t)((uint64_t)h->pwm_period_ns * PWM_CLOCK_KHZ / 1000000);
	uint32_t pol = h->pwm_inverted ? PWM_CTRL_INACTIVE_POSITIVE : PWM_CTRL_DUTY_POSITIVE;
	wr(h->pwm + PWM_CTRL, pol | PWM_CTRL_CONTINUOUS);
	wr(h->pwm + PWM_PERIOD, period);
	wr(h->pwm + PWM_DUTY, period * BACKLIGHT_PERCENT / 100);
	wr(h->pwm + PWM_CTRL, pol | PWM_CTRL_CONTINUOUS | PWM_CTRL_EN);
	iomux(h, h->bl_pin.bank, h->bl_pin.num, h->bl_func);
	return pin_out(h, &h->bl_enable, true);
}

static bool
vo_on(const struct panel_hw *h, struct panel_state *s)
{
	s->gate20 = rd(h->cru + CRU_GATE(20)) & 0xffff;
	s->gate21 = rd(h->cru + CRU_GATE(21)) & 0xffff;
	s->gate33 = rd(h->cru + CRU_GATE(33)) & 0xffff;
	s->softrst16 = rd(h->cru + CRU_SOFTRST(16)) & 0xffff;
	s->softrst17 = rd(h->cru + CRU_SOFTRST(17)) & 0xffff;
	s->softrst27 = rd(h->cru + CRU_SOFTRST(27)) & 0xffff;
	s->pmugate2 = rd(h->pmucru + PMUCRU_GATE(2)) & 0xffff;
	s->idle_req = rd(h->pmu + PMU_BUS_IDLE_SFTCON0) & BUS_IDLE_VO;
	s->pd_vo_was_off = (rd(h->pmu + PMU_PWR_DWN_ST) & PWR_PD_VO) != 0;
	if (s->pd_vo_was_off) {
		wr_masked(h->pmu + PMU_PWR_GATE_SFTCON, PWR_PD_VO, 0);
		if (!wait_bits(h->pmu + PMU_PWR_DWN_ST, PWR_PD_VO, 0)) {
			wr_masked(h->pmu + PMU_PWR_GATE_SFTCON, PWR_PD_VO, PWR_PD_VO);
			printf("pieboot: panel: PD_VO does not power up\n");
			return false;
		}
	}
	wr_masked(h->cru + CRU_GATE(20), GATE20_VO | GATE20_DCLK_VOP(1), 0);
	wr_masked(h->cru + CRU_GATE(21), GATE21_DSITX0, 0);
	wr_masked(h->cru + CRU_GATE(33), GATE33_DSIPHY0, 0);
	wr_masked(h->pmucru + PMUCRU_GATE(2), PMUGATE2_DSIPHY0_REF, 0);
	if ((rd(h->pmu + PMU_BUS_IDLE_ST) | rd(h->pmu + PMU_BUS_IDLE_ACK)) & BUS_IDLE_VO) {
		wr_masked(h->pmu + PMU_BUS_IDLE_SFTCON0, BUS_IDLE_VO, 0);
		if (!wait_bits(h->pmu + PMU_BUS_IDLE_ACK, BUS_IDLE_VO, 0) ||
		    !wait_bits(h->pmu + PMU_BUS_IDLE_ST, BUS_IDLE_VO, 0)) {
			printf("pieboot: panel: the VO bus stays idle\n");
			return false;
		}
	}
	wr_masked(h->cru + CRU_SOFTRST(16), SOFTRST16_VO | SOFTRST16_DCLK_VOP1, 0);
	udelay(10);
	return true;
}

static void
vo_restore(const struct panel_hw *h, const struct panel_state *s)
{
	wr_masked(h->cru + CRU_SOFTRST(27), SOFTRST27_DSIPHY0, s->softrst27);
	wr_masked(h->cru + CRU_SOFTRST(17), SOFTRST17_DSITX0, s->softrst17);
	wr_masked(h->cru + CRU_SOFTRST(16), SOFTRST16_VO | SOFTRST16_DCLK_VOP1, s->softrst16);
	if (s->pd_vo_was_off || s->idle_req) {
		wr_masked(h->pmu + PMU_BUS_IDLE_SFTCON0, BUS_IDLE_VO, BUS_IDLE_VO);
		bool idle = wait_bits(h->pmu + PMU_BUS_IDLE_ACK, BUS_IDLE_VO, BUS_IDLE_VO) &&
		    wait_bits(h->pmu + PMU_BUS_IDLE_ST, BUS_IDLE_VO, BUS_IDLE_VO);
		if (s->pd_vo_was_off && idle) {
			wr_masked(h->pmu + PMU_PWR_GATE_SFTCON, PWR_PD_VO, PWR_PD_VO);
			wait_bits(h->pmu + PMU_PWR_DWN_ST, PWR_PD_VO, PWR_PD_VO);
		}
	}
	wr_masked(h->pmucru + PMUCRU_GATE(2), PMUGATE2_DSIPHY0_REF, s->pmugate2);
	wr_masked(h->cru + CRU_GATE(33), GATE33_DSIPHY0, s->gate33);
	wr_masked(h->cru + CRU_GATE(21), GATE21_DSITX0, s->gate21);
	wr_masked(h->cru + CRU_GATE(20), GATE20_VO | GATE20_DCLK_VOP(1), s->gate20);
}

static bool
vo_running(const struct panel_hw *h)
{
	return !(rd(h->pmu + PMU_PWR_DWN_ST) & PWR_PD_VO) && !(rd(h->pmu + PMU_BUS_IDLE_ST) & BUS_IDLE_VO) &&
	    !(rd(h->cru + CRU_GATE(20)) & GATE20_VO) && !(rd(h->cru + CRU_SOFTRST(16)) & SOFTRST16_VO);
}

static const char *
vpll_user(const struct panel_hw *h)
{
	if ((rd(h->cru + CRU_CLKSEL(38)) & CLKSEL38_ACLK_VOP_PRE) == CLKSEL38_ACLK_VOP_PRE_VPLL) {
		return "VPLL drives the VOP2's bus clock";
	}
	if ((rd(h->cru + CRU_CLKSEL(45)) & CLKSEL45_RKVENC_CORE) == CLKSEL45_RKVENC_CORE ||
	    (rd(h->cru + CRU_CLKSEL(48)) & CLKSEL48_RKVDEC_CA) == CLKSEL48_RKVDEC_CA ||
	    (rd(h->cru + CRU_CLKSEL(49)) & CLKSEL49_RKVDEC_CORE) == CLKSEL49_RKVDEC_CORE ||
	    (rd(h->cru + CRU_CLKSEL(49)) & CLKSEL49_RKVDEC_HEVC_CA) == CLKSEL49_RKVDEC_HEVC_CA) {
		return "VPLL drives the video codecs";
	}
	uint32_t gate20 = rd(h->cru + CRU_GATE(20));
	for (unsigned vp = 0; vp <= 2; vp += 2) {
		if ((rd(h->cru + CRU_CLKSEL(39 + vp)) & DCLK_VOP_SEL) == DCLK_VOP_SEL_VPLL &&
		    !(gate20 & GATE20_DCLK_VOP(vp)) && !(rd(h->vop + VOP2_VP(vp) + VP_DSP_CTRL) & DSP_CTRL_STANDBY)) {
			return vp == 0 ? "VPLL drives video port 0" : "VPLL drives video port 2";
		}
	}
	return NULL;
}

static void
vpll_restore(const struct panel_hw *h, const struct panel_clocks *c)
{
	const uint32_t con0_mask = VPLL_CON0_BYPASS | VPLL_CON0_POSTDIV1 | VPLL_CON0_FBDIV;
	const uint32_t con1_mask = VPLL_CON1_PD | VPLL_CON1_DSMPD | VPLL_CON1_POSTDIV2 | VPLL_CON1_REFDIV;
	wr_masked(h->cru + CRU_MODE_CON00, MODE_VPLL, MODE_VPLL_XIN24M);
	wr_masked(h->cru + CRU_VPLL_CON(0), con0_mask, c->con0);
	wr_masked(h->cru + CRU_VPLL_CON(1), con1_mask, c->con1);
	if ((c->mode & MODE_VPLL) == MODE_VPLL_PLL) {
		wait_bits(h->cru + CRU_VPLL_CON(1), VPLL_CON1_LOCK, VPLL_CON1_LOCK);
	}
	wr_masked(h->cru + CRU_MODE_CON00, MODE_VPLL, c->mode);
}

static void
clocks_restore(const struct panel_hw *h, const struct panel_clocks *c)
{
	wr_masked(h->cru + CRU_GATE(20), GATE20_DCLK_VOP(1), GATE20_DCLK_VOP(1));
	vpll_restore(h, c);
	wr_masked(h->cru + CRU_CLKSEL(40), DCLK_VOP_SEL | DCLK_VOP_DIV, c->clksel40);
}

static bool
pixel_clock(const struct panel_hw *h, const struct rk_pll *pll, struct panel_clocks *c)
{
	c->con0 = rd(h->cru + CRU_VPLL_CON(0)) & 0xffff;
	c->con1 = rd(h->cru + CRU_VPLL_CON(1)) & 0xffff;
	c->mode = rd(h->cru + CRU_MODE_CON00) & 0xffff;
	c->clksel40 = rd(h->cru + CRU_CLKSEL(40)) & 0xffff;
	const uint32_t con0_mask = VPLL_CON0_BYPASS | VPLL_CON0_POSTDIV1 | VPLL_CON0_FBDIV;
	const uint32_t con1_mask = VPLL_CON1_PD | VPLL_CON1_DSMPD | VPLL_CON1_POSTDIV2 | VPLL_CON1_REFDIV;

	wr_masked(h->cru + CRU_MODE_CON00, MODE_VPLL, MODE_VPLL_XIN24M);
	wr_masked(h->cru + CRU_VPLL_CON(0), con0_mask, (uint32_t)pll->postdiv1 << 12 | pll->fbdiv);
	wr_masked(h->cru + CRU_VPLL_CON(1), con1_mask,
	    VPLL_CON1_DSMPD | (uint32_t)pll->postdiv2 << 6 | pll->refdiv);
	if (!wait_bits(h->cru + CRU_VPLL_CON(1), VPLL_CON1_LOCK, VPLL_CON1_LOCK)) {
		vpll_restore(h, c);
		return false;
	}
	wr_masked(h->cru + CRU_MODE_CON00, MODE_VPLL, MODE_VPLL_PLL);
	wr_masked(h->cru + CRU_CLKSEL(40), DCLK_VOP_SEL | DCLK_VOP_DIV, DCLK_VOP_SEL_VPLL | (uint32_t)(pll->div - 1));
	return true;
}

static void
dphy_wr(const struct panel_hw *h, unsigned reg, uint32_t val)
{
	wr(h->dphy + DPHY_REG(reg), val & 0xff);
}

static void
step_dphy(const struct panel_hw *h)
{
	if (!trace) {
		return;
	}
	for (unsigned base = 0; base < 0x80; base += 0x20) {
		char hex[3 * 0x20 + 1];
		for (unsigned i = 0; i < 0x20; i++) {
			snprintf(hex + 3 * i, 4, " %02x", rd(h->dphy + DPHY_REG(base + i)) & 0xff);
		}
		step("D-PHY 0x%02x:%s", base, hex);
	}
}

static bool
link_up(const struct panel_hw *h, const struct dsi_panel *p)
{
	uint64_t dsi = h->dsi;
	uint32_t esc_div = (DPHY_LANE_KBPS / 8 + DSI_ESC_MAX_KHZ - 1) / DSI_ESC_MAX_KHZ;

	wr_masked(h->cru + CRU_SOFTRST(17), SOFTRST17_DSITX0, SOFTRST17_DSITX0);
	wr_masked(h->cru + CRU_SOFTRST(27), SOFTRST27_DSIPHY0, SOFTRST27_DSIPHY0);
	udelay(10);
	wr_masked(h->cru + CRU_SOFTRST(17), SOFTRST17_DSITX0, 0);
	wr_masked(h->cru + CRU_SOFTRST(27), SOFTRST27_DSIPHY0, 0);
	udelay(10);
	wr_masked(h->grf + GRF_VO_CON0, VO_CON0_DSI0, 0);
	wr_masked(h->grf + GRF_VO_CON2, VO_CON2_DSIPHY0, 0);
	wr_masked(h->pmucru + PMUCRU_CLKSEL(8), CLKSEL8_DSIPHY0_REF_OSC, CLKSEL8_DSIPHY0_REF_OSC);

	wr(dsi + DSI_PWR_UP, 0);
	wr(dsi + DSI_PHY_RSTZ, 0);
	wr(dsi + DSI_CLKMGR_CFG, DSI_TO_CLK_DIVISION << 8 | esc_div);
	wr(dsi + DSI_DPI_LP_CMD_TIM, DSI_LP_CMD_BYTES << 16 | DSI_LP_CMD_BYTES);
	wr(dsi + DSI_PCKHDL_CFG, PCKHDL_CRC_RX | PCKHDL_ECC_RX | PCKHDL_BTA | (p->no_eot ? 0 : PCKHDL_EOTP_TX));
	wr(dsi + DSI_GEN_VCID, 0);
	wr(dsi + DSI_MODE_CFG, MODE_COMMAND);
	wr(dsi + DSI_CMD_MODE_CFG, p->lp_commands ? CMD_MODE_ALL_LP : 0);
	wr(dsi + DSI_TO_CNT_CFG, DSI_HSTX_TO << 16 | DSI_LPRX_TO);
	wr(dsi + DSI_HS_RD_TO_CNT, 0);
	wr(dsi + DSI_LP_RD_TO_CNT, 0);
	wr(dsi + DSI_HS_WR_TO_CNT, 0);
	wr(dsi + DSI_LP_WR_TO_CNT, 0);
	wr(dsi + DSI_BTA_TO_CNT, DSI_BTA_TO);
	wr(dsi + DSI_LPCLK_CTRL, 0);
	wr(dsi + DSI_PHY_TMR_LPCLK_CFG, DSI_CLK_HS2LP << 16 | DSI_CLK_LP2HS);
	wr(dsi + DSI_PHY_TMR_CFG, DSI_HS2LP << 24 | DSI_LP2HS << 16 | DSI_MAX_RD_TIME);
	wr(dsi + DSI_PHY_IF_CFG, DSI_STOP_WAIT << 8 | (p->lanes - 1));
	wr(dsi + DSI_PHY_RSTZ, PHY_ENABLECLK);

	dphy_wr(h, DPHY_PREDIV, PREDIV_RESERVED | ((DPHY_PLL_FBDIV >> 8) ? PREDIV_FBDIV8 : 0) | DPHY_PLL_PREDIV);
	dphy_wr(h, DPHY_FBDIV, DPHY_PLL_FBDIV & 0xff);
	dphy_wr(h, DPHY_POSTDIV, POSTDIV_ON);
	for (unsigned reg = DPHY_ANALOG_FIRST; reg < sizeof(dphy_analog); reg++) {
		if (reg != DPHY_PREDIV && reg != DPHY_FBDIV && reg != DPHY_POSTDIV) {
			dphy_wr(h, reg, dphy_analog[reg]);
		}
	}
	dphy_wr(h, DPHY_PLL, PLL_RESERVED | PLL_SYNRST);
	dphy_wr(h, DPHY_LANES, LANES_CLK | LANES_DATA(p->lanes) | LANES_POWER_WORK);
	dphy_wr(h, DPHY_PLL, PLL_RESERVED);
	udelay(DPHY_ANALOG_RESET_US);
	dphy_wr(h, DPHY_DIGITAL, DIGITAL_RESERVED);
	dphy_wr(h, DPHY_DIGITAL, DIGITAL_RESERVED | DIGITAL_RSTN);
	for (unsigned i = 0; i < sizeof(dphy_clock_lane); i++) {
		dphy_wr(h, DPHY_CLOCK_LANE + i, dphy_clock_lane[i]);
		for (unsigned n = 0; n < p->lanes; n++) {
			dphy_wr(h, DPHY_DATA_LANE(n) + i, dphy_data_lane[i]);
		}
	}
	step_dphy(h);

	wr(dsi + DSI_PHY_RSTZ, PHY_ENABLECLK | PHY_RSTZ | PHY_SHUTDOWNZ);
	uint32_t stop = PHY_STOP_CLK;
	for (unsigned i = 0; i < p->lanes; i++) {
		stop |= phy_stop_lane[i];
	}
	if (!wait_bits(dsi + DSI_PHY_STATUS, PHY_LOCK, PHY_LOCK) || !wait_bits(dsi + DSI_PHY_STATUS, stop, stop)) {
		printf("pieboot: panel: the D-PHY does not lock or stop, status 0x%x\n", rd(dsi + DSI_PHY_STATUS));
		return false;
	}
	wr(dsi + DSI_PWR_UP, 1);
	return true;
}

static void
link_down(const struct panel_hw *h)
{
	wr(h->dsi + DSI_PWR_UP, 0);
	wr(h->dsi + DSI_PHY_RSTZ, 0);
}

static bool
packet(const struct panel_hw *h, uint8_t type, const uint8_t *data, unsigned len)
{
	uint64_t dsi = h->dsi;
	uint32_t hdr;
	if (type == DT_GEN_LONG || type == DT_DCS_LONG) {
		for (unsigned i = 0; i < len; i += 4) {
			uint32_t word = 0;
			for (unsigned b = 0; b < 4 && i + b < len; b++) {
				word |= (uint32_t)data[i + b] << (8 * b);
			}
			if (!wait_bits(dsi + DSI_CMD_PKT_STATUS, PKT_GEN_PLD_W_FULL, 0)) {
				return false;
			}
			wr(dsi + DSI_GEN_PLD_DATA, word);
		}
		hdr = (len >> 8) << 16 | (len & 0xff) << 8 | type;
	} else {
		hdr = (len > 1 ? (uint32_t)data[1] << 16 : 0) | (len > 0 ? (uint32_t)data[0] << 8 : 0) | type;
	}
	if (!wait_bits(dsi + DSI_CMD_PKT_STATUS, PKT_GEN_CMD_FULL, 0)) {
		return false;
	}
	wr(dsi + DSI_GEN_HDR, hdr);
	return wait_bits(dsi + DSI_CMD_PKT_STATUS, PKT_GEN_CMD_EMPTY | PKT_GEN_PLD_W_EMPTY,
	    PKT_GEN_CMD_EMPTY | PKT_GEN_PLD_W_EMPTY);
}

static bool
dcs(const struct panel_hw *h, uint8_t cmd)
{
	return packet(h, DT_DCS_SHORT_0, &cmd, 1);
}

static unsigned
dcs_read(const struct panel_hw *h, uint8_t cmd, uint8_t *buf, unsigned max)
{
	uint64_t dsi = h->dsi;
	for (unsigned i = 0; i < 64 && !(rd(dsi + DSI_CMD_PKT_STATUS) & PKT_GEN_PLD_R_EMPTY); i++) {
		rd(dsi + DSI_GEN_PLD_DATA);
	}
	uint8_t size[2] = { (uint8_t)max, 0 };
	if (!packet(h, DT_MAX_RETURN_SIZE, size, 2) || !packet(h, DT_DCS_READ_0, &cmd, 1) ||
	    !wait_bits(dsi + DSI_CMD_PKT_STATUS, PKT_GEN_RD_BUSY, 0)) {
		return 0;
	}
	unsigned n = 0;
	while (n < max && !(rd(dsi + DSI_CMD_PKT_STATUS) & PKT_GEN_PLD_R_EMPTY)) {
		uint32_t word = rd(dsi + DSI_GEN_PLD_DATA);
		for (unsigned b = 0; b < 4 && n < max; b++) {
			buf[n++] = (uint8_t)(word >> (8 * b));
		}
	}
	return n;
}

static bool
send_init(const struct panel_hw *h, const struct dsi_panel *p, unsigned *sent)
{
	struct dsi_packet pkt;
	size_t off = 0;
	*sent = 0;
	while (dsi_packet_next(p, &off, &pkt)) {
		if (!packet(h, pkt.type, pkt.data, pkt.len)) {
			return false;
		}
		(*sent)++;
	}
	return true;
}

static void
video_mode(const struct panel_hw *h, const struct dsi_panel *p, const struct dsi_timing *t, bool pattern)
{
	uint64_t dsi = h->dsi;
	uint32_t type = p->burst ? VID_BURST : p->sync_pulses ? VID_SYNC_PULSES : VID_SYNC_EVENTS;
	wr(dsi + DSI_DPI_VCID, 0);
	wr(dsi + DSI_DPI_COLOR_CODING, COLOR_CODING_24BIT);
	wr(dsi + DSI_DPI_CFG_POL, (p->mode.hsync_low ? CFG_POL_HSYNC_LOW : 0) | (p->mode.vsync_low ? CFG_POL_VSYNC_LOW : 0));
	wr(dsi + DSI_VID_PKT_SIZE, p->mode.hdisplay);
	wr(dsi + DSI_VID_NUM_CHUNKS, 0);
	wr(dsi + DSI_VID_NULL_SIZE, 0);
	wr(dsi + DSI_VID_HSA_TIME, t->hsa);
	wr(dsi + DSI_VID_HBP_TIME, t->hbp);
	wr(dsi + DSI_VID_HLINE_TIME, t->hline);
	wr(dsi + DSI_VID_VSA_LINES, t->vsa);
	wr(dsi + DSI_VID_VBP_LINES, t->vbp);
	wr(dsi + DSI_VID_VFP_LINES, t->vfp);
	wr(dsi + DSI_VID_VACTIVE_LINES, t->vactive);
	wr(dsi + DSI_VID_MODE_CFG, VID_LP_ALL | (p->lp_commands ? VID_LP_CMD_EN : 0) | type | (pattern ? VID_VPG_EN : 0));
	wr(dsi + DSI_LPCLK_CTRL, LPCLK_TXREQUESTCLKHS);
	wr(dsi + DSI_MODE_CFG, MODE_VIDEO);
	dsb_sy();
}

static bool
win_cluster(unsigned win)
{
	return win == VOP2_CLUSTER0 || win == VOP2_CLUSTER1;
}

static bool
win_on(uint64_t vop, unsigned win)
{
	uint64_t w = vop + windows[win].base;
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
	uint64_t w = vop + windows[win].base;
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

static void
vp1_timing(uint64_t vp, const struct dsi_mode *m)
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
	    (uint32_t)(m->hdisplay / 2 + DP_BG_DLY - 1) << 16 | (uint32_t)(m->hsync_end - m->hsync_start));
	wr(vp + VP_DSP_BG, 0);
	wr(vp + VP_BCSH_CTRL, 0);
}

static void
window(uint64_t vop, uint64_t fb, const struct dsi_mode *m, bool on)
{
	uint64_t w = vop + windows[VOP2_SMART0].base;
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
vp1_running(uint64_t vop, const char *when)
{
	uint32_t a = rd(vop + VOP2_STATUS(1));
	udelay(1000);
	uint32_t b = rd(vop + VOP2_STATUS(1));
	step("%s: video port 1 status 0x%08x to 0x%08x in 1 ms", when, a, b);
	return ((b >> 16) & 0x1fff) != ((a >> 16) & 0x1fff);
}

static bool
vp1_wait_running(uint64_t vop, const struct dsi_mode *m)
{
	uint64_t frame_us = ((uint64_t)m->htotal * m->vtotal * 1000 + m->clock_khz - 1) / m->clock_khz;
	uint64_t sample_us = MAX(1, MIN(1000, frame_us / 16));
	uint64_t until = read_cntpct() + read_cntfrq() * 3 * frame_us / 1000000;
	uint32_t line = (rd(vop + VOP2_STATUS(1)) >> 16) & 0x1fff;
	while (read_cntpct() < until) {
		udelay(sample_us);
		if (((rd(vop + VOP2_STATUS(1)) >> 16) & 0x1fff) != line) {
			return true;
		}
	}
	return false;
}

static void
vp1_standby(const struct panel_hw *h)
{
	uint64_t vp = h->vop + VOP2_VP(1);
	uint32_t ctrl = rd(vp + VP_DSP_CTRL);
	if (!(ctrl & DSP_CTRL_STANDBY)) {
		wr(vp + VP_DSP_CTRL, ctrl | DSP_CTRL_STANDBY);
		udelay(50000);
	}
}

static bool
vp1_configure(const struct panel_hw *h, const struct dsi_mode *m, uint64_t fb, bool window_on, uint32_t *ctrl)
{
	uint64_t vop = h->vop, vp = vop + VOP2_VP(1);
	uint32_t layer_sel = rd(vop + VOP2_OVL_LAYER_SEL), port_sel = rd(vop + VOP2_OVL_PORT_SEL);
	struct vop2_ovl ovl;
	if (!vop2_overlay_vp(layer_sel, port_sel, 1, VOP2_SMART0, &ovl)) {
		printf("pieboot: panel: the overlay leaves no layer for video port 0 (layers 0x%08x, ports 0x%08x)\n",
		    layer_sel, port_sel);
		return false;
	}
	uint32_t done = CFG_DONE_GLOBAL_EN | CFG_DONE_VP1 | windows[VOP2_SMART0].cfg_done;
	for (unsigned w = 0; w < 8; w++) {
		if ((ovl.off & (1u << w)) && win_on(vop, w)) {
			win_off(vop, w);
			done |= windows[w].cfg_done;
		}
	}
	if (h->rk3566) {
		wr(vop + VOP2_OTP_WIN_EN, 1);
	}
	wr(vop + VOP2_AUTO_GATING, rd(vop + VOP2_AUTO_GATING) & ~AUTO_GATING_EN);
	window(vop, fb, m, window_on);
	wr(vop + VOP2_OVL_CTRL, (rd(vop + VOP2_OVL_CTRL) & ~OVL_CTRL_VP1_YUV) | OVL_CTRL_REGDONE_IMD);
	if (ovl.layer_sel != layer_sel) {
		wr(vop + VOP2_OVL_LAYER_SEL, ovl.layer_sel);
	}
	if (ovl.port_sel != port_sel) {
		wr(vop + VOP2_OVL_PORT_SEL, ovl.port_sel);
	}
	wr(vop + VOP2_OVL_DP_BG_MIX(1), DP_BG_DLY << 24);
	vp1_timing(vp, m);
	wr(vp + VP_COLOR_CTRL, 0);
	wr(vp + VP_MIPI_CTRL, 0);
	uint32_t dsp_ctrl = DSP_CTRL_OUT_P888 | DSP_CTRL_PRE_DITHER_DOWN | (window_on ? 0 : DSP_CTRL_WIN_BYPASS);
	wr(vp + VP_DSP_CTRL, DSP_CTRL_STANDBY | dsp_ctrl);
	wr(vop + VOP2_DSP_INFACE_CTRL, rd(vop + VOP2_DSP_INFACE_CTRL) | INFACE_CTRL_REGDONE_IMD);
	wr(vop + VOP2_DSP_INFACE_EN,
	    (rd(vop + VOP2_DSP_INFACE_EN) & ~INFACE_EN_MIPI0_MUX) | INFACE_EN_MIPI0_MUX_VP1 | INFACE_EN_MIPI0);
	wr(vop + VOP2_DSP_INFACE_POL, (rd(vop + VOP2_DSP_INFACE_POL) & ~INFACE_POL_MIPI) | INFACE_POL_REGDONE_IMD |
	    (m->hsync_low ? 0 : INFACE_POL_MIPI_HSYNC) | (m->vsync_low ? 0 : INFACE_POL_MIPI_VSYNC));
	wr(vop + VOP2_REG_CFG_DONE, done << 16 | done);
	*ctrl = dsp_ctrl;
	return true;
}

static void
vp1_start(const struct panel_hw *h, uint32_t ctrl)
{
	wr(h->vop + VOP2_VP(1) + VP_DSP_CTRL, ctrl);
	udelay(20000);
}

static void
panel_dark(const struct panel_hw *h, bool vop, bool dsi)
{
	if (!backlight_off(h)) {
		printf("pieboot: panel: cannot force the backlight off\n");
	}
	if (vop) {
		vp1_standby(h);
		wr(h->vop + VOP2_DSP_INFACE_EN, rd(h->vop + VOP2_DSP_INFACE_EN) & ~INFACE_EN_MIPI0);
	}
	if (dsi) {
		link_down(h);
	}
	pin_out(h, &h->reset, true);
}

static void
step_status(const struct panel_hw *h, const char *when)
{
	if (!trace) {
		return;
	}
	step_errors(h, "video mode");
	wr(h->vop + VOP2_PORT1_INTR_CLR, PORT_INTR_ALL << 16 | PORT_INTR_ALL);
	udelay(50000);
	uint64_t w = h->vop + windows[VOP2_SMART0].base;
	uint64_t vp = h->vop + VOP2_VP(1);
	step_state(h, when);
	step("%s: line %u, video port 1 raw status 0x%03x; done 0x%04x, OTP_WIN_EN %u, gating 0x%08x; smart0 0x%x at "
	    "0x%08x; interface 0x%x 0x%08x", when, (rd(h->vop + VOP2_STATUS(1)) >> 16) & 0x1fff,
	    rd(h->vop + VOP2_PORT1_INTR_RAW) & 0xffff, rd(h->vop + VOP2_REG_CFG_DONE) & 0xffff,
	    rd(h->vop + VOP2_OTP_WIN_EN) & 1, rd(h->vop + VOP2_AUTO_GATING), rd(w + WIN_REGION_MST_CTL(0)),
	    rd(w + WIN_REGION0_MST_YRGB), rd(h->vop + VOP2_DSP_INFACE_EN), rd(h->vop + VOP2_DSP_INFACE_POL));
	step("%s: video port 1 control 0x%08x, MIPI 0x%08x, colour 0x%08x; interface control 0x%08x", when,
	    rd(vp + VP_DSP_CTRL), rd(vp + VP_MIPI_CTRL), rd(vp + VP_COLOR_CTRL), rd(h->vop + VOP2_DSP_INFACE_CTRL));
	step("%s: video port 1 timing H 0x%08x 0x%08x, V 0x%08x 0x%08x", when,
	    rd(vp + VP_HTOTAL_HS_END), rd(vp + VP_HACT_ST_END), rd(vp + VP_VTOTAL_VS_END), rd(vp + VP_VACT_ST_END));
	step("%s: video port 1 post H 0x%08x, V 0x%08x, scale 0x%08x 0x%08x; pre-scan 0x%08x, BG 0x%08x, BCSH 0x%08x",
	    when, rd(vp + VP_POST_HACT_INFO), rd(vp + VP_POST_VACT_INFO), rd(vp + VP_POST_SCL_FACTOR),
	    rd(vp + VP_POST_SCL_CTRL), rd(vp + VP_PRE_SCAN_HTIMING), rd(vp + VP_DSP_BG), rd(vp + VP_BCSH_CTRL));
	step("%s: overlay control 0x%08x, layers 0x%08x, ports 0x%08x, BG 0x%08x", when,
	    rd(h->vop + VOP2_OVL_CTRL), rd(h->vop + VOP2_OVL_LAYER_SEL), rd(h->vop + VOP2_OVL_PORT_SEL),
	    rd(h->vop + VOP2_OVL_DP_BG_MIX(1)));
	step("%s: DSI power 0x%x, LP clock 0x%x, DPI colour 0x%x, polarity 0x%x, shadow 0x%x; VO control 0x%04x 0x%04x", when,
	    rd(h->dsi + DSI_PWR_UP), rd(h->dsi + DSI_LPCLK_CTRL), rd(h->dsi + DSI_DPI_COLOR_CODING),
	    rd(h->dsi + DSI_DPI_CFG_POL), rd(h->dsi + DSI_VID_SHADOW_CTRL), rd(h->grf + GRF_VO_CON0) & 0xffff,
	    rd(h->grf + GRF_VO_CON2) & 0xffff);
	step("%s: DSI horizontal %u %u %u, vertical %u %u %u %u, pixels %u", when,
	    rd(h->dsi + DSI_VID_HSA_TIME), rd(h->dsi + DSI_VID_HBP_TIME), rd(h->dsi + DSI_VID_HLINE_TIME),
	    rd(h->dsi + DSI_VID_VSA_LINES), rd(h->dsi + DSI_VID_VBP_LINES), rd(h->dsi + DSI_VID_VFP_LINES),
	    rd(h->dsi + DSI_VID_VACTIVE_LINES), rd(h->dsi + DSI_VID_PKT_SIZE));
	step("%s: DSI chunks %u, null bytes %u, clock dividers 0x%04x, PHY interface 0x%04x", when,
	    rd(h->dsi + DSI_VID_NUM_CHUNKS), rd(h->dsi + DSI_VID_NULL_SIZE), rd(h->dsi + DSI_CLKMGR_CFG),
	    rd(h->dsi + DSI_PHY_IF_CFG));
	step("%s: DSI mode 0x%x, video 0x%08x, PHY status 0x%03x, packets 0x%04x, errors 0x%06x 0x%05x", when,
	    rd(h->dsi + DSI_MODE_CFG), rd(h->dsi + DSI_VID_MODE_CFG), rd(h->dsi + DSI_PHY_STATUS),
	    rd(h->dsi + DSI_CMD_PKT_STATUS), rd(h->dsi + DSI_INT_ST0), rd(h->dsi + DSI_INT_ST1));
}

static struct fdt_node *
chosen_node(const struct fdt *fdt)
{
	struct fdt_node *chosen = fdt_path(fdt, "/chosen");
	unsigned ac = fdt_address_cells(chosen), sc = fdt_size_cells(chosen);
	return chosen && ac >= 1 && ac <= 2 && sc >= 1 && sc <= 2 ? chosen : NULL;
}

static void
add_node(struct fdt_node *chosen, uint64_t base, const struct dsi_mode *m)
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

static struct dsi_prop
prop(const struct fdt_node *n, const char *name)
{
	struct fdt_prop *p = fdt_prop(n, name);
	return (struct dsi_prop){ p ? p->val : NULL, p ? p->len : 0 };
}

static unsigned
panel_entries(const struct fdt_node *panel, struct panel_entry *e, unsigned max)
{
	unsigned n = 0;
	for (struct fdt_node *c = panel->child; c; c = c->sibling) {
		if (strncmp(c->name, "pie,panel-", 10) || !enabled(c)) {
			continue;
		}
		struct dsi_props p = {
			prop(c, "pie,id"), prop(c, "pie,modes"), prop(c, "pie,mode-flags"), prop(c, "pie,dsi-lanes"),
			prop(c, "pie,dsi-format"), prop(c, "pie,dsi-flags"), prop(c, "pie,bus-flags"), prop(c, "pie,init"),
		};
		const char *why;
		struct dsi_panel data;
		if (!dsi_panel_parse(&p, &data, &why)) {
			printf("pieboot: panel: %s: %s; left out\n", c->name, why);
			continue;
		}
		struct dsi_timing timing;
		if (!dsi_timing(&data.mode, data.lanes, DPHY_LANE_KBPS, &timing)) {
			printf("pieboot: panel: %s: the link cannot drive the mode; left out\n", c->name);
			continue;
		}
		struct rk_pll pll;
		if (!rk_pll_exact(data.mode.clock_khz, &pll)) {
			printf("pieboot: panel: %s: no VPLL setting gives the pixel clock; left out\n", c->name);
			continue;
		}
		if (n == max) {
			printf("pieboot: panel: %s: limit of %u usable panel nodes; left out\n", c->name, max);
			continue;
		}
		e[n++] = (struct panel_entry){ c->name, data };
	}
	return n;
}

static bool
word(const char *p, size_t len, const char *w)
{
	return strlen(w) == len && !strncmp(p, w, len);
}

static bool
panel_options(const char *opts, bool *dsibars, bool *pattern)
{
	*dsibars = *pattern = false;
	for (const char *p = opts; p && *p;) {
		const char *e = p;
		while (*e && *e != ',') {
			e++;
		}
		size_t len = (size_t)(e - p);
		if (word(p, len, "off")) {
			printf("pieboot: panel: off, as pieboot.panel asks\n");
			return false;
		} else if (word(p, len, "dsibars")) {
			*dsibars = true;
		} else if (word(p, len, "pattern")) {
			*pattern = true;
		} else {
			printf("pieboot: panel: pieboot.panel takes off, dsibars or pattern, not %.*s\n", (int)len, p);
		}
		p = *e ? e + 1 : e;
	}
	if (*pattern && *dsibars) {
		printf("pieboot: panel: pattern needs a framebuffer; ignored with dsibars\n");
		*pattern = false;
	}
	return true;
}

struct panel_run {
	const struct panel_entry *entry;
	uint8_t id[ID_BYTES];
	unsigned id_len, sent;
	bool touched, powered, power_was_on, running, clocked;
	uint64_t fb, fb_size;
	struct panel_clocks clocks;
};

static const char *
panel_start(const struct panel_hw *h, const struct panel_entry *entries, unsigned count, uint64_t end, bool dsibars,
    bool pattern, struct panel_run *r)
{
	memset(r, 0, sizeof(*r));
	step("reading the VOP2's IOMMU");
	uint32_t mmu = rd(h->vop + VOP2_MMU(0) + MMU_STATUS) | rd(h->vop + VOP2_MMU(1) + MMU_STATUS);
	step("IOMMU status 0x%x; checking what else runs from VPLL", mmu);
	if (mmu & MMU_PAGING) {
		return "the VOP2's IOMMU translates its addresses";
	}
	const char *user = vpll_user(h);
	if (user) {
		return user;
	}
	if ((h->bl_enable.present && !bank_ready(h, h->bl_enable.bank)) || !bank_ready(h, h->bl_pin.bank)) {
		return "the backlight's GPIO bank is gated or in reset";
	}
	if (!bank_ready(h, h->reset.bank) || (h->power.present && !bank_ready(h, h->power.bank))) {
		return "the panel's GPIO bank is gated or in reset";
	}
	r->touched = true;
	step("backlight off, video port 1 0x%08x to standby", rd(h->vop + VOP2_VP(1) + VP_DSP_CTRL));
	if (!backlight_off(h)) {
		return "cannot turn the backlight off";
	}
	vp1_standby(h);
	r->power_was_on = pin_active(h, &h->power);
	step("panel reset, power %s", r->power_was_on ? "on already" : "on");
	if (!pin_out(h, &h->reset, true) || !pin_out(h, &h->power, true)) {
		return "the panel's GPIO bank is gated or in reset";
	}
	r->powered = true;
	udelay(POWER_ON_US);
	const struct dsi_panel *first = &entries[0].panel;
	step("DSI host and D-PHY at %u kbps a lane", DPHY_LANE_KBPS);
	if (!link_up(h, first)) {
		return "the DSI link does not come up";
	}
	udelay(RESET_US);
	step("PHY status 0x%03x; panel out of reset", rd(h->dsi + DSI_PHY_STATUS));
	pin_out(h, &h->reset, false);
	udelay(RESET_DONE_US);
	step("reading the panel's ID");
	r->id_len = dcs_read(h, DCS_GET_DISPLAY_ID, r->id, ID_BYTES);
	for (unsigned i = 0; i < count && !r->entry; i++) {
		if (dsi_panel_matches(&entries[i].panel, r->id, r->id_len)) {
			r->entry = &entries[i];
		}
	}
	if (!r->entry) {
		printf("pieboot: panel: ID %02x %02x %02x, %u bytes; errors 0x%06x 0x%05x\n", r->id[0], r->id[1], r->id[2],
		    r->id_len, rd(h->dsi + DSI_INT_ST0), rd(h->dsi + DSI_INT_ST1));
		return r->id_len ? "the panel's ID matches no pie,panel-* node" : "the panel does not answer DCS 0x04";
	}
	const struct dsi_panel *p = &r->entry->panel;
	const struct dsi_mode *m = &p->mode;
	step("ID %02x %02x %02x: %s; sending its init list", r->id[0], r->id[1], r->id[2], r->entry->name);
	if (p->lanes != first->lanes || p->no_eot != first->no_eot || p->lp_commands != first->lp_commands) {
		link_down(h);
		if (!link_up(h, p)) {
			return "the DSI link does not come up for the panel's lanes";
		}
	}
	if (!send_init(h, p, &r->sent) || !dcs(h, DCS_EXIT_SLEEP_MODE)) {
		return "the DSI host does not send the init list";
	}
	udelay(SLEEP_OUT_US);
	if (!dcs(h, DCS_SET_DISPLAY_ON)) {
		return "the DSI host does not send display on";
	}
	udelay(DISPLAY_ON_US);
	struct dsi_timing t;
	if (!dsi_timing(m, p->lanes, DPHY_LANE_KBPS, &t)) {
		return "the link is too slow for the panel's mode";
	}
	struct rk_pll pll;
	if (!rk_pll_exact(m->clock_khz, &pll)) {
		return "no VPLL setting gives the panel's pixel clock";
	}
	step("%u packets sent; VPLL refdiv %u fbdiv %u postdiv %u %u, DCLK_VOP1 divided by %u", r->sent, pll.refdiv,
	    pll.fbdiv, pll.postdiv1, pll.postdiv2, pll.div);
	if (!pixel_clock(h, &pll, &r->clocks)) {
		return "VPLL does not lock";
	}
	r->clocked = true;
	r->fb_size = ALIGN_UP((uint64_t)m->hdisplay * 4 * m->vdisplay, 0x4000);
	r->fb = end - r->fb_size;
	if (!dsibars) {
		step("%s the framebuffer, 0x%lx-0x%lx", pattern ? "filling" : "clearing", r->fb, end);
		dcache_clean_invalidate(r->fb, end);
		if (pattern) {
			dsi_panel_pattern(m, (uint32_t *)(uintptr_t)r->fb);
		} else {
			memset((void *)(uintptr_t)r->fb, 0, r->fb_size);
		}
		dsb_sy();
	}
	step("setting up smart0, the overlay, video port 1 and the interface");
	step_errors(h, "before video port 1");
	uint32_t vp1_ctrl;
	if (!vp1_configure(h, m, r->fb, !dsibars, &vp1_ctrl)) {
		return "video port 1 cannot take smart0";
	}
	r->running = vp1_running(h->vop, "before video mode");
	step("video port 1 %s; the DSI host to video mode%s", r->running ? "running" : "not running",
	    dsibars ? " with its colour bars" : "");
	step_errors(h, "before video mode");
	video_mode(h, p, &t, dsibars);
	step_errors(h, "video mode before video port 1");
	step("video port 1 out of standby");
	vp1_start(h, vp1_ctrl);
	if (recording->lost) {
		return "too many registers changed to put back";
	}
	if (!dsibars && !vp1_wait_running(h->vop, m)) {
		return "video port 1 does not advance";
	}
	step("backlight on");
	if (!backlight_on(h)) {
		return "cannot turn the backlight on";
	}
	if (recording->lost) {
		return "too many registers changed to put back";
	}
	recording = NULL;
	step_status(h, "started");
	r->running = vp1_running(h->vop, "started");
	return NULL;
}

bool
rk_panel_board(const struct fdt *fdt)
{
	struct panel_hw h;
	return panel_find(fdt, &h);
}

void
rk_panel_off(const struct fdt *fdt, const char *opts, bool verbose)
{
	struct panel_hw h;
	if (!panel_find(fdt, &h)) {
		return;
	}
	trace = verbose;
	bool dsibars, pattern;
	if (!panel_options(opts, &dsibars, &pattern) || panel_wiring(fdt, &h)) {
		return;
	}
	struct panel_entry entries[MAX_PANELS];
	if (!panel_entries(h.panel, entries, MAX_PANELS)) {
		return;
	}
	uint32_t gates[sizeof(gpio_banks) / sizeof(gpio_banks[0])];
	for (unsigned b = 0; b < sizeof(gpio_banks) / sizeof(gpio_banks[0]); b++) {
		gates[b] = rd(bank_clocks(&h, b) + gpio_banks[b].gate);
	}
	bool vop = vo_running(&h);
	bool dsi = vop && !(rd(h.cru + CRU_GATE(21)) & GATE21_DSITX0) && !(rd(h.cru + CRU_SOFTRST(17)) & SOFTRST17_DSITX0);
	step("another output has the console: the panel off; video port 1 %s, DSI host %s", vop ? "to standby" : "off",
	    dsi ? "down" : "off");
	panel_dark(&h, vop, dsi);
	pin_out(&h, &h.power, false);
	for (unsigned b = 0; b < sizeof(gpio_banks) / sizeof(gpio_banks[0]); b++) {
		wr_masked(bank_clocks(&h, b) + gpio_banks[b].gate, 1u << gpio_banks[b].gate_bit, gates[b]);
	}
}

bool
rk_panel(struct fdt *fdt, const char *opts, bool verbose, struct place_range ram, const struct place_range *busy, unsigned n,
    uint64_t *base, uint64_t *size)
{
	struct panel_hw h;
	if (!panel_find(fdt, &h)) {
		return false;
	}
	trace = verbose;

	bool dsibars, pattern;
	if (!panel_options(opts, &dsibars, &pattern)) {
		return false;
	}

	const char *why = panel_wiring(fdt, &h);
	if (why) {
		printf("pieboot: panel: %s; the panel left off\n", why);
		return false;
	}
	struct panel_entry entries[MAX_PANELS];
	unsigned count = panel_entries(h.panel, entries, MAX_PANELS);
	uint64_t largest = 0;
	for (unsigned i = 0; i < count; i++) {
		largest = MAX(largest, ALIGN_UP((uint64_t)entries[i].panel.mode.hdisplay * 4 * entries[i].panel.mode.vdisplay,
		    0x4000));
	}
	uint64_t end = ram.end;
	bool room = dsibars || (ram.base < end && end - ram.base > largest && !(end & 0x3fff));
	for (unsigned i = 0; i < n && room && !dsibars; i++) {
		room = busy[i].end <= end - largest || busy[i].base >= end || busy[i].base >= busy[i].end;
	}
	if (!count) {
		why = "no usable pie,panel-* node under the panel's node";
	} else if (!dsibars && end > 0x100000000ULL) {
		why = "the top of RAM is above 4 GB, out of the VOP2's reach";
	} else if (!room) {
		why = "no room for the framebuffer below the panic region";
	} else if (!pattern && !chosen_node(fdt)) {
		why = "/chosen cannot take a framebuffer node";
	}
	if (why) {
		printf("pieboot: panel: %s; the panel left off\n", why);
		return false;
	}

	struct panel_state s;
	struct panel_run r;
	step_state(&h, "as found");
	step("VO on");
	if (!vo_on(&h, &s)) {
		vo_restore(&h, &s);
		return false;
	}
	step_state(&h, "VO on");
	static struct panel_log found;
	record(&found, &h);
	why = panel_start(&h, entries, count, end, dsibars, pattern, &r);
	recording = NULL;
	if (why) {
		printf("pieboot: panel: %s; the panel left off\n", why);
		if (r.touched) {
			panel_dark(&h, true, true);
		}
		if (r.powered && !r.power_was_on) {
			pin_out(&h, &h.power, false);
		}
		put_back(&found);
		if (r.clocked) {
			clocks_restore(&h, &r.clocks);
		}
		vo_restore(&h, &s);
		return false;
	}
	const struct dsi_mode *m = &r.entry->panel.mode;
	printf("pieboot: panel: %ux%u at %u kHz, %s; %u lanes at %u kbps; video port 1 %s\n", m->hdisplay,
	    m->vdisplay, m->clock_khz, r.entry->name, r.entry->panel.lanes, DPHY_LANE_KBPS,
	    r.running ? "running" : "not running");
	if (dsibars) {
		printf("pieboot: panel: the DSI host's colour bars, as pieboot.panel asks; no framebuffer\n");
		return false;
	}
	if (pattern) {
		printf("pieboot: panel: test image shown; framebuffer kept but not handed on\n");
	} else {
		add_node(chosen_node(fdt), r.fb, m);
	}
	*base = r.fb;
	*size = r.fb_size;
	return true;
}
