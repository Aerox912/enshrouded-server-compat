"""Bounded dispatcher verifier regressions; no game inputs or process access."""
import struct,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import verify_creative_flight_dispatch as verify

def fixture(name):
 spec=verify.SPECS[name]
 text_size=(spec["table"]+0x2000+0xfff)&~0xfff
 data=bytearray(0x200+text_size)
 data[:2]=b"MZ";struct.pack_into("<I",data,0x3c,0x80);data[0x80:0x84]=b"PE\0\0"
 struct.pack_into("<HHIIIHH",data,0x84,0x8664,1,0,0,0,0xf0,0x22)
 optional=0x98;struct.pack_into("<H",data,optional,0x20b)
 struct.pack_into("<Q",data,optional+24,0x140000000)
 struct.pack_into("<I",data,optional+56,text_size+0x1000);struct.pack_into("<I",data,optional+108,16)
 def write(rva,value):data[0x200+rva-0x1000:0x200+rva-0x1000+len(value)]=value
 pdata=(spec["table"]+0x100)&~3
 struct.pack_into("<II",data,optional+112+24,pdata,24)
 section=optional+0xf0;data[section:section+8]=b".text\0\0\0"
 struct.pack_into("<IIII",data,section+8,text_size,0x1000,text_size,0x200);struct.pack_into("<I",data,section+36,0x60000020)
 entry=spec["entry"];body=entry+0x57;parent_unwind=pdata+0x40;body_unwind=pdata+0x60
 write(pdata,struct.pack("<IIIIII",entry,body,parent_unwind,body,spec["continue"]+0x40,body_unwind))
 write(parent_unwind,bytes.fromhex("011204001201530003600250"))
 write(body_unwind,bytes([0x21,0,0,0])+struct.pack("<III",entry,body,parent_unwind))
 write(entry,bytes.fromhex("405556488DAC2468FEFFFF4881EC98020000"))
 site=spec["site"];cont=spec["continue"]
 write(site,verify.DISPLACED+bytes.fromhex("83F90A77")+bytes([cont-site-13])+bytes.fromhex("418B8C8E")+struct.pack("<I",spec["table"])+bytes.fromhex("4903CEFFE1"))
 write(spec["table"],struct.pack("<11I",*spec["targets"]))
 args=bytes.fromhex("488D95B8010000488D4DA0")
 for state,mover in enumerate(spec["movers"]):
  if mover is None:continue
  case=spec["targets"][state]
  write(case,args+b"\xe8"+struct.pack("<i",mover-case-16))
  if state!=10:write(case+16,bytes([0xeb,cont-case-18]))
 write(cont,args+b"\xe8"+struct.pack("<i",spec["post"]-cont-16))
 return data

class DispatchTests(unittest.TestCase):
 def image(self,name,data=None):return verify.PEImage(bytes(fixture(name) if data is None else data))
 def mutate(self,data,rva,byte):data[0x200+rva-0x1000]=byte
 def test_paired_actual_table_shape(self):
  for name in verify.SPECS:
   result=verify.verify_dispatch(self.image(name),name,False)
   self.assertEqual(result["original_states_checked"],256)
   self.assertEqual(result["native_rsp_mod16"],0)
   self.assertEqual(result["native_rbp_minus_rsp"],0x100)
 def test_default_hash_gate(self):
  for name in verify.SPECS:
   with self.assertRaises(verify.VerificationError):verify.verify_dispatch(self.image(name),name)
 def test_displaced_byte_drift(self):
  data=fixture("server");self.mutate(data,verify.SPECS["server"]["site"],0x90)
  with self.assertRaises(verify.VerificationError):verify.verify_dispatch(self.image("server",data),"server",False)
 def test_cmp_ja_or_table_address_drift(self):
  for offset in (8,11,17,25):
   data=fixture("client");self.mutate(data,verify.SPECS["client"]["site"]+offset,0x90)
   with self.assertRaises(verify.VerificationError):verify.verify_dispatch(self.image("client",data),"client",False)
 def test_flying_must_not_be_glider(self):
  data=fixture("server");spec=verify.SPECS["server"]
  struct.pack_into("<I",data,0x200+spec["table"]-0x1000+3*4,spec["targets"][4])
  with self.assertRaises(verify.VerificationError):verify.verify_dispatch(self.image("server",data),"server",False)
 def test_glider_edge_drift(self):
  data=fixture("client");spec=verify.SPECS["client"];self.mutate(data,spec["targets"][4]+12,0)
  with self.assertRaises(verify.VerificationError):verify.verify_dispatch(self.image("client",data),"client",False)
 def test_continuation_drift(self):
  data=fixture("server");self.mutate(data,verify.SPECS["server"]["continue"]+11,0x90)
  with self.assertRaises(verify.VerificationError):verify.verify_dispatch(self.image("server",data),"server",False)
 def test_frame_prolog_drift(self):
  data=fixture("client");self.mutate(data,verify.SPECS["client"]["entry"]+13,0)
  with self.assertRaises(verify.VerificationError):verify.verify_dispatch(self.image("client",data),"client",False)
 def test_parent_unwind_stack_drift(self):
  data=fixture("server");image=self.image("server",data);entry=image.function_at(verify.SPECS["server"]["entry"])
  self.mutate(data,entry.unwind+6,0x54)
  with self.assertRaises(verify.VerificationError):verify.verify_dispatch(self.image("server",data),"server",False)
 def test_dispatch_chain_drift(self):
  data=fixture("client");image=self.image("client",data);entry=image.function_at(verify.SPECS["client"]["site"])
  self.mutate(data,entry.unwind+4,0)
  with self.assertRaises(verify.VerificationError):verify.verify_dispatch(self.image("client",data),"client",False)
 def test_truncated_unwind_operand(self):
  data=fixture("server");image=self.image("server",data);entry=image.function_at(verify.SPECS["server"]["entry"])
  self.mutate(data,entry.unwind+2,1)
  with self.assertRaises(verify.VerificationError):verify.unwind(self.image("server",data),entry.unwind)
 def test_bridge_missing_frame_rejected(self):
  with self.assertRaises(verify.VerificationError):verify.verify_bridge(self.image("server"))
if __name__=="__main__":unittest.main()
