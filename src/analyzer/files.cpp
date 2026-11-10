#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "analyzer/analysis.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
namespace analyzer {
Recording loadWav(const std::string& path){
    drwav wav{};auto wide=std::filesystem::u8path(path).wstring();if(!drwav_init_file_w(&wav,wide.c_str(),nullptr))throw std::runtime_error("Cannot open WAV (PCM/IEEE float/RF64 supported)");
    if(!wav.channels||wav.channels>32||wav.sampleRate<8000||wav.sampleRate>768000||wav.totalPCMFrameCount>33554432ULL/wav.channels){drwav_uninit(&wav);throw std::runtime_error("WAV is too large (256 MiB decoded limit) or has unsupported channel/rate metadata");}
    Recording r;r.rate=wav.sampleRate;r.channels=wav.channels;r.samples.resize(size_t(wav.totalPCMFrameCount)*wav.channels);
    std::vector<int32_t> integer(8192*r.channels);std::vector<float> floating(8192*r.channels);size_t position=0;
    while(position<wav.totalPCMFrameCount){auto count=std::min<drwav_uint64>(8192,wav.totalPCMFrameCount-position);drwav_uint64 frames=0;
        if(wav.translatedFormatTag==DR_WAVE_FORMAT_IEEE_FLOAT&&wav.bitsPerSample==64)frames=drwav_read_pcm_frames(&wav,count,r.samples.data()+position*r.channels);
        else if(wav.translatedFormatTag==DR_WAVE_FORMAT_IEEE_FLOAT){frames=drwav_read_pcm_frames_f32(&wav,count,floating.data());for(size_t i=0;i<frames*r.channels;++i)r.samples[position*r.channels+i]=floating[i];}
        else {frames=drwav_read_pcm_frames_s32(&wav,count,integer.data());for(size_t i=0;i<frames*r.channels;++i)r.samples[position*r.channels+i]=integer[i]/2147483648.;}
        position+=frames;if(!frames)break;
    }
    drwav_uninit(&wav);r.samples.resize(position*r.channels);if(!position)throw std::runtime_error("Empty or unreadable WAV");return r;
}
void saveWav(const std::string& path,const Recording& r){
    if(!r.channels||!r.rate||r.samples.empty()||r.samples.size()%r.channels)throw std::runtime_error("No complete audio frames to save");
    drwav_data_format format{drwav_container_riff,DR_WAVE_FORMAT_PCM,r.channels,r.rate,32};drwav file{};auto wide=std::filesystem::u8path(path).wstring();
    if(!drwav_init_file_write_w(&file,wide.c_str(),&format,nullptr))throw std::runtime_error("Cannot create WAV");
    std::vector<int32_t> buffer(8192*r.channels);size_t position=0;
    while(position<r.samples.size()){size_t count=std::min(buffer.size(),r.samples.size()-position);for(size_t i=0;i<count;++i)buffer[i]=int32_t(std::clamp<int64_t>(std::llround(std::clamp(r.samples[position+i],-1.,1.)*2147483648.),-2147483648LL,2147483647LL));if(drwav_write_pcm_frames(&file,count/r.channels,buffer.data())!=count/r.channels){drwav_uninit(&file);throw std::runtime_error("WAV write failed");}position+=count;}
    if(drwav_uninit(&file)!=DRWAV_SUCCESS)throw std::runtime_error("WAV finalization failed");
}
static void number(std::ostream& f,double x){if(std::isfinite(x))f<<std::setprecision(14)<<x;else f<<"null";}
void exportJson(const std::string& path,const std::array<Result,2>& r){std::ofstream f(std::filesystem::u8path(path));if(!f)throw std::runtime_error("Cannot create report JSON");f<<"{\n\"method\":\"Kaiser beta38 PSD + window-weighted sine fit; THD+N referenced to fundamental\",\n\"channels\":[\n";
    for(int ch=0;ch<2;++ch){if(ch)f<<",\n";auto& x=r[ch];f<<"{\"channel\":"<<ch+1<<",\"valid\":"<<(x.valid?"true":"false")<<",\"rate\":"<<x.rate<<",\"fft_size\":"<<x.size<<",\"averages\":"<<x.averages;
        for(auto v:{std::pair<const char*,double>{"frequency_hz",x.frequency},{"thd_db",x.thdDb},{"thdn_db",x.thdnDb},{"snr_db",x.snrDb},{"sinad_db",x.sinadDb},{"rms_dbfs",db(x.rms*x.rms)},{"peak_dbfs",db(x.peak*x.peak)},{"rbw_hz",x.rbw},{"bin_hz",x.df},{"low_hz",x.low},{"high_hz",x.high},{"clipped_fraction",x.clipped},{"compute_ms",x.computeMs}}){f<<",\""<<v.first<<"\":";number(f,v.second);}
        f<<",\"harmonics\":[";for(size_t i=0;i<x.harmonic.size();++i){if(i)f<<',';auto& h=x.harmonic[i];f<<"{\"order\":"<<h.order<<",\"frequency_hz\":";number(f,h.frequency);f<<",\"dbc\":";number(f,h.dbCarrier);f<<",\"noise_limited\":"<<(h.noiseLimited?"true":"false")<<'}';}f<<"]}";
    }f<<"\n]}\n";if(!f)throw std::runtime_error("Report write failed");
}
void exportCsv(const std::string& path,const std::array<Result,2>& r){std::ofstream f(std::filesystem::u8path(path));if(!f)throw std::runtime_error("Cannot create spectrum CSV");f<<"frequency_hz,ch1_dbfs_per_hz,ch2_dbfs_per_hz,ch1_residual_dbfs_per_hz,ch2_residual_dbfs_per_hz\n";size_t n=std::max(r[0].psd.size(),r[1].psd.size());for(size_t i=0;i<n;++i){f<<std::setprecision(14)<<i*(r[0].df?r[0].df:r[1].df);for(int residual=0;residual<2;++residual)for(int ch=0;ch<2;++ch){const auto& p=residual?r[ch].residualPsd:r[ch].psd;f<<',';if(i<p.size())f<<db(p[i]);}f<<'\n';}if(!f)throw std::runtime_error("CSV write failed");}
}
