add_library(TinyUSB_Gamepad
src/hid_driver.cpp
src/net_driver.cpp
src/tusb_driver.cpp
src/usb_descriptors.cpp
src/xinput_driver.cpp
src/ps4_driver.cpp
)
target_include_directories(TinyUSB_Gamepad PUBLIC 
src
${CMAKE_SOURCE_DIR}/headers
${CMAKE_SOURCE_DIR}/lib/CRC32/src
${CMAKE_SOURCE_DIR}/lib/mbedtls/include
)
target_link_libraries(TinyUSB_Gamepad 
pico_stdlib
pico_mbedtls
tinyusb_device
rndis
)