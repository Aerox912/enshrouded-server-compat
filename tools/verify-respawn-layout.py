"""Verify the pinned server's respawn ABI and reflection metadata without running it.

Usage: python verify-respawn-layout.py SERVER_EXE [--json OUTPUT_JSON]
Requires pefile. Prints JSON on success; unsupported binaries fail closed.
Static evidence does not establish callback scheduling or multiplayer acceptance.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct


SERVER_SHA256 = "001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637"


def require(condition, label):
    # Keep validation active even when Python runs with -O.
    if not condition:
        raise ValueError(label)


def verify(binary):
    raw = binary.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == SERVER_SHA256,
            "unsupported server SHA-256")
    import pefile

    pe = pefile.PE(data=raw)
    try:
        image = pe.get_memory_mapped_image()
        base = pe.OPTIONAL_HEADER.ImageBase
        functions = [(entry.struct.BeginAddress, entry.struct.EndAddress)
                     for entry in pe.DIRECTORY_ENTRY_EXCEPTION]
    finally:
        pe.close()
    checks = []

    def u64(at):
        return struct.unpack_from("<Q", image, at)[0]

    def string_pointer(at):
        rva = u64(at) - base
        require(0 <= rva < len(image), "reflection string pointer out of range")
        return image[rva:rva + 512].split(b"\0", 1)[0].decode("ascii")

    def exact(at, hex_bytes, label):
        data = bytes.fromhex(hex_bytes)
        require(image[at:at + len(data)] == data, label)
        checks.append(label)

    exact(0x69290, "48 89 74 24 18 55 48 8b ec 48 81 ec 80 00 00 00",
          "actor system hook signature")
    exact(0x692a0, "41 b8 20 00 00 00", "native actor query row size 0x20")
    for at, target in ((0x692ad, 0x5dc7f0), (0x692bf, 0x5d7960), (0x69529, 0x5d7960)):
        require(image[at] == 0xe8 and
                at + 5 + struct.unpack_from("<i", image, at + 1)[0] == target,
                f"native query call at {at:#x}")
    checks.append("native queryBegin/queryNext call ABI")
    exact(0x692f0, "48 8b 4d c0", "native Actor pointer is row +8")
    exact(0x693c6, "48 8b 91 f8 0b 00 00",
          "native currentState is 64 bits at Actor +0xbf8")
    exact(0x5d796c, "8b 41 08", "queryNext reads context cursor +8")
    exact(0x5d5130, "8b 41 08", "entity resolver reads same context cursor +8")
    require(string_pointer(0x10e3140) == "currentState" and
            u64(0x10e3140 + 0x18) == 0xbf8 and
            u64(0x10e3140 + 0x10) - base == 0x10e9c20 and
            string_pointer(0x10e9c20 + 0x20) == "keen::actor::StateMask",
            "reflection currentState offset and StateMask type")
    checks.append("reflection currentState offset and StateMask type")
    for at, label, value in ((0x10ddb28, "Dead", 7), (0x10ddbf0, "Spawning", 12)):
        require(string_pointer(at) == label and u64(at + 0x10) == value,
                f"actor state enum {label}")
    checks.append("actor state enum rows: Dead 7 and Spawning 12")
    require(string_pointer(0x131d070) == "actor_to_network" and
            u64(0x131d070 + 0x10) - base == 0x69290,
            "actor_to_network reflection callback")
    checks.append("system reflection callback actor_to_network at 0x69290")

    callers = []
    for start, end in functions:
        data = image[start:end]
        for offset in range(len(data) - 4):
            if data[offset] == 0xe8 and start + offset + 5 + struct.unpack_from(
                    "<i", data, offset + 1)[0] == 0x68a7e0:
                callers.append(start + offset)
    require(sorted(callers) == [0x687dc2, 0x68a486, 0x69f5ed, 0x6a0454],
            "player-reset direct call sites")
    exact(0x687dbb, "39 16 74 08", "player construction destroys only differing handles")
    exact(0x69f5df, "85 c9 74 0f 3b c8 74 0b",
          "player update destroys only nonzero differing handles")
    exact(0x68a471, "48 8d bb c0 01 00 00 be 10 00 00 00",
          "world teardown destroys all sixteen player slots")
    require(b"[server] Remove Player '%k'\0" in image[0x12fe638:0x12fe700],
            "logged player removal label")
    checks.append("four direct player-reset call sites: construction, teardown, handle change, logged removal")
    return {
        "static_only": True,
        "server_sha256": SERVER_SHA256,
        "verified_checks": checks,
        "player_reset_direct_callers": [hex(at) for at in callers],
        "limitation": "Static checks do not establish ECS callback scheduling, transient-state sampling, or multiplayer acceptance.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("executable", type=Path, help="supported enshrouded_server.exe")
    parser.add_argument("--json", type=Path, metavar="OUTPUT_JSON", help="also export successful results")
    args = parser.parse_args()
    try:
        if args.json:
            require(args.json.resolve() != args.executable.resolve(),
                    "JSON output must not overwrite the executable")
            if args.json.exists():
                require(not args.json.samefile(args.executable),
                        "JSON output must not alias the executable")
        result = json.dumps(verify(args.executable), indent=2) + "\n"
        if args.json:
            args.json.write_text(result, encoding="utf-8")
    except (OSError, ImportError, ValueError, struct.error) as error:
        parser.error(str(error))
    print(result, end="")


if __name__ == "__main__":
    main()
