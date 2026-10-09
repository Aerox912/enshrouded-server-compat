"""Read-only verifier for the pinned Enshrouded Creative-flight binary leads.

This checks PE identity, unwind ranges, instruction bytes, and direct call edges.
It proves reflected action names, but does not establish actor ownership, a safe
hook ABI, held/toggle input semantics, or runtime
acceptance, and it never writes to a target binary.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


class VerificationError(RuntimeError):
    pass


@dataclass(frozen=True)
class Section:
    name: str
    virtual_size: int
    virtual_address: int
    raw_size: int
    raw_pointer: int
    characteristics: int


@dataclass(frozen=True)
class RuntimeFunction:
    begin: int
    end: int
    unwind: int


class PEImage:
    def __init__(self, data: bytes, path: str = "<memory>") -> None:
        self.data = data
        self.path = path
        self.sha256 = hashlib.sha256(data).hexdigest().upper()
        self.sections: list[Section] = []
        self.runtime_functions: list[RuntimeFunction] = []
        self._parse()

    @classmethod
    def read(cls, path: str | Path) -> "PEImage":
        file_path = Path(path)
        return cls(file_path.read_bytes(), str(file_path))

    def _unpack(self, fmt: str, offset: int) -> tuple:
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(self.data):
            raise VerificationError(f"{self.path}: truncated PE structure at file offset {offset:#x}")
        return struct.unpack_from(fmt, self.data, offset)

    def _parse(self) -> None:
        if len(self.data) < 0x40 or self.data[:2] != b"MZ":
            raise VerificationError(f"{self.path}: missing DOS header")
        pe_offset, = self._unpack("<I", 0x3C)
        if self.data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise VerificationError(f"{self.path}: missing PE signature")
        self.machine, section_count, self.timestamp, _, _, optional_size, _ = self._unpack("<HHIIIHH", pe_offset + 4)
        if self.machine != 0x8664:
            raise VerificationError(f"{self.path}: expected AMD64 PE, found machine {self.machine:#x}")
        optional = pe_offset + 24
        magic, = self._unpack("<H", optional)
        if magic != 0x20B:
            raise VerificationError(f"{self.path}: expected PE32+ optional header")
        self.image_base, = self._unpack("<Q", optional + 24)
        self.image_size, = self._unpack("<I", optional + 56)
        directory_count, = self._unpack("<I", optional + 108)
        if directory_count <= 3:
            raise VerificationError(f"{self.path}: no PE exception directory")
        exception_rva, exception_size = self._unpack("<II", optional + 112 + 3 * 8)
        section_offset = optional + optional_size
        for index in range(section_count):
            offset = section_offset + index * 40
            raw_name = self.data[offset:offset + 8]
            if len(raw_name) != 8:
                raise VerificationError(f"{self.path}: truncated section table")
            name = raw_name.rstrip(b"\0").decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_pointer = self._unpack("<IIII", offset + 8)
            characteristics, = self._unpack("<I", offset + 36)
            self.sections.append(Section(name, virtual_size, virtual_address, raw_size, raw_pointer, characteristics))
        if exception_size % 12:
            raise VerificationError(f"{self.path}: malformed exception directory size {exception_size:#x}")
        exception_offset = self.rva_to_offset(exception_rva, exception_size)
        for offset in range(exception_offset, exception_offset + exception_size, 12):
            begin, end, unwind = self._unpack("<III", offset)
            if begin and begin < end:
                self.runtime_functions.append(RuntimeFunction(begin, end, unwind))

    def rva_to_offset(self, rva: int, size: int = 1) -> int:
        if rva < 0 or size < 0:
            raise VerificationError(f"{self.path}: invalid RVA/size {rva:#x}/{size:#x}")
        for section in self.sections:
            extent = max(section.virtual_size, section.raw_size)
            if section.virtual_address <= rva and rva + size <= section.virtual_address + extent:
                delta = rva - section.virtual_address
                if delta + size > section.raw_size:
                    break
                offset = section.raw_pointer + delta
                if offset + size > len(self.data):
                    break
                return offset
        raise VerificationError(f"{self.path}: RVA {rva:#x} (+{size:#x}) is not backed by a section")

    def code_bytes(self, rva: int, size: int) -> bytes:
        section = next((s for s in self.sections
                        if s.virtual_address <= rva and rva + size <=
                        s.virtual_address + max(s.virtual_size, s.raw_size)), None)
        if section is None or not section.characteristics & 0x20000000:
            raise VerificationError(f"{self.path}: RVA {rva:#x} is not in an executable section")
        start = self.rva_to_offset(rva, size)
        return self.data[start:start + size]

    def functions_at(self, rva: int) -> list[RuntimeFunction]:
        return [entry for entry in self.runtime_functions if entry.begin <= rva < entry.end]

    def function_at(self, rva: int) -> RuntimeFunction:
        matches = self.functions_at(rva)
        if len(matches) != 1:
            raise VerificationError(
                f"{self.path}: expected one unwind range containing {rva:#x}, found {len(matches)}")
        return matches[0]

    def manifest(self) -> dict:
        return {
            "path": self.path,
            "sha256": self.sha256,
            "timestamp": f"0x{self.timestamp:08x}",
            "image_base": f"0x{self.image_base:x}",
            "image_size": f"0x{self.image_size:x}",
        }


    def qword_at(self, rva: int) -> int:
        offset = self.rva_to_offset(rva, 8)
        value, = self._unpack("<Q", offset)
        return value

    def c_string_at(self, rva: int, limit: int = 256) -> str:
        offset = self.rva_to_offset(rva)
        end = self.data.find(b"\0", offset, min(len(self.data), offset + limit))
        if end < 0:
            raise VerificationError(f"{self.path}: unterminated string at RVA {rva:#x}")
        try:
            return self.data[offset:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise VerificationError(f"{self.path}: non-ASCII registration name at RVA {rva:#x}") from error


SERVER_BUILD = {
    "sha256": "001C1B40ED091D8C1AEE583ADDE3800D7C858AE2C7F4DFF54FCA2938B2BE1637",
    "timestamp": 0x69FDECC9,
    "image_base": 0x140000000,
    "image_size": 0x1DA7000,
}
CLIENT_BUILD = {
    "sha256": "AF2F5A1227911D8AA06B3908D6BD0211838211CAE14EA91099CB57D0DF990781",
    "timestamp": 0x6A4236C8,
    "image_base": 0x140000000,
    "image_size": 0x2DA7000,
}
CREATIVE_BUILD = {
    "sha256": "636BB4DF119EEB999D0348FF31E26038716FB01515DBFBC81A9323ECAF2F9698",
    "timestamp": 0x6AC7A6B7,
    "image_base": 0x180000000,
    "image_size": 0x44F000,
}

SERVER_SITES = (
    {"name": "locomotion-state-entry", "rva": 0x1767C0, "function": (0x1767C0, 0x176858),
     "signature": "48895C240848896C2410488974241848897C2420", "instruction_lengths": (5, 5, 5, 5)},
    {"name": "air-jump-static-candidate", "rva": 0x174E21, "function": (0x174DC0, 0x174EBA),
     "signature": "80BACD00000001", "instruction_lengths": (7,)},
    {"name": "dive-mover-a-entry", "rva": 0x17E480, "function": (0x17E480, 0x17E717),
     "signature": "488BC4488958104889781855488D68A1", "instruction_lengths": (3, 4, 4, 1, 4)},
    {"name": "dive-mover-b-entry", "rva": 0x180820, "function": (0x180820, 0x180AB7),
     "signature": "488BC4488958104889781855488D68A1", "instruction_lengths": (3, 4, 4, 1, 4)},
    {"name": "gravity-static-candidate", "rva": 0x7E8AE, "function": (0x7E817, 0x7E989),
     "signature": "488B442458", "instruction_lengths": (5,)},
    {"name": "fall-damage-static-candidate", "rva": 0x6B711, "function": (0x6B67E, 0x6B98D),
     "signature": "400FB6FE", "instruction_lengths": (4,)},
    {"name": "mover-dispatch-static-candidate", "rva": 0x187595, "function": (0x187177, 0x187751),
     "signature": "488B45380FB6483D83F90A7777418B8C8E5C7718004903CEFFE1",
     "instruction_lengths": (4, 4, 3, 2, 8, 3, 2)},
)

CALL_EDGES = (
    {"name": "dispatch-to-mover-b", "rva": 0x187602, "signature": "E81992FFFF", "target": 0x180820},
    {"name": "dispatch-to-mover-a", "rva": 0x187614, "signature": "E8676EFFFF", "target": 0x17E480},
    {"name": "state-decision-caller", "rva": 0x189DB3, "signature": "E808CAFEFF", "target": 0x1767C0},
)

STATE_QUERY_LOOP = {
    "name": "locomotion-state-query-loop",
    "rva": 0x189BF0,
    "function": (0x189BF0, 0x189C30),
    "signature": (
        "405556488DAC2458FFFFFF4881ECA8010000"
        "41B870000000488D542440488BF1E8DB2B4500"
        "41B870000000488D542440488BCEE838DD4400"
        "84C00F841E060000"
    ),
    "instruction_lengths": (2, 1, 8, 7, 6, 5, 3, 5, 6, 5, 3, 5, 2, 6),
}
STATE_QUERY_EDGES = (
    {"name": "locomotion-state-query-begin", "rva": 0x189C10,
     "signature": "E8DB2B4500", "target": 0x5DC7F0},
    {"name": "locomotion-state-query-step", "rva": 0x189C23,
     "signature": "E838DD4400", "target": 0x5D7960},
)

# Registration records contain a string-view (name pointer/length), callback,
# then group pointer/count pairs. Group names do not prove runtime row order.
REGISTERED_SYSTEMS = (
    {
        "name_entry": 0x1340100, "name": "locomotion_state", "name_length": 0x10,
        "callback": 0x189BF0,
        "groups": (
            {"descriptor": 0x1340118, "array": 0x1340048,
             "components": (("Locomotion", 10),)},
            {"descriptor": 0x1340138, "array": 0x1340058,
             "components": (("Actor", 5), ("Movement", 8), ("WorldCollider", 13))},
            {"descriptor": 0x1340148, "array": 0x1340090,
             "components": (("Climb", 5), ("HookShot", 8), ("InertialFrame", 13), ("SlopeConfig", 11))},
            {"descriptor": 0x1340158, "array": 0x13400D0,
             "components": (("ActorInput", 10), ("DynamicLocomotion", 17))},
        ),
    },
)

STATE_BIT_QUERIES = (
    {"rva": 0x176841, "state_bit": 0x27, "signature": "B227488BCBE89539EBFF"},
    {"rva": 0x176856, "state_bit": 0x2C, "signature": "B22C0F29742430488BCBE87B39EBFF"},
    {"rva": 0x176869, "state_bit": 0x24, "signature": "B224488BCBE86D39EBFF"},
    {"rva": 0x17688A, "state_bit": 0x2B, "signature": "B22B488BCBE84C39EBFF"},
    {"rva": 0x176898, "state_bit": 0x24, "signature": "B224488BCBE83E39EBFF"},
    {"rva": 0x1768B9, "state_bit": 0x25, "signature": "B225488BCBE81D39EBFF"},
    {"rva": 0x1768CB, "state_bit": 0x00, "signature": "33D2488BCBE80B39EBFF"},
)
ACTOR_STATE_HELPER = {
    "rva": 0x2A1E0,
    "signature": (
        "4C8BC10FB6CABA0100000048D3E241F680B101000001"
        "498B80F80B00007414498B88D80B0000490B80D00B0000"
        "48F7D14823C1488BCA4823C8483BCA0F94C0C3"
    ),
    "instruction_lengths": (3, 3, 5, 3, 8, 7, 2, 7, 7, 3, 3, 3, 3, 3, 3, 1),
}
STATE_CALLER_SETUP = {
    "rva": 0x189D79,
    "signature": (
        "488B4424784C8B4D80488B542450488B4DA8"
        "410FB6783D4889442438488B4424684889442430"
        "488B4424604889442428488B4424584889442420"
        "E808CAFEFF"
    ),
    "instruction_lengths": (5, 4, 5, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5),
}

PLUGIN_CALLBACKS = (
    {"name": "state-callback", "rva": 0xD8E80, "function": (0xD8E80, 0xD8FF2)},
    {"name": "fall-callback", "rva": 0xD9070, "function": (0xD9070, 0xD9102)},
    {"name": "gravity-callback", "rva": 0xD92B0, "function": (0xD92B0, 0xD9328)},
    {"name": "mover-callback", "rva": 0xD9370, "function": (0xD9370, 0xD95AD)},
    {"name": "local-input-callback", "rva": 0xD9720, "function": (0xD9720, 0xD977B)},
)


def require_build(image: PEImage, expected: dict) -> None:
    for field in ("sha256", "timestamp", "image_base", "image_size"):
        want = expected[field]
        got = getattr(image, field)
        if got != want:
            if field == "sha256":
                expected_text, actual_text = want, got
            else:
                expected_text, actual_text = f"{want:#x}", f"{got:#x}"
            raise VerificationError(
                f"{image.path}: {field} mismatch: expected {expected_text}, found {actual_text}")


def verify_site(image: PEImage, spec: dict) -> dict:
    signature = bytes.fromhex(spec["signature"])
    if len(signature) != sum(spec["instruction_lengths"]):
        raise VerificationError(f"{spec['name']}: instruction lengths do not cover the signature")
    actual = image.code_bytes(spec["rva"], len(signature))
    if actual != signature:
        raise VerificationError(
            f"{image.path}: {spec['name']} bytes drifted at RVA {spec['rva']:#x}: "
            f"expected {signature.hex()}, found {actual.hex()}")
    actual_range = None
    if spec["function"] is None:
        if image.functions_at(spec["rva"]):
            raise VerificationError("expected a leaf anchor without an unwind record")
    else:
        actual_range = image.function_at(spec["rva"])
        if (actual_range.begin, actual_range.end) != spec["function"]:
            raise VerificationError(
                f"{image.path}: {spec['name']} unwind range changed: "
                f"expected {spec['function'][0]:#x}-{spec['function'][1]:#x}, "
                f"found {actual_range.begin:#x}-{actual_range.end:#x}")
    return {
        "name": spec["name"],
        "rva": f"0x{spec['rva']:x}",
        "function": [f"0x{actual_range.begin:x}", f"0x{actual_range.end:x}"] if actual_range else None,
        "bytes": actual.hex().upper(),
        "instruction_lengths": list(spec["instruction_lengths"]),
    }


def relative_call_target(rva: int, instruction: bytes) -> int:
    if len(instruction) != 5 or instruction[0] != 0xE8:
        raise VerificationError(f"RVA {rva:#x}: expected a five-byte direct CALL rel32")
    displacement, = struct.unpack_from("<i", instruction, 1)
    return rva + 5 + displacement


def verify_call_edges(image: PEImage, edges: Iterable[dict]) -> list[dict]:
    result = []
    for edge in edges:
        expected = bytes.fromhex(edge["signature"])
        actual = image.code_bytes(edge["rva"], len(expected))
        if actual != expected:
            raise VerificationError(
                f"{image.path}: {edge['name']} bytes drifted at RVA {edge['rva']:#x}")
        target = relative_call_target(edge["rva"], actual)
        if target != edge["target"]:
            raise VerificationError(
                f"{image.path}: {edge['name']} target changed: expected "
                f"{edge['target']:#x}, found {target:#x}")
        containing = image.function_at(edge["rva"])
        result.append({
            "name": edge["name"],
            "rva": f"0x{edge['rva']:x}",
            "target": f"0x{target:x}",
            "caller_function": [f"0x{containing.begin:x}", f"0x{containing.end:x}"],
            "bytes": actual.hex().upper(),
        })
    return result


def registration_name(image: PEImage, entry: int) -> str:
    """Validate a native string-view, without inferring component identity/order."""
    pointer = image.qword_at(entry)
    if not image.image_base <= pointer < image.image_base + image.image_size:
        raise VerificationError("invalid registration name pointer")
    name = image.c_string_at(pointer - image.image_base)
    if not name or image.qword_at(entry + 8) != len(name):
        raise VerificationError("registration string-view length drift")
    return name


def verify_registered_systems(image: PEImage) -> list[dict]:
    results = []
    for spec in REGISTERED_SYSTEMS:
        name = registration_name(image, spec["name_entry"])
        callback = image.qword_at(spec["name_entry"] + 0x10)
        if name != spec["name"] or len(name) != spec["name_length"]:
            raise VerificationError(f"{image.path}: registration name/length changed")
        if callback != image.image_base + spec["callback"]:
            raise VerificationError(f"{image.path}: {spec['name']} callback registration changed")
        component_groups = []
        for group in spec["groups"]:
            descriptor = group["descriptor"]
            if (image.qword_at(descriptor) != image.image_base + group["array"] or
                    image.qword_at(descriptor + 8) != len(group["components"])):
                raise VerificationError(f"{image.path}: query descriptor drifted at {descriptor:#x}")
            names = []
            for index, (expected_name, expected_length) in enumerate(group["components"]):
                pair = group["array"] + index * 0x10
                found = registration_name(image, pair)
                if found != expected_name or len(found) != expected_length:
                    raise VerificationError(f"{image.path}: component name drifted at {pair:#x}")
                names.append(found)
            component_groups.append(names)
        results.append(dict(name=name, name_length=len(name), callback=hex(spec["callback"]),
                            query_groups=component_groups, runtime_row_order_proven=False))
    return results


def verify_state_query_trace(image: PEImage) -> dict:
    loop = verify_site(image, STATE_QUERY_LOOP)
    edges = verify_call_edges(image, STATE_QUERY_EDGES)
    caller = verify_caller_setup(image)
    owner_call = image.function_at(0x189DB3)
    if (owner_call.begin, owner_call.end) != (0x189C30, 0x18A24E):
        raise VerificationError(f"{image.path}: locomotion_state continuation range changed")
    return {
        "query_entry": loop,
        "iterator_edges": edges,
        "context": "RCX is preserved in RSI and passed to query begin and step",
        "row_buffer": "RSP+0x40",
        "row_size": "0x70",
        "native_state_call": caller,
        "state_input_pointer": "RDX <- [RSP+0x50], row offset +0x10",
        "query_continuation": [f"0x{owner_call.begin:x}", f"0x{owner_call.end:x}"],
    }

def verify_state_queries(image: PEImage) -> list[dict]:
    results = []
    for query in STATE_BIT_QUERIES:
        expected = bytes.fromhex(query["signature"])
        actual = image.code_bytes(query["rva"], len(expected))
        if actual != expected:
            raise VerificationError(
                f"{image.path}: state-query sequence drifted at {query['rva']:#x}")
        call_rva = query["rva"] + len(expected) - 5
        target = relative_call_target(call_rva, actual[-5:])
        if target != ACTOR_STATE_HELPER["rva"]:
            raise VerificationError(
                f"{image.path}: state query at {query['rva']:#x} calls {target:#x}, "
                f"expected helper {ACTOR_STATE_HELPER['rva']:#x}")
        results.append({"rva": f"0x{query['rva']:x}", "state_bit": query["state_bit"]})
    helper_signature = bytes.fromhex(ACTOR_STATE_HELPER["signature"])
    if image.code_bytes(ACTOR_STATE_HELPER["rva"], len(helper_signature)) != helper_signature:
        raise VerificationError(f"{image.path}: native actor-state helper bytes drifted")
    if len(helper_signature) != sum(ACTOR_STATE_HELPER["instruction_lengths"]):
        raise VerificationError("actor-state helper instruction lengths do not cover signature")
    if image.functions_at(ACTOR_STATE_HELPER["rva"]):
        raise VerificationError("native actor-state helper unexpectedly has an unwind range")
    return results


def verify_caller_setup(image: PEImage) -> dict:
    spec = STATE_CALLER_SETUP
    expected = bytes.fromhex(spec["signature"])
    actual = image.code_bytes(spec["rva"], len(expected))
    if actual != expected or len(expected) != sum(spec["instruction_lengths"]):
        raise VerificationError(f"{image.path}: state-decision caller setup drifted")
    call_rva = spec["rva"] + len(expected) - 5
    target = relative_call_target(call_rva, actual[-5:])
    if target != 0x1767C0:
        raise VerificationError(f"{image.path}: state-decision caller target changed to {target:#x}")
    containing = image.function_at(spec["rva"])
    return {
        "rva": f"0x{spec['rva']:x}",
        "target": f"0x{target:x}",
        "function": [f"0x{containing.begin:x}", f"0x{containing.end:x}"],
        "bytes": actual.hex().upper(),
        "instruction_lengths": list(spec["instruction_lengths"]),
    }


def verify_callbacks(image: PEImage) -> list[dict]:
    results = []
    for callback in PLUGIN_CALLBACKS:
        begin, end = callback["function"]
        actual = image.function_at(callback["rva"])
        if (actual.begin, actual.end) != (begin, end) or callback["rva"] != begin:
            raise VerificationError(f"{image.path}: {callback['name']} range changed")
        results.append({
            "name": callback["name"],
            "rva": f"0x{callback['rva']:x}",
            "function": [f"0x{begin:x}", f"0x{end:x}"],
        })
    return results


STATE_NAMES = (
    (0x10DDA10, "Grounded", 0), (0x10DDFB0, "Attached", 0x24),
    (0x10DDFD8, "HangGliding", 0x25), (0x10DE028, "Flying", 0x27),
    (0x10DE0C8, "FloatingOnWater", 0x2B), (0x10DE0F0, "UnderWater", 0x2C),
)


def reflected_fields(image: PEImage, descriptor: int) -> dict[str, int]:
    count = image.qword_at(descriptor + 0x48) & 0xFFFFFFFF
    if count > 128:
        raise VerificationError("unbounded reflection field count")
    table = image.qword_at(descriptor + 0x58) - image.image_base
    fields = {}
    for index in range(count):
        entry = table + index * 48
        name = image.c_string_at(image.qword_at(entry) - image.image_base)
        if name in fields:
            raise VerificationError("duplicate reflection field name")
        fields[name] = image.qword_at(entry + 24)
    return fields


def verify_state_names(image: PEImage) -> list[dict]:
    rows = []
    for at, name, bit in STATE_NAMES:
        found = image.c_string_at(image.qword_at(at) - image.image_base)
        value = image.qword_at(at + 16)
        if (found, value) != (name, bit):
            raise VerificationError("Actor State enum reflection drift")
        rows.append(dict(name=name, state_bit=bit, reflection_rva=hex(at)))
    return rows


OBSERVER_BINDINGS = (
    {"name": "inventory-player-input-row-load", "rva": 0x15B18D,
     "function": (0x15B145, 0x15C4F5), "signature": "4C8B6DD0", "instruction_lengths": (4,)},
    {"name": "inventory-player-input-native-field", "rva": 0x15B1D7,
     "function": (0x15B145, 0x15C4F5), "signature": "488B45F88B8934030000", "instruction_lengths": (4,6)},
    {"name": "inventory-row-passed-to-actor-helper", "rva": 0x15B8BB,
     "function": (0x15B145, 0x15C4F5), "signature": "488D55C0498BCEE8C990FFFF", "instruction_lengths": (4,3,5)},
    {"name": "inventory-actor-helper-preserves-row", "rva": 0x1549AC,
     "function": (0x154990, 0x154AA5), "signature": "488B72104C8BE1488B4A404C8BF2", "instruction_lengths": (4,3,4,3)},
    {"name": "inventory-linked-actor-resolve-and-state", "rva": 0x154B2D,
     "function": (0x154B2D, 0x15541F),
     "signature": "458B4710498BCC498B96B0000000E86085EDFF4885C00F84A0080000B207488BC8E88D56EDFF",
     "instruction_lengths": (4,3,7,5,3,6,2,3,5)},
)


def verify_observer_bindings(image: PEImage) -> dict:
    anchors = [verify_site(image, spec) for spec in OBSERVER_BINDINGS]
    edges = verify_call_edges(image, (
        dict(name="inventory-row-actor-helper", rva=0x15B8C2, signature="E8C990FFFF", target=0x154990),
        dict(name="inventory-linked-actor-resolver", rva=0x154B3B, signature="E86085EDFF", target=0x2D0A0),
        dict(name="inventory-linked-actor-state-test", rva=0x154B4E, signature="E88D56EDFF", target=0x2A1E0),
    ))
    return dict(anchors=anchors, call_edges=edges, player_input_row=0x10,
                actor_descriptor_row=0xB0, actor_input_row_proven=False,
                observer_reads_actor_input=False,
                proof_basis="native row loads, component resolver and Actor state helper; no registration order arithmetic")


def verify_player_input(image: PEImage) -> dict:
    player = reflected_fields(image, 0xDEC3E0)
    client = reflected_fields(image, 0xFF2C10)
    actor_input = reflected_fields(image, 0xDF4A40)
    if (player.get("fromClient") != 8 or player.get("fromClientInputMode") != 0x519
            or client.get("digitalInput") != 0x318
            or actor_input.get("desiredWorldMoveInput") != 0x198):
        raise VerificationError("PlayerInput/ActorInput layout drift")
    if image.qword_at(0xFEC8B0 + 0x40) & 0xFFFFFFFF != 8:
        raise VerificationError("PlayerDigitalInput width drift")
    table = image.qword_at(0xFC37A0 + 0x60) - image.image_base
    count = image.qword_at(0xFC37A0 + 0x48) & 0xFFFFFFFF
    if count != 54:
        raise VerificationError("PlayerInputType count drift")
    values = {image.c_string_at(image.qword_at(table + i * 40) - image.image_base):
              image.qword_at(table + i * 40 + 16) for i in range(count)}
    actions = {"Jump": 30, "Sprint": 50, "Sneak": 52}
    if any(values.get(k) != v for k, v in actions.items()):
        raise VerificationError("PlayerInputType action values drift")
    # Names establish query membership only. Actual pointer slots require
    # separate native operand/resolver evidence, never alphabetical group order.
    for at, expected in ((0x1338D88, "PlayerInput"), (0x1338DA0, "ActorInput"),
                         (0x1338E90, "Actor")):
        if image.c_string_at(image.qword_at(at) - image.image_base) != expected:
            raise VerificationError("inventory observer component registration drift")
    return dict(player_descriptor="0xdec3e0", digital_mask_offset=0x320,
                input_mode_offset=0x519, actor_input_world_move_offset=0x198,
                inventory_query_slots=dict(player_input=0x10),
                actor_descriptor_row=0xB0, actor_input_row_proven=False,
                observer_reads_actor_input=False, actions=actions,
                semantic_class="configured gameplay input, separate from actor state bits",
                held_toggle_semantics_observed=False)


def verify_input_consumer(image: PEImage, query_rva: int, consume_rva: int) -> dict:
    query = bytes.fromhex("4C8BC1B8010000000FB6CA48D3E04985400874084985007503B001C332C0C3")
    consume = bytes.fromhex("0FB6C2488B11480FABC2488911C3")
    if (image.code_bytes(query_rva, len(query)) != query or
            image.code_bytes(consume_rva, len(consume)) != consume):
        raise VerificationError("configured action consumer leaf semantics drift")
    return dict(query_rva=hex(query_rva), consume_rva=hex(consume_rva),
                query="mask+8 contains action and consumed mask+0 does not contain action",
                consume="sets action bit in consumed mask+0", held_toggle_meaning_proven=False)


CLIENT_OWNERSHIP_ANCHORS = (
    dict(name="local-player-hide-entity-and-predicate", rva=0x30B760, function=(0x30B754,0x30B7D6),
         signature="488B5C2440488D542478488BCFE81E7D5C00488BD38B08E8D4B50200", instruction_lengths=(5,5,3,5,3,2,5)),
    dict(name="local-player-entity-predicate", rva=0x336D50, function=None,
         signature="394A040F94C0C3", instruction_lengths=(3,3,1)),
    dict(name="local-player-data-global-registration", rva=0x3260F9, function=(0x324E30,0x326F6A),
         signature="488D0568DC0D01C685B0000000014C8D8DB000000048894424304D8D85C005000048C74424380F000000488D5424304889742420488BCBE8DBF05C00",
         instruction_lengths=(7,7,7,5,7,9,5,5,3,5)),
    dict(name="global-registry-fnv-loop", rva=0x8F5240, function=(0x8F5210,0x8F52BD),
         signature="410FB6040B48FFC133C369D893010001493BCA72EB", instruction_lengths=(5,3,2,6,3,2)),
    dict(name="global-registry-bucket-and-key", rva=0x8F5264, function=(0x8F5210,0x8F52BD),
         signature="4C8B9EB00F6D004D8D42FF8BC34C23C0498BC0410FB6C848C1E806498B14C348D3EAF6C20174324C8B8ED00F6D0043391C817452",
         instruction_lengths=(7,4,2,3,3,4,4,4,3,3,2,7,4,2)),
    dict(name="global-record-stride", rva=0x8F532A, function=(0x8F530B,0x8F558E),
         signature="488B45084C8D3C4049C1E7064C037D00", instruction_lengths=(4,4,4,4)),
    dict(name="global-record-hash-and-data", rva=0x8F5434, function=(0x8F530B,0x8F558E),
         signature="488B8424800000004C8B4424704885C04D8987A80000004C0F45C041899FA0000000488B4424784D8987B0000000",
         instruction_lengths=(8,5,3,7,4,7,5,7)),
)


def verify_client_ownership(image: PEImage) -> dict:
    anchors = [verify_site(image, spec) for spec in CLIENT_OWNERSHIP_ANCHORS]
    if (registration_name(image,0x1D22A80)!="local_player_hide" or
            image.qword_at(0x1D22A90)!=image.image_base+0x30B720 or
            image.c_string_at(0x1403D68)!="LocalPlayerData"):
        raise VerificationError("paired client local-player native names drift")
    edges = verify_call_edges(image, (
        dict(name="local-row-entity-helper", rva=0x30B76D, signature="E81E7D5C00", target=0x8D3490),
        dict(name="local-row-ownership-predicate", rva=0x30B777, signature="E8D4B50200", target=0x336D50),
        dict(name="local-data-register-global", rva=0x326130, signature="E8DBF05C00", target=0x8F5210),
    ))
    return dict(anchors=anchors, call_edges=edges, globals_offset=0x6D0F80, record_stride=0xC0,
                exact_name="LocalPlayerData", name_hash="0x9117771d", actor_id_offset=4,
                reader_bound=dict(max_records=512,max_capacity=4096,max_probes=64),
                current_query_world_binding_proven=False, read_only_decoder_installed=False,
                meaning="native local-player predicate compares current entity with current LocalPlayerData+4; this is not server approval")


def verify_client_toggle(image: PEImage) -> dict:
    anchors = [verify_site(image, dict(name="configured-toggle-mask-copy", rva=0x28F2D4,
        function=(0x28F277,0x28F374), signature="488B8A1005000048894C2438", instruction_lengths=(7,5))),
        verify_site(image, dict(name="native-digital-toggle-update", rva=0xCA4060,
        function=None,
        signature="80790300741E807902007525803900740E807901007508807904000F94C0C30FB64104C38039007408807902000F94C0C332C0C3",
        instruction_lengths=(4,2,4,2,3,2,4,2,4,3,1,4,1,3,2,4,3,1,2,1)))]
    return dict(anchors=anchors, toggle_config_offset=0x510,
                toggle="unless consumed, invert previous active only on raw rising edge; otherwise preserve previous active",
                hold="raw down and not consumed", sprint_sneak_have_additional_native_branches=True,
                g_climb_sink_boost_runtime_mapping_accepted=False)


def verify_control_frame(image: PEImage, client: bool = False) -> dict:
    """Direct paired native readers/writers, independent of registration order."""
    fragment = (0x2873E3,0x28958B) if client else (0xA0873,0xA2A1B)
    rows = (("player-input-row20",0x2874D4 if client else 0xA0964,"488B75D0",4),
            ("actor-input-row58",0x2874F8 if client else 0xA0988,"4C8B7508",4),
            ("actor-row48",0x287504 if client else 0xA0994,"488B4DF8",4))
    if not client:
        rows += (("native-world-move-x",0xA0CCB,"F3410F118698010000",9),
                 ("native-world-move-y",0xA0CD8,"F3410F118E9C010000",9),
                 ("native-world-move-z",0xA0CE1,"F3410F1196A0010000",9))
        if (registration_name(image,0x134B620)!="player_control_locomotion" or
                image.qword_at(0x134B630)!=image.image_base+0xA0830):
            raise VerificationError("server player control registration drift")
    anchors = [verify_site(image,dict(name=name,rva=at,function=fragment,
               signature=signature,instruction_lengths=(length,))) for name,at,signature,length in rows]
    return dict(anchors=anchors, query_size=0xB0, row_origin="native RBP-0x50",
                player_input_row=0x20, actor_row=0x48, actor_input_row=0x58,
                actor_input_world_move_offset=0x198, registration_order_used=False,
                input_capture_requires_completed_native_control=True,
                movement_hook_installed=False)


def verify_client(client_path: str) -> dict:
    image = PEImage.read(client_path)
    require_build(image, CLIENT_BUILD)
    for at, name, callback in ((0x1D32280, "locomotion_state", 0x3A8700),
                               (0x1D31450, "locomotion_execution", 0x3A5C30),
                               (0x1D3D7A0, "player_control_locomotion", 0x2873A0)):
        if (image.c_string_at(image.qword_at(at) - image.image_base) != name or
                image.qword_at(at + 16) != image.image_base + callback):
            raise VerificationError("paired client system registration drift")
    edges = []
    for at, target in ((0x3A8720, 0x8DA7C0), (0x3A8733, 0x8D5CA0),
                       (0x3A88C3, 0x3955A0)):
        actual = image.code_bytes(at, 5)
        if relative_call_target(at, actual) != target:
            raise VerificationError("paired client state/query edge drift")
        edges.append(dict(rva=hex(at), target=hex(target)))
    caller = bytes.fromhex(STATE_CALLER_SETUP["signature"][:-10])
    if image.code_bytes(0x3A8889, len(caller)) != caller:
        raise VerificationError("paired client eight-argument caller setup drift")
    helper = bytes.fromhex(ACTOR_STATE_HELPER["signature"])
    if image.code_bytes(0x1DFA50, len(helper)) != helper:
        raise VerificationError("paired client Actor state helper drift")
    player = reflected_fields(image, 0x1782F60)
    client = reflected_fields(image, 0x19D8550)
    actor = reflected_fields(image, 0x178D5A0)
    if (player.get("fromClient") != 8 or player.get("fromClientInputMode") != 0x519
            or client.get("digitalInput") != 0x318 or actor.get("desiredWorldMoveInput") != 0x198):
        raise VerificationError("paired client PlayerInput/ActorInput reflection drift")
    table = image.qword_at(0x19B5410 + 0x60) - image.image_base
    count = image.qword_at(0x19B5410 + 0x48) & 0xFFFFFFFF
    if count != 54:
        raise VerificationError("paired client action count drift")
    actions = {image.c_string_at(image.qword_at(table + i * 40) - image.image_base):
               image.qword_at(table + i * 40 + 16) for i in range(count)}
    if any(actions.get(k) != v for k, v in {"Jump": 30, "Sprint": 50, "Sneak": 52}.items()):
        raise VerificationError("paired client configured action values drift")
    # The original input lead follows these ActorInput local movement writes.
    if image.code_bytes(0x28752B, 18) != bytes.fromhex("F2410F11868C010000F3410F119694010000"):
        raise VerificationError("paired client ActorInput local movement writes drift")
    return dict(build=image.manifest(), registrations=dict(state=0x3A8700, execution=0x3A5C30,
                player_control_locomotion=0x2873A0), state_query_edges=edges,
                state_actor_row_offset=0x10, configured_actions={k: actions[k] for k in ("Jump", "Sprint", "Sneak")},
                digital_mask_offset=0x320, input_mode_offset=0x519,
                input_consumer=verify_input_consumer(image, 0x3CC720, 0x3CC6B0),
                configured_toggle=verify_client_toggle(image),
                control_frame=verify_control_frame(image,True),
                local_ownership=verify_client_ownership(image),
                local_input_lead=0x28753D, local_player_ownership_proven=False,
                prediction_runtime_acceptance=False)


def verify(server_path: str, creative_path: str) -> dict:
    server = PEImage.read(server_path)
    creative = PEImage.read(creative_path)
    require_build(server, SERVER_BUILD)
    require_build(creative, CREATIVE_BUILD)
    return {
        "status": "static offsets and bytes match pinned inputs",
        "static_only": True,
        "server": server.manifest(),
        "creative_original_module": creative.manifest(),
        "server_sites": [verify_site(server, spec) for spec in SERVER_SITES],
        "direct_call_edges": verify_call_edges(server, CALL_EDGES),
        "actor_state_bits_observed": verify_state_queries(server),
        "actor_state_names": verify_state_names(server),
        "configured_player_input": verify_player_input(server),
        "observer_native_bindings": verify_observer_bindings(server),
        "control_frame": verify_control_frame(server),
        "configured_input_consumer": verify_input_consumer(server, 0x1A81D0, 0x1A8160),
        "state_caller_setup": verify_caller_setup(server),
        "state_query_trace": verify_state_query_trace(server),
        "registered_systems": verify_registered_systems(server),
        "creative_callbacks": verify_callbacks(creative),
        "actor_state_helper": {
            "rva": f"0x{ACTOR_STATE_HELPER['rva']:x}",
            "argument": "DL is zero-extended as an Actor state-bit index",
            "input_fields": ["+0x1B1 bit 0", "+0xBF8", "+0xBD0", "+0xBD8"],
            "meaning": "bit test with conditional combination and consumed-mask filtering",
        },
        "proven_static_trace": [
            "locomotion_state is registered with Actor, Movement, and input-related query groups",
            "state caller loads its Actor argument from row+0x10; registration order is not row proof",
            "the registered state query uses the same generic begin/step functions as the F6 actor query",
        ],
        "unproven": [
            "live controller/remap behavior of G-local configured hold/toggle inputs and paired prediction",
            "complete callable ABI and data ownership for gravity/fall/mover hooks",
            "live per-player flight and controller behavior",
        ],
        "hook_implementation_authorized_by_this_report": False,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, help="pinned enshrouded_server.exe")
    parser.add_argument("--creative-module", required=True, help="authorized original creative.dll")
    parser.add_argument("--client", help="optional pinned client executable for paired static checks")
    parser.add_argument("--json", type=Path, help="optional static evidence JSON output")
    args = parser.parse_args(argv)
    try:
        result = verify(args.server, args.creative_module)
        if args.client:
            result["paired_client"] = verify_client(args.client)
    except (OSError, VerificationError) as error:
        print(json.dumps({"status": "verification failed", "error": str(error)}, indent=2), file=sys.stderr)
        return 1
    rendered = json.dumps(result, indent=2)
    if args.json:
        args.json.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
