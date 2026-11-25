/* $NetBSD: dw_hdmi.c,v 1.12 2024/02/09 16:56:23 skrll Exp $ */
/* $NetBSD: dw_hdmi_phy.c,v 1.3 2021/12/19 11:00:47 riastradh Exp $ */
/* $NetBSD: rk_dwhdmi.c,v 1.8 2023/04/11 08:40:19 riastradh Exp $ */

/*-
 * Copyright (c) 2019 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Modified 2026-11-20: one file for pieboot, without DRM, DDC, CEC or audio; a fixed mode, sent as DVI or with an AVI
 * InfoFrame; each step of the setup reported to the caller, and registers read for its diagnostics.
 */

#include "dwhdmi.h"

#define HDMI_DESIGN_ID                  0x0000
#define HDMI_REVISION_ID                0x0001
#define HDMI_CONFIG2_ID                 0x0006

#define HDMI_IH_I2CMPHY_STAT0           0x0108
#define  IH_I2CMPHY_STAT0_DONE          (1u << 1)
#define  IH_I2CMPHY_STAT0_ERROR         (1u << 0)

#define HDMI_TX_INVID0                  0x0200
#define  TX_INVID0_RGB444_8             0x01
#define HDMI_TX_INSTUFFING              0x0201
#define  TX_INSTUFFING_GY_RCR_BCB       0x07

#define HDMI_VP_PR_CD                   0x0801
#define HDMI_VP_STUFF                   0x0802
#define  VP_STUFF_IDEFAULT_PHASE        (1u << 5)
#define  VP_STUFF_YCC422                (1u << 2)
#define  VP_STUFF_PP                    (1u << 1)
#define  VP_STUFF_PR                    (1u << 0)
#define HDMI_VP_REMAP                   0x0803
#define HDMI_VP_CONF                    0x0804
#define  VP_CONF_BYPASS_EN              (1u << 6)
#define  VP_CONF_BYPASS_SELECT          (1u << 2)
#define  VP_CONF_OUTPUT_BYPASS          (2u << 0)

#define HDMI_FC_INVIDCONF               0x1000
#define  FC_INVIDCONF_VSYNC_HIGH        (1u << 6)
#define  FC_INVIDCONF_HSYNC_HIGH        (1u << 5)
#define  FC_INVIDCONF_DE_HIGH           (1u << 4)
#define  FC_INVIDCONF_HDMI              (1u << 3)
#define HDMI_FC_INHACTIV0               0x1001
#define HDMI_FC_INHACTIV1               0x1002
#define HDMI_FC_INHBLANK0               0x1003
#define HDMI_FC_INHBLANK1               0x1004
#define HDMI_FC_INVACTIV0               0x1005
#define HDMI_FC_INVACTIV1               0x1006
#define HDMI_FC_INVBLANK                0x1007
#define HDMI_FC_HSYNCINDELAY0           0x1008
#define HDMI_FC_HSYNCINDELAY1           0x1009
#define HDMI_FC_HSYNCINWIDTH0           0x100a
#define HDMI_FC_HSYNCINWIDTH1           0x100b
#define HDMI_FC_VSYNCINDELAY            0x100c
#define HDMI_FC_VSYNCINWIDTH            0x100d
#define HDMI_FC_CTRLDUR                 0x1011
#define HDMI_FC_EXCTRLDUR               0x1012
#define HDMI_FC_EXCTRLSPAC              0x1013
#define HDMI_FC_CH0PREAM                0x1014
#define HDMI_FC_CH1PREAM                0x1015
#define HDMI_FC_CH2PREAM                0x1016
#define HDMI_FC_AVICONF3                0x1017
#define HDMI_FC_AVICONF0                0x1019
#define HDMI_FC_AVICONF1                0x101a
#define HDMI_FC_AVICONF2                0x101b
#define HDMI_FC_AVIVID                  0x101c
#define HDMI_FC_PACKET_TX_EN            0x10e3
#define  FC_PACKET_TX_EN_AUDI           (1u << 3)

