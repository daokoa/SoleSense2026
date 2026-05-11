#!/bin/bash
# flash-littlefs.sh -- build and flash the LittleFS data folder to the XIAO ESP32-C3
#
# Run from anywhere:
#   bash firmware/SoleSense/flash-littlefs.sh
# or make it executable and run direct:
#   chmod +x firmware/SoleSense/flash-littlefs.sh
#   firmware/SoleSense/flash-littlefs.sh
#
# Override the serial port if auto-detect picks the wrong one:
#   PORT=/dev/cu.usbmodem2101 firmware/SoleSense/flash-littlefs.sh

set -e

# -- Locate Arduino-supplied tools ----------------------------------------------
MKLITTLEFS=$(ls -1 ~/Library/Arduino15/packages/esp32/tools/mklittlefs/*/mklittlefs 2>/dev/null | head -1)
ESPTOOL=$(ls -1 ~/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | head -1)

if [ -z "$MKLITTLEFS" ]; then
  echo "error: mklittlefs not found under ~/Library/Arduino15/packages/esp32/tools/mklittlefs/" >&2
  echo "       Install the ESP32 board package in Arduino IDE first." >&2
  exit 1
fi
if [ -z "$ESPTOOL" ]; then
  echo "error: esptool not found under ~/Library/Arduino15/packages/esp32/tools/esptool_py/" >&2
  exit 1
fi

# -- Locate the data folder (sibling of this script) ---------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="$SCRIPT_DIR/data"

if [ ! -d "$DATA_DIR" ]; then
  echo "error: data directory not found at $DATA_DIR" >&2
  exit 1
fi
if [ ! -f "$DATA_DIR/index.html" ]; then
  echo "warning: $DATA_DIR/index.html does not exist -- flashing an empty filesystem" >&2
fi

# -- Detect the XIAO's USB port ------------------------------------------------
if [ -z "$PORT" ]; then
  PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
fi
if [ -z "$PORT" ]; then
  echo "error: no /dev/cu.usbmodem* found. Is the XIAO plugged in?" >&2
  echo "       List ports: ls /dev/cu.*" >&2
  echo "       Override:   PORT=/dev/cu.usbmodem2101 $0" >&2
  exit 1
fi

# -- Partition layout for 'Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)' -
OFFSET=0x290000
SIZE=0x160000        # 1441792 bytes = 1.5 MB
PAGE=256
BLOCK=4096

IMG="/tmp/solesense-littlefs-$$.bin"

# -- Build the image -----------------------------------------------------------
echo "==> Building LittleFS image"
echo "    source:   $DATA_DIR"
echo "    size:     $SIZE ($(printf '%d' $SIZE) bytes)"
echo "    output:   $IMG"
"$MKLITTLEFS" -c "$DATA_DIR" -s "$SIZE" -p "$PAGE" -b "$BLOCK" "$IMG"

# -- Flash it ------------------------------------------------------------------
echo ""
echo "==> Flashing to XIAO ESP32-C3"
echo "    port:     $PORT"
echo "    offset:   $OFFSET"
echo "    tool:     $ESPTOOL"
echo ""
echo "    If esptool can't enter download mode, hold the BOOT button on the"
echo "    XIAO while it connects, then release."
echo ""
"$ESPTOOL" --chip esp32c3 --port "$PORT" --baud 921600 write_flash "$OFFSET" "$IMG"

rm -f "$IMG"

echo ""
echo "==> Done"
echo "    Reset the XIAO (or unplug + replug USB)."
echo "    On phone/laptop connected to SoleSense WiFi, hard-refresh http://192.168.4.1/"
