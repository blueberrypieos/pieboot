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

/*
 * A DesignWare watchdog, on a board whose boot loader keeps the one-shot
 * boot flag itself, as U-Boot does on the RK3566: the longest count, 2^31
 * cycles of its clock, 89 s at 24 MHz. It cannot be stopped once started.
 */
#define DW_CR                   0x00
#define DW_CR_EN                0x01u
#define DW_CR_RMOD              0x02u
#define DW_TORR                 0x04
#define DW_CCVR                 0x08
#define DW_CRR                  0x0c
#define DW_CRR_RESTART          0x76u
#define DW_TOP_MAX              0xfu
#define DW_REGS_SIZE            0x18

static uint64_t dw_base;

static const char *const wdt_keys[] = {
	"snps,dw-wdt", "brcm,bcm2835-pm-wdt", "brcm,bcm2711-pm", "brcm,bcm2712-pm",
};

static bool
prop_has(const struct fdt_node *n, const char *name, const char *s)
{
	struct fdt_prop *p = fdt_prop(n, name);
	size_t want = strlen(s);
	for (uint32_t i = 0; p && i < p->len;) {
		size_t len = strnlen((const char *)p->val + i, p->len - i);
		if (len == want && !strncmp((const char *)p->val + i, s, len)) {
			return true;
		}
		i += (uint32_t)len + 1;
	}
	return false;
}

static bool
kernel_watchdog_key(const struct fdt_node *n)
{
	char base[64];
	fdt_basename(n, base, sizeof(base));
	for (unsigned i = 0; i < sizeof(wdt_keys) / sizeof(wdt_keys[0]); i++) {
		if (prop_has(n, "compatible", wdt_keys[i]) || prop_has(n, "name", wdt_keys[i]) ||
		    prop_has(n, "device_type", wdt_keys[i]) || prop_has(n, "model", wdt_keys[i]) ||
		    !strcmp(base, wdt_keys[i])) {
			return true;
		}
	}
	return false;
}

static void
count_watchdogs(struct fdt_node *n, struct fdt_node **first, unsigned *count)
{
	for (struct fdt_node *c = n->child; c; c = c->sibling) {
		if (kernel_watchdog_key(c) && (*count)++ == 0) {
			*first = c;
		}
		count_watchdogs(c, first, count);
	}
}

static bool
published_at_top(const struct fdt *fdt, const struct fdt_node *n)
{
	struct fdt_prop *status = fdt_prop(n, "status");
	size_t len = status ? strnlen((const char *)status->val, status->len) : 0;
	bool enabled = !status || (len == 4 && !strncmp((const char *)status->val, "okay", 4)) ||
	               (len == 2 && !strncmp((const char *)status->val, "ok", 2));
	return enabled && n->parent == fdt->root && !is_toplevel_skipped(n) && !fdt_prop(n, "AAPL,ignore") &&
	       !prop_has(n, "device_type", "associated-service");
}

static enum fallback
arm_dw_watchdog(const struct fdt *fdt)
{
	struct fdt_node *wdt = NULL;
	unsigned count = 0;
	uint64_t base, size;

	count_watchdogs(fdt->root, &wdt, &count);
	if (count == 0) {
		return FALLBACK_NO_FLAG_WDT;
	}
	if (count > 1 || !prop_has(wdt, "compatible", "snps,dw-wdt") || !published_at_top(fdt, wdt) ||
	    !fdt_reg(wdt, 0, &base, &size) || size < DW_REGS_SIZE) {
		return FALLBACK_WDT_MANY;
	}
	pm_write(base + DW_TORR, (DW_TOP_MAX << 4) | DW_TOP_MAX);
	pm_write(base + DW_CR, (pm_read(base + DW_CR) & ~DW_CR_RMOD) | DW_CR_EN);
	pm_write(base + DW_CRR, DW_CRR_RESTART);
	uint32_t cr = pm_read(base + DW_CR);
	if (!(cr & DW_CR_EN)) {
		return FALLBACK_WDT_OFF;
	}
	if (cr & DW_CR_RMOD) {
		return FALLBACK_WDT_UNCHECKED;
	}
	/* Armed only if it counts: its clock may be off. */
	uint32_t first = pm_read(base + DW_CCVR);
	bool counting = false;
	for (uint64_t until = read_cntpct() + read_cntfrq() / 100; !counting && read_cntpct() < until;) {
		counting = pm_read(base + DW_CCVR) != first;
	}
	if (!counting) {
		return FALLBACK_WDT_UNCHECKED;
	}
	dw_base = base;
	return FALLBACK_WDT_ARMED;
}

void
fallback_renew(void)
{
	if (wdog_base) {
		pm_write(wdog_base + PM_WDOG, PM_PASSWORD | PM_WDOG_TIME);
	}
	if (dw_base) {
		pm_write(dw_base + DW_CRR, DW_CRR_RESTART);
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
		if (!watchdog) {
			return FALLBACK_NO_FLAG;
		}
		return arm_dw_watchdog(fdt);
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
	case FALLBACK_WDT_ARMED:
		printf("pieboot: fallback: watchdog armed, 89 s; the boot loader's own flag picks the next boot\n");
		break;
	case FALLBACK_NO_FLAG_WDT:
		printf("pieboot: fallback: no firmware flag and no watchdog to arm; nothing armed\n");
		break;
	case FALLBACK_WDT_MANY:
		printf("pieboot: fallback: no single watchdog the kernel certainly takes over; nothing armed\n");
		break;
	case FALLBACK_WDT_OFF:
		printf("pieboot: fallback: the watchdog did not turn on; nothing armed\n");
		break;
	case FALLBACK_WDT_UNCHECKED:
		printf("pieboot: fallback: the watchdog is on but does not count in its reset mode; a hang may stay hung\n");
		break;
	}
	if (f != FALLBACK_OFF && f != FALLBACK_BAD_VALUE && f != FALLBACK_MBOX_TIMEOUT && mbox_timed_out()) {
		printf("pieboot: fallback: the firmware mailbox stopped answering after the flag request\n");
	}
}
