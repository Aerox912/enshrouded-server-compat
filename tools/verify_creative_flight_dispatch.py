"""Read-only G-DISPATCH7 proof for paired pinned dispatchers and built CALL stub.

Usage: python verify_creative_flight_dispatch.py --server SERVER.exe
 --client CLIENT.exe [--bridge-pe isolated-bridge-tests.exe] [--json evidence.json]
 [--freeze-package]
Never modifies a game image or installs hooks. Hashes fail closed. Dynamic phase
order, gravity/landing and paired client prediction remain gameplay gates.
"""
from __future__ import annotations
import argparse,hashlib,json,struct
from pathlib import Path
from verify_creative_flight_offsets import PEImage,VerificationError,SERVER_BUILD,CLIENT_BUILD

DISPLACED=bytes.fromhex("488B45380FB6483D")
SPECS={
 "server":{"build":SERVER_BUILD,"entry":0x187120,"site":0x187595,"table":0x18775c,"continue":0x187619,
           "targets":[0x1875af,0x1875c1,0x1875d3,0x187619,0x1875e5,0x187619,0x187619,0x187619,0x187619,0x1875f7,0x187609],
           "movers":[0x180f60,0x1801a0,0x17e720,None,0x17f5a0,None,None,None,None,0x180820,0x17e480],"post":0x17ea40},
 "client":{"build":CLIENT_BUILD,"entry":0x3a5c30,"site":0x3a60a5,"table":0x3a626c,"continue":0x3a6129,
           "targets":[0x3a60bf,0x3a60d1,0x3a60e3,0x3a6129,0x3a60f5,0x3a6129,0x3a6129,0x3a6129,0x3a6129,0x3a6107,0x3a6119],
           "movers":[0x39fc70,0x39eeb0,0x39d430,None,0x39e2b0,None,None,None,None,0x39f530,0x39d190],"post":0x39d750}
}
def require(value,label):
 if not value:raise VerificationError(label)
def raw(image,rva,n):
 o=image.rva_to_offset(rva,n);return image.data[o:o+n]
