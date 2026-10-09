#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

namespace xhl::creative_flight {
// Read-only pinned-client ownership decoder, not an authorization source.
// Caller must verify the pinned client hash, supply the current native ECS world,
// and check the current server G lease/host generation before and after this read.
// No pointers escape. It is not installed or called by any production hook yet.
inline constexpr std::uint32_t local_player_data_hash=0x9117771d; // FNV-1a of LocalPlayerData; verified below.
inline constexpr std::uintptr_t client_globals_offset=0x6d0f80;
inline constexpr std::uint32_t name_hash(const char* text) noexcept {
    std::uint32_t value=0x811c9dc5;
    for(;*text;++text)value=(value^static_cast<unsigned char>(*text))*0x1000193;
    return value;
}
static_assert(name_hash("LocalPlayerData")==local_player_data_hash);

struct GlobalTable {
    std::uintptr_t records=0;
    std::uint64_t count=0,record_capacity=0;
    std::uintptr_t bitmap=0;
    std::uint64_t capacity=0;
    std::uintptr_t keys=0,indices=0;
    std::uint32_t occupied=0;
    bool operator==(const GlobalTable&) const = default;
};
struct GlobalRecord {
    std::uintptr_t name=0;
    std::uint64_t length=0;
    std::uint32_t hash=0;
    std::uintptr_t readable=0,writable=0;
    bool operator==(const GlobalRecord&) const = default;
};
template<class Read,class Value>
inline bool local_field(Read& read,std::uintptr_t pointer,std::uintptr_t offset,Value& value) {
    if(!pointer || offset>UINTPTR_MAX-pointer || sizeof(Value)>UINTPTR_MAX-pointer-offset)return false;
    return read(pointer+offset,&value,sizeof(Value));
}
template<class Read>
inline bool global_table(Read& read,std::uintptr_t world,GlobalTable& table) {
    if(!world || world>UINTPTR_MAX-client_globals_offset)return false;
    const auto at=world+client_globals_offset;
    return local_field(read,at,0,table.records) && local_field(read,at,8,table.count) &&
        local_field(read,at,0x10,table.record_capacity) && local_field(read,at,0x30,table.bitmap) &&
        local_field(read,at,0x38,table.capacity) && local_field(read,at,0x50,table.keys) &&
        local_field(read,at,0x68,table.indices) && local_field(read,at,0x7c,table.occupied);
}
template<class Read>
inline bool global_record(Read& read,std::uintptr_t at,GlobalRecord& record) {
    return local_field(read,at,0,record.name) && local_field(read,at,8,record.length) &&
        local_field(read,at,0xa0,record.hash) && local_field(read,at,0xa8,record.readable) &&
        local_field(read,at,0xb0,record.writable);
}
template<class Read>
inline std::optional<std::uint32_t> read_pinned_client_local_actor(Read&& read,std::uintptr_t world) {
    GlobalTable table;
    if(!global_table(read,world,table) || !table.records || !table.bitmap || !table.keys || !table.indices ||
       !table.count || table.count>512 || table.record_capacity<table.count || table.record_capacity>4096 ||
       !table.capacity || table.capacity>4096 || (table.capacity&(table.capacity-1)) ||
       table.occupied!=table.count || table.occupied>table.capacity)return {};
    auto slot=std::uint64_t(local_player_data_hash)&(table.capacity-1);
    for(unsigned attempt=0;attempt<64;++attempt,slot=(slot+1)&(table.capacity-1)) {
        std::uint64_t occupied=0;std::uint32_t key=0;
        if(!local_field(read,table.bitmap,(slot/64)*8,occupied) || !(occupied&(1ull<<(slot%64))))return {};
        if(!local_field(read,table.keys,slot*4,key))return {};
        if(key!=local_player_data_hash)continue;
        std::uint16_t index=0;
        if(!local_field(read,table.indices,slot*2,index) || index>=table.count ||
           table.records>UINTPTR_MAX-std::uintptr_t(index)*0xc0)return {};
        const auto at=table.records+std::uintptr_t(index)*0xc0;
        GlobalRecord record;
        if(!global_record(read,at,record) || record.length!=15 || record.hash!=local_player_data_hash ||
           !record.readable || record.readable!=record.writable)return {};
        std::array<char,15> name{};
        if(!local_field(read,record.name,0,name) || std::memcmp(name.data(),"LocalPlayerData",name.size()))return {};
        std::uint32_t actor=0,again=0;
        if(!local_field(read,record.readable,4,actor) || !actor)return {};
        GlobalTable after;GlobalRecord record_after;
        if(!global_table(read,world,after) || after!=table || !global_record(read,at,record_after) ||
           record_after!=record || !local_field(read,record.readable,4,again) || again!=actor)return {};
        return actor;
    }
    return {}; // Collision chains are strictly bounded; unsupported tables fail closed.
}
}
