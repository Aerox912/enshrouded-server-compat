#!/usr/bin/env python3
"""Verify pinned server/client consumable-removal caller evidence offline."""

import argparse
import hashlib
from pathlib import Path
import struct


SERVER_SHA256 = "001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637"
CLIENT_SHA256 = "af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781"
IMAGE_BASE = 0x140000000
SERVER_REMOVE = 0x163E70
CLIENT_REMOVE = 0x3822B0


def require(condition, message):
    if not condition:
        raise ValueError(message)


class PEImage:
    def __init__(self, path, expected_hash, label):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        digest = hashlib.sha256(self.data).hexdigest()
        require(digest == expected_hash,
                f"{label} SHA-256 {digest} does not match pinned image")
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        require(self.data[pe:pe + 4] == b"PE\0\0", f"{label} PE signature")
        self.machine, section_count = struct.unpack_from("<HH", self.data, pe + 4)
        optional_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        optional = pe + 24
        require(struct.unpack_from("<H", self.data, optional)[0] == 0x20B,
                f"{label} is not PE32+")
        self.base = struct.unpack_from("<Q", self.data, optional + 24)[0]
        require(self.base == IMAGE_BASE, f"{label} image base")
        self.image_size = struct.unpack_from("<I", self.data, optional + 56)[0]
        section = optional + optional_size
        self.sections = []
        for index in range(section_count):
            entry = section + index * 40
            name = self.data[entry:entry + 8].split(b"\0", 1)[0]
            virtual_size, virtual, raw_size, raw = struct.unpack_from(
                "<IIII", self.data, entry + 8)
            characteristics = struct.unpack_from("<I", self.data, entry + 36)[0]
            self.sections.append((name, virtual, virtual_size, raw, raw_size,
                                  characteristics))
        self.text = next(s for s in self.sections if s[0] == b".text")
        pdata_rva, pdata_size = struct.unpack_from("<II", self.data,
            optional + 112 + 3 * 8)
        pdata_raw = self.raw_offset(pdata_rva, pdata_size)
        self.functions = []
        for offset in range(pdata_raw, pdata_raw + pdata_size, 12):
            begin, end, _unwind = struct.unpack_from("<III", self.data, offset)
            if begin:
                self.functions.append((begin, end))

    def raw_offset(self, rva, size=1):
        for _name, virtual, virtual_size, raw, raw_size, _flags in self.sections:
            backed = min(virtual_size, raw_size)
            relative = rva - virtual
            if relative >= 0 and relative + size <= backed:
                result = raw + relative
                require(result + size <= len(self.data), "RVA outside file")
                return result
        raise ValueError(f"RVA 0x{rva:x} is not backed by file bytes")

    def bytes_at(self, rva, size):
        offset = self.raw_offset(rva, size)
        return self.data[offset:offset + size]

    def u64(self, rva):
        return struct.unpack("<Q", self.bytes_at(rva, 8))[0]

    def string_at_va(self, address):
        require(self.base <= address < self.base + self.image_size,
                "string pointer leaves image")
        offset = self.raw_offset(address - self.base)
        end = self.data.find(b"\0", offset, min(len(self.data), offset + 512))
        require(end >= 0, "unterminated image string")
        return self.data[offset:end].decode("ascii")

    def call_target(self, rva):
        instruction = self.bytes_at(rva, 5)
        require(instruction[0] == 0xE8, f"expected CALL at 0x{rva:x}")
        return rva + 5 + struct.unpack_from("<i", instruction, 1)[0]

    def function_range(self, rva):
        matches = [(begin, end) for begin, end in self.functions
                   if begin <= rva < end]
        require(len(matches) == 1, f"no unique PDATA range for 0x{rva:x}")
        return matches[0]

    def direct_callers(self, target):
        _name, virtual, _virtual_size, raw, raw_size, _flags = self.text
        found = []
        for offset in range(raw, raw + raw_size - 4):
            if self.data[offset] != 0xE8:
                continue
            rva = virtual + offset - raw
            if self.call_target(rva) == target:
                found.append(rva)
        return found


def normalized_window(server, server_site, server_query_call,
                      client, client_site, client_query_call,
                      mask_setup_call=False, span=0x42):
    server_start = server_site - 0x30
    client_start = client_site - 0x30
    left = bytearray(server.bytes_at(server_start, span))
    right = bytearray(client.bytes_at(client_start, span))
    for start, site, query_call in (
        (server_start, server_site, server_query_call),
        (client_start, client_site, client_query_call),
    ):
        window = left if start == server_start else right
        calls = [site, query_call]
        if mask_setup_call:
            calls.append(site - 0x30)
        for call in calls:
            index = call - start
            require(window[index] == 0xE8,
                    f"expected relative CALL at RVA 0x{call:x}")
            window[index + 1:index + 5] = b"\0\0\0\0"
    return left == right


