"""Small, framework-independent parser for M9 HTTP Range downloads."""

from __future__ import annotations

from dataclasses import dataclass


class RangeNotSatisfiable(ValueError):
    """The request is not one satisfiable HTTP byte range."""


@dataclass(frozen=True)
class ByteRange:
    start: int
    end: int

    @property
    def length(self) -> int:
        return self.end - self.start + 1


def _parse_decimal(value: str) -> int:
    if not value or not value.isascii() or not value.isdecimal():
        raise RangeNotSatisfiable("range value must be a decimal integer")
    return int(value)


def parse_single_range(header_value: str | None, resource_size: int) -> ByteRange | None:
    """Return a single inclusive range, or None when no Range header was sent."""
    if header_value is None:
        return None
    if resource_size <= 0:
        raise RangeNotSatisfiable("empty resource has no satisfiable byte range")

    unit, separator, spec = header_value.strip().partition("=")
    if unit.lower() != "bytes" or separator != "=" or "," in spec:
        raise RangeNotSatisfiable("only one bytes range is supported")
    start_text, separator, end_text = spec.strip().partition("-")
    if separator != "-":
        raise RangeNotSatisfiable("range is missing '-'")

    if start_text:
        start = _parse_decimal(start_text)
        end = resource_size - 1 if not end_text else _parse_decimal(end_text)
        if start >= resource_size or end < start:
            raise RangeNotSatisfiable("range is outside the resource")
        return ByteRange(start, min(end, resource_size - 1))

    suffix_length = _parse_decimal(end_text)
    if suffix_length == 0:
        raise RangeNotSatisfiable("suffix range must be non-zero")
    return ByteRange(max(resource_size - suffix_length, 0), resource_size - 1)
