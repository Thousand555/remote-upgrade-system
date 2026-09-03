#!/usr/bin/env python3
"""Verify the M10 HTTPS service, certificate trust, and Range contract."""

from __future__ import annotations

import argparse
import json
import ssl
import urllib.error
import urllib.request
from pathlib import Path


def request(url: str, context: ssl.SSLContext, headers: dict[str, str] | None = None):
    return urllib.request.urlopen(
        urllib.request.Request(url, headers=headers or {}),
        context=context,
        timeout=10,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="https://localhost:8443")
    parser.add_argument(
        "--ca-cert",
        type=Path,
        default=Path("firmware/esp32_gateway/components/firmware_downloader/certs/m10_ca.pem"),
    )
    parser.add_argument("--firmware-id", default="f407-node-1.2.0")
    args = parser.parse_args()

    base_url = args.base_url.rstrip("/")
    trusted = ssl.create_default_context(cafile=str(args.ca_cert.resolve()))

    with request(f"{base_url}/healthz", trusted) as response:
        if response.status != 200 or json.load(response) != {"status": "ok"}:
            raise RuntimeError("HTTPS health check failed")

    release_url = f"{base_url}/api/v1/firmwares/{args.firmware_id}"
    with request(f"{release_url}/manifest", trusted) as response:
        manifest = json.load(response)
        etag = response.headers.get("ETag")
        if response.status != 200 or not etag:
            raise RuntimeError("HTTPS manifest response is missing status or ETag")

    with request(
        f"{release_url}/binary",
        trusted,
        {"Range": "bytes=4096-8191", "If-Range": etag},
    ) as response:
        body = response.read()
        if response.status != 206 or len(body) != 4096:
            raise RuntimeError("HTTPS Range response did not return 4096 bytes with status 206")
        expected_range = f"bytes 4096-8191/{manifest['package_size']}"
        if response.headers.get("Content-Range") != expected_range:
            raise RuntimeError("HTTPS Content-Range does not match the manifest")

    try:
        with request(f"{base_url}/healthz", ssl.create_default_context()):
            pass
    except (urllib.error.URLError, ssl.SSLError):
        pass
    else:
        raise RuntimeError("The development HTTPS endpoint unexpectedly passed without its CA")

    print("[PASS] trusted HTTPS health and manifest")
    print("[PASS] trusted HTTPS Range/If-Range returned 206 and 4096 bytes")
    print("[PASS] untrusted development certificate was rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
