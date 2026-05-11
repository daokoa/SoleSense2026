# SoleSense Firmware -- PlatformIO build (stub)

A skeletal PlatformIO project for the XIAO ESP32-C3. **Not currently used for the demo or for v0.2 development** -- both of those run through the Arduino IDE paths in [`../SoleSense/`](../SoleSense/) and [`../SoleSenseV2/`](../SoleSenseV2/).

## What's here

```
firmware/platformio/
|-- platformio.ini      <- board/framework config
|-- src/main.cpp        <- ~76 lines: dummy random sensor data, alt SSID, ArduinoOTA
|-- include/            <- (empty)
|-- lib/                <- (empty)
`-- test/               <- (empty)
```

`src/main.cpp` brings up an AP (SSID `XIAO-ESP32`, pw `12345678` -- different from the v0.1/v0.2 SSIDs) and serves a couple of test endpoints with **fake** randomly-generated FSR/IMU values. It's a build-system smoke test, not a working firmware.

## When you might use this

- You want to migrate the build off Arduino IDE 2 onto VS Code + PlatformIO.
- You want OTA flashing (the stub already imports `ArduinoOTA`).
- You want library-pinning + a proper test harness.

If you go this route, port the contents of `../SoleSense/SoleSense.ino` (v0.1) or `../SoleSenseV2/` (v0.2) into `src/` and adapt `platformio.ini` to pick up the libraries (`ESPAsyncWebServer`, `AsyncTCP`, `LittleFS_esp32`, `Adafruit_MPU6050`, `Adafruit_Sensor`).

## Compile / flash

```bash
cd firmware/platformio
pio run --target upload     # sketch
pio run --target uploadfs   # LittleFS (needs platformio/data/index.html)
pio device monitor          # open serial console
```

## Status

Stub. Until someone ports v0.1 or v0.2 in here, **don't flash this onto a board you care about** -- it'll bring up an AP with a different SSID and serve fake data, which will confuse anything pointed at the real firmware.
