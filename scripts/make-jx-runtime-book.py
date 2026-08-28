#!/usr/bin/env python3
"""Build OSAura's deterministic bootstrap JX .64B compiled Book.

The package follows dompipe/jx NativeBook64: the first stored entry is the
48-byte JX64/header.bin identity record, the second is JX64/manifest.json, and
compiled sections follow in stable lexical order.

This bootstrap Book deliberately carries compiler-produced runtime roots rather
than relying on C slot order:

* BAG/schema.bin       canonical record Bag field layout
* CODE/applied-bus.bin stable JX BUS system entrypoints
* CODE/prepared.bin    compact prepared-call byte stream
* HOT/calls.bin        generation-scoped call bindings
* HOT/registers.bin    W:slot:shadow -> reaction routes
* HOT/reactions.bin    reaction -> prepared-call offsets
* META/generations.bin active/candidate program generations
"""

from __future__ import annotations

import hashlib
import json
import struct
import sys
import zipfile
from pathlib import Path

FIXED_TIME = (2000, 1, 1, 0, 0, 0)
MAGIC = b"JX64B001"
FORMAT = "jx.64B/1"

BAG_FIELDS = (
    "heartbeat",
    "bus_ticks",
    "bus_collects",
    "channel_messages",
    "channel_deliveries",
    "channel_switches",
    "last_message_type",
    "prepared_calls",
    "hot_dispatches",
    "reaction_runs",
    "reaction_value",
    "generation_swaps",
    "active_generation",
)


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def stable_manifest(sections: dict[str, bytes]) -> tuple[bytes, str]:
    rows: list[dict[str, object]] = []
    canonical = bytearray()
    for name in sorted(sections):
        data = sections[name]
        digest = sha256(data)
        encoded_name = name.encode("utf-8")
        rows.append({"name": name, "bytes": len(data), "sha256": digest.hex()})
        canonical += struct.pack("<I", len(encoded_name))
        canonical += encoded_name
        canonical += struct.pack("<I", len(data))
        canonical += digest

    content_sha = sha256(bytes(canonical)).hex()
    manifest = {
        "format": FORMAT,
        "kind": "compiled-book",
        "arch": "x86_64",
        "target": "osaura",
        "book": "runtime-bootstrap",
        "compiler": "osaura-bootstrap/2",
        "content_sha256": content_sha,
        "sections": rows,
    }
    encoded = (
        json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
    return encoded, content_sha


def bag_schema() -> bytes:
    out = bytearray(b"JXBAG001")
    out += struct.pack("<HH", 1, len(BAG_FIELDS))
    for slot, name in enumerate(BAG_FIELDS):
        encoded = name.encode("ascii")
        out += struct.pack("<BBH", slot, 1, len(encoded))  # type 1 = u64
        out += encoded
    return bytes(out)


def prepared_calls() -> bytes:
    # JX ASM-call v3 rows:
    # generation, family, slot, promoted-opcode, micro-slot, arity,
    # native-operation, flags.
    rows = []
    for generation in (1, 2):
        rows.extend(
            (
                (generation, 0, 1, 0x80, 0xFF, 0, 1, 0),  # heartbeat++
                (generation, 0, 2, 0x81, 0xFF, 0, 2, 0),  # reaction_runs++
                (generation, 0, 3, 0xFF, 0, 1, 3, 0),     # reaction_value += r0
            )
        )
    out = bytearray(b"JXCALL01")
    out += struct.pack("<HH", 3, len(rows))
    for row in rows:
        out += struct.pack("<BBBBBBBB", *row)
    return bytes(out)


def hot_registers() -> bytes:
    # generation, register, slot, shadow, reaction-id, delivery, flags.
    rows = (
        (1, 1, 0, 0, 1, 0, 0),
        (1, 1, 1, 0, 2, 0, 0),
        (2, 1, 0, 0, 3, 0, 0),
        (2, 1, 1, 0, 4, 0, 0),
    )
    out = bytearray(b"JXREG001")
    out += struct.pack("<HH", 1, len(rows))
    for generation, reg, slot, shadow, reaction, delivery, flags in rows:
        out += struct.pack(
            "<BBBBHBB", generation, reg, slot, shadow, reaction, delivery, flags
        )
    return bytes(out)


def hot_reactions() -> bytes:
    # generation, reaction-id, prepared-code-offset, frame-register,
    # immediate scalar, flags.
    rows = (
        (1, 1, 1, 0, 0, 0),  # promoted 0x81: reaction_runs++
        (1, 2, 4, 0, 3, 0),  # micro0 r0: reaction_value += 3
        (2, 3, 1, 0, 0, 0),
        (2, 4, 4, 0, 5, 0),  # same route after cutover adds 5
    )
    out = bytearray(b"JXREA001")
    out += struct.pack("<HH", 1, len(rows))
    for generation, reaction, offset, frame_reg, immediate, flags in rows:
        out += struct.pack(
            "<BBHBBH", generation, reaction, offset, frame_reg, immediate, flags
        )
    return bytes(out)


def generations() -> bytes:
    rows = (
        (1, 0x80000001),
        (2, 0x80000002),
    )
    out = bytearray(b"JXGEN001")
    out += struct.pack("<HH", 1, len(rows))
    for generation, endpoint in rows:
        out += struct.pack("<QI", generation, endpoint)
    return bytes(out)


def zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, FIXED_TIME)
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def build(path: Path) -> None:
    sections = {
        "BAG/schema.bin": bag_schema(),
        "CODE/applied-bus.bin": bytes((0x7F, 0x00, 0x01, 0x7F, 0x00, 0x02)),
        # Exercises all three JX prepared-call tiers:
        #   0x80       promoted one-byte call
        #   0x81       promoted one-byte call
        #   0x00 0x02 sparse family/slot two-byte call
        #   0xC0       micro0 with first selector r0 (arity 1 => one byte)
        "CODE/prepared.bin": bytes((0x80, 0x81, 0x00, 0x02, 0xC0)),
        "HOT/calls.bin": prepared_calls(),
        "HOT/reactions.bin": hot_reactions(),
        "HOT/registers.bin": hot_registers(),
        "META/generations.bin": generations(),
    }

    manifest, content_sha = stable_manifest(sections)
    header = (
        MAGIC
        + struct.pack("<HHI", 1, 0, len(sections))
        + sha256(manifest)
    )
    if len(header) != 48:
        raise RuntimeError("JX64 header must be exactly 48 bytes")

    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED, allowZip64=False) as book:
        book.writestr(zip_info("JX64/header.bin"), header)
        book.writestr(zip_info("JX64/manifest.json"), manifest)
        for name in sorted(sections):
            book.writestr(zip_info(name), sections[name])

    print(f"created {path}")
    print(f"content_sha256={content_sha}")
    print(f"file_sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/runtime.64B")
    build(output)
