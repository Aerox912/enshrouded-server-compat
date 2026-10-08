import sys, bisect, json
sys.path.insert(0,r'E:\PackageCaches\enshrouded-analysis')
import pefile,capstone
C=pefile.PE(r'D:\Games\SteamLibrary\steamapps\common\Enshrouded\enshrouded.exe')
S=pefile.PE(r'E:\Scratch\enshrouded-vein-mining-20261005\enshrouded_server.exe')
P=pefile.PE(r'E:\Scratch\enshrouded-vein-mining-20261005\official\mods\XHL-Vein-Mining\XHL-Vein-Mining.dll')
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
mapping=[(0x3e5610,0x1c0be0,16,0x25b0,[0x49588,0x495a8]),(0x994810,0x87dae0,16,0x38d0,[0x49580,0x495a0]),(0x3f7940,0x1d38a0,19,0x2190,[0x49578,0x49590]),(0x3f63b0,0x1d2310,13,0x3300,[0x49530,0x49538]),(0x362510,0x145510,12,0x4bd0,[0x49520,0x49528])]
def normalized(pe,rva):
    ends={e.struct.BeginAddress:e.struct.EndAddress for e in pe.DIRECTORY_ENTRY_EXCEPTION}
    data=pe.get_data(rva,ends[rva]-rva)
    out=bytearray(data)
    for i in md.disasm(data,rva):
        off=i.address-rva
        if i.mnemonic=='call' or i.group(capstone.CS_GRP_JUMP):
            out[off+i.imm_offset:off+i.imm_offset+i.imm_size]=bytes(i.imm_size)
        if any(o.type==capstone.x86.X86_OP_MEM and o.mem.base==capstone.x86.X86_REG_RIP for o in i.operands):
            out[off+i.disp_offset:off+i.disp_offset+i.disp_size]=bytes(i.disp_size)
    return bytes(out)
for c,s,length,callback,globals_ in mapping:
    a=normalized(C,c);b=normalized(S,s)
    cb=next(e.struct.EndAddress for e in P.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress==callback)
    print(json.dumps({'client':hex(c),'server':hex(s),'client_bytes':len(a),'server_bytes':len(b),'full_normalized_match':a==b,'stolen_equal':C.get_data(c,length)==S.get_data(s,length),'stolen_bytes':S.get_data(s,length).hex(),'callback':hex(callback),'callback_end':hex(cb),'globals':[hex(x) for x in globals_]}))
