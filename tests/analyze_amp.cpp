#include "../src/amp.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

int main(int argc,char** argv) {
    if(argc==6 && std::string(argv[1])=="--impulse") {
        const auto h=amp::make_eq_impulse(std::stof(argv[3]),std::stof(argv[4]),std::stof(argv[5]));
        FILE* f=std::fopen(argv[2],"wb");
        if(!f) return 1;
        const auto written=std::fwrite(h.data(),sizeof(float),h.size(),f);
        std::fclose(f);
        return written==h.size() ? 0 : 1;
    }
    if(argc!=1) { std::fprintf(stderr,"Usage: %s [--impulse output.f32 bass middle treble]\n",argv[0]); return 1; }
    auto amp=std::make_unique<amp::Amp>();
    amp::Params p; p.drive=true;
    amp->prepare(48000,p);
    const auto start=std::chrono::steady_clock::now();
    double sum=0;
    for(int i=0;i<480000;++i) sum+=amp->process(0.05f*std::sin(i*0.1f),p);
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::printf("10 seconds of audio: %.4f seconds processing, %.2f%% of one realtime core; checksum %.6f\n",seconds,seconds*10,sum);
}
