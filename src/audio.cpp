#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "tone/audio.hpp"
#include "RtAudio.h"
#include "asio.h"
#include "tone/asio_events.hpp"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <future>
#include <thread>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <avrt.h>
#include <cstring>
namespace tone {
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr,const char* operation) {if(FAILED(hr)){std::ostringstream s;s<<operation<<" failed (0x"<<std::hex<<uint32_t(hr)<<"). Device may be busy or format unsupported.";throw std::runtime_error(s.str());}}
struct Com { HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); ~Com(){if(SUCCEEDED(hr))CoUninitialize();} };
struct Event { HANDLE handle=CreateEventW(nullptr,FALSE,FALSE,nullptr); ~Event(){if(handle)CloseHandle(handle);} };
struct Priority { DWORD task=0; HANDLE handle=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task); ~Priority(){if(handle)AvRevertMmThreadCharacteristics(handle);} };
struct Audio::Impl {
    Audio* owner;std::unique_ptr<RtAudio> rt;std::unique_ptr<Generator> gen;
    std::thread worker;std::atomic<bool> quit{false},active{false};std::atomic<uint64_t> underruns{0};
    mutable std::mutex mutex;std::string message="Ready. Output is stopped.";
    std::atomic<bool> panel{false};
    Format currentFormat=Format::I32;
    explicit Impl(Audio* a):owner(a){}
    void set(std::string s){std::lock_guard<std::mutex> lock(mutex);message=std::move(s);}
    void render(void* out,unsigned frames){std::array<Channel,2> c{owner->controls[0].get(),owner->controls[1].get()};gen->render(out,frames,c);}
    static int callback(void* out,void*,unsigned frames,double,RtAudioStreamStatus status,void* ptr){auto p=static_cast<Impl*>(ptr);if(status)++p->underruns;if(tonelabAsioEvent.load())std::memset(out,0,frames*2*bytes(p->currentFormat));else p->render(out,frames);return 0;}
    void rtRun(Config cfg,std::promise<void> ready){
        bool announced=false;
        try{
            if(cfg.backend==Backend::DirectSound&&cfg.format!=Format::I16)throw std::runtime_error("DirectSound supports PCM16; use WASAPI or ASIO for higher precision.");
            rt=std::make_unique<RtAudio>(cfg.backend==Backend::Asio?RtAudio::WINDOWS_ASIO:RtAudio::WINDOWS_DS);
            RtAudioFormat formats[]={RTAUDIO_SINT16,RTAUDIO_SINT24,RTAUDIO_SINT32,RTAUDIO_FLOAT32};
            if(cfg.backend==Backend::Asio&&cfg.autoFormat){auto d=rt->getDeviceInfo(cfg.device);bool found=false;for(int i:{2,1,0,3})if(d.nativeFormats&formats[i]){cfg.format=Format(i);found=true;break;}if(!found)throw std::runtime_error("ASIO driver reports an unsupported native format");}
            if(cfg.format==Format::I24In32)throw std::runtime_error("24in32 is currently supported by WASAPI only");
            currentFormat=cfg.format;
            gen=std::make_unique<Generator>(cfg.rate,cfg.format);
            for(int attempt=0;attempt<3&&!quit;++attempt){
            tonelabAsioEvent=0;
            tonelabAsioExpectedRate=cfg.rate;
            RtAudio::StreamParameters output;output.deviceId=cfg.device;output.nChannels=2;output.firstChannel=0;unsigned frames=cfg.buffer;
            if(rt->openStream(&output,nullptr,formats[int(cfg.format)],cfg.rate,&frames,&Impl::callback,this)!=RTAUDIO_NO_ERROR)throw std::runtime_error(rt->getErrorText());
            if(rt->getStreamSampleRate()!=cfg.rate)throw std::runtime_error("Driver sample rate differs from requested rate");
            if(rt->startStream()!=RTAUDIO_NO_ERROR)throw std::runtime_error(rt->getErrorText());
            active=true;set(std::to_string(cfg.rate)+" Hz / "+formatName(cfg.format)+" / "+backendName(cfg.backend)+" / buffer "+std::to_string(frames)+(cfg.backend==Backend::DirectSound?" / SYSTEM MIXER":" / native driver format verified"));
            if(!announced){ready.set_value();announced=true;}
            int notification=0;
            while(!quit&&rt->isStreamRunning()){
                notification=tonelabAsioEvent.load();if(notification)break;
                if(panel.exchange(false)&&cfg.backend==Backend::Asio){if(ASIOControlPanel()!=ASE_OK)set("ASIO control panel could not be opened");}
                MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                Sleep(10);
            }
            if(quit)break;
            if(rt->isStreamRunning())rt->abortStream();
            if(rt->isStreamOpen())rt->closeStream();
            if(notification==1&&attempt<2){set("ASIO driver requested reset; reopening on the audio control thread...");Sleep(150);continue;}
            if(notification==2)set("ASIO external sample clock changed. Select the new rate and restart.");
            else set("ASIO stream stopped after repeated driver reset requests. Check enabled devices in the ASIO panel.");
            break;
            }
        }catch(...){if(!announced)ready.set_exception(std::current_exception());else{try{throw;}catch(const std::exception& e){set(e.what());}}}
        if(rt){if(rt->isStreamRunning())rt->abortStream();if(rt->isStreamOpen())rt->closeStream();rt.reset();}active=false;
    }
    void wasapi(Config cfg,std::wstring endpoint,std::promise<void> ready){
        bool announced=false;
        try {
            Com com; check(com.hr,"COM initialization");
            ComPtr<IMMDeviceEnumerator> e;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)),"Enumerator");
            ComPtr<IMMDevice> d;check(e->GetDevice(endpoint.c_str(),&d),"GetDevice");
            ComPtr<IAudioClient> client;check(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),"Activate");
            WAVEFORMATEXTENSIBLE f{};f.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;f.Format.nChannels=2;f.Format.nSamplesPerSec=cfg.rate;
            f.Format.wBitsPerSample=bytes(cfg.format)*8;f.Samples.wValidBitsPerSample=cfg.format==Format::I24In32?24:f.Format.wBitsPerSample;f.Format.nBlockAlign=2*bytes(cfg.format);f.Format.nAvgBytesPerSec=cfg.rate*f.Format.nBlockAlign;f.Format.cbSize=22;f.dwChannelMask=SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT;f.SubFormat=cfg.format==Format::F32?KSDATAFORMAT_SUBTYPE_IEEE_FLOAT:KSDATAFORMAT_SUBTYPE_PCM;
            if(client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,&f.Format,nullptr)!=S_OK){
                if(cfg.format==Format::I24In32)throw std::runtime_error("Exact exclusive format unsupported");
                f.Format.wFormatTag=cfg.format==Format::F32?WAVE_FORMAT_IEEE_FLOAT:WAVE_FORMAT_PCM;f.Format.cbSize=0;
                check(client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,&f.Format,nullptr),"Exact exclusive format");
            }
            REFERENCE_TIME period=0,minPeriod=0;check(client->GetDevicePeriod(&period,&minPeriod),"GetDevicePeriod");
            REFERENCE_TIME duration=std::max<REFERENCE_TIME>(minPeriod,10000000LL*cfg.buffer/cfg.rate);
            HRESULT init=client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,duration,duration,&f.Format,nullptr);
            if(init==AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED){
                UINT32 aligned=0;check(client->GetBufferSize(&aligned),"Aligned buffer size");
                duration=REFERENCE_TIME(10000000.0*aligned/cfg.rate+.5);client.Reset();
                check(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),"Reactivate aligned client");
                init=client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,duration,duration,&f.Format,nullptr);
            }
            check(init,"Exclusive Initialize");
            Event event;if(!event.handle)throw std::runtime_error("Cannot create audio event");check(client->SetEventHandle(event.handle),"SetEventHandle");Priority priority;
            UINT32 size=0;check(client->GetBufferSize(&size),"GetBufferSize");ComPtr<IAudioRenderClient> render;check(client->GetService(IID_PPV_ARGS(&render)),"Render service");
            BYTE* data=nullptr;check(render->GetBuffer(size,&data),"Prime buffer");this->render(data,size);check(render->ReleaseBuffer(size,0),"Prime release");check(client->Start(),"Start");
            active=true;set(std::string("Native stream: ")+std::to_string(cfg.rate)+" Hz / "+formatName(cfg.format)+" / stereo / WASAPI Exclusive / buffer "+std::to_string(size));ready.set_value();announced=true;
            auto last=std::chrono::steady_clock::now();
            try {while(!quit){DWORD wait=WaitForSingleObject(event.handle,100);if(quit)break;if(wait==WAIT_TIMEOUT)throw std::runtime_error("Audio device stopped signaling (disconnected or driver failure)");if(wait!=WAIT_OBJECT_0)throw std::runtime_error("Audio event wait failed");auto now=std::chrono::steady_clock::now();if(std::chrono::duration<double>(now-last).count()>2.0*size/cfg.rate)++underruns;last=now;check(render->GetBuffer(size,&data),"GetBuffer");this->render(data,size);check(render->ReleaseBuffer(size,0),"ReleaseBuffer");}}catch(...){client->Stop();throw;}
            client->Stop();active=false;
        }catch(...){active=false;if(!announced)ready.set_exception(std::current_exception());else {try{throw;}catch(const std::exception& ex){set(ex.what());}}}
    }
};
Audio::Audio():p_(new Impl(this)){}
Audio::~Audio(){stop();}
void Audio::stop(){p_->quit=true;if(p_->worker.joinable())p_->worker.join();p_->active=false;p_->gen.reset();}
void Audio::start(const Config& cfg){
    stop();for(auto& c:controls)validate(c.get(),cfg.rate);if(cfg.buffer<32||cfg.buffer>32768)throw std::runtime_error("Buffer must be 32..32768 frames");
    p_->quit=false;p_->underruns=0;p_->panel=false;
    std::promise<void> ready;auto future=ready.get_future();
    if(cfg.backend==Backend::Wasapi){
        auto list=devices(cfg.backend);auto it=std::find_if(list.begin(),list.end(),[&](const Device& d){return d.id==cfg.device;});
        if(it==list.end())throw std::runtime_error("Select an available output device");
        p_->gen=std::make_unique<Generator>(cfg.rate,cfg.format);
        p_->worker=std::thread(&Impl::wasapi,p_.get(),cfg,it->endpoint,std::move(ready));
    }else p_->worker=std::thread(&Impl::rtRun,p_.get(),cfg,std::move(ready));
    try{future.get();}catch(...){stop();throw;}
}
bool Audio::running()const{return p_->active;}
void Audio::asioPanel(){p_->panel=true;}
std::string Audio::status()const{std::lock_guard<std::mutex> lock(p_->mutex);return p_->message;}
uint64_t Audio::xruns()const{return p_->underruns;}
}
