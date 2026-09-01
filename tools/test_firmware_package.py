import binascii
import unittest

from firmware_package import (
    HEADER_SIZE,
    IMAGE_OFFSET,
    MAX_IMAGE_SIZE,
    build_package,
    pad_image,
    parse_package,
)


class FirmwarePackageTests(unittest.TestCase):
    def test_round_trip(self):
        image = bytes(range(256)) * 3
        package = build_package(image, 7)
        manifest = parse_package(package)

        self.assertEqual(len(package), IMAGE_OFFSET + len(image))
        self.assertEqual(package[IMAGE_OFFSET:], image)
        self.assertEqual(manifest.firmware_version, 7)
        self.assertEqual(manifest.image_size, len(image))
        self.assertEqual(manifest.image_crc32, binascii.crc32(image) & 0xFFFFFFFF)

    def test_header_is_fixed_size_and_image_is_sector_aligned(self):
        package = build_package(b"M7", 1)
        self.assertEqual(HEADER_SIZE, 128)
        self.assertEqual(package[HEADER_SIZE:IMAGE_OFFSET], bytes([0xFF]) * (IMAGE_OFFSET - HEADER_SIZE))

    def test_corruption_is_rejected(self):
        package = bytearray(build_package(b"firmware", 2))
        package[IMAGE_OFFSET] ^= 0x01
        with self.assertRaisesRegex(ValueError, "CRC32"):
            parse_package(bytes(package))

    def test_invalid_inputs_are_rejected(self):
        with self.assertRaises(ValueError):
            build_package(b"", 1)
        with self.assertRaises(ValueError):
            build_package(b"x", 0)
        with self.assertRaises(ValueError):
            build_package(bytes(MAX_IMAGE_SIZE + 1), 1)

    def test_image_padding_preserves_input_and_uses_erased_bytes(self):
        image = b"STM32"
        padded = pad_image(image, 4096)
        self.assertEqual(len(padded), 4096)
        self.assertEqual(padded[: len(image)], image)
        self.assertEqual(padded[len(image) :], bytes([0xFF]) * (4096 - len(image)))
        self.assertEqual(pad_image(image, None), image)

    def test_invalid_padding_targets_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "smaller"):
            pad_image(b"firmware", 4)
        with self.assertRaisesRegex(ValueError, "896 KiB"):
            pad_image(b"firmware", MAX_IMAGE_SIZE + 1)


if __name__ == "__main__":
    unittest.main()
