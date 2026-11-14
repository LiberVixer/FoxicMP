// Real private continuous AV graph, software H264 and internal AAC PCM window.
#include "filters/transform/MPCVideoDec/stdafx.h"
#include "DSUtil/Profile.h"
#include "DSUtil/HarvesterSession.h"
#include "filters/parser/BaseSplitter/AsyncReader.h"
#include "filters/parser/MatroskaSplitter/MatroskaSplitter.h"
#include "filters/transform/MPCVideoDec/MPCVideoDec.h"
#include "filters/transform/MpaDecFilter/MpaDecFilter.h"
#include "DSUtil/DSUtil.h"
#include "DSUtil/NullRenderers.h"
#include "../../shared/progressivecachedremux.h"
#include "../../shared/progressiveretirement.h"
#include <mmreg.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3d12video.h>
#include <fstream>
#include <iostream>
CModApp app;
namespace p=foxicmp::progressive;
static void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
static std::string text(const char* p){std::ifstream f(p,std::ios::binary);require(f.good(),"snapshot input");return {std::istreambuf_iterator<char>(f),{}};}
class Capture final:public CNullAudioRenderer {
 std::mutex mutex;std::ofstream file;uint64_t samples=0;UINT rate,channels;size_t quantum;REFERENCE_TIME start=-1,last=-1;bool invalid=false,compressed=false;
public:
 Capture(HRESULT* hr,const char* output,UINT r,UINT ch,bool foreign=false):CNullAudioRenderer(nullptr,hr),rate(r),channels(ch),quantum(ch*4),compressed(foreign){Reset(output);}
 DECLARE_IUNKNOWN;
 void Reset(const char* path){std::lock_guard<std::mutex> l(mutex);file.close();file.open(path,std::ios::binary|std::ios::trunc);require(file.good(),"capture file");samples=0;start=last=-1;invalid=false;}
 uint64_t Count(){std::lock_guard<std::mutex> l(mutex);return samples;}
 void Finish(){std::lock_guard<std::mutex> l(mutex);file.flush();require(!invalid && file.good(),"invalid PCM/timestamps");}
 HRESULT CheckMediaType(const CMediaType* mt)override {
  if(compressed)return mt->majortype==MEDIATYPE_Audio?S_OK:VFW_E_TYPE_NOT_ACCEPTED;
  if(mt->majortype!=MEDIATYPE_Audio || mt->formattype!=FORMAT_WaveFormatEx || mt->cbFormat<sizeof(WAVEFORMATEX))return VFW_E_TYPE_NOT_ACCEPTED;
  const auto* w=reinterpret_cast<const WAVEFORMATEX*>(mt->Format());
  if(w->nSamplesPerSec!=rate || w->nChannels!=channels || w->wBitsPerSample!=32 || (mt->subtype!=MEDIASUBTYPE_IEEE_FLOAT))return VFW_E_TYPE_NOT_ACCEPTED;
  return S_OK;
 }
 HRESULT DoRenderSample(IMediaSample* sample)override {
  std::lock_guard<std::mutex> l(mutex);BYTE* data=nullptr;REFERENCE_TIME a=0,b=0;
  const auto size=sample->GetActualDataLength();
  if(FAILED(sample->GetPointer(&data)) || size<0 || size%quantum || FAILED(sample->GetTime(&a,&b)) || a<0 || b<a){invalid=true;return E_INVALIDARG;}
  if(start<0)start=a;
  // Integer DirectShow ticks may differ by a few ticks from exact sample time.
  if(a<last){invalid=true;return E_INVALIDARG;}
  last=a;samples+=size/quantum;file.write(reinterpret_cast<char*>(data),size);return file.good()?S_OK:E_FAIL;
 }
};

