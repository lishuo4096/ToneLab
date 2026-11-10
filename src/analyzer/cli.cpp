#include "analyzer/analysis.hpp"
#include "analyzer/capture.hpp"
#include <windows.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <algorithm>
#include <thread>
using namespace analyzer;
static double numeric(const std::string& x){size_t n=0;double v=std::stod(x,&n);if(n!=x.size()||!std::isfinite(v))throw std::runtime_error("Invalid number: "+x);return v;}
static unsigned integer(const std::string& x){double v=numeric(x);if(v<0||v>1048576||v!=std::floor(v))throw std::runtime_error("Invalid unsigned integer: "+x);return unsigned(v);}
int main(int argc,char** argv){SetConsoleOutputCP(CP_UTF8);try{
    if(argc<2||std::string(argv[1])=="--help"){std::cout<<R"(ToneScope audio analyzer
  tonescope-cli list
  tonescope-cli analyze --input recording.wav [--fft 65536 --low 20 --high 20000 --tone 0 --harmonics 10 --start 0 --duration 0 --json report.json --csv spectrum.csv]
  tonescope-cli record --input-device 0 --rate 96000 --seconds 3 --output capture.wav [--shared 1 --json report.json]
  tonescope-cli measure --input-device 0 --rate 384000 --seconds 3 --output capture.wav --backend wasapi --output-device 0 --tone 997 --amp 0.05 --drive left [analysis options]
  tonescope-cli demo --output reference.wav [--rate 48000 --seconds 2 --json report.json]
  tonescope-cli benchmark [--rate 384000 --fft 65536]
Metrics: unweighted low..high Hz; THD / THD+N referenced to fundamental;
SNR excludes fitted H2..Hn; single-tone stationary input required.
Input recording is physical WASAPI capture, not a Windows output loopback stream.
measure launches the existing tonelab-cli.exe; drive=left|right|both, other output is zero.
)";return 0;}
    std::string command=argv[1];std::map<std::string,std::string> a;for(int i=2;i<argc;i+=2){if(i+1>=argc)throw std::runtime_error("Missing option value");if(!a.emplace(argv[i],argv[i+1]).second)throw std::runtime_error("Duplicate option");}
    const std::vector<std::string> valid={"--input","--input-device","--output-device","--rate","--seconds","--output","--shared","--backend","--tone","--amp","--drive","--fft","--low","--high","--harmonics","--start","--duration","--json","--csv"};for(auto& kv:a)if(std::find(valid.begin(),valid.end(),kv.first)==valid.end())throw std::runtime_error("Unknown option: "+kv.first);
    auto value=[&](const char* k,const char* fallback){auto i=a.find(k);return i==a.end()?std::string(fallback):i->second;};
    if(command=="list"){for(auto& d:inputDevices())std::cout<<"["<<d.id<<"] "<<d.name<<" | Windows mix "<<d.defaultRate<<" Hz\n";return 0;}
    Settings settings;settings.fftSize=integer(value("--fft","65536"));settings.low=numeric(value("--low","20"));settings.high=numeric(value("--high","20000"));settings.expectedTone=numeric(value("--tone","0"));settings.harmonics=integer(value("--harmonics","10"));Processor check(settings);
    unsigned rate=integer(value("--rate","48000"));double seconds=numeric(value("--seconds","3"));if(rate<8000||rate>768000||seconds<=0||seconds>30)throw std::runtime_error("Rate must be 8..768 kHz and duration >0..30 seconds");
    if(value("--shared","0")!="0"&&value("--shared","0")!="1")throw std::runtime_error("--shared must be 0 or 1");
    Recording recording;
    if(command=="analyze"){if(!a.count("--input"))throw std::runtime_error("analyze requires --input");recording=loadWav(a.at("--input"));}
    else if(command=="demo"||command=="benchmark"){recording=demoRecording(rate,command=="benchmark"?std::max(1.,double(settings.fftSize)/rate):seconds);if(command=="demo"){if(!a.count("--output"))throw std::runtime_error("demo requires --output");saveWav(a.at("--output"),recording);}}
    else if(command=="record"||command=="measure"){
        if(!a.count("--output"))throw std::runtime_error("record/measure requires --output WAV path");
        Capture input;CaptureConfig cfg;cfg.device=integer(value("--input-device","0"));cfg.rate=rate;cfg.exclusive=value("--shared","0")=="0";input.start(cfg);Source source;
        if(command=="measure"){std::string backend=value("--backend","wasapi"),drive=value("--drive","left");if(backend!="wasapi"&&backend!="asio"&&backend!="ds")throw std::runtime_error("Invalid backend");if(drive!="left"&&drive!="right"&&drive!="both")throw std::runtime_error("Invalid drive channel");double amp=numeric(value("--amp","0.05")),tone=numeric(value("--tone","997"));if(amp<0||amp>1||tone<=0||tone>=rate/2.)throw std::runtime_error("Invalid tone or amplitude");
            std::string args="--backend "+backend+" --rate "+std::to_string(rate)+" --format auto --buffer 4096 --left-freq "+std::to_string(tone)+" --right-freq "+std::to_string(tone)+" --left-amp "+std::to_string(amp)+" --right-amp "+std::to_string(amp)+" --left-mute "+(drive=="right"?"1":"0")+" --right-mute "+(drive=="left"?"1":"0");if(a.count("--output-device"))args+=" --device "+std::to_string(integer(a.at("--output-device")));source.start(std::wstring(args.begin(),args.end()));std::this_thread::sleep_for(std::chrono::milliseconds(800));if(!source.running())throw std::runtime_error(source.status());}
        input.beginRecording(seconds);while(input.running()&&input.recording())std::this_thread::sleep_for(std::chrono::milliseconds(25));if(!input.running())throw std::runtime_error(input.status());recording=input.recorded();source.stop();input.stop();saveWav(a.at("--output"),recording);std::cout<<"Captured "<<recording.samples.size()/recording.channels<<" frames / "<<recording.rate<<" Hz; discontinuities="<<input.discontinuities()<<'\n';
    }else throw std::runtime_error("Unknown command");
    auto start=std::chrono::steady_clock::now();auto result=analyzeRecording(recording,settings,numeric(value("--start","0")),numeric(value("--duration","0")));
    for(unsigned ch=0;ch<std::min(2u,recording.channels);++ch){auto& r=result[ch];std::cout<<"CH"<<ch+1<<" valid="<<r.valid<<" f0="<<r.frequency<<" Hz THD="<<r.thdDb<<" dBc THD+N="<<r.thdnDb<<" dBc SNR="<<r.snrDb<<" dB SINAD="<<r.sinadDb<<" dB RBW="<<r.rbw<<" Hz averages="<<r.averages<<"\n";if(!r.warning.empty())std::cout<<r.warning<<'\n';}
    std::cout<<"Analysis wall time: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
    if(a.count("--json"))exportJson(a.at("--json"),result);
    if(a.count("--csv"))exportCsv(a.at("--csv"),result);
    return 0;
}catch(const std::exception& e){std::cerr<<"Error: "<<e.what()<<'\n';return 1;}}
