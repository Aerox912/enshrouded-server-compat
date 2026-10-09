#include "creative_client_life.hpp"
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace xhl::creative_flight;
namespace {
int checks=0;
void check(bool yes,const char* label){++checks;if(!yes)throw std::runtime_error(label);}
struct Fixture {
 static constexpr std::uintptr_t base=0x140000000,client=0x10000000,session=0x20000000,scene=0x30000000,world=0x40000000,simulation=0x50000000;
 static constexpr std::uintptr_t records=0x60000000,global_records=0x61000000,global_bitmap=0x62000000,global_keys=0x63000000,global_indices=0x64000000,name=0x65000000,local_data=0x66000000;
 static constexpr std::uintptr_t bitmap=0x70000000,keys=0x71000000,values=0x72000000,record=0x73000000,layout=0x74000000,data=0x75000000;
 static constexpr std::uint16_t index=2,group=1,offset=0x20;
 static constexpr std::uint32_t entity=77,stride=0x2000;
 static constexpr std::uintptr_t actor=data+stride+offset;
 std::map<std::uintptr_t,std::vector<unsigned char>> memory;
 std::function<void(Fixture&,std::uintptr_t)> mutate;
 unsigned reads=0;std::uintptr_t fail_at=0;bool changed=false,fail=false;
 void region(std::uintptr_t at,std::size_t size){memory[at].resize(size);}
 template<class T>void put(std::uintptr_t at,T value){for(auto& [start,bytes]:memory)if(at>=start && at-start<=bytes.size() && sizeof(T)<=bytes.size()-(at-start)){std::memcpy(bytes.data()+at-start,&value,sizeof(value));return;}throw std::runtime_error("fixture write");}
 Fixture(){
  region(base+client_life_detail::game_rva+0x250,8);put(base+client_life_detail::game_rva+0x250,client);
  region(client+0x52888,8);put(client+0x52888,session);region(session,0x188);put(session+0x20,std::uint8_t(1));put(session+0x180,scene);
  region(scene+0x1c0,8);put(scene+0x1c0,world);region(scene+0x3439c0,8);put(scene+0x3439c0,simulation);region(simulation,16);put(simulation+8,world);
  region(world+0x930,16);put(world+0x930,records);put(world+0x938,std::uint64_t(3));region(world+0xcc4218,0x1a0);put(world+0xcc4218,world+0x928);
  region(records,1280*0x100);put(records+index*0x100+0x28,base+client_life_detail::actor_type_rva);
  region(world+client_globals_offset,0x80);region(global_records,0xc0);region(global_bitmap,8);region(global_keys,64*4);region(global_indices,64*2);region(name,15);region(local_data,8);
  const auto globals=world+client_globals_offset;put(globals,global_records);put(globals+8,std::uint64_t(1));put(globals+0x10,std::uint64_t(1));put(globals+0x30,global_bitmap);put(globals+0x38,std::uint64_t(64));put(globals+0x50,global_keys);put(globals+0x68,global_indices);put(globals+0x7c,std::uint32_t(1));
  std::memcpy(memory[name].data(),"LocalPlayerData",15);put(global_records,name);put(global_records+8,std::uint64_t(15));put(global_records+0xa0,local_player_data_hash);put(global_records+0xa8,local_data);put(global_records+0xb0,local_data);put(local_data+4,entity);
  auto global_slot=local_player_data_hash&63;put(global_bitmap,1ull<<global_slot);put(global_keys+global_slot*4,local_player_data_hash);
  region(bitmap,8);region(keys,64*4);region(values,64*8);region(record,0x38);region(layout,0x1500);region(data,0x8000);
  const auto map=world+0xcc4360;put(map+8,bitmap);put(map+0x10,std::uint64_t(64));put(map+0x28,keys);put(map+0x40,values);put(map+0x54,std::uint32_t(1));
  auto slot=client_life_detail::entity_hash(entity)&63;put(bitmap,1ull<<slot);put(keys+slot*4,entity);put(values+slot*8,record);
  put(record+0x18,layout);put(record+0x20,data);put(record+0x30,stride);put(layout,1ull<<index);put(layout+0xa84+index*2,group);put(layout+0x84+index*2,offset);put(actor+0xbf8,std::uint64_t(1));
 }
 bool read(std::uintptr_t at,void* out,std::size_t size){++reads;if(fail || at==fail_at)return false;for(const auto& [start,bytes]:memory)if(at>=start && at-start<=bytes.size() && size<=bytes.size()-(at-start)){std::memcpy(out,bytes.data()+at-start,size);if(mutate && !changed)mutate(*this,at);return true;}return false;}
 auto run(bool pinned=true,std::uintptr_t image=base,LocalLifeReadDiagnostic* diagnostic=nullptr){return read_pinned_client_life([&](auto at,auto out,auto size){return read(at,out,size);},image,pinned,diagnostic);}
};
}
int main(){try {
 {Fixture f;auto s=f.run();check(s && s->alive(),"current owner alive");check(s->identity.world==f.world && s->identity.entity==f.entity && s->identity.actor_storage==f.actor,"copied native identity");check(f.reads<180,"small normal read");}
 {
  Fixture plain;auto expected=plain.run();const auto plain_reads=plain.reads;
  Fixture observed;LocalLifeReadDiagnostic diagnostic;auto actual=observed.run(true,Fixture::base,&diagnostic);
  check(expected && actual,"diagnostic does not change valid read result");
  check(expected->identity==actual->identity && expected->current_state==actual->current_state
      && expected->effective_state==actual->effective_state,"diagnostic preserves copied life snapshot");
  check(plain_reads==observed.reads && diagnostic.read_attempts==observed.reads
      && diagnostic.read_attempts<180 && diagnostic.stage==LocalLifeReadStage::none,
      "diagnostic preserves valid read attempt count");
 }
 {
  const auto expect_stage=[](Fixture& fixture,LocalLifeReadStage expected,const char* expected_name,bool pinned=true) {
   LocalLifeReadDiagnostic diagnostic;
   check(!fixture.run(pinned,Fixture::base,&diagnostic),"diagnostic stage still denies unavailable life");
   check(diagnostic.stage==expected,"reader reports the expected failure stage");
   check(diagnostic.read_attempts<=4097,"diagnostic read count remains capped");
   const auto* name=local_life_read_stage_name(diagnostic.stage);
   check(name && std::strlen(name)>0 && std::strlen(name)<=32
       && std::strcmp(name,"unknown")!=0,"failure stage has a bounded name");
   check(std::strcmp(name,expected_name)==0,"failure stage name matches its enum");
   check(!fixture.run(pinned),"diagnostic leaves the original fail-closed result unchanged");
  };
  {Fixture f;expect_stage(f,LocalLifeReadStage::full_hash_guard,"full-hash-guard",false);}
  {Fixture f;f.put(Fixture::session+0x20,std::uint8_t(0));expect_stage(f,LocalLifeReadStage::world_chain,"world-chain");}
  {Fixture f;f.put(Fixture::world+client_globals_offset,std::uintptr_t(0));expect_stage(f,LocalLifeReadStage::owned_actor,"owned-actor");}
  {Fixture f;f.put(Fixture::world+0xcc4218,Fixture::world+0x930);expect_stage(f,LocalLifeReadStage::component_registry,"component-registry");}
  {Fixture f;f.put(Fixture::records+Fixture::index*0x100+0x28,std::uintptr_t(0));expect_stage(f,LocalLifeReadStage::component_type_lookup,"component-type-lookup");}
  {Fixture f;f.put(Fixture::world+0xcc4360+0x10,std::uint64_t(0));expect_stage(f,LocalLifeReadStage::entity_map,"entity-map");}
  {Fixture f;f.put(Fixture::bitmap,std::uint64_t(0));expect_stage(f,LocalLifeReadStage::actor_location,"actor-location");}
  {Fixture f;f.fail_at=Fixture::actor+0x1b1;expect_stage(f,LocalLifeReadStage::life_state_read,"life-state-read");}
  {Fixture f;f.mutate=[](Fixture& v,auto at){if(at==v.actor+0xbf8){v.changed=true;v.put(v.local_data+4,std::uint32_t(78));}};expect_stage(f,LocalLifeReadStage::consistency_reread,"consistency-reread");}
 }
 {Fixture f;check(!f.run(false) && !f.reads,"full pinned hash required before read");check(!f.run(true,0) && !f.reads,"zero module rejected");check(!f.run(true,UINTPTR_MAX-0x100) && !f.reads,"module overflow rejected");}
 {Fixture f;f.fail=true;check(!f.run(),"read failure unavailable");}
 for(auto state:{1ull<<7,1ull<<12,(1ull<<7)|(1ull<<12)}){Fixture f;f.put(f.actor+0xbf8,state);auto s=f.run();check(s && !s->alive(),"known Dead/Spawning is copied and inactive");}
 for(auto state:{1ull<<7,1ull<<12}){Fixture f;f.put(f.actor+0x1b1,std::uint8_t(1));f.put(f.actor+0xbd0,state);auto s=f.run();check(s && s->current_state==1 && !s->alive(),"native predicted death/spawn denies");}
 {Fixture f;f.put(f.actor+0x1b1,std::uint8_t(1));f.put(f.actor+0xbf8,1ull<<7);f.put(f.actor+0xbd8,1ull<<7);auto s=f.run();check(s && s->effective_state==0 && !s->alive(),"prediction cannot clear authoritative dead gate");}
 {Fixture f;f.put(f.actor+0xbd0,1ull<<7);auto s=f.run();check(s && s->alive(),"inactive prediction mask matches native semantics");}
 for(auto field:{Fixture::base+client_life_detail::game_rva+0x250,Fixture::client+0x52888,Fixture::session+0x180,Fixture::scene+0x3439c0,Fixture::scene+0x1c0,Fixture::simulation+8}){Fixture f;f.put(field,std::uintptr_t(0));check(!f.run(),"missing current world chain unavailable");}
 {Fixture f;f.put(f.session+0x20,std::uint8_t(0));check(!f.run(),"inactive session unavailable");}
 {Fixture f;f.put(f.simulation+8,f.world+1);check(!f.run(),"native world crosscheck required");}
 for(auto count:{std::uint64_t(0),std::uint64_t(1281)}){Fixture f;f.put(f.world+0x938,count);check(!f.run(),"registry bounds");}
 {Fixture f;f.put(f.world+0xcc4218,f.world+0x930);check(!f.run(),"registry is current world's own registry");}
 {Fixture f;f.put(f.records+f.index*0x100+0x28,std::uintptr_t(0));check(!f.run(),"missing exact native Actor metadata unavailable");}
 {Fixture f;f.put(f.records+0x28,f.base+client_life_detail::actor_type_rva);check(!f.run(),"duplicate exact Actor registration rejected");}
 {Fixture f;f.put(f.layout,std::uint64_t(0));check(!f.run(),"Actor absent from current entity layout");}
 for(auto capacity:{std::uint64_t(0),std::uint64_t(63),std::uint64_t(2097152)}){Fixture f;f.put(f.world+0xcc4360+0x10,capacity);check(!f.run(),"entity table bounds");}
 {Fixture f;f.put(f.world+0xcc4360+0x54,std::uint32_t(65));check(!f.run(),"inconsistent entity table unavailable");}
 {Fixture f;f.put(f.bitmap,std::uint64_t(0));check(!f.run(),"entity absent");}
 {Fixture f;f.put(f.record+0x30,std::uint32_t(0));check(!f.run(),"invalid component stride");}
 {Fixture f;f.put(f.record+0x20,UINTPTR_MAX-1);check(!f.run(),"Actor address overflow");}
 {Fixture f;f.put(f.bitmap,UINT64_MAX);for(unsigned slot=0;slot<64;++slot)f.put(f.keys+slot*4,std::uint32_t(1));check(!f.run() && f.reads<300,"entity collision chain bounded");}
 {Fixture f;f.put(f.world+0x938,std::uint64_t(1280));check(f.run().has_value() && f.reads<4096,"maximum supported registry remains bounded");}
 // Mutate only after first life read so every source was initially valid.
 for(auto field:{Fixture::base+client_life_detail::game_rva+0x250,Fixture::client+0x52888,Fixture::session+0x180,Fixture::scene+0x3439c0,Fixture::scene+0x1c0,Fixture::simulation+8,Fixture::world+0x930,Fixture::record+0x18,Fixture::record+0x20,Fixture::values+(client_life_detail::entity_hash(Fixture::entity)&63)*8}) {
  Fixture f;f.mutate=[field](Fixture& value,auto at){if(at==value.actor+0xbf8){value.changed=true;value.put(field,std::uintptr_t(0));}};check(!f.run(),"chain/registry/entity/component mutation denies stale copy");
 }
 {Fixture f;f.mutate=[](Fixture& v,auto at){if(at==v.actor+0xbf8){v.changed=true;v.put(v.actor+0xbf8,1ull<<7);}};check(!f.run(),"life mutation denied");}
 {Fixture f;f.mutate=[](Fixture& v,auto at){if(at==v.actor+0xbf8){v.changed=true;v.put(v.local_data+4,std::uint32_t(78));}};check(!f.run(),"local entity mutation denied");}
 for(auto field:{Fixture::actor+0x1b1,Fixture::actor+0xbd0,Fixture::actor+0xbd8}){Fixture f;f.mutate=[field](Fixture& v,auto at){if(at==v.actor+0xbf8){v.changed=true;v.memory[v.data][field-v.data]=1;}};check(!f.run(),"prediction mask mutation denied");}
 {Fixture f;auto first=f.run();f.put(f.layout+0x84+f.index*2,std::uint16_t(0x40));f.put(f.data+f.stride+0x40+0xbf8,std::uint64_t(1));auto second=f.run();check(first && second && first->identity.entity==second->identity.entity && first->identity.actor_storage!=second->identity.actor_storage,"same local entity with replaced Actor storage is distinct");}
 {Fixture f;LocalLifeBinding b;auto alive=f.run();check(b.bind(1,alive) && b.check(alive),"new accepted epoch bound");check(b.bind(1,alive),"same active heartbeat continues");f.put(f.actor+0xbf8,1ull<<7);check(!b.check(f.run()) && !b.token(),"death retires binding");f.put(f.actor+0xbf8,std::uint64_t(1));check(!b.bind(1,f.run()),"old heartbeat cannot rebind after death");check(b.bind(2,f.run()),"fresh accepted epoch rebinds");check(!b.check({}),"unknown evidence retires");check(!b.bind(1,f.run()) && !b.bind(2,f.run()),"all stale accepted epochs rejected");check(b.bind(3,f.run()),"new epoch after unknown");b.clear();check(!b.bind(3,f.run()),"explicit revocation retires");}
 {Fixture f;LocalLifeBinding b;auto first=f.run();check(b.bind(1,first),"replacement binding setup");f.put(f.layout+0x84+f.index*2,std::uint16_t(0x40));f.put(f.data+f.stride+0x40+0xbf8,std::uint64_t(1));check(!b.check(f.run()) && !b.bind(1,f.run()),"reused entity changed storage retires epoch");check(b.bind(2,f.run()),"fresh binding permits replacement");}
 {Fixture f;LocalLifeBinding b;check(!b.bind(4,{}),"unavailable newly accepted token denied");check(!b.bind(4,f.run()),"failed acceptance cannot later bind via same heartbeat");check(b.bind(5,f.run()),"later newly accepted token can bind");}
 std::cout<<checks<<" current client life checks passed\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
