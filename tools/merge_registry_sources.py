#!/usr/bin/env python3
"""Merge national registers into an ICAO-hex CSV for build_radar_sd_database.py.

Official registers use registration marks while the radar uses the Mode-S/ICAO
address.  An OpenSky metadata CSV supplies that address-to-registration bridge.
Official fields override OpenSky; OpenSky supplies any remaining operator data.
"""
from __future__ import annotations

import argparse
import csv
import io
import urllib.request
import zipfile
from pathlib import Path

csv.field_size_limit(16 * 1024 * 1024)

FAA_URL = "https://registry.faa.gov/database/ReleasableAircraft.zip"


def value(record: dict, *names: str) -> str:
    for name in names:
        found = record.get(name, "")
        if str(found).strip():
            return str(found).strip()
    return ""


def normalise_registration(mark: str) -> str:
    return "".join(ch for ch in mark.upper() if ch.isalnum())


def read_opensky(path: Path) -> dict[str, dict]:
    mapped: dict[str, dict] = {}
    with path.open(newline="", encoding="utf-8-sig") as stream:
        # OpenSky publishes this CSV with single-quoted fields.
        for row in csv.DictReader(stream, quotechar="'"):
            registration = normalise_registration(value(row, "registration", "reg"))
            aircraft_hex = value(row, "icao24", "hex", "icao") .lower()
            if registration and len(aircraft_hex) == 6:
                mapped[registration] = {"hex": aircraft_hex, "registration": value(row, "registration", "reg"),
                                        "owner": value(row, "owner"), "operator": value(row, "operator"),
                                        "year": value(row, "year", "year_built"),
                                        "source": "OpenSky aircraft metadata"}
    return mapped


def download_faa() -> list[str]:
    request = urllib.request.Request(FAA_URL, headers={"User-Agent": "PlaneRadar/1.0 (personal SD database builder)"})
    with urllib.request.urlopen(request, timeout=120) as response:
        archive = zipfile.ZipFile(io.BytesIO(response.read()))
    master = next(name for name in archive.namelist() if name.upper().endswith("MASTER.TXT"))
    return archive.read(master).decode("latin-1").splitlines()


def apply_faa(records: dict[str, dict]) -> None:
    for line in download_faa():
        # FAA MASTER.txt fixed fields: N-number, serial, model code, year, registrant name.
        mark = normalise_registration("N" + line[0:5].strip())
        current = records.get(mark)
        if not current:
            continue
        year, owner = line[49:53].strip(), line[54:104].strip()
        if owner: current["owner"] = owner
        if year.isdigit(): current["year"] = year
        current["source"] = "FAA + OpenSky map"


def apply_national_csv(records: dict[str, dict], source: Path) -> None:
    """Overlay a lawful national-register CSV by registration mark.

    Accepted headings are registration/reg, owner/registered_owner, operator/
    aoc_holder, and year/year_built. Unknown columns are deliberately ignored.
    """
    updates = 0
    with source.open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            current = records.get(normalise_registration(value(row, "registration", "reg")))
            if not current:
                continue
            for target, aliases in {
                "owner": ("owner", "registered_owner"),
                "operator": ("operator", "aoc_holder", "ownop"),
                "year": ("year", "year_built", "year_of_manufacture"),
            }.items():
                replacement = value(row, *aliases)
                if replacement:
                    current[target] = replacement
            current["source"] = f"{source.stem} + OpenSky map"
            updates += 1
    print(f"Applied {updates} matching records from {source}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Create a radar registry CSV from national sources.")
    parser.add_argument("--opensky-csv", type=Path, required=True, help="OpenSky aircraft metadata CSV")
    parser.add_argument("--faa", action="store_true", help="Download and merge the current FAA master register")
    parser.add_argument("--national-csv", type=Path, action="append", default=[],
                        help="National-register CSV to overlay (may be repeated)")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    records = read_opensky(args.opensky_csv)
    if args.faa:
        apply_faa(records)
    for source in args.national_csv:
        apply_national_csv(records, source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=["hex", "registration", "owner", "operator", "year", "source"])
        writer.writeheader()
        writer.writerows(sorted(records.values(), key=lambda row: row["hex"]))
    print(f"Wrote {len(records)} mapped aircraft to {args.output}")


if __name__ == "__main__":
    main()
