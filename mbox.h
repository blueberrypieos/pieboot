/* SPDX-License-Identifier: BSD-2-Clause */
/* VideoCore firmware property mailbox (Raspberry Pi). See mbox.c. */
#ifndef PIEBOOT_MBOX_H
#define PIEBOOT_MBOX_H
#include "lib.h"
#include "fdt.h"
#include "boot_args.h"

/* Find the mailbox in the FDT; false if there is none. */
bool mbox_init(const struct fdt *fdt);

/*
 * Ask the firmware for a 32 bpp framebuffer at the current display size
 * and describe it in *video. False if there is no mailbox or it fails.
 */
bool mbox_framebuffer(struct Boot_Video *video);

/* Firmware clock IDs (raspberrypi,firmware-clocks). */
#define MBOX_CLOCK_EMMC         1
#define MBOX_CLOCK_EMMC2        12

/* The current rate of a firmware clock in Hz; 0 if unknown. */
uint32_t mbox_clock_rate(uint32_t clock_id);

/* Whether a message went unanswered for a second in this boot. */
bool mbox_timed_out(void);

/* The firmware's revision, its build time; QEMU's is 346337. 0 if unknown. */
uint32_t mbox_firmware_revision(void);
#define MBOX_QEMU_REVISION      346337

/*
 * Set the flags the firmware reads at the next reset. With
 * MBOX_REBOOT_TRYBOOT it reads tryboot.txt instead of config.txt, once.
 */
bool mbox_set_reboot_flags(uint32_t flags);
#define MBOX_REBOOT_TRYBOOT     1
#endif
