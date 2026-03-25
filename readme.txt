
# Install PICO-SDK (or update it)
Use pico-sdk 2.2.0 (for RP2350, pico2)
------------------------------------
git clone -b master https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk/
git submodule update --init
REPLACE tinyusb with latest version:
cd lib
mv tinyusb tinyusb_old
git clone https://github.com/hathach/tinyusb.git
mv tinyusb-master tinyusb
cd tinyusb
python3 tools/get_deps.py rp2040

# Export PICO_SDK_PATH (e.g.)
export PICO_SDK_PATH=/Users/jean-marcharvengt/Documents/pico/pico-sdk-2.2.0 (path to pico-sdk!)


#setup arm toolchain 
Use gcc-arm-none-eabi-10.3-2021.10 toolchain from https://developer.arm.com/downloads/-/gnu-rm
Confirmed working on macOS with vkey usb tool!
export PICO_TOOLCHAIN_PATH=/Users/jean-marcharvengt/Documents/pico/gcc-arm-none-eabi-10.3-2021.10/bin

Or try latest from https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
Not working for me on M1!
export PICO_TOOLCHAIN_PATH=/Users/jean-marcharvengt/Documents/pico/arm-gnu-toolchain-12.2.rel1-darwin-arm64-arm-none-eabi/bin

Avoid brew executables on OSX as you only get latest version which seems problematic!


# Compile the project (ARM)
Go to project dir:
mkdir build
cd build
pico2/2w: 
cmake -DPICO_PLATFORM=rp2350 -DPICO_BOARD=pico2 ..
make


# Compile the project (RISCV)
Never tried....
cmake -DPICO_PLATFORM=rp2350-riscv -DPICO_RISCV_ABI=rv32imac -DPICO_BOARD=pico2 ..
make

# Compile vkey USB tool (OSX)
brew install libusb 
(adapt Makefile for latest libusb path)
make
