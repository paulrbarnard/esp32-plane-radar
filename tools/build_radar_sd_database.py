#!/usr/bin/env python3
"""Build the SD-card database used by Plane Radar.

The tool deliberately has no fixed online data provider.  Aircraft type data,
registry/owner data and photographs have different licences and update cycles.
Supply data files you are entitled to use, then copy the generated RADARDB
directory to a FAT-formatted microSD card.
"""

from __future__ import annotations

import argparse
import csv
import errno
import json
import re
import struct
import subprocess
import shutil
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

HEX = re.compile(r"^[0-9a-fA-F]{6}$")
TYPE = re.compile(r"^[A-Za-z0-9]{2,4}$")


def remove_tree(path: Path) -> None:
    """Remove a generated directory, tolerating macOS FAT metadata races."""
    def missing_ok(_func, _path, error):
        if error[1].errno != errno.ENOENT:
            raise error[1]
    shutil.rmtree(path, onerror=missing_ok)


def text(record: dict, *names: str) -> str:
    for name in names:
        value = record.get(name)
        if value is not None and str(value).strip():
            return str(value).strip()
    return ""


def build_types(source: Path, destination: Path) -> int:
    records = json.loads(source.read_text(encoding="utf-8"))
    if not isinstance(records, list):
        raise ValueError("type source must be a JSON array")
    count = 0
    seen: set[str] = set()
    output = destination / "types.jsonl"
    with output.open("w", encoding="utf-8") as stream:
        for record in records:
            designator = text(record, "Designator", "tdesig", "designator").upper()
            if not TYPE.fullmatch(designator) or designator in seen:
                continue
            seen.add(designator)
            payload = {
                "type": designator,
                "manufacturer": text(record, "ManufacturerCode", "manufacturer_code", "manufacturer"),
                "model": text(record, "ModelFullName", "model_name", "model"),
                "airframe": text(record, "AircraftDescription", "aircraft_desc", "airframe"),
                "engine_count": text(record, "EngineCount", "engine_count"),
                "engine_type": text(record, "EngineType", "engine_type"),
                "description": text(record, "Description", "description"),
                "wake_category": text(record, "WTC", "wtc"),
            }
            stream.write(json.dumps(payload, separators=(",", ":"), ensure_ascii=False) + "\n")
            count += 1
    return count


