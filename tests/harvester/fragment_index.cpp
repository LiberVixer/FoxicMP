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
        for(uint64_t prefix=0;prefix<length;prefix+=1009)index.Update(read,prefix,std::stoi(argv[2]));
        index.Update(read,length,std::stoi(argv[2]),true);
        std::cout<<"offset,size,start,end,decode,sync\n";
        for(const auto& s:index.Samples())std::cout<<s.offset<<','<<s.size<<','<<s.start<<','<<s.end<<','<<s.decode<<','<<s.sync<<'\n';
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
