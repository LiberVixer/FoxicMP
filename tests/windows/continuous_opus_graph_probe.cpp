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

// Completed-file decoder control for the variable-packet preflight. This
// deliberately bypasses the progressive cache, whose admission remains narrow.
static int fileCapture(char** argv){
 require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");
 require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 HRESULT hr=S_OK;CComPtr<IGraphBuilder> graph;require(SUCCEEDED(graph.CoCreateInstance(CLSID_FilterGraph)),"graph");
 auto* splitter=new CMatroskaSourceFilter(nullptr,&hr);CComPtr<IBaseFilter> split=splitter;require(SUCCEEDED(hr),"splitter");
 CComQIPtr<IFileSourceFilter> file=split;require(file && SUCCEEDED(file->Load(CA2W(argv[1],CP_UTF8),nullptr)),"completed WebM load");
 auto* decoder=new CMpaDecFilter(nullptr,&hr);CComPtr<IBaseFilter> audio=decoder;require(SUCCEEDED(hr),"decoder");
 require(SUCCEEDED(decoder->SetDynamicRangeControl(false)) && SUCCEEDED(decoder->SetAVSyncCorrection(false)),"audio config");
 auto* capture=new Capture(&hr,argv[2],48000,std::stoul(argv[3]));CComPtr<IBaseFilter> sink=capture;require(SUCCEEDED(hr),"capture");
 require(SUCCEEDED(graph->AddFilter(split,L"Completed WebM")) && SUCCEEDED(graph->AddFilter(audio,L"Internal Opus")) && SUCCEEDED(graph->AddFilter(sink,L"Private PCM")),"add filters");
 // Select the audio pin by media type; file track order is not an API promise.
 bool connected=false;
 for(int i=0;i<splitter->GetPinCount();++i){
  auto* pin=splitter->GetPin(i);PIN_DIRECTION direction;
  if(SUCCEEDED(pin->QueryDirection(&direction)) && direction==PINDIR_OUTPUT &&
     SUCCEEDED(graph->ConnectDirect(pin,decoder->GetPin(0),nullptr))){connected=true;break;}
 }
 require(connected && SUCCEEDED(graph->ConnectDirect(decoder->GetPin(1),capture->GetPin(0),nullptr)),"connect Opus PCM");
 CComQIPtr<IMediaFilter> clock=graph;require(SUCCEEDED(clock->SetSyncSource(nullptr)),"null clock");
 CComQIPtr<IMediaControl> control=graph;CComQIPtr<IMediaEvent> events=graph;long event=0;
 require(SUCCEEDED(control->Run()) && events->WaitForCompletion(30000,&event)==S_OK && event==EC_COMPLETE,"completed Opus EOF");
 capture->Finish();auto count=capture->Count();require(SUCCEEDED(control->Stop()),"stop graph");
 events.Release();control.Release();clock.Release();file.Release();graph->RemoveFilter(split);graph->RemoveFilter(audio);graph->RemoveFilter(sink);
 graph.Release();split.Release();audio.Release();sink.Release();
 std::cout<<"{\"result\":\"PASS\",\"samples\":"<<count<<",\"audioDeviceOpened\":false,\"completedFileOnly\":true}"<<std::endl;
 CoUninitialize();return 0;
}

