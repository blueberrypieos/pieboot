/* SPDX-License-Identifier: BSD-2-Clause */
#include "uart.h"
#include "fbcon.h"

/* PL011 registers. The firmware/QEMU has usually set up the baud rate. */
#define UARTDR   0x00
#define UARTFR   0x18
#define UARTFR_TXFF (1u << 5)
#define UARTFR_BUSY (1u << 3)
#define UARTIBRD 0x24
#define UARTFBRD 0x28
#define UARTLCR_H 0x2c
#define UARTLCR_H_WLEN8 (3u << 5)
#define UARTLCR_H_FEN   (1u << 4)
#define UARTCR   0x30
#define UARTCR_UARTEN   (1u << 0)
#define UARTCR_TXE      (1u << 8)
#define UARTCR_RXE      (1u << 9)

/* 8250 registers, at reg-shift strides, 32-bit access. */
#define UART8250_THR 0
#define UART8250_LSR 5
#define UART8250_LSR_THRE (1u << 5)

/* A RAM console in the layout of Linux's persistent RAM buffer: "DBGC", start, size, data. */
#define RAMCONS_SIG 0x43474244u
#define RAMCONS_HDR 12

static volatile uint8_t *uart_base;
static bool uart_dead;                  /* its FIFO stopped draining: given up */
static bool uart_8250;
static uint32_t uart_shift;
static bool uart_wide;
static volatile uint8_t *ramcons_base;
static uint32_t ramcons_cap;

static inline uint32_t
rd(uint32_t off)
{
	return *(volatile uint32_t *)(uart_base + off);
}

static inline void
wr(uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(uart_base + off) = v;
}

void
uart_set_base(uint64_t pa)
{
	uart_base = (volatile uint8_t *)(uintptr_t)pa;
}

void
uart_set_8250(uint64_t pa, uint32_t reg_shift, uint32_t io_width)
{
	if ((io_width != 1 && io_width != 4) || reg_shift > 4 || (io_width == 4 && reg_shift < 2)) {
		return;
	}
	uart_base = (volatile uint8_t *)(uintptr_t)pa;
	uart_8250 = true;
	uart_shift = reg_shift;
	uart_wide = io_width == 4;
}

static uint32_t
uart8250_read(uint32_t reg)
{
	return uart_wide ? *(volatile uint32_t *)(uart_base + (reg << uart_shift)) : uart_base[reg << uart_shift];
}

static void
uart8250_write(uint32_t reg, uint8_t v)
{
	if (uart_wide) {
		*(volatile uint32_t *)(uart_base + (reg << uart_shift)) = v;
	} else {
		uart_base[reg << uart_shift] = v;
	}
}

bool
ramcons_init(uint64_t base, uint64_t size)
{
	if (size <= RAMCONS_HDR || size > 0x10000000) {
		ramcons_base = 0;
		return false;
	}
	ramcons_base = (volatile uint8_t *)(uintptr_t)base;
	ramcons_cap = (uint32_t)(size - RAMCONS_HDR);
	volatile uint32_t *hdr = (volatile uint32_t *)ramcons_base;
	hdr[0] = RAMCONS_SIG;
	hdr[1] = 0;
	hdr[2] = 0;
	return true;
}

static void
ramcons_putc(char c)
{
	if (!ramcons_base) {
		return;
	}
	volatile uint32_t *hdr = (volatile uint32_t *)ramcons_base;
	uint32_t start = hdr[1], size = hdr[2];
	if (start >= ramcons_cap) {
		start = 0;
	}
	ramcons_base[RAMCONS_HDR + start] = (uint8_t)c;
	hdr[1] = start + 1 == ramcons_cap ? 0 : start + 1;
	hdr[2] = size < ramcons_cap ? size + 1 : ramcons_cap;
}

/*
 * Set 8N1 at baud from the UART clock, unless the firmware already enabled
 * the UART with a baud rate (the Pi 5's debug UART may be left alone).
 */
void
uart_setup(uint32_t clock_hz, uint32_t baud)
{
	if (!uart_base || uart_8250 || !clock_hz || !baud ||
	    ((rd(UARTCR) & UARTCR_UARTEN) && rd(UARTIBRD) != 0)) {
		return;
	}
	/* Divisor = clock / (16 baud), in 1/64ths, rounded. */
	uint32_t div64 = (uint32_t)((((uint64_t)clock_hz * 4) + baud / 2) / baud);
	wr(UARTCR, 0);
	for (uint32_t spins = 0; (rd(UARTFR) & UARTFR_BUSY) && spins < 5000000; spins++) {
	}
	wr(UARTIBRD, div64 >> 6);
	wr(UARTFBRD, div64 & 63);
	wr(UARTLCR_H, UARTLCR_H_WLEN8 | UARTLCR_H_FEN);
	wr(UARTCR, UARTCR_UARTEN | UARTCR_TXE | UARTCR_RXE);
}

/*
 * A character to the UART and to the framebuffer console. A UART whose
 * FIFO does not drain (no clock, say) is given up on after about a
 * second rather than hanging the boot.
 */
void
uart_putc(char c)
{
	ramcons_putc(c);
	fbcon_putc(c);
	if (!uart_base || uart_dead) {
		return;
	}
	if (c == '\n') {
		uart_putc_raw('\r');
	}
	uart_putc_raw(c);
}

void
uart_putc_raw(char c)
{
	if (!uart_base || uart_dead) {
		return;
	}
	if (uart_8250) {
		for (uint32_t spins = 0; !(uart8250_read(UART8250_LSR) & UART8250_LSR_THRE); spins++) {
			if (spins > 5000000) {
				uart_dead = true;
				return;
			}
		}
		uart8250_write(UART8250_THR, (uint8_t)c);
		return;
	}
	for (uint32_t spins = 0; *(volatile uint32_t *)(uart_base + UARTFR) & UARTFR_TXFF; spins++) {
		if (spins > 5000000) {
			uart_dead = true;
			return;
		}
	}
	*(volatile uint32_t *)(uart_base + UARTDR) = (uint8_t)c;
}

bool
uart_working(void)
{
	return uart_base && !uart_dead;
}

void
uart_puts(const char *s)
{
	while (*s) {
		uart_putc(*s++);
	}
}