#define HDMI_PHY_CONF0                  0x3000
#define  PHY_CONF0_PDZ                  (1u << 7)
#define  PHY_CONF0_ENTMDS               (1u << 6)
#define  PHY_CONF0_SVSRET               (1u << 5)
#define  PHY_CONF0_PDDQ                 (1u << 4)
#define  PHY_CONF0_TXPWRON              (1u << 3)
#define  PHY_CONF0_ENHPDRXSENSE         (1u << 2)
#define  PHY_CONF0_SELDATAENPOL         (1u << 1)
#define  PHY_CONF0_SELDIPIF             (1u << 0)
#define HDMI_PHY_TST0                   0x3001
#define  PHY_TST0_TESTCLR               (1u << 5)
#define HDMI_PHY_STAT0                  0x3004
#define  PHY_STAT0_HPD                  (1u << 1)
#define  PHY_STAT0_TX_PHY_LOCK          (1u << 0)
#define HDMI_PHY_I2CM_SLAVE             0x3020
#define  PHY_I2CM_SLAVE_GEN2            0x69
#define HDMI_PHY_I2CM_ADDRESS           0x3021
#define HDMI_PHY_I2CM_DATAO_1           0x3022
#define HDMI_PHY_I2CM_DATAO_0           0x3023
#define HDMI_PHY_I2CM_OPERATION         0x3026
#define  PHY_I2CM_OPERATION_WRITE       0x10

#define HDMI_MC_CLKDIS                  0x4001
#define  MC_CLKDIS_H22S                 (1u << 7)
#define  MC_CLKDIS_HDCP                 (1u << 6)
#define  MC_CLKDIS_CEC                  (1u << 5)
#define  MC_CLKDIS_CSC                  (1u << 4)
#define  MC_CLKDIS_AUD                  (1u << 3)
#define  MC_CLKDIS_PREP                 (1u << 2)
#define HDMI_MC_SWRSTZREQ               0x4002
#define  MC_SWRSTZREQ_TMDS              (1u << 1)
#define HDMI_MC_FLOWCTRL                0x4004
#define HDMI_MC_PHYRSTZ                 0x4005
#define HDMI_MC_HEACPHY_RST             0x4007

#define PHY_I2C_CKCALCTRL               0x05
#define  CKCALCTRL_OVERRIDE             0x8000
#define PHY_I2C_CPCE_CTRL               0x06
#define PHY_I2C_CKSYMTXCTRL             0x09
#define PHY_I2C_VLEVCTRL                0x0e
#define PHY_I2C_CURRCTRL                0x10
#define PHY_I2C_PLLPHBYCTRL             0x13
#define PHY_I2C_GMPCTRL                 0x15
#define PHY_I2C_MSM_CTRL                0x17
#define  MSM_CTRL_FB_CLK                0x0006
#define PHY_I2C_TXTERM                  0x19

struct mpll_config {
	uint32_t pixel_clock;
	uint16_t cpce, gmp, curr;
};

struct phy_config {
	uint32_t pixel_clock;
	uint16_t sym, term, vlev;
};

static const struct mpll_config rk_mpll_config[] = {
	{ 40000,	0x00b3, 0x0000, 0x0018 },
	{ 65000,	0x0072, 0x0001, 0x0028 },
	{ 66000,	0x013e, 0x0003, 0x0038 },
	{ 83500,	0x0072, 0x0001, 0x0028 },
	{ 146250,	0x0051, 0x0002, 0x0038 },
	{ 148500,	0x0051, 0x0003, 0x0000 },
	{ 272000,	0x0040, 0x0003, 0x0000 },
	{ 340000,	0x0040, 0x0003, 0x0000 },
	{ 0,		0x0051, 0x0003, 0x0000 },
};

static const struct phy_config rk_phy_config[] = {
	{ 74250,	0x8009, 0x0004, 0x0272 },
	{ 148500,	0x802b, 0x0004, 0x028d },
	{ 297000,	0x8039, 0x0005, 0x028d },
	{ 594000,	0x8039, 0x0000, 0x019d },
	{ 0,		0x0000, 0x0000, 0x0000 }
};

static uint8_t
dwhdmi_read(uint64_t base, uint32_t reg)
{
	return (uint8_t)*(volatile uint32_t *)(uintptr_t)(base + 4 * (uint64_t)reg);
}

static void
dwhdmi_write(uint64_t base, uint32_t reg, uint8_t val)
{
	*(volatile uint32_t *)(uintptr_t)(base + 4 * (uint64_t)reg) = val;
}

static void
dwhdmi_phy_conf0(uint64_t base, uint8_t bits, bool set)
{
	uint8_t reg = dwhdmi_read(base, HDMI_PHY_CONF0);
	dwhdmi_write(base, HDMI_PHY_CONF0, set ? reg | bits : reg & ~bits);
}

