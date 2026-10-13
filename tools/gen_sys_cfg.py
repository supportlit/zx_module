#!/usr/bin/env python3
"""
Generate sys_cfg.bin from a simple CSV.

CSV rows:
- ext_flash_type,<NOR|NAND|NOR_DUL>
- ext_flash_size,<bytes_per_flash>
- ext_flash_num,<flash_count>
- <mode>,<reg_addr>,<action>,<reg_value>
    where mode is NO_USB or USB_IN, action is taken as-is from CSV (numeric).

The tool enforces exactly 32 register entries per mode.
Outputs a 4 KiB binary with magic, version, ext flash info, register tables, and CRC32.
"""
import argparse
import binascii
import csv
import os
import struct
import sys

MAGIC = 0x53434647  # 'SCFG'
VERSION = 0x0001
RECORD_FMT = "<III"  # reg_addr, action, reg_value
RECORD_COUNT_PER_MODE = 32
MODES = {"no_usb": "no_usb", "usb_in": "usb_in"}
EXT_FLASH_MAP = {
    "NOR": 0,
    "NAND": 1,
    "NOR_DUL": 2,
    "NOR_DUAL": 2,
}
OUTPUT_SIZE = 4096


def _normalize_type(raw: str) -> str:
    """Normalize flash type strings (case/spacing-insensitive)."""

    return raw.replace(" ", "_").replace("-", "_").upper()


class SysCfgError(Exception):
    pass


def _parse_size(text: str) -> int:
    """Parse size strings like 64MB, 512KB, 16*1024*1024, or plain integers."""

    if text is None:
        raise SysCfgError("ext_flash_size missing")

    s = str(text).strip().upper().replace(" ", "")
    if not s:
        raise SysCfgError("ext_flash_size empty")

    # Expression style: 16*1024*1024
    if "*" in s:
        total = 1
        for part in s.split("*"):
            if not part:
                raise SysCfgError(f"Invalid ext_flash_size expression: {text}")
            try:
                total *= int(part, 0)
            except ValueError as exc:
                raise SysCfgError(f"Non-integer ext_flash_size term: {part}") from exc
        return total

    multiplier = 1
    for suffix, mult in (("GB", 1024 ** 3), ("MB", 1024 ** 2), ("KB", 1024), ("B", 1)):
        if s.endswith(suffix):
            multiplier = mult
            s = s[: -len(suffix)]
            break

    try:
        base = int(s, 0)
    except ValueError as exc:
        raise SysCfgError(f"Non-integer ext_flash_size: {text}") from exc

    return base * multiplier


def _parse_csv(path, default_ext_type, default_ext_size, default_ext_num):
    entries = {"no_usb": [], "usb_in": []}
    ext_type = _normalize_type(default_ext_type)
    ext_size = _parse_size(default_ext_size)
    ext_num = default_ext_num

    if not path:
        return ext_type, ext_size, ext_num, entries

    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row:
                continue
            row = [c.strip() for c in row]
            if not row[0] or row[0].startswith("#"):
                continue

            key = row[0].lower()
            if len(row) >= 2 and key == "ext_flash_type":
                ext_type = _normalize_type(row[1])
                continue
            if len(row) >= 2 and key == "ext_flash_size":
                ext_size = _parse_size(row[1])
                continue
            if len(row) >= 2 and key == "ext_flash_num":
                try:
                    ext_num = int(row[1], 0)
                except ValueError as exc:
                    raise SysCfgError(f"Non-integer ext_flash_num: {row}") from exc
                continue

            if len(row) < 4:
                raise SysCfgError(f"Invalid row (expect 4 columns): {row}")

            mode = key
            if mode not in MODES:
                raise SysCfgError(f"Unknown mode '{row[0]}', expected NO_USB or USB_IN")

            try:
                reg = int(row[1], 0)
                act = int(row[2], 0)
                val = int(row[3], 0)
            except ValueError as exc:
                raise SysCfgError(f"Non-integer reg/action/value: {row}") from exc

            record = struct.pack(RECORD_FMT, reg & 0xFFFFFFFF, act & 0xFFFFFFFF, val & 0xFFFFFFFF)
            entries[mode].append(record)

    ext_type = _normalize_type(ext_type)
    return ext_type, ext_size, ext_num, entries


def _build_image(ext_type, ext_size, ext_num, entries):
    ext_upper = _normalize_type(ext_type)
    if ext_upper not in EXT_FLASH_MAP:
        raise SysCfgError(f"Unsupported ext_flash_type '{ext_type}', use NOR, NAND or NOR_DUL")
    if ext_num < 1:
        raise SysCfgError("ext_flash_num must be >= 1")
    if ext_size <= 0:
        raise SysCfgError("ext_flash_size must be > 0")

    for mode in ("no_usb", "usb_in"):
        if len(entries[mode]) == 0:
            # fill with zeros if missing
            zero_rec = struct.pack(RECORD_FMT, 0, 0, 0)
            entries[mode] = [zero_rec] * RECORD_COUNT_PER_MODE
        if len(entries[mode]) != RECORD_COUNT_PER_MODE:
            raise SysCfgError(f"Mode {mode} must have {RECORD_COUNT_PER_MODE} entries, got {len(entries[mode])}")

    header = struct.pack("<IHHI", MAGIC, VERSION, 0, EXT_FLASH_MAP[ext_upper])
    body = b"".join(entries["no_usb"]) + b"".join(entries["usb_in"])
    tail = struct.pack("<II", ext_size & 0xFFFFFFFF, ext_num & 0xFFFFFFFF)
    data_no_crc = header + body + tail
    crc = binascii.crc32(data_no_crc) & 0xFFFFFFFF
    blob = data_no_crc + struct.pack("<I", crc)

    if len(blob) > OUTPUT_SIZE:
        raise SysCfgError("sys_cfg image exceeds partition size")

    return blob.ljust(OUTPUT_SIZE, b"\xFF")


def main():
    parser = argparse.ArgumentParser(description="Generate sys_cfg.bin from CSV")
    parser.add_argument("--csv", help="Input CSV path", default="")
    parser.add_argument("--out", help="Output sys_cfg.bin", required=False)
    parser.add_argument("--ext-flash-type", dest="ext_flash_type", default="NOR", help="Default ext flash type when CSV missing")
    parser.add_argument("--ext-flash-size", dest="ext_flash_size", type=str, default="0", help="Size of each flash in bytes; supports forms like 64MB, 512KB, 16*1024*1024 (used if CSV omits ext_flash_size)")
    parser.add_argument("--ext-flash-num", dest="ext_flash_num", type=lambda v: int(v, 0), default=1, help="Number of flashes (used if CSV omits ext_flash_num)")
    parser.add_argument("--print-type", action="store_true", help="Print EXT_FLASH_TYPE=<type> (and size/num) to stdout")
    args = parser.parse_args()

    try:
        ext_type, ext_size, ext_num, entries = _parse_csv(args.csv, args.ext_flash_type, args.ext_flash_size, args.ext_flash_num)
        blob = _build_image(ext_type, ext_size, ext_num, entries)
        if args.out:
            os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
            with open(args.out, "wb") as f:
                f.write(blob)
        if args.print_type:
            print(f"EXT_FLASH_TYPE={ext_type}")
            print(f"EXT_FLASH_SIZE={ext_size}")
            print(f"EXT_FLASH_NUM={ext_num}")
    except SysCfgError as exc:
        sys.stderr.write(f"sys_cfg generation failed: {exc}\n")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