int main(int argc,char** argv)try{
 if(argc==4)return fileCapture(argv);
 const auto selection=argc>=11?std::string(argv[8]):std::string();
 const bool derived=selection=="derived-av";
 const bool nominal=selection=="nominal-vfr",autoCount=derived || nominal || selection=="auto-vfr";
 const bool growing=derived && argc==12;
 const bool simple=autoCount || selection=="simple-vfr",vfr=simple || selection=="vfr",cfr=selection=="cfr";
 require(argc==8 || (argc==9 && std::string(argv[8])=="ntsc") || vfr || cfr,"snapshot worker registry cache output channels samples");require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 auto path=[](const char* s){return std::filesystem::u8path(s);};auto json=text(argv[1]);auto model=p::Parse(json);auto owner=std::make_shared<p::StreamCacheOwner>(std::make_shared<p::StreamCacheBudget>());
 const auto frameClock=derived?harvester::WebmContinuousClock::VfrDerivedAv(std::stoull(argv[10])):nominal?harvester::WebmContinuousClock::VfrNominal(std::stoull(argv[10])):autoCount?harvester::WebmContinuousClock::VfrAutoCount(std::stoull(argv[10])):vfr?harvester::WebmContinuousClock::Vfr(std::stoull(argv[9]),std::stoull(argv[10])):cfr?harvester::WebmContinuousClock(uint32_t(std::stoul(argv[9])),uint32_t(std::stoul(argv[10]))):harvester::WebmContinuousClock();
 auto session=std::make_unique<p::CachedRemuxSession>(path(argv[1]).parent_path(),path(argv[3]),path(argv[4]),json,"clip",path(argv[2]),owner,64*1024*1024,p::StreamRemuxLimits{},derived?p::StreamRemuxProfile::WebmVp9OpusDerivedAv:nominal?p::StreamRemuxProfile::WebmVp9OpusNominalVfr:autoCount?p::StreamRemuxProfile::WebmVp9OpusSimpleAutoVfr:simple?p::StreamRemuxProfile::WebmVp9OpusSimpleVfr:vfr?p::StreamRemuxProfile::WebmVp9OpusVfr:cfr?p::StreamRemuxProfile::WebmVp9OpusCfr:argc==9?p::StreamRemuxProfile::WebmVp9OpusNtsc:p::StreamRemuxProfile::WebmVp9Opus,std::stoull(argv[7]),frameClock);
 const auto deadline=GetTickCount64()+30000;while(growing?(!session->status().output.initialized || !session->status().output.videoSamples || session->status().output.audioPresentationSamples<=648):!session->status().done){if(GetTickCount64()>=deadline){auto s=session->status();std::cerr<<"producer "<<s.source.code<<" "<<s.source.reason<<" "<<s.source.bytesRead<<" "<<s.output.written<<" "<<s.output.failure<<std::endl;throw std::runtime_error("cache timeout");}require(session->status().output.failure.empty(),session->status().output.failure.c_str());Sleep(5);}require(growing?!session->status().output.complete:session->status().output.complete,"cache phase");auto decoded=session->decoderSource(true);CStringW error;auto source=CHarvesterSession::OpenContinuous(decoded,model.duration,error);require(bool(source),CW2A(error,CP_UTF8));
 HRESULT hr=S_OK;CComPtr<IGraphBuilder> graph;require(SUCCEEDED(graph.CoCreateInstance(CLSID_FilterGraph)),"graph");
 auto* splitter=new CMatroskaSourceFilter(nullptr,&hr);CComPtr<IBaseFilter> split=splitter;require(SUCCEEDED(hr),"splitter");auto* nativeReader=new CHarvesterFileReader(source,1,hr);CComPtr<IAsyncReader> reader=nativeReader;require(SUCCEEDED(hr),"reader");require(SUCCEEDED(splitter->LoadReader(source->Path(1).c_str(),reader)),"WebM load reader");nativeReader->StartStreaming();
 auto* decoder=new CMpaDecFilter(nullptr,&hr);CComPtr<IBaseFilter> audio=decoder;require(SUCCEEDED(hr),"decoder");require(SUCCEEDED(decoder->SetDynamicRangeControl(false)) && SUCCEEDED(decoder->SetAVSyncCorrection(false)),"audio config");
 auto* capture=new Capture(&hr,argv[5],48000,std::stoul(argv[6]));CComPtr<IBaseFilter> sink=capture;require(SUCCEEDED(hr),"capture");require(SUCCEEDED(graph->AddFilter(split,L"WebM Opus")) && SUCCEEDED(graph->AddFilter(audio,L"Internal Opus")) && SUCCEEDED(graph->AddFilter(sink,L"Private PCM")),"add filters");
 require(SUCCEEDED(graph->ConnectDirect(splitter->GetPin(0),decoder->GetPin(0),nullptr)),"connect Opus decoder");require(SUCCEEDED(graph->ConnectDirect(decoder->GetPin(1),capture->GetPin(0),nullptr)),"connect PCM");
 CComQIPtr<IMediaFilter> clock=graph;require(SUCCEEDED(clock->SetSyncSource(nullptr)),"null clock");CComQIPtr<IMediaControl> control=graph;CComQIPtr<IMediaEvent> events=graph;require(SUCCEEDED(control->Run()),"run Opus graph");uint64_t partialSamples=0;long event=0;
 if(growing){const auto until=GetTickCount64()+15000;while(capture->Count()<48000){require(GetTickCount64()<until && source->Error().empty(),"No growing PCM prefix");Sleep(5);}partialSamples=capture->Count();require(events->WaitForCompletion(100,&event)!=S_OK && !session->status().output.complete,"Premature Opus EOF");session->publish(text(argv[11]));}
 require(events->WaitForCompletion(30000,&event)==S_OK && event==EC_COMPLETE,"Opus graph EOF");capture->Finish();auto count=capture->Count();
 require(SUCCEEDED(control->Stop()),"stop graph");auto released=source->ReleaseReceipt();source->Cancel();events.Release();control.Release();clock.Release();graph->RemoveFilter(split);graph->RemoveFilter(audio);graph->RemoveFilter(sink);graph.Release();split.Release();audio.Release();sink.Release();reader.Release();source.reset();require(p::SourceRetirement::waitEmpty(std::chrono::milliseconds(5000)) && released->load(),"reader release");decoded.reset();session->retire();session.reset();owner->collect();require(owner->count()==0,"cache release");
 std::cout<<"{\"partialPcmSamples\":"<<partialSamples<<",\"growingDecoder\":"<<(growing?"true":"false")<<",\"result\":\"PASS\",\"samples\":"<<count<<",\"audioDeviceOpened\":false,\"nativeOpusGraph\":true}"<<std::endl;CoUninitialize();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
