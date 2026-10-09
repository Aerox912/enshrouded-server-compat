"""Verify the pinned server's crafting EntityId-to-Actor lookup evidence.

Usage: python verify-native-cost-operator.py SERVER_EXE
This is a static source/ABI check; it does not execute a server or prove gameplay.
"""

import argparse
import hashlib
from pathlib import Path
import struct


SERVER_SHA256 = "001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637"
IMAGE_BASE = 0x140000000


def require(condition, label):
    if not condition:
        raise ValueError(label)


def verify(path):
    data = path.read_bytes()
    require(hashlib.sha256(data).hexdigest() == SERVER_SHA256,
            "unsupported server SHA-256")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[pe:pe + 4] == b"PE\0\0", "invalid PE signature")
    sections_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    require(struct.unpack_from("<H", data, optional)[0] == 0x20B,
            "server is not PE32+")
    require(struct.unpack_from("<Q", data, optional + 24)[0] == IMAGE_BASE,
            "unexpected image base")
    sections = []
    section = optional + optional_size
    for index in range(sections_count):
        entry = section + index * 40
        name = data[entry:entry + 8].split(b"\0", 1)[0]
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, entry + 8)
        sections.append((name, virtual_address, raw_offset, raw_size,
                         virtual_size))

    def offset(rva, size=1):
        for name, virtual, raw, raw_size, virtual_size in sections:
            extent = max(raw_size, virtual_size)
            if virtual <= rva and rva + size <= virtual + extent:
                at = raw + rva - virtual
                require(at + size <= len(data), "RVA outside file data")
                return at
        raise ValueError(f"RVA is not mapped: {rva:#x}")

    def bytes_at(rva, size):
        at = offset(rva, size)
        return data[at:at + size]

    def u64(rva):
        return struct.unpack_from("<Q", data, offset(rva, 8))[0]

    def cstring_rva(rva):
        at = offset(rva)
        end = data.find(b"\0", at, min(len(data), at + 512))
        require(end >= 0, "unterminated reflection string")
        return data[at:end].decode("ascii")

    def reflected_name(pointer_rva):
        pointer = u64(pointer_rva)
        require(IMAGE_BASE <= pointer < IMAGE_BASE + 0x1DA7000,
                "reflection pointer outside pinned image")
        return cstring_rva(pointer - IMAGE_BASE)

    def call_target(rva):
        call = bytes_at(rva, 5)
        require(call[0] == 0xE8, f"expected direct call at {rva:#x}")
        return rva + 5 + struct.unpack("<i", call[1:])[0]

    checks = []

    # Both event fields use the same reflected EntityId type; station stays a
    # separate field at +0x0c from operator at +0x08.
    operator_field = 0xDC59C0
    station_field = 0xDC59F0
    entity_type = 0x11A9870
    require(reflected_name(operator_field) == "craftingOperatorId" and
            u64(operator_field + 0x10) - IMAGE_BASE == entity_type and
            u64(operator_field + 0x18) == 0x08,
            "craftingOperatorId reflection name/type/offset")
    require(reflected_name(station_field) == "craftingStationId" and
            u64(station_field + 0x10) - IMAGE_BASE == entity_type and
            u64(station_field + 0x18) == 0x0C,
            "craftingStationId reflection name/type/offset")
    require(reflected_name(entity_type + 0x20) == "keen::ds::ecs::EntityId",
            "native EntityId reflection type")
    checks.append("craft operator and station are separate reflected EntityId fields")

    # The recipe callback's native operator validator accepts the complete
    # 32-bit key 1..0x3ff, verifies the keyed 0x60 record, and reads its data.
    require(bytes_at(0x119D70, 32) == bytes.fromhex(
        "8d42ff 3dfe030000 771a 4c8d0c40 49c1e105 43391401 750c "
        "430fb6440110 8801"),
        "operator EntityId range/table-key validation bytes")
    checks.append("raw operator EntityId is range-checked without compact-owner masking")

    # Match the original component lookup ABI used by Creative inventory and
    # by the native operator Actor validator. The ECS accessor is a direct
    # context+0x30 load; the event path resolves through 0x5c8f30.
    require(bytes_at(0x5C8F30, 24) == bytes.fromhex(
        "4053 4883ec20 488bd9 e892000000 488bc3 4883c420 5b c3 cc"),
        "native component resolver prologue/return")
    require(bytes_at(0x5D52D0, 5) == bytes.fromhex("488b4130 c3"),
            "native query-context-to-ECS accessor")
    require(bytes_at(0x5D5130, 3) == bytes.fromhex("8b4108"),
            "native query entity accessor uses context cursor")
    call = bytes_at(0x169CA4, 5)
    require(call[0] == 0xE8 and
            0x169CA4 + 5 + struct.unpack("<i", call[1:])[0] == 0x5C8F30,
            "operator Actor validator resolves through native component resolver")
    checks.append("Actor component resolver and query-context ECS ABI")

    # The registered system descriptor resolves directly to the pinned
    # CraftRecipeEvent callback. This is the exact callback hooked by runtime.
    require(call_target(0x1D662F) == 0xA73010 and
            call_target(0x1D663D) == 0x5F7AF0,
            "CraftRecipeEvent descriptor registration call chain")
    require(bytes_at(0xA73010, 8) == bytes.fromhex("488d05b9c58a00c3") and
            u64(0x131F5D0 + 0x10) - IMAGE_BASE == 0x6C1A0,
            "registered callback descriptor points to system callback at +0x10")
    checks.append("registered CraftRecipeEvent system callback at 0x6c1a0")

    # System callback/query and event-row ABI. The query row is 0xe8 bytes;
    # its Actor descriptor is row+0x38. Event operator/station IDs are copied
    # to the first two dwords of the 0x1aba90 action context.
    require(bytes_at(0x6C1A0, 16) == bytes.fromhex(
        "40554154488d ac24b8fdffff4881ec48"),
        "CraftRecipeEvent system callback entry ABI")
    require(bytes_at(0x6C1B3, 6) == bytes.fromhex("41b8e8000000") and
            call_target(0x6C1C3) == 0x5DC7F0 and
            call_target(0x6C1D8) == 0x5D7960,
            "native callback query row size and iterator ABI")
    require(bytes_at(0x6C304, 7) == bytes.fromhex("4c8b85e8000000") and
            bytes_at(0x6C1B9, 3) == bytes.fromhex("488d95"),
            "native Actor descriptor is query row+0x38")
    require(call_target(0x6C264) == 0x5D5130 and
            bytes_at(0x6C269, 5) == bytes.fromhex("8b08394e0c"),
            "query entity is compared with event stationId")
    require(bytes_at(0x6C362, 14) == bytes.fromhex(
        "8b4608894424408b460c89442444"),
        "action context stores operatorId then stationId")
    require(bytes_at(0x1ABA90, 16) == bytes.fromhex(
        "48895c2410488974241848897c242055") and
            bytes_at(0x150990, 16) == bytes.fromhex(
        "48895c2420555641564881ec80000000"),
        "craft action and processor hook entry signatures")
    require(call_target(0x6C5E5) == 0x1ABA90 and
            call_target(0x6C658) == 0x1ABA90 and
            call_target(0x1ABE6E) == 0x150990,
            "registered crafting action and nested transaction processor calls")
    checks.append("station/owner event offsets and nested craft action ABI")

    # The processor's bit 3 branch skips ingredient validation and rejoins the
    # normal output path; the hook only scopes that transaction flag.
    require(bytes_at(0x150A1A, 8) == bytes.fromhex("a8080f85fc000000") and
            bytes_at(0x150B1E, 7) == bytes.fromhex("f685b000000004"),
            "ingredient bypass branch rejoins normal transaction output")
    checks.append("crafting flag skips ingredients while preserving output")

    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path,
                        help="pinned dedicated enshrouded_server.exe")
    args = parser.parse_args()
    checks = verify(args.executable)
    for check in checks:
        print(f"PASS {check}")
    print("Static actor-owner mapping evidence verified; no gameplay claim.")


if __name__ == "__main__":
    main()
