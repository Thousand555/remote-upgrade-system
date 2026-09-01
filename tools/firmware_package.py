"""Build and inspect the M7 STM32 firmware cache package."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path


PACKAGE_MAGIC = 0x31465747
PACKAGE_FORMAT = 1
HEADER_SIZE = 128
IMAGE_OFFSET = 0x1000
VALID_MARKER = 0xA5C3F00D
MAX_IMAGE_SIZE = 0xE0000
DEFAULT_PRODUCT_ID = 0x0001
DEFAULT_HARDWARE_ID = 0x0001

_HEADER_PREFIX = struct.Struct("<IHHIIIHH32s")
_HEADER_SUFFIX = struct.Struct("<II64s")


@dataclass(frozen=True)
class FirmwareManifest:
    firmware_version: int
    image_size: int
    image_crc32: int
    product_id: int
    hardware_id: int
    image_sha256: bytes
    header_crc32: int


def pad_image(image: bytes, target_size: int | None) -> bytes:
    """Extend an APP image with erased Flash bytes without changing its vectors."""
    if target_size is None:
        return image
    if target_size < len(image):
        raise ValueError("pad target is smaller than the input image")
    if target_size > MAX_IMAGE_SIZE:
        raise ValueError("pad target exceeds the 896 KiB STM32 APP partition")
    return image + bytes([0xFF]) * (target_size - len(image))


def _u32(value: int, name: str) -> int:
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError(f"{name} must fit in uint32")
    return value


def _u16(value: int, name: str) -> int:
    if not 0 <= value <= 0xFFFF:
        raise ValueError(f"{name} must fit in uint16")
    return value


def build_package(
    image: bytes,
    firmware_version: int,
    product_id: int = DEFAULT_PRODUCT_ID,
    hardware_id: int = DEFAULT_HARDWARE_ID,
) -> bytes:
    if not image:
        raise ValueError("firmware image is empty")
    if len(image) > MAX_IMAGE_SIZE:
        raise ValueError("firmware image exceeds the 896 KiB STM32 APP partition")

    firmware_version = _u32(firmware_version, "firmware_version")
    if firmware_version == 0:
        raise ValueError("firmware_version must be non-zero")
    product_id = _u16(product_id, "product_id")
    hardware_id = _u16(hardware_id, "hardware_id")

    image_crc32 = binascii.crc32(image) & 0xFFFFFFFF
    image_sha256 = hashlib.sha256(image).digest()
    prefix = _HEADER_PREFIX.pack(
        PACKAGE_MAGIC,
        PACKAGE_FORMAT,
        HEADER_SIZE,
        firmware_version,
        len(image),
        image_crc32,
        product_id,
        hardware_id,
        image_sha256,
    )
    header_crc32 = binascii.crc32(prefix) & 0xFFFFFFFF
    header = prefix + _HEADER_SUFFIX.pack(
        header_crc32,
        VALID_MARKER,
        bytes([0xFF]) * 64,
    )
    if len(header) != HEADER_SIZE:
        raise AssertionError("firmware package header layout drift")
    return header + bytes([0xFF]) * (IMAGE_OFFSET - HEADER_SIZE) + image


def parse_package(package: bytes, validate_image: bool = True) -> FirmwareManifest:
    if len(package) < IMAGE_OFFSET:
        raise ValueError("package is shorter than the image offset")

    prefix = package[: _HEADER_PREFIX.size]
    (
        magic,
        format_version,
        header_size,
        firmware_version,
        image_size,
        image_crc32,
        product_id,
        hardware_id,
        image_sha256,
    ) = _HEADER_PREFIX.unpack(prefix)
    header_crc32, valid_marker, _reserved = _HEADER_SUFFIX.unpack(
        package[_HEADER_PREFIX.size : HEADER_SIZE]
    )

    if magic != PACKAGE_MAGIC:
        raise ValueError("package magic mismatch")
    if format_version != PACKAGE_FORMAT or header_size != HEADER_SIZE:
        raise ValueError("unsupported package format")
    if valid_marker != VALID_MARKER:
        raise ValueError("package is not committed")
    if firmware_version == 0 or not 0 < image_size <= MAX_IMAGE_SIZE:
        raise ValueError("invalid firmware version or image size")
    if binascii.crc32(prefix) & 0xFFFFFFFF != header_crc32:
        raise ValueError("package header CRC32 mismatch")
    if len(package) != IMAGE_OFFSET + image_size:
        raise ValueError("package length does not match the manifest")

    image = package[IMAGE_OFFSET:]
    if validate_image:
        if binascii.crc32(image) & 0xFFFFFFFF != image_crc32:
            raise ValueError("firmware image CRC32 mismatch")
        if hashlib.sha256(image).digest() != image_sha256:
            raise ValueError("firmware image SHA-256 mismatch")

    return FirmwareManifest(
        firmware_version=firmware_version,
        image_size=image_size,
        image_crc32=image_crc32,
        product_id=product_id,
        hardware_id=hardware_id,
        image_sha256=image_sha256,
        header_crc32=header_crc32,
    )


def parse_int(text: str) -> int:
    return int(text, 0)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Package a raw STM32 APP binary for the ESP32 stm_fw partition"
    )
    parser.add_argument("--input", required=True, type=Path, help="raw STM32 APP .bin")
    parser.add_argument("--output", required=True, type=Path, help="output package .bin")
    parser.add_argument("--version", required=True, type=parse_int)
    parser.add_argument("--product-id", type=parse_int, default=DEFAULT_PRODUCT_ID)
    parser.add_argument("--hardware-id", type=parse_int, default=DEFAULT_HARDWARE_ID)
    parser.add_argument(
        "--pad-to",
        type=parse_int,
        help="extend the raw image to this size with 0xFF bytes (for physical tests)",
    )
    args = parser.parse_args()

    image = pad_image(args.input.read_bytes(), args.pad_to)
    package = build_package(
        image,
        args.version,
        args.product_id,
        args.hardware_id,
    )
    manifest = parse_package(package)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    print(
        f"Package: {args.output.resolve()}\n"
        f"Image size: {manifest.image_size} bytes\n"
        f"Version: {manifest.firmware_version}\n"
        f"CRC32: 0x{manifest.image_crc32:08X}\n"
        f"SHA-256: {manifest.image_sha256.hex().upper()}\n"
        f"Partition bytes: {len(package)}"
    )


if __name__ == "__main__":
    main()
