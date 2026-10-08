from pathlib import Path
exec(Path(__file__).with_name('map-xhl-extras.py').read_text(encoding='utf-8').split('targets=')[0])
for rva,length in [(0x36fee0,0x420),(0x8d3490,0x90),(0x389870,0x500)]:
 print('LONG',hex(rva),hex(length),list(map(hex,matches(rva,length))))
def calls_to(pe,target):
 result=[]
 for sec in pe.sections:
  if not sec.Characteristics&0x20000000:continue
  data=sec.get_data()
  for m in re.finditer(b'\xe8',data):
   off=m.start();rva=sec.VirtualAddress+off
   if rva+5+int.from_bytes(data[off+1:off+5],'little',signed=True)==target:result.append(rva)
 return result
for pe,target in [(c,0x36fee0),(s,0x152f20),(s,0x153350),(s,0x833a30)]:
 print('CALLS', 'client' if pe is c else 'server',hex(target),list(map(hex,calls_to(pe,target))))
for addr in calls_to(c,0x36fee0):
 for before in (0,5,10,16,32):
  match=matches(addr-before,64)
  if match:print('CALLSITE MAP',hex(addr),before,list(map(hex,match)))
