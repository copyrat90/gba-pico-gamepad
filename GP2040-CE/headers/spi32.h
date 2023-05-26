/*
 * SPDX-License-Identifier: CC0-1.0
 */

#ifndef _SPI32_H_
#define _SPI32_H_

// Pico SPI
#include "hardware/spi.h"

uint32_t swapByte32(uint32_t val);

uint32_t spi32(uint32_t val);

#endif
