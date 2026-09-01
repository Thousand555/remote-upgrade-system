"""Validation and lookup for immutable M8 firmware releases."""

from __future__ import annotations

import hashlib
import hmac
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


MAX_STM32_IMAGE_SIZE = 0x000E0000
FIRMWARE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
HEX32_RE = re.compile(r"^[0-9A-Fa-f]{8}$")
HEX64_RE = re.compile(r"^[0-9A-Fa-f]{64}$")


class CatalogError(ValueError):
    """A firmware release on disk is malformed or was modified."""


@dataclass(frozen=True)
class FirmwareRecord:
    firmware_id: str
    manifest: dict[str, Any]
    binary_path: Path
    package_size: int
    etag: str


def _require_string(manifest: dict[str, Any], key: str) -> str:
    value = manifest.get(key)
    if not isinstance(value, str) or not value:
        raise CatalogError(f"manifest field '{key}' must be a non-empty string")
    return value


def _require_uint(manifest: dict[str, Any], key: str, maximum: int) -> int:
    value = manifest.get(key)
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise CatalogError(f"manifest field '{key}' is outside its allowed range")
    return value


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as binary:
        for block in iter(lambda: binary.read(64 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def load_record(release_directory: Path) -> FirmwareRecord:
    """Load one release and verify its package checksum before serving it."""
    manifest_path = release_directory / "manifest.json"
    binary_path = release_directory / "firmware.bin"
    try:
        manifest_data = json.loads(manifest_path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise CatalogError("manifest.json is missing") from error
    except json.JSONDecodeError as error:
        raise CatalogError("manifest.json is not valid JSON") from error

    if not isinstance(manifest_data, dict):
        raise CatalogError("manifest root must be a JSON object")
    if manifest_data.get("schema_version") != 1:
        raise CatalogError("unsupported manifest schema_version")

    firmware_id = _require_string(manifest_data, "firmware_id")
    if (FIRMWARE_ID_RE.fullmatch(firmware_id) is None) or firmware_id != release_directory.name:
        raise CatalogError("firmware_id is invalid or does not match the release directory")

    _require_uint(manifest_data, "product_id", 0xFFFF)
    _require_uint(manifest_data, "hardware_id", 0xFFFF)
    _require_string(manifest_data, "firmware_version")
    if _require_uint(manifest_data, "firmware_version_code", 0xFFFFFFFF) == 0:
        raise CatalogError("firmware_version_code must be non-zero")
    image_size = _require_uint(manifest_data, "image_size", MAX_STM32_IMAGE_SIZE)
    if image_size == 0:
        raise CatalogError("image_size must be non-zero")

    for key, expression in (("crc32", HEX32_RE), ("sha256", HEX64_RE),
                            ("package_crc32", HEX32_RE), ("package_sha256", HEX64_RE)):
        if expression.fullmatch(_require_string(manifest_data, key)) is None:
            raise CatalogError(f"manifest field '{key}' has an invalid hexadecimal value")

    package_size = _require_uint(manifest_data, "package_size", 0xFFFFFFFF)
    if package_size == 0:
        raise CatalogError("package_size must be non-zero")
    if _require_uint(manifest_data, "package_format", 0xFFFF) != 1:
        raise CatalogError("unsupported package_format")
    if _require_uint(manifest_data, "package_header_size", 0xFFFF) != 128:
        raise CatalogError("unsupported package_header_size")
    package_image_offset = _require_uint(manifest_data, "package_image_offset", 0xFFFFFFFF)
    if package_image_offset != 0x1000:
        raise CatalogError("unsupported package_image_offset")
    if package_size != package_image_offset + image_size:
        raise CatalogError("package_size does not match package_image_offset and image_size")
    expected_url = f"/api/v1/firmwares/{firmware_id}/binary"
    if _require_string(manifest_data, "download_url") != expected_url:
        raise CatalogError("download_url does not match firmware_id")

    try:
        actual_size = binary_path.stat().st_size
    except FileNotFoundError as error:
        raise CatalogError("firmware.bin is missing") from error
    if actual_size != package_size:
        raise CatalogError("firmware.bin size does not match package_size")

    actual_sha256 = _sha256_file(binary_path)
    expected_sha256 = manifest_data["package_sha256"].upper()
    if not hmac.compare_digest(actual_sha256, expected_sha256):
        raise CatalogError("firmware.bin SHA-256 does not match package_sha256")

    return FirmwareRecord(
        firmware_id=firmware_id,
        manifest=manifest_data,
        binary_path=binary_path,
        package_size=package_size,
        etag=f'"{expected_sha256.lower()}"',
    )


class FirmwareCatalog:
    """Filesystem-backed catalog; each release directory is immutable."""

    def __init__(self, firmware_root: Path):
        self._firmware_root = firmware_root.resolve()

    def get(self, firmware_id: str) -> FirmwareRecord | None:
        if FIRMWARE_ID_RE.fullmatch(firmware_id) is None:
            return None
        release_directory = (self._firmware_root / firmware_id).resolve()
        if release_directory.parent != self._firmware_root or not release_directory.is_dir():
            return None
        return load_record(release_directory)
