// Modal menus own gameplay input until all controls have been released.
#pragma once
#include <atomic>
namespace recomp3ds {
class InputCaptureGate {
    std::atomic<bool> open_{false}, waiting_{false};
public:
    void set_open(bool open){waiting_.store(true);open_.store(open);}
    void poll(bool neutral){if(!open_.load() && neutral)waiting_.store(false);}
    bool blocked() const{return open_.load() || waiting_.load();}
};
}
