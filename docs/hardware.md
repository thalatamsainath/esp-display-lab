# Hardware, Wi-Fi and recovery

## Reference target

The reference is the GeekMagic SmallTV-Ultra style ESP12F mini display: ESP8266, 4 MB SPI flash and a 240 × 240 ST7789 panel. Similar-looking AliExpress displays can use different controllers, pins or flash sizes. Check your exact board.

| Signal | Connection |
| --- | --- |
| LCD MOSI | GPIO 13 |
| LCD SCLK | GPIO 14 |
| LCD CS | Tied to ground; `TFT_CS=-1` |
| LCD DC | GPIO 0 |
| LCD reset | GPIO 2 |
| Backlight | GPIO 5, active LOW |
| SPI frequency | 20 MHz |
| Flash settings | 4 MB, 1 MB filesystem, DIO, 40 MHz |

[hardware/User_Setup.h](../hardware/User_Setup.h) is a readable reference for these definitions. The build script supplies them as flags, so copying it over your global TFT_eSPI configuration is unnecessary.

GPIO 0 and GPIO 2 also participate in ESP8266 boot strapping. Avoid changing their reset-time behavior casually. The samples use rotation 0; panel variants may need an adjusted rotation or controller-specific offsets.

## Wi-Fi and EEPROM

Both samples keep the inherited credential layout: bytes 0–31 for SSID and bytes 32–95 for password. They connect as a station at boot, then provide an AP if connection fails. Wi-Fi is normally entered through the web UI rather than compiled into source.

The NAS sample reserves byte 96 for marker `0xB4` and byte 97 for brightness, using an EEPROM length of 128. It preserves bytes 0–95 when saving brightness. The limits sample uses the 96-byte credential layout; switching to it can discard settings beyond that range. Document and preserve shared fields when adding a new sketch.

Fallback AP: `MiniScreen-Setup`, default setup password `12345678`, setup page `http://192.168.4.1`. This is a public sample setup password, not a user's network credential. It can be customized consistently in your local firmware.

## OTA

The samples install `ESP8266HTTPUpdateServer` at `/update`. Browser authentication uses the currently installed image's credential header. A newly compiled header controls authentication only after that image is installed.

Use the firmware upload field. These samples do not require a filesystem image. Flash layout, running-image size and temporary OTA space must all be compatible. A stock `SDP_` filename gate is separate from binary compatibility. A device that cannot boot or connect cannot be rescued through its web updater.

## UART recovery

Use a complete USB-to-UART adapter with verified **3.3 V logic**, suitable target power, TX/RX/GND connections, and the ESP8266 bootloader wiring for your board. Hold GPIO 0 low while resetting to enter the ROM serial loader. Refer to [Espressif's boot mode guidance](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html) and the upstream board instructions before wiring.

A CH340C is a bridge chip; you do not need that exact chip if another appropriate UART adapter is available. A CH341A SPI/BIOS programmer is not automatically a UART adapter. Its board must support the needed mode and expose verified 3.3 V TX/RX/GND; some variants power a target at 3.3 V while using unsafe logic levels. Direct SPI-flash programming is a different operation with different access and image-layout requirements.
