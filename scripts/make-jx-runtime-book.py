#!/usr/bin/env python3
"""Build deterministic OSAura JX .64B compiled Books.

Profiles:
  boot  -> generations 1 and 2
  next  -> generations 2 and 3

The overlap is intentional: the running runtime can require that the incoming
Book contains the currently active generation before allowing a quiescent
cutover to the new generation.

Hot-call ABI v4:
  1xxxxxxx             = one-byte [bank:4][shadow:3] hot call
  0xxxxxxx xxxxxxxx    = two-byte extended family/slot call

Operands/selectors live in HOT/calls.bin metadata and prepared state; an MSB=1
opcode never consumes a second byte.
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

PROFILES = {
    "boot": ((1, 3), (2, 5)),
    "next": ((2, 5), (3, 9)),
}


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def stable_manifest(sections: dict[str, bytes], profile: str) -> tuple[bytes, str]:
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
        "book": f"runtime-{profile}",
        "compiler": "osaura-bootstrap/3",
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
        out += struct.pack("<BBH", slot, 1, len(encoded))
        out += encoded
    return bytes(out)


def prepared_calls(specs: tuple[tuple[int, int], ...]) -> bytes:
    """HOT/calls.bin v4 rows.

    Row bytes:
      generation, family, slot, hot_opcode, selector0, arity,
      native_operation, flags

    0xff hot_opcode means the row is available only through extended family/slot.
    A hot opcode may alias the same family/slot row used by the extended form.
    """
    rows = []
    for generation, _ in specs:
        rows.extend(
            (
                (generation, 0, 1, 0x80, 0xFF, 0, 1, 0),
                (generation, 0, 2, 0x81, 0xFF, 0, 2, 0),
                (generation, 0, 3, 0xC0, 0x00, 1, 3, 0),
            )
        )
    out = bytearray(b"JXCALL01")
    out += struct.pack("<HH", 4, len(rows))
    for row in rows:
        out += struct.pack("<BBBBBBBB", *row)
    return bytes(out)


def hot_registers(specs: tuple[tuple[int, int], ...]) -> bytes:
    rows = []
    for generation, _ in specs:
        base = generation * 2 - 1
        rows.extend(
            (
                (generation, 1, 0, 0, base, 0, 0),
                (generation, 1, 1, 0, base + 1, 0, 0),
            )
        )
    out = bytearray(b"JXREG001")
    out += struct.pack("<HH", 1, len(rows))
    for row in rows:
        out += struct.pack("<BBBBHBB", *row)
    return bytes(out)


def hot_reactions(specs: tuple[tuple[int, int], ...]) -> bytes:
    rows = []
    for generation, reaction_value in specs:
        base = generation * 2 - 1
        rows.extend(
            (
                (generation, base, 1, 0, 0, 0),
                (generation, base + 1, 4, 0, reaction_value, 0),
            )
        )
    out = bytearray(b"JXREA001")
    out += struct.pack("<HH", 1, len(rows))
    for row in rows:
        out += struct.pack("<BBHBBH", *row)
    return bytes(out)


def generations(specs: tuple[tuple[int, int], ...]) -> bytes:
    out = bytearray(b"JXGEN001")
    out += struct.pack("<HH", 1, len(specs))
    for generation, _ in specs:
        out += struct.pack("<QI", generation, 0x80000000 | generation)
    return bytes(out)


def zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, FIXED_TIME)
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def build(path: Path, profile: str) -> None:
    try:
        specs = PROFILES[profile]
    except KeyError as exc:
        raise SystemExit(f"unknown profile {profile!r}; expected boot or next") from exc

    sections = {
        "BAG/schema.bin": bag_schema(),
        "CODE/applied-bus.bin": bytes((0x7F, 0x00, 0x01, 0x7F, 0x00, 0x02)),
        # 0x80, 0x81 and 0xC0 are all complete one-byte hot calls under v4.
        # 0x00,0x02 is one complete two-byte extended call.
        "CODE/prepared.bin": bytes((0x80, 0x81, 0x00, 0x02, 0xC0)),
        "HOT/calls.bin": prepared_calls(specs),
        "HOT/reactions.bin": hot_reactions(specs),
        "HOT/registers.bin": hot_registers(specs),
        "META/generations.bin": generations(specs),
    }

    manifest, content_sha = stable_manifest(sections, profile)
    header = MAGIC + struct.pack("<HHI", 1, 0, len(sections)) + sha256(manifest)
    if len(header) != 48:
        raise RuntimeError("JX64 header must be exactly 48 bytes")

    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED, allowZip64=False) as book:
        book.writestr(zip_info("JX64/header.bin"), header)
        book.writestr(zip_info("JX64/manifest.json"), manifest)
        for name in sorted(sections):
            book.writestr(zip_info(name), sections[name])

    print(f"created {path} profile={profile}")
    print(f"content_sha256={content_sha}")
    print(f"file_sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/runtime.64B")
    profile = sys.argv[2] if len(sys.argv) > 2 else "boot"
    build(output, profile)
