/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2021 Jason Skuby (mytechtoybox.com)
 */

// Pi Pico includes
#include "pico/multicore.h"

// GP2040 includes
#include "gp2040.h"
#include "gp2040aux.h"

// GBA multiboot includes
#include "gba/multiboot.h"
#include "../../build/gba_rom.hpp"

// Launch our second core with additional modules loaded in
void core1() {
	multicore_lockout_victim_init(); // block core 1

	// Create GP2040 w/ Additional Modules for Core 1
	GP2040Aux * gp2040Core1 = new GP2040Aux();
	gp2040Core1->setup();
	gp2040Core1->run();
}

int main() {
	// Send GBA program via SPI, which sends its key presses to the RPi Pico
	gba::sendGBARom(LinkSPI_demo_mb_gba, LinkSPI_demo_mb_gba_len);
	// Give user some time to change Input Mode (https://gp2040-ce.info/#/usage?id=input-modes)
	sleep_ms(3000);

	// Create GP2040 Main Core (core0), Core1 is dependent on Core0
	GP2040 * gp2040 = new GP2040();
	gp2040->setup();

	// Create GP2040 Thread for Core1
	multicore_launch_core1(core1);

	// Start Core0 Loop
	gp2040->run();
	return 0;
}
