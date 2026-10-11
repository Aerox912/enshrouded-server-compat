#pragma once
#include "flight_session.hpp"
#include <array>
#include <charconv>
#include <cstdio>
#include <optional>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#endif

namespace xhl::creative_flight {
// Diagnostic only. No movement writes, hooks, remote configuration or F6 lease.
// A private process environment value must opt in before the first callback.
struct ProbeConfig {
    bool enabled=false;
    std::uint32_t owner=0,duration_ms=20000,sample_limit=200,interval_ms=100;
};
inline ProbeConfig parse_probe_config(std::string_view text) noexcept {
    ProbeConfig result;unsigned seen=0;
    if(text.empty() || text.size()>160)return {};
    while(!text.empty()) {
        const auto delimiter=text.find(';');
        const auto token=text.substr(0,delimiter);
        const auto equals=token.find('=');
        if(equals==std::string_view::npos || equals==0 || equals+1==token.size())return {};
        const auto key=token.substr(0,equals),value=token.substr(equals+1);
        std::uint32_t number=0;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
        if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())return {};
        unsigned bit=0;
        if(key=="enabled"){bit=1;if(number!=1)return {};result.enabled=true;}
        else if(key=="owner"){bit=2;result.owner=number;}
        else if(key=="duration_ms"){bit=4;result.duration_ms=number;}
        else if(key=="sample_limit"){bit=8;result.sample_limit=number;}
        else if(key=="interval_ms"){bit=16;result.interval_ms=number;}
        else return {};
        if(seen&bit)return {};
        seen|=bit;
        if(delimiter==std::string_view::npos)break;
        text.remove_prefix(delimiter+1);
        if(text.empty())return {};
    }
    if((seen&3)!=3 || result.owner<1 || result.owner>16 || !result.duration_ms ||
       result.duration_ms>20000 || !result.sample_limit || result.sample_limit>200 ||
       result.interval_ms<100 || result.interval_ms>20000)return {};
    return result;
}
struct ProbeFrame {
    flight::Identity identity{};
    std::uintptr_t actor=0,player_input=0;
};
struct InputSample {
    bool jump=false,sneak=false,sprint=false,grounded=false,flying=false;
    std::uint8_t input_mode=0;
};
class InputProbe {
    ProbeConfig config_;
    std::optional<flight::Identity> identity_;
    std::uint64_t started_=0,last_=0;
    std::uint32_t attempts_=0;
    bool begun_=false,stopped_=false;
public:
    explicit InputProbe(ProbeConfig config={}):config_(config) {
        if(config_.owner<1 || config_.owner>16 || !config_.duration_ms || config_.duration_ms>20000 ||
           !config_.sample_limit || config_.sample_limit>200 || config_.interval_ms<100 || config_.interval_ms>20000)
            config_.enabled=false;
    }
    bool enabled() const noexcept{return config_.enabled && !stopped_;}
    std::uint32_t attempts() const noexcept{return attempts_;}
    // Caller serializes this observer with the existing inventory callback guard.
    // authorize returns only a current authenticated, alive, approved full Identity.
    // Component resolution is lazy and happens only after the first authorization.
    template<class Authorize,class FrameGetter,class Read,class Emit>
    bool observe(std::uint64_t now,std::uint32_t owner,Authorize&& authorize,
                 FrameGetter&& frame_getter,Read&& read,Emit&& emit) {
        if(!enabled() || owner!=config_.owner)return false;
        if(begun_ && (now<last_ || now<started_ || now-started_>=config_.duration_ms || attempts_>=config_.sample_limit)) {
            stopped_=true;return false;
        }
        const auto before=authorize();
        if(!before || !before->valid(owner-1)) {
            if(begun_){++attempts_;stopped_=true;}
            return false;
        }
        if(identity_ && *identity_!=*before){stopped_=true;return false;}
        if(!begun_){begun_=true;started_=last_=now;identity_=*before;}
        if(attempts_ && now-last_<config_.interval_ms)return false;
        last_=now;++attempts_; // Failed reads/auth checks consume budget as well.
        const ProbeFrame frame=frame_getter();
        if(frame.identity!=*before || !frame.actor || !frame.player_input)return false;
        if(frame.player_input>UINTPTR_MAX-0x528 || frame.actor>UINTPTR_MAX-0xc00)return false;
        std::uint64_t mask=0,state=0;InputSample sample;
        if(!read(frame.player_input+0x320,&mask,sizeof(mask)) ||
           !read(frame.player_input+0x519,&sample.input_mode,sizeof(sample.input_mode)) ||
           !read(frame.actor+0xbf8,&state,sizeof(state)))return false;
        const auto after=authorize();
        if(!after || *after!=*before){stopped_=true;return false;}
        sample.jump=(mask&(1ull<<30))!=0;
        sample.sprint=(mask&(1ull<<50))!=0;
        sample.sneak=(mask&(1ull<<52))!=0;
        sample.grounded=(state&1)!=0;
        sample.flying=(state&(1ull<<39))!=0;
        emit(sample);return true;
    }
};
inline InputProbe& private_input_probe() {
    static InputProbe probe([] {
#ifdef _WIN32
        std::array<char,161> value{};
        const auto length=GetEnvironmentVariableA("ENSHROUDED_CREATIVE_FLIGHT_INPUT_PROBE",value.data(),static_cast<DWORD>(value.size()));
        if(length && length<value.size())return parse_probe_config({value.data(),length});
#endif
        return ProbeConfig{};
    }());
    return probe;
}
inline void format_sample(const InputSample& sample,char (&line)[256]) noexcept {
    // Labels describe encoded game input bits. Held/toggle semantics need observation.
    std::snprintf(line,sizeof(line),"CREATIVE G INPUT: Jump=%u Sneak=%u Sprint=%u mode=%u Grounded=%u Flying=%u.",
        unsigned(sample.jump),unsigned(sample.sneak),unsigned(sample.sprint),unsigned(sample.input_mode),
        unsigned(sample.grounded),unsigned(sample.flying));
}
}
