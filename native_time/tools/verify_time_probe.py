#!/usr/bin/env python3
"""Offline evidence checks for the pinned Enshrouded time-control binaries."""
from __future__ import annotations

import hashlib
import json
import struct
import sys
from pathlib import Path

SERVER_PATH = Path(r"E:\Scratch\enshrouded-flight-runtime-20261008\enshrouded_server.exe")
DLL_PATH = Path(r"E:\Scratch\creative-original-1.1-20261008\creative\creative.dll")
SIGNATURE_PATH = Path(r"E:\Scratch\creative-original-1.1-20261008\creative\sigs\6A4236C8-2DA7000.json")
EXPECTED = {
    SERVER_PATH: "001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637",
    DLL_PATH: "636bb4df119eeb999d0348ff31e26038716fb01515dbfbc81a9323ecaf2f9698",
    SIGNATURE_PATH: "00b843d5f343801ca54f021a9499581622eb46014a2e58a56dd2ea15a8926d38",
}

class PEImage:
    def __init__(self, path: Path, expected_base: int):
        self.path = path
        self.data = path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError(f"{path}: missing DOS signature")
        peoff = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[peoff:peoff + 4] != b"PE\0\0":
            raise ValueError(f"{path}: missing PE signature")
        coff = peoff + 4
        machine, section_count = struct.unpack_from("<HH", self.data, coff)
        optional_size = struct.unpack_from("<H", self.data, coff + 16)[0]
        opt = coff + 20
        if machine != 0x8664 or struct.unpack_from("<H", self.data, opt)[0] != 0x20B:
            raise ValueError(f"{path}: expected AMD64 PE32+")
        self.image_base = struct.unpack_from("<Q", self.data, opt + 24)[0]
        if self.image_base != expected_base:
            raise ValueError(f"{path}: image base {self.image_base:#x} != {expected_base:#x}")
        section_offset = opt + optional_size
        self.sections = []
        for i in range(section_count):
            off = section_offset + 40 * i
            name = self.data[off:off + 8].split(b"\0", 1)[0]
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", self.data, off + 8)
            self.sections.append((name, rva, virtual_size, raw_offset, raw_size))
        self.opt = opt

    def offset(self, rva: int, size: int = 1) -> int:
        for name, start, virtual_size, raw_offset, raw_size in self.sections:
            span = max(virtual_size, raw_size)
            if start <= rva and rva + size <= start + span:
                delta = rva - start
                if delta + size > raw_size:
                    break
                return raw_offset + delta
        raise ValueError(f"{self.path}: RVA {rva:#x}+{size:#x} is not backed by file data")

    def read(self, rva: int, size: int) -> bytes:
        off = self.offset(rva, size)
        return self.data[off:off + size]

    def read_u64(self, rva: int) -> int:
        return struct.unpack("<Q", self.read(rva, 8))[0]

    def read_cstr(self, rva: int) -> str:
        off = self.offset(rva)
        end = self.data.find(b"\0", off)
        if end < 0:
            raise ValueError(f"{self.path}: unterminated string at RVA {rva:#x}")
        return self.data[off:end].decode("ascii", errors="replace")

    def import_at_iat(self, wanted_iat_rva: int) -> tuple[str, str] | None:
        directory_rva, directory_size = struct.unpack_from("<II", self.data, self.opt + 112 + 8)
        cursor = directory_rva
        end = directory_rva + directory_size
        while cursor + 20 <= end:
            original, timestamp, forwarder, name_rva, first_thunk = struct.unpack("<IIIII", self.read(cursor, 20))
            if not (original or timestamp or forwarder or name_rva or first_thunk):
                break
            dll_name = self.read_cstr(name_rva)
            lookup = original or first_thunk
            index = 0
            while True:
                entry = struct.unpack("<Q", self.read(lookup + 8 * index, 8))[0]
                if entry == 0:
                    break
                if not entry & (1 << 63):
                    imported_name = self.read_cstr(entry + 2)
                    if first_thunk + 8 * index == wanted_iat_rva:
                        return dll_name, imported_name
                index += 1
            cursor += 20
        return None


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"PASS {message}")


