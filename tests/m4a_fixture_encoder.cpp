// Test-only deterministic AAC packets, using an external FDK-AAC encoder.
// GPL-3.0-or-later. Not linked or shipped with the player.
#include "aacenc_lib.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>
static void check(bool b){if(!b)throw std::runtime_error("FDK fixture encoding failed");}
static void u32(FILE* f,unsigned n){for(int i=3;i>=0;--i)std::fputc((n>>(i*8))&255,f);}
int main(int argc,char** argv)try{
    check(argc==6);const unsigned rate=std::stoul(argv[1]),channels=std::stoul(argv[2]),aot=std::stoul(argv[3]),samples=std::stoul(argv[4]);
    check((rate==44100 || rate==48000) && (channels==1 || channels==2) && (aot==2 || aot==5 || aot==29) && samples && samples<=rate*60);
    HANDLE_AACENCODER enc=nullptr;check(aacEncOpen(&enc,0,channels)==AACENC_OK);
    const auto param=[&](AACENC_PARAM p,unsigned v){check(aacEncoder_SetParam(enc,p,v)==AACENC_OK);};
    param(AACENC_AOT,aot);param(AACENC_SAMPLERATE,rate);param(AACENC_CHANNELMODE,channels==1?MODE_1:MODE_2);
    param(AACENC_CHANNELORDER,1);param(AACENC_BITRATE,aot==2?96000:48000);param(AACENC_TRANSMUX,TT_MP4_RAW);
    param(AACENC_SIGNALING_MODE,2);param(AACENC_AFTERBURNER,1);check(aacEncEncode(enc,nullptr,nullptr,nullptr,nullptr)==AACENC_OK);
    AACENC_InfoStruct info{};check(aacEncInfo(enc,&info)==AACENC_OK);
    FILE* file=std::fopen(argv[5],"wb");check(file);u32(file,info.confSize);std::fwrite(info.confBuf,1,info.confSize,file);
    u32(file,info.frameLength);u32(file,info.nDelayCore);u32(file,info.nDelay);
    std::vector<INT_PCM> pcm(info.frameLength*channels);uint8_t bytes[20480];unsigned at=0;
    for(unsigned guard=0;guard<samples/info.frameLength+32;++guard){
        const auto n=std::min(unsigned(info.frameLength),samples-at);
        for(unsigned i=0;i<n;++i)for(unsigned c=0;c<channels;++c)pcm[i*channels+c]=INT_PCM(8000*std::sin(6.283185307179586*(c?503:997)*(at+i)/rate));
        int id=IN_AUDIO_DATA,size=int(n*channels*sizeof(INT_PCM)),element=sizeof(INT_PCM);void* data=pcm.data();
        AACENC_BufDesc in{1,&data,&id,&size,&element};int oid=OUT_BITSTREAM_DATA,osize=sizeof(bytes),oelement=1;void* outdata=bytes;
        AACENC_BufDesc out{1,&outdata,&oid,&osize,&oelement};AACENC_InArgs args{};args.numInSamples=n?int(n*channels):-1;AACENC_OutArgs result{};
        const auto status=aacEncEncode(enc,&in,&out,&args,&result);if(status==AACENC_ENCODE_EOF){std::fclose(file);aacEncClose(&enc);return 0;}
        check(status==AACENC_OK);at+=result.numInSamples/channels;
        if(result.numOutBytes){u32(file,result.numOutBytes);check(std::fwrite(bytes,1,result.numOutBytes,file)==unsigned(result.numOutBytes));}
    }
    throw std::runtime_error("FDK fixture flush did not finish");
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
