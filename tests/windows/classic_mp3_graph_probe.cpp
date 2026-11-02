// Real private DirectShow source -> internal decoder -> PCM capture graph.
// GPL-3.0-or-later.
#include "filters/transform/MpaDecFilter/stdafx.h"
#include "DSUtil/Profile.h"
#include "DSUtil/HarvesterSession.h"
#include "filters/parser/BaseSplitter/AsyncReader.h"
#include "filters/parser/MP4Splitter/MP4Splitter.h"
#include "filters/transform/MpaDecFilter/MpaDecFilter.h"
#include "filters/switcher/AudioSwitcher/AudioSwitcher.h"
#include "DSUtil/DSUtil.h"
#include "DSUtil/NullRenderers.h"
#include "../../shared/progressiveretirement.h"
#include <mmreg.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3d12video.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
CModApp app;
static void require(bool v,const char* what){if(!v)throw std::runtime_error(what);}
static std::string read(const char* p){std::ifstream f(p,std::ios::binary);require(f.good(),"snapshot input");return {std::istreambuf_iterator<char>(f),{}};}
// Extend the production null audio renderer so AudioSwitcher recognizes its
// supported downstream contract; only capture/type checks are specialized.
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
  if(llabs(a-REFERENCE_TIME(samples*10000000/rate))>3 || a<last){invalid=true;return E_INVALIDARG;}
  last=a;samples+=size/quantum;file.write(reinterpret_cast<char*>(data),size);return file.good()?S_OK:E_FAIL;
 }
};
int main(int argc,char** argv)try {
 require(argc==10 || (argc==11 && (std::string(argv[10])=="--unsupported-decoder" || std::string(argv[10])=="--seek-incomplete" || std::string(argv[10])=="--audio-switcher")),"initial full complete output rate channels targetSample growing registry [--unsupported-decoder|--seek-incomplete]");
 const bool gain=argc==11 && std::string(argv[10])=="--audio-switcher";
 const bool foreign=argc==11 && std::string(argv[10])=="--unsupported-decoder",seekIncomplete=argc==11 && std::string(argv[10])=="--seek-incomplete";
 require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 const UINT rate=UINT(std::stoul(argv[5])),channels=UINT(std::stoul(argv[6]));const auto target=std::stoull(argv[7]);const bool growing=std::string(argv[8])=="1";
 CStringW error;std::string reason;const std::wstring registry=static_cast<LPCWSTR>(CA2W(argv[9],CP_UTF8));auto source=CHarvesterSession::OpenPublished(read(growing?argv[1]:argv[3]),error,registry,&reason,true,true);require(bool(source),CW2A(error,CP_UTF8));const auto readerId=source->RegisteredReaderId();require(!readerId.empty(),"missing grouped reader");
 CComPtr<IGraphBuilder> graph;require(SUCCEEDED(graph.CoCreateInstance(CLSID_FilterGraph)),"graph init");HRESULT hr=S_OK;
 auto* splitter=new CMP4SourceFilter(nullptr,&hr);CComPtr<IBaseFilter> sourceFilter=splitter;require(SUCCEEDED(hr),"MP4 source construct");
 auto* reader=new CHarvesterFileReader(source,0,hr);CComPtr<IAsyncReader> async=reader;require(SUCCEEDED(hr),"reader construct");require(SUCCEEDED(splitter->LoadReader(source->Path(0).c_str(),async)),"MP4 load reader");reader->StartStreaming();
 auto* decoder=new CMpaDecFilter(nullptr,&hr);CComPtr<IBaseFilter> decoderFilter=decoder;require(SUCCEEDED(hr),"decoder construct");
 CComPtr<IBaseFilter> switcherFilter;
 if(gain){auto* sw=new CAudioSwitcherFilter(nullptr,&hr);switcherFilter=sw;require(SUCCEEDED(hr),"audio switcher construct");require(SUCCEEDED(sw->SetChannelMixer(false,SPK_STEREO)) && SUCCEEDED(sw->SetAudioGain(-3.0)) && SUCCEEDED(sw->SetAutoVolumeControl(false,false,75,8)) && SUCCEEDED(sw->SetOutputFormats(SFMT_FLOAT)) && SUCCEEDED(sw->SetAudioTimeShift(0)),"configure private gain");require(SUCCEEDED(graph->AddFilter(switcherFilter,L"Private AudioSwitcher gain")),"add audio switcher");}
 const auto prime=std::string(argv[4])+".prime";auto* capture=new Capture(&hr,target?prime.c_str():argv[4],rate,channels,foreign);CComPtr<IBaseFilter> sink=capture;require(SUCCEEDED(hr),"capture construct");
 require(SUCCEEDED(graph->AddFilter(sourceFilter,L"Private progressive MP4")) && (foreign || SUCCEEDED(graph->AddFilter(decoderFilter,L"Internal MP3 decoder"))) && SUCCEEDED(graph->AddFilter(sink,L"PCM capture")),"add graph filters");
 if(foreign)require(SUCCEEDED(graph->ConnectDirect(splitter->GetPin(0),capture->GetPin(0),nullptr)),"unsupported decoder connection");
 else {require(SUCCEEDED(graph->ConnectDirect(splitter->GetPin(0),decoder->GetPin(0),nullptr)),"source-decoder connection");if(gain){require(SUCCEEDED(graph->ConnectDirect(decoder->GetPin(1),GetFirstPin(switcherFilter,PINDIR_INPUT),nullptr)),"decoder-switcher connection");require(SUCCEEDED(graph->ConnectDirect(GetFirstPin(switcherFilter,PINDIR_OUTPUT),capture->GetPin(0),nullptr)),"switcher-capture connection");}
 else require(SUCCEEDED(graph->ConnectDirect(decoder->GetPin(1),capture->GetPin(0),nullptr)),"decoder-capture connection");}
 CComQIPtr<IMediaFilter> clock=graph;require(SUCCEEDED(clock->SetSyncSource(nullptr)),"private null clock");CComQIPtr<IMediaControl> control=graph;CComQIPtr<IMediaEvent> events=graph;CComQIPtr<IMediaSeeking> seeking=sourceFilter;CComQIPtr<IProgressivePcmControl> window=decoderFilter;
 const auto close=[&]{const auto released=source->ReleaseReceipt();source->Cancel();window.Release();seeking.Release();events.Release();control.Release();clock.Release();
  if(switcherFilter)graph->RemoveFilter(switcherFilter);switcherFilter.Release();graph->RemoveFilter(sourceFilter);if(!foreign)graph->RemoveFilter(decoderFilter);graph->RemoveFilter(sink);
  graph.Release();sourceFilter.Release();decoderFilter.Release();sink.Release();async.Release();source.reset();
  require(foxicmp::progressive::SourceRetirement::waitEmpty(std::chrono::milliseconds(5000)) && released->load(),"graph did not close resources/release reader");};
 if(!foreign){require(window->ArmPcmWindow(2,0,10,rate,0)==E_INVALIDARG,"unknown PCM protocol accepted");require(window->ArmPcmWindow(1,10,9,rate,0)==E_INVALIDARG,"invalid window accepted");}
 require(SUCCEEDED(control->Run()),"run graph");
 if(foreign){long code=0;require(events->WaitForCompletion(10000,&code)==S_OK && code==EC_ERRORABORT,"unsupported decoder was not rejected");require(capture->Count()==0 && !source->Error().empty(),"unsupported decoder produced output");require(SUCCEEDED(control->Stop()),"stop rejected graph");close();std::cout<<"{\"result\":\"PASS\",\"unsupportedDecoderRejected\":true,\"samples\":0,\"released\":true}"<<std::endl;return 0;}
 if(growing){
  const auto deadline=GetTickCount64()+10000;while(capture->Count()<rate && GetTickCount64()<deadline)Sleep(10);
  if(capture->Count()<rate){DWORD state=0;window->PcmWindowState(&state);long code=0;events->WaitForCompletion(1,&code);std::cerr<<"partial PCM count "<<capture->Count()<<" flags "<<state<<" event "<<code<<" source "<<source->Error()<<" ready "<<source->ReadyEnd()<<std::endl;}
  require(capture->Count()>=rate,"no PCM before complete");long code=0;require(events->WaitForCompletion(50,&code)!=S_OK,"early EOF on partial file");
  require(source->Publish(read(argv[2]),error,&reason),CW2A(error,CP_UTF8));const auto tailDeadline=GetTickCount64()+10000;while(capture->Count()<rate*3 && GetTickCount64()<tailDeadline)Sleep(10);
  require(capture->Count()>=rate*3,"full prefix did not play");require(events->WaitForCompletion(100,&code)!=S_OK,"early EOF before writer complete");
  if(seekIncomplete){
   require(target>0,"missing incomplete seek target");require(SUCCEEDED(control->Pause()),"pause incomplete graph");capture->Reset(argv[4]);REFERENCE_TIME time=REFERENCE_TIME((target*10000000+rate/2)/rate);
   require(SUCCEEDED(seeking->SetPositions(&time,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)),"seek incomplete graph");require(SUCCEEDED(control->Run()),"resume incomplete graph");
   const auto seekDeadline=GetTickCount64()+10000;while(capture->Count()<rate/2 && GetTickCount64()<seekDeadline)Sleep(10);
   require(capture->Count()>=rate/2,"no PCM after incomplete seek");require(events->WaitForCompletion(100,&code)!=S_OK,"incomplete seek produced EOF");
  }
  require(source->Publish(read(argv[3]),error,&reason),CW2A(error,CP_UTF8));
 }
 const auto finish=[&]{long code=0;const auto done=events->WaitForCompletion(15000,&code);if(done!=S_OK || code!=EC_COMPLETE)std::cerr<<"graph completion "<<std::hex<<done<<" event "<<code<<" source "<<source->Error()<<std::endl;require(done==S_OK && code==EC_COMPLETE,"graph did not complete");capture->Finish();};
 finish();
 if(target && !seekIncomplete){
  require(SUCCEEDED(control->Pause()),"pause before graph seek");capture->Reset(argv[4]);REFERENCE_TIME time=REFERENCE_TIME((target*10000000+rate/2)/rate);
  require(SUCCEEDED(seeking->SetPositions(&time,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)),"same-graph seek");require(SUCCEEDED(control->Run()),"resume graph after seek");finish();
 }
 rapidjson::Document details;details.Parse(source->PlaybackDetails().c_str());require(details["mp3Index"]["gaplessTrimApplied"].GetBool() && std::string(details["mp3Index"]["pcmWindowState"].GetString())=="applied","session did not report actual decoder state");
 DWORD flags=0;require(window && window->PcmWindowState(&flags)==S_OK && flags==3,"window not actually applied");const auto count=capture->Count();require(source->RegisteredReaderId()==readerId,"reader changed on seek");
 require(window->ArmPcmWindow(1,0,10,rate,0)==E_INVALIDARG,"window replacement after PCM accepted");
 require(SUCCEEDED(control->Stop()),"stop graph");require(window->PcmWindowState(&flags)==S_OK && flags==0,"stop leaked PCM window");
 details.Parse(source->PlaybackDetails().c_str());require(!details["mp3Index"]["gaplessTrimApplied"].GetBool(),"stop leaked session PCM state");
 source->Cancel();source->ReportPcmState(3);details.Parse(source->PlaybackDetails().c_str());require(!details["mp3Index"]["gaplessTrimApplied"].GetBool(),"late delivery restored cancelled PCM state");
 require(source->Error().empty(),"source error");close();
 std::cout<<"{\"result\":\"PASS\",\"samples\":"<<count<<",\"sameGraphSeek\":"<<(target?"true":"false")<<",\"seekWhileIncomplete\":"<<(seekIncomplete?"true":"false")<<",\"growing\":"<<(growing?"true":"false")<<",\"windowApplied\":true,\"stopCleared\":true,\"readerPreserved\":true}"<<std::endl;return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