def direct_target(image: PEImage, rva: int, opcode: int) -> int:
    raw = image.read(rva, 5)
    if raw[0] != opcode:
        raise AssertionError(f"{image.path}: expected opcode {opcode:#x} at {rva:#x}, got {raw[0]:#x}")
    displacement = struct.unpack_from("<i", raw, 1)[0]
    return rva + 5 + displacement


def indirect_iat_target(image: PEImage, rva: int) -> int:
    raw = image.read(rva, 6)
    if raw[:2] != b"\xff\x15":
        raise AssertionError(f"{image.path}: expected RIP-relative indirect call at {rva:#x}")
    displacement = struct.unpack_from("<i", raw, 2)[0]
    return rva + 6 + displacement




def direct_call_sites(image: PEImage, target_rva: int) -> list[int]:
    sites: list[int] = []
    for name, section_rva, _virtual_size, _raw_offset, raw_size in image.sections:
        if name != b".text":
            continue
        code = image.read(section_rva, raw_size)
        for offset in range(len(code) - 4):
            if code[offset] != 0xE8:
                continue
            displacement = struct.unpack_from("<i", code, offset + 1)[0]
            if section_rva + offset + 5 + displacement == target_rva:
                sites.append(section_rva + offset)
    return sites

def runtime_function_ranges(image: PEImage, target_rva: int) -> list[tuple[int, int]]:
    # IMAGE_DIRECTORY_ENTRY_EXCEPTION is data-directory index 3 in PE32+.
    directory_rva, directory_size = struct.unpack_from(
        "<II", image.data, image.opt + 112 + 8 * 3
    )
    table = image.read(directory_rva, directory_size)
    return [
        struct.unpack_from("<II", table, offset)
        for offset in range(0, len(table) - 11, 12)
        if struct.unpack_from("<I", table, offset)[0] == target_rva
    ]

def require_bytes(image: PEImage, rva: int, hex_bytes: str, label: str) -> None:
    wanted = bytes.fromhex(hex_bytes)
    actual = image.read(rva, len(wanted))
    check(actual == wanted, f"{label} at RVA {rva:#x}")


