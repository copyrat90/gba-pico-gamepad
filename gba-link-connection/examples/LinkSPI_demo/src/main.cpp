#include <tonc.h>
#include <string>
#include "../../_lib/interrupt.h"

// (0) Include the header
#include "../../../lib/LinkSPI.h"

void log(std::string text);
void wait(u32 verticalLines);
inline void VBLANK() {}

// (1) Create a LinkSPI instance
LinkSPI* linkSPI = new LinkSPI();

void init() {
  REG_DISPCNT = DCNT_MODE0 | DCNT_BG0;
  tte_init_se_default(0, BG_CBB(0) | BG_SBB(31));

  // (2) Add the interrupt service routines
  interrupt_init();
  interrupt_set_handler(INTR_VBLANK, VBLANK);
  interrupt_enable(INTR_VBLANK);
  interrupt_set_handler(INTR_SERIAL, LINK_SPI_ISR_SERIAL);
  interrupt_enable(INTR_SERIAL);
}

int main() {
  init();

  bool firstTransfer = true;

  linkSPI->activate(LinkSPI::Mode::SLAVE);

  while (true) {
    std::string output = "[gba-pico-gamepad]\n\n";
    u16 keys = ~REG_KEYS & KEY_ANY;

    if (firstTransfer) {
      log(output + "Waiting...");
      firstTransfer = false;
    }

    // Exchange 32-bit data with the other end
    u32 remoteKeys = linkSPI->transfer(keys);
    output += "send: " + std::to_string(keys) + "\n";
    output += "recv: " + std::to_string(remoteKeys) + "\n";

    // Print
    VBlankIntrWait();
    log(output);
  }

  return 0;
}

void log(std::string text) {
  tte_erase_screen();
  tte_write("#{P:0,0}");
  tte_write(text.c_str());
}

void wait(u32 verticalLines) {
  u32 count = 0;
  u32 vCount = REG_VCOUNT;

  while (count < verticalLines) {
    if (REG_VCOUNT != vCount) {
      count++;
      vCount = REG_VCOUNT;
    }
  };
}