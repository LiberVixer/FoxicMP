// Bounded standalone M4A parser mutation test, suitable for ASan/UBSan.
#include "DSUtil/HarvesterM4A.h"
#include <fstream>
#include <iostream>
#include <random>
int main(int argc,char** argv)try{
    if(argc!=2)return 2;std::ifstream f(argv[1],std::ios::binary);if(!f)return 2;
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};
    const auto movie=harvester::BoxData{bytes.data(),bytes.size()}.One("moov");
    harvester::m4a::Info good;good.Parse(movie);
    std::mt19937 random(0x4d3441);size_t accepted=0,rejected=0;
    for(size_t i=0;i<10000;++i){std::vector<uint8_t> copy(movie.data,movie.data+movie.size);copy[random()%copy.size()]^=uint8_t(1+random()%255);
        try{harvester::m4a::Info info;info.Parse({copy.data(),copy.size()});++accepted;}catch(const std::exception&){++rejected;}}
    for(size_t i=0;i<movie.size;++i)try{harvester::m4a::Info info;info.Parse({movie.data,i});return 1;}catch(const std::exception&){}
    std::cout<<"{\"result\":\"PASS\",\"mutations\":10000,\"accepted\":"<<accepted<<",\"rejected\":"<<rejected<<",\"truncations\":"<<movie.size<<"}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
