/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * This is a port of `gba_03_multiboot` to the RPi Pico.
 * https://github.com/akkera102/gba_03_multiboot
 */

#pragma once

#include <cstdint>

namespace gba
{

/// @return `true`  if ROM is sent
/// @return `false` if GBA program is already running, and only `Start` is pressed on it
bool sendGBARom(const uint8_t* romAddr, uint32_t romSize);

}
