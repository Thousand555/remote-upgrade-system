#!/usr/bin/env python3
"""M6 USART1 firmware upgrade client for the STM32 Bootloader."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import random
import struct
import sys
import time
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Optional


FUNCTION_CODE = 0x41
PROTOCOL_VERSION = 1
MAX_ADU_SIZE = 256
MAX_PAYLOAD_SIZE = 224
HEADER_SIZE = 18
CHECKPOINT_SIZE = 4096

CAP_BOOTLOADER = 0x0001
CAP_ENTER_BOOT = 0x0002
CAP_RESUME = 0x0004
CAP_CRC32 = 0x0008


class Subfunction(IntEnum):
    HELLO = 0x01
    GET_INFO = 0x02
    ENTER_BOOT = 0x03
    START = 0x10
    ERASE = 0x11
    DATA = 0x12
    QUERY_PROGRESS = 0x13
    VERIFY = 0x14
    ACTIVATE = 0x15
    ABORT = 0x16


class Status(IntEnum):
    OK = 0
    BAD_FRAME = 1
    BAD_CRC = 2
    BAD_SESSION = 3
    BAD_SEQUENCE = 4
    BAD_OFFSET = 5
    BAD_IMAGE_SIZE = 6
    BAD_PRODUCT = 7
    BAD_HARDWARE = 8
    VERSION_REJECTED = 9
    FLASH_ERROR = 10
    VERIFY_FAILED = 11
    BUSY = 12
    TIMEOUT = 13


class BootState(IntEnum):
    EMPTY = 0
    APP_VALID = 1
    UPDATE_REQUESTED = 2
    ERASING = 3
    RECEIVING = 4
    VERIFYING = 5
    PENDING_BOOT = 6
    CONFIRMED = 7
    FAILED = 8


@dataclass(frozen=True)
class Message:
    subfunction: int
    status_or_flags: int = 0
    session_id: int = 0
    sequence: int = 0
    offset: int = 0
    payload: bytes = b""


@dataclass(frozen=True)
class Progress:
    state: BootState
    received_bytes: int
    image_size: int
    error_code: int
    session_id: int


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def encode_message(address: int, message: Message) -> bytes:
    if not 1 <= address <= 247:
        raise ValueError("Modbus address must be in 1..247")
    if len(message.payload) > MAX_PAYLOAD_SIZE:
        raise ValueError("upgrade payload exceeds 224 bytes")

    body = struct.pack(
        "<BBHIIIH",
        int(message.subfunction),
        PROTOCOL_VERSION,
        message.status_or_flags,
        message.session_id,
        message.sequence,
        message.offset,
        len(message.payload),
    ) + message.payload
    adu_without_crc = bytes((address, FUNCTION_CODE)) + body
    return adu_without_crc + struct.pack("<H", crc16_modbus(adu_without_crc))


def decode_message(adu: bytes, expected_address: Optional[int] = None) -> Message:
    if not 22 <= len(adu) <= MAX_ADU_SIZE:
        raise ValueError(f"invalid RTU length: {len(adu)}")
    if expected_address is not None and adu[0] != expected_address:
        raise ValueError(f"unexpected Modbus address: {adu[0]}")
    if adu[1] != FUNCTION_CODE:
        raise ValueError(f"unexpected function: 0x{adu[1]:02X}")
    received_crc = struct.unpack_from("<H", adu, len(adu) - 2)[0]
    if received_crc != crc16_modbus(adu[:-2]):
        raise ValueError("Modbus CRC16 mismatch")

    subfunction, version, status_or_flags, session_id, sequence, offset, payload_len = (
        struct.unpack_from("<BBHIIIH", adu, 2)
    )
    if version != PROTOCOL_VERSION:
        raise ValueError(f"unsupported protocol version: {version}")
    if payload_len != len(adu) - 22:
        raise ValueError("upgrade payload length mismatch")
    return Message(
        subfunction=subfunction,
        status_or_flags=status_or_flags,
        session_id=session_id,
        sequence=sequence,
        offset=offset,
        payload=adu[20:-2],
    )


def encode_start_manifest(
    firmware_version: int,
    image: bytes,
    product_id: int,
    hardware_id: int,
    image_crc32: Optional[int] = None,
) -> bytes:
    crc32 = binascii.crc32(image) & 0xFFFFFFFF if image_crc32 is None else image_crc32
    return struct.pack(
        "<III32sHH",
        firmware_version,
        len(image),
        crc32,
        hashlib.sha256(image).digest(),
        product_id,
        hardware_id,
    )


def decode_hello(payload: bytes) -> tuple[int, int, int]:
    if len(payload) != 8:
        raise ValueError("HELLO response payload must be 8 bytes")
    return struct.unpack("<HHI", payload)


def decode_info(payload: bytes) -> tuple[int, int, int, int, int, int, int, int]:
    if len(payload) != 24:
        raise ValueError("GET_INFO response payload must be 24 bytes")
    return struct.unpack("<HHIIIIHH", payload)


def decode_progress(message: Message) -> Progress:
    if len(message.payload) != 16:
        raise ValueError("QUERY_PROGRESS response payload must be 16 bytes")
    state, _reserved, received, image_size, error = struct.unpack(
        "<HHIII", message.payload
    )
    return Progress(BootState(state), received, image_size, error, message.session_id)


class UpgradeClient:
    def __init__(
        self,
        port: str,
        baud: int,
        address: int,
        timeout: float,
        retries: int,
        verbose: bool,
    ) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise RuntimeError(
                "pyserial is required; run: python -m pip install pyserial"
            ) from exc

        self._serial_module = serial
        self.serial = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout,
            write_timeout=timeout,
        )
        self.address = address
        self.retries = retries
        self.verbose = verbose
        self.sequence = 0

    def close(self) -> None:
        self.serial.close()

    def _read_exact(self, count: int) -> bytes:
        data = bytearray()
        deadline = time.monotonic() + float(self.serial.timeout)
        while len(data) < count:
            part = self.serial.read(count - len(data))
            if part:
                data.extend(part)
                continue
            if time.monotonic() >= deadline:
                raise TimeoutError(f"serial response timeout ({len(data)}/{count} bytes)")
        return bytes(data)

    def _read_adu(self) -> bytes:
        prefix = self._read_exact(2)
        if prefix[1] & 0x80:
            exception = prefix + self._read_exact(3)
            if crc16_modbus(exception[:-2]) != struct.unpack("<H", exception[-2:])[0]:
                raise ValueError("Modbus exception CRC mismatch")
            raise RuntimeError(f"Modbus exception code 0x{exception[2]:02X}")
        header_tail = self._read_exact(18)
        header = prefix + header_tail
        payload_length = struct.unpack_from("<H", header, 18)[0]
        if payload_length > MAX_PAYLOAD_SIZE:
            raise ValueError("response payload exceeds protocol limit")
        return header + self._read_exact(payload_length + 2)

    def transact(
        self,
        request: Message,
        retries: Optional[int] = None,
        simulate_drop: bool = False,
    ) -> Message:
        request_adu = encode_message(self.address, request)
        attempts = self.retries if retries is None else retries
        last_error: Optional[Exception] = None

        for attempt in range(1, attempts + 1):
            try:
                self.serial.write(request_adu)
                self.serial.flush()
                response_adu = self._read_adu()
                response = decode_message(response_adu, self.address)
                if response.subfunction != request.subfunction:
                    raise ValueError("response subfunction mismatch")
                if response.sequence != request.sequence:
                    raise ValueError("response sequence mismatch")
                if simulate_drop and attempt == 1:
                    if self.verbose:
                        print("  fault injection: discarded one valid ACK")
                    raise TimeoutError("injected ACK loss")
                if self.verbose:
                    print(
                        f"  RX {Subfunction(request.subfunction).name}: "
                        f"status={Status(response.status_or_flags).name}, "
                        f"next={response.offset}"
                    )
                return response
            except (TimeoutError, ValueError) as exc:
                last_error = exc
                if self.verbose:
                    print(f"  retry {attempt}/{attempts}: {exc}")
        raise TimeoutError(f"request failed after {attempts} attempts: {last_error}")

    def request(
        self,
        subfunction: Subfunction,
        session_id: int = 0,
        offset: int = 0,
        payload: bytes = b"",
        retries: Optional[int] = None,
    ) -> Message:
        request = Message(
            subfunction=subfunction,
            session_id=session_id,
            sequence=self.sequence,
            offset=offset,
            payload=payload,
        )
        response = self.transact(request, retries=retries)
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        return response

    def hello(self, retries: Optional[int] = None) -> tuple[Message, int, int, int]:
        response = self.request(Subfunction.HELLO, retries=retries)
        require_status(response, Status.OK)
        capabilities, max_payload, version = decode_hello(response.payload)
        return response, capabilities, max_payload, version

    def ensure_bootloader(self,
                          discovery_timeout: float,
                          enter_from_app: bool = True) -> None:
        deadline = time.monotonic() + discovery_timeout
        last_error: Optional[Exception] = None
        original_timeout = self.serial.timeout
        # Probe faster than the 500 ms Bootloader recovery window. The normal
        # one-second command timeout would otherwise make a manual reset easy
        # to miss when an older APP does not implement ENTER_BOOT yet.
        self.serial.timeout = min(float(original_timeout), 0.1)
        try:
            while time.monotonic() < deadline:
                try:
                    _response, capabilities, max_payload, version = self.hello(retries=1)
                    if max_payload != MAX_PAYLOAD_SIZE:
                        raise RuntimeError(f"device reports unsupported payload size {max_payload}")
                    if capabilities & CAP_BOOTLOADER:
                        print(f"Bootloader connected, version=0x{version:08X}")
                        return
                    if capabilities & CAP_ENTER_BOOT:
                        if not enter_from_app:
                            print(f"APP service connected, version={version}")
                            return
                        print("APP service connected; requesting Bootloader entry...")
                        session_id = random_session_id()
                        response = self.request(Subfunction.ENTER_BOOT, session_id=session_id)
                        require_status(response, Status.OK)
                        time.sleep(0.3)
                        continue
                    raise RuntimeError(f"unknown service capabilities 0x{capabilities:04X}")
                except (TimeoutError, ValueError, RuntimeError) as exc:
                    last_error = exc
                    time.sleep(0.02)
        finally:
            self.serial.timeout = original_timeout
        raise TimeoutError(
            "Bootloader discovery timed out. Reset the board while this command is running. "
            f"Last error: {last_error}"
        )

    def query_progress(self, retries: Optional[int] = None) -> tuple[Message, Progress]:
        response = self.request(Subfunction.QUERY_PROGRESS, retries=retries)
        if response.status_or_flags not in (Status.OK, Status.BUSY):
            require_status(response, Status.OK)
        return response, decode_progress(response)


def require_status(response: Message, expected: Status) -> None:
    try:
        actual = Status(response.status_or_flags)
    except ValueError as exc:
        raise RuntimeError(f"unknown device status {response.status_or_flags}") from exc
    if actual != expected:
        raise RuntimeError(f"device returned {actual.name}, expected {expected.name}")


def random_session_id() -> int:
    value = random.SystemRandom().getrandbits(32)
    return value if value != 0 else 1


def next_chunk_end(offset: int, image_size: int) -> int:
    checkpoint_end = ((offset // CHECKPOINT_SIZE) + 1) * CHECKPOINT_SIZE
    return min(offset + MAX_PAYLOAD_SIZE, checkpoint_end, image_size)


def run_upgrade(args: argparse.Namespace) -> None:
    image = b""
    if not args.probe_only:
        if args.file is None:
            raise ValueError("--file is required unless --probe-only is used")
        image_path = Path(args.file).resolve()
        image = image_path.read_bytes()
        if not image:
            raise ValueError("firmware image is empty")
        if len(image) > 0xE0000:
            raise ValueError("firmware image exceeds the 896 KiB APP partition")

        crc32 = binascii.crc32(image) & 0xFFFFFFFF
        print(
            f"Image: {image_path}\n"
            f"Size: {len(image)} bytes, CRC32=0x{crc32:08X}, version={args.version}"
        )
    if args.dry_run:
        if args.probe_only:
            raise ValueError("--dry-run and --probe-only cannot be combined")
        print("Dry run complete; no serial port was opened.")
        return

    client = UpgradeClient(
        port=args.port,
        baud=args.baud,
        address=args.address,
        timeout=args.timeout,
        retries=args.retries,
        verbose=args.verbose,
    )
    try:
        client.ensure_bootloader(args.discovery_timeout,
                                 enter_from_app=not args.probe_only)
        info_response = client.request(Subfunction.GET_INFO)
        require_status(info_response, Status.OK)
        (
            product_id,
            hardware_id,
            boot_version,
            app_version,
            app_base,
            app_max_size,
            state,
            capabilities,
        ) = decode_info(info_response.payload)
        print(
            f"Device: product=0x{product_id:04X}, hardware=0x{hardware_id:04X}, "
            f"state={BootState(state).name}, APP=0x{app_base:08X}/0x{app_max_size:X}"
        )
        if product_id != args.product_id or hardware_id != args.hardware_id:
            raise RuntimeError("command-line product/hardware IDs do not match the device")
        if not capabilities & CAP_CRC32:
            if not args.probe_only:
                raise RuntimeError("device does not report CRC32 verification capability")

        if args.probe_only:
            print(
                f"Probe complete: boot=0x{boot_version:08X}, "
                f"app={app_version}, capabilities=0x{capabilities:04X}"
            )
            return

        session_id = args.session_id or random_session_id()
        if args.resume:
            _query_response, existing = client.query_progress()
            if existing.session_id != 0:
                session_id = existing.session_id
                print(f"Resume session discovered: 0x{session_id:08X}")

        manifest = encode_start_manifest(
            args.version,
            image,
            args.product_id,
            args.hardware_id,
            args.override_crc32,
        )
        start = client.request(Subfunction.START, session_id=session_id, payload=manifest)
        if start.status_or_flags not in (Status.OK, Status.BUSY):
            require_status(start, Status.OK)

        _phase_response, progress = client.query_progress()
        already_verified = progress.state == BootState.PENDING_BOOT
        needs_verify_only = progress.state == BootState.VERIFYING

        if not already_verified and not needs_verify_only:
            erase = client.request(Subfunction.ERASE, session_id=session_id)
            if erase.status_or_flags not in (Status.OK, Status.BUSY):
                require_status(erase, Status.OK)

            erase_deadline = time.monotonic() + args.erase_timeout
            time.sleep(0.5)
            while time.monotonic() < erase_deadline:
                try:
                    _response, progress = client.query_progress(retries=1)
                    if progress.state == BootState.RECEIVING:
                        break
                    if progress.state == BootState.FAILED:
                        raise RuntimeError(f"erase failed, error={progress.error_code}")
                except TimeoutError:
                    pass
                time.sleep(0.1)
            if progress.state != BootState.RECEIVING:
                raise TimeoutError("APP erase did not reach RECEIVING state")

        if progress.image_size != len(image):
            raise RuntimeError("device session image size does not match the local image")

        offset = progress.received_bytes
        if offset > len(image):
            raise RuntimeError("device resume offset exceeds the local image")
        if not already_verified and not needs_verify_only:
            print(f"Transferring from offset {offset}...")
        while (not already_verified) and (not needs_verify_only) and offset < len(image):
            end = next_chunk_end(offset, len(image))
            payload = image[offset:end]
            request = Message(
                subfunction=Subfunction.DATA,
                session_id=session_id,
                sequence=client.sequence,
                offset=offset,
                payload=payload,
            )
            inject_drop = args.inject_drop_ack_rate > 0 and (
                random.random() < args.inject_drop_ack_rate
            )
            response = client.transact(request, simulate_drop=inject_drop)
            require_status(response, Status.OK)
            if len(response.payload) != 12:
                raise RuntimeError("DATA ACK payload length is not 12")
            ack_status, _reserved, accepted_sequence, next_offset = struct.unpack(
                "<HHII", response.payload
            )
            if ack_status != Status.OK or accepted_sequence != client.sequence:
                raise RuntimeError("DATA ACK content mismatch")
            if next_offset != end:
                raise RuntimeError(f"device requested offset {next_offset}, expected {end}")

            if args.inject_duplicate_rate > 0 and (
                random.random() < args.inject_duplicate_rate
            ):
                duplicate = client.transact(request)
                require_status(duplicate, Status.OK)
                duplicate_next = struct.unpack("<HHII", duplicate.payload)[3]
                if duplicate_next != end:
                    raise RuntimeError("duplicate DATA changed device progress")
                if args.verbose:
                    print(f"  fault injection: duplicate offset {offset} accepted")

            client.sequence = (client.sequence + 1) & 0xFFFFFFFF
            offset = end
            if args.verbose or offset == len(image) or offset % CHECKPOINT_SIZE == 0:
                print(f"  {offset}/{len(image)} bytes ({offset * 100 // len(image)}%)")

        if not already_verified:
            verify = client.request(Subfunction.VERIFY, session_id=session_id)
            require_status(verify, Status.OK)
            print("CRC32 verification passed.")
        else:
            print("Image was already verified; continuing from PENDING_BOOT.")

        if args.no_activate:
            print("Activation skipped; device remains in PENDING_BOOT.")
        else:
            activate = client.request(Subfunction.ACTIVATE, session_id=session_id)
            require_status(activate, Status.OK)
            print("Activation acknowledged; STM32 is resetting into the APP.")
    finally:
        client.close()


def parse_u32(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("value must fit uint32")
    return value


def parse_u16(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFFFF:
        raise argparse.ArgumentTypeError("value must fit uint16")
    return value


def probability(text: str) -> float:
    value = float(text)
    if not 0.0 <= value <= 1.0:
        raise argparse.ArgumentTypeError("probability must be in 0..1")
    return value


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--address", type=int, default=1)
    parser.add_argument("--file")
    parser.add_argument("--version", type=parse_u32, default=1)
    parser.add_argument("--product-id", type=parse_u16, default=1)
    parser.add_argument("--hardware-id", type=parse_u16, default=1)
    parser.add_argument("--session-id", type=parse_u32)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--no-activate", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--probe-only",
        action="store_true",
        help="only enter/discover Bootloader and read device information; do not erase APP",
    )
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--erase-timeout", type=float, default=60.0)
    parser.add_argument("--discovery-timeout", type=float, default=10.0)
    parser.add_argument("--retries", type=int, default=5)
    parser.add_argument("--inject-drop-ack-rate", type=probability, default=0.0)
    parser.add_argument("--inject-duplicate-rate", type=probability, default=0.0)
    parser.add_argument(
        "--override-crc32",
        type=parse_u32,
        help="test-only manifest CRC32 override (for VERIFY failure tests)",
    )
    parser.add_argument("--verbose", action="store_true")
    return parser


def main() -> int:
    try:
        run_upgrade(build_parser().parse_args())
        return 0
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
