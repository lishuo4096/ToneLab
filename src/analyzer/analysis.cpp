#include "analyzer/analysis.hpp"
#include "kiss_fft.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
namespace analyzer {
constexpr double pi=3.14159265358979323846;
double db(double p){return 10*std::log10(std::max(p,1e-300));}
static double bessel(double x){double s=1,t=1;for(int k=1;k<150;++k){t*=x*x/(4.*k*k);s+=t;if(t<s*1e-16)break;}return s;}
struct Processor::Impl {
    Settings settings;std::vector<double> window;std::vector<kiss_fft_cpx> input,output;kiss_fft_cfg fft=nullptr;double sum=0,sum2=0;
    explicit Impl(Settings s):settings(s),window(s.fftSize),input(s.fftSize),output(s.fftSize){
        if(s.fftSize<1024||s.fftSize>1048576||(s.fftSize&(s.fftSize-1)))throw std::runtime_error("FFT size must be a power of two from 1024 to 1048576");
        if(!std::isfinite(s.low)||!std::isfinite(s.high)||s.low<0||s.high<=s.low||s.harmonics<2||s.harmonics>32||!std::isfinite(s.expectedTone)||s.expectedTone<0)throw std::runtime_error("Invalid analysis settings");
        double base=bessel(38);for(unsigned i=0;i<s.fftSize;++i){double x=2.*i/(s.fftSize-1)-1;double w=bessel(38*std::sqrt(std::max(0.,1-x*x)))/base;window[i]=w;sum+=w;sum2+=w*w;}
        fft=kiss_fft_alloc(int(s.fftSize),0,nullptr,nullptr);if(!fft)throw std::bad_alloc();
    }
    ~Impl(){kiss_fft_free(fft);}
    std::vector<double> spectrum(const std::vector<double>& x,unsigned rate){
        for(unsigned i=0;i<settings.fftSize;++i){input[i].r=x[i]*window[i];input[i].i=0;}
        kiss_fft(fft,input.data(),output.data());std::vector<double> p(settings.fftSize/2+1);
        for(unsigned k=0;k<p.size();++k)p[k]=(output[k].r*output[k].r+output[k].i*output[k].i)/(rate*sum2)*(k==0||k==settings.fftSize/2?1:2);
        return p;
    }
    // Weighted sinusoidal least squares. Window-separated components are orthogonal;
    // solve the exact 2x2 sin/cos Gram matrix instead of assuming integer cycles.
    double removeTone(std::vector<double>& x,double frequency,unsigned rate){
        double angle=2*pi*frequency/rate,sd=std::sin(angle),cd=std::cos(angle),sn=0,cs=1;
        double ss=0,cc=0,sc=0,ys=0,yc=0;
        for(unsigned i=0;i<x.size();++i){double w=window[i];ss+=w*sn*sn;cc+=w*cs*cs;sc+=w*sn*cs;ys+=w*x[i]*sn;yc+=w*x[i]*cs;double n=sn*cd+cs*sd;cs=cs*cd-sn*sd;sn=n;}
        double det=ss*cc-sc*sc;if(det<1e-20)return 0;double a=(ys*cc-yc*sc)/det,b=(yc*ss-ys*sc)/det;
        sn=0;cs=1;for(unsigned i=0;i<x.size();++i){x[i]-=a*sn+b*cs;double n=sn*cd+cs*sd;cs=cs*cd-sn*sd;sn=n;}
        return (a*a+b*b)/2;
    }
};
Processor::Processor(Settings s):p_(new Impl(s)){}Processor::~Processor()=default;
Result Processor::analyze(const std::vector<double>& mono,unsigned rate){
    auto begin=std::chrono::steady_clock::now();auto& p=*p_;auto s=p.settings;Result r;r.rate=rate;r.size=s.fftSize;r.averages=1;r.df=double(rate)/r.size;r.rbw=rate*p.sum2/(p.sum*p.sum);r.low=s.low;r.high=std::min(s.high,rate/2.);
    if(rate<8000||rate>768000||mono.size()<s.fftSize||r.high<=r.low)throw std::runtime_error("Recording too short, unsupported rate, or invalid measurement bandwidth");
    std::vector<double> x(mono.begin(),mono.begin()+s.fftSize);double square=0,weightedDc=0;
    for(unsigned i=0;i<x.size();++i){if(!std::isfinite(x[i]))throw std::runtime_error("Input contains nonfinite samples");square+=x[i]*x[i];r.dc+=x[i];weightedDc+=x[i]*p.window[i];r.peak=std::max(r.peak,std::abs(x[i]));if(std::abs(x[i])>=.999969)r.clipped+=1;}
    r.rms=std::sqrt(square/x.size());r.dc/=x.size();r.clipped/=x.size();weightedDc/=p.sum;for(auto& v:x)v-=weightedDc;
    r.psd=p.spectrum(x,rate);
    unsigned lower=std::max(1u,unsigned(std::ceil(r.low/r.df))),upper=std::min(unsigned(r.psd.size()-1),unsigned(r.high/r.df));
    auto peak=lower;if(s.expectedTone>0){if(s.expectedTone<r.low||s.expectedTone>=r.high)throw std::runtime_error("Expected fundamental is outside measurement band");lower=std::max(lower,unsigned(std::max(1.,std::floor(s.expectedTone/r.df-4))));upper=std::min(upper,unsigned(std::ceil(s.expectedTone/r.df+4)));peak=lower;}
    for(unsigned k=lower;k<=upper;++k)if(r.psd[k]>r.psd[peak])peak=k;
    double power=0,moment=0;for(unsigned k=peak>16?peak-16:1;k<=std::min(peak+16,unsigned(r.psd.size()-2));++k){power+=r.psd[k];moment+=r.psd[k]*k;}
    r.frequency=power>1e-290?moment/power*r.df:0;
    if(r.frequency<32*r.df||r.frequency>=r.high||r.rms<1e-12){r.warning="No isolated single tone (need f0 >= 32 FFT bins). Increase FFT size or check input.";r.residualPsd=r.psd;return r;}
    r.signalPower=p.removeTone(x,r.frequency,rate);
    auto fundamentalResidual=p.spectrum(x,rate);
    double noiseDist=0;for(unsigned k=unsigned(std::ceil(r.low/r.df));k<fundamentalResidual.size()&&k*r.df<=r.high;++k)noiseDist+=fundamentalResidual[k]*r.df;
    for(unsigned h=2;h<=s.harmonics;++h){double f=h*r.frequency;if(f>=rate/2.||f>r.high)break;if(f<r.low)continue;double hp=p.removeTone(x,f,rate);r.distortionPower+=hp;r.harmonic.push_back({h,f,hp,db(hp/r.signalPower),false});}
    r.residualPsd=p.spectrum(x,rate);
    for(unsigned k=unsigned(std::ceil(r.low/r.df));k<r.residualPsd.size()&&k*r.df<=r.high;++k)r.noisePower+=r.residualPsd[k]*r.df;
    r.thdDb=db(r.distortionPower/r.signalPower);r.thdnDb=db(noiseDist/r.signalPower);r.snrDb=db(r.signalPower/r.noisePower);r.sinadDb=-r.thdnDb;
    double floor=r.noisePower/(r.high-r.low)*r.rbw;
    for(auto& h:r.harmonic){h.noiseLimited=h.power<10*floor;r.noiseLimited|=h.noiseLimited;}
    r.valid=r.signalPower>1e-24&&noiseDist<r.signalPower*.3;
    if(!r.valid)r.warning="Single-tone assumption uncertain: nonfundamental content exceeds 30% of fundamental.";
    else if(r.clipped>0)r.warning="Input clipping detected: lower device or source level.";
    else if(r.noiseLimited)r.warning="Some harmonics are near the noise floor; THD includes estimation uncertainty.";
    r.computeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return r;
}
std::array<Result,2> analyzeRecording(const Recording& recording,Settings s,double start,double duration){
    if(!recording.rate||!recording.channels||recording.channels>32||recording.samples.size()%recording.channels||!std::isfinite(start)||!std::isfinite(duration)||start<0||duration<0)throw std::runtime_error("Invalid recording or selected time range");
    size_t total=recording.samples.size()/recording.channels;if(start>=double(total)/recording.rate)throw std::runtime_error("Start time is beyond recording");size_t first=size_t(start*recording.rate);size_t length=duration>0&&duration<double(total-first)/recording.rate?size_t(duration*recording.rate):total-first;
    if(length<s.fftSize)throw std::runtime_error("Selected segment is shorter than FFT window");
    std::array<Result,2> result;Processor processor(s);
    for(unsigned ch=0;ch<std::min(2u,recording.channels);++ch){
        std::vector<double> data(s.fftSize);unsigned count=0;double signal=0,noise=0,dist=0,nd=0,rms2=0,dc=0,clip=0,frequency=0;Result sum;
        // 50% overlap, bounded to 64 windows spread across the selected segment.
        size_t possible=1+(length-s.fftSize)/(s.fftSize/2),windows=std::min<size_t>(64,possible);
        for(size_t w=0;w<windows;++w){size_t offset=windows==1?0:w*(length-s.fftSize)/(windows-1);for(unsigned i=0;i<s.fftSize;++i)data[i]=recording.samples[(first+offset+i)*recording.channels+ch];auto r=processor.analyze(data,recording.rate);
            if(!count){sum=r;std::fill(sum.psd.begin(),sum.psd.end(),0);std::fill(sum.residualPsd.begin(),sum.residualPsd.end(),0);for(auto& h:sum.harmonic)h.power=0;}
            for(size_t k=0;k<r.psd.size();++k){sum.psd[k]+=r.psd[k];sum.residualPsd[k]+=r.residualPsd[k];}
            for(size_t i=0;i<std::min(sum.harmonic.size(),r.harmonic.size());++i)sum.harmonic[i].power+=r.harmonic[i].power;
            signal+=r.signalPower;noise+=r.noisePower;dist+=r.distortionPower;nd+=r.signalPower*std::pow(10.,r.thdnDb/10);rms2+=r.rms*r.rms;dc+=r.dc;clip+=r.clipped;frequency+=r.frequency;sum.valid&=r.valid;sum.peak=std::max(sum.peak,r.peak);sum.computeMs+=count?r.computeMs:0;++count;
        }
        for(auto& v:sum.psd)v/=count;
        for(auto& v:sum.residualPsd)v/=count;
        sum.averages=count;sum.rms=std::sqrt(rms2/count);sum.dc=dc/count;sum.clipped=clip/count;sum.frequency=frequency/count;sum.signalPower=signal/count;sum.noisePower=noise/count;sum.distortionPower=dist/count;sum.thdDb=signal>0?db(dist/signal):0;sum.thdnDb=signal>0?db(nd/signal):0;sum.snrDb=signal>0?db(signal/std::max(noise,1e-300)):0;sum.sinadDb=-sum.thdnDb;
        for(auto& h:sum.harmonic){h.power/=count;h.dbCarrier=db(h.power/std::max(sum.signalPower,1e-300));h.frequency=h.order*sum.frequency;}
        result[ch]=std::move(sum);
    }
    return result;
}
Recording demoRecording(unsigned rate,double seconds){Recording r;r.rate=rate;r.channels=2;r.samples.resize(size_t(rate*seconds)*2);uint64_t seed=17;for(size_t i=0;i<r.samples.size()/2;++i)for(unsigned ch=0;ch<2;++ch){double t=double(i)/rate,f=ch?2003.17:997.13;seed=seed*6364136223846793005ULL+1;double noise=(double(seed>>11)/9007199254740992.-.5)*3.464101615*.000003;r.samples[i*2+ch]=.25*std::sin(2*pi*f*t)+.00025*std::sin(4*pi*f*t)+.000125*std::sin(6*pi*f*t)+noise;}return r;}
}
