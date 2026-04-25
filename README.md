# SoleSense
### Injury Prevention Running Insole
**Spring 2026**

---

## What Is SoleSense

SoleSense is a 3D printed TPU insole embedded with pressure sensors
and a motion sensor that records biomechanical data during a run.

After the run, the insole connects to WiFi and sends your data to a
web app that generates a report that uses existing data to identify injury risk
patterns and how to fix your running form.

---

## How It Works

1. Insert SoleSense into your running shoe
2. Run normally 8 FSR pressure sensors and an IMU record
   14 channels of data at 40Hz to onboard flash memory
3. After you run the insole connects to WiFi and transmits your data
4. Open the SoleSense web app on any device
5. Provides Biomechanical results

---

## What It Detects

- Heel striking
- High loading rate
- Low cadence
- Overpronation
- Supination
- Bilateral asymmetry
- Ground contact time too long

All 7 flags are sourced from peer-reviewed biomechanics literature.

---

## Hardware

| Component | Purpose |
|---|---|
| Seeed XIAO ESP32C3 | Microcontroller — reads all sensors at up to 160hz, transmits data over WiFi |
| MPU6050 IMU | 3-axis accelerometer + 3-axis gyroscope |
| FSR 402 x8 | Pressure sensors distributed across insole |
| LIR2032 x3 per insole | 3.6V rechargeable coin cell batteries |
| TPU 85A | Insole material — honeycomb or gyroid infill 20–25% |

---

## Tech Stack

**Firmware:** C++ — Arduino IDE with ESP32C3 board support

**Frontend:** HTML, CSS, JavaScript — single page stored on
ESP32C3 flash. JavaScript fetches run data as CSV directly
from the ESP32 and renders the full injury report in the browser.
No external server required.

**Data Format:** CSV stored on ESP32C3 onboard flash via SPIFFS

**CAD:** Fusion 360

---

## Architecture

'''
SoleSense/
├── hardware/
│   ├── cad/          # Fusion 360 files and STL exports
│   ├── electrical/   # Wiring diagrams and schematics
│   └── pcb/          # PCB design files
├── firmware/         # ESP32C3 C++ firmware
├── software/
│   └── frontend/     # HTML, CSS, JavaScript web app
├── docs/             # Engineering report and research
├── assets/           # Images and diagrams
└── README.md
'''

## Team

| Name | Role |
|---|---|
| Jordan Chan | Mechanical Lead |
| Kiara Peters | Mechanical |
| James Kim | Mechanical |
| Daniel Grivennikov | Mechanical |
| Ethan Kim | Mechanical |
| Maxim Varakuta | Mechanical |
| Norton Hoong | Electrical & Firmware Lead |
| Dao Doan | Firmware / Software |
| Andony Velasquez | Software Lead |
| Jasmine Dhaliwal | Software |
| Natalie Dai | Software |