def unwind(image,rva):
 head=raw(image,rva,4);version=head[0]&7;flags=head[0]>>3
 require(version==1,"unsupported unwind version")
 n=head[2];codes=raw(image,rva+4,n*2);index=0;ops=[]
 while index<n:
  offset,encoded=codes[index*2:index*2+2];op=encoded&15;info=encoded>>4;index+=1
  result={"offset":offset,"op":op,"info":info}
  if op in (1,4,8):
   words=2 if op==1 and info==1 else 1
   require(index+words<=n,"truncated unwind operand")
   value=int.from_bytes(codes[index*2:(index+words)*2],"little");index+=words
   if op==1:result["size"]=value*(8 if info==0 else 1)
   if op==4:result["save"]=value*8
   if op==8:result["save"]=value*16
  elif op==2:result["size"]=info*8+8
  elif op not in (0,3):raise VerificationError("unsupported unwind operation")
  ops.append(result)
 chain=None
 if flags&4:chain=struct.unpack("<III",raw(image,rva+4+((n+1)//2)*4,12))
 return {"prolog":head[1],"flags":flags,"frame_register":head[3]&15,"frame_offset":(head[3]>>4)*16,"ops":ops,"chain":chain}
def verify_dispatch(image,name,check_hash=True):
 spec=SPECS[name];build=spec["build"]
 if check_hash:
  for key,value in build.items():require(getattr(image,key)==value,f"{name} pinned {key} mismatch")
 entry,site,cont=spec["entry"],spec["site"],spec["continue"]
 prolog=bytes.fromhex("405556488DAC2468FEFFFF4881EC98020000")
 require(image.code_bytes(entry,len(prolog))==prolog,f"{name} native frame prolog drift")
 body=image.function_at(site);parent=image.function_at(entry)
 u=unwind(image,body.unwind);p=unwind(image,parent.unwind)
 require(u["chain"]==(parent.begin,parent.end,parent.unwind),f"{name} dispatch unwind is not chained to verified entry")
 require(p["ops"]==[{"offset":18,"op":1,"info":0,"size":0x298},{"offset":3,"op":0,"info":6},{"offset":2,"op":0,"info":5}],f"{name} parent stack unwind drift")
 # Initial RSP mod16=8. Two pushes +0x298 allocation => native RSP mod16=0.
 # RBP established before allocation at pushed RSP-0x198 => native RSP+0x100.
 dispatch=DISPLACED+bytes.fromhex("83F90A77")+bytes([cont-(site+13)])+bytes.fromhex("418B8C8E")+struct.pack("<I",spec["table"])+bytes.fromhex("4903CEFFE1")
 require(image.code_bytes(site,26)==dispatch,f"{name} full CMP/JA/table bytes drift")
 targets=list(struct.unpack("<11I",raw(image,spec["table"],44)))
 require(targets==spec["targets"],f"{name} actual dispatch table changed")
 require(targets[3]==cont and targets[4]!=cont,f"{name} Flying/glider table semantics changed")
 call_edges=[]
 args=bytes.fromhex("488D95B8010000488D4DA0")
 for state,mover in enumerate(spec["movers"]):
  if mover is None:continue
  case=targets[state]
  expected=args+b"\xe8"+struct.pack("<i",mover-(case+16))
  require(image.code_bytes(case,16)==expected,f"{name} state{state} real mover edge changed")
  if state!=10:require(image.code_bytes(case+16,2)==bytes([0xeb,cont-(case+18)]),f"{name} state{state} continuation changed")
  else:require(case+16==cont,f"{name} Dive native fall-through changed")
  call_edges.append({"state":state,"case":hex(case),"mover":hex(mover)})
 require(image.code_bytes(cont,16)==args+b"\xe8"+struct.pack("<i",spec["post"]-(cont+16)),f"{name} post-dispatch continuation drift")
 # Mechanically walk all original uint8 states, including >10 JA.
 semantics=[targets[s] if s<=10 else cont for s in range(256)]
 require(all(semantics[s]==cont for s in [3,5,6,7,8,255]),"unexpected direct-continuation state")
 return {"image":image.manifest(),"site":hex(site),"displaced":DISPLACED.hex(),"native_rsp_mod16":0,"native_rbp_minus_rsp":0x100,
         "row_from_rbp":-0x60,"second_from_rbp":0x1b8,"parent_unwind":p,"dispatch_unwind":u,
         "targets":[hex(v) for v in targets],"call_edges":call_edges,"original_states_checked":len(semantics)}
def verify_adapter_anchors(image):
 anchors={
  "world_registry_address":(0x59a3f2,"498D9628090000"),
  "world_execution_registry":(0x59a496,"4989961842CC00"),
  "query_row_context":(0x5dc8dc,"48891F"),
  "mover_query_begin_row":(0x18714b,"41B838010000488D55A0488BCEE893564500"),
  "mover_query_step_row":(0x18715d,"41B838010000488D55A0488BCEE8F1074500"),
  "query_original_time":(0x5d98b0,"488B01488B4858488BC248890AC3"),
  "gravity_query_begin":(0x7e7e9,"41B830000000488D542430488BD9E8F4DF5500"),
  "gravity_query_step":(0x7e7fc,"41B830000000488D542430488BCBE851915500"),
  "gravity_private_active_gate":(0x7e850,"488B442440807810000F84FC000000"),
  "gravity_actor_row":(0x7e85f,"488B4C243833D2E875B9FAFF"),
  "actor_prediction_state":(0x2a1ee,"41F680B101000001498B80F80B00007414498B88D80B0000490B80D00B000048F7D14823C1")
 }
 result={}
 for name,(rva,signature) in anchors.items():
  expected=bytes.fromhex(signature)
  require(image.code_bytes(rva,len(expected))==expected,f"server {name} anchor drift")
  result[name]={"rva":hex(rva),"bytes":signature}
 require(struct.unpack("<I",raw(image,0x11a0370+0x40,4))[0]==0x14,"Gravity size metadata drift")
 result["gravity_size"]={"descriptor":"0x11a0370","size":0x14}
 return result
def verify_bridge(image):
 prefix=bytes.fromhex("559C4881ECA8010000")
 found=[f for f in image.runtime_functions if image.code_bytes(f.begin,min(9,f.end-f.begin))==prefix]
 require(len(found)==1,"expected unique assembled CALL bridge FRAME")
 f=found[0];u=unwind(image,f.unwind)
 require(u["flags"]==0 and u["frame_register"]==5 and u["frame_offset"]==0 and u["chain"] is None,"bridge needs standalone RBP FRAME with no handler/chain")
 require(sum(o["op"]==3 for o in u["ops"])==1,"bridge lacks SET_FPREG unwind code")
 allocations=sum(o.get("size",0)+(8 if o["op"]==0 else 0) for o in u["ops"])
 require(allocations==0x1b8,"bridge stack unwind size mismatch")
 gpr={o["info"]:o["save"] for o in u["ops"] if o["op"]==4}
 xmm={o["info"]:o["save"] for o in u["ops"] if o["op"]==8}
 require(gpr=={3:0x38,6:0x40,7:0x48,12:0x70,13:0x78,14:0x80,15:0x88},"bridge nonvolatile GPR unwind mismatch")
 require(xmm=={r:0x100+(r-6)*16 for r in range(6,16)},"bridge nonvolatile XMM unwind mismatch")
 require([o["info"] for o in u["ops"] if o["op"]==0]==[5],"original RBP not restored from real push")
 body=image.code_bytes(f.begin,f.end-f.begin)
 require(body[-9:]==bytes.fromhex("488DA5B00100005DC3"),"bridge lacks recognized Win64 frame epilog")
 require(bytes.fromhex("488B85B0010000488B40380FB6483D") in body,"bridge does not replay displaced MOVs against original RBP")
 require(bytes.fromhex("FFB5A80100009D") in body,"bridge lacks permanent saved-flags restore")
 return {"image":image.manifest(),"function":[hex(f.begin),hex(f.end)],"unwind":u,
         "stack_bytes_to_real_return_pc":hex(allocations),"body_sha256":hashlib.sha256(body).hexdigest().upper()}
def freeze_package():
 root=Path(__file__).resolve().parents[1]
 files=("src/creative_flight_server_runtime.cpp","src/creative_flight_server_runtime.hpp",
        "src/creative_flight_dispatch_bridge.hpp","src/creative_flight_dispatch_bridge.asm",
        "tests/creative_flight_server_runtime_tests.cpp","tests/creative_flight_dispatch_bridge_tests.cpp",
        "tests/creative_flight_dispatch_fixture.asm","tools/test_creative_flight_server_runtime.py",
        "tools/verify_creative_flight_dispatch.py","tests/test_creative_flight_dispatch.py",
        "research/creative-flight-server-runtime-abi-20261009.md",
        "research/creative-flight-server-runtime-dispatch-correction-20261009.md")
 return {"task_id":"G-DISPATCH7","root":str(root),
         "files_sha256":{name:hashlib.sha256((root/name).read_bytes()).hexdigest().upper() for name in files},
         "evidence_self_hash":"Recorded externally after JSON serialization; excluded to avoid self-reference."}

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument("--server",required=True,type=Path);parser.add_argument("--client",required=True,type=Path)
 parser.add_argument("--bridge-pe",type=Path);parser.add_argument("--json",type=Path)
 parser.add_argument("--freeze-package",action="store_true",help="include exact bounded source/document hashes, excluding this output JSON")
 args=parser.parse_args()
 result={name:verify_dispatch(PEImage.read(path),name) for name,path in [("server",args.server),("client",args.client)]}
 result["server"]["adapter_anchors"]=verify_adapter_anchors(PEImage.read(args.server))
 if args.bridge_pe:result["assembled_bridge"]=verify_bridge(PEImage.read(args.bridge_pe))
 if args.freeze_package:result["package_freeze"]=freeze_package()
 if args.json:args.json.write_text(json.dumps(result,indent=2)+"\n")
 print("Paired pinned dispatch tables, frame/branches/mover edges"+(" and emitted CALL bridge unwind" if args.bridge_pe else "")+" verified; static-only, no installation.")
if __name__=="__main__":main()
