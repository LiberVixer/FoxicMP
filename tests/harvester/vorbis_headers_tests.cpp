// Exercise bounded Vorbis header paths, including truncation/mutation. GPL-3.0-or-later.
#include "DSUtil/HarvesterVorbis.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<uint8_t> original((std::istreambuf_iterator<char>(file)),{});
    if(original.empty())return 2;
    try{const auto info=harvester::VorbisConfiguration(original.data(),original.size());if(info.modes.empty() || info.rate!=48000)return 3;}
    catch(const std::exception& e){std::cerr<<e.what();return 4;}
    unsigned rejected=0,accepted=0;uint32_t random=0x21c0ffee;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned i=0;i<4000;++i){
        auto bytes=original;
        if(i<1000)bytes.resize(next()%bytes.size());
        else for(unsigned change=0,count=1+next()%4;change<count;++change)bytes[next()%bytes.size()]^=uint8_t(1u<<(next()%8));
        try{auto info=harvester::VorbisConfiguration(bytes.data(),bytes.size());++accepted;
            for(unsigned j=0;j<8;++j){uint8_t packet[8];for(auto& b:packet)b=uint8_t(next());try{info.Duration(packet,j);}catch(const std::exception&){}}
        }catch(const std::exception&){++rejected;}
    }
    if(rejected<1000)return 5;
    std::cout<<"Vorbis bounded mutations: "<<accepted<<" accepted, "<<rejected<<" rejected\n";return 0;
}
