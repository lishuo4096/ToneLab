#include "tone/ui.hpp"
#include "tone/audio.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <future>
#include <chrono>
using namespace tone;
namespace {
Audio audio;
Config config;
std::array<Channel,2> channel;
std::vector<Device> outputs;
int backend=0,selected=-1;
std::string error;
bool initialized=false,followWindows=true,haveDetails=false,haveVolume=false;
DeviceDetails details;
Volume volume;
std::future<std::vector<Device>> scan;
std::future<DeviceDetails> probe;
std::future<void> action;
std::future<Volume> volumeRead;
double lastVolume=0,lastProbe=0;
const ImVec4 teal(.24f,.86f,.73f,1),blue(.40f,.65f,1,1),muted(.52f,.59f,.68f,1);
template<class T> bool ready(std::future<T>& f){return f.valid()&&f.wait_for(std::chrono::seconds(0))==std::future_status::ready;}
bool busy(){return scan.valid()||probe.valid()||action.valid();}
void beginProbe(){
    haveDetails=false;haveVolume=false;error.clear();lastProbe=ImGui::GetTime();
    if(selected<0)return;
    auto d=outputs[selected];auto b=config.backend;
    probe=std::async(std::launch::async,[b,d]{return probeDevice(b,d);});
}
void refresh(){
    selected=-1;haveDetails=false;haveVolume=false;outputs.clear();error.clear();auto b=config.backend;
    scan=std::async(std::launch::async,[b]{audio.stop();return devices(b);});
}
void poll(){
    try{
        if(ready(scan)){outputs=scan.get();selected=outputs.empty()?-1:0;beginProbe();}
        if(ready(probe)){
            details=probe.get();haveDetails=true;
            if(!details.supported.empty()){
                auto choice=details.supported.front();bool chosen=false;
                if(followWindows&&details.hasWindowsDefault)for(auto f:details.supported)if(f.rate==details.windowsDefault.rate&&f.format==details.windowsDefault.format){choice=f;chosen=true;break;}
                if(!chosen)for(auto f:details.supported)if(f.rate==(followWindows?details.windowsDefault.rate:config.rate)){choice=f;chosen=true;if(f.format==config.format)break;}
                config.rate=choice.rate;config.format=choice.format;
                for(auto& c:channel)c.frequency=std::min(c.frequency,config.rate*.499);
            }
        }
        if(ready(action))action.get();
        if(ready(volumeRead)){volume=volumeRead.get();haveVolume=true;}
        if(!busy()&&!volumeRead.valid()&&!ImGui::IsAnyItemActive()&&selected>=0&&!outputs[selected].endpoint.empty()&&ImGui::GetTime()-lastVolume>1){auto id=outputs[selected].endpoint;lastVolume=ImGui::GetTime();volumeRead=std::async(std::launch::async,[id]{return deviceVolume(id);});}
        if(!busy()&&!volumeRead.valid()&&!audio.running()&&followWindows&&config.backend==Backend::Wasapi&&ImGui::GetTime()-lastProbe>4)beginProbe();
    }catch(const std::exception& e){error=e.what();}
}
void channelCard(int ch,float width){
    auto& c=channel[ch];auto color=ch==0?teal:blue;
    ImGui::PushID(ch);ImGui::BeginChild("channel",ImVec2(width,440),ImGuiChildFlags_Borders);
    ImGui::TextColored(color,ch==0?"01  /  LEFT CHANNEL":"02  /  RIGHT CHANNEL");ImGui::SameLine(ImGui::GetWindowWidth()-130);ImGui::Checkbox("Output ON",&c.enabled);
    ImGui::Spacing();
    float plot[240];for(int i=0;i<240;++i)plot[i]=float(waveform(c.wave,std::fmod(i*2.0/239,1.),std::clamp(c.frequency/config.rate,1e-9,.499))*c.amplitude*(c.enabled?1:0));
    ImGui::PushStyleColor(ImGuiCol_PlotLines,color);ImGui::PlotLines("##preview",plot,240,0,"2 cycles / signal preview",-1,1,ImVec2(-1,90));ImGui::PopStyleColor();
    ImGui::TextColored(muted,"WAVEFORM");int wave=int(c.wave);ImGui::SetNextItemWidth(-1);ImGui::Combo("##wave",&wave,"Sine\0Square (PolyBLEP)\0Saw (PolyBLEP)\0Triangle (band limited)\0");c.wave=Wave(wave);
    ImGui::Spacing();ImGui::TextColored(muted,"FREQUENCY / Hz");ImGui::SetNextItemWidth(-1);ImGui::InputDouble("##frequency",&c.frequency,1,100,"%.3f");
    c.frequency=std::clamp(std::isfinite(c.frequency)?c.frequency:1000.,.01,config.rate*.499);
    float freq=float(c.frequency);ImGui::SetNextItemWidth(-1);if(ImGui::SliderFloat("##freqslider",&freq,1,float(config.rate*.499),"%.1f Hz",ImGuiSliderFlags_Logarithmic))c.frequency=freq;
    ImGui::Spacing();ImGui::TextColored(muted,"AMPLITUDE / PEAK FULL SCALE");float gain=float(c.amplitude);ImGui::SetNextItemWidth(-1);if(ImGui::SliderFloat("##gain",&gain,0,1,"%.4f FS"))c.amplitude=gain;
    if(!c.enabled)ImGui::TextColored(muted,"OFF / digital zero output");
    else ImGui::TextColored(color,"%s  |  %.2f %%",c.amplitude>0?(std::to_string(20*std::log10(c.amplitude)).substr(0,6)+" dBFS").c_str():"-inf dBFS",100*c.amplitude);
    ImGui::EndChild();ImGui::PopID();
}
}
void setupTheme(){
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowPadding=ImVec2(24,20);s.FramePadding=ImVec2(12,6);s.ItemSpacing=ImVec2(12,7);s.WindowRounding=0;s.ChildRounding=14;s.FrameRounding=7;s.GrabRounding=7;s.PopupRounding=8;s.ScrollbarRounding=7;
    auto* c=s.Colors;c[ImGuiCol_WindowBg]=ImVec4(.045f,.06f,.085f,1);c[ImGuiCol_ChildBg]=ImVec4(.075f,.095f,.13f,1);c[ImGuiCol_Border]=ImVec4(.14f,.18f,.23f,1);c[ImGuiCol_FrameBg]=ImVec4(.105f,.135f,.18f,1);c[ImGuiCol_FrameBgHovered]=ImVec4(.15f,.2f,.26f,1);c[ImGuiCol_FrameBgActive]=ImVec4(.17f,.25f,.30f,1);c[ImGuiCol_Text]=ImVec4(.90f,.94f,.98f,1);c[ImGuiCol_TextDisabled]=muted;c[ImGuiCol_CheckMark]=teal;c[ImGuiCol_SliderGrab]=teal;c[ImGuiCol_SliderGrabActive]=teal;c[ImGuiCol_Button]=ImVec4(.13f,.30f,.30f,1);c[ImGuiCol_ButtonHovered]=ImVec4(.17f,.43f,.40f,1);c[ImGuiCol_ButtonActive]=ImVec4(.20f,.51f,.45f,1);c[ImGuiCol_Header]=ImVec4(.12f,.30f,.30f,1);c[ImGuiCol_HeaderHovered]=ImVec4(.17f,.38f,.37f,1);
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",18);
    ImFontConfig merge;merge.MergeMode=true;
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc",18,&merge,io.Fonts->GetGlyphRangesChineseFull());
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
}
void snapshotBackend(int value){backend=value;config.backend=value==2?Backend::Asio:value==1?Backend::DirectSound:Backend::Wasapi;}
bool snapshotReady(){return initialized&&!busy()&&haveDetails&&(config.backend!=Backend::Wasapi||haveVolume);}
void drawToneLab(){
    if(!initialized){refresh();initialized=true;}
    poll();
    auto* viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("ToneLab",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.65f);ImGui::TextUnformatted("ToneLab");ImGui::SetWindowFontScale(1);ImGui::SameLine();ImGui::SetCursorPosY(ImGui::GetCursorPosY()+9);ImGui::TextColored(teal,"PRECISION SIGNAL STUDIO");
    ImGui::TextColored(muted,"Stereo synthesis  /  Windows x64  /  Native-rate engine");ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    ImGui::BeginChild("routing",ImVec2(0,280),ImGuiChildFlags_Borders);
    ImGui::TextColored(muted,"OUTPUT ROUTING");
    ImGui::BeginDisabled(audio.running()||busy()||volumeRead.valid());
    ImGui::SetNextItemWidth(230);if(ImGui::Combo("##backend",&backend,"WASAPI Exclusive\0DirectSound\0ASIO\0")){config.backend=backend==0?Backend::Wasapi:backend==1?Backend::DirectSound:Backend::Asio;followWindows=true;refresh();}
    ImGui::SameLine();ImGui::SetNextItemWidth(std::max(160.f,ImGui::GetContentRegionAvail().x-115));
    if(ImGui::BeginCombo("##device",selected>=0?outputs[selected].name.c_str():"Scanning output devices...")){
        for(int i=0;i<int(outputs.size());++i)if(ImGui::Selectable(outputs[i].name.c_str(),i==selected)){selected=i;followWindows=true;beginProbe();}
        ImGui::EndCombo();
    }
    ImGui::SameLine();if(ImGui::Button("Refresh"))refresh();
    ImGui::BeginDisabled(!haveDetails||details.supported.empty());
    std::vector<unsigned> rates;for(auto f:details.supported)if(std::find(rates.begin(),rates.end(),f.rate)==rates.end())rates.push_back(f.rate);
    ImGui::SetNextItemWidth(180);
    if(ImGui::BeginCombo("Sample rate",(std::to_string(config.rate)+" Hz").c_str())){
        for(auto r:rates)if(ImGui::Selectable((std::to_string(r)+" Hz").c_str(),r==config.rate)){
            config.rate=r;followWindows=false;bool match=false;for(auto f:details.supported)if(f.rate==r&&f.format==config.format)match=true;
            if(!match)for(auto f:details.supported)if(f.rate==r){config.format=f.format;break;}
            for(auto& c:channel)c.frequency=std::min(c.frequency,config.rate*.499);
        }ImGui::EndCombo();
    }
    ImGui::SameLine();ImGui::SetNextItemWidth(220);
    if(ImGui::BeginCombo("Sample format",formatName(config.format))){for(auto f:details.supported)if(f.rate==config.rate)if(ImGui::Selectable(formatName(f.format),f.format==config.format)){config.format=f.format;followWindows=false;}ImGui::EndCombo();}
    int buffer=int(config.buffer);ImGui::SetNextItemWidth(180);if(ImGui::InputInt("Buffer frames",&buffer,128,512))config.buffer=std::clamp(buffer,32,32768);
    ImGui::SameLine();if(config.backend==Backend::Wasapi){if(ImGui::Checkbox("Follow Windows default",&followWindows)&&followWindows)beginProbe();}
    ImGui::EndDisabled();ImGui::EndDisabled();
    if(details.hasWindowsDefault&&haveDetails)ImGui::TextColored(muted,"Windows default: %u Hz / %s",details.windowsDefault.rate,formatName(details.windowsDefault.format));
    else ImGui::TextColored(muted,config.backend==Backend::Asio?"ASIO host selects the sample rate; output precision follows the driver's native type.":"System mixer path / PCM16 / hardware format not guaranteed");
    if(selected>=0&&!outputs[selected].endpoint.empty()){
        ImGui::BeginDisabled(!haveVolume||action.valid()||volumeRead.valid());float level=volume.level*100;ImGui::SetNextItemWidth(260);bool changed=ImGui::SliderFloat("Device volume",&level,0,100,"%.0f %%");
        if(changed)volume.level=level/100;
        bool commit=ImGui::IsItemDeactivatedAfterEdit();ImGui::SameLine();commit=ImGui::Checkbox("Device mute",&volume.muted)||commit;
        if(commit){auto id=outputs[selected].endpoint;auto value=volume;action=std::async(std::launch::async,[id,value]{setDeviceVolume(id,value.level,value.muted);});}
        ImGui::EndDisabled();
        ImGui::TextColored(muted,!haveVolume?"Reading endpoint volume...":volume.hardware?"Endpoint hardware volume: applies to exclusive output; mirrors Windows volume.":"Software endpoint volume: may not affect exclusive output. Channel gain remains available.");
    }else ImGui::TextColored(muted,"ASIO device volume is managed by its driver / control panel.");
    if(busy())ImGui::TextColored(teal,"Working in background... UI and channel controls remain responsive.");
    ImGui::EndChild();
    float width=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)/2;channelCard(0,width);ImGui::SameLine();channelCard(1,width);
    for(int i=0;i<2;++i)audio.controls[i].set(channel[i]);
    ImGui::Spacing();bool playing=audio.running();
    ImGui::BeginDisabled(busy()||(!playing&&(!haveDetails||details.supported.empty())));
    if(ImGui::Button(playing?"STOP OUTPUT":"START OUTPUT",ImVec2(200,44))){
        error.clear();if(playing)action=std::async(std::launch::async,[]{audio.stop();});
        else if(selected>=0){config.device=outputs[selected].id;config.autoFormat=false;auto cfg=config;action=std::async(std::launch::async,[cfg]{audio.start(cfg);});}
    }
    ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button("Mute both",ImVec2(125,44)))for(auto& c:channel)c.enabled=false;
    ImGui::SameLine();ImGui::TextColored(audio.running()?teal:muted,"%s   |   Underruns: %llu",audio.running()?"LIVE":"STOPPED",static_cast<unsigned long long>(audio.xruns()));
    if(config.backend==Backend::Asio){ImGui::SameLine();ImGui::BeginDisabled(!playing);if(ImGui::Button("ASIO panel",ImVec2(120,44)))audio.asioPanel();ImGui::EndDisabled();}
    ImGui::TextWrapped("%s",audio.status().c_str());if(!error.empty()){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.48f,.42f,1));ImGui::TextWrapped("%s",error.c_str());ImGui::PopStyleColor();}
    ImGui::TextColored(muted,"Frequency and gain update live. Stop output to change the device or stream format.");
    ImGui::End();
}
