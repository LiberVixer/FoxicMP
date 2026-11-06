// Actual Classic AAC decoder with an explicit raw PCM window; no renderer.
#include "filters/transform/MpaDecFilter/stdafx.h"
#include "DSUtil/Profile.h"
#include "filters/transform/MpaDecFilter/MpaDecFilter.h"
#include "AudioTools/AudioHelper.h"
#include <wmcodecdsp.h>
#include <moreuuids.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3d12video.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
extern "C" {
#include "ExtLib/ffmpeg/libavcodec/avcodec.h"
}
CModApp app;
static void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(int argc,char** argv)try{
 require(argc==11,"packets output asc rate channels samples skip target mode expectedBytes");
 require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 const auto rate=std::stoul(argv[4]),channels=std::stoul(argv[5]);const auto samples=std::stoull(argv[6]),skip=std::stoull(argv[7]),target=std::stoull(argv[8]);const std::string mode=argv[9];
 require(skip<=1024,"initial skip bound");harvester::pcm::AacWindow plan;require(plan.Configure(rate,channels,samples,skip),"AAC window geometry");
 std::ifstream config(argv[3],std::ios::binary);std::vector<BYTE> asc((std::istreambuf_iterator<char>(config)),{});require(asc.size()>0 && asc.size()<=1024,"AAC configuration");
 WAVEFORMATEX wf{};wf.wFormatTag=0xff;wf.nChannels=WORD(channels);wf.nSamplesPerSec=DWORD(rate);wf.nBlockAlign=1;wf.cbSize=WORD(asc.size());
 std::vector<BYTE> format(sizeof(wf)+asc.size());memcpy(format.data(),&wf,sizeof(wf));memcpy(format.data()+sizeof(wf),asc.data(),asc.size());
 CMediaType mt;mt.SetType(&MEDIATYPE_Audio);mt.SetSubtype(&MEDIASUBTYPE_RAW_AAC1);mt.SetFormatType(&FORMAT_WaveFormatEx);mt.SetFormat(format.data(),ULONG(format.size()));
 HRESULT hr=S_OK;CMpaDecFilter parent(nullptr,&hr);require(SUCCEEDED(hr),"filter construct");CFFAudioDecoder decoder(&parent);
 require(!decoder.SetAacPcmWindow(plan),"uninitialized decoder accepted AAC window");require(decoder.Init(AV_CODEC_ID_AAC,&mt),"AAC init");
 const std::string codecName=decoder.GetCodecName();
 require(!decoder.SetPcmWindow(0,samples,rate,0),"MP3-only entry accepted AAC");
 if(mode!="plain" && mode!="seek-raw"){
  require(decoder.SetAacPcmWindow(plan,target),"AAC window admission");require(!decoder.SetAacPcmWindow(plan,samples),"invalid replacement accepted");
  if(mode=="flush")decoder.FlushBuffers();
  if(mode=="replace")require(decoder.Init(AV_CODEC_ID_AAC,&mt),"replacement init");
 }
 std::ifstream input(argv[1],std::ios::binary);std::ofstream output(argv[2],std::ios::binary);require(input.good() && output.good(),"private files");
 std::vector<BYTE> decoded;uint64_t total=0;bool recording=mode!="seek-raw";
 const auto drain=[&]{size_t size=0;SampleFormat format=SAMPLE_FMT_NONE;REFERENCE_TIME time=INVALID_TIME;HRESULT result;
  while((result=decoder.ReceiveData(decoded,size,format,time))==S_OK){if(!size)continue;
   const auto stride=channels*get_bytes_per_sample(format);require(stride && size%stride==0,"PCM alignment");const size_t n=size/stride;
   std::vector<float> pcm(n*channels);convert_to_float(format,int(channels),int(n),decoded.data(),pcm.data());
   if(mode=="trim")require(time==REFERENCE_TIME(total*10000000/rate),"PCM timestamp");
   if(recording){output.write(reinterpret_cast<const char*>(pcm.data()),pcm.size()*sizeof(float));total+=n;}
  }
  require(result!=E_INVALIDARG && !decoder.PcmWindowFailed(),"PCM decode/window failure");
 };
 uint32_t length;
 while(input.read(reinterpret_cast<char*>(&length),sizeof(length))){require(length>0 && length<1048576,"packet length");std::vector<BYTE> data(length+AV_INPUT_BUFFER_PADDING_SIZE);require(bool(input.read(reinterpret_cast<char*>(data.data()),length)),"short packet");int used=0;require(SUCCEEDED(decoder.SendData(data.data(),int(length),&used)) && used==int(length),"AAC packet decode");drain();}
 require(input.eof(),"packet read");require(SUCCEEDED(decoder.SendData(nullptr,0)),"AAC drain");drain();
 if(mode=="seek-raw"){
  // Independent untrimmed oracle: reproduce decoder history and seek preroll.
  // AAC PNS state survives avcodec_flush_buffers; a full-file suffix is not
  // a valid sample-value oracle after seeking, though its length is exact.
  decoder.FlushBuffers();input.clear();input.seekg(0);recording=true;uint64_t packet=0;
  while(input.read(reinterpret_cast<char*>(&length),sizeof(length))){
   require(length>0 && length<1048576,"seek packet length");std::vector<BYTE> data(length+AV_INPUT_BUFFER_PADDING_SIZE);require(bool(input.read(reinterpret_cast<char*>(data.data()),length)),"short seek packet");
   if(packet++<target)continue;int used=0;require(SUCCEEDED(decoder.SendData(data.data(),int(length),&used)) && used==int(length),"seek AAC decode");drain();
  }
  require(input.eof(),"seek packet read");require(SUCCEEDED(decoder.SendData(nullptr,0)),"seek AAC drain");drain();
 }
 require(decoder.FinishPcmWindow(),"AAC PCM window truncated before end");decoder.StreamFinish();output.close();
 require(total*channels*sizeof(float)==std::stoull(argv[10]),"PCM sample count");
 std::cout<<"{\"result\":\"PASS\",\"samples\":"<<total<<",\"mode\":\""<<mode<<"\",\"decoder\":\""<<codecName<<"\"}"<<std::endl;CoUninitialize();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
