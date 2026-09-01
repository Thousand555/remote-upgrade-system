"""M8 HTTP firmware service.

The service deliberately has no upload or deployment endpoint. A release is
created locally by tools/pack_firmware.py, reviewed, then copied into
server/firmware/<firmware_id>/ before this service exposes it.
"""

from __future__ import annotations

import os
import time
from collections.abc import Iterator
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse, StreamingResponse

from .catalog import CatalogError, FirmwareCatalog, FirmwareRecord
from .range_requests import RangeNotSatisfiable, parse_single_range


DEFAULT_FIRMWARE_ROOT = Path(__file__).resolve().parents[1] / "firmware"
STREAM_BLOCK_SIZE = 64 * 1024
MAX_TEST_STREAM_DELAY_MS = 5000
TEST_STREAM_DELAY_ENV = "M8_TEST_STREAM_DELAY_MS"


def _validate_test_stream_delay_ms(value: int) -> int:
    if isinstance(value, bool) or not 0 <= value <= MAX_TEST_STREAM_DELAY_MS:
        raise ValueError(
            f"test stream delay must be in 0..{MAX_TEST_STREAM_DELAY_MS} ms"
        )
    return value


def _test_stream_delay_from_environment() -> int:
    raw_value = os.environ.get(TEST_STREAM_DELAY_ENV, "0")
    try:
        value = int(raw_value, 10)
    except ValueError as error:
        raise RuntimeError(f"{TEST_STREAM_DELAY_ENV} must be a decimal integer") from error
    try:
        return _validate_test_stream_delay_ms(value)
    except ValueError as error:
        raise RuntimeError(f"invalid {TEST_STREAM_DELAY_ENV}: {error}") from error


def _stream_file(path: Path, start: int, length: int, delay_ms: int) -> Iterator[bytes]:
    with path.open("rb") as binary:
        binary.seek(start)
        remaining = length
        while remaining:
            block = binary.read(min(STREAM_BLOCK_SIZE, remaining))
            if not block:
                raise RuntimeError("firmware file changed while being served")
            remaining -= len(block)
            yield block
            if remaining and delay_ms:
                time.sleep(delay_ms / 1000.0)


def _record_or_http_error(catalog: FirmwareCatalog, firmware_id: str) -> FirmwareRecord:
    try:
        record = catalog.get(firmware_id)
    except CatalogError as error:
        raise HTTPException(status_code=500, detail=f"invalid firmware release: {error}") from error
    if record is None:
        raise HTTPException(status_code=404, detail="firmware release was not found")
    return record


def create_app(
    firmware_root: Path | None = None,
    test_stream_delay_ms: int | None = None,
) -> FastAPI:
    root = firmware_root or Path(os.environ.get("FIRMWARE_ROOT", DEFAULT_FIRMWARE_ROOT))
    stream_delay_ms = (
        _test_stream_delay_from_environment()
        if test_stream_delay_ms is None
        else _validate_test_stream_delay_ms(test_stream_delay_ms)
    )
    catalog = FirmwareCatalog(root)
    app = FastAPI(title="Remote Upgrade Firmware Server", version="1.0.0")

    @app.get("/healthz", include_in_schema=False)
    def healthz() -> dict[str, str]:
        return {"status": "ok"}

    @app.get("/api/v1/firmwares/{firmware_id}/manifest")
    def get_manifest(firmware_id: str) -> JSONResponse:
        record = _record_or_http_error(catalog, firmware_id)
        return JSONResponse(record.manifest, headers={"ETag": record.etag})

    @app.get("/api/v1/firmwares/{firmware_id}/binary")
    def get_binary(firmware_id: str, request: Request) -> StreamingResponse:
        record = _record_or_http_error(catalog, firmware_id)
        range_header = request.headers.get("range")
        if_range = request.headers.get("if-range")
        honor_range = range_header is not None and (if_range is None or if_range.strip() == record.etag)

        if honor_range:
            try:
                selected_range = parse_single_range(range_header, record.package_size)
            except RangeNotSatisfiable:
                return StreamingResponse(
                    iter(()),
                    status_code=416,
                    headers={
                        "Accept-Ranges": "bytes",
                        "Content-Range": f"bytes */{record.package_size}",
                        "ETag": record.etag,
                    },
                )
        else:
            selected_range = None

        if selected_range is None:
            start = 0
            length = record.package_size
            status_code = 200
            headers = {
                "Accept-Ranges": "bytes",
                "Content-Length": str(length),
                "ETag": record.etag,
            }
        else:
            start = selected_range.start
            length = selected_range.length
            status_code = 206
            headers = {
                "Accept-Ranges": "bytes",
                "Content-Length": str(length),
                "Content-Range": f"bytes {selected_range.start}-{selected_range.end}/{record.package_size}",
                "ETag": record.etag,
            }

        return StreamingResponse(
            _stream_file(record.binary_path, start, length, stream_delay_ms),
            status_code=status_code,
            media_type="application/octet-stream",
            headers=headers,
        )

    return app


app = create_app()
