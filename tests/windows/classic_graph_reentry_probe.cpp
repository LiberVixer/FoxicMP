// Real FilterGraph -> MP4 source -> software H264 decoder -> NV12 capture.
// Reentry occurs in a private source subclass while Graph.SetPositions is on
// the owner's stack. This is not the installed CMainFrame/STA message pump.
#include "filters/transform/MPCVideoDec/stdafx.h"
#include "DSUtil/Profile.h"
#include "DSUtil/HarvesterSession.h"
#include "filters/parser/BaseSplitter/AsyncReader.h"
#include "filters/parser/MP4Splitter/MP4Splitter.h"
#include "filters/transform/MPCVideoDec/MPCVideoDec.h"
#include "DSUtil/DSUtil.h"
#include "DSUtil/NullRenderers.h"
#include "../../shared/progressivestreamplayback.h"
#include "../../shared/progressiveretirement.h"
#include <fstream>
#include <iostream>
CModApp app;
namespace p=foxicmp::progressive;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static std::string hash(const char* bytes,size_t n){p::Sha256 d;d.update(bytes,n);return d.hex();}
class Capture final:public CNullVideoRenderer {
 std::mutex mutex;std::vector<std::string> hashes;int stride=0,height=0;
public:
 explicit Capture(HRESULT* hr):CNullVideoRenderer(nullptr,hr){}
 void reset(){std::lock_guard<std::mutex> lock(mutex);hashes.clear();}
 size_t count(){std::lock_guard<std::mutex> lock(mutex);return hashes.size();}
 std::vector<std::string> snapshot(){std::lock_guard<std::mutex> lock(mutex);return hashes;}
 HRESULT CheckMediaType(const CMediaType* mt)override{
  BITMAPINFOHEADER b{};if(mt->subtype!=MEDIASUBTYPE_NV12 || !ExtractBIH(mt,&b) || b.biWidth<160 || b.biWidth>512 || abs(b.biHeight)<90 || abs(b.biHeight)>128)return VFW_E_TYPE_NOT_ACCEPTED;
  stride=b.biWidth;height=abs(b.biHeight);return S_OK;
 }
 HRESULT DoRenderSample(IMediaSample* sample)override{
  BYTE* bytes=nullptr;REFERENCE_TIME a=0,b=0;if(FAILED(sample->GetPointer(&bytes)) || FAILED(sample->GetTime(&a,&b)) || b<a || sample->GetActualDataLength()<stride*height*3/2)return E_FAIL;
  p::Sha256 d;for(int y=0;y<90;++y)d.update(bytes+y*stride,160);for(int y=0;y<45;++y)d.update(bytes+stride*height+y*stride,160);
  std::lock_guard<std::mutex> lock(mutex);hashes.push_back(d.hex());return S_OK;
 }
};
class ReentrantSource final:public CMP4SourceFilter {
public:
 bool tracking=false;unsigned depth=0,maxDepth=0;DWORD owner=GetCurrentThreadId();std::function<void(bool)> callback;std::vector<int64_t> targets;std::vector<HRESULT> results;
 ReentrantSource(HRESULT* hr):CMP4SourceFilter(nullptr,hr){}
 STDMETHODIMP SetPositions(LONGLONG* current,DWORD flags,LONGLONG* stop,DWORD stopFlags)override{
  const bool tracked=tracking;if(tracked){check(GetCurrentThreadId()==owner,"graph seek changed owner thread");++depth;maxDepth=(std::max)(depth,maxDepth);targets.push_back(current?*current:-1);if(callback)callback(false);}
  const auto result=CMP4SourceFilter::SetPositions(current,flags,stop,stopFlags);
  if(tracked){results.push_back(result);if(callback)callback(true);--depth;}return result;
 }
};
int main(int argc,char** argv)try{
 check(argc==6,"input work prefix mode reordered");const auto root=std::filesystem::u8path(argv[2]);const std::string mode=argv[4];const bool reordered=std::string(argv[5])=="1";
 check(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 std::ifstream file(std::filesystem::u8path(argv[1]),std::ios::binary);check(bool(file),"fixture open");const std::vector<char> raw{std::istreambuf_iterator<char>(file),{}};const auto prefix=size_t(std::stoull(argv[3]));check(prefix<raw.size(),"prefix geometry");
 auto budget=std::make_shared<p::StreamCacheBudget>();p::StreamCacheOwner owner(budget);auto store=owner.create(root/"cache",root/"registry",p::lease_detail::RandomId(),1);
 const auto append=[&](size_t at,size_t end){while(at<end){const auto n=(std::min)(size_t(65536),end-at);store->append(at,std::vector<char>(raw.begin()+at,raw.begin()+at+n));at+=n;}};append(0,prefix);
 auto decoded=std::make_shared<p::StreamDecoderSource>(store,reordered?"continuous-h264-reordered-video-v1":"continuous-h264-video-v1");CStringW error;auto native=CHarvesterSession::OpenContinuous(decoded,240000000,error);check(bool(native),"OpenContinuous");const auto readerId=native->RegisteredReaderId();
 auto independent=decoded->reader();HRESULT hr=S_OK;auto* readerObject=new CHarvesterFileReader(native,0,hr);CComPtr<IAsyncReader> reader=readerObject;check(SUCCEEDED(hr),"reader init");readerObject->StartStreaming();
 CComPtr<IGraphBuilder> graph;check(SUCCEEDED(graph.CoCreateInstance(CLSID_FilterGraph)),"graph init");auto* sourceObject=new ReentrantSource(&hr);CComPtr<IBaseFilter> source=sourceObject;
 check(SUCCEEDED(sourceObject->LoadReader(native->Path(0).c_str(),reader)),"splitter load");auto* videoObject=new CMPCVideoDecFilter(nullptr,&hr);CComPtr<IBaseFilter> video=videoObject;
 check(SUCCEEDED(videoObject->SetHwCodec(HWCodec_H264,false)) && SUCCEEDED(videoObject->SetActiveCodecs(CODEC_H264)) && SUCCEEDED(videoObject->SetThreadNumber(2)) && SUCCEEDED(videoObject->SetFFMpegCodec(VDEC_H264,true)),"software H264 config");
 for(int i=0;i<PixFmt_count;++i)check(SUCCEEDED(videoObject->SetSwPixelFormat(MPCPixelFormat(i),i==PixFmt_NV12)),"NV12 config");
 auto* capture=new Capture(&hr);CComPtr<IBaseFilter> sink=capture;for(auto filter:{source,video,sink})check(SUCCEEDED(graph->AddFilter(filter,L"Private Classic reentry")),"add filter");
 check(SUCCEEDED(graph->ConnectDirect(sourceObject->GetPin(0),videoObject->GetPin(0),nullptr)) && SUCCEEDED(graph->ConnectDirect(videoObject->GetPin(1),capture->GetPin(0),nullptr)),"connect graph");
 CComQIPtr<IMediaControl> control=graph;CComQIPtr<IMediaSeeking> seeking=graph;CComQIPtr<IMediaEvent> events=graph;CComQIPtr<IMediaFilter> clock=graph;check(bool(seeking),"graph seeking unavailable");check(SUCCEEDED(clock->SetSyncSource(nullptr)),"null clock");
 const auto wait=[&](auto predicate){const auto start=GetTickCount64();while(!predicate()){check(GetTickCount64()-start<10000,"graph condition deadline");Sleep(5);}};
 check(SUCCEEDED(control->Run()),"run graph");wait([&]{return capture->count()>=25;});check(SUCCEEDED(control->Pause()),"pause graph");check(!native->Complete() && capture->count()<600,"premature EOF/full playback");
 p::ContinuousPlayback playback;playback.source=decoded;playback.duration=240000000;playback.output=[&]{return store->status();};playback.producerError=[&]{return store->status().failure;};
 bool fired=false,nestedBlocked=false;unsigned callbacks=0,nestedSubmits=0;sourceObject->tracking=true;
 sourceObject->callback=[&](bool after){
  if(fired || after!=(mode.rfind("after-",0)==0))return;fired=true;++callbacks;check(sourceObject->depth==1,"callback outside graph seek");
  if(mode.find("supersede")!=std::string::npos)playback.queueSeek(2);
  else if(mode=="same-target"){playback.clearPendingSeek();playback.queueSeek(1);}
  else if(mode=="nested-poll")nestedBlocked=playback.applyQueuedSeek([]{return true;},[](double){return true;},[&](double){++nestedSubmits;return true;})==p::ContinuousPlayback::QueuedApply::None;
  else playback.clearPendingSeek();
 };
 const auto apply=[&]{return playback.applyQueuedSeek([&]{return !decoded->stopped();},[&](double seconds){return native->CanSeek(int64_t(seconds*10000000.));},[&](double seconds){
  check(playback.queuedSubmissionAllowed(),"guard rejected graph seek");REFERENCE_TIME target=REFERENCE_TIME(seconds*10000000.);return SUCCEEDED(seeking->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning));});};
 playback.queueSeek(1);check(apply()==p::ContinuousPlayback::QueuedApply::Applied && fired && callbacks==1,"graph callback not applied");
 const auto firstState=playback.seekState;const auto firstPending=playback.pendingSeekSeconds;const auto firstRevision=playback.seekRevision;check(playback.growingSeeks==1,"first accepted seek quota");
 const bool superseding=mode.find("supersede")!=std::string::npos || mode=="same-target";
 if(superseding){check(firstState=="waiting-range" && firstPending==(mode=="same-target"?1.:2.) && firstRevision==2,"new target erased");check(apply()==p::ContinuousPlayback::QueuedApply::Applied && playback.growingSeeks==2 && playback.seekState=="applied" && playback.pendingSeekSeconds<0,"new target not applied");}
 else if(mode=="nested-poll")check(nestedBlocked && nestedSubmits==0 && firstState=="applied" && firstPending<0,"nested graph poll submitted twice");
 else check(firstState=="cancelled" && firstPending<0,"graph completion erased cancel");
 check(sourceObject->maxDepth==1 && sourceObject->targets.size()==(superseding?2u:1u) && native->RegisteredReaderId()==readerId,"graph/reader identity changed");
 auto targets=sourceObject->targets;auto results=sourceObject->results;sourceObject->tracking=false;sourceObject->callback={};const auto beforeComplete=capture->count();
 append(prefix,raw.size());store->producerClosed({},"",hash(raw.data(),raw.size()));wait([&]{return native->Complete();});check(SUCCEEDED(control->Run()),"run complete graph");long event=0;check(events->WaitForCompletion(15000,&event)==S_OK && event==EC_COMPLETE,"complete graph EOF");
 check(SUCCEEDED(control->Pause()),"pause for verification seek");capture->reset();REFERENCE_TIME target=100000000;check(SUCCEEDED(seeking->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)),"verification seek");check(SUCCEEDED(control->Run()),"verification run");check(events->WaitForCompletion(15000,&event)==S_OK && event==EC_COMPLETE,"verification EOF");const auto frames=capture->snapshot();check(frames.size()==350 && native->Error().empty(),"verification frame count/error");
 check(SUCCEEDED(control->Stop()),"stop graph");auto released=native->ReleaseReceipt();native->Cancel();seeking.Release();clock.Release();events.Release();control.Release();graph->RemoveFilter(sink);graph->RemoveFilter(video);graph->RemoveFilter(source);sink.Release();video.Release();source.Release();reader.Release();graph.Release();native.reset();
 check(p::SourceRetirement::waitEmpty(std::chrono::seconds(5)) && released->load() && !decoded->stopped(),"native context release");char sample[64];check(independent->read(0,sample,64).bytes==64 && hash(sample,64)==hash(raw.data(),64),"independent reader lost");independent->close();
 const auto ids=store->status().readerIds;check(store->retire(),"cache retirement");owner.collect();check(store->status().removed && budget->bytes()==0,"cache budget leak");
 for(const auto& id:ids){rapidjson::Document d;d.Parse(p::ReaderRequest(root/"registry","{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}").c_str());check(d["ok"].GetBool() && d["reader"]["released"].GetBool() && d["reader"]["filesClosed"].GetBool(),"reader proof");d.Parse(p::ReaderRequest(root/"registry","{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}").c_str());check(d["ok"].GetBool(),"reader forget");}
 rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("result","PASS",a);out.AddMember("mode",p::Value(mode.c_str(),a),a);out.AddMember("callbacks",callbacks,a);out.AddMember("sourceSeekDepth",1,a);out.AddMember("nestedPollBlocked",nestedBlocked,a);
 out.AddMember("firstState",p::Value(firstState.c_str(),a),a);out.AddMember("firstPending",firstPending,a);out.AddMember("firstRevision",firstRevision,a);out.AddMember("acceptedGrowingSeeks",playback.growingSeeks,a);out.AddMember("framesBeforeWriterComplete",uint64_t(beforeComplete),a);
 p::Value commands(rapidjson::kArrayType),codes(rapidjson::kArrayType),images(rapidjson::kArrayType);for(auto value:targets)commands.PushBack(value,a);for(auto value:results)codes.PushBack(int64_t(value),a);for(auto& value:frames)images.PushBack(p::Value(value.c_str(),a),a);out.AddMember("seekTargets100ns",commands,a);out.AddMember("seekResults",codes,a);out.AddMember("verificationFrameHashes",images,a);out.AddMember("closedAndForgottenReaders",uint64_t(ids.size()),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}
