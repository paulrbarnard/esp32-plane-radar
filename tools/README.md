# Radar SD database builder

`build_radar_sd_database.py` creates the `RADARDB` directory read by the
Waveshare Radar firmware. It never formats a card. The generated directory can
be copied to any FAT-formatted microSD card; use `--sd-root` to copy it to an
already-mounted card.

Type-level data is an ICAO-designator JSON array. The builder recognises the
field names used by common Doc 8643 exports: `Designator`, `ManufacturerCode`,
`ModelFullName`, `AircraftDescription`, `EngineCount`, `EngineType`, and `WTC`.

Registry data is optional and must be data you are authorised to use. Supply a
CSV containing a six-character `hex` (or `icao`/`icao24`) column, with optional
`registration`, `owner`, `operator`, and `year` columns. Put any licensed photo
files in a directory named `<hex>.jpg`, `<hex>.jpeg`, or `<hex>.png`. On macOS,
the builder converts each photo to a 160×120 BMP thumbnail for the MORE panel.

Example:

```zsh
python3 tools/build_radar_sd_database.py \
  --types-json ~/Downloads/aircraftDesignators.json \
  --registry-csv ~/Downloads/aircraft_registry.csv \
  --photos ~/Downloads/aircraft_photos \
  --sd-root /Volumes/RADAR_SD --replace
```

The card receives:

```text
RADARDB/
  manifest.json
  types.jsonl
  aircraft.jsonl
  aircraft.idx
  photos/400123.bmp
```

## Live Wikimedia photographs through a trusted LAN proxy

The ESP32 asks for a photograph only when **MORE** is opened. Wikimedia
Commons may rate-limit direct embedded-device requests, so the included proxy
can relay that one on-demand request through a Mac or another trusted computer
on the same home network. It is not a cache and does not bulk-download or
scrape Commons.

1. Give the computer running the proxy a stable LAN address (or update the
   firmware proxy address when it changes).
2. Start the proxy, binding it only to that LAN address:

   ```zsh
   python3 tools/radar_photo_proxy.py --host 192.168.0.52 --port 8088
   ```

3. Check it locally from another device on the LAN:

   ```zsh
   curl --fail http://192.168.0.52:8088/health
   ```

Do not bind the proxy to `0.0.0.0`, expose it to the Internet, or run it
through a public port-forward. It accepts only one registration query at a
time, rate-limits requests, and returns a 160px JPEG only when the Commons
result title explicitly contains that registration.

The firmware proxy endpoint is defined by `RADAR_PHOTO_PROXY` in
`idf/main/radar/radar_photo.c`. Change the address there if the proxy host has
a different LAN IP, then rebuild and flash the firmware.

`types.jsonl` is a single compact catalogue. `aircraft.idx` is a fixed-record
index into `aircraft.jsonl`, so the device can resolve an ICAO hex address
without scanning a large registration database.

## Add a selected Commons photograph

Do not bulk-download Commons photos for the whole database. Add a photograph
only for a specific aircraft requested from the Radar's **MORE** panel. This
downloads one 160×120 thumbnail, stores its creator/licence attribution and
rebuilds only the lookup index. For example, G-EZWD is ICAO address `406755`:

```zsh
python3 tools/cache_wikimedia_photo.py \
  --radardb /Volumes/DISK_IMG/RADARDB --hex 406755
```

Reinsert the card after the command finishes. The MORE panel will then show
the thumbnail and its Commons attribution. The command makes one Commons API
lookup and one thumbnail download; it does not scrape or prefetch a catalogue.

## Registry-source merge

`merge_registry_sources.py` joins national registration data to the ICAO hex
address used by the Radar. Start with an OpenSky metadata CSV (the bridge from
registration mark to ICAO address), then overlay authoritative sources. The
first automated overlay is the FAA's current master register:

```zsh
python3 tools/merge_registry_sources.py \
  --opensky-csv ~/Downloads/opensky-aircraft-metadata.csv \
  --faa --output ~/Downloads/radar_registry.csv
```

Use `radar_registry.csv` as `--registry-csv` in the database-builder command.
Official CSV/XLSX data from other national registers, including a licensed UK
CAA G-INFO export, will be added as source-specific overlays rather than
scraped from read-only web search pages.

For CASA, Canada, or a G-INFO workbook exported to CSV, use one or more
`--national-csv` arguments. The importer recognises `registration` (or `reg`),
`owner` (or `registered_owner`), `operator` (or `aoc_holder`), and `year` (or
`year_built`) headings:

```zsh
python3 tools/merge_registry_sources.py \
  --opensky-csv ~/Downloads/opensky-aircraft-metadata.csv \
  --faa --national-csv ~/Downloads/casa-register.csv \
  --national-csv ~/Downloads/g-info-export.csv \
  --output ~/Downloads/radar_registry.csv
```
