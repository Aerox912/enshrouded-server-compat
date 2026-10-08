"""Read-only comparison of the client glider hook with a dedicated-server PE.

Requires pefile and capstone. This reports static candidates, not working hooks.
No executable or game resource is modified.
"""
import argparse
import bisect
import hashlib
import json
import re
import struct
from pathlib import Path

import capstone
import pefile


class Image:
    def __init__(self, path):
        self.path = Path(path)
        self.pe = pefile.PE(str(path))
        self.data = self.pe.get_memory_mapped_image()
        self.functions = [(e.struct.BeginAddress, e.struct.EndAddress)
                          for e in self.pe.DIRECTORY_ENTRY_EXCEPTION]
        self.starts = [start for start, _ in self.functions]
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def function(self, rva):
        index = bisect.bisect_right(self.starts, rva) - 1
        if index < 0 or not self.functions[index][0] <= rva < self.functions[index][1]:
            raise ValueError(f"No unwind range contains {rva:#x}")
        return self.functions[index]

    def instructions(self, start, end):
        return list(self.md.disasm(self.data[start:end], start))

    def normalized(self, start, end):
        data = bytearray(self.data[start:end])
        mask = bytearray(len(data))
        previous = None
        for instruction in self.instructions(start, end):
            offset = instruction.address - start
            if instruction.mnemonic == "call" or instruction.group(capstone.CS_GRP_JUMP):
                mask[offset + instruction.imm_offset:offset + instruction.imm_offset + instruction.imm_size] = bytes([1]) * instruction.imm_size
            memory = [o.mem for o in instruction.operands if o.type == capstone.x86.X86_OP_MEM]
            relative = any(o.base == capstone.x86.X86_REG_RIP for o in memory)
            # MSVC switch tables: lea reg,[image base]; mov eax,[reg+index*4+RVA].
            # Only normalize an image RVA when the immediately preceding LEA
            # establishes that exact base register. Ordinary field offsets stay.
            if previous is not None and previous.mnemonic == "lea" and len(previous.operands) == 2:
                dest, source = previous.operands
                if (dest.type == capstone.x86.X86_OP_REG and source.type == capstone.x86.X86_OP_MEM
                        and source.mem.base == capstone.x86.X86_REG_RIP
                        and previous.address + previous.size + source.mem.disp == 0):
                    relative |= any(o.base == dest.reg and o.index != 0 and 0 <= o.disp < len(self.data)
                                    for o in memory)
            if relative:
                # Capstone 5 reports disp_size=2 for some 0x66-prefixed SSE
                # instructions even though their encoded RIP displacement is 4.
                size = instruction.size - instruction.disp_offset - instruction.imm_size
                mask[offset + instruction.disp_offset:offset + instruction.disp_offset + size] = bytes([1]) * size
            previous = instruction
        return bytes(data), bytes(mask)

    def find(self, data, mask):
        pattern = b"".join(b"." if masked else re.escape(bytes([value]))
                           for value, masked in zip(data, mask))
        return [section.VirtualAddress + match.start()
                for section in self.pe.sections if section.Characteristics & 0x20000000
                for match in re.finditer(pattern, section.get_data(), re.DOTALL)]

    def manifest(self):
        return {"name": self.path.name,
                "sha256": hashlib.sha256(self.path.read_bytes()).hexdigest(),
                "timestamp": hex(self.pe.FILE_HEADER.TimeDateStamp),
                "image_size": hex(self.pe.OPTIONAL_HEADER.SizeOfImage)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("client")
    parser.add_argument("server")
    parser.add_argument("--site", type=lambda value: int(value, 0), default=0x22AFE1)
    args = parser.parse_args()
    client, server = Image(args.client), Image(args.server)
    start, end = client.function(args.site)
    full_matches = server.find(*client.normalized(start, end))
    local_matches = server.find(*client.normalized(args.site, args.site + 0x100))
    report = {"client": client.manifest(), "server": server.manifest(),
              "client_site": hex(args.site), "client_unwind_range": [hex(start), hex(end)],
              "full_normalized_matches": [hex(m) for m in full_matches],
              "full_match_hook_sites": [hex(m + args.site - start) for m in full_matches],
              "local_256_byte_matches": [hex(m) for m in local_matches],
              "candidates": [], "runtime_verified": False}
    for match in local_matches:
        original = server.data[match:match + 16]
        constant = match + 8 + struct.unpack_from("<i", original, 4)[0]
        report["candidates"].append({
            "site": hex(match), "signature": original.hex(),
            "constant_rva": hex(constant),
            "constant_float": struct.unpack_from("<f", server.data, constant)[0],
            "unwind_range": [hex(rva) for rva in server.function(match)],
            "instructions": [f"{i.address:08x} {i.mnemonic} {i.op_str}"
                             for i in server.instructions(match, match + 0x70)]})
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
