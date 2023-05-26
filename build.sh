#!/bin/bash

mkdir -p build/

cd gba-link-connection/examples/LinkSPI_demo/
make rebuild
cp LinkSPI_demo.mb.gba ../../../build/

cd ../../../build/
echo -e "#pragma once\ninline constexpr " > gba_rom.hpp
xxd -i LinkSPI_demo.mb.gba >> gba_rom.hpp

cd ../GP2040-CE/
mkdir build/
cd build/
cmake ..
make -j16
cp GP2040-CE_0.7.1_Pico.uf2 ../../build/gba-pico-gamepad.uf2
