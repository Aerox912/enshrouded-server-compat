#include "creative_flight_probe.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace xhl::creative_flight;
namespace {
int checks=0;
void check(bool condition,const char* label){++checks;if(!condition)throw std::runtime_error(label);}
struct Fixture {
    xhl::flight::Identity id{1,2,3,64,128,64,0x0110000100000001,1,1};
    std::array<unsigned char,0xc00> actor{};
    std::array<unsigned char,0x528> player{};
    bool approved=true,fail=false,change_after_read=false;
    unsigned authorizations=0,frames=0,reads=0,emissions=0;
    InputSample sample{};
    Fixture(){set(player,0x320,(1ull<<30)|(1ull<<50));set(actor,0xbf8,1ull);set(player,0x519,std::uint8_t(2));}
    template<class T,std::size_t N> static void set(std::array<unsigned char,N>& data,std::size_t offset,T value){std::memcpy(data.data()+offset,&value,sizeof(value));}
    std::optional<xhl::flight::Identity> authorize(){++authorizations;return approved?std::optional(id):std::nullopt;}
    ProbeFrame frame(){++frames;return {id,reinterpret_cast<std::uintptr_t>(actor.data()),reinterpret_cast<std::uintptr_t>(player.data())};}
    bool read(std::uintptr_t address,void* output,std::size_t size){++reads;if(fail)return false;std::memcpy(output,reinterpret_cast<void*>(address),size);
        if(change_after_read && reads==3)++id.lifecycle;return true;}
    bool run(InputProbe& probe,std::uint64_t now,std::uint32_t owner=1){return probe.observe(now,owner,[&]{return authorize();},[&]{return frame();},
        [&](auto address,auto out,auto size){return read(address,out,size);},[&](const auto& value){++emissions;sample=value;});}
};
ProbeConfig config(){return parse_probe_config("enabled=1;owner=1");}
}
int main(){try {
    check(!parse_probe_config("").enabled,"default off");
    check(config().enabled,"explicit enabled");
    for(const auto bad:{"owner=1","enabled=1;owner=0","enabled=1;owner=17","enabled=1;owner=1;owner=2",
        "enabled=1;owner=1;duration_ms=20001","enabled=1;owner=1;sample_limit=201","enabled=1;owner=1;interval_ms=99",
        "enabled=1;owner=1;unknown=1","enabled=1;owner=1;","enabled=1;owner=1;duration_ms=0",
        "enabled=1;owner=1;sample_limit=0","enabled=1;owner=1;interval_ms=0","enabled=1;owner=1;enabled=1"})
        check(!parse_probe_config(bad).enabled,"strict config fail closed");
    {Fixture f;InputProbe p;check(!f.run(p,1000) && !f.frames && !f.authorizations && !f.reads,"disabled has zero native work");}
    {Fixture f;InputProbe p(config());check(!f.run(p,1000,2) && !f.authorizations && !f.frames,"only selected local owner");}
    {Fixture f;f.approved=false;InputProbe p(config());check(!f.run(p,1000) && !f.frames && !f.reads,"unauthorized no component resolve");}
    {Fixture f;f.approved=false;InputProbe p(config());check(!f.run(p,1000) && !f.run(p,50000) && p.attempts()==0,"approval delay does not consume capture window");
        f.approved=true;check(f.run(p,60000) && f.run(p,60100),"capture starts with first approved identity");
        f.approved=false;check(!f.run(p,60101) && !p.enabled() && f.emissions==2,"revocation terminates even inside rate interval");
        f.approved=true;check(!f.run(p,60200) && f.emissions==2,"revoked probe cannot resume");}
    {Fixture f;f.id.authentication=0;InputProbe p(config());check(!f.run(p,1000) && !f.frames,"invalid identity rejected");}
    {Fixture f;InputProbe p(config());check(f.run(p,1000),"authorized sample");
        check(f.sample.jump && f.sample.sprint && !f.sample.sneak && f.sample.grounded && !f.sample.flying && f.sample.input_mode==2,"named actual input bits separate from states");
        check(f.authorizations==2 && f.reads==3,"before and after authorization");
        check(!f.run(p,1099) && f.emissions==1 && f.frames==1,"rate limit avoids native work");
        Fixture::set(f.player,0x320,1ull<<52);Fixture::set(f.actor,0xbf8,1ull<<39);
        check(f.run(p,1100) && !f.sample.jump && f.sample.sneak && !f.sample.sprint && f.sample.flying,"sneak versus flying independent");
        ++f.id.lifecycle;check(!f.run(p,1200) && !p.enabled() && f.emissions==2,"new life terminates probe");}
    {Fixture f;f.change_after_read=true;InputProbe p(config());check(!f.run(p,1000) && !p.enabled() && !f.emissions,"delayed sample identity changes rejected");}
    {Fixture f;InputProbe p(config());unsigned calls=0;
        check(!p.observe(1000,1,[&]()->std::optional<xhl::flight::Identity>{++calls;return calls==1?std::optional(f.id):std::nullopt;},
            [&]{return f.frame();},[&](auto address,auto out,auto size){return f.read(address,out,size);},[&](const auto&){++f.emissions;}) && !f.emissions && !p.enabled(),"approval lost before emit");}
    {Fixture f;InputProbe p(config());f.fail=true;check(!f.run(p,1000) && !f.emissions && p.attempts()==1,"failed read consumes budget");}
    {InputSample sample;char line[256];format_sample(sample,line);
        check(std::string_view(line).find("move=")==std::string_view::npos,"unproven movement is omitted, never zero disguised as measurement");}
    {Fixture f;InputProbe p(parse_probe_config("enabled=1;owner=1;sample_limit=2"));check(f.run(p,1000) && f.run(p,1100) && !f.run(p,1200) && !p.enabled() && f.emissions==2,"sample cap");}
    {Fixture f;InputProbe p(config());check(f.run(p,1000) && !f.run(p,21000) && !p.enabled(),"20 second exact deadline");}
    {Fixture f;InputProbe p(config());check(f.run(p,1000) && !f.run(p,999) && !p.enabled(),"clock rollback terminates");}
    {Fixture f;InputProbe p(config());auto other=f.id;++other.lifecycle;
        check(!p.observe(1000,1,[&]{return std::optional(f.id);},[&]{auto frame=f.frame();frame.identity=other;return frame;},
            [&](auto address,auto out,auto size){return f.read(address,out,size);},[&](const auto&){++f.emissions;}) && !f.reads && !f.emissions,"component frame bound to captured life");}
    {Fixture f;InputProbe p(config());check(!p.observe(1000,1,[&]{return std::optional(f.id);},[&]{auto frame=f.frame();frame.actor=0;return frame;},
            [&](auto address,auto out,auto size){return f.read(address,out,size);},[&](const auto&){++f.emissions;}) && !f.reads,"missing actor has no reads");}
    {Fixture f;InputProbe p(config());check(!p.observe(1000,1,[&]{return std::optional(f.id);},[&]{auto frame=f.frame();frame.player_input=UINTPTR_MAX-0x100;return frame;},
            [&](auto address,auto out,auto size){return f.read(address,out,size);},[&](const auto&){++f.emissions;}) && !f.reads,"address overflow rejected before reads");}
    {Fixture f;InputProbe p(config());check(f.run(p,1000),"captured initial world");++f.id.world;
        check(!f.run(p,1100) && !p.enabled() && f.frames==1,"full world identity change stops before components");}
    {InputSample s;s.jump=true;char line[256];format_sample(s,line);check(std::string_view(line).find("Jump=1 Sneak=0 Sprint=0")!=std::string_view::npos,"bounded labels without identities");}
    std::cout<<checks<<" G input observer checks passed\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
