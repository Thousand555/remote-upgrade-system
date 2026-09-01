from __future__ import annotations

import binascii
import json
import tempfile
import unittest
from pathlib import Path

from firmware_package import IMAGE_OFFSET, pad_image, parse_package
from pack_firmware import build_release, write_release


class PackFirmwareTests(unittest.TestCase):
    def test_release_manifest_describes_the_m7_compatible_package(self):
        image = b"M8 STM32 image"
        package, manifest = build_release(
            image=image,
            firmware_id="f407-node-1.2.0",
            firmware_version=2,
            product_id=1,
            hardware_id=1,
            display_version="1.2.0",
            source_commit="a" * 40,
            source_filename="stm32_app.bin",
            created_at="2026-09-01T00:00:00Z",
        )

        package_manifest = parse_package(package)
        self.assertEqual(package[IMAGE_OFFSET:], image)
        self.assertEqual(package_manifest.firmware_version, 2)
        self.assertEqual(manifest["firmware_version"], "1.2.0")
        self.assertEqual(manifest["image_size"], len(image))
        self.assertEqual(manifest["crc32"], f"{binascii.crc32(image) & 0xFFFFFFFF:08X}")
        self.assertEqual(manifest["package_size"], len(package))
        self.assertEqual(manifest["download_url"], "/api/v1/firmwares/f407-node-1.2.0/binary")

    def test_invalid_firmware_id_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "firmware_id"):
            build_release(
                image=b"x",
                firmware_id="../escape",
                firmware_version=1,
                product_id=1,
                hardware_id=1,
                display_version=None,
                source_commit="commit",
                source_filename="app.bin",
            )

    def test_padded_test_release_remains_a_valid_m7_package(self):
        image = pad_image(b"M9 resume test image", 0x11000)
        package, manifest = build_release(
            image=image,
            firmware_id="f407-node-m9-resume",
            firmware_version=2,
            product_id=1,
            hardware_id=1,
            display_version="1.2.0-m9-resume",
            source_commit="b" * 40,
            source_filename="stm32_app.bin",
            created_at="2026-09-01T00:00:00Z",
        )

        parsed = parse_package(package)
        self.assertEqual(parsed.image_size, 0x11000)
        self.assertEqual(manifest["package_size"], IMAGE_OFFSET + 0x11000)
        self.assertGreater(manifest["package_size"], 64 * 1024)

    def test_write_release_creates_an_immutable_directory(self):
        package, manifest = build_release(
            image=b"release image",
            firmware_id="f407-node-1.2.0",
            firmware_version=2,
            product_id=1,
            hardware_id=1,
            display_version=None,
            source_commit="commit",
            source_filename="app.bin",
        )
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "f407-node-1.2.0"
            write_release(output, package, manifest, force=False)
            self.assertEqual((output / "firmware.bin").read_bytes(), package)
            self.assertEqual(json.loads((output / "manifest.json").read_text(encoding="utf-8")), manifest)
            with self.assertRaises(FileExistsError):
                write_release(output, package, manifest, force=False)