static bool
dwhdmi_phy_i2c_write(uint64_t base, unsigned short data, unsigned char addr)
{
	dwhdmi_write(base, HDMI_IH_I2CMPHY_STAT0, IH_I2CMPHY_STAT0_DONE | IH_I2CMPHY_STAT0_ERROR);
	dwhdmi_write(base, HDMI_PHY_I2CM_ADDRESS, addr);
	dwhdmi_write(base, HDMI_PHY_I2CM_DATAO_1, (data >> 8) & 0xff);
	dwhdmi_write(base, HDMI_PHY_I2CM_DATAO_0, data & 0xff);
	dwhdmi_write(base, HDMI_PHY_I2CM_OPERATION, PHY_I2CM_OPERATION_WRITE);
	for (uint64_t until = read_cntpct() + read_cntfrq() / 100;;) {
		uint8_t val = dwhdmi_read(base, HDMI_IH_I2CMPHY_STAT0);
		if (val & IH_I2CMPHY_STAT0_ERROR) {
			return false;
		}
		if (val & IH_I2CMPHY_STAT0_DONE) {
			return true;
		}
		if (read_cntpct() > until) {
			return false;
		}
	}
}

static void
dwhdmi_report(void (*step)(const char *), const char *what)
{
	if (step) {
		step(what);
	}
}

static bool
dwhdmi_phy_configure(uint64_t base, uint32_t clock, uint8_t phytype, void (*step)(const char *))
{
	dwhdmi_report(step, "PHY: power down, reset, I2C address");
	dwhdmi_write(base, HDMI_MC_FLOWCTRL, 0);
	dwhdmi_phy_conf0(base, PHY_CONF0_TXPWRON, false);
	dwhdmi_phy_conf0(base, PHY_CONF0_PDDQ, true);

	dwhdmi_write(base, HDMI_MC_PHYRSTZ, 1);
	dwhdmi_write(base, HDMI_MC_PHYRSTZ, 0);
	dwhdmi_write(base, HDMI_MC_HEACPHY_RST, 1);

	uint8_t tst0 = dwhdmi_read(base, HDMI_PHY_TST0);
	dwhdmi_write(base, HDMI_PHY_TST0, tst0 | PHY_TST0_TESTCLR);
	dwhdmi_write(base, HDMI_PHY_I2CM_SLAVE, PHY_I2CM_SLAVE_GEN2);
	dwhdmi_write(base, HDMI_PHY_TST0, tst0 & ~PHY_TST0_TESTCLR);

	const struct mpll_config *mpll_conf = rk_mpll_config;
	while (mpll_conf->pixel_clock != 0 && clock > mpll_conf->pixel_clock) {
		mpll_conf++;
	}
	const struct phy_config *phy_conf = rk_phy_config;
	while (phy_conf->pixel_clock != 0 && clock > phy_conf->pixel_clock) {
		phy_conf++;
	}
	dwhdmi_report(step, "PHY: settings over I2C");
	if (!dwhdmi_phy_i2c_write(base, mpll_conf->cpce, PHY_I2C_CPCE_CTRL) ||
	    !dwhdmi_phy_i2c_write(base, mpll_conf->gmp, PHY_I2C_GMPCTRL) ||
	    !dwhdmi_phy_i2c_write(base, mpll_conf->curr, PHY_I2C_CURRCTRL) ||
	    !dwhdmi_phy_i2c_write(base, 0x0000, PHY_I2C_PLLPHBYCTRL) ||
	    !dwhdmi_phy_i2c_write(base, MSM_CTRL_FB_CLK, PHY_I2C_MSM_CTRL) ||
	    !dwhdmi_phy_i2c_write(base, phy_conf->term, PHY_I2C_TXTERM) ||
	    !dwhdmi_phy_i2c_write(base, phy_conf->sym, PHY_I2C_CKSYMTXCTRL) ||
	    !dwhdmi_phy_i2c_write(base, phy_conf->vlev, PHY_I2C_VLEVCTRL) ||
	    !dwhdmi_phy_i2c_write(base, CKCALCTRL_OVERRIDE, PHY_I2C_CKCALCTRL)) {
		return false;
	}

	dwhdmi_report(step, "PHY: power up, waiting for its PLL");
	dwhdmi_phy_conf0(base, PHY_CONF0_PDZ, true);
	dwhdmi_phy_conf0(base, PHY_CONF0_ENTMDS, false);
	dwhdmi_phy_conf0(base, PHY_CONF0_ENTMDS, true);
	dwhdmi_phy_conf0(base, PHY_CONF0_TXPWRON, true);
	dwhdmi_phy_conf0(base, PHY_CONF0_PDDQ, false);
	switch (phytype) {
	case 0xb2:
	case 0xc2:
	case 0xf3:
		dwhdmi_phy_conf0(base, PHY_CONF0_SVSRET, true);
		break;
	}

	for (uint64_t until = read_cntpct() + read_cntfrq() / 100;;) {
		if (dwhdmi_read(base, HDMI_PHY_STAT0) & PHY_STAT0_TX_PHY_LOCK) {
			return true;
		}
		if (read_cntpct() > until) {
			return false;
		}
	}
}

