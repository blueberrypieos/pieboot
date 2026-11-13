/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Fallback boot: a system that fails to boot falls back to one that works.
 * config.txt boots the new files, with pieboot.fallback=1 in their command
 * line; tryboot.txt boots the known-good ones. Before anything else,
 * pieboot sets the firmware's tryboot flag and arms the PM watchdog with
 * its full count, about 16 s, and sets the count again as it enters the
 * kernel. A kernel that boots takes the watchdog over. With
 * firmware that keeps the flag over a watchdog reset, a hang, a panic or
 * any later reset then boots tryboot.txt's system. pieboot.fallback=flag
 * sets the flag only. A mailbox that does not answer, or a hang before the
 * flag is set, is not covered.
 *
 * QEMU's raspi4b resets at once when the watchdog is armed, so there it is
 * left alone.
 */

#include "fallback.h"
#include "mbox.h"

#define PM_RSTC                 0x1c
#define PM_WDOG                 0x24
#define PM_PASSWORD             0x5a000000u
#define PM_PASSWORD_MASK        0xff000000u
#define PM_REGS_SIZE            0x28
#define PM_RSTC_WRCFG_MASK      0x00000030u
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020u
#define PM_WDOG_TIME            0x000fffffu

static void
pm_write(uint64_t addr, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)addr = v;
}

static uint32_t
pm_read(uint64_t addr)
{
	return *(volatile uint32_t *)(uintptr_t)addr;
}

static uint64_t wdog_base;

static bool
arm_watchdog(const struct fdt *fdt)
{
	static const char *const compat[] = {
		"brcm,bcm2835-pm-wdt", "brcm,bcm2835-pm", "brcm,bcm2712-pm", NULL
	};
	struct fdt_node *pm = NULL;
	uint64_t base, size;

	for (const char *const *c = compat; *c && !pm; c++) {
		pm = fdt_find_compatible(fdt->root, *c);
	}
	if (!pm || !fdt_reg(pm, 0, &base, &size) || size < PM_REGS_SIZE) {
		return false;
	}
	pm_write(base + PM_WDOG, PM_PASSWORD | PM_WDOG_TIME);
	uint32_t rstc = pm_read(base + PM_RSTC) & ~(PM_PASSWORD_MASK | PM_RSTC_WRCFG_MASK);
	pm_write(base + PM_RSTC, PM_PASSWORD | rstc | PM_RSTC_WRCFG_FULL_RESET);
	if ((pm_read(base + PM_RSTC) & PM_RSTC_WRCFG_MASK) != PM_RSTC_WRCFG_FULL_RESET) {
		return false;
	}
	wdog_base = base;
	return true;
}

void
fallback_renew(void)
{
	if (wdog_base) {
		pm_write(wdog_base + PM_WDOG, PM_PASSWORD | PM_WDOG_TIME);
	}
}

enum fallback
fallback_arm(const struct fdt *fdt, const char *mode)
{
	if (!mode) {
		return FALLBACK_OFF;
	}
	bool watchdog = !strcmp(mode, "1");
	if (!watchdog && strcmp(mode, "flag")) {
		return FALLBACK_BAD_VALUE;
	}
	if (!mbox_init(fdt)) {
		return FALLBACK_NO_FLAG;
	}
	bool flag = mbox_set_reboot_flags(MBOX_REBOOT_TRYBOOT);
	if (mbox_firmware_revision() == MBOX_QEMU_REVISION) {
		return FALLBACK_QEMU;
	}
	if (!flag) {
		return mbox_timed_out() ? FALLBACK_MBOX_TIMEOUT : FALLBACK_NO_FLAG;
	}
	if (!watchdog) {
		return FALLBACK_FLAG;
	}
	return arm_watchdog(fdt) ? FALLBACK_ARMED : FALLBACK_NO_WATCHDOG;
}

void
fallback_report(enum fallback f)
{
	switch (f) {
	case FALLBACK_OFF:
		break;
	case FALLBACK_BAD_VALUE:
		printf("pieboot: fallback: pieboot.fallback= takes 1 or flag; nothing done\n");
		break;
	case FALLBACK_NO_FLAG:
		printf("pieboot: fallback: the firmware did not take the tryboot flag; nothing armed\n");
		break;
	case FALLBACK_MBOX_TIMEOUT:
		printf("pieboot: fallback: the firmware mailbox did not answer; nothing armed\n");
		break;
	case FALLBACK_FLAG:
		printf("pieboot: fallback: tryboot flag set; the next reset boots tryboot.txt\n");
		break;
	case FALLBACK_NO_WATCHDOG:
		printf("pieboot: fallback: tryboot flag set, but the watchdog could not be armed: a hang stays hung\n");
		break;
	case FALLBACK_ARMED:
		printf("pieboot: fallback: tryboot flag set, watchdog armed; the next reset boots tryboot.txt\n");
		break;
	case FALLBACK_QEMU:
		printf("pieboot: fallback: QEMU, watchdog not armed\n");
		break;
	}
	if (f != FALLBACK_OFF && f != FALLBACK_BAD_VALUE && f != FALLBACK_MBOX_TIMEOUT && mbox_timed_out()) {
		printf("pieboot: fallback: the firmware mailbox stopped answering after the flag request\n");
	}
}
