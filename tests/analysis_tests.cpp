#include "analyzer/analysis.hpp"
#include <cmath>
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace analyzer;
static void require(bool x,const char* label){if(!x)throw std::runtime_error(label);}
int main(){try{
    constexpr double pi=3.141592653589793;unsigned tests=0;
    for(unsigned rate:{48000u,384000u})for(unsigned n:{65536u,262144u}){
        Settings s;s.fftSize=n;s.low=20;s.high=20000;Processor p(s);std::vector<double> x(n);
        double f=997.137,a=.25,b=2.5e-6,c=1.25e-6;
        for(unsigned i=0;i<n;++i){double t=double(i)/rate;x[i]=a*std::sin(2*pi*f*t)+b*std::sin(4*pi*f*t+.3)+c*std::sin(6*pi*f*t+.7);}
        auto r=p.analyze(x,rate);double expected=10*std::log10((b*b+c*c)/(a*a));std::cout<<rate<<" Hz N="<<n<<" f="<<r.frequency<<" THD="<<r.thdDb<<" expected="<<expected<<" THD+N="<<r.thdnDb<<" ms="<<r.computeMs<<"\n";
        require(r.valid,"Known single tone rejected");require(std::abs(r.frequency-f)<1e-5,"Noncoherent frequency estimate inaccurate");require(std::abs(r.thdDb-expected)<.03,"Deep THD (-99 dBc) inaccurate");require(std::abs(r.thdnDb-expected)<.05,"THD+N inaccurate without noise");
        double integrated=0;for(auto v:r.psd)integrated+=v*r.df;require(std::abs(integrated-a*a/2)<1e-8,"PSD scaling / Parseval mismatch");++tests;
        std::mt19937 rng(47);std::normal_distribution<double> noise(0,1e-5);for(auto& v:x)v+=noise(rng);auto noisy=p.analyze(x,rate);double ideal=10*std::log10((a*a/2)/(1e-10*(20000-20)/(rate/2.)));std::cout<<"SNR="<<noisy.snrDb<<" expected="<<ideal<<'\n';require(std::abs(noisy.snrDb-ideal)<.7,"Band-limited noise SNR inaccurate");require(std::abs(noisy.sinadDb+noisy.thdnDb)<1e-10,"SINAD identity incorrect");++tests;
    }
    Settings s;Processor p(s);std::vector<double> zero(s.fftSize);require(!p.analyze(zero,48000).valid,"Silence returned valid THD");auto r=demoRecording();saveWav("build/analysis-reference.wav",r);auto loaded=loadWav("build/analysis-reference.wav");require(loaded.rate==r.rate&&loaded.samples.size()==r.samples.size(),"WAV roundtrip metadata");for(size_t i=0;i<r.samples.size();i+=113)require(std::abs(r.samples[i]-loaded.samples[i])<1./2147483648,"PCM32 precision lost");
    auto results=analyzeRecording(loaded,s);exportJson("build/analysis-reference.json",results);exportCsv("build/analysis-reference.csv",results);
    std::cout<<"PASS: "<<tests<<" numerical cases, deep THD, bandwidth SNR, PSD scaling, noncoherent frequency, silence rejection, PCM32 WAV roundtrip and exports.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
