import struct
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import verify_creative_flight_offsets as verify


def synthetic_pe() -> bytes:
    data = bytearray(0x600)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    coff = 0x84
    struct.pack_into("<HHIIIHH", data, coff, 0x8664, 2, 0x12345678, 0, 0, 0xF0, 0x22)
    optional = coff + 20
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    struct.pack_into("<I", data, optional + 56, 0x3000)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<II", data, optional + 112 + 3 * 8, 0x2000, 12)
    section_table = optional + 0xF0

    data[section_table:section_table + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", data, section_table + 8, 0x1000, 0x1000, 0x200, 0x200)
    struct.pack_into("<I", data, section_table + 36, 0x60000020)

    second = section_table + 40
    data[second:second + 8] = b".pdata\0\0"
    struct.pack_into("<IIII", data, second + 8, 0x100, 0x2000, 0x200, 0x400)
    struct.pack_into("<I", data, second + 36, 0x40000040)

    data[0x210:0x215] = bytes.fromhex("E805000000")
    struct.pack_into("<III", data, 0x400, 0x1000, 0x1100, 0x2050)
    return bytes(data)


class PEImageTests(unittest.TestCase):
    def test_maps_code_and_reads_unwind_range(self):
        image = verify.PEImage(synthetic_pe())
        self.assertEqual(image.code_bytes(0x1010, 5), bytes.fromhex("E805000000"))
        entry = image.function_at(0x1010)
        self.assertEqual((entry.begin, entry.end), (0x1000, 0x1100))

    def test_verifies_site_instruction_lengths_and_bytes(self):
        image = verify.PEImage(synthetic_pe())
        result = verify.verify_site(image, {
            "name": "synthetic-call",
            "rva": 0x1010,
            "function": (0x1000, 0x1100),
            "signature": "E805000000",
            "instruction_lengths": (5,),
        })
        self.assertEqual(result["bytes"], "E805000000")

    def test_rejects_byte_drift(self):
        data = bytearray(synthetic_pe())
        data[0x210] = 0x90
        image = verify.PEImage(bytes(data))
        with self.assertRaises(verify.VerificationError):
            verify.verify_site(image, {
                "name": "synthetic-call",
                "rva": 0x1010,
                "function": (0x1000, 0x1100),
                "signature": "E805000000",
                "instruction_lengths": (5,),
            })

    def test_decodes_rel32_target(self):
        self.assertEqual(verify.relative_call_target(0x1010, bytes.fromhex("E805000000")), 0x101A)

    def test_reads_ascii_registration_data(self):
        data = bytearray(synthetic_pe())
        data[0x220:0x225] = b"Actor\0"
        struct.pack_into("<Q", data, 0x230, 0x140001020)
        image = verify.PEImage(bytes(data))
        self.assertEqual(image.qword_at(0x1030), 0x140001020)
        self.assertEqual(image.c_string_at(0x1020), "Actor")

    def test_registration_second_word_is_string_length(self):
        data = bytearray(synthetic_pe())
        data[0x220:0x226] = b"Actor\0"
        struct.pack_into("<QQ", data, 0x230, 0x140001020, 5)
        self.assertEqual(verify.registration_name(verify.PEImage(bytes(data)), 0x1030), "Actor")
        struct.pack_into("<Q", data, 0x238, 4)
        with self.assertRaises(verify.VerificationError):
            verify.registration_name(verify.PEImage(bytes(data)), 0x1030)

    def test_actor_states_and_gameplay_actions_are_separate(self):
        states = {name: bit for _, name, bit in verify.STATE_NAMES}
        self.assertEqual(states["Flying"], 39)
        self.assertNotIn(30, states.values())

    def test_registration_rejects_pointer_outside_image(self):
        data = bytearray(synthetic_pe())
        struct.pack_into("<QQ", data, 0x230, 0x140003000, 5)
        with self.assertRaises(verify.VerificationError):
            verify.registration_name(verify.PEImage(bytes(data)), 0x1030)

    def test_reflection_field_count_is_bounded(self):
        data = bytearray(synthetic_pe())
        struct.pack_into("<Q", data, 0x298, 129)
        image = verify.PEImage(bytes(data))
        with self.assertRaises(verify.VerificationError):
            verify.reflected_fields(image, 0x1050)

    def test_duplicate_reflection_fields_rejected(self):
        data = bytearray(synthetic_pe())
        struct.pack_into("<Q", data, 0x298, 2)
        struct.pack_into("<Q", data, 0x2A8, 0x140001100)
        for entry in (0x300, 0x330):
            struct.pack_into("<Q", data, entry, 0x1400011A0)
            struct.pack_into("<Q", data, entry + 24, 8)
        data[0x3A0:0x3A6] = b"field\0"
        image = verify.PEImage(bytes(data))
        with self.assertRaises(verify.VerificationError):
            verify.reflected_fields(image, 0x1050)

    def test_leaf_anchor_requires_absent_unwind_record(self):
        data = bytearray(synthetic_pe())
        data[0x300] = 0xC3
        spec = dict(name="leaf-ret", rva=0x1100, function=None,
                    signature="C3", instruction_lengths=(1,))
        self.assertIsNone(verify.verify_site(verify.PEImage(bytes(data)), spec)["function"])
        struct.pack_into("<I", data, 0x404, 0x1200)
        with self.assertRaises(verify.VerificationError):
            verify.verify_site(verify.PEImage(bytes(data)), spec)

    def test_rejects_bad_instruction_length_manifest(self):
        image = verify.PEImage(synthetic_pe())
        with self.assertRaises(verify.VerificationError):
            verify.verify_site(image, {
                "name": "synthetic-call",
                "rva": 0x1010,
                "function": (0x1000, 0x1100),
                "signature": "E805000000",
                "instruction_lengths": (4,),
            })


if __name__ == "__main__":
    unittest.main()
