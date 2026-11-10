#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
namespace analyzer {
struct Settings { unsigned fftSize=65536; double low=20,high=20000,expectedTone=0; unsigned harmonics=10; };
struct Harmonic { unsigned order; double frequency,power,dbCarrier; bool noiseLimited=false; };
struct Result {
    unsigned rate=0,size=0,averages=0; double df=0,rbw=0,frequency=0,rms=0,peak=0,dc=0,clipped=0;
    double signalPower=0,noisePower=0,distortionPower=0,thdDb=0,thdnDb=0,snrDb=0,sinadDb=0,computeMs=0;
    double low=0,high=0; bool valid=false,noiseLimited=false; std::string warning;
    std::vector<double> psd,residualPsd; std::vector<Harmonic> harmonic;
};
double db(double power);
class Processor {
    struct Impl; std::unique_ptr<Impl> p_;
public:
    explicit Processor(Settings);~Processor();
    Result analyze(const std::vector<double>& mono,unsigned rate);
};
struct Recording { unsigned rate=0,channels=0; std::vector<double> samples; };
Recording loadWav(const std::string& path);
void saveWav(const std::string& path,const Recording&);
std::array<Result,2> analyzeRecording(const Recording&,Settings,double startSeconds=0,double durationSeconds=0);
void exportCsv(const std::string&,const std::array<Result,2>&);
void exportJson(const std::string&,const std::array<Result,2>&);
Recording demoRecording(unsigned rate=48000,double seconds=2);
}
