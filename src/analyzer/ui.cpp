#include "analyzer/ui.hpp"
#include "analyzer/analysis.hpp"
#include "analyzer/capture.hpp"
#include "tone/audio.hpp"
#include "imgui.h"
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <sstream>
using namespace analyzer;
namespace {
Capture input;Source source;Settings settings;CaptureConfig captureConfig;
std::vector<InputDevice> inputs;std::vector<tone::Device> outputs;int selectedInput=0,selectedOutput=0,backend=0,drive=0;
Recording recording;std::array<Result,2> results;bool initialized=false,live=false,residual=false,logFrequency=true,snapshotLive=false;
double recordSeconds=3,segmentStart=0,segmentDuration=0,toneFrequency=997,amplitude=.05,lastAnalysis=0;
float minDb=-170,maxDb=0,viewLow=20,viewHigh=20000;int fftIndex=2;std::string message="Ready",error,origin="Synthetic demo";
struct Work {Recording recording;std::array<Result,2> result;std::string name;};
struct Plan {unsigned size=0;double low=0,high=0,frequency=0;unsigned harmonics=0;std::unique_ptr<Processor> p;};Plan plan;
std::future<Work> work;std::future<void> action;std::future<std::vector<InputDevice>> inputScan;std::future<std::vector<tone::Device>> outputScan;
const ImVec4 green(.23f,.86f,.73f,1),blue(.40f,.65f,1,1),muted(.53f,.61f,.7f,1);
template<class T>bool ready(std::future<T>& f){return f.valid()&&f.wait_for(std::chrono::seconds(0))==std::future_status::ready;}
std::string utf8(const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;}
std::string dialog(bool save,const wchar_t* filter,const wchar_t* extension){wchar_t path[32768]={};OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=GetActiveWindow();f.lpstrFilter=filter;f.lpstrFile=path;f.nMaxFile=32768;f.lpstrDefExt=extension;f.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);return (save?GetSaveFileNameW(&f):GetOpenFileNameW(&f))?utf8(path):"";}
void analyzeFile(std::string path){if(work.valid())return;live=false;auto s=settings;double first=segmentStart,duration=segmentDuration;work=std::async(std::launch::async,[path,s,first,duration]{auto rec=loadWav(path);auto data=analyzeRecording(rec,s,first,duration);return Work{std::move(rec),std::move(data),path};});message="Loading and analyzing WAV...";}
void offline(){if(work.valid())return;if(live&&input.running()){recording=input.latest(settings.fftSize);origin="Frozen physical input block";segmentStart=0;segmentDuration=0;}if(recording.samples.empty())return;auto rec=recording;auto s=settings;double first=segmentStart,duration=segmentDuration;auto name=origin;work=std::async(std::launch::async,[rec=std::move(rec),s,first,duration,name]()mutable{auto data=analyzeRecording(rec,s,first,duration);return Work{std::move(rec),std::move(data),name};});live=false;message="Precision analysis in background...";}
void poll(){try{
    if(ready(inputScan)){inputs=inputScan.get();selectedInput=0;for(int i=0;i<int(inputs.size());++i)if(inputs[i].name.find("CH32V305")!=std::string::npos)selectedInput=i;if(snapshotLive&&!inputs.empty()){captureConfig.device=inputs[selectedInput].id;auto cfg=captureConfig;live=true;action=std::async(std::launch::async,[cfg]{input.start(cfg);});}}
    if(ready(outputScan)){outputs=outputScan.get();selectedOutput=0;for(int i=0;i<int(outputs.size());++i)if(outputs[i].name.find("CH32V305")!=std::string::npos)selectedOutput=i;}
    if(ready(action)){action.get();message="Input operation complete";}
    if(ready(work)){auto w=work.get();results=std::move(w.result);if(!w.recording.samples.empty())recording=std::move(w.recording);origin=w.name;message="Analysis ready";}
    if(live&&input.running()&&!work.valid()&&ImGui::GetTime()-lastAnalysis>.1){auto rec=input.latest(settings.fftSize);if(rec.samples.size()>=settings.fftSize*2){auto s=settings;lastAnalysis=ImGui::GetTime();work=std::async(std::launch::async,[rec=std::move(rec),s]()mutable{
        if(!plan.p||plan.size!=s.fftSize||plan.low!=s.low||plan.high!=s.high||plan.frequency!=s.expectedTone||plan.harmonics!=s.harmonics){plan.p=std::make_unique<Processor>(s);plan.size=s.fftSize;plan.low=s.low;plan.high=s.high;plan.frequency=s.expectedTone;plan.harmonics=s.harmonics;}
        Work w;w.name="Live physical input";std::vector<double> mono(s.fftSize);for(int ch=0;ch<2;++ch){for(unsigned i=0;i<s.fftSize;++i)mono[i]=rec.samples[i*2+ch];w.result[ch]=plan.p->analyze(mono,rec.rate);}return w;});}}
}catch(const std::exception& e){error=e.what();live=false;}}
void spectrum(const Result& r,int ch,float height){
    ImGui::PushID(ch);ImGui::BeginChild("spectrum",ImVec2(0,height),ImGuiChildFlags_Borders);auto color=ch?blue:green;
    ImGui::TextColored(color,"CH %d  /  %s",ch+1,r.valid?"SINGLE-TONE ANALYSIS":"SPECTRUM / CHECK SIGNAL");
    if(r.valid)ImGui::Text("f0 %.4f Hz   THD %.2f dBc (%.5f %%)   THD+N %.2f dBc   SNR %.2f dB   SINAD %.2f dB",r.frequency,r.thdDb,100*std::pow(10.,r.thdDb/20),r.thdnDb,r.snrDb,r.sinadDb);
    else ImGui::TextColored(muted,"Metrics need an isolated stationary tone; spectrum remains available.");
    ImGui::TextColored(muted,"RMS %.2f dBFS   Peak %.2f dBFS   RBW %.3f Hz   bin %.3f Hz   N %u   averages %u   DSP %.2f ms",db(r.rms*r.rms),db(r.peak*r.peak),r.rbw,r.df,r.size,r.averages,r.computeMs/std::max(1u,r.averages));
    ImVec2 pos=ImGui::GetCursorScreenPos();float width=ImGui::GetContentRegionAvail().x;float ph=std::max(110.f,ImGui::GetContentRegionAvail().y-30);ImGui::InvisibleButton("plot",ImVec2(width,ph));auto* draw=ImGui::GetWindowDrawList();ImVec2 a(pos.x+58,pos.y+4),b(pos.x+width-18,pos.y+ph-24);
    draw->AddRectFilled(a,b,IM_COL32(10,15,23,255));double low=std::max(.1,double(viewLow)),high=std::min(double(viewHigh),r.rate?r.rate/2.:24000.);high=std::max(low+1,high);
    auto xmap=[&](double f){return float(a.x+(b.x-a.x)*(logFrequency?std::log(std::max(f,low)/low)/std::log(high/low):(f-low)/(high-low)));};auto ymap=[&](double d){return float(b.y-(b.y-a.y)*(std::clamp(d,double(minDb),double(maxDb))-minDb)/(maxDb-minDb));};
    for(int d=int(std::ceil(minDb/20)*20);d<=maxDb;d+=20){float y=ymap(d);draw->AddLine(ImVec2(a.x,y),ImVec2(b.x,y),IM_COL32(41,51,64,255));char label[20];snprintf(label,sizeof label,"%d",d);draw->AddText(ImVec2(pos.x+8,y-8),IM_COL32(135,151,172,255),label);}
    for(double f:{20.,50.,100.,200.,500.,1000.,2000.,5000.,10000.,20000.,50000.,100000.})if(f>=low&&f<=high){float x=xmap(f);draw->AddLine(ImVec2(x,a.y),ImVec2(x,b.y),IM_COL32(35,45,59,255));char label[20];snprintf(label,sizeof label,f>=1000?"%.0fk":"%.0f",f>=1000?f/1000:f);draw->AddText(ImVec2(x-8,b.y+5),IM_COL32(135,151,172,255),label);}
    draw->PushClipRect(a,b,true);
    auto curve=[&](const std::vector<double>& psd,ImU32 ink){if(psd.empty())return;int columns=std::max(1,int(b.x-a.x));std::vector<float> lo(columns,1e9f),hi(columns,-1e9f);for(size_t k=1;k<psd.size();++k){double f=k*r.df;if(f<low||f>high)continue;int column=std::clamp(int(xmap(f)-a.x),0,columns-1);float y=ymap(db(psd[k]));lo[column]=std::min(lo[column],y);hi[column]=std::max(hi[column],y);}ImVec2 previous;bool has=false;for(int i=0;i<columns;++i)if(hi[i]>=0){ImVec2 point(a.x+i,lo[i]);if(has)draw->AddLine(previous,point,ink,1);draw->AddLine(point,ImVec2(point.x,hi[i]),ink,1);previous=ImVec2(point.x,hi[i]);has=true;}};
    curve(r.psd,ImGui::ColorConvertFloat4ToU32(color));if(residual)curve(r.residualPsd,IM_COL32(231,162,76,220));
    if(r.valid)for(auto& h:r.harmonic)if(h.frequency>=low&&h.frequency<=high){float x=xmap(h.frequency);draw->AddLine(ImVec2(x,a.y),ImVec2(x,b.y),IM_COL32(85,80,49,120));char label[12];snprintf(label,sizeof label,"H%u",h.order);draw->AddText(ImVec2(x+3,a.y+5),IM_COL32(226,183,98,255),label);}
    draw->PopClipRect();if(ImGui::IsItemHovered()&&!r.psd.empty()){auto mouse=ImGui::GetIO().MousePos;double t=std::clamp(double((mouse.x-a.x)/(b.x-a.x)),0.,1.);double f=logFrequency?low*std::pow(high/low,t):low+(high-low)*t;size_t k=std::min(r.psd.size()-1,size_t(std::round(f/r.df)));draw->AddLine(ImVec2(mouse.x,a.y),ImVec2(mouse.x,b.y),IM_COL32(210,220,230,150));ImGui::SetTooltip("%.3f Hz\nPSD %.2f dBFS/Hz",k*r.df,db(r.psd[k]));}
    ImGui::TextColored(muted,"PSD / dBFS per Hz    |    Hz %s    |    unweighted %.0f..%.0f Hz",logFrequency?"(log)":"(linear)",r.low,r.high);
    ImGui::EndChild();ImGui::PopID();
}
}
void setupTheme(){ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowPadding=ImVec2(20,16);s.FramePadding=ImVec2(10,5);s.ItemSpacing=ImVec2(10,7);s.ChildRounding=10;s.FrameRounding=5;auto* c=s.Colors;c[ImGuiCol_WindowBg]=ImVec4(.04f,.055f,.08f,1);c[ImGuiCol_ChildBg]=ImVec4(.065f,.085f,.115f,1);c[ImGuiCol_FrameBg]=ImVec4(.10f,.13f,.18f,1);c[ImGuiCol_Button]=ImVec4(.12f,.29f,.30f,1);c[ImGuiCol_ButtonHovered]=ImVec4(.16f,.4f,.4f,1);c[ImGuiCol_CheckMark]=green;c[ImGuiCol_SliderGrab]=green;c[ImGuiCol_Text]=ImVec4(.9f,.94f,.98f,1);auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",16);ImFontConfig merge;merge.MergeMode=true;io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc",16,&merge,io.Fonts->GetGlyphRangesChineseFull());}
void snapshotBackend(int value){snapshotLive=value==1;}
bool snapshotReady(){return initialized&&!results[0].psd.empty()&&(snapshotLive?origin=="Live physical input":!work.valid());}
void analyzerOpenPath(const wchar_t* path){analyzeFile(utf8(path));}
void drawToneLab(){
    if(!initialized){initialized=true;recording=demoRecording();offline();origin="Synthetic demo (not hardware)";inputScan=std::async(std::launch::async,[]{return inputDevices();});outputScan=std::async(std::launch::async,[]{return tone::devices(tone::Backend::Wasapi);});}
    poll();auto* vp=ImGui::GetMainViewport();ImGui::SetNextWindowPos(vp->WorkPos);ImGui::SetNextWindowSize(vp->WorkSize);ImGui::Begin("ToneScope",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.55f);ImGui::Text("ToneScope");ImGui::SetWindowFontScale(1);ImGui::SameLine();ImGui::TextColored(green,"AUDIO ANALYSIS STUDIO");ImGui::SameLine();ImGui::TextColored(muted,"Double precision / Kaiser beta 38 / ToneLab CLI source");
    if(ImGui::BeginTabBar("workspace")){
        if(ImGui::BeginTabItem("Live input / recording")){
            ImGui::BeginDisabled(action.valid()||inputScan.valid());ImGui::SetNextItemWidth(340);if(ImGui::BeginCombo("Input",inputs.empty()?"Scanning...":inputs[selectedInput].name.c_str())){for(int i=0;i<int(inputs.size());++i)if(ImGui::Selectable(inputs[i].name.c_str(),selectedInput==i))selectedInput=i;ImGui::EndCombo();}ImGui::SameLine();int rate=int(captureConfig.rate);ImGui::SetNextItemWidth(110);if(ImGui::InputInt("Capture Hz",&rate,0,0))captureConfig.rate=std::clamp(rate,8000,768000);ImGui::SameLine();ImGui::Checkbox("Exclusive",&captureConfig.exclusive);
            ImGui::SameLine();if(ImGui::Button(input.running()?"Stop input":"Start input")){if(input.running()){live=false;action=std::async(std::launch::async,[]{input.stop();});}else if(!inputs.empty()){captureConfig.device=inputs[selectedInput].id;auto cfg=captureConfig;live=true;action=std::async(std::launch::async,[cfg]{input.start(cfg);});}}
            ImGui::EndDisabled();ImGui::SetNextItemWidth(90);ImGui::InputDouble("Record seconds",&recordSeconds,0,0,"%.1f");ImGui::SameLine();ImGui::BeginDisabled(!input.running()||input.recording());if(ImGui::Button("Record"))try{input.beginRecording(recordSeconds);}catch(const std::exception& e){error=e.what();}ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(input.recording()||work.valid());if(ImGui::Button("Analyze captured segment")){auto r=input.recorded();if(!r.samples.empty()){recording=std::move(r);live=false;origin="Recorded physical input";segmentStart=0;segmentDuration=0;offline();}else error="Record a segment first";}ImGui::SameLine();if(ImGui::Button("Save captured WAV")){auto path=dialog(true,L"WAV audio\0*.wav\0\0",L"wav");if(!path.empty())try{auto r=input.recorded();saveWav(path,r);}catch(const std::exception& e){error=e.what();}}ImGui::EndDisabled();ImGui::SameLine();ImGui::Checkbox("Live FFT",&live);ImGui::SameLine();ImGui::TextColored(input.recording()?green:muted,"%s / gaps %llu",input.recording()?"RECORDING":"Ready",(unsigned long long)input.discontinuities());
            ImGui::TextColored(muted,"%s",input.status().c_str());ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("WAV / offline")){
            ImGui::BeginDisabled(work.valid());if(ImGui::Button("Open WAV...")){auto path=dialog(false,L"WAV audio\0*.wav\0\0",L"wav");if(!path.empty()){segmentStart=0;segmentDuration=0;analyzeFile(path);}}ImGui::SameLine();ImGui::SetNextItemWidth(100);ImGui::InputDouble("Start / s",&segmentStart,0,0,"%.3f");ImGui::SameLine();ImGui::SetNextItemWidth(100);ImGui::InputDouble("Length / s (0=rest)",&segmentDuration,0,0,"%.3f");ImGui::SameLine();if(ImGui::Button("Analyze selection"))offline();ImGui::EndDisabled();ImGui::TextColored(muted,"Loaded %.3f s / %u Hz / %u channels. Drag a WAV onto this window to open.",recording.rate&&recording.channels?double(recording.samples.size())/recording.channels/recording.rate:0,recording.rate,recording.channels);ImGui::TextWrapped("%s",origin.c_str());ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("ToneLab CLI source")){
            ImGui::BeginDisabled(source.running()||outputScan.valid());ImGui::SetNextItemWidth(170);if(ImGui::Combo("Backend",&backend,"WASAPI Exclusive\0DirectSound\0ASIO\0")){auto b=backend==0?tone::Backend::Wasapi:backend==1?tone::Backend::DirectSound:tone::Backend::Asio;outputScan=std::async(std::launch::async,[b]{return tone::devices(b);});}
            ImGui::SameLine();ImGui::SetNextItemWidth(330);if(ImGui::BeginCombo("Output",outputs.empty()?"No output":outputs[selectedOutput].name.c_str())){for(int i=0;i<int(outputs.size());++i)if(ImGui::Selectable(outputs[i].name.c_str(),selectedOutput==i))selectedOutput=i;ImGui::EndCombo();}
            ImGui::SameLine();ImGui::SetNextItemWidth(150);ImGui::Combo("Driven channel",&drive,"Left (right zero)\0Right (left zero)\0Both\0");
            ImGui::SetNextItemWidth(110);ImGui::InputDouble("Tone Hz",&toneFrequency,0,0,"%.3f");ImGui::SameLine();ImGui::SetNextItemWidth(100);ImGui::InputDouble("Peak FS",&amplitude,0,0,"%.4f");ImGui::EndDisabled();
            ImGui::SameLine();if(ImGui::Button(source.running()?"Stop CLI source":"Start CLI source")){if(source.running())source.stop();else try{if(outputs.empty()||toneFrequency<=0||toneFrequency>=captureConfig.rate/2.||amplitude<0||amplitude>1)throw std::runtime_error("Invalid source selection, frequency or amplitude");std::ostringstream args;args<<"--backend "<<(backend==0?"wasapi":backend==1?"ds":"asio")<<" --device "<<outputs[selectedOutput].id<<" --rate "<<captureConfig.rate<<" --format auto --buffer 4096 --left-freq "<<toneFrequency<<" --right-freq "<<toneFrequency<<" --left-amp "<<amplitude<<" --right-amp "<<amplitude<<" --left-mute "<<(drive==1)<<" --right-mute "<<(drive==0);auto text=args.str();source.start(std::wstring(text.begin(),text.end()));}catch(const std::exception& e){error=e.what();}}
            ImGui::TextColored(muted,"%s",source.status().c_str());ImGui::TextColored(muted,"THD: inspect driven input. Crosstalk: inspect undriven input. Source rate follows Capture Hz.");ImGui::EndTabItem();
        }ImGui::EndTabBar();
    }
    ImGui::Separator();
    if(ImGui::Button("Measurement settings"))ImGui::OpenPopup("Measurement settings");
    if(ImGui::BeginPopup("Measurement settings")){int h=int(settings.harmonics);if(ImGui::SliderInt("Harmonics through Hn",&h,2,32))settings.harmonics=h;ImGui::TextWrapped("THD sums H2..Hn inside the selected band. SNR removes those fitted harmonics. THD+N includes all in-band residuals. Unweighted; no A weighting or AES17 filter.");ImGui::EndPopup();}
    ImGui::SameLine();ImGui::SetNextItemWidth(140);const char* fftNames[]={"16,384","32,768","65,536","131,072","262,144","524,288","1,048,576"};ImGui::Combo("FFT size",&fftIndex,fftNames,7);settings.fftSize=16384u<<fftIndex;ImGui::SameLine();ImGui::SetNextItemWidth(85);ImGui::InputDouble("Band low",&settings.low,0,0,"%.0f");ImGui::SameLine();ImGui::SetNextItemWidth(90);ImGui::InputDouble("Band high",&settings.high,0,0,"%.0f");ImGui::SameLine();ImGui::SetNextItemWidth(85);ImGui::InputDouble("Expected f0 (0=auto)",&settings.expectedTone,0,0,"%.2f");ImGui::SameLine();ImGui::BeginDisabled(work.valid());if(ImGui::Button("Reanalyze"))offline();ImGui::EndDisabled();
    ImGui::Checkbox("Log Hz",&logFrequency);ImGui::SameLine();ImGui::Checkbox("Residual noise",&residual);ImGui::SameLine();ImGui::SetNextItemWidth(80);ImGui::InputFloat("View from",&viewLow,0,0,"%.0f");ImGui::SameLine();ImGui::SetNextItemWidth(90);ImGui::InputFloat("to Hz",&viewHigh,0,0,"%.0f");ImGui::SameLine();ImGui::SetNextItemWidth(70);ImGui::InputFloat("Floor dB",&minDb,0,0,"%.0f");minDb=std::clamp(minDb,-300.f,-20.f);viewLow=std::max(.1f,viewLow);viewHigh=std::max(viewLow+1,viewHigh);
    ImGui::SameLine();if(ImGui::Button("Export CSV")){auto path=dialog(true,L"Spectrum CSV\0*.csv\0\0",L"csv");if(!path.empty())try{exportCsv(path,results);}catch(const std::exception& e){error=e.what();}}ImGui::SameLine();if(ImGui::Button("Report JSON")){auto path=dialog(true,L"JSON report\0*.json\0\0",L"json");if(!path.empty())try{exportJson(path,results);}catch(const std::exception& e){error=e.what();}}
    float height=std::max(240.f,(ImGui::GetContentRegionAvail().y-65)/2);spectrum(results[0],0,height);spectrum(results[1],1,height);
    ImGui::TextColored(origin.find("Synthetic")!=std::string::npos?ImVec4(.96f,.72f,.3f,1):muted,"Data: %s | %s%s",origin.c_str(),message.c_str(),work.valid()?" / processing...":"");if(!error.empty()){ImGui::SameLine();ImGui::TextColored(ImVec4(1,.5f,.4f,1),"%s",error.c_str());if(ImGui::IsItemClicked())error.clear();}
    if(!results[0].warning.empty())ImGui::TextColored(muted,"%s",results[0].warning.c_str());
    ImGui::End();
}
