# Build environments

## Versions and setup

The reproducible reference is Arduino CLI 1.5.1, ESP8266 Arduino core 3.1.2, TFT_eSPI 2.5.43 and Python 3.9+. Install Arduino CLI from [its official installation guide](https://arduino.github.io/arduino-cli/latest/installation/), then run the core/library commands in the root README. Host tests additionally need `c++` (Xcode Command Line Tools on macOS or a C++ compiler on Linux). Run the shell scripts in a POSIX shell; Windows users can use a Linux environment configured with its own Arduino toolchain.

```sh
python3 scripts/generate_ota_credentials.py synology
sh scripts/build_firmware.sh synology
python3 scripts/generate_ota_credentials.py limits
sh scripts/build_firmware.sh limits
```

The compile target is `esp8266:esp8266:generic:eesz=4M1M,FlashMode=dio,FlashFreq=40`. GPIO definitions in `scripts/build_firmware.sh` match [the hardware table](hardware.md). `USER_SETUP_LOADED` bypasses TFT_eSPI's global `User_Setup.h`, so builds do not alter another project's library installation. Only the GLCD font is required.

Objects and images are under `build/<sample>/`; the shared Arduino cache defaults to `build/cache/`. All are ignored. To rebuild cleanly, remove that sample's generated build folder and run the build again. Do not erase your credential header.

## An isolated Arduino installation

The scripts accept `ARDUINO_CLI` and `ARDUINO_CONFIG_FILE`. The configuration must point at the data directory containing the core and the user directory containing TFT_eSPI.

```sh
ARDUINO_CLI=/absolute/path/to/kit/bin/arduino-cli \
ARDUINO_CONFIG_FILE=/absolute/path/to/kit/arduino-config.json \
sh scripts/build_firmware.sh synology
```

`ARDUINO_DIRECTORIES_DATA` and other Arduino environment overrides take precedence over configuration. If your shell already exports one, unset it or point it at the same isolated installation. `ARDUINO_BUILD_CACHE_PATH` optionally overrides the repository-local cache.

## Apple Silicon

An ARM64 Arduino CLI does not make the ESP8266 core's bundled host executables ARM64. The stock 3.1.2 macOS tool bundle can require Rosetta. Alternatively use an isolated native build kit whose Xtensa compiler, Arduino ctags, Python launcher and any filesystem tools run on ARM64. The samples were built with an ESPHome native `xtensa-lx106-elf` GCC 10.3.0-esphome.2 toolchain configured for this Arduino core.

A compiler replacement must retain the core's expected directory/recipe layout. Confirm versions and run both sample builds; do not assume an arbitrary compiler is compatible. This repository includes no downloaded executables, workstation paths or native-kit archive. A previously configured kit can be selected using the environment variables above.

## Image validation

`check_firmware_image.py` verifies the Arduino eboot header, application offset, embedded length and full-image CRC, then prints a SHA256. The CRC fields are zeroed when calculating the CRC, matching ESP8266 Arduino's image generation. It validates the reference 4 MB / DIO / 40 MHz format; it is not a general checker for every ESP8266 flash layout.

```sh
python3 scripts/check_firmware_image.py build/synology/SDP_synology_ota.bin
```

There is no automatic flash step in the build. Test on hardware separately, and keep a compatible 3.3 V UART recovery option available while developing firmware.