static bool
dwhdmi_phy_init(uint64_t base, uint32_t clock, void (*step)(const char *))
{
	uint8_t phytype = dwhdmi_read(base, HDMI_CONFIG2_ID);
	bool locked = false;

	for (int i = 0; i < 2; i++) {
		dwhdmi_phy_conf0(base, PHY_CONF0_SELDATAENPOL, true);
		dwhdmi_phy_conf0(base, PHY_CONF0_SELDIPIF, false);
		dwhdmi_phy_conf0(base, PHY_CONF0_ENTMDS, false);
		dwhdmi_phy_conf0(base, PHY_CONF0_PDZ, false);
		locked = dwhdmi_phy_configure(base, clock, phytype, step);
	}
	return locked;
}

static void
dwhdmi_vp_init(uint64_t base)
{
	dwhdmi_write(base, HDMI_VP_PR_CD, 0);
	dwhdmi_write(base, HDMI_VP_STUFF, VP_STUFF_IDEFAULT_PHASE | VP_STUFF_YCC422 | VP_STUFF_PP | VP_STUFF_PR);
	dwhdmi_write(base, HDMI_VP_REMAP, 0);
	dwhdmi_write(base, HDMI_VP_CONF, VP_CONF_BYPASS_EN | VP_CONF_BYPASS_SELECT | VP_CONF_OUTPUT_BYPASS);
}

static void
dwhdmi_fc_init(uint64_t base, const struct dwhdmi_mode *mode)
{
	const uint16_t inhactiv = mode->hdisplay;
	const uint16_t inhblank = mode->htotal - mode->hdisplay;
	const uint16_t invactiv = mode->vdisplay;
	const uint8_t invblank = (uint8_t)(mode->vtotal - mode->vdisplay);
	const uint16_t hsyncindelay = mode->hsync_start - mode->hdisplay;
	const uint16_t hsyncinwidth = mode->hsync_end - mode->hsync_start;
	const uint8_t vsyncindelay = (uint8_t)(mode->vsync_start - mode->vdisplay);
	const uint8_t vsyncinwidth = (uint8_t)(mode->vsync_end - mode->vsync_start);

	uint8_t val = FC_INVIDCONF_DE_HIGH;
	if (mode->vsync_high) {
		val |= FC_INVIDCONF_VSYNC_HIGH;
	}
	if (mode->hsync_high) {
		val |= FC_INVIDCONF_HSYNC_HIGH;
	}
	if (mode->hdmi) {
		val |= FC_INVIDCONF_HDMI;
	}
	dwhdmi_write(base, HDMI_FC_INVIDCONF, val);

	dwhdmi_write(base, HDMI_FC_INHACTIV0, inhactiv & 0xff);
	dwhdmi_write(base, HDMI_FC_INHACTIV1, inhactiv >> 8);
	dwhdmi_write(base, HDMI_FC_INHBLANK0, inhblank & 0xff);
	dwhdmi_write(base, HDMI_FC_INHBLANK1, inhblank >> 8);
	dwhdmi_write(base, HDMI_FC_INVACTIV0, invactiv & 0xff);
	dwhdmi_write(base, HDMI_FC_INVACTIV1, invactiv >> 8);
	dwhdmi_write(base, HDMI_FC_INVBLANK, invblank);
	dwhdmi_write(base, HDMI_FC_HSYNCINDELAY0, hsyncindelay & 0xff);
	dwhdmi_write(base, HDMI_FC_HSYNCINDELAY1, hsyncindelay >> 8);
	dwhdmi_write(base, HDMI_FC_HSYNCINWIDTH0, hsyncinwidth & 0xff);
	dwhdmi_write(base, HDMI_FC_HSYNCINWIDTH1, hsyncinwidth >> 8);
	dwhdmi_write(base, HDMI_FC_VSYNCINDELAY, vsyncindelay);
	dwhdmi_write(base, HDMI_FC_VSYNCINWIDTH, vsyncinwidth);

	dwhdmi_write(base, HDMI_FC_CTRLDUR, 12);
	dwhdmi_write(base, HDMI_FC_EXCTRLDUR, 32);
	dwhdmi_write(base, HDMI_FC_EXCTRLSPAC, 1);

	dwhdmi_write(base, HDMI_FC_CH0PREAM, 0x0b);
	dwhdmi_write(base, HDMI_FC_CH1PREAM, 0x16);
	dwhdmi_write(base, HDMI_FC_CH2PREAM, 0x21);
}

