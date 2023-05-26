/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * This is a port of `gba_03_multiboot` to the RPi Pico.
 * https://github.com/akkera102/gba_03_multiboot
 */

#include "gba/spi32.h"

#include <cstdlib>
#include "pico/stdlib.h"

namespace gba
{

static constexpr int GBA_DELAY_MS = 3;

uint32_t spi32Delay(uint32_t val) {
    uint32_t result = gba::spi32(val);
    sleep_ms(GBA_DELAY_MS);

    return result;
}

bool sendGBARom(const uint8_t* romAddr, const uint32_t romSize) {
    initSpi32();

    uint32_t recv;
    uint32_t fsize = romSize;

    // -----------------------------------------------------
    // printf("Waiting for GBA...\n");
    
    do {
        recv = gba::spi32(0x6202);
        sleep_ms(10);
    } while ((recv >> 16) != 0x7202 && recv != (1 << 9));

    // if GBA program is already running, and only `L` is pressed on it
    if (recv == (1 << 9))
        return false;
    
    // -----------------------------------------------------
    // printf("Sending header.\n");

    spi32Delay(0x6102);

    const uint16_t* fdata16 = (const uint16_t*)romAddr;
    for (uint32_t i = 0; i < 0xC0; i += 2)
        spi32Delay(fdata16[i / 2]);

    spi32Delay(0x6200);

    // -----------------------------------------------------
    // printf("Getting encryption and crc seeds.\n");

    spi32Delay(0x6202);
    spi32Delay(0x63D1);

    uint32_t token = spi32Delay(0x63D1);

    if ((token >> 24) != 0x73)
    {
        // fprintf(stderr, "Failed handshake!\n");
        exit(1);
    }


    uint32_t crcA, crcB, crcC, seed;

    crcA = (token >> 16) & 0xFF;
    seed = 0xFFFF00D1 | (crcA << 8);
    crcA = (crcA + 0xF) & 0xFF;

    spi32Delay(0x6400 | crcA);

    fsize += 0xF;
    fsize &= ~0xF;

    token = spi32Delay((fsize - 0x190) / 4);
    crcB = (token >> 16) & 0xFF;
    crcC = 0xC387;

    // -----------------------------------------------------
    // printf("Sending...\n");
    
    const uint32_t* fdata32 = (const uint32_t*)romAddr;

    for (uint32_t i = 0xC0; i < fsize; i += 4)
    {
        uint32_t dat = fdata32[i / 4];

        // crc step
        uint32_t tmp = dat;

        for (uint32_t b = 0; b < 32; b++)
        {
            uint32_t bit = (crcC ^ tmp) & 1;

            crcC = (crcC >> 1) ^ (bit ? 0xc37b : 0);
            tmp >>= 1;
        }

        // encrypt step
        seed = seed * 0x6F646573 + 1;
        dat = seed ^ dat ^ (0xFE000000 - i) ^ 0x43202F2F;

        // send
        uint32_t chk = spi32Delay(dat) >> 16;

        if (chk != (i & 0xFFFF))
        {
            // fprintf(stderr, "Transmission error at byte %zu: chk == %08x\n", i, chk);
            exit(1);
        }
    }

    // crc step final
    uint32_t tmp = 0xFFFF0000 | (crcB << 8) | crcA;

    for (uint32_t b = 0; b < 32; b++)
    {
        uint32_t bit = (crcC ^ tmp) & 1;

        crcC = (crcC >> 1) ^ (bit ? 0xc37b : 0);
        tmp >>= 1;
    }

    // -----------------------------------------------------
    // printf("Waiting for checksum...\n");

    spi32Delay(0x0065);

    do
    {
        recv = spi32Delay(0x0065) >> 16;
        sleep_us(10000);

    } while (recv != 0x0075);

    spi32Delay(0x0066);
    uint32_t crcGBA = spi32Delay(crcC & 0xFFFF) >> 16;

    // printf("Gba: %x, Cal: %x\n", crcGBA, crcC);
    // printf("Done.\n");

    deinitSpi32();

    return true;
}

}