def verify(server_path, client_path):
    server = PEImage(server_path, SERVER_SHA256, "server")
    client = PEImage(client_path, CLIENT_SHA256, "client")
    require(server.machine == client.machine == 0x8664, "expected AMD64 images")

    # Client use and fire callers map to same offsets within matching server
    # function ranges. Only the two direct-call relocations differ locally.
    pairs = (
        (0x5C980, 0x5C95C, 0x222450, 0x22242C, 0x5D5130,
         0x8D3490, 0x163E70, 0x3822B0, 0x597, "used"),
        (0x30CE4, 0x30CC1, 0x1E61C4, 0x1E61A1, 0x5D5130,
         0x8D3490, 0x163E70, 0x3822B0, 0x2A4, "fired"),
    )
    for (server_site, server_query, client_site, client_query,
         server_query_target, client_query_target, server_remove_target,
         client_remove_target, expected_offset, label) in pairs:
        require(server.call_target(server_site) == server_remove_target,
                f"server {label} removal target")
        require(client.call_target(client_site) == client_remove_target,
                f"client {label} removal target")
        require(server.call_target(server_query) == server_query_target,
                f"server {label} query-entity helper")
        require(client.call_target(client_query) == client_query_target,
                f"client {label} query-entity helper")
        require(server.bytes_at(server_site - 3, 3) == bytes.fromhex("44 8b 00") and
                client.bytes_at(client_site - 3, 3) == bytes.fromhex("44 8b 00"),
                f"{label} removal owner is copied from query-entity result")
        server_function = server.function_range(server_site)
        client_function = client.function_range(client_site)
        require(server_function[1] - server_function[0] ==
                client_function[1] - client_function[0],
                f"{label} server/client function size")
        require(server_site - server_function[0] == expected_offset and
                client_site - client_function[0] == expected_offset,
                f"{label} server/client callsite offset")
        require(normalized_window(server, server_site, server_query,
                client, client_site, client_query,
                mask_setup_call=(label == "fired")),
                f"{label} caller argument/result sequence differs")
        print(f"PASS {label} paired caller ABI, owner source, and result sequence")

    require(server.bytes_at(0x5C985, 5) == bytes.fromhex("40 38 74 24 38") and
            server.bytes_at(0x5C98C, 4) == bytes.fromhex("39 74 24 3c"),
            "used caller status/remainder result checks")
    require(server.bytes_at(0x30CE9, 4) == bytes.fromhex("80 7d 10 00") and
            server.bytes_at(0x30CF3, 4) == bytes.fromhex("83 7d 14 00"),
            "fired caller status/remainder result checks")
    print("PASS native result fields are status +0 and remainder +4 at both server sites")

    # The used path is a registered server system callback. The descriptor getter
    # returns the descriptor whose callback at +0x10 is the query system entry.
    getter_call = 0x1D6A1E
    require(server.call_target(getter_call) == 0xA72CB0,
            "used action descriptor getter call")
    getter = server.bytes_at(0xA72CB0, 8)
    require(getter[:3] == bytes.fromhex("48 8d 05") and getter[7] == 0xC3,
            "used action descriptor getter shape")
    descriptor = 0xA72CB0 + 7 + struct.unpack_from("<i", getter, 3)[0]
    require(descriptor == 0x1319530 and
            server.string_at_va(server.u64(descriptor)) == "actor_apply_buff" and
            server.u64(descriptor + 0x10) == server.base + 0x5C3A0,
            "actor_apply_buff descriptor callback pointer")
    require(server.bytes_at(0x1D6A23, 3) == bytes.fromhex("48 8b d0") and
            server.call_target(0x1D6A2C) == 0x5F7AF0,
            "used action descriptor is passed to native registration")
    require(server.bytes_at(0x5C3BD, 6) == bytes.fromhex("41 b8 a0 00 00 00") and
            server.call_target(0x5C3CA) == 0x5DC7F0 and
            server.call_target(0x5C3DC) == 0x5D7960,
            "registered used callback query row and iterator ABI")
    print("PASS used action callback is registered as actor_apply_buff")

    # The fired removal helper is narrowly identified and owner scoped, but its
    # enclosing system registration is intentionally not asserted here.
    fired_callers = server.direct_callers(0x30A40)
    require(fired_callers == [0x33897, 0x33E57, 0x34062],
            f"fired helper direct callers changed: {fired_callers!r}")
    enclosing = {server.function_range(site) for site in fired_callers}
    require(enclosing == {(0x33289, 0x34258)},
            "fired helper callers no longer share the pinned wrapper")
    require(server.bytes_at(0x30CE1, 3) == bytes.fromhex("44 8b 00") and
            server.call_target(0x30CC1) == 0x5D5130,
            "fired helper passes its native query entity as inventory owner")
    print("PASS fired site has one native helper and three direct caller edges")

    remover_callers = server.direct_callers(SERVER_REMOVE)
    require(remover_callers == [0x2FA4B, 0x30CE4, 0x5C980,
        0x1073EC, 0x1572DB, 0x163E5D],
        f"server generic remover caller set changed: {remover_callers!r}")
    print("PASS exact bypass policy can preserve the other four direct removal sites")
    print("LIMIT fired enclosing-system registration is not proven; no hooks or support are enabled")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--client", required=True, type=Path)
    args = parser.parse_args()
    verify(args.server, args.client)


if __name__ == "__main__":
    main()
