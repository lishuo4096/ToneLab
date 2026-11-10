// Physical DAC -> ADC regression; never drives the desktop or changes system settings.
#include "tone/audio.hpp"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace tone;
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr,const char* op){if(FAILED(hr)){char text[160];snprintf(text,sizeof text,"%s: HRESULT 0x%08lx",op,(unsigned long)hr);throw std::runtime_error(text);}}
static double decode(const BYTE* p,Format f){if(f==Format::F32){float v;memcpy(&v,p,4);return v;}int size=bytes(f);int32_t value=0;for(int i=0;i<size;++i)value|=uint32_t(p[i])<<(8*i);if(size<4&&p[size-1]&128)value|=uint32_t(-1)<<(size*8);return value/std::ldexp(1.,size*8-1);}
static double rms(const std::vector<double>& s){double sum=0;for(double x:s)sum+=x*x;return s.empty()?0:std::sqrt(sum/s.size());}
static double amplitude(const std::vector<double>& s,double freq,unsigned rate){std::complex<double> sum{},osc(1,0),step=std::polar(1.,-2*3.141592653589793*freq/rate);for(double x:s){sum+=x*osc;osc*=step;}return s.empty()?0:2*std::abs(sum)/s.size();}
int main(int argc,char** argv){
    SetConsoleOutputCP(CP_UTF8);CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct ComCleanup { ~ComCleanup(){CoUninitialize();} } comCleanup;
    HANDLE event=nullptr;
    try{
        unsigned rate=argc>1?unsigned(std::stoul(argv[1])):384000;Backend backend=argc>2?parseBackend(argv[2]):Backend::Wasapi;
        auto outputs=devices(backend);auto out=outputs.end();for(auto i=outputs.begin();i!=outputs.end();++i)if(i->name.find("CH32V305")!=std::string::npos){out=i;break;}if(out==outputs.end()&&backend==Backend::Asio&&!outputs.empty())out=outputs.begin();if(out==outputs.end())throw std::runtime_error("CH32V305 playback device not found");
        ComPtr<IMMDeviceEnumerator> e;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)),"Enumerator");ComPtr<IMMDeviceCollection> list;check(e->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&list),"Capture endpoints");UINT count=0;list->GetCount(&count);ComPtr<IMMDevice> input;
        for(UINT i=0;i<count;++i){ComPtr<IMMDevice> d;list->Item(i,&d);ComPtr<IPropertyStore> props;d->OpenPropertyStore(STGM_READ,&props);PROPVARIANT name;PropVariantInit(&name);props->GetValue(PKEY_Device_FriendlyName,&name);bool match=name.vt==VT_LPWSTR&&wcsstr(name.pwszVal,L"CH32V305");PropVariantClear(&name);if(match){input=d;break;}}
        if(!input)throw std::runtime_error("CH32V305 physical capture endpoint not found");
        ComPtr<IAudioClient> client;check(input->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),"Capture activate");
        WAVEFORMATEXTENSIBLE f{};Format captureFormat=Format::I32;bool supported=false;
        for(auto format:{Format::I32,Format::I24In32,Format::I24,Format::I16,Format::F32}){
            f={};f.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;f.Format.cbSize=22;f.Format.nChannels=2;f.Format.nSamplesPerSec=rate;f.Format.wBitsPerSample=bytes(format)*8;f.Format.nBlockAlign=2*bytes(format);f.Format.nAvgBytesPerSec=rate*f.Format.nBlockAlign;f.Samples.wValidBitsPerSample=format==Format::I24In32?24:f.Format.wBitsPerSample;f.dwChannelMask=3;f.SubFormat=format==Format::F32?KSDATAFORMAT_SUBTYPE_IEEE_FLOAT:KSDATAFORMAT_SUBTYPE_PCM;
            if(client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,&f.Format,nullptr)==S_OK){captureFormat=format;supported=true;break;}
        }
        if(!supported)throw std::runtime_error("Physical capture does not support test sample rate");
        REFERENCE_TIME period=100000;HRESULT init=client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,period,period,&f.Format,nullptr);
        if(init==AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED){UINT32 size=0;client->GetBufferSize(&size);period=REFERENCE_TIME(10000000.0*size/rate+.5);client.Reset();check(input->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),"Capture reactivate");init=client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,period,period,&f.Format,nullptr);}check(init,"Capture initialize");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(client->SetEventHandle(event),"Capture event");ComPtr<IAudioCaptureClient> capture;check(client->GetService(IID_PPV_ARGS(&capture)),"Capture service");
        Audio audio;Config cfg;cfg.backend=backend;cfg.device=out->id;cfg.rate=rate;cfg.buffer=4096;cfg.format=backend==Backend::DirectSound?Format::I16:Format::I32;
        std::array<Channel,2> channels{{{997,.05,Wave::Sine,true},{2003,.05,Wave::Sine,true}}};for(int ch=0;ch<2;++ch)audio.controls[ch].set(channels[ch]);
        check(client->Start(),"Capture start");audio.start(cfg);std::cout<<audio.status()<<"\nCapture: "<<rate<<" Hz / "<<formatName(captureFormat)<<"\n";
        std::array<std::array<std::vector<double>,2>,4> samples;
        auto begin=std::chrono::steady_clock::now();int previous=-1;unsigned discontinuities=0;
        for(;;){double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();int stage=int(elapsed/.8);if(stage>=4)break;
            if(stage!=previous){channels[0].enabled=stage==0||stage==3;channels[1].enabled=stage==0||stage==1;for(int ch=0;ch<2;++ch)audio.controls[ch].set(channels[ch]);previous=stage;}
            DWORD wait=WaitForSingleObject(event,200);if(wait!=WAIT_OBJECT_0)throw std::runtime_error("Capture event timeout");
            UINT32 frames=0;check(capture->GetNextPacketSize(&frames),"Capture packet");while(frames){BYTE* data=nullptr;DWORD flags=0;check(capture->GetBuffer(&data,&frames,&flags,nullptr,nullptr),"Capture buffer");if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)++discontinuities;
                double position=elapsed-stage*.8;
                if(position>.25&&position<.70)for(UINT32 i=0;i<frames;++i)for(int ch=0;ch<2;++ch)samples[stage][ch].push_back(flags&AUDCLNT_BUFFERFLAGS_SILENT?0:decode(data+(i*2+ch)*bytes(captureFormat),captureFormat));
                capture->ReleaseBuffer(frames);check(capture->GetNextPacketSize(&frames),"Next packet");
            }
        }
        audio.stop();client->Stop();bool passed=true;
        std::ofstream report("docs/loopback-"+std::to_string(rate)+"-"+(backend==Backend::Asio?"asio":"wasapi")+".json");report<<"{\n  \"rate\": "<<rate<<",\n  \"capture_discontinuities\": "<<discontinuities<<",\n  \"output_underruns\": "<<audio.xruns()<<",\n  \"stages\": [\n";
        for(int stage=0;stage<4;++stage){if(stage)report<<",\n";report<<"    [";for(int ch=0;ch<2;++ch){auto& s=samples[stage][ch];double r=rms(s),own=amplitude(s,ch==0?997:2003,rate),other=amplitude(s,ch==0?2003:997,rate);bool on=stage==0||(stage==1&&ch==1)||(stage==3&&ch==0);bool valid=s.size()>rate*.1&&(on?(r>1e-5&&own>other*3):(r<rms(samples[0][ch])*.10+1e-5));passed&=valid;
            std::cout<<"stage="<<stage<<" channel="<<ch<<" samples="<<s.size()<<" RMS="<<r<<" own_tone="<<own<<" other_tone="<<other<<" "<<(valid?"PASS":"FAIL")<<'\n';if(ch)report<<",";report<<"{\"channel\":"<<ch<<",\"rms\":"<<r<<",\"own_tone\":"<<own<<",\"other_tone\":"<<other<<",\"pass\":"<<(valid?"true":"false")<<"}";
        }report<<"]";}report<<"\n  ],\n  \"pass\": "<<(passed?"true":"false")<<"\n}\n";
        CloseHandle(event);event=nullptr;std::cout<<(passed?"PASS":"FAIL")<<": physical loopback, independent tones, left OFF, both OFF, left ON/right OFF.\n";return passed?0:1;
    }catch(const std::exception& e){if(event)CloseHandle(event);std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