def main() -> int:
    for path, expected_hash in EXPECTED.items():
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        check(actual_hash == expected_hash, f"pinned SHA-256 for {path.name}: {actual_hash}")

    server = PEImage(SERVER_PATH, 0x140000000)
    dll = PEImage(DLL_PATH, 0x180000000)
    signatures = json.loads(SIGNATURE_PATH.read_text(encoding="utf-8"))
    symbols = signatures["symbols"]
    offsets = signatures["offsets"]
    check(symbols["daytime.serverTick"] == "0x2D1B91", "original signature JSON pins daytime.serverTick")
    check(symbols["daytime.wantedScale"] == "0x2D05D1", "original signature JSON pins daytime.wantedScale")
    expected_offsets = {
        "daytime.syncAnchor": "0x10",
        "daytime.syncBase": "0x18",
        "daytime.syncScale": "0x20",
        "daytime.syncVersion": "0x24",
        "daytime.dawn": "0x28",
        "daytime.dusk": "0x30",
        "daytime.dayLength": "0x38",
        "daytime.nightLength": "0x40",
        "daytime.timeOfDay": "0x48",
    }
    check(all(offsets.get(k) == v for k, v in expected_offsets.items()), "original JSON pins the nine daytime field offsets")

    lambda3_type = b".?AV?$_Func_impl_no_alloc@V<lambda_3>@?7??settingsServices@Mod@ecm@@AEAA?AUSettingsServices@features@4@XZ@V?$optional@M@std@@$$V@std@@"
    lambda4_type = b".?AV?$_Func_impl_no_alloc@V<lambda_4>@?7??settingsServices@Mod@ecm@@AEAA?AUSettingsServices@features@4@XZ@XM@std@@"
    check(dll.data.find(lambda3_type) == dll.offset(0x415940), "RTTI identifies SettingsServices lambda_3 as the optional-float callback")
    check(dll.data.find(lambda4_type) == dll.offset(0x415A00), "RTTI identifies SettingsServices lambda_4 as the void-float callback")
    check(dll.read_u64(0x30FC08 + 24) == dll.image_base + 0x44ED0, "lambda_3 vtable routes its typed callback through RVA 0x44ED0")
    check(dll.read_u64(0x30FC50 + 24) == dll.image_base + 0x44F10, "lambda_4 vtable routes its typed callback through RVA 0x44F10")
    check(direct_target(dll, 0x44F18, 0xE9) == 0x5B3C, "lambda_4 adapter tail-jumps through RVA 0x5B3C")
    check(direct_target(dll, 0x5B3C, 0xE9) == 0xC8DE0, "lambda_4 adapter reaches its body at RVA 0xC8DE0")
    check(direct_target(dll, 0x44EDD, 0xE8) == 0x1271, "lambda_3 adapter calls its helper thunk")
    check(direct_target(dll, 0x1271, 0xE9) == 0xC8D10, "lambda_3 helper reaches its body at RVA 0xC8D10")

    require_bytes(dll, 0x126029, "80 7d 3c 00 74 26 f3 0f 10 45 30 e8 94 c8 ee ff 48 8b 4b 78 f3 0f 11 45 28 48 85 c9 0f 84 cb 00 00 00 48 8b 01 48 8d 55 28 ff 50 10", "TIME OF DAY UI invokes a void-float callback")
    require_bytes(dll, 0xC8DE0, "48 89 5c 24 08 57 48 83 ec 30 0f 29 74 24 20 48 8b f9 0f 28 f1 ff 15 ad b2 37 00 48 8d 4f 50 48 8b d8 e8 d9 55 f4 ff 0f 57 c9 0f 28 c6 48 87 18 e8 cb 0f f4 ff f3 0f 10 0d 73 e2 24 00 e8 94 d5 f3 ff 48 8d 4f 4c 0f 28 f0 e8 3b cd f3 ff 48 8b 5c 24 40 66 0f 7e f1 0f 28 74 24 20 87 08 48 83 c4 30 5f c3", "hour setter body stores the request value and timestamp")
    gettick_iat = indirect_iat_target(dll, 0xC8DF5)
    check(gettick_iat == 0x4440A8, "hour setter reads GetTickCount64 through IAT RVA 0x4440A8")
    check(dll.import_at_iat(gettick_iat) == ("KERNEL32.dll", "GetTickCount64"), "hour setter IAT entry resolves to Kernel32 GetTickCount64")
    check(struct.unpack("<f", dll.read(0x317090, 4))[0] == 24.0, "hour setter wraps at 24 hours")
    check(struct.unpack("<d", dll.read(0x317098, 8))[0] == 3_600_000_000_000.0, "native time units convert at 3.6e12 units per hour")

    require_bytes(dll, 0xC9110, "4c 89 44 24 18 53 56 57 48 83 ec 50 0f 29 74 24 20 48 8b f2 48 8b f9", "time-control callback accepts controller and native clock pointers")
    require_bytes(dll, 0xC913F, "48 8b 57 40 e8 c8 e3 ff ff", "time-control callback reads the native timeOfDay slot")
    require_bytes(dll, 0xC9191, "48 8d 4f 4c e8 ef e0 f3 ff b9 00 00 80 bf 0f 57 c0 87 08", "time-control callback consumes and clears the queued hour")
    check(direct_target(dll, 0xC9143, 0xE8) == 0xC7510, "native field reads use the qword reader")
    check(direct_target(dll, 0xC926D, 0xE8) == 0xC74F0, "syncScale is read as a float")
    check(direct_target(dll, 0xC927C, 0xE8) == 0xC7510, "syncAnchor is read as a qword")
    check(direct_target(dll, 0xC92A4, 0xE8) == 0xC7510, "syncBase is read as a qword")
    check(direct_target(dll, 0xC92BF, 0xE8) == 0xC79B0, "syncAnchor is written as a qword")
    check(direct_target(dll, 0xC92CB, 0xE8) == 0xC79B0, "syncBase is written as a qword")
    check(direct_target(dll, 0xC92D4, 0xE8) == 0xC74E0, "syncVersion is read as a dword")
    check(direct_target(dll, 0xC92DD, 0xE8) == 0xC79A0, "syncVersion is incremented and written as a dword")
    check(direct_target(dll, 0xC9341, 0xE8) == 0xC8CC0 and direct_target(dll, 0xC934F, 0xE8) == 0xC8CC0, "requested time is wrapped into the native cycle")
    check(direct_target(dll, 0xC935F, 0xE8) == 0xC79B0, "final requested time is applied through a syncBase qword write")
    require_bytes(dll, 0xC7510, "48 8b 04 11 c3", "qword reader loads native base plus configured offset")
    require_bytes(dll, 0xC74F0, "8b 04 11 89 44 24 08 f3 0f 10 44 24 08 c3", "float reader loads native base plus configured offset")
    require_bytes(dll, 0xC74E0, "8b 04 11 c3", "dword reader loads native base plus configured offset")
    require_bytes(dll, 0xC79B0, "4c 89 04 11 c3", "qword writer stores to native base plus configured offset")
    require_bytes(dll, 0xC79A0, "44 89 04 11 c3", "dword writer stores to native base plus configured offset")

    check(not direct_call_sites(server, 0xCACE0), "no direct E8 rel32 call in .text targets the daytime callback")
    check(
        runtime_function_ranges(server, 0xCACE0) == [(0xCACE0, 0xCAD66)],
        "server daytime callback has one PDATA range at RVA 0xCACE0..0xCAD66",
    )
    check(
        server.read_u64(0x136B9C0) == server.image_base + 0x136F730
        and server.read_cstr(0x136F730) == "server_update_daytime"
        and server.read_u64(0x136B9C8) == len("server_update_daytime")
        and server.read_u64(0x136B9D0) == server.image_base + 0xCACE0,
        "server metadata associates name, string length, and callback for server_update_daytime",
    )
    check(
        server.read_u64(0x136BA58) == server.image_base + 0x136B998
        and server.read_u64(0x136BA60) == 1
        and server.read_u64(0x136BA70) == server.image_base + 0x136B9A8
        and server.read_u64(0x136BA78) == 1
        and server.read_cstr(0xB5E1D8) == "BalancingRegistry"
        and server.read_u64(0x136B9A0) == len("BalancingRegistry")
        and server.read_cstr(0xB5E048) == "g38_daytime::Daytime"
        and server.read_u64(0x136B9B0) == len("g38_daytime::Daytime"),
        "daytime metadata has two one-count name descriptors; adjacent qwords are string lengths",
    )
    require_bytes(
        server,
        0xCACE0,
        "48 89 5c 24 08 48 89 74 24 18 57 48 83 ec 40 41 b8 18 00 00 00 48 8d 54 24 20 48 8b f1",
        "daytime callback preserves its RCX context before manager lookup",
    )
    check(
        direct_target(server, 0xCAD07, 0xE8) == 0x82B4C0
        and direct_target(server, 0xCAD14, 0xE8) == 0x8344A0,
        "daytime callback extracts both inputs from query slot 1",
    )
    require_bytes(server, 0x5DC804, "4c 8b 09", "manager lookup resolves its registry root from [context]")
    require_bytes(server, 0x5DC8DC, "48 89 1f", "manager lookup preserves context in its output record")
    require_bytes(
        server,
        0x5DC7F0,
        "48 89 5c 24 10 55 56 57 41 54 41 55 41 56 41 57 48 83 ec 60 "
        "4c 8b 09 48 8b fa 48 8b d9 41 0f b6 81 98 04 00 00 "
        "41 0f b7 b1 a2 04 00 00 48 c1 e6 04 4c 8d 04 c5 00 00 00 00",
        "manager query helper derives group sizes and replaces incoming R8 before use",
    )
    check(
        runtime_function_ranges(server, 0x5DC7F0) == [(0x5DC7F0, 0x5DCA0E)],
        "manager query helper has one complete PDATA range at RVA 0x5DC7F0..0x5DCA0E",
    )
    require_bytes(
        server,
        0x5DC815,
        "41 0f b7 b1 a2 04 00 00 48 c1 e6 04",
        "manager query helper computes one component group at a 16-byte stride",
    )
    require_bytes(
        server,
        0x5D98D0,
        "48 8b 01 44 8b 40 6c 48 8b 40 50",
        "manager helper reads registry-root fields +0x6c and +0x50",
    )
    check(direct_target(server, 0xCACFD, 0xE8) == 0x5DC7F0, "server wrapper enters the manager-backed object lookup")
    check(direct_target(server, 0xCAD24, 0xE8) == 0x5D98D0 and direct_target(server, 0xCAD44, 0xE8) == 0x5D98D0, "server wrapper repeats the object retrieval before both clock calls")
    require_bytes(server, 0xCAD29, "48 8b 4c 24 30", "day/night updater receives the wrapper object from local +0x30")
    require_bytes(server, 0xCAD49, "48 8b 4c 24 30", "native tick receives the same wrapper object from local +0x30")
    require_bytes(server, 0xCAD02, "48 8b 4c 24 28", "day/night inputs are extracted from query slot 1")
    require_bytes(
        server,
        0xCAD29,
        "48 8b 4c 24 30 4c 8b c3 48 8b d7 4c 8b 08",
        "day/night callback passes query slot 2 and both extracted settings to the updater",
    )
    check(direct_target(server, 0xCAD37, 0xE8) == 0x861790, "server wrapper sends the object to the day/night updater")
    check(direct_target(server, 0xCAD51, 0xE8) == 0x8614B0, "server wrapper tick candidate reaches native clock tick 0x8614B0")
    check(direct_target(server, 0x606C6D, 0xE8) == 0x4EF180, "alternate server candidate reaches helper 0x4EF180")
    check(direct_target(server, 0xC96BE, 0xE9) == 0xC9791, "server scale candidate 0xC9791 is an intra-function jump destination")
    check(direct_target(server, 0xC9802, 0xE8) == 0x8507C0, "server scale path calls native scale writer 0x8507C0")
    check(
        server.read_u64(0x136A360) == server.image_base + 0x136F550
        and server.read_cstr(0x136F550) == "server_nighttime_skip"
        and server.read_u64(0x136A368) == len("server_nighttime_skip")
        and server.read_u64(0x136A370) == server.image_base + 0xC9670,
        "registered server_nighttime_skip entry points to callback RVA 0xC9670",
    )
    check(
        runtime_function_ranges(server, 0xC9670) == [(0xC9670, 0xC9684)]
        and runtime_function_ranges(server, 0xC9684) == [(0xC9684, 0xC96C3)]
        and runtime_function_ranges(server, 0xC96C3) == [(0xC96C3, 0xC96CE)]
        and runtime_function_ranges(server, 0xC96CE) == [(0xC96CE, 0xC9754)]
        and runtime_function_ranges(server, 0xC9754) == [(0xC9754, 0xC9791)]
        and runtime_function_ranges(server, 0xC9791) == [(0xC9791, 0xC97E7)]
        and runtime_function_ranges(server, 0xC97E7) == [(0xC97E7, 0xC9816)],
        "nighttime skip callback's split PDATA fragments cover the setter path",
    )
    require_bytes(
        server,
        0xC9684,
        "48 89 58 08 41 b8 20 00 00 00 0f 29 78 c8 4c 8b f9 e8",
        "nighttime skip callback queries the current manager context before reading clock slot 2",
    )
    require_bytes(
        server,
        0xC9692,
        "4c 8b f9 e8",
        "nighttime skip callback saves its sole RCX context before manager lookup",
    )
    require_bytes(
        server,
        0xC9802,
        "e8 b9 6f 78 00",
        "nighttime skip callback's conditional branch invokes the native scale writer",
    )
    check(
        direct_call_sites(server, 0x8507C0) == [0xC9802],
        "pinned .text has one direct call site to native scale writer 0x8507C0",
    )
    check(
        runtime_function_ranges(server, 0x8507C0) == [(0x8507C0, 0x850800)]
        and runtime_function_ranges(server, 0x861790) == [(0x861790, 0x861814)],
        "native scale writer and day/night updater have bounded PDATA bodies",
    )
    require_bytes(
        server,
        0x8507C0,
        "48 89 5c 24 08 57 48 83 ec 30 0f 29 74 24 20 49 8b d0 "
        "0f 28 f1 49 8b d8 48 8b f9 e8",
        "native scale writer consumes clock RCX, scale XMM1, and tick R8",
    )
    require_bytes(
        server,
        0x8614B0,
        "40 53 57 48 81 ec 88 00 00 00 48 8b da 48 8b f9",
        "native tick consumes clock RCX and current tick RDX",
    )
    require_bytes(
        server,
        0x8617A4,
        "41 56 48 83 ec 20 49 8b f9 49 8b d8 48 8b f2 48 8b e9",
        "day/night updater consumes settings RDX/R8 and current tick R9 with clock RCX",
    )
    check(
        [runtime_function_ranges(server, rva) for rva in
         (0x8614B0, 0x86156C, 0x8615AA, 0x8615F6, 0x861633, 0x86164B, 0x86176D)]
        == [[(0x8614B0, 0x86156C)], [(0x86156C, 0x8615AA)],
            [(0x8615AA, 0x8615F6)], [(0x8615F6, 0x861633)],
            [(0x861633, 0x86164B)], [(0x86164B, 0x86176D)],
            [(0x86176D, 0x861781)]],
        "native tick body continues through all adjacent PDATA fragments",
    )
    check(
        direct_call_sites(server, 0x861790) == [0xCAD37]
        and direct_call_sites(server, 0x8614B0) == [0xCAD51],
        "pinned .text directly routes native day/night updater and tick only from server_update_daytime",
    )
    require_bytes(server, 0x8617EE, "ff 45 24", "day/night updater increments syncVersion on its update branch")
    require_bytes(server, 0x8617F1, "48 89 45 18", "day/night updater updates syncBase")
    require_bytes(server, 0x8617F5, "48 89 7d 10", "day/night updater updates syncAnchor")
    require_bytes(server, 0x8615E0, "ff 47 24", "server clock tick increments syncVersion")
    require_bytes(server, 0x8615E6, "48 89 47 18", "server clock tick writes syncBase")
    require_bytes(server, 0x8615EA, "48 89 5f 10", "server clock tick writes syncAnchor")
    require_bytes(server, 0x86162A, "48 89 47 48", "server clock tick writes timeOfDay")
    require_bytes(server, 0x8507E0, "ff 47 24", "server scale writer increments syncVersion")
    require_bytes(server, 0x8507E3, "f3 0f 11 77 20", "server scale writer stores syncScale")
    require_bytes(server, 0x8507ED, "48 89 5f 10", "server scale writer stores syncAnchor")
    require_bytes(server, 0x8507F6, "48 89 47 18", "server scale writer stores syncBase")
    require_bytes(
        server,
        0x82D040,
        "f2 0f 59 05 30 ea 31 00 66 0f 5a c0 f3 0f 59 41 20 0f 5a c8",
        "server clock tick converts elapsed time to float before applying syncScale",
    )
    require_bytes(
        server,
        0x82D054,
        "f2 0f 59 0d d4 20 32 00 f2 48 0f 2c c9",
        "server clock tick converts back to integer units and truncates",
    )
    check(
        struct.unpack("<d", server.read(0xB4BA78, 8))[0] == 1.0e-9
        and struct.unpack("<d", server.read(0xB4F130, 8))[0] == 1.0e9,
        "server clock tick constants are 1e-9 and 1e9",
    )

    print("LIMIT query_layout: the 16-byte lane's counter meaning remains unresolved; policy uses only the callback-local clock view and parent-validated world query.")
    print("LIMIT callback_serialization: server_nighttime_skip and server_update_daytime are separate registered writers; static traces do not prove their runtime thread/order or safe guard lock order.")
    print("LIMIT world_reuse: Identity.lifecycle is an owner/actor lifecycle, not a native world generation. Observable world/clock/owner discontinuities must invalidate state; indistinguishable same-address reuse still requires a native generation/destruction signal or fail-closed abandonment of restoration.")
    print("LIMIT restoration: native tick advances syncVersion, syncBase, syncAnchor, and timeOfDay. Exact version equality is too strict; safe compare/restore progression still needs runtime evidence and must preserve unrelated changes.")
    print("LIMIT native_activation: the isolated read-only server observer adapter is default-off and not integrated or activated; no time-write backend or live write is active.")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
