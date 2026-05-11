# SoleSense Firmware v0.1 (DEMO-READY)

The original, hardware-verified firmware. Single-file Arduino sketch (~520 lines), raw 50 Hz CSV recording -> LittleFS, browser-side JS analysis. **This is what you flash for the live demo.**

> The post-demo modular rewrite lives in [`../SoleSenseV2/`](../SoleSenseV2/).
> Don't migrate to v0.2 until the open issues listed in [`../README.md`](../README.md) close.

## What this firmware does

- Brings up a WiFi AP: `SoleSense-XXXX` / pw `solesense`
- Serves the dao frontend (`software/frontend/solesense-v1/index.html`) from LittleFS at `/`
- Exposes the v0.1 HTTP API:
  - `GET /api/sensor` -- live one-shot FSR + IMU read
  - `POST /api/start` / `POST /api/stop` -- gate 50 Hz CSV recording into LittleFS
  - `GET /data.csv` -- download the recorded run for browser-side analysis
  - `POST /api/calibrate/zero` -- capture per-channel FSR baseline (averaged ~1 s)
  - `POST /api/calibrate/imu` -- capture IMU bias (held still ~1 s)
  - `POST /api/data/clear` -- wipe `/data.csv`
  - `GET /api/device` -- identity / firmware version / uptime
  - `POST /api/sleep` -- deep sleep, wake on D9 LOW
- Records at 50 Hz: 6 FSR channels (matrix-scan via D3/D10 power x A0/A1/A2 ADC) + 6 IMU axes
- All analysis (cadence, pressure distribution, injury flags) runs in the browser after Stop, against the downloaded CSV

## Why v0.1 is still the demo path

The frontend's JS analysis pipeline produces real, defensible numbers from the recorded CSV. The firmware itself is small, predictable, and has been hardware-verified end-to-end. There is no on-device FFT, no flash-slot ring buffer, no pause-on-disconnect -- and that simplicity is *why* it works reliably.

## Compile / flash

Sketch -- Arduino IDE (open `SoleSense.ino` and hit Upload), or terminal via the bundled arduino-cli:

```bash
ARDUINO_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$ARDUINO_CLI" compile --fqbn "esp32:esp32:XIAO_ESP32C3" firmware/SoleSense
"$ARDUINO_CLI" upload  --fqbn "esp32:esp32:XIAO_ESP32C3" --port /dev/cu.usbmodem2101 firmware/SoleSense
```

LittleFS data (the v0.1 frontend):

```bash
cp software/frontend/solesense-v1/index.html firmware/SoleSense/data/index.html
bash firmware/SoleSense/flash-littlefs.sh
```

The flash script auto-detects mklittlefs/esptool, port, and partition offsets. **Close any open Serial Monitor first** -- it locks the USB port and the upload will fail with `exit status 2`. If that happens, replug the XIAO and try again.

After flashing, verify:

```bash
curl --noproxy '*' -s http://192.168.4.1/api/device | python3 -m json.tool | head -3
```

`firmware` should read `SoleSense v0.1`.

## Pin map

```
GPIO5  / D3   -- FSR matrix power set 1 (heel + lateral arch + medial ball)
GPIO10 / D10  -- FSR matrix power set 2 (medial arch + lateral ball + toe)
GPIO2  / A0   -- ADC channel A
GPIO3  / A1   -- ADC channel B
GPIO4  / A2   -- ADC channel C
GPIO6  / SDA  -- MPU-6050 I^2C SDA
GPIO7  / SCL  -- MPU-6050 I^2C SCL
GPIO9  / D9   -- deep-sleep wake (active-LOW button to GND)
```

Each ADC pin needs a 10 kOhm pulldown to GND. The FSR matrix is documented in the [root README](../../README.md#pin-map).

## Notes

- Sketch state lives at the top of `SoleSense.ino` -- pin defs, sample rate, calibration counts, sleep timeout. Modify there if you need to retune.
- The IMU is optional -- if no MPU-6050 is wired, the firmware still records FSR data and the IMU columns in the CSV stay zero (or stale calibration value). Calibrate before treating IMU data as truth.
- This sketch will be retired once v0.2 closes the open issues in [`../SoleSenseV2/README.md`](../SoleSenseV2/README.md). Until then: **flash this for the demo.**
