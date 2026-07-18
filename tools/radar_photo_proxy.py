#!/usr/bin/env python3
"""LAN-only, on-demand Wikimedia thumbnail relay for Plane Radar.

It is deliberately not a cache or a scraper: one Radar MORE request causes at
most one Commons metadata request and one 160px thumbnail request. Bind it to
the Mac's LAN address only, never to 0.0.0.0 or an Internet-facing interface.
"""
from __future__ import annotations

import argparse
import json
import re
import time
import urllib.parse
import urllib.request
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_IMAGE_BYTES = 192 * 1024
REGISTRATION = re.compile(r"^[A-Za-z0-9-]{2,12}$")
USER_AGENT = "PlaneRadarPhotoProxy/1.0 (personal ESP32 aircraft display)"


def commons_thumbnail(registration: str) -> tuple[bytes, str, str]:
    query = urllib.parse.urlencode({
        "action": "query", "generator": "search", "gsrsearch": registration,
        "gsrnamespace": "6", "gsrlimit": "5", "prop": "imageinfo",
        "iiprop": "url|extmetadata", "iiurlwidth": "160", "iiurlheight": "120", "format": "json",
    })
    request = urllib.request.Request("https://commons.wikimedia.org/w/api.php?" + query,
                                     headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=15) as response:
        pages = json.load(response).get("query", {}).get("pages", {}).values()
    upper_mark = registration.upper()
    choices = list(pages)
    # Never fall back to an unrelated Commons search result: a registration
    # miss must be shown as a miss, not as a random aircraft or PDF preview.
    page = next((item for item in choices if upper_mark in item.get("title", "").upper()), None)
    if not page or not page.get("imageinfo"):
        raise FileNotFoundError("no Commons image")
    image = page["imageinfo"][0]
    url = image.get("thumburl")
    if not url:
        raise FileNotFoundError("no Commons thumbnail")
    image_request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "image/jpeg"})
    with urllib.request.urlopen(image_request, timeout=20) as response:
        content_type = response.headers.get_content_type()
        payload = response.read(MAX_IMAGE_BYTES + 1)
    if content_type != "image/jpeg" or len(payload) > MAX_IMAGE_BYTES or not payload.startswith(b"\xff\xd8"):
        raise ValueError("Commons did not return a suitable JPEG thumbnail")
    metadata = image.get("extmetadata", {})
    plain = lambda name: re.sub(r"<[^>]+>", "", metadata.get(name, {}).get("value", "")).strip()
    return payload, plain("Artist") or plain("Credit") or "Wikimedia Commons", plain("LicenseShortName")


class PhotoHandler(BaseHTTPRequestHandler):
    last_request = 0.0

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"{self.client_address[0]} - {fmt % args}", flush=True)

    def respond(self, status: HTTPStatus, text: str) -> None:
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        request = urllib.parse.urlparse(self.path)
        if request.path == "/health":
            self.respond(HTTPStatus.OK, "Radar photo proxy ready\n")
            return
        if request.path != "/photo":
            self.respond(HTTPStatus.NOT_FOUND, "Use /photo?registration=G-EZWD\n")
            return
        registration = urllib.parse.parse_qs(request.query).get("registration", [""])[0].upper()
        if not REGISTRATION.fullmatch(registration):
            self.respond(HTTPStatus.BAD_REQUEST, "invalid registration\n")
            return
        now = time.monotonic()
        if now - type(self).last_request < 2:
            self.respond(HTTPStatus.TOO_MANY_REQUESTS, "wait before the next photo request\n")
            return
        type(self).last_request = now
        try:
            image, credit, licence = commons_thumbnail(registration)
        except FileNotFoundError:
            self.respond(HTTPStatus.NOT_FOUND, "no Commons image\n")
            return
        except Exception as error:
            self.respond(HTTPStatus.BAD_GATEWAY, f"Commons request failed: {error}\n")
            return
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "image/jpeg")
        self.send_header("Content-Length", str(len(image)))
        self.send_header("X-Photo-Credit", credit[:160])
        self.send_header("X-Photo-License", licence[:48])
        self.end_headers()
        self.wfile.write(image)


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve live Commons thumbnails to Radar on a trusted LAN.")
    parser.add_argument("--host", default="192.168.0.52", help="Mac LAN address to bind (never use 0.0.0.0)")
    parser.add_argument("--port", default=8088, type=int)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), PhotoHandler)
    print(f"Radar photo proxy listening on http://{args.host}:{args.port} (LAN only)", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
