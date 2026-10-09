// Regression: opening a progressive track must not freeze its initial fragment index.
#define _USE_MATH_DEFINES
#include <cmath>
#include <Windows.h>
#include "ExtLib/Bento4/Core/Ap4File.h"
#include "ExtLib/Bento4/Core/Ap4Movie.h"
#include "ExtLib/Bento4/Core/Ap4ByteStream.h"
#include "ExtLib/Bento4/Core/Ap4AtomFactory.h"
#include "ExtLib/Bento4/Core/Ap4Track.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc,char** argv) {
    assert(argc==3);
    for(int kind=0;kind<2;++kind) {
        std::ifstream input(argv[kind+1],std::ios::binary);
        std::vector<AP4_UI08> data{std::istreambuf_iterator<char>(input),{}};
        assert(!data.empty());
        for(bool headOnly:{true,false}) {
            auto stream=new AP4_MemoryByteStream(data.data(),AP4_Size(data.size()));
            {
                AP4_File file(*stream,false,AP4_AtomFactory::DefaultFactory,headOnly);
                auto movie=file.GetMovie();assert(movie);
                auto track=movie->GetTracks().FirstItem()->GetData();
                assert(movie->HasFragments()==(kind==0));
                if(headOnly && kind==0) {
                    assert(track->GetSampleCount()==0); // Only our growing index supplies these samples.
                    AP4_Offset position=0;stream->Tell(position);
                    assert(position<data.size()/2); // No moof/mdat/tail scanning.
                } else assert(track->GetSampleCount()>0);
            }
            stream->Release();
        }
    }
    std::cout << "Progressive metadata-only and ordinary MP4 parsing passed\n";
}
