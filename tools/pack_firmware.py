"""Create an immutable M8 firmware release for the local PC server.

The downloadable firmware.bin is the exact M7 cache package accepted by the
ESP32 firmware_store component. M9 can therefore download it directly into
the stm_fw partition without translating or buffering the STM32 image.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from firmware_package import (
    DEFAULT_HARDWARE_ID,
    DEFAULT_PRODUCT_ID,
    HEADER_SIZE,
    IMAGE_OFFSET,
    build_package,
    pad_image,
    parse_int,
    parse_package,
)


APP_BASE_ADDRESS = "0x08020000"
FIRMWARE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def current_git_commit(repository_root: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=repository_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return "UNKNOWN"
    return result.stdout.strip() or "UNKNOWN"


def build_release(
    image: bytes,
    firmware_id: str,
    firmware_version: int,
    product_id: int,
    hardware_id: int,
    display_version: str | None,
    source_commit: str,
    source_filename: str,
    created_at: str | None = None,
) -> tuple[bytes, dict[str, Any]]:
    """Return a M7 package and the M8 manifest describing that same package."""
    if FIRMWARE_ID_RE.fullmatch(firmware_id) is None:
        raise ValueError("firmware_id may contain only letters, digits, '.', '_' and '-'")
    if not source_commit:
        raise ValueError("source_commit must not be empty")

    package = build_package(image, firmware_version, product_id, hardware_id)
    package_manifest = parse_package(package)
    package_crc32 = binascii.crc32(package) & 0xFFFFFFFF
    timestamp = created_at or datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    manifest: dict[str, Any] = {
        "schema_version": 1,
        "firmware_id": firmware_id,
        "product_id": package_manifest.product_id,
        "hardware_id": package_manifest.hardware_id,
        "firmware_version": display_version or str(package_manifest.firmware_version),
        "firmware_version_code": package_manifest.firmware_version,
        "app_base": APP_BASE_ADDRESS,
        "image_size": package_manifest.image_size,
        "crc32": f"{package_manifest.image_crc32:08X}",
        "sha256": package_manifest.image_sha256.hex().upper(),
        "package_format": 1,
        "package_header_size": HEADER_SIZE,
        "package_image_offset": IMAGE_OFFSET,
        "package_size": len(package),
        "package_crc32": f"{package_crc32:08X}",
        "package_sha256": sha256_hex(package),
        "download_url": f"/api/v1/firmwares/{firmware_id}/binary",
        "created_at": timestamp,
        "source": {
            "git_commit": source_commit,
            "input_filename": source_filename,
        },
    }
    return package, manifest


def write_release(output_directory: Path, package: bytes, manifest: dict[str, Any], force: bool) -> None:
    if output_directory.name != manifest["firmware_id"]:
        raise ValueError("output directory name must match firmware_id")
    manifest_path = output_directory / "manifest.json"
    package_path = output_directory / "firmware.bin"
    if not force and (manifest_path.exists() or package_path.exists()):
        raise FileExistsError("release already exists; choose a new firmware_id or pass --force")

    output_directory.mkdir(parents=True, exist_ok=True)
    package_path.write_bytes(package)
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build an immutable M8 server firmware release")
    parser.add_argument("--input", required=True, type=Path, help="raw STM32 APP .bin")
    parser.add_argument("--firmware-id", required=True)
    parser.add_argument("--version", required=True, type=parse_int, help="non-zero uint32 version code")
    parser.add_argument("--display-version", help="human-readable version, for example 1.2.0")
    parser.add_argument("--product-id", type=parse_int, default=DEFAULT_PRODUCT_ID)
    parser.add_argument("--hardware-id", type=parse_int, default=DEFAULT_HARDWARE_ID)
    parser.add_argument(
        "--pad-to",
        type=parse_int,
        help="test-only: pad the STM32 image to this size with erased 0xFF bytes",
    )
    parser.add_argument("--output", required=True, type=Path, help="server/firmware/<firmware_id>")
    parser.add_argument("--git-commit", help="source commit; defaults to the current repository HEAD")
    parser.add_argument("--force", action="store_true", help="overwrite an existing release directory")
    args = parser.parse_args()

    repository_root = Path(__file__).resolve().parents[1]
    source_commit = args.git_commit or current_git_commit(repository_root)
    image = pad_image(args.input.read_bytes(), args.pad_to)
    package, manifest = build_release(
        image,
        args.firmware_id,
        args.version,
        args.product_id,
        args.hardware_id,
        args.display_version,
        source_commit,
        args.input.name,
    )
    write_release(args.output, package, manifest, args.force)
    print(
        f"Release: {args.output.resolve()}\n"
        f"Firmware ID: {manifest['firmware_id']}\n"
        f"Image: {manifest['image_size']} bytes, CRC32={manifest['crc32']}\n"
        f"Package: {manifest['package_size']} bytes, SHA-256={manifest['package_sha256']}\n"
        f"Download: {manifest['download_url']}"
    )


if __name__ == "__main__":
    main()
