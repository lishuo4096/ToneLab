#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
namespace tone {
enum class Wave { Sine, Square, Saw, Triangle };
enum class Format { I16, I24, I32, F32, I24In32 };
struct Channel { double frequency=1000, amplitude=0.1; Wave wave=Wave::Sine; bool enabled=true; };
struct Controls {
    std::atomic<double> frequency{1000}, amplitude{0.1};
    std::atomic<int> wave{0}; std::atomic<bool> enabled{true};
    void set(const Channel& c);
    Channel get() const;
};
int bytes(Format f);
const char* formatName(Format f);
Format parseFormat(const std::string& s);
Wave parseWave(const std::string& s);
void validate(const Channel& c, unsigned rate);
double waveform(Wave wave, double phase, double increment);
void encode(double value, Format format, unsigned char* destination);
class Generator {
    unsigned rate_; Format format_; std::array<double,2> phase_{};
    std::array<double,2> gain_{};
public:
    Generator(unsigned rate, Format format):rate_(rate),format_(format){}
    void render(void* output, unsigned frames, const std::array<Channel,2>& channels);
};
}
