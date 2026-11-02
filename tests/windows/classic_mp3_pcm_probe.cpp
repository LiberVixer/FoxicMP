// Exercise the actual internal Classic audio decoder without renderer/hardware.
// GPL-3.0-or-later.
#include "filters/transform/MpaDecFilter/stdafx.h"
#include "DSUtil/Profile.h"
#include "filters/transform/MpaDecFilter/MpaDecFilter.h"
#include "AudioTools/AudioHelper.h"
#include <mmreg.h>
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
static void require(bool v,const char* what){if(!v)throw std::runtime_error(what);}
int main(int argc,char** argv)try {
 require(argc==10,"packets output rate channels begin end decodeFirst mode expectedBytes");
 require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");
 require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 const auto rate=std::stoul(argv[3]),channels=std::stoul(argv[4]);
 const auto begin=std::stoull(argv[5]),end=std::stoull(argv[6]),first=std::stoull(argv[7]);const std::string mode=argv[8];
 HRESULT hr=S_OK;CMpaDecFilter parent(nullptr,&hr);require(SUCCEEDED(hr),"filter construct");
 CFFAudioDecoder decoder(&parent);CMediaType mt;mt.SetType(&MEDIATYPE_Audio);mt.SetFormatType(&FORMAT_WaveFormatEx);
 WAVEFORMATEX wf={};wf.wFormatTag=WAVE_FORMAT_MPEGLAYER3;wf.nChannels=WORD(channels);wf.nSamplesPerSec=DWORD(rate);wf.nBlockAlign=1;
 mt.SetFormat(reinterpret_cast<BYTE*>(&wf),sizeof(wf));
 require(!decoder.SetPcmWindow(begin,end,DWORD(rate),first),"uninitialized decoder accepted window");
 wf.wBitsPerSample=16;wf.nBlockAlign=WORD(channels*2);mt.SetFormat(reinterpret_cast<BYTE*>(&wf),sizeof(wf));
 require(decoder.Init(AV_CODEC_ID_MP2,&mt),"non-MP3 decoder init");
 require(!decoder.SetPcmWindow(begin,end,DWORD(rate),first),"non-MP3 decoder accepted window");
 wf.wBitsPerSample=0;wf.nBlockAlign=1;mt.SetFormat(reinterpret_cast<BYTE*>(&wf),sizeof(wf));
 require(decoder.Init(AV_CODEC_ID_MP3,&mt),"MP3 init");
 const std::string name=decoder.GetCodecName();
 require(!decoder.SetPcmWindow(5,4,DWORD(rate),0),"invalid window accepted");
 require(!decoder.SetPcmWindow(begin,end,32000,first),"wrong rate accepted");
 require(!decoder.SetPcmWindow(begin,end,DWORD(rate),begin+1),"bad preroll accepted");
 if(mode!="plain") {
  require(decoder.SetPcmWindow(begin,end,DWORD(rate),first),"window admission");
  require(!decoder.SetPcmWindow(end,begin,DWORD(rate),first),"invalid replacement accepted");
  if(mode=="flush")decoder.FlushBuffers();
  if(mode=="replace")require(decoder.Init(AV_CODEC_ID_MP3,&mt),"replacement init");
 }
 std::ifstream input(argv[1],std::ios::binary);require(input.good(),"packet input");std::ofstream output(argv[2],std::ios::binary);require(output.good(),"PCM output");
 uint64_t total=0,frame=0;std::vector<BYTE> decoded;REFERENCE_TIME previous=-1;
 const auto drain=[&]{
  size_t size=0;SampleFormat format=SAMPLE_FMT_NONE;REFERENCE_TIME time=INVALID_TIME;HRESULT result;
  while((result=decoder.ReceiveData(decoded,size,format,time))==S_OK) {
   if(!size)continue;
   const auto stride=channels*get_bytes_per_sample(format);require(stride && size%stride==0,"unaligned PCM");
   size_t samples=size/stride;std::vector<float> interleaved(samples*channels);convert_to_float(format,int(channels),int(samples),decoded.data(),interleaved.data());
   if(mode=="trim"){require(time>=0 && time>=previous,"non-monotonic trim timestamp");require(time==REFERENCE_TIME(total*10000000/rate),"wrong trim timestamp");previous=time;}
   output.write(reinterpret_cast<const char*>(interleaved.data()),interleaved.size()*sizeof(float));total+=samples;
  }
  require(result!=E_INVALIDARG,"PCM window rejected decoded format");
  require(!decoder.PcmWindowFailed(),"PCM window decoder failure");
 };
 uint32_t length;
 while(input.read(reinterpret_cast<char*>(&length),sizeof(length))) {
  require(length>0 && length<1048576,"bad packet length");std::vector<BYTE> packet(length+AV_INPUT_BUFFER_PADDING_SIZE);require(bool(input.read(reinterpret_cast<char*>(packet.data()),length)),"short packet");
  if(frame++*1152<first)continue;
  int consumed=0;require(SUCCEEDED(decoder.SendData(packet.data(),int(length),&consumed)),"send MP3 packet");require(consumed==int(length),"packet not consumed");drain();
 }
 require(input.eof(),"packet read error");require(SUCCEEDED(decoder.SendData(nullptr,0)),"decoder drain");drain();require(decoder.FinishPcmWindow(),"PCM window truncated before end");decoder.StreamFinish();output.close();
 require(total*channels*sizeof(float)==std::stoull(argv[9]),"PCM output size mismatch");
 std::cout<<"{\"result\":\"PASS\",\"decoder\":\""<<name<<"\",\"samples\":"<<total<<",\"mode\":\""<<mode<<"\"}"<<std::endl;
 CoUninitialize();return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
