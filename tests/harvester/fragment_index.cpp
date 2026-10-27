#include "DSUtil/HarvesterMP4.h"
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    if(argc<3)return 2;
    try {
        std::ifstream file(argv[1],std::ios::binary);file.seekg(0,std::ios::end);const uint64_t length=file.tellg();
        harvester::FragmentIndex index;
        auto read=[&](uint64_t at,void* out,size_t size){file.clear();file.seekg(at);file.read((char*)out,size);return bool(file);};
        // Prefix updates include incomplete headers, moof and packet payloads.
        for(uint64_t prefix=0;prefix<length;prefix+=1009)index.Update(read,prefix,std::stoi(argv[2]),false,0,argc>3 && std::string(argv[3])=="hevc",argc>3 && std::string(argv[3])=="av1",argc>3 && std::string(argv[3])=="vp9",argc>3 && std::string(argv[3])=="opus");
        index.Update(read,length,std::stoi(argv[2]),true,0,argc>3 && std::string(argv[3])=="hevc",argc>3 && std::string(argv[3])=="av1",argc>3 && std::string(argv[3])=="vp9",argc>3 && std::string(argv[3])=="opus");
        if(argc>4) {
            const auto found=harvester::AudioSeekSample(index.Samples(),std::stoll(argv[4]),800000);
            std::cout<<found<<'\n';return 0;
        }
        std::cout<<"offset,size,start,end,decode,sync\n";
        for(const auto& s:index.Samples())std::cout<<s.offset<<','<<s.size<<','<<s.start<<','<<s.end<<','<<s.decode<<','<<s.sync<<'\n';
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
