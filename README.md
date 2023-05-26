# gba-pico-gamepad

Proof-of-concept GBA to USB controller, which uses Raspberry Pi Pico as a bridge.

# Usage

1. Download the `gba-pico-gamepad-v*.uf2` binary from the [Release](https://github.com/copyrat90/gba-pico-gamepad/releases), 

2. Wire the GBA to RPi Pico, with your custom GBA link cable.
    * Default SPI0 Pin is used, so you should wire to the `21`, `24`, `25` pins. [(RPi Pico Pinout)](https://datasheets.raspberrypi.com/pico/Pico-R3-A4-Pinout.pdf)

3. Turn on the GBA **without a cartridge**.
    * ROM file is sent from the RPi Pico via multiboot.

4. Plug the USB Cable to your PC.

# Build

I tested building on Ubuntu 22.04 in WSL2.\
You have to use [WSL2](https://learn.microsoft.com/en-us/windows/wsl/install) if you're on Windows.

1. Install [devkitARM](https://devkitpro.org/wiki/Getting_Started) and `gba-dev` package.

2. Install the dependencies via command below.
    ```bash
    sudo apt install -y cmake xxd python3
    ```

3. Run this command to build.
    > Change the path if you've installed devkitARM somewhere else.
    ```bash
    export DEVKITPRO=/opt/devkitpro/
    export DEVKITARM=/opt/devkitpro/devkitARM/
    export PATH=$DEVKITARM/bin:/$DEVKITPRO/tools/bin/:$PATH
    export CC=$DEVKITARM/bin/arm-none-eabi-gcc
    export CXX=$DEVKITARM/bin/arm-none-eabi-g++

    ./build.sh
    ```

4. If everything goes right, you should see the `build/gba-pico-gamepad.uf2` binary.


# Credits

* [gba-link-connection](https://github.com/rodri042/gba-link-connection) : Game Boy Advance (GBA) C++ libraries to interact with the Serial Port.
* [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) : Gamepad firmware for the Raspberry Pi Pico
* [gba_03_multiboot](https://github.com/akkera102/gba_03_multiboot) : Raspberry Pi GBA Loader
    * It's integrated in [`GP2040-CE/src/gba/multiboot.cpp`](GP2040-CE/src/gba/multiboot.cpp)


# License

See the license of each project above.\
`build.sh` is 0BSD.
