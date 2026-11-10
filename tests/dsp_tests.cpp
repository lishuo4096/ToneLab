#include "tone/dsp.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace tone;
static void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main(){try{
    unsigned checks=0;
    for(unsigned rate:{44100u,48000u,96000u,192000u,384000u,768000u}){
        std::array<Channel,2> c{{{1234.567,.31,Wave::Sine,true},{4321.123,.17,Wave::Sine,true}}};
        Generator full(rate,Format::I32),split(rate,Format::I32);std::vector<int32_t>a(8192*2),b(a.size());
        full.render(a.data(),8192,c);split.render(b.data(),3001,c);split.render(b.data()+6002,5191,c);
        require(a==b,"Block boundaries changed signal");++checks;
        for(unsigned frame=4000;frame<8192;frame+=17)for(int ch=0;ch<2;++ch){double expected=c[ch].amplitude*std::sin(2*3.14159265358979323846*std::fmod(double(frame)*c[ch].frequency/rate,1.));require(std::abs(a[frame*2+ch]/2147483648.-expected)<1e-8,"Native-rate phase/frequency mismatch");++checks;}
        bool lowBits=false;for(int32_t x:a)if(x&127)lowBits=true;require(lowBits,"PCM32 precision was reduced to float/24 bits");++checks;
    }
    unsigned char value[4];encode(1,Format::I32,value);require(value[0]==255&&value[1]==255&&value[2]==255&&value[3]==127,"Positive fullscale overflow");encode(-1,Format::I24,value);require(value[0]==0&&value[1]==0&&value[2]==128,"PCM24 negative fullscale");
    for(auto wave:{Wave::Sine,Wave::Square,Wave::Saw,Wave::Triangle})for(int i=0;i<1000;++i){double x=waveform(wave,i/1000.,.01);require(std::isfinite(x)&&std::abs(x)<=1.001,"Waveform invalid or unbounded");++checks;}
    std::array<Channel,2> c;c[0].enabled=false;c[1].frequency=2000;Generator gen(384000,Format::I32);std::vector<int32_t> samples(8192);gen.render(samples.data(),4096,c);for(size_t i=0;i<samples.size();i+=2)require(samples[i]==0,"Left mute affected isolation");
    for(auto format:{Format::I16,Format::I24,Format::I32,Format::F32,Format::I24In32}) {
        Generator live(384000,format);std::array<Channel,2> state;
        std::vector<unsigned char> block(4096*2*bytes(format));live.render(block.data(),4096,state);
        state[0].enabled=false;live.render(block.data(),4096,state);
        bool rightActive=false;
        for(int frame=0;frame<4096;++frame)for(int b=0;b<bytes(format);++b){
            require(block[(frame*2)*bytes(format)+b]==0,"Live hard-mute did not write zero bytes");
            rightActive|=block[(frame*2+1)*bytes(format)+b]!=0;
        }
        require(rightActive,"Left hard-mute silenced right channel");
        state[1].enabled=false;live.render(block.data(),4096,state);
        for(auto b:block)require(b==0,"Both channels off must write all zero bytes");
        state[0].enabled=true;live.render(block.data(),4096,state);
        bool leftActive=false;for(int frame=0;frame<4096;++frame)for(int b=0;b<bytes(format);++b){
            leftActive|=block[frame*2*bytes(format)+b]!=0;
            require(block[(frame*2+1)*bytes(format)+b]==0,"Re-enabling left disturbed muted right");
        }
        require(leftActive,"Re-enabling left produced no output");++checks;
    }
    encode(.5,Format::I24In32,value);require(value[0]==0&&value[1]==0&&value[2]==0&&value[3]==64,"24 valid bits in PCM32 must be left aligned");
    bool rejected=false;try{validate({192000,.1,Wave::Sine,true},384000);}catch(...){rejected=true;}require(rejected,"Nyquist was accepted");
    std::cout<<"PASS: "<<checks<<" checks; native rates, phase continuity, independent channels, PCM32 low bits, fullscale, waveform bounds, Nyquist.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
