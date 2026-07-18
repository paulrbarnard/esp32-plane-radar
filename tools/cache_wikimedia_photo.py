#!/usr/bin/env python3
"""Add one attributed Wikimedia Commons thumbnail to an existing RADARDB."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path

HEX = re.compile(r"^[0-9a-fA-F]{6}$")


def commons_photo(registration: str, aircraft_hex: str, database: Path) -> dict:
    """Find one Commons image, convert it to a compact BMP, and return its credit."""
    query = urllib.parse.urlencode({
        "action": "query", "generator": "search", "gsrsearch": registration,
        "gsrnamespace": "6", "gsrlimit": "1", "prop": "imageinfo",
        "iiprop": "url|extmetadata", "iiurlwidth": "160", "format": "json",
    })
    request = urllib.request.Request(
        "https://commons.wikimedia.org/w/api.php?" + query,
        headers={"User-Agent": "PlaneRadar/1.0 (personal ESP32 display)"},
    )
    with urllib.request.urlopen(request, timeout=20) as response:
        result = json.load(response)
    pages = result.get("query", {}).get("pages", {})
    page = next(iter(pages.values()))
    image = page["imageinfo"][0]
    thumbnail = image.get("thumburl")
    if not thumbnail:
        raise RuntimeError("Commons supplied no thumbnail")
    temporary = database / ".commons-source"
    try:
        image_request = urllib.request.Request(
            thumbnail, headers={"User-Agent": "PlaneRadar/1.0 (personal ESP32 display)"}
        )
        with urllib.request.urlopen(image_request, timeout=30) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        target = database / "photos" / f"{aircraft_hex.lower()}.bmp"
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["sips", "-s", "format", "bmp", "-z", "120", "160", str(temporary), "--out", str(target)],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
    finally:
        temporary.unlink(missing_ok=True)
    metadata = image.get("extmetadata", {})
    def value(key: str) -> str:
        return re.sub("<[^>]+>", "", metadata.get(key, {}).get("value", "")).strip()
    return {
        "photo": str(target.relative_to(database)).replace("\\", "/"),
        "photo_credit": value("Artist") or value("Credit") or "Wikimedia Commons",
        "photo_license": value("LicenseShortName"),
        "photo_url": image.get("descriptionurl", ""),
    }


def update_record(database: Path, aircraft_hex: str) -> None:
    source = database / "aircraft.jsonl"
    index = database / "aircraft.idx"
    if not source.is_file() or not index.is_file():
        raise RuntimeError(f"{database} is not a RADARDB directory")
    target_hex = aircraft_hex.lower()
    target_payload = None
    with source.open(encoding="utf-8") as stream:
        for line in stream:
            payload = json.loads(line)
            if payload.get("hex", "").lower() == target_hex:
                target_payload = payload
                break
    if not target_payload:
        raise RuntimeError(f"ICAO hex {aircraft_hex} is not present in {source}")
    registration = target_payload.get("registration", "")
    if not registration:
        raise RuntimeError(f"{aircraft_hex} has no registration mark in this database")
    target_payload.update(commons_photo(registration, target_hex, database))
    # Regenerate the fixed-record index with the rewritten JSONL offsets.
    with tempfile.NamedTemporaryFile("wb", delete=False, dir=database, prefix=".aircraft.") as data_out, \
         tempfile.NamedTemporaryFile("wb", delete=False, dir=database, prefix=".index.") as index_out:
        data_name, index_name = data_out.name, index_out.name
        with source.open(encoding="utf-8") as stream:
            for line in stream:
                payload = json.loads(line)
                if payload.get("hex", "").lower() == target_hex:
                    payload = target_payload
                offset = data_out.tell()
                data_out.write((json.dumps(payload, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8"))
                index_out.write(payload["hex"].lower().encode("ascii") + struct.pack("<I", offset))
    os.replace(data_name, source)
    os.replace(index_name, index)


def main() -> None:
    parser = argparse.ArgumentParser(description="Cache one Commons aircraft photograph in an existing RADARDB.")
    parser.add_argument("--radardb", type=Path, required=True, help="Existing RADARDB directory on the mounted SD card")
    parser.add_argument("--hex", required=True, help="Six-digit ICAO address, e.g. 406755")
    args = parser.parse_args()
    if not HEX.fullmatch(args.hex):
        raise SystemExit("--hex must be exactly six hexadecimal characters")
    update_record(args.radardb.resolve(), args.hex)
    print(f"Cached Commons photo for {args.hex.lower()} in {args.radardb}")


if __name__ == "__main__":
    main()
