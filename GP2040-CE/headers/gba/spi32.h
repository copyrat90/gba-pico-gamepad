/*
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once

// Pico SPI
#include "hardware/spi.h"

namespace gba
{

uint32_t swapByte32(uint32_t val);

void initSpi32();
void deinitSpi32();
uint32_t spi32(uint32_t val);

}
