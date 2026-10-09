#pragma once
#include "creative_flight_local.hpp"

namespace xhl::creative_flight {
// Pinned CLIENT only. These are copied numeric identity tags, never borrowed
// pointers to dereference later. This reader is not an authorization source.
struct LocalLifeIdentity {
    std::uintptr_t client=0,session=0,scene=0,simulation=0,world=0,actor_storage=0;
    std::uint32_t entity=0;
    bool operator==(const LocalLifeIdentity&) const = default;
};
struct LocalLifeSnapshot {
    LocalLifeIdentity identity{};
    std::uint64_t current_state=0,effective_state=0;
    bool alive() const noexcept { return !((current_state|effective_state)&((1ull<<7)|(1ull<<12))); }
};
enum class LocalLifeReadStage : std::uint8_t {
    none,
    full_hash_guard,
    image_base_unavailable,
    world_chain,
    owned_actor,
    component_registry,
    component_type_lookup,
    entity_map,
    actor_location,
    life_state_read,
    consistency_reread,
    read_attempt_limit
};
struct LocalLifeReadDiagnostic {
    LocalLifeReadStage stage=LocalLifeReadStage::none;
    std::uint16_t read_attempts=0;
};
inline const char* local_life_read_stage_name(LocalLifeReadStage stage) noexcept {
    switch(stage) {
    case LocalLifeReadStage::none:return "none";
    case LocalLifeReadStage::full_hash_guard:return "full-hash-guard";
    case LocalLifeReadStage::image_base_unavailable:return "image-base-unavailable";
    case LocalLifeReadStage::world_chain:return "world-chain";
    case LocalLifeReadStage::owned_actor:return "owned-actor";
    case LocalLifeReadStage::component_registry:return "component-registry";
    case LocalLifeReadStage::component_type_lookup:return "component-type-lookup";
    case LocalLifeReadStage::entity_map:return "entity-map";
    case LocalLifeReadStage::actor_location:return "actor-location";
    case LocalLifeReadStage::life_state_read:return "life-state-read";
    case LocalLifeReadStage::consistency_reread:return "consistency-reread";
    case LocalLifeReadStage::read_attempt_limit:return "read-attempt-limit";
    }
    return "unknown";
}
namespace client_life_detail {
inline constexpr std::uintptr_t game_rva=0x1f07cc0, client_actor_type_rva=0x17bd910;
struct WorldChain {
    std::uintptr_t client=0,session=0,scene=0,simulation=0,world=0,simulation_world=0;
    std::uint8_t active=0;
    bool operator==(const WorldChain&) const = default;
};
template<class Read>
bool chain(Read& read,std::uintptr_t base,WorldChain& value) {
    if(!base || base>UINTPTR_MAX-game_rva)return false;
    return local_field(read,base+game_rva,0x250,value.client) &&
        local_field(read,value.client,0x52888,value.session) &&
        local_field(read,value.session,0x20,value.active) && value.active &&
        local_field(read,value.session,0x180,value.scene) &&
        local_field(read,value.scene,0x3439c0,value.simulation) &&
        local_field(read,value.scene,0x1c0,value.world) && value.world &&
        local_field(read,value.simulation,8,value.simulation_world) && value.world==value.simulation_world;
}
struct Components {
    std::uintptr_t registry=0,records=0;std::uint64_t count=0;
    bool operator==(const Components&) const = default;
};
template<class Read>
bool components(Read& read,std::uintptr_t world,Components& value) {
    if(world>UINTPTR_MAX-0x928)return false;
    return local_field(read,world,0xcc4218,value.registry) && value.registry==world+0x928 &&
        local_field(read,world,0x930,value.records) && value.records &&
        local_field(read,world,0x938,value.count) && value.count && value.count<=1280;
}
template<class Read>
std::optional<std::uint16_t> client_actor_type_index(Read& read,const Components& value,std::uintptr_t type) {
    std::optional<std::uint16_t> found;
    for(std::uint64_t index=0;index<value.count;++index) {
        std::uintptr_t metadata=0;
        if(!local_field(read,value.records,index*0x100+0x28,metadata))return {};
        if(metadata==type) {
            if(found)return {}; // Ambiguous registration is never accepted.
            found=static_cast<std::uint16_t>(index);
        }
    }
    return found;
}
struct EntityMap {
    std::uintptr_t bitmap=0,keys=0,values=0;std::uint64_t capacity=0;std::uint32_t occupied=0;
    bool operator==(const EntityMap&) const = default;
};
template<class Read>
bool entity_map(Read& read,std::uintptr_t world,EntityMap& value) {
    if(world>UINTPTR_MAX-0xcc4360)return false;
    const auto at=world+0xcc4360;
    return local_field(read,at,8,value.bitmap) && value.bitmap &&
        local_field(read,at,0x10,value.capacity) && value.capacity && value.capacity<=1048576 &&
        !(value.capacity&(value.capacity-1)) && local_field(read,at,0x28,value.keys) && value.keys &&
        local_field(read,at,0x40,value.values) && value.values &&
        local_field(read,at,0x54,value.occupied) && value.occupied && value.occupied<=value.capacity;
}
inline std::uint32_t entity_hash(std::uint32_t value) noexcept {
    value=((value>>16)^value)*0x45d9f3bu;
    value=((value>>16)^value)*0x45d9f3bu;
    return (value>>16)^value;
}
struct ClientActorBits {
    std::uint64_t current=0,added=0,removed=0;std::uint8_t prediction=0;
    bool operator==(const ClientActorBits&) const = default;
    std::uint64_t effective() const noexcept { return prediction&1 ? (current|added)&~removed : current; }
};
template<class Read>
bool client_actor_bits(Read& read,std::uintptr_t actor,ClientActorBits& value) {
    return local_field(read,actor,0x1b1,value.prediction) && local_field(read,actor,0xbd0,value.added) &&
        local_field(read,actor,0xbd8,value.removed) && local_field(read,actor,0xbf8,value.current);
}
struct ClientActorLocation {
    std::uintptr_t record=0,layout=0,data=0,client_actor=0;std::uint32_t stride=0;
    std::uint16_t group=0,offset=0;
    bool operator==(const ClientActorLocation&) const = default;
};
template<class Read>
bool locate_client_actor(Read& read,const EntityMap& map,std::uint32_t entity,std::uint16_t index,ClientActorLocation& value) {
    auto slot=std::uint64_t(entity_hash(entity))&(map.capacity-1);
    for(unsigned attempt=0;attempt<64;++attempt,slot=(slot+1)&(map.capacity-1)) {
        std::uint64_t bitmap=0;std::uint32_t key=0;
        if(!local_field(read,map.bitmap,(slot/64)*8,bitmap) || !(bitmap&(1ull<<(slot%64))) ||
           !local_field(read,map.keys,slot*4,key))return false;
        if(key!=entity)continue;
        if(!local_field(read,map.values,slot*8,value.record) ||
           !local_field(read,value.record,0x18,value.layout) ||
           !local_field(read,value.record,0x20,value.data) || !value.data ||
           !local_field(read,value.record,0x30,value.stride) || value.stride>0x100000)return false;
        std::uint64_t present=0;
        if(!local_field(read,value.layout,(index/64)*8,present) || !(present&(1ull<<(index%64))) ||
           !local_field(read,value.layout,0xa84+std::uintptr_t(index)*2,value.group) ||
           !local_field(read,value.layout,0x84+std::uintptr_t(index)*2,value.offset))return false;
        const auto displacement=std::uint64_t(value.group)*value.stride+value.offset;
        if(displacement>UINTPTR_MAX-value.data)return false;
        value.client_actor=value.data+static_cast<std::uintptr_t>(displacement);
        return value.client_actor!=0;
    }
    return false;
}
}
// Caller verifies the full pinned client binary hash once before enabling this
// API. Read must safely copy exactly the requested bytes or return false; it must
// not throw. Call synchronously, without caching pointers or an alive result.
// The hard limit is 4096 memory-copy attempts per invocation. A concurrent change
// in copied chain, ownership, registry, entity layout or life bits denies.
// Repeated copies detect observed inconsistency, not undetectable engine ABA.
template<class Read>
std::optional<LocalLifeSnapshot> read_pinned_client_life(Read&& copy,std::uintptr_t base,
    bool full_hash_verified,LocalLifeReadDiagnostic* diagnostic=nullptr) {
    using namespace client_life_detail;
    if(diagnostic)*diagnostic={};
    unsigned attempts=0;
    bool attempt_limit_hit=false;
    const auto failed=[&](LocalLifeReadStage stage)->std::optional<LocalLifeSnapshot> {
        if(diagnostic) {
            diagnostic->stage=attempt_limit_hit?LocalLifeReadStage::read_attempt_limit:stage;
            diagnostic->read_attempts=static_cast<std::uint16_t>(attempts);
        }
        return {};
    };
    if(!full_hash_verified || !base || base>UINTPTR_MAX-client_actor_type_rva)
        return failed(LocalLifeReadStage::full_hash_guard);
    auto read=[&](std::uintptr_t at,void* output,std::size_t size) {
        if(++attempts>4096) { attempt_limit_hit=true;return false; }
        return copy(at,output,size);
    };
    WorldChain before,after;Components types,types_after;EntityMap map,map_after;
    if(!chain(read,base,before))return failed(LocalLifeReadStage::world_chain);
    auto entity=read_pinned_client_local_actor(read,before.world);
    if(!entity)return failed(LocalLifeReadStage::owned_actor);
    if(!components(read,before.world,types))return failed(LocalLifeReadStage::component_registry);
    const auto type=base+client_actor_type_rva;
    auto index=client_actor_type_index(read,types,type);
    if(!index)return failed(LocalLifeReadStage::component_type_lookup);
    ClientActorLocation location,again;
    if(!entity_map(read,before.world,map))return failed(LocalLifeReadStage::entity_map);
    if(!locate_client_actor(read,map,*entity,*index,location))return failed(LocalLifeReadStage::actor_location);
    ClientActorBits state,state_after;
    if(!client_actor_bits(read,location.client_actor,state))return failed(LocalLifeReadStage::life_state_read);
    if(!client_actor_bits(read,location.client_actor,state_after) || state!=state_after)
        return failed(LocalLifeReadStage::consistency_reread);
    if(!entity_map(read,before.world,map_after) || map_after!=map)
        return failed(LocalLifeReadStage::consistency_reread);
    if(!locate_client_actor(read,map_after,*entity,*index,again) || again!=location)
        return failed(LocalLifeReadStage::consistency_reread);
    if(!components(read,before.world,types_after) || types_after!=types)
        return failed(LocalLifeReadStage::consistency_reread);
    if(client_actor_type_index(read,types_after,type)!=index)
        return failed(LocalLifeReadStage::consistency_reread);
    if(read_pinned_client_local_actor(read,before.world)!=entity)
        return failed(LocalLifeReadStage::consistency_reread);
    if(!chain(read,base,after) || after!=before)
        return failed(LocalLifeReadStage::consistency_reread);
    if(!client_actor_bits(read,again.client_actor,state_after) || state_after!=state)
        return failed(LocalLifeReadStage::consistency_reread);
    if(diagnostic) {
        diagnostic->stage=LocalLifeReadStage::none;
        diagnostic->read_attempts=static_cast<std::uint16_t>(attempts);
    }
    return LocalLifeSnapshot{{before.client,before.session,before.scene,before.simulation,before.world,location.client_actor,*entity},state.current,state.effective()};
}
// Host connection/capability epoch are supplied by the client protocol provider,
// never a wire ServerIdentity. An unchanged heartbeat must not rebind a retired
// token. After any unavailable/dead/changed observation, require a freshly
// accepted strictly increasing local capability token (never a server nonce). Observe/check/bind belong under the provider mutex.
class LocalLifeBinding {
public:
    bool bind(std::uint64_t token,const std::optional<LocalLifeSnapshot>& sample) {
        if(!token || token<=retired_ || (token_ && token<token_))return false;
        if(!sample || !sample->alive()) { clear();if(token>retired_)retired_=token;return false; }
        if(token==token_)return check(sample);
        token_=token;identity_=sample->identity;return true;
    }
    bool check(const std::optional<LocalLifeSnapshot>& sample) {
        if(!token_)return false;
        if(!sample || !sample->alive() || sample->identity!=identity_) { clear();return false; }
        return true;
    }
    void clear() noexcept { if(token_>retired_)retired_=token_;token_=0;identity_={}; }
    std::uint64_t token() const noexcept { return token_; }
private:
    LocalLifeIdentity identity_{};std::uint64_t token_=0,retired_=0;
};
}
