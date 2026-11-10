#include "tone/dsp.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace tone {
void Controls::set(const Channel& c) { frequency=c.frequency; amplitude=c.amplitude; wave=int(c.wave); enabled=c.enabled; }
Channel Controls::get() const { return {frequency.load(),amplitude.load(),Wave(wave.load()),enabled.load()}; }
int bytes(Format f) { return f==Format::I16?2:f==Format::I24?3:4; }
const char* formatName(Format f) { static const char* n[]={"PCM 16-bit","PCM 24-bit","PCM 32-bit","Float 32-bit","PCM 24-bit in 32-bit"}; return n[int(f)]; }
Format parseFormat(const std::string& s) { if(s=="16")return Format::I16; if(s=="24")return Format::I24; if(s=="32")return Format::I32; if(s=="f32")return Format::F32; if(s=="24in32")return Format::I24In32; throw std::runtime_error("Format must be 16, 24, 32, f32 or 24in32"); }
Wave parseWave(const std::string& s) { if(s=="sine")return Wave::Sine; if(s=="square")return Wave::Square; if(s=="saw")return Wave::Saw; if(s=="triangle")return Wave::Triangle; throw std::runtime_error("Unknown waveform: "+s); }
void validate(const Channel& c,unsigned rate) { if(rate<8000 || rate>768000 || !std::isfinite(c.frequency) || c.frequency<=0 || c.frequency>=rate/2.0 || !std::isfinite(c.amplitude) || c.amplitude<0 || c.amplitude>1) throw std::runtime_error("Frequency must be > 0 and < sample rate / 2; amplitude must be 0..1; rate 8000..768000"); }
static double blep(double t,double dt) { if(t<dt) {t/=dt;return t+t-t*t-1;} if(t>1-dt){t=(t-1)/dt;return t*t+t+t+1;} return 0; }
double waveform(Wave w,double p,double dt) {
    constexpr double pi=3.14159265358979323846;
    if(w==Wave::Sine)return std::sin(2*pi*p);
    if(w==Wave::Saw)return 2*p-1-blep(p,dt);
    if(w==Wave::Square)return (p<.5?1:-1)+blep(p,dt)-blep(std::fmod(p+.5,1.0),dt);
    // Finite Fourier series, excludes all harmonics at/above Nyquist.
    double v=0; int sign=1;
    double angle=2*pi*p, sn=std::sin(angle), cs=std::cos(angle);
    const double s2=2*sn*cs, c2=cs*cs-sn*sn;
    for(int n=1;n<=127 && n*dt<.5;n+=2,sign=-sign) {
        v+=sign*sn/(double(n)*n);
        double next=sn*c2+cs*s2;cs=cs*c2-sn*s2;sn=next;
    }
    return v*8/(pi*pi);
}
void encode(double x,Format f,unsigned char* out) {
    x=std::clamp(x,-1.0,1.0);
    if(f==Format::F32){float v=float(x);std::memcpy(out,&v,4);return;}
    int bits=f==Format::I24In32?24:bytes(f)*8;
    const double scale=std::ldexp(1.0,bits-1);
    int64_t v=std::clamp<int64_t>(std::llround(x*scale),-int64_t(scale),int64_t(scale)-1);
    if(f==Format::I24In32){out[0]=0;for(int i=0;i<3;++i)out[i+1]=static_cast<unsigned char>(uint64_t(v)>>(8*i));}
    else for(int i=0;i<bits/8;++i)out[i]=static_cast<unsigned char>(uint64_t(v)>>(8*i));
}
void Generator::render(void* output,unsigned frames,const std::array<Channel,2>& c) {
    auto out=static_cast<unsigned char*>(output); const int size=bytes(format_);
    const double slew=1.0/(rate_*.005);
    for(unsigned i=0;i<frames;++i)for(int ch=0;ch<2;++ch){
        double dt=std::clamp(c[ch].frequency/double(rate_),1e-12,.499999);
        if(!c[ch].enabled) {
            // Hard channel switch: integer PCM and float both receive zero bytes.
            gain_[ch]=0;
            std::memset(out,0,size);
        } else {
            gain_[ch]+=std::clamp(c[ch].amplitude-gain_[ch],-slew,slew);
            encode(waveform(c[ch].wave,phase_[ch],dt)*gain_[ch],format_,out);
        }
        out+=size;
        phase_[ch]+=dt;phase_[ch]-=std::floor(phase_[ch]);
    }
}
}
