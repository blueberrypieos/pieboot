/* SPDX-License-Identifier: BSD-2-Clause */
/* Fallback boot. See fallback.c. */
#ifndef PIEBOOT_FALLBACK_H
#define PIEBOOT_FALLBACK_H
#include "lib.h"
#include "fdt.h"

enum fallback {
	FALLBACK_OFF,           /* no pieboot.fallback= */
	FALLBACK_BAD_VALUE,     /* pieboot.fallback= with another value: nothing done */
	FALLBACK_NO_FLAG,       /* the firmware refused the flag: nothing armed */
	FALLBACK_MBOX_TIMEOUT,  /* the mailbox did not answer: nothing armed */
	FALLBACK_FLAG,          /* tryboot flag set */
	FALLBACK_NO_WATCHDOG,   /* tryboot flag set, but no watchdog to arm */
	FALLBACK_ARMED,         /* tryboot flag set, watchdog armed */
	FALLBACK_QEMU,          /* QEMU: flag asked for, watchdog left alone */
	FALLBACK_WDT_ARMED,     /* no tryboot flag here (the boot loader keeps its own): watchdog armed */
	FALLBACK_NO_FLAG_WDT,   /* no tryboot flag and no watchdog: nothing armed */
	FALLBACK_WDT_MANY,      /* no tryboot flag; no single watchdog the kernel certainly takes over: nothing armed */
	FALLBACK_WDT_OFF,       /* no tryboot flag; the watchdog did not turn on: nothing armed */
	FALLBACK_WDT_UNCHECKED, /* no tryboot flag; the watchdog is on but not counting in its reset mode */
};

/*
 * mode is the value of pieboot.fallback=, or NULL. "1" sets the firmware's
 * tryboot flag and arms the watchdog; "flag" sets the flag only.
 */
enum fallback fallback_arm(const struct fdt *fdt, const char *mode);

/* main.c: a top-level FDT node that the kernel's device tree leaves out. */
bool is_toplevel_skipped(const struct fdt_node *n);

/* One line for the console about what fallback_arm did. */
void fallback_report(enum fallback f);

/* Give an armed watchdog its full count again, as the kernel is entered. */
void fallback_renew(void);
#endif
