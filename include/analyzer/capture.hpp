#pragma once
#include "analysis.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <vector>
namespace analyzer {
struct InputDevice { unsigned id;std::string name;std::wstring endpoint; unsigned defaultRate=0; };
std::vector<InputDevice> inputDevices();
struct CaptureConfig { unsigned device=0,rate=48000;bool exclusive=true;unsigned buffer=4096; };
class Capture {
    struct Impl;std::unique_ptr<Impl> p_;
public:
    Capture();~Capture();void start(CaptureConfig);void stop();bool running()const;
    unsigned rate()const;uint64_t discontinuities()const;std::string status()const;
    Recording latest(unsigned frames)const;
    void beginRecording(double seconds);bool recording()const;Recording recorded()const;
};
class Source {
    struct Impl;std::unique_ptr<Impl> p_;
public:
    Source();~Source();void start(const std::wstring& args);void stop();bool running()const;std::string status()const;
};
}
