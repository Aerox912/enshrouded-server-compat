#pragma once
#include <cstdint>
namespace xhl::client {
struct Host {std::uintptr_t context=0;std::uint16_t peer=0;std::uint64_t steam=0,generation=0,seen=0;};
struct Toggle {
    bool on=false,armed=false;
    bool sample(bool down,bool focused) {
        if(!focused){armed=false;return false;}
        if(!down){armed=true;return false;}
        if(!armed)return false;
        armed=false;on=!on;return true;
    }
};
Host current_host();
bool allowed_host(std::uint64_t steam);
std::uint64_t random_number();
bool prepare();
void set_local_flight(bool enabled);
}
