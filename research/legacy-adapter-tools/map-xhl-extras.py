import sys,re,bisect,json
from pathlib import Path
sys.path.insert(0,r'E:\PackageCaches\enshrouded-analysis')
import pefile,capstone
c=pefile.PE(r'D:\Games\SteamLibrary\steamapps\common\Enshrouded\enshrouded.exe')
s=pefile.PE(r'E:\Scratch\enshrouded-vein-mining-20261005\enshrouded_server.exe')
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
def normalized(pe,rva,length):
    data=bytearray(pe.get_data(rva,length));mask=[False]*len(data)
    for i in md.disasm(data,rva):
        offset=i.address-rva
        if i.mnemonic=='call' or i.group(capstone.CS_GRP_JUMP):
            for n in range(i.imm_offset,i.imm_offset+i.imm_size):mask[offset+n]=True
        if any(o.type==capstone.x86.X86_OP_MEM and o.mem.base==capstone.x86.X86_REG_RIP for o in i.operands):
            for n in range(i.disp_offset,i.disp_offset+i.disp_size):mask[offset+n]=True
    return data,mask
def matches(rva,length):
    data,mask=normalized(c,rva,length)
    pattern=b''.join(b'.' if mask[n] else re.escape(bytes([b])) for n,b in enumerate(data))
    return [sec.VirtualAddress+m.start() for sec in s.sections if sec.Characteristics&0x20000000 for m in re.finditer(pattern,sec.get_data(),re.S)]
functions=[(e.struct.BeginAddress,e.struct.EndAddress) for e in c.DIRECTORY_ENTRY_EXCEPTION];begins=[f[0] for f in functions]
targets=[0x36fee0,0x389870,0x2c5c41,0x8d3490,0x269739,0x269980,0x371997,0xa518eb,0xcb7a90]
for rva in targets:
    b,e=functions[bisect.bisect_right(begins,rva)-1]
    if not b<=rva<e:b,e=rva,rva+9
    found=matches(b,e-b)
    print('client',hex(rva),'function',hex(b),hex(e),'full matches',list(map(hex,found)))
    if found:print('mapped site',*[hex(x+rva-b) for x in found])
    else:
        for length in (128,64,32,16):
            found=matches(rva,length)
            if found:
                print('partial',length,list(map(hex,found[:20])),'count',len(found));break
    if len(sys.argv)>1 and sys.argv[1]=='disasm':
        for i in md.disasm(c.get_data(b,min(e-b,900)),b):print(hex(i.address),i.mnemonic,i.op_str)
