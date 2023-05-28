# gba-pico-gamepad

Proof-of-concept Game Boy Advance (GBA) to USB controller, which uses Raspberry Pi Pico as a bridge.

[![](https://img.youtube.com/vi/JmBufgcb4Gw/hqdefault.jpg)](https://www.youtube.com/watch?v=JmBufgcb4Gw "Youtube video for gba-pico-gamepad")


# How it's done

This project is essentially a modified version of [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) that enables communication with the Game Boy Advance using the GBA link cable.

In the original GP2040-CE, key presses were received using pull-up GPIO signals.\
However, The GBA link cable only provides [4 pins for the communication](https://user-images.githubusercontent.com/1631752/124884342-8ee7fc80-dfa8-11eb-9bd2-4741a4b9acc6.png), which is insufficient for the 10 keys on the GBA.\
To overcome this limitation, GP2040-CE was modified to utilize SPI for receiving packets from the GBA.

To send key presses on the GBA via SPI, an example program from the [gba-link-connection](https://github.com/rodri042/gba-link-connection) was used with a minor modification.

To run this program on the GBA, [gba_03_multiboot](https://github.com/akkera102/gba_03_multiboot) is ported to the RPi Pico.\
This allows for sending a program from RPi Pico to GBA via SPI and running it, eliminating the need for additional hardware such as a flash cart.

And that's about it.

[Read this with a litte more detail (in Korean)](https://velog.io/@copyrat90/gba-pico-gamepad)


# Usage

1. Download the `gba-pico-gamepad-v*.uf2` binary from the [Release](https://github.com/copyrat90/gba-pico-gamepad/releases), and flash it to your RPi Pico.
    * You may have to [nuke your RPi Pico](https://www.raspberrypi.org/documentation/pico/getting-started/static/6f6f31460c258138bd33cc96ddd76b91/flash_nuke.uf2) before flashing it.

2. Cut your GBA link cable, and wire it to the RPi Pico as below.
    * RPi Pico `21` (`SPI0 RX`) pin <-> GBA `SO` pin
    * RPi Pico `23` (`GND`) pin <-> GBA `GND` pin
    * RPi Pico `24` (`SPI0 SCK`) pin <-> GBA `SC` pin
    * RPi Pico `25` (`SPI0 TX`) pin <-> GBA `SI` pin
    * ![Overall pinout](https://i.ibb.co/xgZW66y/rpi-pico-pinout.png "Overall pinout")
    * [Raspberry Pi Pico Pinout](https://datasheets.raspberrypi.com/pico/Pico-R3-A4-Pinout.pdf)
    * [GBA Link cable Pinout](https://gist.github.com/copyrat90/5be788ccec65f3d3ca3de468203c75b7)
        * Your GBA Link cable colors are likely to be different.\
        It is highly recommended to cut and open the shell to see your pinout.
        * ![GBA pinout](https://user-images.githubusercontent.com/1631752/124884342-8ee7fc80-dfa8-11eb-9bd2-4741a4b9acc6.png "GBA pinout")

3. Connect this cable to the GBA and turn it on **without a cartridge**.
    * The program is sent from RPi Pico to GBA via multiboot, and with a cartridge it will not work.

4. Plug the USB Cable to your PC.\
   It will start sending the program once the GBA is ready.
    * You can hold down certain key on boot to change Input Mode.
        + Note that the key binding is differ from the [original](https://gp2040-ce.info/#/usage?id=input-modes).
        + Hold `B` on boot -> Nintendo Switch
        + Hold `A` on boot -> XInput
        + Hold `L` on boot -> DirectInput/PS3
        + Hold `R` on boot -> PS4
    * You can [change the D-Pad Mode anytime with certain key combination.](https://gp2040-ce.info/#/usage?id=d-pad-modes)
    * GP2040-CE's Web Config is disabled.

5. Enjoy your GBA as an USB gamepad.
    * If you accidentally pulled out your cable, you can re-plug it and press `Start` to reconnect.


# Build

This is a build process for the Ubuntu 22.04.\
You have to use [WSL2](https://learn.microsoft.com/en-us/windows/wsl/install) if you are on Windows.

1. Install [devkitARM](https://devkitpro.org/wiki/Getting_Started) and `gba-dev` package.

2. Install the dependencies via command below.
    ```bash
    sudo apt install -y cmake xxd python3
    ```

3. Run this command to build.
    > Change the path if you have installed devkitARM somewhere else.
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

This project is essentially an integration of the projects listed below.\
Without them, this would not have been possible.

* [gba-link-connection](https://github.com/rodri042/gba-link-connection) : Game Boy Advance (GBA) C++ libraries to interact with the Serial Port.
* [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) : Gamepad firmware for the Raspberry Pi Pico
* [gba_03_multiboot](https://github.com/akkera102/gba_03_multiboot) : Raspberry Pi GBA Loader
    * It's integrated in [`GP2040-CE/src/gba/multiboot.cpp`](GP2040-CE/src/gba/multiboot.cpp)


# License

See the license of each project above.\
`/build.sh` is 0BSD.