def commons_photo(registration: str, aircraft_hex: str, destination: Path) -> dict:
    """Fetch one attributed, 160px Wikimedia Commons thumbnail per registration."""
    if not registration:
        return {}
    query = urllib.parse.urlencode({
        "action": "query", "generator": "search", "gsrsearch": registration,
        "gsrnamespace": "6", "gsrlimit": "1", "prop": "imageinfo",
        "iiprop": "url|extmetadata", "iiurlwidth": "160", "format": "json",
    })
    try:
        with urllib.request.urlopen("https://commons.wikimedia.org/w/api.php?" + query, timeout=20) as response:
            result = json.load(response)
        page = next(iter(result.get("query", {}).get("pages", {}).values()))
        image = page["imageinfo"][0]
        thumbnail = image.get("thumburl")
        if not thumbnail:
            return {}
        temporary = destination / ".commons-source"
        with urllib.request.urlopen(thumbnail, timeout=30) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        target = destination / "photos" / f"{aircraft_hex}.bmp"
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["sips", "-s", "format", "bmp", "-z", "120", "160", str(temporary), "--out", str(target)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        temporary.unlink(missing_ok=True)
        metadata = image.get("extmetadata", {})
        value = lambda key: re.sub("<[^>]+>", "", metadata.get(key, {}).get("value", "")).strip()
        return {"photo": str(target.relative_to(destination)), "photo_credit": value("Artist") or value("Credit"),
                "photo_license": value("LicenseShortName"), "photo_url": image.get("descriptionurl", "")}
    except (KeyError, StopIteration, OSError, ValueError, subprocess.CalledProcessError):
        return {}


def build_registry(source: Path | None, photos: Path | None, destination: Path, commons: bool) -> int:
    if source is None:
        return 0
    records: list[tuple[str, dict]] = []
    with source.open(newline="", encoding="utf-8-sig") as stream:
        for record in csv.DictReader(stream):
            aircraft_hex = text(record, "hex", "icao", "icao24", "mode_s").lower()
            if not HEX.fullmatch(aircraft_hex):
                continue
            photo_name = ""
            if photos:
                for suffix in (".jpg", ".jpeg", ".png"):
                    photo = photos / f"{aircraft_hex}{suffix}"
                    if photo.is_file():
                        # Small BMP thumbnails use LVGL's built-in decoder and keep the
                        # shared LVGL component unmodified.
                        target = destination / "photos" / f"{aircraft_hex}.bmp"
                        target.parent.mkdir(parents=True, exist_ok=True)
                        try:
                            subprocess.run(
                                ["sips", "-s", "format", "bmp", "-z", "120", "160",
                                 str(photo), "--out", str(target)],
                                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                            )
                        except (FileNotFoundError, subprocess.CalledProcessError) as error:
                            raise RuntimeError(
                                "Photo conversion needs macOS 'sips'. Convert images to 160x120 PNG first."
                            ) from error
                        photo_name = str(target.relative_to(destination)).replace("\\", "/")
                        break
            payload = {
                "hex": aircraft_hex,
                "registration": text(record, "registration", "reg"),
                "owner": text(record, "owner", "registered_owner"),
                "operator": text(record, "operator", "ownop", "operator_name"),
                "year": text(record, "year", "year_built"),
                "source": text(record, "source"),
                "photo": photo_name,
            }
            if commons and not photo_name:
                payload.update(commons_photo(payload["registration"], aircraft_hex, destination))
            records.append((aircraft_hex, payload))
    records.sort(key=lambda item: item[0])
    with (destination / "aircraft.jsonl").open("wb") as data, (destination / "aircraft.idx").open("wb") as index:
        for aircraft_hex, payload in records:
            offset = data.tell()
            data.write((json.dumps(payload, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8"))
            index.write(aircraft_hex.encode("ascii") + struct.pack("<I", offset))
    return len(records)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build an SD-card RADARDB directory.")
    parser.add_argument("--types-json", type=Path, required=True, help="ICAO designator JSON source")
    parser.add_argument("--registry-csv", type=Path, help="Optional licensed registry CSV")
    parser.add_argument("--photos", type=Path, help="Optional directory of <icao-hex>.jpg/.png files")
    parser.add_argument("--wikimedia-photos", action="store_true", help="Fetch one attributed Commons thumbnail per registration")
    parser.add_argument("--output", type=Path, default=Path("build/RADARDB"), help="Generated database directory")
    parser.add_argument("--sd-root", type=Path, help="FAT-formatted SD-card mount point to receive RADARDB")
    parser.add_argument("--replace", action="store_true", help="Replace an existing RADARDB directory at --sd-root")
    args = parser.parse_args()

    root = args.output.resolve()
    if root.exists():
        remove_tree(root)
    root.mkdir(parents=True)
    type_count = build_types(args.types_json, root)
    aircraft_count = build_registry(args.registry_csv, args.photos, root, args.wikimedia_photos)
    manifest = {
        "format": 2,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "type_records": type_count,
        "aircraft_records": aircraft_count,
    }
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Built {root}: {type_count} type records, {aircraft_count} aircraft records")

    if args.sd_root:
        target = args.sd_root.resolve() / "RADARDB"
        if target.exists():
            if not args.replace:
                raise SystemExit(f"{target} exists; rerun with --replace to replace only that directory")
            remove_tree(target)
        shutil.copytree(root, target)
        print(f"Copied database to {target}")


if __name__ == "__main__":
    main()
