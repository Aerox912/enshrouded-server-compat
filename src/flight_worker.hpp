#pragma once
#include <windows.h>
#include <atomic>

namespace xhl::flight {
// The loader updates mods every 500 ms by default. Input edges and networking
// need their own short cadence; all flight session/UI state has one owner.
class PollWorker {
    HANDLE stop_ = nullptr, thread_ = nullptr;
    void (*tick_)(void*) = nullptr;
    void (*finish_)(void*) = nullptr;
    void* context_ = nullptr;
    std::atomic<bool> failed_{false};
    static DWORD WINAPI run(void* value) noexcept {
        auto& self=*static_cast<PollWorker*>(value);
        try {
            while(WaitForSingleObject(self.stop_,0)==WAIT_TIMEOUT) {
                self.tick_(self.context_);
                if(WaitForSingleObject(self.stop_,8)!=WAIT_TIMEOUT)break;
            }
        } catch(...) { self.failed_.store(true); }
        try { self.finish_(self.context_); } catch(...) { self.failed_.store(true); }
        return 0;
    }
public:
    PollWorker()=default;
    PollWorker(const PollWorker&)=delete;
    PollWorker& operator=(const PollWorker&)=delete;
    ~PollWorker(){stop();}
    bool start(void (*tick)(void*),void (*finish)(void*),void* context) {
        if(thread_ || !tick || !finish)return false;
        stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop_)return false;
        tick_=tick;finish_=finish;context_=context;failed_.store(false);
        thread_=CreateThread(nullptr,0,run,this,0,nullptr);
        if(!thread_){CloseHandle(stop_);stop_=nullptr;return false;}
        return true;
    }
    void stop() {
        if(!thread_)return;
        SetEvent(stop_);WaitForSingleObject(thread_,INFINITE);
        CloseHandle(thread_);CloseHandle(stop_);thread_=stop_=nullptr;
    }
    bool failed() const noexcept{return failed_.load();}
};
}
