#include "analyzer/capture.hpp"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <avrt.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <sstream>
#include <filesystem>
#include <cmath>
namespace analyzer {
using Microsoft::WRL::ComPtr;
static void check(HRESULT h,const char* op){if(FAILED(h)){std::ostringstream s;s<<op<<": HRESULT 0x"<<std::hex<<uint32_t(h);throw std::runtime_error(s.str());}}
struct Com {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Com(){if(SUCCEEDED(hr))CoUninitialize();}};
static std::string utf8(const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;}
std::vector<InputDevice> inputDevices(){Com com;ComPtr<IMMDeviceEnumerator> e;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)),"Enumerator");ComPtr<IMMDeviceCollection> list;check(e->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&list),"Inputs");UINT n=0;check(list->GetCount(&n),"Input count");std::vector<InputDevice> result;
    for(UINT i=0;i<n;++i){ComPtr<IMMDevice> d;check(list->Item(i,&d),"Input");LPWSTR id=nullptr;check(d->GetId(&id),"Input ID");InputDevice x;x.id=i;x.endpoint=id;CoTaskMemFree(id);ComPtr<IPropertyStore> props;check(d->OpenPropertyStore(STGM_READ,&props),"Properties");PROPVARIANT v;PropVariantInit(&v);check(props->GetValue(PKEY_Device_FriendlyName,&v),"Input name");x.name=v.vt==VT_LPWSTR?utf8(v.pwszVal):"Audio input";PropVariantClear(&v);ComPtr<IAudioClient> c;if(SUCCEEDED(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(c.GetAddressOf())))){WAVEFORMATEX* f=nullptr;if(SUCCEEDED(c->GetMixFormat(&f))){x.defaultRate=f->nSamplesPerSec;CoTaskMemFree(f);}}result.push_back(x);}
    return result;
}
struct Capture::Impl {
    mutable std::mutex mutex;std::thread worker;std::atomic<bool> quit{false},active{false},rec{false};std::atomic<unsigned> sampleRate{0};std::atomic<uint64_t> gaps{0};
    std::string message="Input stopped";std::vector<double> ring;uint64_t written=0;Recording saved;size_t recordLimit=0;
    void set(std::string s){std::lock_guard<std::mutex> lock(mutex);message=std::move(s);}
    void run(CaptureConfig cfg,std::wstring endpoint,std::promise<void> ready){bool announced=false;
        try{Com com;check(com.hr,"COM");ComPtr<IMMDeviceEnumerator> e;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)),"Enumerator");ComPtr<IMMDevice> d;check(e->GetDevice(endpoint.c_str(),&d),"Get input");ComPtr<IAudioClient> client;auto activate=[&]{check(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),"Capture activate");};activate();
            WAVEFORMATEXTENSIBLE exclusive{};WAVEFORMATEX* mix=nullptr;WAVEFORMATEX* format=nullptr;
            struct MixCleanup{WAVEFORMATEX*& f;~MixCleanup(){CoTaskMemFree(f);}}cleanup{mix};
            if(cfg.exclusive){bool found=false;for(auto encoding:{std::pair<unsigned,unsigned>{32,32},{32,24},{24,24},{16,16}}){exclusive={};auto& f=exclusive.Format;f.wFormatTag=WAVE_FORMAT_EXTENSIBLE;f.cbSize=22;f.nChannels=2;f.nSamplesPerSec=cfg.rate;f.wBitsPerSample=encoding.first;f.nBlockAlign=2*encoding.first/8;f.nAvgBytesPerSec=f.nSamplesPerSec*f.nBlockAlign;exclusive.Samples.wValidBitsPerSample=encoding.second;exclusive.dwChannelMask=3;exclusive.SubFormat=KSDATAFORMAT_SUBTYPE_PCM;if(client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,&f,nullptr)==S_OK){found=true;break;}}
                if(!found)throw std::runtime_error("Input does not support requested native stereo rate / PCM format");
                format=&exclusive.Format;
            }else{check(client->GetMixFormat(&mix),"Input Windows mix format");format=mix;}
            bool fp=format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;if(format->wFormatTag==WAVE_FORMAT_EXTENSIBLE)fp=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            if(format->nChannels>32||format->nChannels==0||(fp?format->wBitsPerSample!=32&&format->wBitsPerSample!=64:format->wBitsPerSample!=16&&format->wBitsPerSample!=24&&format->wBitsPerSample!=32))throw std::runtime_error("Unsupported input sample encoding");
            REFERENCE_TIME period=std::max<REFERENCE_TIME>(100000,10000000LL*cfg.buffer/format->nSamplesPerSec);auto mode=cfg.exclusive?AUDCLNT_SHAREMODE_EXCLUSIVE:AUDCLNT_SHAREMODE_SHARED;
            HRESULT init=client->Initialize(mode,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,cfg.exclusive?period:0,cfg.exclusive?period:0,format,nullptr);
            if(init==AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED&&cfg.exclusive){UINT32 aligned=0;check(client->GetBufferSize(&aligned),"Input alignment");period=REFERENCE_TIME(10000000.*aligned/format->nSamplesPerSec+.5);client.Reset();activate();init=client->Initialize(mode,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,period,period,format,nullptr);}check(init,"Capture initialize");
            struct Event{HANDLE h=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(h)CloseHandle(h);}}event;if(!event.h)throw std::runtime_error("Capture event creation failed");check(client->SetEventHandle(event.h),"Input event");ComPtr<IAudioCaptureClient> capture;check(client->GetService(IID_PPV_ARGS(&capture)),"Capture service");
            sampleRate=format->nSamplesPerSec;{std::lock_guard<std::mutex> lock(mutex);ring.assign(2097152*2,0);written=0;saved={};recordLimit=0;rec=false;}
            DWORD task=0;HANDLE priority=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);struct Priority{HANDLE h;~Priority(){if(h)AvRevertMmThreadCharacteristics(h);}}pr{priority};
            check(client->Start(),"Capture start");active=true;set(std::to_string(sampleRate.load())+" Hz / "+std::to_string(format->wBitsPerSample)+"-bit "+(fp?"float":"PCM")+(cfg.exclusive?" / WASAPI native exclusive":" / WASAPI shared (Windows mix format)"));ready.set_value();announced=true;
            try{while(!quit){DWORD wait=WaitForSingleObject(event.h,std::max(500u,unsigned(3000.*cfg.buffer/format->nSamplesPerSec+100)));if(quit)break;if(wait!=WAIT_OBJECT_0)throw std::runtime_error("Input stopped signaling or device disconnected");UINT32 frames=0;check(capture->GetNextPacketSize(&frames),"Input packet");while(frames){BYTE* data=nullptr;DWORD flags=0;check(capture->GetBuffer(&data,&frames,&flags,nullptr,nullptr),"Input buffer");if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)++gaps;
                    {std::lock_guard<std::mutex> lock(mutex);for(UINT32 i=0;i<frames;++i)for(unsigned ch=0;ch<2;++ch){double value=0;if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT)){unsigned sourceCh=std::min<unsigned>(ch,format->nChannels-1);auto src=data+i*format->nBlockAlign+sourceCh*format->wBitsPerSample/8;if(fp){if(format->wBitsPerSample==32){float v;memcpy(&v,src,4);value=v;}else memcpy(&value,src,8);}else{unsigned size=format->wBitsPerSample/8;uint32_t v=0;for(unsigned b=0;b<size;++b)v|=uint32_t(src[b])<<(8*b);if(size<4&&(src[size-1]&128))v|=uint32_t(-1)<<(size*8);value=int32_t(v)/std::ldexp(1.,format->wBitsPerSample-1);}}
                            ring[written++%ring.size()]=value;if(rec&&saved.samples.size()<recordLimit)saved.samples.push_back(value);}
                        if(rec&&saved.samples.size()>=recordLimit)rec=false;}
                    check(capture->ReleaseBuffer(frames),"Release capture");check(capture->GetNextPacketSize(&frames),"Next input packet");}
                }}catch(...){client->Stop();throw;}client->Stop();active=false;rec=false;
        }catch(...){active=false;rec=false;if(!announced)ready.set_exception(std::current_exception());else{try{throw;}catch(const std::exception& e){set(e.what());}}}
    }
};
Capture::Capture():p_(new Impl){}Capture::~Capture(){stop();}
void Capture::start(CaptureConfig cfg){stop();auto list=inputDevices();auto d=std::find_if(list.begin(),list.end(),[&](auto& x){return x.id==cfg.device;});if(d==list.end())throw std::runtime_error("Input device not found");p_->quit=false;p_->gaps=0;std::promise<void> ready;auto f=ready.get_future();p_->worker=std::thread(&Impl::run,p_.get(),cfg,d->endpoint,std::move(ready));try{f.get();}catch(...){stop();throw;}}
void Capture::stop(){p_->quit=true;if(p_->worker.joinable())p_->worker.join();p_->active=false;p_->rec=false;}
bool Capture::running()const{return p_->active;}unsigned Capture::rate()const{return p_->sampleRate;}uint64_t Capture::discontinuities()const{return p_->gaps;}
std::string Capture::status()const{std::lock_guard<std::mutex> lock(p_->mutex);return p_->message;}
Recording Capture::latest(unsigned frames)const{Recording r;r.rate=rate();r.channels=2;std::lock_guard<std::mutex> lock(p_->mutex);size_t count=std::min<uint64_t>(std::min<uint64_t>(uint64_t(frames)*2,p_->written),p_->ring.size());r.samples.resize(count);for(size_t i=0;i<count;++i)r.samples[i]=p_->ring[(p_->written-count+i)%p_->ring.size()];return r;}
void Capture::beginRecording(double seconds){if(!running()||!std::isfinite(seconds)||seconds<=0||seconds>30||seconds*rate()*2>16777216)throw std::runtime_error("Recording requires active input and <=30 seconds / 128 MiB");std::lock_guard<std::mutex> lock(p_->mutex);p_->saved={rate(),2,{}};p_->recordLimit=size_t(seconds*rate())*2;p_->saved.samples.reserve(p_->recordLimit);p_->rec=true;}
bool Capture::recording()const{return p_->rec;}Recording Capture::recorded()const{std::lock_guard<std::mutex> lock(p_->mutex);return p_->saved;}
struct Source::Impl {HANDLE process=nullptr;std::string message="Source stopped";};
Source::Source():p_(new Impl){}Source::~Source(){stop();}
void Source::start(const std::wstring& args){stop();wchar_t module[32768];GetModuleFileNameW(nullptr,module,32768);auto exe=std::filesystem::path(module).parent_path()/L"tonelab-cli.exe";if(!std::filesystem::exists(exe))throw std::runtime_error("Place tonelab-cli.exe beside ToneScope.exe");std::wstring cmd=L"\""+exe.wstring()+L"\" play "+args;STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi))throw std::runtime_error("Cannot start ToneLab CLI");p_->process=pi.hProcess;CloseHandle(pi.hThread);p_->message="ToneLab CLI source running (owned child process)";}
void Source::stop(){if(p_->process){if(WaitForSingleObject(p_->process,0)==WAIT_TIMEOUT)TerminateProcess(p_->process,0);WaitForSingleObject(p_->process,1000);CloseHandle(p_->process);p_->process=nullptr;}p_->message="Source stopped";}
bool Source::running()const{return p_->process&&WaitForSingleObject(p_->process,0)==WAIT_TIMEOUT;}
std::string Source::status()const{if(p_->process&&!running()){DWORD code=0;GetExitCodeProcess(p_->process,&code);return "ToneLab CLI exited with code "+std::to_string(code)+" (check device, rate and native format)";}return p_->message;}
}