static void
dwhdmi_avi_init(uint64_t base, const struct dwhdmi_mode *mode)
{
	dwhdmi_write(base, HDMI_FC_AVICONF0, 0);
	dwhdmi_write(base, HDMI_FC_AVICONF1, (uint8_t)(mode->aspect << 4));
	dwhdmi_write(base, HDMI_FC_AVICONF2, 0);
	dwhdmi_write(base, HDMI_FC_AVICONF3, 0);
	dwhdmi_write(base, HDMI_FC_AVIVID, mode->vic & 0x7f);
	uint8_t val = dwhdmi_read(base, HDMI_FC_PACKET_TX_EN);
	dwhdmi_write(base, HDMI_FC_PACKET_TX_EN, val & ~FC_PACKET_TX_EN_AUDI);
}

static void
dwhdmi_tx_init(uint64_t base)
{
	dwhdmi_write(base, HDMI_TX_INVID0, TX_INVID0_RGB444_8);
	dwhdmi_write(base, HDMI_TX_INSTUFFING, TX_INSTUFFING_GY_RCR_BCB);
}

static void
dwhdmi_mc_init(uint64_t base)
{
	dwhdmi_write(base, HDMI_MC_FLOWCTRL, 0);
	dwhdmi_write(base, HDMI_MC_CLKDIS, MC_CLKDIS_H22S | MC_CLKDIS_HDCP | MC_CLKDIS_CEC | MC_CLKDIS_CSC |
	    MC_CLKDIS_AUD | MC_CLKDIS_PREP);
	dwhdmi_write(base, HDMI_MC_SWRSTZREQ, 0xff & ~MC_SWRSTZREQ_TMDS);

	unsigned iter = dwhdmi_version(base) == 0x130a ? 4 : 1;
	uint8_t val = dwhdmi_read(base, HDMI_FC_INVIDCONF);
	for (unsigned n = 0; n < iter; n++) {
		dwhdmi_write(base, HDMI_FC_INVIDCONF, val);
	}
}

uint32_t
dwhdmi_version(uint64_t base)
{
	return (uint32_t)dwhdmi_read(base, HDMI_DESIGN_ID) << 8 | dwhdmi_read(base, HDMI_REVISION_ID);
}

uint8_t
dwhdmi_phy_type(uint64_t base)
{
	return dwhdmi_read(base, HDMI_CONFIG2_ID);
}

uint8_t
dwhdmi_phy_status(uint64_t base)
{
	return dwhdmi_read(base, HDMI_PHY_STAT0);
}

uint8_t
dwhdmi_peek(uint64_t base, uint32_t reg)
{
	return dwhdmi_read(base, reg);
}

uint8_t
dwhdmi_hpd_sense(uint64_t base)
{
	uint8_t conf0 = dwhdmi_read(base, HDMI_PHY_CONF0);
	if (!(conf0 & PHY_CONF0_ENHPDRXSENSE)) {
		dwhdmi_write(base, HDMI_PHY_CONF0, conf0 | PHY_CONF0_ENHPDRXSENSE);
	}
	return conf0;
}

void
dwhdmi_hpd_sense_restore(uint64_t base, uint8_t conf0)
{
	if (dwhdmi_read(base, HDMI_PHY_CONF0) != conf0) {
		dwhdmi_write(base, HDMI_PHY_CONF0, conf0);
	}
}

bool
dwhdmi_hpd(uint64_t base)
{
	return (dwhdmi_read(base, HDMI_PHY_STAT0) & PHY_STAT0_HPD) != 0;
}

bool
dwhdmi_enable(uint64_t base, const struct dwhdmi_mode *mode, void (*step)(const char *what))
{
	if (!dwhdmi_phy_init(base, mode->clock_khz, step)) {
		return false;
	}
	dwhdmi_report(step, "controller: video packetizer, frame composer, main controller");
	dwhdmi_vp_init(base);
	dwhdmi_fc_init(base, mode);
	if (mode->hdmi) {
		dwhdmi_avi_init(base, mode);
	}
	dwhdmi_tx_init(base);
	dwhdmi_mc_init(base);
	return true;
}

void
dwhdmi_disable(uint64_t base)
{
	dwhdmi_phy_conf0(base, PHY_CONF0_TXPWRON | PHY_CONF0_ENTMDS | PHY_CONF0_PDZ, false);
	dwhdmi_phy_conf0(base, PHY_CONF0_PDDQ, true);
}
