/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "spi32.h"

uint32_t swapByte32(uint32_t val) {
	union {
		uint32_t u32;
		uint8_t u8[4];
	} x;

	x.u32 = val;

	uint8_t tmp, tmp2;
	tmp = x.u8[0];
	tmp2 = x.u8[3];
	x.u8[0] = tmp2;
	x.u8[3] = tmp;
	tmp = x.u8[1];
	tmp2 = x.u8[2];
	x.u8[1] = tmp2;
	x.u8[2] = tmp;

	return x.u32;
}

uint32_t spi32(uint32_t val) {
	union {
		uint32_t u32;
		uint8_t u8[4];
	} send, recv;

	send.u32 = swapByte32(val);
	spi_write_read_blocking(spi_default, send.u8, recv.u8, 4);

	return swapByte32(recv.u32);
}
