#include "creative_flight_local.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace xhl::creative_flight;
namespace {
int checks=0;
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
struct Fixture {
    static constexpr std::uintptr_t world=0x1000,records=0x10000,bitmap=0x30000,keys=0x40000,indices=0x50000,name=0x60000,data=0x70000;
    std::map<std::uintptr_t,std::vector<unsigned char>> memory;
    unsigned reads=0;bool fail=false;unsigned mutation=0;bool mutated=false;
    template<class T> void set(std::uintptr_t address,T value) {
        for(auto& [at,bytes]:memory)if(address>=at && address-at<=bytes.size() && sizeof(T)<=bytes.size()-(address-at)){
            std::memcpy(bytes.data()+address-at,&value,sizeof(value));return;}
        throw std::runtime_error("fixture write outside memory");
    }
    Fixture(){
        memory[world+client_globals_offset].resize(0x80);memory[records].resize(512*0xc0);
        memory[bitmap].resize(512);memory[keys].resize(4096*4);memory[indices].resize(4096*2);
        memory[name].resize(15);std::memcpy(memory[name].data(),"LocalPlayerData",15);memory[data].resize(8);
        const auto at=world+client_globals_offset;
        set(at,records);set(at+8,std::uint64_t(1));set(at+0x10,std::uint64_t(512));set(at+0x30,bitmap);
        set(at+0x38,std::uint64_t(64));set(at+0x50,keys);set(at+0x68,indices);set(at+0x7c,std::uint32_t(1));
        set(records,name);set(records+8,std::uint64_t(15));set(records+0xa0,local_player_data_hash);
        set(records+0xa8,data);set(records+0xb0,data);set(data+4,std::uint32_t(7));
        const auto slot=local_player_data_hash&63;set(bitmap,1ull<<slot);set(keys+slot*4,local_player_data_hash);set(indices+slot*2,std::uint16_t(0));
    }
    bool read(std::uintptr_t address,void* out,std::size_t size){
        ++reads;if(fail)return false;
        for(const auto& [at,bytes]:memory)if(address>=at && address-at<=bytes.size() && size<=bytes.size()-(address-at)){
            std::memcpy(out,bytes.data()+address-at,size);
            if(address==data+4 && mutation && !mutated){mutated=true;
                if(mutation==1)set(data+4,std::uint32_t(9));
                if(mutation==2)set(world+client_globals_offset+8,std::uint64_t(2));
                if(mutation==3)set(records+0xb0,data+1);}
            return true;}
        return false;
    }
    std::optional<std::uint32_t> run(std::uintptr_t at=world){return read_pinned_client_local_actor([&](auto address,auto out,auto size){return read(address,out,size);},at);}
};
}
int main(){try {
    check(local_player_data_hash==0x9117771d,"pinned FNV value");
    {Fixture f;check(f.run()==7u,"exact named current-world local actor");check(f.reads<40,"successful read remains small and bounded");}
    {Fixture f;check(!f.run(0) && !f.reads,"null world rejected before read");}
    {Fixture f;check(!f.run(UINTPTR_MAX-0x100) && !f.reads,"world overflow rejected");}
    {Fixture f;f.fail=true;check(!f.run(),"failed memory read unavailable");}
    for(auto count:{std::uint64_t(0),std::uint64_t(513)}){Fixture f;f.set(f.world+client_globals_offset+8,count);check(!f.run(),"record count bounds");}
    for(auto capacity:{std::uint64_t(0),std::uint64_t(65),std::uint64_t(8192)}){Fixture f;f.set(f.world+client_globals_offset+0x38,capacity);check(!f.run(),"hash table power-of-two bounds");}
    {Fixture f;f.set(f.world+client_globals_offset+0x10,std::uint64_t(0));check(!f.run(),"record capacity smaller than count");}
    {Fixture f;f.set(f.world+client_globals_offset+0x7c,std::uint32_t(0));check(!f.run(),"registration in progress unavailable");}
    {Fixture f;f.set(f.bitmap,std::uint64_t(0));check(!f.run(),"empty hash bucket unavailable");}
    {Fixture f;f.set(f.indices+(local_player_data_hash&63)*2,std::uint16_t(1));check(!f.run(),"record index outside current count");}
    {Fixture f;f.set(f.records+8,std::uint64_t(14));check(!f.run(),"name length must match");}
    {Fixture f;f.memory[f.name][0]='X';check(!f.run(),"hash collision cannot impersonate LocalPlayerData");}
    {Fixture f;f.set(f.records+0xa0,std::uint32_t(0));check(!f.run(),"record hash agrees with index");}
    {Fixture f;f.set(f.records+0xa8,std::uintptr_t(0));check(!f.run(),"missing data pointer unavailable");}
    {Fixture f;f.set(f.records+0xb0,f.data+1);check(!f.run(),"unproven alternate data pointer unavailable");}
    {Fixture f;f.set(f.data+4,std::uint32_t(0));check(!f.run(),"no local actor unavailable");}
    for(unsigned mutation=1;mutation<=3;++mutation){Fixture f;f.mutation=mutation;check(!f.run(),"changing actor/table/record rejects stale evidence");}
    {Fixture f;const auto slot=local_player_data_hash&63;const auto next=(slot+1)&63;
        f.set(f.world+client_globals_offset+8,std::uint64_t(2));f.set(f.world+client_globals_offset+0x7c,std::uint32_t(2));
        f.set(f.bitmap,(1ull<<slot)|(1ull<<next));f.set(f.keys+slot*4,std::uint32_t(0x1234));
        f.set(f.keys+next*4,local_player_data_hash);f.set(f.indices+next*2,std::uint16_t(0));check(f.run()==7u,"bounded open-addressing collision resolves exact record");}
    {Fixture f;f.set(f.bitmap,UINT64_MAX);for(unsigned slot=0;slot<64;++slot)f.set(f.keys+slot*4,std::uint32_t(1));
        check(!f.run() && f.reads<=140,"collision chain cannot loop without bound");}
    {Fixture f;auto first=f.run();f.set(f.data+4,std::uint32_t(11));check(first==7u && f.run()==11u,"every invocation reads current local actor without borrowed cache");}
    {Fixture f;f.set(f.records+0xa8,UINTPTR_MAX-2);f.set(f.records+0xb0,UINTPTR_MAX-2);check(!f.run(),"local actor pointer overflow rejected");}
    std::cout<<checks<<" pinned client local actor checks passed\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}