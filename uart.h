/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef PIEBOOT_UART_H
#define PIEBOOT_UART_H
#include <stdbool.h>
#include <stdint.h>

/* Early PL011 or 8250 console; output is dropped until uart_set_base() or uart_set_8250(). */
void uart_set_base(uint64_t pa);
void uart_set_8250(uint64_t pa, uint32_t reg_shift, uint32_t io_width);   /* io_width 1 or 4 bytes */
bool ramcons_init(uint64_t base, uint64_t size);   /* also to a RAM console from here on; size 0 stops it; false if refused */
void uart_setup(uint32_t clock_hz, uint32_t baud);
void uart_putc(char c);                 /* also on the framebuffer console */
void uart_putc_raw(char c);             /* the UART alone, no newline translation */
bool uart_working(void);
void uart_puts(const char *s);
#endif
