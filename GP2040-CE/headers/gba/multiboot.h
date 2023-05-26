/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * This is a port of `gba_03_multiboot` to the RPi Pico.
 * https://github.com/akkera102/gba_03_multiboot
 */

#ifndef _GBA_SPI32_H_
#define _GBA_SPI32_H_

#include <cstdint>

namespace gba
{

void sendGBARom(const uint8_t* romAddr, uint32_t romSize);

}

#endif