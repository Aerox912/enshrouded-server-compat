#include "creative_flight_server_runtime.hpp"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace xhl::creative_flight {
namespace {
constexpr std::uintptr_t execution_offset=0xcc4218;
constexpr std::array<std::uint32_t,4> row_sizes{0xb0,0x70,0x138,0x30};
constexpr std::array<std::uintptr_t,4> actor_offsets{0x48,0x10,0x68,8};
constexpr std::uint64_t max_command_age_ms=100;
constexpr std::uint64_t unavailable=(1ull<<7)|(1ull<<12);
constexpr std::uint64_t fnv_offset=14695981039346656037ull;
constexpr std::uint64_t fnv_prime=1099511628211ull;
unsigned phase_index(ServerPhase phase){return static_cast<unsigned>(phase);}
void digest_byte(std::uint64_t& digest,std::uint8_t value) noexcept {
    digest^=value;digest*=fnv_prime;
}
void digest_event(std::uint64_t& digest,ServerPhase phase,std::uint64_t order,
        std::uint64_t elapsed) noexcept {
    digest_byte(digest,static_cast<std::uint8_t>(phase));
    for(unsigned shift=0;shift<64;shift+=8)digest_byte(digest,static_cast<std::uint8_t>(order>>shift));
    for(unsigned shift=0;shift<64;shift+=8)digest_byte(digest,static_cast<std::uint8_t>(elapsed>>shift));
}
}
struct ServerGRuntime::Impl {
    struct Slot {
        MovementPolicy policy;
        std::optional<MovementCommand> command;
        std::optional<MovementApproval> approval;
        std::uint64_t time=0;
        bool selected=false,moved=false;
        void retire(){policy.revoke();command.reset();approval.reset();selected=false;moved=false;}
        void land(){policy.state(1);command.reset();selected=false;moved=false;}
    };
    struct Scope {
        Impl& runtime;Scope* previous=nullptr;
        ServerPhase phase;void* query=nullptr;
        std::uintptr_t* row=nullptr;std::uint32_t size=0,owner=0;
        std::uintptr_t world=0,actor=0,definition=0,execution=0;
        unsigned steps=0;
        bool valid=false,patched=false;
        std::uintptr_t gravity_original=0;
        alignas(16) std::array<unsigned char,0x14> gravity{};
        Scope(Impl& r,ServerPhase p,void* q):runtime(r),previous(current),phase(p),query(q){current=this;}
        void restore() noexcept {
            if(patched){row[0x10/8]=gravity_original;patched=false;}
        }
        void reset_row() noexcept {restore();valid=false;row=nullptr;owner=0;world=0;actor=0;}
        ~Scope(){restore();current=previous;}
    };
    static thread_local Scope* current;
    std::recursive_mutex mutex;
    std::condition_variable_any drained;
    bool enabled=false;
    unsigned callbacks=0;
    ServerGProvider provider;ServerGOptions options;
    std::array<Slot,16> slots{};
    struct Observer {
        bool authorized=false,started=false,stopped=false,accepted=false;
        std::optional<MovementApproval> approval_binding;
        std::optional<ServerObservationBinding> observation_binding;
        std::uint64_t observation_id=0,start=0,order=0,digest=fnv_offset,elapsed=0;
        std::uint8_t phase_mask=0;
        unsigned attempts=0;
        std::array<std::uint64_t,4> last{};
        std::array<bool,4> seen{};
        std::vector<ServerPhaseEvent> events;
    } observer;
    std::uint64_t next_observation_id=1;
    void retire_all(){for(auto& slot:slots)slot.retire();}
    bool enter(){std::lock_guard lock(mutex);if(!enabled)return false;++callbacks;return true;}
    void leave(){std::lock_guard lock(mutex);if(!--callbacks)drained.notify_all();}
    bool query_world(void* query,std::uintptr_t& world,std::uintptr_t& definition,std::uintptr_t& execution){
        std::uintptr_t again=0,registry=0;
        const auto address=reinterpret_cast<std::uintptr_t>(query);
        if(!movement_read(provider.read,address,0,definition) ||
           !movement_read(provider.read,definition,0,execution) || execution<=execution_offset)return false;
        world=execution-execution_offset;
        return world<=UINTPTR_MAX-0x928 && movement_read(provider.read,execution,0,registry) && registry==world+0x928 &&
            movement_read(provider.read,address,0,again) && again==definition &&
            movement_read(provider.read,definition,0,again) && again==execution;
    }
    bool capture(Scope& scope,void* output,std::uint32_t size){
        scope.reset_row();
        const auto index=phase_index(scope.phase);
        if(index>=row_sizes.size() || size!=row_sizes[index] || !output ||
           !query_world(scope.query,scope.world,scope.definition,scope.execution))return false;
        provider.entity(scope.query,&scope.owner);
        if(scope.owner<1 || scope.owner>16)return false;
        auto fail=[&]{if(options.movement)slots[scope.owner-1].retire();return false;};
        std::array<std::uintptr_t,0x138/8> copy{};
        if(!provider.read(reinterpret_cast<std::uintptr_t>(output),copy.data(),size) ||
           copy[0]!=reinterpret_cast<std::uintptr_t>(scope.query))return fail();
        scope.actor=copy[actor_offsets[index]/8];
        if(!scope.actor)return fail();
        scope.row=static_cast<std::uintptr_t*>(output);scope.size=size;scope.valid=true;
        return true;
    }
    // Same pinned native Actor state formula as 2A1E0. Conservative raw OR
    // effective state prevents predicted death/spawn/grounding being ignored.
    // No borrowed state; every source field is reread before accepting it.
    bool actor_state(std::uintptr_t actor,std::uint64_t& state){
        std::uint64_t raw=0,again=0,add=0,remove=0,value=0;
        std::uint8_t flags=0,flags_after=0;
        if(!movement_read(provider.read,actor,0xbf8,raw) ||
           !movement_read(provider.read,actor,0x1b1,flags))return false;
        if(flags&1){
            if(!movement_read(provider.read,actor,0xbd0,add) ||
               !movement_read(provider.read,actor,0xbd8,remove) ||
               !movement_read(provider.read,actor,0xbd0,value) || value!=add ||
               !movement_read(provider.read,actor,0xbd8,value) || value!=remove)return false;
        }
        if(!movement_read(provider.read,actor,0x1b1,flags_after) || flags_after!=flags ||
           !movement_read(provider.read,actor,0xbf8,again) || again!=raw)return false;
        state=raw | ((raw|add)&~remove);return true;
    }
    std::optional<MovementApproval> approval(Scope& scope){
        if(!enabled || !scope.valid)return {};
        std::uintptr_t world=0,definition=0,execution=0;
        if(!query_world(scope.query,world,definition,execution) || world!=scope.world ||
           definition!=scope.definition || execution!=scope.execution)return {};
        std::uint32_t owner=0;provider.entity(scope.query,&owner);
        std::uintptr_t actor=0;
        if(owner!=scope.owner || !movement_read(provider.read,reinterpret_cast<std::uintptr_t>(scope.row),
            actor_offsets[phase_index(scope.phase)],actor) || actor!=scope.actor)return {};
        auto value=provider.approve(world,owner,actor);
        if(!value || !value->valid() || value->identity.world!=world || value->actor!=actor ||
           (value->identity.player&63)+1!=owner)return {};
        std::uint64_t current_state=0;
        if(!actor_state(actor,current_state) || (current_state&unavailable))return {};
        return value;
    }
    // Grounding ends only this airborne run. Helpers revoke on a denied read,
    // so run them against a candidate policy and retain the armed binding when
    // this gate observes landing. Death/revocation still retire the generation.
    std::optional<MovementApproval> airborne_approval(Scope& scope,bool& landed){
        auto value=approval(scope);std::uint64_t state=0;
        if(!value || !actor_state(scope.actor,state) || (state&unavailable))return {};
        if(state&1){landed=true;return {};}
        return value;
    }
    void observe(Scope& scope){
        if(!options.observe_phases || !observer.authorized || observer.stopped
            || observer.accepted || !scope.valid)return;
        if(phase_index(scope.phase)>=4)return;
        const auto now=provider.now_ms();
        if(!now){if(observer.started)observer.stopped=true;return;}
        std::optional<ServerObservationBinding> observed;
        std::optional<MovementApproval> approved;
        if(provider.observe_binding){
            observed=provider.observe_binding(scope.world,scope.owner,scope.actor);
            if(!observed||!observed->valid()||observed->world!=scope.world
                ||observed->owner!=scope.owner||observed->actor!=scope.actor){
                if(observer.started)observer.stopped=true;
                return;
            }
            if(observer.observation_binding&&*observer.observation_binding!=*observed){
                observer.stopped=true;return;
            }
        }else{
            // Compatibility for the earlier approval-bound fixture path. The
            // production private observer uses observe_binding and never mints G.
            if(observer.approval_binding&&(observer.approval_binding->identity.world!=scope.world
                ||(observer.approval_binding->identity.player&63)+1!=scope.owner))return;
            approved=approval(scope);
            if(!approved){if(observer.started)observer.stopped=true;return;}
            if(observer.approval_binding&&*observer.approval_binding!=*approved){
                observer.stopped=true;return;
            }
        }
        if(!observer.started){
            if(!next_observation_id){observer.stopped=true;return;}
            observer.started=true;observer.start=now;observer.observation_id=next_observation_id++;
            if(observed)observer.observation_binding=observed;
            else observer.approval_binding=approved;
        }
        if(now<observer.start||now-observer.start>=20000||++observer.attempts>200){
            observer.stopped=true;return;
        }
        std::uint64_t state=0,after=0;
        if(!actor_state(scope.actor,state)||(state&unavailable)){
            observer.stopped=true;return;
        }
        if(provider.observe_binding){
            const auto current_binding=provider.observe_binding(scope.world,scope.owner,scope.actor);
            if(!current_binding||!observer.observation_binding||*current_binding!=*observer.observation_binding){
                observer.stopped=true;return;
            }
        }else if(approval(scope)!=approved){observer.stopped=true;return;}
        if(!actor_state(scope.actor,after)||after!=state){
            observer.stopped=true;return;
        }
        const auto index=phase_index(scope.phase);
        if(observer.seen[index]&&now>=observer.last[index]&&now-observer.last[index]<50)return;
        if(observer.events.size()>=200){observer.stopped=true;return;}
        observer.seen[index]=true;observer.last[index]=now;
        observer.elapsed=now-observer.start;
        const auto order=++observer.order;
        observer.events.push_back({scope.phase,order,observer.elapsed});
        observer.phase_mask|=static_cast<std::uint8_t>(1u<<index);
        digest_event(observer.digest,scope.phase,order,observer.elapsed);
        if(observer.phase_mask==0x0f&&observer.order>=16&&observer.elapsed>=1000)
            observer.stopped=true;
    }
    void completed_control(Scope& scope){
        if(!options.movement || !scope.valid)return;
        auto& slot=slots[scope.owner-1];
        auto approved=approval(scope);
        if(!approved){slot.retire();return;}
        MovementInputFrame input{*approved,0,0};
        const auto row=reinterpret_cast<std::uintptr_t>(scope.row);
        if(!movement_read(provider.read,row,0x20,input.player_input) ||
           !movement_read(provider.read,row,0x58,input.actor_input)){slot.retire();return;}
        auto authorize=[&]{return approval(scope);};
        auto command=read_movement_input(slot.policy,input,authorize,provider.read);
        if(!command){slot.retire();return;}
        slot.command=command;slot.approval=approved;slot.time=provider.now_ms();slot.selected=false;slot.moved=false;
    }
    bool fresh(Slot& slot,const MovementApproval& approved){
        const auto now=provider.now_ms();
        if(!slot.command || !slot.approval || *slot.approval!=approved || now<slot.time ||
            now-slot.time>max_command_age_ms){slot.retire();return false;}
        return true;
    }
    void gravity_patch(Scope& scope){
        if(!options.movement || !scope.valid)return;
        auto& slot=slots[scope.owner-1];auto approved=approval(scope);
        if(!approved){slot.retire();return;}
        std::uint64_t state=0;
        if(!fresh(slot,*approved) || !slot.selected || !slot.policy.flying())return;
        if(!actor_state(scope.actor,state) || (state&unavailable)) {slot.retire();return;}
        if(state&1){slot.land();return;} // Grounded retires this local flight run.
        std::uintptr_t gravity=0;
        if(!movement_read(provider.read,reinterpret_cast<std::uintptr_t>(scope.row),0x10,gravity) ||
            !movement_read(provider.read,gravity,0,scope.gravity)) {slot.retire();return;}
        std::uint64_t after=0;
        bool landed=false;
        if(airborne_approval(scope,landed)!=approved){if(landed)slot.land();else slot.retire();return;}
        if(!actor_state(scope.actor,after) || (after&unavailable)){slot.retire();return;}
        if(after&1){slot.land();return;}
        if(after!=state){slot.retire();return;}
        scope.gravity[0x10]=0; // Native early gate: skip only this actor's gravity row.
        scope.gravity_original=gravity;
        scope.row[0x10/8]=reinterpret_cast<std::uintptr_t>(scope.gravity.data());scope.patched=true;
    }
};
thread_local ServerGRuntime::Impl::Scope* ServerGRuntime::Impl::current=nullptr;
ServerGRuntime::ServerGRuntime():impl_(std::make_unique<Impl>()){}
ServerGRuntime::~ServerGRuntime(){if(!stop())std::terminate();}
bool ServerGRuntime::start(ServerGProvider provider,ServerGOptions options){
    std::lock_guard lock(impl_->mutex);
    if(impl_->enabled || impl_->callbacks || (!options.movement && !options.observe_phases) ||
       !options.full_pinned_server_hash_verified || (options.movement && !options.phase_order_accepted) ||
       !provider.read || !provider.approve || !provider.now_ms || !provider.entity ||
       (options.movement && !provider.dive))return false;
    impl_->provider=std::move(provider);impl_->options=options;impl_->observer={};impl_->retire_all();impl_->enabled=true;return true;
}
bool ServerGRuntime::stop(){
    std::unique_lock lock(impl_->mutex);impl_->enabled=false;impl_->retire_all();impl_->observer.stopped=true;
    for(auto* scope=Impl::current;scope;scope=scope->previous)if(&scope->runtime==impl_.get())return false;
    impl_->drained.wait(lock,[&]{return impl_->callbacks==0;});return true;
}
bool ServerGRuntime::active() const{std::lock_guard lock(impl_->mutex);return impl_->enabled;}
std::vector<ServerPhaseEvent> ServerGRuntime::take_phase_events(){std::lock_guard lock(impl_->mutex);auto result=std::move(impl_->observer.events);impl_->observer.events.clear();return result;}
ServerPhaseEvidence ServerGRuntime::phase_evidence() const {
    std::lock_guard lock(impl_->mutex);const auto& observer=impl_->observer;
    const bool complete=observer.started&&observer.phase_mask==0x0f&&observer.order>=16&&observer.elapsed>=1000;
    return {observer.observation_id,observer.order,observer.digest,observer.elapsed,
        observer.phase_mask,complete,observer.stopped,observer.accepted};
}
bool ServerGRuntime::reset_phase_observer() {
    std::lock_guard lock(impl_->mutex);
    for(auto* scope=Impl::current;scope;scope=scope->previous)
        if(&scope->runtime==impl_.get())return false;
    if(!impl_->enabled||!impl_->options.observe_phases||impl_->options.movement
        ||impl_->observer.accepted)return false;
    impl_->observer={};impl_->observer.authorized=true;return true;
}
bool ServerGRuntime::accept_phase_order(const ServerPhaseEvidence& evidence) {
    std::lock_guard lock(impl_->mutex);
    for(auto* scope=Impl::current;scope;scope=scope->previous)
        if(&scope->runtime==impl_.get())return false;
    auto& observer=impl_->observer;
    if(!impl_->enabled||!impl_->options.observe_phases||impl_->options.movement
        ||!observer.authorized||observer.accepted||!impl_->provider.observe_binding||!observer.observation_binding
        ||!evidence.complete||evidence.phase_mask!=0x0f||evidence.event_count<16
        ||evidence.elapsed_ms<1000||evidence.observation_id!=observer.observation_id
        ||evidence.event_count!=observer.order||evidence.phase_mask!=observer.phase_mask
        ||evidence.digest!=observer.digest||evidence.elapsed_ms!=observer.elapsed)return false;
    const auto& binding=*observer.observation_binding;
    std::optional<ServerObservationBinding> current_binding;
    try{current_binding=impl_->provider.observe_binding(binding.world,binding.owner,binding.actor);}
    catch(...){return false;}
    if(!current_binding||*current_binding!=binding)return false;
    observer.accepted=true;observer.stopped=true;impl_->options.phase_order_accepted=true;return true;
}
bool ServerGRuntime::enable_movement() {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->enabled||!impl_->options.full_pinned_server_hash_verified
        ||!impl_->options.phase_order_accepted||!impl_->observer.accepted
        ||!impl_->provider.dive||!impl_->provider.approve)return false;
    impl_->options.movement=true;return true;
}
void ServerGRuntime::callback(ServerPhase phase,void* query,ServerCallback original){
    if(!original)throw std::invalid_argument("missing native callback original");
    if(phase_index(phase)>=row_sizes.size() || !query || !impl_->enter()){original(query);return;}
    struct Leave{Impl& runtime;~Leave(){runtime.leave();}} leave{*impl_};
    Impl::Scope scope(*impl_,phase,query);
    if(phase==ServerPhase::control){
        std::lock_guard lock(impl_->mutex);
        std::uintptr_t world=0,definition=0,execution=0;bool known=false;
        try{known=impl_->query_world(query,world,definition,execution);}catch(...){}
        // Every new control callback supersedes pending outputs for that world.
        // Unknown context suspends all copied commands without inventing a life
        // transition or revoking unvisited owners' authenticated evidence.
        for(auto& slot:impl_->slots)if(!known || (slot.approval && slot.approval->identity.world==world)){
            slot.command.reset();slot.selected=false;slot.moved=false;
        }
    }
    try{original(query);}catch(...){std::lock_guard lock(impl_->mutex);impl_->retire_all();throw;}
}
bool ServerGRuntime::query_step(void* query,void* row,std::uint32_t size,ServerIterator original){
    if(!original)throw std::invalid_argument("missing native iterator original");
    auto* scope=Impl::current;
    if(!scope || &scope->runtime!=impl_.get() || scope->query!=query ||
       phase_index(scope->phase)>=row_sizes.size() || row_sizes[phase_index(scope->phase)]!=size)return original(query,row,size);
    {
        std::lock_guard lock(impl_->mutex);
        try{if(impl_->enabled && scope->phase==ServerPhase::control)impl_->completed_control(*scope);}
        catch(...){impl_->retire_all();}
        scope->reset_row();
    }
    const bool result=original(query,row,size); // Exactly once, even on failed capture.
    {
        std::lock_guard lock(impl_->mutex);
        if(!impl_->enabled || !result || ++scope->steps>16384)return result;
        try {
            if(impl_->capture(*scope,row,size)){impl_->observe(*scope);if(scope->phase==ServerPhase::gravity)impl_->gravity_patch(*scope);}
        }catch(...){scope->reset_row();impl_->retire_all();}
    }
    return result;
}
std::uint8_t ServerGRuntime::state_decision(void* a,void* actor,void* c,void* d,void* e,void* f,void* g,void* h,ServerState original){
    if(!original)throw std::invalid_argument("missing native state original");
    const auto native=original(a,actor,c,d,e,f,g,h); // Forward every argument once.
    auto* scope=Impl::current;
    if(!scope || &scope->runtime!=impl_.get() || scope->phase!=ServerPhase::state || !scope->valid ||
       scope->actor!=reinterpret_cast<std::uintptr_t>(actor))return native;
    std::lock_guard lock(impl_->mutex);
    auto& slot=impl_->slots[scope->owner-1];
    try {
        auto approved=impl_->approval(*scope);
        if(!impl_->options.movement || !approved || !impl_->fresh(slot,*approved) || slot.selected){slot.retire();return native;}
        bool landed=false;auto candidate=slot.policy;
        auto authorize=[&]{return impl_->airborne_approval(*scope,landed);};
        const auto selected=select_g_state(candidate,*approved,native,authorize,impl_->provider.read);
        if(landed){slot.land();return native;}
        slot.policy=std::move(candidate);
        slot.selected=selected==3 && slot.policy.flying();
        if(!slot.selected)slot.command.reset();
        return selected;
    }catch(...){slot.retire();return native;}
}
void ServerGRuntime::dispatch_current(std::uintptr_t* row,void* second){
    auto* scope=Impl::current;
    if(!scope || scope->phase!=ServerPhase::mover || !scope->valid || scope->row!=row)return;
    auto* impl_=&scope->runtime;
    bool invoked=false;
    {
        std::lock_guard lock(impl_->mutex);auto& slot=impl_->slots[scope->owner-1];
        try {
            if(!impl_->options.movement)return;
            bool landed=false;auto approved=impl_->airborne_approval(*scope,landed);
            if(!approved){if(landed)slot.land();else slot.retire();return;}
            if(impl_->fresh(slot,*approved) && slot.selected && !slot.moved){
                auto candidate=slot.policy;
                auto authorize=[&]{return impl_->airborne_approval(*scope,landed);};
                // Recheck at the actual native entry after private-pointer setup.
                // If Dive throws, never run the vanilla mover a second time.
                auto invoke=[&](auto native_row,auto native_second){
                    if(authorize()!=approved)return;
                    invoked=true;impl_->provider.dive(native_row,native_second);
                };
                const bool result=invoke_g_dive(candidate,*approved,*slot.command,MoverRow(row,0x138/8),second,authorize,impl_->provider.read,invoke);
                if(landed)slot.land();
                else {slot.policy=std::move(candidate);slot.moved=result && invoked;}
            }
        }catch(...){slot.retire();if(invoked)throw;}
    }
    // The real bridge always returns into native dispatch; state4 and denied
    // rows keep their native branches. Dive failures propagate through FRAME.
}
}
extern "C" void creative_g_dispatch_route(std::uintptr_t* row,void* second) noexcept(false){
    xhl::creative_flight::ServerGRuntime::dispatch_current(row,second);
}

