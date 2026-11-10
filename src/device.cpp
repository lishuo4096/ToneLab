#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "tone/audio.hpp"
#include "RtAudio.h"
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
#include <endpointvolume.h>
#include <cstring>
namespace tone {
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr,const char* operation) {if(FAILED(hr)){std::ostringstream s;s<<operation<<" failed (0x"<<std::hex<<uint32_t(hr)<<"). Device may be busy or format unsupported.";throw std::runtime_error(s.str());}}
struct Com { HRESULT hr; explicit Com(bool sta=false):hr(CoInitializeEx(nullptr,sta?COINIT_APARTMENTTHREADED:COINIT_MULTITHREADED)){} ~Com(){if(SUCCEEDED(hr))CoUninitialize();} };
static std::string utf8(const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;}
static std::string fromAcp(const std::string& s){int n=MultiByteToWideChar(CP_ACP,0,s.c_str(),-1,nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_ACP,0,s.c_str(),-1,w.data(),n);return utf8(w.c_str());}
Backend parseBackend(const std::string& s){if(s=="wasapi")return Backend::Wasapi;if(s=="ds"||s=="directsound")return Backend::DirectSound;if(s=="asio")return Backend::Asio;throw std::runtime_error("Backend must be wasapi, ds or asio");}
const char* backendName(Backend b){return b==Backend::Wasapi?"WASAPI Exclusive":b==Backend::Asio?"ASIO":"DirectSound";}
static RtAudio::Api api(Backend b){return b==Backend::Asio?RtAudio::WINDOWS_ASIO:RtAudio::WINDOWS_DS;}
std::vector<Device> devices(Backend b){
    Com com(b==Backend::Asio); std::vector<Device> result;
    if(b!=Backend::Wasapi){RtAudio a(api(b));for(auto id:a.getDeviceIds()){auto d=a.getDeviceInfo(id);if(d.outputChannels>=2)result.push_back({id,b==Backend::DirectSound?fromAcp(d.name):d.name,{},d.sampleRates,unsigned(d.nativeFormats),d.currentSampleRate?d.currentSampleRate:d.preferredSampleRate});}return result;}
    ComPtr<IMMDeviceEnumerator> e;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)),"Enumerator");
    ComPtr<IMMDeviceCollection> list;check(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&list),"EnumAudioEndpoints");UINT n=0;check(list->GetCount(&n),"Device count");
    for(UINT i=0;i<n;++i){ComPtr<IMMDevice> d;check(list->Item(i,&d),"Endpoint");LPWSTR id=nullptr;check(d->GetId(&id),"Endpoint ID");std::wstring endpoint=id;CoTaskMemFree(id);ComPtr<IPropertyStore> props;check(d->OpenPropertyStore(STGM_READ,&props),"Device properties");PROPVARIANT v;PropVariantInit(&v);check(props->GetValue(PKEY_Device_FriendlyName,&v),"Device name");std::string name=v.vt==VT_LPWSTR?utf8(v.pwszVal):"Audio device";PropVariantClear(&v);result.push_back({i,name,endpoint,{}});}
    return result;
}
static ComPtr<IMMDevice> endpointDevice(const std::wstring& id){ComPtr<IMMDeviceEnumerator> e;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)),"Enumerator");ComPtr<IMMDevice> d;check(e->GetDevice(id.c_str(),&d),"GetDevice");return d;}
static StreamFormat decodeFormat(const WAVEFORMATEX* f){
    unsigned valid=f->wBitsPerSample;bool fp=f->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
    if(f->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&f->cbSize>=22){auto x=reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);valid=x->Samples.wValidBitsPerSample;fp=x->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;}
    Format format=fp?Format::F32:valid==16?Format::I16:valid==24?(f->wBitsPerSample==32?Format::I24In32:Format::I24):Format::I32;
    return {f->nSamplesPerSec,format};
}
DeviceDetails probeDevice(Backend b,const Device& device){
    DeviceDetails details;
    if(b!=Backend::Wasapi){
        RtAudioFormat formats[]={RTAUDIO_SINT16,RTAUDIO_SINT24,RTAUDIO_SINT32,RTAUDIO_FLOAT32};
        for(auto rate:device.rates)for(int i=0;i<4;++i)if(b==Backend::DirectSound?i==0:bool(device.nativeFormats&formats[i]))details.supported.push_back({rate,Format(i)});
        details.windowsDefault.rate=device.preferredRate?device.preferredRate:48000;
        details.note=b==Backend::Asio?"ASIOCanSampleRate + native output type; host selects rate. Driver may reconfigure after opening.":"DirectSound uses system mixer; rate list is a candidate list.";
        return details;
    }
    Com com;auto d=endpointDevice(device.endpoint);ComPtr<IAudioClient> client;check(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),"Activate");
    // PKEY_AudioEngine_DeviceFormat is the Control Panel device format, not float mix format.
    const PROPERTYKEY key={{0xf19f064d,0x082c,0x4e27,{0xbc,0x73,0x68,0x82,0xa1,0xbb,0x8e,0x4c}},0};
    ComPtr<IPropertyStore> props;check(d->OpenPropertyStore(STGM_READ,&props),"Device format properties");PROPVARIANT v;PropVariantInit(&v);
    if(SUCCEEDED(props->GetValue(key,&v))&&v.vt==VT_BLOB&&v.blob.cbSize>=sizeof(WAVEFORMATEX)){
        auto f=reinterpret_cast<const WAVEFORMATEX*>(v.blob.pBlobData);
        if(v.blob.cbSize>=sizeof(WAVEFORMATEX)+f->cbSize){details.windowsDefault=decodeFormat(f);details.hasWindowsDefault=true;}
    }
    PropVariantClear(&v);
    if(!details.hasWindowsDefault){WAVEFORMATEX* f=nullptr;check(client->GetMixFormat(&f),"GetMixFormat");details.windowsDefault=decodeFormat(f);CoTaskMemFree(f);details.note="Windows mix format (device default unavailable).";details.hasWindowsDefault=true;}
    std::vector<unsigned> rates={8000,11025,16000,22050,32000,44100,48000,64000,88200,96000,128000,176400,192000,256000,352800,384000,512000,705600,768000};
    rates.push_back(details.windowsDefault.rate);std::sort(rates.begin(),rates.end());rates.erase(std::unique(rates.begin(),rates.end()),rates.end());
    for(auto rate:rates)for(auto format:{Format::I16,Format::I24,Format::I32,Format::F32,Format::I24In32}){
        WAVEFORMATEXTENSIBLE f{};f.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;f.Format.cbSize=22;f.Format.nChannels=2;f.Format.nSamplesPerSec=rate;f.Format.wBitsPerSample=bytes(format)*8;f.Samples.wValidBitsPerSample=format==Format::I24In32?24:f.Format.wBitsPerSample;f.Format.nBlockAlign=2*bytes(format);f.Format.nAvgBytesPerSec=rate*f.Format.nBlockAlign;f.dwChannelMask=SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT;f.SubFormat=format==Format::F32?KSDATAFORMAT_SUBTYPE_IEEE_FLOAT:KSDATAFORMAT_SUBTYPE_PCM;
        bool supported=client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,&f.Format,nullptr)==S_OK;
        if(!supported&&format!=Format::I24In32){f.Format.wFormatTag=format==Format::F32?WAVE_FORMAT_IEEE_FLOAT:WAVE_FORMAT_PCM;f.Format.cbSize=0;supported=client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,&f.Format,nullptr)==S_OK;}
        if(supported)details.supported.push_back({rate,format});
    }
    return details;
}
static ComPtr<IAudioEndpointVolume> volumeEndpoint(const std::wstring& id){auto d=endpointDevice(id);ComPtr<IAudioEndpointVolume> v;check(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(v.GetAddressOf())),"Endpoint volume");return v;}
Volume deviceVolume(const std::wstring& endpoint){Com com;auto v=volumeEndpoint(endpoint);Volume result;BOOL muted=FALSE;DWORD support=0;check(v->GetMasterVolumeLevelScalar(&result.level),"Get volume");check(v->GetMute(&muted),"Get mute");check(v->QueryHardwareSupport(&support),"Volume hardware support");result.muted=muted;result.hardware=(support&ENDPOINT_HARDWARE_SUPPORT_VOLUME)!=0;return result;}
void setDeviceVolume(const std::wstring& endpoint,float level,bool muted){Com com;auto v=volumeEndpoint(endpoint);check(v->SetMasterVolumeLevelScalar(std::clamp(level,0.f,1.f),nullptr),"Set device volume");check(v->SetMute(muted,nullptr),"Set device mute");}
}
