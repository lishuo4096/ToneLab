#include "tone/audio.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace tone;
static std::atomic<bool> cancelled{false};
static BOOL WINAPI handler(DWORD type){if(type==CTRL_C_EVENT||type==CTRL_BREAK_EVENT){cancelled=true;return TRUE;}return FALSE;}
static void help(){std::cout<<R"(ToneLab - native stereo signal generator
Usage:
  tonelab-cli list [--backend wasapi|ds|asio|all]
  tonelab-cli inspect --backend wasapi --device ID
  tonelab-cli volume --device ID [--level 0..100] [--mute 0|1]
  tonelab-cli play [options]
  tonelab-cli render --output file.wav [options]
Options:
  --backend wasapi|ds|asio  (default wasapi, exclusive)
  --device ID               ID from list for the chosen backend
  --rate 384000             Sample rate, default 48000
  --format auto|16|24|32|f32|24in32 (play: Windows/native default)
  --buffer 2048             Requested buffer frames
  --left-freq 1000 --right-freq 2000
  --left-wave sine|square|saw|triangle --right-wave sine
  --left-amp 0.1 --right-amp 0.1  Linear full-scale peak (0..1)
  --left-mute 0|1 --right-mute 0|1
  --seconds 5               Play: optional (Ctrl+C stops); render: required
Examples:
  tonelab-cli list --backend all
  tonelab-cli play --backend wasapi --device 0 --rate 384000 --format 32 --seconds 5
  tonelab-cli render --output native.wav --rate 384000 --format 32 --seconds 2
Exit codes: 0 success, 1 device/runtime/I/O failure, 2 invalid arguments.
No resampling or silent format fallback. DirectSound uses PCM16 and the system mixer.
)";}
static double number(const std::string& s){size_t used=0;double v=std::stod(s,&used);if(used!=s.size()||!std::isfinite(v))throw std::runtime_error("Invalid numeric value: "+s);return v;}
static unsigned integer(const std::string& s){double v=number(s);if(v<0||v>100000000||std::floor(v)!=v)throw std::runtime_error("Expected unsigned integer: "+s);return unsigned(v);}
static void writeLE(std::ostream& f,uint32_t v,int n){for(int i=0;i<n;++i)f.put(char(v>>(8*i)));}
static void renderWav(const std::string& path,Config cfg,const std::array<Channel,2>& channels,double seconds){
    uint64_t frames=uint64_t(seconds*cfg.rate),dataBytes=frames*2*bytes(cfg.format);if(dataBytes>0xffffffffULL-36)throw std::runtime_error("WAV exceeds RIFF 4 GiB limit");
    std::ofstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot create WAV: "+path);
    bool isFloat=cfg.format==Format::F32,extended=cfg.format==Format::I24In32;
    file.write("RIFF",4);writeLE(file,uint32_t(dataBytes+36+(isFloat?12:0)+(extended?24:0)),4);file.write("WAVEfmt ",8);writeLE(file,extended?40:16,4);writeLE(file,extended?65534:isFloat?3:1,2);writeLE(file,2,2);writeLE(file,cfg.rate,4);writeLE(file,cfg.rate*2*bytes(cfg.format),4);writeLE(file,2*bytes(cfg.format),2);writeLE(file,8*bytes(cfg.format),2);
    if(extended){writeLE(file,22,2);writeLE(file,24,2);writeLE(file,3,4);const unsigned char guid[]={1,0,0,0,0,0,16,0,128,0,0,170,0,56,155,113};file.write(reinterpret_cast<const char*>(guid),16);}
    if(isFloat){file.write("fact",4);writeLE(file,4,4);writeLE(file,uint32_t(frames),4);}
    file.write("data",4);writeLE(file,uint32_t(dataBytes),4);
    Generator gen(cfg.rate,cfg.format);std::vector<unsigned char> buffer(4096*2*bytes(cfg.format));
    while(frames){unsigned n=unsigned(std::min<uint64_t>(frames,4096));gen.render(buffer.data(),n,channels);file.write(reinterpret_cast<char*>(buffer.data()),n*2*bytes(cfg.format));frames-=n;}
    file.flush();if(!file)throw std::runtime_error("WAV write failed");std::cout<<"Rendered "<<path<<": "<<cfg.rate<<" Hz / "<<formatName(cfg.format)<<" / stereo\n";
}
int main(int argc,char** argv){
    SetConsoleOutputCP(CP_UTF8);
    if(argc<2||std::string(argv[1])=="--help"||std::string(argv[1])=="help"){help();return 0;}
    Config cfg;std::array<Channel,2> channels;double seconds=-1,level=-1;int mute=-1;bool specifiedRate=false;std::string output,command=argv[1],backend="wasapi";
    try {
        if(command!="list"&&command!="play"&&command!="render"&&command!="inspect"&&command!="volume")throw std::runtime_error("Command must be list, play, render, inspect or volume");
        std::map<std::string,std::string> options;
        for(int i=2;i<argc;i+=2){if(i+1>=argc)throw std::runtime_error("Missing value for "+std::string(argv[i]));if(!options.emplace(argv[i],argv[i+1]).second)throw std::runtime_error("Duplicate option: "+std::string(argv[i]));}
        for(auto& [key,value]:options){
            if(key=="--backend")backend=value;
            else if(key=="--device")cfg.device=integer(value);
            else if(key=="--rate"){cfg.rate=integer(value);specifiedRate=true;}
            else if(key=="--format"){cfg.autoFormat=value=="auto";if(!cfg.autoFormat)cfg.format=parseFormat(value);}
            else if(key=="--level"){level=number(value);if(level<0||level>100)throw std::runtime_error("Device volume must be 0..100");}
            else if(key=="--mute"){if(value!="0"&&value!="1")throw std::runtime_error("Mute must be 0 or 1");mute=integer(value);}
            else if(key=="--buffer")cfg.buffer=integer(value);
            else if(key=="--seconds"){seconds=number(value);if(seconds<=0||seconds>86400)throw std::runtime_error("Seconds must be >0 and <=86400");}
            else if(key=="--output")output=value;
            else {
                int ch=key.rfind("--left-",0)==0?0:key.rfind("--right-",0)==0?1:-1;
                if(ch<0)throw std::runtime_error("Unknown option: "+key);
                std::string property=key.substr(ch==0?7:8);
                if(property=="freq")channels[ch].frequency=number(value);
                else if(property=="amp")channels[ch].amplitude=number(value);
                else if(property=="wave")channels[ch].wave=parseWave(value);
                else if(property=="mute"){if(value!="0"&&value!="1")throw std::runtime_error("Mute must be 0 or 1");channels[ch].enabled=value=="0";}
                else throw std::runtime_error("Unknown option: "+key);
            }
        }
        if(command=="list"){
            for(auto b:{Backend::Wasapi,Backend::DirectSound,Backend::Asio}){if(backend!="all"&&parseBackend(backend)!=b)continue;std::cout<<backendName(b)<<"\n";auto list=devices(b);if(list.empty())std::cout<<"  (no stereo output devices)\n";for(auto& d:list){std::cout<<"  ["<<d.id<<"] "<<d.name;if(!d.rates.empty()){std::cout<<" | candidate rates:";for(auto r:d.rates)std::cout<<' '<<r;}std::cout<<'\n';}}return 0;
        }
        cfg.backend=parseBackend(backend);for(auto& c:channels)validate(c,cfg.rate);
        if(command=="render"&&(output.empty()||seconds<0))throw std::runtime_error("Render requires --output and --seconds");
    }catch(const std::exception& e){std::cerr<<"Argument error: "<<e.what()<<'\n';return 2;}
    try {
        if(command=="render"){renderWav(output,cfg,channels,seconds);return 0;}
        bool specifiedDevice=false;for(int i=2;i<argc;i+=2)if(std::string(argv[i])=="--device")specifiedDevice=true;
        auto list=devices(cfg.backend);if(list.empty())throw std::runtime_error("No stereo output device for this backend");
        if(!specifiedDevice)cfg.device=list.front().id;
        auto it=std::find_if(list.begin(),list.end(),[&](const Device& d){return d.id==cfg.device;});if(it==list.end())throw std::runtime_error("Device ID not available");
        if(command=="volume"){
            if(it->endpoint.empty())throw std::runtime_error("Volume command requires WASAPI endpoint");
            auto v=deviceVolume(it->endpoint);if(level>=0||mute>=0){setDeviceVolume(it->endpoint,level>=0?float(level/100):v.level,mute>=0?mute!=0:v.muted);v=deviceVolume(it->endpoint);}std::cout<<"Device volume: "<<v.level*100<<" % | mute="<<v.muted<<" | hardware="<<v.hardware<<'\n';return 0;
        }
        auto details=probeDevice(cfg.backend,*it);
        if(command=="inspect"){
            std::cout<<it->name<<'\n';if(details.hasWindowsDefault)std::cout<<"Windows default: "<<details.windowsDefault.rate<<" Hz / "<<formatName(details.windowsDefault.format)<<'\n';
            for(auto f:details.supported)std::cout<<f.rate<<" Hz / "<<formatName(f.format)<<'\n';
            std::cout<<details.note<<'\n';return 0;
        }
        if(!specifiedRate&&details.hasWindowsDefault)cfg.rate=details.windowsDefault.rate;
        if(cfg.autoFormat&&cfg.backend==Backend::Wasapi){bool found=false;for(auto f:details.supported)if(f.rate==cfg.rate&&f.format==details.windowsDefault.format){cfg.format=f.format;found=true;break;}if(!found)for(auto f:details.supported)if(f.rate==cfg.rate){cfg.format=f.format;break;}}
        if(cfg.autoFormat&&cfg.backend==Backend::DirectSound)cfg.format=Format::I16;
        SetConsoleCtrlHandler(handler,TRUE);Audio audio;for(int i=0;i<2;++i)audio.controls[i].set(channels[i]);audio.start(cfg);std::cout<<audio.status()<<"\nCtrl+C to stop.\n";
        auto begin=std::chrono::steady_clock::now();while(!cancelled&&audio.running()&&(seconds<0||std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<seconds))std::this_thread::sleep_for(std::chrono::milliseconds(25));
        bool failed=!cancelled&&!audio.running();auto status=audio.status();audio.stop();std::cout<<"Stopped. Underruns: "<<audio.xruns()<<'\n';if(failed)throw std::runtime_error(status);return 0;
    }catch(const std::exception& e){std::cerr<<"Error: "<<e.what()<<'\n';return 1;}
}
