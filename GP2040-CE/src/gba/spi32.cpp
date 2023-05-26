/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "gba/spi32.h"

#include "pico/stdlib.h"

namespace gba
{

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

void initSpi32() {
	spi_init(spi_default, 1000 * 1000);
	gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
	gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
	gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);
	gpio_set_function(PICO_DEFAULT_SPI_CSN_PIN, GPIO_FUNC_SPI);
	spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}

void deinitSpi32() {
    spi_deinit(spi_default);
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

}
