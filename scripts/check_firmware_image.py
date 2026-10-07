#!/usr/bin/env python3
"""Validate the Arduino ESP8266 full-image CRC and the expected flash header."""
import argparse
import hashlib
from pathlib import Path
import struct


def crc32_msb(data):
    """Non-reflected CRC-32, polynomial 0x04C11DB7, init FFFFFFFF, xorout 0."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
    return crc


def validate(path):
    raw = bytearray(path.read_bytes())
    if len(raw) < 4120 or raw[:4] != bytes((0xE9, 2, 2, 0x40)):
        raise ValueError("Expected Arduino eboot image with DIO, 4 MB flash, 40 MHz")
    if raw[4096] != 0xE9:
        raise ValueError("Missing application image at offset 4096")
    declared_size, expected_crc = struct.unpack_from("<II", raw, 4112)
    if declared_size != len(raw):
        raise ValueError("Firmware length does not match the embedded image size")
    struct.pack_into("<II", raw, 4112, 0, 0)
    if crc32_msb(raw) != expected_crc:
        raise ValueError("Full-image CRC mismatch")
    return len(raw), hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    args = parser.parse_args()
    try:
        length, sha256 = validate(args.firmware)
    except (OSError, ValueError) as error:
        parser.exit(1, "Firmware check failed: %s\n" % error)
    print("Verified %d-byte ESP8266 image; full-image CRC valid." % length)
    print("SHA256: %s" % sha256)


if __name__ == "__main__":
    main()
