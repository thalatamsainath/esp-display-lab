#!/bin/sh
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sample=${1:-synology}
case "$sample" in
  synology) sketch=esp_synology_display ;;
  limits) sketch=esp_ai_limits ;;
  *) echo 'Usage: sh scripts/build_firmware.sh [synology|limits]' >&2; exit 2 ;;
esac
arduino_cli=${ARDUINO_CLI:-arduino-cli}
credentials="$repo_root/firmware/$sketch/ota_credentials.h"
if [ ! -f "$credentials" ]; then
  echo "Create credentials first: python3 scripts/generate_ota_credentials.py $sample" >&2
  exit 1
fi
if grep -q 'CHANGE_ME' "$credentials"; then
  echo 'Replace the example OTA password before building.' >&2
  exit 1
fi
# TFT_eSPI's setup is applied to this build only, never to the global library.
tft_flags='-DUSER_SETUP_LOADED -DST7789_DRIVER -DTFT_WIDTH=240 -DTFT_HEIGHT=240 -DTFT_MOSI=13 -DTFT_SCLK=14 -DTFT_CS=-1 -DTFT_DC=0 -DTFT_RST=2 -DTFT_BL=5 -DTFT_BACKLIGHT_ON=LOW -DLOAD_GLCD -DSMOOTH_FONT -DSPI_FREQUENCY=20000000'
mkdir -p "$repo_root/build/$sample"
export ARDUINO_BUILD_CACHE_PATH="${ARDUINO_BUILD_CACHE_PATH:-$repo_root/build/cache}"
set -- compile --fqbn esp8266:esp8266:generic:eesz=4M1M,FlashMode=dio,FlashFreq=40 \
  --build-property "compiler.cpp.extra_flags=$tft_flags" \
  --build-path "$repo_root/build/$sample/objects" \
  --output-dir "$repo_root/build/$sample" "$repo_root/firmware/$sketch"
if [ -n "${ARDUINO_CONFIG_FILE:-}" ]; then
  set -- "$@" --config-file "$ARDUINO_CONFIG_FILE"
fi
"$arduino_cli" "$@"
firmware="$repo_root/build/$sample/SDP_${sample}_ota.bin"
cp "$repo_root/build/$sample/$sketch.ino.bin" "$firmware"
python3 "$repo_root/scripts/check_firmware_image.py" "$firmware"
echo "Firmware: $firmware"
