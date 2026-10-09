#!/usr/bin/env python3
"""Offline verifier for the paired native building payment path."""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

SERVER_SHA256 = "001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637"
CLIENT_SHA256 = "af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781"
SERVER_BASE = 0x140000000
SERVER_PAYMENT = 0x1C5E80
CLIENT_PAYMENT = 0x3EA870
PAYMENT_SIZE = 0x1B4
RELOCATION_RANGES = ((0x2F, 0x33), (0x95, 0x99), (0xDE, 0xE2),
                     (0x11F, 0x123), (0x171, 0x175))


class VerificationError(RuntimeError):
    pass


class PEImage:
    def __init__(self, data: bytes, label: str):
        self.data = data
        self.label = label
        nt = self.u32(0x3C)
        if data[:2] != b"MZ" or self.bytes_at(nt, 4) != b"PE\0\0":
            raise VerificationError(f"{label}: invalid PE headers")
        fh = nt + 4
        self.machine = self.u16(fh)
        count = self.u16(fh + 2)
        optional = fh + 20
        optional_size = self.u16(fh + 16)
        if self.u16(optional) != 0x20B:
            raise VerificationError(f"{label}: expected PE32+")
        self.image_base = self.u64(optional + 24)
        section = optional + optional_size
        self.sections = []
        for i in range(count):
            off = section + i * 40
            vs, va, rs, ro = struct.unpack_from("<IIII", data, off + 8)
            self.sections.append((va, ro, rs))

    def bytes_at(self, off: int, count: int) -> bytes:
        if off < 0 or count < 0 or off + count > len(self.data):
            raise VerificationError(f"{self.label}: file range invalid")
        return self.data[off:off + count]

    def u16(self, off: int) -> int:
        return struct.unpack_from("<H", self.bytes_at(off, 2))[0]

    def u32(self, off: int) -> int:
        return struct.unpack_from("<I", self.bytes_at(off, 4))[0]

    def u64(self, off: int) -> int:
        return struct.unpack_from("<Q", self.bytes_at(off, 8))[0]

    def raw(self, rva: int, count: int) -> int:
        for va, ro, rs in self.sections:
            delta = rva - va
            if delta >= 0 and delta + count <= rs:
                return ro + delta
        raise VerificationError(f"{self.label}: unmapped RVA 0x{rva:x}")

    def at(self, rva: int, count: int) -> bytes:
        return self.bytes_at(self.raw(rva, count), count)

    def u64_at_rva(self, rva: int) -> int:
        return self.u64(self.raw(rva, 8))

    def call(self, rva: int) -> int:
        ins = self.at(rva, 5)
        if ins[0] != 0xE8:
            raise VerificationError(f"{self.label}: not CALL at 0x{rva:x}")
        return rva + 5 + struct.unpack_from("<i", ins, 1)[0]

    def string_pointer(self, rva: int) -> tuple[int, str]:
        va = self.u64_at_rva(rva)
        if va < self.image_base:
            raise VerificationError(f"{self.label}: invalid descriptor string pointer")
        string_rva = va - self.image_base
        raw = self.at(string_rva, 256)
        end = raw.find(b"\0")
        if end < 0:
            raise VerificationError(f"{self.label}: unterminated descriptor name")
        return string_rva, raw[:end].decode("ascii")


def require(ok: bool, message: str) -> None:
    if not ok:
        raise VerificationError(message)


