// Real private continuous AV graph, software H264 and internal AAC PCM window.
#include "filters/transform/MPCVideoDec/stdafx.h"
#include "DSUtil/Profile.h"
#include "DSUtil/HarvesterSession.h"
#include "filters/parser/BaseSplitter/AsyncReader.h"
#include "filters/parser/MP4Splitter/MP4Splitter.h"
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
  if(llabs(a-REFERENCE_TIME(samples*10000000/rate))>3 || a<last){invalid=true;return E_INVALIDARG;}
  last=a;samples+=size/quantum;file.write(reinterpret_cast<char*>(data),size);return file.good()?S_OK:E_FAIL;
 }
};
class VideoCapture final:public CNullVideoRenderer {
 std::mutex mutex;std::ofstream file;unsigned count=0;int stride=0,height=0;
public:
 VideoCapture(HRESULT* hr,const char* path):CNullVideoRenderer(nullptr,hr){Reset(path);}
 void Reset(const char* path){std::lock_guard<std::mutex> lock(mutex);file.close();file.open(path,std::ios::binary|std::ios::trunc);require(file.good(),"video capture file");count=0;}
 unsigned Count(){std::lock_guard<std::mutex> lock(mutex);return count;}
 HRESULT CheckMediaType(const CMediaType* mt)override{BITMAPINFOHEADER bih{};if(mt->subtype!=MEDIASUBTYPE_NV12 || !ExtractBIH(mt,&bih) || bih.biWidth<160 || bih.biWidth>512 || abs(bih.biHeight)<90 || abs(bih.biHeight)>128)return VFW_E_TYPE_NOT_ACCEPTED;stride=bih.biWidth;height=abs(bih.biHeight);return S_OK;}
 HRESULT DoRenderSample(IMediaSample* sample)override{
  std::lock_guard<std::mutex> lock(mutex);BYTE* bytes=nullptr;REFERENCE_TIME begin=0,end=0;
  if(FAILED(sample->GetPointer(&bytes)) || FAILED(sample->GetTime(&begin,&end)) || sample->GetActualDataLength()<stride*height*3/2 || end<begin)return E_FAIL;
  p::Sha256 hash;for(int y=0;y<90;++y)hash.update(bytes+y*stride,160);for(int y=0;y<45;++y)hash.update(bytes+stride*height+y*stride,160);
  file<<"{\"pts\":"<<begin<<",\"end\":"<<end<<",\"sha256\":\""<<hash.hex()<<"\"}\n";file.flush();++count;return file.good()?S_OK:E_FAIL;
 }
};
int main(int argc,char** argv)try{
 require(argc==11,"initial full complete worker registry cache output mode rate channels");const std::string mode=argv[8];const bool foreign=mode=="foreign",seeking=mode=="seek" || mode=="seek-ended",growingSeek=mode=="growing-seek",repeatSeek=mode=="repeat-seek";
 require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 auto path=[](const char* p){return std::filesystem::u8path(p);};const auto model=p::Parse(text(mode=="complete" || seeking?argv[3]:argv[1]));const auto input=text(mode=="complete" || seeking?argv[3]:argv[1]);
 const UINT rate=std::stoul(argv[9]),channels=std::stoul(argv[10]);auto owner=std::make_shared<p::StreamCacheOwner>(std::make_shared<p::StreamCacheBudget>());
 auto session=std::make_unique<p::CachedRemuxSession>(path(argv[1]).parent_path(),path(argv[5]),path(argv[6]),input,"clip",path(argv[4]),owner,64*1024*1024,p::StreamRemuxLimits{},p::StreamRemuxProfile::H264AacWindow);
 auto wait=[&](auto fn,unsigned ms=20000){const auto start=GetTickCount64();while(!fn()){require(GetTickCount64()-start<ms,"graph wait timeout");Sleep(10);}};
 wait([&]{const auto s=session->status();require(s.output.failure.empty(),s.output.failure.c_str());return s.output.fragments>0 && (!growingSeek || s.output.indexedEnd100ns>=15000000);});const auto worker=session->status().source.workerId;
 const auto startupIndexedEnd=session->status().output.indexedEnd100ns;
 auto decoded=session->decoderSource(true);CStringW error;auto source=CHarvesterSession::OpenContinuous(decoded,model.duration,error);require(bool(source),CW2A(error,CP_UTF8));require(source->TrackCount()==2,"two AV views missing");const auto receipt=source->RegisteredReaderId();
 HRESULT hr=S_OK;CComPtr<IGraphBuilder> graph;require(SUCCEEDED(graph.CoCreateInstance(CLSID_FilterGraph)),"graph init");
 auto* videoSplit=new CMP4SourceFilter(nullptr,&hr);CComPtr<IBaseFilter> vs=videoSplit;auto* audioSplit=new CMP4SourceFilter(nullptr,&hr);CComPtr<IBaseFilter> as=audioSplit;
 auto* vr=new CHarvesterFileReader(source,0,hr);CComPtr<IAsyncReader> videoReader=vr;auto* ar=new CHarvesterFileReader(source,1,hr);CComPtr<IAsyncReader> audioReader=ar;
 require(SUCCEEDED(videoSplit->LoadReader(source->Path(0).c_str(),videoReader)) && SUCCEEDED(audioSplit->LoadReader(source->Path(1).c_str(),audioReader)),"load AV projections");vr->StartStreaming();ar->StartStreaming();
 auto* video=new CMPCVideoDecFilter(nullptr,&hr);CComPtr<IBaseFilter> vd=video;
 require(SUCCEEDED(video->SetHwCodec(HWCodec_H264,false)) && SUCCEEDED(video->SetActiveCodecs(CODEC_H264)) && SUCCEEDED(video->SetThreadNumber(2)) && SUCCEEDED(video->SetFFMpegCodec(VDEC_H264,true)),"software H264 config");
 for(int i=0;i<PixFmt_count;++i)require(SUCCEEDED(video->SetSwPixelFormat(MPCPixelFormat(i),i==PixFmt_NV12)),"NV12 config");
 auto* audio=new CMpaDecFilter(nullptr,&hr);CComPtr<IBaseFilter> ad=audio;
 // Internal AAC decoding keeps its native float output; the capture pin validates it.
 require(SUCCEEDED(audio->SetDynamicRangeControl(false)) && SUCCEEDED(audio->SetAVSyncCorrection(false)),"private AAC config");
 const auto videoFile=std::string(argv[7])+".video.jsonl",prime=std::string(argv[7])+".prime";
 auto* ac=new Capture(&hr,seeking || repeatSeek?prime.c_str():argv[7],rate,channels,foreign);CComPtr<IBaseFilter> audioSink=ac;
 auto* vc=new VideoCapture(&hr,videoFile.c_str());CComPtr<IBaseFilter> videoSink=vc;
 for(auto f:{vs,as,vd,videoSink,audioSink})require(SUCCEEDED(graph->AddFilter(f,L"Private continuous AV")),"add AV graph filter");if(!foreign)require(SUCCEEDED(graph->AddFilter(ad,L"Private internal AAC")),"add AAC decoder");
 require(SUCCEEDED(graph->ConnectDirect(videoSplit->GetPin(0),video->GetPin(0),nullptr)) && SUCCEEDED(graph->ConnectDirect(video->GetPin(1),vc->GetPin(0),nullptr)),"connect H264 graph");
 if(foreign)require(SUCCEEDED(graph->ConnectDirect(audioSplit->GetPin(0),ac->GetPin(0),nullptr)),"connect foreign decoder");
 else require(SUCCEEDED(graph->ConnectDirect(audioSplit->GetPin(0),audio->GetPin(0),nullptr)) && SUCCEEDED(graph->ConnectDirect(audio->GetPin(1),ac->GetPin(0),nullptr)),"connect AAC graph");
 CComQIPtr<IMediaControl> control=graph;CComQIPtr<IMediaEvent> events=graph;CComQIPtr<IMediaFilter> clock=graph;CComQIPtr<IMediaSeeking> audioSeek=as,videoSeek=vs;CComQIPtr<IProgressiveAacPcmControl> window=ad;CComQIPtr<IProgressivePcmControl> oldWindow=ad;
 require(SUCCEEDED(clock->SetSyncSource(nullptr)),"null test clock");require(oldWindow->ArmPcmWindow(1,0,10,rate,0)==E_INVALIDARG,"MP3 entry widened to AAC");
 require(window->ArmAacPcmWindow(2,rate,channels,10,0,0,0)==E_INVALIDARG,"unknown AAC control version");
 if(growingSeek){
  REFERENCE_TIME target=10000000;require(!source->Complete() && source->CanSeek(target),"confirmed growing seek unavailable");
  require(SUCCEEDED(audioSeek->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)) && SUCCEEDED(videoSeek->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)),"initial growing AV seek");
 }
 require(SUCCEEDED(control->Run()),"run AV graph");double stopSeconds=0;uint64_t before=0;
 if(foreign){long event=0;require(events->WaitForCompletion(10000,&event)==S_OK && event==EC_ERRORABORT && ac->Count()==0 && !source->Error().empty(),"foreign AAC decoder accepted");}
 else if(mode=="growing" || growingSeek || repeatSeek){
  const bool audioEnded=growingSeek && decoded->audioWindow()->pcm.Samples()<=rate;
  wait([&]{return vc->Count()>=10 && (audioEnded || ac->Count()>0);});before=ac->Count();long event=0;require(events->WaitForCompletion(100,&event)!=S_OK && !source->Complete(),"early AV EOF");
  if(repeatSeek){
   require(SUCCEEDED(control->Pause()),"pause before repeated seek");session->publish(text(argv[2]));
   wait([&]{return session->status().source.code=="waiting" && session->status().output.indexedEnd100ns>30000000;});
   unsigned step=0;
   for(REFERENCE_TIME target:{10000000LL,20000000LL,5000000LL,10000000LL}){
    if(++step==4){ac->Reset(argv[7]);vc->Reset(videoFile.c_str());}
    require(source->CanSeek(target) && !source->Complete(),"growing repeated target unavailable");
    require(SUCCEEDED(audioSeek->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)) && SUCCEEDED(videoSeek->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)),"same graph repeated AV seek");
    require(events->WaitForCompletion(25,&event)!=S_OK,"repeated seek manufactured EOF");
   }
  }
  require(SUCCEEDED(control->Pause()),"pause AV graph");Sleep(100);const auto paused=ac->Count();if(!repeatSeek)session->publish(text(argv[2]));Sleep(200);require(ac->Count()==paused,"manual audio pause lost");require(SUCCEEDED(control->Run()),"resume AV graph");
  wait([&]{return session->status().source.code=="waiting";});require(events->WaitForCompletion(100,&event)!=S_OK,"EOF before writer complete");session->publish(text(argv[3]));
 }else if(mode=="stop" || mode=="failure"){
  wait([&]{return vc->Count()>=10 && ac->Count()>0;});if(mode=="failure")session->cancel();const auto start=GetTickCount64();source->Cancel();require(SUCCEEDED(control->Stop()),"stop AV graph");stopSeconds=double(GetTickCount64()-start)/1000.;require(stopSeconds<2,"AV stop latency");
 }
 if(!foreign && mode!="stop" && mode!="failure"){
  const auto finish=[&]{long event=0;const auto rc=events->WaitForCompletion(15000,&event);if(rc!=S_OK || event!=EC_COMPLETE)std::cerr<<"AV event "<<event<<" source "<<source->Error()<<std::endl;require(rc==S_OK && event==EC_COMPLETE,"AV graph EOF");ac->Finish();};finish();
  if(seeking){
   require(SUCCEEDED(control->Pause()),"pause completed AV graph");ac->Reset(argv[7]);vc->Reset(videoFile.c_str());REFERENCE_TIME target=mode=="seek-ended"?(rate==44100?100000:41000000):10000000;
   require(SUCCEEDED(audioSeek->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)) && SUCCEEDED(videoSeek->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)),"same graph AV seek");require(SUCCEEDED(control->Run()),"resume sought AV graph");finish();
  }
  DWORD flags=0;require(window->AacPcmWindowState(&flags)==S_OK && flags==(mode=="seek-ended" || ((growingSeek || repeatSeek) && decoded->audioWindow()->pcm.Samples()<=rate)?0u:3u),"AAC window not applied/cleared");require(source->Error().empty(),"AV source error");
 }
 const auto samples=ac->Count();const auto frames=vc->Count();auto released=source->ReleaseReceipt();source->Cancel();require(SUCCEEDED(control->Stop()),"final AV stop");DWORD flags=99;require(window->AacPcmWindowState(&flags)==S_OK && flags==0,"AAC window survives stop");
 window.Release();oldWindow.Release();audioSeek.Release();videoSeek.Release();clock.Release();events.Release();control.Release();
 for(auto f:{vs,as,vd,videoSink,audioSink})graph->RemoveFilter(f);if(!foreign)graph->RemoveFilter(ad);
 graph.Release();vs.Release();as.Release();vd.Release();ad.Release();videoSink.Release();audioSink.Release();videoReader.Release();audioReader.Release();source.reset();
 require(p::SourceRetirement::waitEmpty(std::chrono::seconds(5)) && released->load(),"AV source not retired");require(!decoded->stopped(),"graph cancelled shared cache source");session->cancel();wait([&]{return session->status().done;});require(session->status().source.workerId==worker,"worker changed");session.reset();owner->collect();require(decoded->status().removed,"AV cache retained");
 std::cout<<"{\"result\":\"PASS\",\"samples\":"<<samples<<",\"frames\":"<<frames<<",\"pcmBeforeComplete\":"<<before<<",\"startupIndexedEnd100ns\":"<<startupIndexedEnd<<",\"stopSeconds\":"<<stopSeconds<<",\"foreignRejected\":"<<(foreign?"true":"false")<<",\"released\":true,\"audioDeviceOpened\":false}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