int main(int argc,char** argv)try{
 require(argc==8 || (argc==9 && std::string(argv[8])=="ntsc"),"snapshot worker registry cache output channels samples");require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 auto path=[](const char* s){return std::filesystem::u8path(s);};auto json=text(argv[1]);auto model=p::Parse(json);auto owner=std::make_shared<p::StreamCacheOwner>(std::make_shared<p::StreamCacheBudget>());
 auto session=std::make_unique<p::CachedRemuxSession>(path(argv[1]).parent_path(),path(argv[3]),path(argv[4]),json,"clip",path(argv[2]),owner,64*1024*1024,p::StreamRemuxLimits{},argc==9?p::StreamRemuxProfile::WebmVp9OpusNtsc:p::StreamRemuxProfile::WebmVp9Opus,std::stoull(argv[7]));
 const auto deadline=GetTickCount64()+30000;while(!session->status().done){if(GetTickCount64()>=deadline){auto s=session->status();std::cerr<<"producer "<<s.source.code<<" "<<s.source.reason<<" "<<s.source.bytesRead<<" "<<s.output.written<<" "<<s.output.failure<<std::endl;throw std::runtime_error("cache timeout");}require(session->status().output.failure.empty(),session->status().output.failure.c_str());Sleep(5);}require(session->status().output.complete,"cache incomplete");auto decoded=session->decoderSource(true);CStringW error;auto source=CHarvesterSession::OpenContinuous(decoded,model.duration,error);require(bool(source),CW2A(error,CP_UTF8));
 HRESULT hr=S_OK;CComPtr<IGraphBuilder> graph;require(SUCCEEDED(graph.CoCreateInstance(CLSID_FilterGraph)),"graph");
 auto* splitter=new CMatroskaSourceFilter(nullptr,&hr);CComPtr<IBaseFilter> split=splitter;require(SUCCEEDED(hr),"splitter");auto* nativeReader=new CHarvesterFileReader(source,1,hr);CComPtr<IAsyncReader> reader=nativeReader;require(SUCCEEDED(hr),"reader");require(SUCCEEDED(splitter->LoadReader(source->Path(1).c_str(),reader)),"WebM load reader");nativeReader->StartStreaming();
 auto* decoder=new CMpaDecFilter(nullptr,&hr);CComPtr<IBaseFilter> audio=decoder;require(SUCCEEDED(hr),"decoder");require(SUCCEEDED(decoder->SetDynamicRangeControl(false)) && SUCCEEDED(decoder->SetAVSyncCorrection(false)),"audio config");
 auto* capture=new Capture(&hr,argv[5],48000,std::stoul(argv[6]));CComPtr<IBaseFilter> sink=capture;require(SUCCEEDED(hr),"capture");require(SUCCEEDED(graph->AddFilter(split,L"WebM Opus")) && SUCCEEDED(graph->AddFilter(audio,L"Internal Opus")) && SUCCEEDED(graph->AddFilter(sink,L"Private PCM")),"add filters");
 require(SUCCEEDED(graph->ConnectDirect(splitter->GetPin(0),decoder->GetPin(0),nullptr)),"connect Opus decoder");require(SUCCEEDED(graph->ConnectDirect(decoder->GetPin(1),capture->GetPin(0),nullptr)),"connect PCM");
 CComQIPtr<IMediaFilter> clock=graph;require(SUCCEEDED(clock->SetSyncSource(nullptr)),"null clock");CComQIPtr<IMediaControl> control=graph;CComQIPtr<IMediaEvent> events=graph;require(SUCCEEDED(control->Run()),"run Opus graph");long event=0;require(events->WaitForCompletion(30000,&event)==S_OK && event==EC_COMPLETE,"Opus graph EOF");capture->Finish();auto count=capture->Count();
 require(SUCCEEDED(control->Stop()),"stop graph");auto released=source->ReleaseReceipt();source->Cancel();events.Release();control.Release();clock.Release();graph->RemoveFilter(split);graph->RemoveFilter(audio);graph->RemoveFilter(sink);graph.Release();split.Release();audio.Release();sink.Release();reader.Release();source.reset();require(p::SourceRetirement::waitEmpty(std::chrono::milliseconds(5000)) && released->load(),"reader release");decoded.reset();session->retire();session.reset();owner->collect();require(owner->count()==0,"cache release");
 std::cout<<"{\"result\":\"PASS\",\"samples\":"<<count<<",\"audioDeviceOpened\":false,\"nativeOpusGraph\":true}"<<std::endl;CoUninitialize();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
