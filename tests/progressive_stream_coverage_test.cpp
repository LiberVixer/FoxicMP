// Deterministic interval oracle: B-frame order, holes and signed boundaries.
// GPL-3.0-or-later.
#include "../shared/progressivestreamcoverage.h"
#include <iostream>
#include <random>

using foxicmp::progressive::StreamIndexedCoverage;
static unsigned checks=0;
static void check(bool value){++checks;if(!value)throw std::runtime_error("coverage oracle failed");}
static harvester::Sample sample(int64_t begin,int64_t end){harvester::Sample s;s.start=begin;s.end=end;return s;}
int main(){try{
    StreamIndexedCoverage coverage;std::vector<harvester::Sample> samples;
    auto add=[&](int64_t begin,int64_t end,int64_t expected){samples.push_back(sample(begin,end));coverage.update(samples);check(coverage.end100ns()==expected);};
    add(30,40,0);add(10,20,0);add(-10,10,20);add(50,60,20);
    add(20,30,40);add(35,55,60);add(12,17,60);add(-30,-1,60);add(80,80,60);
    coverage.update(samples);check(coverage.end100ns()==60);
    bool rejected=false;try{coverage.update({});}catch(const std::logic_error&){rejected=true;}check(rejected);
    // Independent integer-grid oracle. Shuffle PTS intervals as a decoder's
    // append-only index would; byte counts and decode timestamps are irrelevant.
    std::mt19937 rng(0xF0C1C);
    for(unsigned run=0;run<200;++run){
        StreamIndexedCoverage actual;std::vector<harvester::Sample> ordered;
        std::array<bool,256> covered{};
        for(unsigned step=0;step<200;++step){
            const int64_t begin=int(rng()%300)-30,end=begin+int(rng()%35);
            ordered.push_back(sample(begin,end));actual.update(ordered);
            for(int64_t i=(std::max)(int64_t(0),begin);i<(std::min)(int64_t(256),end);++i)covered[size_t(i)]=true;
            size_t expected=0;while(expected<covered.size() && covered[expected])++expected;
            check((std::min)(actual.end100ns(),int64_t(256))==int64_t(expected));
        }
    }
    StreamIndexedCoverage limits;
    limits.update({sample(INT64_MIN,0),sample(1,INT64_MAX)});check(limits.end100ns()==0);
    limits.update({sample(INT64_MIN,0),sample(1,INT64_MAX),sample(INT64_MIN,1)});check(limits.end100ns()==INT64_MAX);
    std::cout<<"{\"result\":\"PASS\",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
