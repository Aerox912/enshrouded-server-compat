"""Read only the isolated flight test's native ownership tables, never write memory."""
import ctypes as c
import json
from pathlib import Path
import re
import struct

root = Path(r"E:\Scratch\enshrouded-flight-runtime-20261008")
pid = int((root / "test.pid").read_text().strip())
k = c.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.argtypes = [c.c_uint, c.c_bool, c.c_uint]
k.OpenProcess.restype = c.c_void_p
k.CloseHandle.argtypes = [c.c_void_p]
k.ReadProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.QueryFullProcessImageNameW.argtypes = [c.c_void_p, c.c_uint, c.c_wchar_p, c.POINTER(c.c_uint)]
h = k.OpenProcess(0x1010, False, pid)
if not h:
    raise c.WinError(c.get_last_error())
try:
    path = c.create_unicode_buffer(32768)
    length = c.c_uint(len(path))
    if not k.QueryFullProcessImageNameW(h, 0, path, c.byref(length)) or Path(path.value) != root / "enshrouded_server.exe":
        raise RuntimeError("PID does not belong to the isolated test server")
    def read(at, fmt="Q"):
        buf = c.create_string_buffer(struct.calcsize(fmt))
        done = c.c_size_t()
        if not at or not k.ReadProcessMemory(h, at, buf, len(buf), c.byref(done)) or done.value != len(buf):
            return 0
        return struct.unpack("<" + fmt, buf.raw)[0]
    matches = re.findall(r"state=([0-9A-F]+) image=([0-9A-F]+)", (root / "xhl-server-adapter.log").read_text())
    state, base = [int(v, 16) for v in matches[-1]]
    channels = read(state + 0x38)
    session = read(channels)
    wrapper = read(session + 0x10)
    backend = read(wrapper + 0x18)
    internal = session + 0x4b68
    result = {"pid": pid, "state": hex(state), "world": hex(read(state+0x1b0)),
              "channels": hex(channels), "session": hex(session), "wrapper": hex(wrapper),
              "backend": hex(backend), "vtable_rva": hex(read(backend)-base), "players": []}
    for slot in range(16):
        record = state + 0x1c0 + slot * 0x2bb38
        player, machine = read(record,"I"), read(record+4,"I")
        if not player:
            continue
        smachine = read(internal+0x2ab4+slot*0x1d0,"I")
        mrecord = internal + 0x4770 + (machine & 127)*0xb50
        peer = read(mrecord+6,"H")
        context = backend+0xd88
        precord = context+0x70+(peer&63)*0x118
        result["players"].append({"slot": slot,"player":player,"machine":machine,
            "session_player":read(internal+0x2a60+slot*0x1d0,"I"),"session_machine":smachine,
            "machine_record":read(mrecord,"I"),"peer":peer,"peer_record":read(precord,"H"),
            "peer_state":read(precord+0x98,"B"),"auth_enabled":read(context+0x1510,"B"),
            "hosting":read(context+0x1512,"B")})
    print(json.dumps(result,indent=2))
finally:
    k.CloseHandle(h)