def load(path: Path, expected: str, label: str) -> PEImage:
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    require(digest == expected, f"{label}: SHA-256 mismatch: {digest}")
    print(f"PASS {label} SHA-256 {digest}")
    return PEImage(data, label)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--client", required=True, type=Path)
    args = parser.parse_args()
    try:
        server = load(args.server, SERVER_SHA256, "server")
        client = load(args.client, CLIENT_SHA256, "client")
        require(server.machine == client.machine == 0x8664 and
                server.image_base == client.image_base == SERVER_BASE,
                "expected paired AMD64 PE images at the pinned image base")

        for image, rva, label in ((server, SERVER_PAYMENT, "server"),
                                  (client, CLIENT_PAYMENT, "client")):
            require(image.at(rva, 16) == bytes.fromhex(
                "40 55 56 48 83 ec 78 48 8b f2 48 8b e9 0f b6 11"),
                f"{label}: payment prologue mismatch")
        sb = server.at(SERVER_PAYMENT, PAYMENT_SIZE)
        cb = client.at(CLIENT_PAYMENT, PAYMENT_SIZE)
        for i, (s, c) in enumerate(zip(sb, cb)):
            if s != c:
                require(any(a <= i < b for a, b in RELOCATION_RANGES),
                        f"paired payment body differs at +0x{i:x}")

        for image, base, targets, label in (
            (server, SERVER_PAYMENT, (0x1A27F0, 0x150370, 0x165090), "server"),
            (client, CLIENT_PAYMENT, (0x3C6ED0, 0x36D370, 0x3834D0), "client"),
        ):
            for off, target in zip((0x94, 0xDD, 0x170), targets):
                require(image.call(base + off) == target,
                        f"{label}: paired helper call mismatch at +0x{off:x}")
        print("PASS paired 0x1B4 building payment body and three relocated helper edges")

        name_rva, name = server.string_pointer(0x1349CF0)
        callback = server.u64_at_rva(0x1349CF0 + 0x10)
        require(name == "player_building_place_prop" and
                callback == SERVER_BASE + 0x9B810,
                "registered placement descriptor mismatch")
        _, blueprint = server.string_pointer(0x1349590)
        blueprint_callback = server.u64_at_rva(0x1349590 + 0x10)
        require(blueprint == "player_building_blueprints" and
                blueprint_callback == SERVER_BASE + 0x99C20,
                "separate blueprint descriptor mismatch")

        for site, target in ((0x9B836, 0x5DC7F0), (0x9B848, 0x5D7960),
                             (0x9BC73, 0x5D5130), (0x9BCEA, 0x16BE30),
                             (0x9BFF7, SERVER_PAYMENT),
                             (0x9C054, SERVER_PAYMENT),
                             (0x9C0C7, 0x1BDAD0), (0x9C0E8, 0x1C2DC0)):
            require(server.call(site) == target,
                    f"server: call at 0x{site:x} target mismatch")
        require(server.at(0x9BC81, 2) == bytes.fromhex("8b 08") and
                server.at(0x9BC92, 6) == bytes.fromhex("89 8d e8 01 00 00"),
                "query owner is not copied to transaction input +0xC8")
        require(server.at(0x16BF74, 24) == bytes.fromhex(
            "8b 82 c8 00 00 00 89 81 e0 00 00 00 "
            "8b 82 cc 00 00 00 89 81 e4 00 00 00"),
            "transaction input +0xC8 owner copy mismatch")
        require(server.at(0x9BF94, 14) == bytes.fromhex(
            "48 8d 85 40 05 00 00 48 89 85 28 02 00 00") and
            server.at(0x9BFA5, 6) == bytes.fromhex("88 9d 20 02 00 00") and
            server.at(0x9BFE1, 7) == bytes.fromhex("48 8d 8d 20 02 00 00") and
            server.at(0x9C04D, 7) == bytes.fromhex("48 8d 8d 20 02 00 00"),
            "placement payment context transaction/mode/argument fields mismatch")
        require(server.at(0x9BFEF, 13) == bytes.fromhex(
            "45 0f b6 80 f8 01 00 00 e8 84 9e 12 00") and
            server.at(0x9C043, 3) == bytes.fromhex("41 b0 01"),
            "placement payment request argument setup mismatch")
        print("PASS registered placement, owner-to-transaction mapping, two payment callsites, and native finalization")
        print(f"PASS distinct {blueprint} callback at RVA 0x{blueprint_callback - SERVER_BASE:x} stays unsupported")
        print("PASS native building payment static evidence")
    except (OSError, VerificationError, struct.error, UnicodeDecodeError) as exc:
        print(f"FAIL {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
