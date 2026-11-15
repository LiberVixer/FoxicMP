// Bounded real-packet mutations for ASan/UBSan; no product admission changes.
#include "DSUtil/HarvesterOggOpus.h"
#include <fstream>
#include <iostream>
#include <random>
int main(int argc,char** argv)try{
    if(argc!=2)return 2;
    std::ifstream input(argv[1],std::ios::binary);
    if(!input)throw std::runtime_error("packet-file");
    std::vector<std::vector<uint8_t>> packets;uint64_t totalBytes=0;
    while(packets.size()<50000){
        uint8_t header[4];input.read(reinterpret_cast<char*>(header),4);
        if(input.gcount()==0 && input.eof())break;
        if(input.gcount()!=4)throw std::runtime_error("packet-header");
        const auto size=harvester::oggopus::LE32(header);
        if(!size || size>65536 || (totalBytes+=size)>16*1024*1024)throw std::runtime_error("packet-budget");
        std::vector<uint8_t> data(size);
        if(!input.read(reinterpret_cast<char*>(data.data()),size))throw std::runtime_error("packet-body");
        packets.push_back(std::move(data));
    }
    if(packets.empty() || !input.eof())throw std::runtime_error("packet-count");
    std::mt19937 random(1106);unsigned accepted=0,rejected=0;
    for(unsigned i=0;i<20000;++i){
        auto data=packets[random()%packets.size()];
        switch(i%4){
            case 0:data.resize(random()%(data.size()+1));break;
            case 1:data[random()%data.size()]^=uint8_t(1u<<(random()%8));break;
            case 2:for(unsigned j=0;j<4 && j<data.size();++j)data[j]=uint8_t(random());break;
            default:data.insert(data.end(),random()%33,uint8_t(random()));break;
        }
        try{const auto samples=harvester::oggopus::Duration(data);
            if(samples<120 || samples>5760 || samples%120)throw std::logic_error("invalid admitted duration");
            ++accepted;
        }catch(const std::logic_error&){throw;}
         catch(const std::runtime_error&){++rejected;}
    }
    std::cout<<"{\"result\":\"PASS\",\"mutations\":20000,\"framingAccepted\":"<<accepted
             <<",\"framingRejected\":"<<rejected<<",\"decoderValidityClaimed\":false}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
