#!/usr/bin/env python3
"""Build OSAura's deterministic bootstrap JX .64B compiled Book.

The package follows dompipe/jx NativeBook64: the first stored entry is the
48-byte JX64/header.bin identity record, the second is JX64/manifest.json, and
compiled sections follow in stable lexical order.
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
        "compiler": "osaura-bootstrap/1",
        "content_sha256": content_sha,
        "sections": rows,
    }
    encoded = (
        json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
    return encoded, content_sha


def zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, FIXED_TIME)
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def build(path: Path) -> None:
    sections = {
        "BAG/schema.bin": (
            b"jx.bag.container/1\0record\0heartbeat\0bus_ticks\0bus_collects\0"
            b"channel_messages\0channel_deliveries\0"
        ),
        "CODE/applied-bus.bin": bytes((0x7F, 0x00, 0x01, 0x7F, 0x00, 0x02)),
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
