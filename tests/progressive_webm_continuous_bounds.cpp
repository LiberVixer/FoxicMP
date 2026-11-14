#include "DSUtil/HarvesterWebMContinuous.h"
#include <fstream>
#include <iostream>
#include <random>
#include <string>
int main(int argc,char** argv){
    using Bytes=std::vector<uint8_t>;
    if(argc==2 && std::string(argv[1])=="--fuzz"){
        std::mt19937 random(196);
        for(unsigned i=0;i<10000;++i){Bytes raw(random()%1024);for(auto& b:raw)b=uint8_t(random());size_t at=0;
            try{harvester::ContinuousWebmVp9 reader([&](uint8_t* p,size_t n){n=(std::min)(n,raw.size()-at);if(n)std::memcpy(p,raw.data()+at,n);at+=n;return n;},raw.size(),1);uint8_t out[97];while(reader.read(out,sizeof(out))){};}catch(const std::exception&){}
        }
        std::cout<<"{\"result\":\"PASS\",\"fuzzCases\":10000}\n";return 0;
    }
    if(argc<3)return 2;
    try{
        std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> raw{std::istreambuf_iterator<char>(file),{}};if(!file)throw std::runtime_error("input-open-failed");size_t at=0;
        harvester::ContinuousWebmVp9 reader([&](uint8_t* p,size_t n){n=(std::min)({n,raw.size()-at,size_t(17)});if(n)std::memcpy(p,raw.data()+at,n);at+=n;return n;},raw.size(),std::stoull(argv[2]));
        uint8_t out[97];Bytes actual;for(size_t n;(n=reader.read(out,sizeof(out)))!=0;)actual.insert(actual.end(),out,out+n);
        if(actual!=raw)throw std::runtime_error("input-passthrough-changed");
        const auto packets=reader.pendingPackets();std::cout<<"offset,size,pts,key,sha256\n";
        for(const auto& p:packets){std::cout<<p.offset<<','<<p.bytes<<','<<p.milliseconds<<','<<p.key<<','<<p.sha256<<'\n';reader.consume(raw.data()+p.offset,p.bytes,p.milliseconds,p.key);}
        reader.finish();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
