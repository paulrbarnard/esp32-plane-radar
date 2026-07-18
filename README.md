# Plane Radar

Plane Radar is a native **ESP-IDF 5.5** application for the
**Waveshare ESP32-S3-Touch-LCD-2.1**: a 480×480 round ST7701 RGB display with
a CST820 capacitive touch controller. It presents live ADS-B aircraft around a
configured centre point, with classification, details, registry enrichment and
on-demand aircraft photographs.

Based on the [MakerWorld enclosure and assembly project](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083)
and the [original Plane Radar firmware](https://github.com/MatixYo/ESP32-Plane-Radar).

## In action

<p align="center">
  <img width="31%" alt="Plane Radar display with live classified aircraft" src="docs/images/radar-overview.jpeg" />
  <img width="31%" alt="Plane Radar aircraft information panel" src="docs/images/aircraft-information.jpeg" />
  <img width="31%" alt="Plane Radar aircraft registry and Wikimedia photo panel" src="docs/images/aircraft-registry.jpeg" />
</p>

## Features

- Native ESP-IDF RGB-panel rendering with PSRAM bounce buffers for a stable,
  tear-free display.
- CST820 touch input with generous aircraft and close-button hit targets.
- Live aircraft from [adsb.fi](https://opendata.adsb.fi/) refreshed about every
  three seconds while Wi-Fi is connected.
- Kilometre-only radar scales: **5, 10, 15, 20, and 25 km**.
- Directional aircraft icons: jets (blue), prop aircraft (amber), gliders
  (purple), helicopters (green), and unknown aircraft (yellow). A neutral-grey
  vector shows heading and relative speed.
- First-level touch panel with live altitude, speed, heading, ICAO type,
  manufacturer, model, airframe and engine information.
- **MORE** panel with registration, operator, owner, build year, UK CAA
  G-INFO enrichment, SD-card registry data, and an on-demand Wikimedia Commons
  image.
- Optional FAT-formatted microSD `RADARDB` database for type and registration
  data.

## Controls

| Input | Action |
|---|---|
| Short press **BOOT** (GPIO 0) | Cycle `5 → 10 → 15 → 20 → 25 km`; redraws the cached aircraft immediately. |
| Hold **BOOT** for 3 seconds | Clears saved ESP-IDF Wi-Fi credentials and Radar location/range settings, then restarts. |
| Tap an aircraft | Open the live aircraft-information panel. |
| Tap **MORE** | Open registry, CAA and photograph details. |
| Tap the panel or **×** | Close the active details panel. |

The selected range is retained in NVS. The default after clearing settings is
15 km.

## Wi-Fi and location

The native target runs as an ESP-IDF Wi-Fi station and reconnects using
credentials already stored in the ESP32's Wi-Fi NVS storage. The legacy
Arduino/WiFiManager captive portal is **not** part of the native `idf/` target.

The application currently uses the saved Radar location when present; otherwise
it defaults to **52.3676, 4.9041**. Consequently, do not use the three-second
BOOT reset unless you can provision the station credentials again. A native
configuration portal is not currently implemented.

## Build and flash

The supported firmware is the native ESP-IDF project in [`idf/`](idf/). It is
built with the shared ESP-IDF installation, not PlatformIO.

This workspace expects:

- ESP-IDF 5.5 at `/Volumes/Shed Data/ownCloud/Development/ESP32/esp-idf`
- ESP-IDF tools at `/Volumes/Shed Data/ownCloud/Development/ESP32/.espressif`
- the shared LVGL component at
  `/Volumes/Shed Data/ownCloud/Development/ESP32/Defender/LandyGauge/components/lvgl__lvgl`

Build:

```zsh
export IDF_TOOLS_PATH="/Volumes/Shed Data/ownCloud/Development/ESP32/.espressif"
source "/Volumes/Shed Data/ownCloud/Development/ESP32/esp-idf/export.sh"
cd "/Volumes/Shed Data/ownCloud/Development/ESP32/Radar/idf"
idf.py build
```

Flash and open the serial monitor for the usual board port:

```zsh
export IDF_TOOLS_PATH="/Volumes/Shed Data/ownCloud/Development/ESP32/.espressif"
source "/Volumes/Shed Data/ownCloud/Development/ESP32/esp-idf/export.sh"
cd "/Volumes/Shed Data/ownCloud/Development/ESP32/Radar/idf"
idf.py -p /dev/cu.usbmodem241201 flash monitor
```

Exit the monitor with `Ctrl+]`.

## SD-card database

The Radar looks for `RADARDB` on a FAT-formatted microSD card. It can contain a
compact ICAO type catalogue plus indexed registration, owner, operator and
year data. The device continues to work without a card using its small built-in
type catalogue and live ADS-B data.

Create and install a database with the supplied tools. Full input formats,
licensing notes and examples are in [`tools/README.md`](tools/README.md).

```zsh
python3 tools/build_radar_sd_database.py \
  --types-json ~/Downloads/aircraftDesignators.json \
  --sd-root /Volumes/RADAR_SD --replace
```

The tool never formats a card; it writes only the `RADARDB` directory to the
mounted FAT volume supplied by `--sd-root`.

## Live Wikimedia photographs

Photographs are requested only after the user opens **MORE**. To avoid direct
embedded-device rate limiting, the firmware uses the included trusted-LAN
relay, [`tools/radar_photo_proxy.py`](tools/radar_photo_proxy.py). Run it on a
computer with a stable LAN address:

```zsh
python3 tools/radar_photo_proxy.py --host 192.168.0.52 --port 8088
```

Keep the proxy on the private LAN only; do not bind it to `0.0.0.0` or expose it
to the Internet. If the proxy computer's address changes, update
`RADAR_PHOTO_PROXY` in [`idf/main/radar/radar_photo.c`](idf/main/radar/radar_photo.c)
and rebuild.

## Repository layout

```text
idf/                 Native ESP-IDF 5.5 application that is flashed to the board
idf/main/radar/      ADS-B, UI, range, SD, CAA and photograph modules
tools/               SD database builder, registry importers and photo proxy
docs/images/         Project photographs used by this README
partitions/          ESP-IDF partition-table definition
```

GitHub Actions firmware builds are intentionally disabled for this project.
