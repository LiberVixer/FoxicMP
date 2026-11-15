// Test-only exercise of the production Opus framing parser. No playback/API.
#include "DSUtil/HarvesterOggOpus.h"
#include <fstream>
#include <iostream>
int main(int argc,char** argv)try{
    if(argc!=2)return 2;
    std::ifstream input(argv[1],std::ios::binary);
    if(!input)throw std::runtime_error("packet-file");
    unsigned count=0;
    for(;;){
        uint8_t size[4];input.read(reinterpret_cast<char*>(size),4);
        if(input.gcount()==0 && input.eof())break;
        if(input.gcount()!=4 || ++count>50000)throw std::runtime_error("packet-header");
        const auto n=harvester::oggopus::LE32(size);
        if(n>65536)throw std::runtime_error("packet-budget");
        std::vector<uint8_t> bytes(n);
        if(n && !input.read(reinterpret_cast<char*>(bytes.data()),n))throw std::runtime_error("packet-body");
        std::cout<<harvester::oggopus::Duration(bytes)<<'\n';
    }
    if(!count)throw std::runtime_error("empty-packet-file");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
