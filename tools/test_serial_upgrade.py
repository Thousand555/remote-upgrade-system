import binascii
import hashlib
import struct
import unittest

from serial_upgrade import (
    CHECKPOINT_SIZE,
    MAX_PAYLOAD_SIZE,
    Message,
    Subfunction,
    crc16_modbus,
    decode_hello,
    decode_info,
    decode_message,
    encode_message,
    encode_start_manifest,
    next_chunk_end,
)


class ProtocolCodecTests(unittest.TestCase):
    def test_modbus_reference_vector(self):
        self.assertEqual(crc16_modbus(b"123456789"), 0x4B37)

    def test_m5_hello_golden_vector(self):
        request = Message(Subfunction.HELLO)
        self.assertEqual(
            encode_message(1, request).hex(" ").upper(),
            "01 41 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 31 95",
        )

    def test_round_trip_all_payload_lengths(self):
        for length in range(MAX_PAYLOAD_SIZE + 1):
            message = Message(
                Subfunction.DATA,
                session_id=0x12345678,
                sequence=9,
                offset=224,
                payload=bytes(index & 0xFF for index in range(length)),
            )
            self.assertEqual(decode_message(encode_message(1, message), 1), message)

    def test_start_manifest_layout(self):
        image = b"M6 image"
        manifest = encode_start_manifest(7, image, 0x1122, 0x3344)
        self.assertEqual(len(manifest), 48)
        version, size, crc32 = struct.unpack_from("<III", manifest)
        self.assertEqual(version, 7)
        self.assertEqual(size, len(image))
        self.assertEqual(crc32, binascii.crc32(image) & 0xFFFFFFFF)
        self.assertEqual(manifest[12:44], hashlib.sha256(image).digest())
        self.assertEqual(struct.unpack_from("<HH", manifest, 44), (0x1122, 0x3344))

    def test_response_payload_layouts(self):
        self.assertEqual(decode_hello(struct.pack("<HHI", 0xD, 224, 0x10000)),
                         (0xD, 224, 0x10000))
        info = (1, 2, 3, 4, 0x08020000, 0xE0000, 7, 0xD)
        self.assertEqual(decode_info(struct.pack("<HHIIIIHH", *info)), info)

    def test_chunks_do_not_cross_checkpoint(self):
        image_size = CHECKPOINT_SIZE * 2 + 3
        offset = 0
        while offset < image_size:
            end = next_chunk_end(offset, image_size)
            self.assertLessEqual(end - offset, MAX_PAYLOAD_SIZE)
            self.assertFalse(offset // CHECKPOINT_SIZE != (end - 1) // CHECKPOINT_SIZE)
            offset = end
        self.assertEqual(offset, image_size)


if __name__ == "__main__":
    unittest.main()
