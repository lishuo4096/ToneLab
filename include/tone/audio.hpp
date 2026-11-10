#pragma once
#include "dsp.hpp"
#include <memory>
#include <vector>
namespace tone {
enum class Backend { Wasapi, DirectSound, Asio };
Backend parseBackend(const std::string&);
const char* backendName(Backend);
struct Device { unsigned id; std::string name; std::wstring endpoint; std::vector<unsigned> rates; unsigned nativeFormats=0,preferredRate=0; };
struct StreamFormat { unsigned rate=48000; Format format=Format::I32; };
struct DeviceDetails { std::vector<StreamFormat> supported; StreamFormat windowsDefault; bool hasWindowsDefault=false; std::string note; };
struct Volume { float level=0; bool muted=false,hardware=false; };
struct Config { Backend backend=Backend::Wasapi; unsigned device=0,rate=48000,buffer=2048; Format format=Format::I32; bool autoFormat=true; };
std::vector<Device> devices(Backend);
DeviceDetails probeDevice(Backend,const Device&);
Volume deviceVolume(const std::wstring& endpoint);
void setDeviceVolume(const std::wstring& endpoint,float level,bool muted);
class Audio {
    struct Impl; std::unique_ptr<Impl> p_;
public:
    std::array<Controls,2> controls;
    Audio(); ~Audio();
    void start(const Config&);
    void stop();
    bool running() const;
    std::string status() const;
    uint64_t xruns() const;
    void asioPanel();
};
}
