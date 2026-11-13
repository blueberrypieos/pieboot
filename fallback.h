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
};

/*
 * mode is the value of pieboot.fallback=, or NULL. "1" sets the firmware's
 * tryboot flag and arms the watchdog; "flag" sets the flag only.
 */
enum fallback fallback_arm(const struct fdt *fdt, const char *mode);

/* One line for the console about what fallback_arm did. */
void fallback_report(enum fallback f);

/* Give an armed watchdog its full count again, as the kernel is entered. */
void fallback_renew(void);
#endif
