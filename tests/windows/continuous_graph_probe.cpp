// Private production Classic splitter -> software H.264 decoder -> NV12 capture.
// No installed application, display renderer, audio device or public IPC.
#include "filters/transform/MPCVideoDec/stdafx.h"
#include "DSUtil/Profile.h"
#include "DSUtil/HarvesterSession.h"
#include "filters/parser/BaseSplitter/AsyncReader.h"
#include "filters/parser/MP4Splitter/MP4Splitter.h"
#include "filters/transform/MPCVideoDec/MPCVideoDec.h"
#include "DSUtil/DSUtil.h"
#include "DSUtil/NullRenderers.h"
#include "../../shared/progressivecachedremux.h"
#include "../../shared/progressiveretirement.h"
#include <fstream>
#include <iostream>
CModApp app;
namespace p=foxicmp::progressive;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static std::string text(const char* path){std::ifstream f(path,std::ios::binary);check(f.good(),"input JSON");return {std::istreambuf_iterator<char>(f),{}};}
class Capture final:public CNullVideoRenderer {
    std::mutex mutex;std::ofstream file;unsigned count=0;int stride=0,height=0;
public:
    Capture(HRESULT* hr,const char* path):CNullVideoRenderer(nullptr,hr){Reset(path);}
    void Reset(const char* path){std::lock_guard<std::mutex> lock(mutex);file.close();file.open(path,std::ios::binary|std::ios::trunc);count=0;check(file.good(),"capture file");}
    unsigned Count(){std::lock_guard<std::mutex> lock(mutex);return count;}
    HRESULT CheckMediaType(const CMediaType* mt)override{
        BITMAPINFOHEADER bih{};if(mt->subtype!=MEDIASUBTYPE_NV12 || !ExtractBIH(mt,&bih) || bih.biWidth<160 || bih.biWidth>512 || abs(bih.biHeight)<90 || abs(bih.biHeight)>128)return VFW_E_TYPE_NOT_ACCEPTED;
        stride=bih.biWidth;height=abs(bih.biHeight);return S_OK;
    }
    HRESULT DoRenderSample(IMediaSample* sample)override{
        std::lock_guard<std::mutex> lock(mutex);BYTE* bytes=nullptr;REFERENCE_TIME begin=0,end=0;
        if(FAILED(sample->GetPointer(&bytes)) || FAILED(sample->GetTime(&begin,&end)) || sample->GetActualDataLength()<stride*height*3/2 || end<begin)return E_FAIL;
        p::Sha256 hash;for(int y=0;y<90;++y)hash.update(bytes+y*stride,160);for(int y=0;y<45;++y)hash.update(bytes+stride*height+y*stride,160);
        file<<"{\"pts\":"<<begin<<",\"end\":"<<end<<",\"sha256\":\""<<hash.hex()<<"\"}\n";file.flush();++count;return file.good()?S_OK:E_FAIL;
    }
};
int main(int argc,char** argv)try{
    check(argc==9 || (argc==10 && std::string(argv[9])=="reordered"),"initial full complete worker registry cache output mode [reordered]");const auto mode=std::string(argv[8]);
    check(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
    auto path=[](const char* s){return std::filesystem::u8path(s);};const auto registry=path(argv[5]);const auto input=text(mode=="complete" || mode=="seek"?argv[3]:argv[1]);const auto model=p::Parse(input);
    const auto profile=argc==10?p::StreamRemuxProfile::H264ReorderedVideo:p::StreamRemuxProfile::H264Video;
    auto owner=std::make_shared<p::StreamCacheOwner>(std::make_shared<p::StreamCacheBudget>());auto session=std::make_unique<p::CachedRemuxSession>(path(argv[1]).parent_path(),registry,path(argv[6]),input,"clip",path(argv[4]),owner,64*1024*1024,p::StreamRemuxLimits{},profile);
    const auto wait=[&](auto predicate,DWORD ms=15000){const auto start=GetTickCount64();while(!predicate()){check(GetTickCount64()-start<ms,"native wait timeout");Sleep(10);}};
    wait([&]{return session->status().output.fragments>0;});auto decoded=session->decoderSource();CStringW error;
    auto source=CHarvesterSession::OpenContinuous(decoded,model.duration,error);if(!source)throw std::runtime_error(CW2A(error,CP_UTF8));const auto readerId=source->RegisteredReaderId();check(!readerId.empty(),"missing output receipt");
    HRESULT hr=S_OK;CComPtr<IGraphBuilder> graph;check(SUCCEEDED(graph.CoCreateInstance(CLSID_FilterGraph)),"graph init");
    auto* splitter=new CMP4SourceFilter(nullptr,&hr);CComPtr<IBaseFilter> split=splitter;check(SUCCEEDED(hr),"splitter init");
    auto* reader=new CHarvesterFileReader(source,0,hr);CComPtr<IAsyncReader> async=reader;check(SUCCEEDED(hr),"reader init");check(SUCCEEDED(splitter->LoadReader(source->Path(0).c_str(),async)),"splitter open cache");reader->StartStreaming();
    auto* video=new CMPCVideoDecFilter(nullptr,&hr);CComPtr<IBaseFilter> decoder=video;check(SUCCEEDED(hr),"decoder init");
    check(SUCCEEDED(video->SetHwCodec(HWCodec_H264,false)) && SUCCEEDED(video->SetActiveCodecs(CODEC_H264)) && SUCCEEDED(video->SetThreadNumber(2)),"software decoder settings");
    check(SUCCEEDED(video->SetFFMpegCodec(VDEC_H264,true)),"enable internal H264 decoder");
    for(int i=0;i<PixFmt_count;++i)check(SUCCEEDED(video->SetSwPixelFormat(MPCPixelFormat(i),i==PixFmt_NV12)),"decoder NV12 format");
    auto* capture=new Capture(&hr,argv[7]);CComPtr<IBaseFilter> sink=capture;check(SUCCEEDED(hr),"capture init");
    check(SUCCEEDED(graph->AddFilter(split,L"Private continuous MP4")) && SUCCEEDED(graph->AddFilter(decoder,L"Internal software H264")) && SUCCEEDED(graph->AddFilter(sink,L"Private NV12 capture")),"add graph filters");
    check(SUCCEEDED(graph->ConnectDirect(splitter->GetPin(0),video->GetPin(0),nullptr)),"connect splitter decoder");check(SUCCEEDED(graph->ConnectDirect(video->GetPin(1),capture->GetPin(0),nullptr)),"connect decoder capture");
    CComQIPtr<IMediaControl> control=graph;CComQIPtr<IMediaEvent> events=graph;CComQIPtr<IMediaFilter> clock=graph;CComQIPtr<IMediaSeeking> seeking=split;check(SUCCEEDED(clock->SetSyncSource(nullptr)),"private null clock");
    const auto worker=session->status().source.workerId;check(SUCCEEDED(control->Run()),"run graph");wait([&]{return capture->Count()>=25;});const auto before=capture->Count();long event=0;double stopSeconds=0;
    if(mode=="growing"){
        check(!source->Complete() && events->WaitForCompletion(100,&event)!=S_OK,"false early EOF");check(SUCCEEDED(control->Pause()),"pause graph");Sleep(100);const auto paused=capture->Count();session->publish(text(argv[2]));Sleep(200);check(capture->Count()==paused,"manual pause lost");check(SUCCEEDED(control->Run()),"resume graph");
        wait([&]{return capture->Count()>=400;});check(events->WaitForCompletion(100,&event)!=S_OK,"EOF before writer complete");session->publish(text(argv[3]));
    }else if(mode=="stop" || mode=="failure"){
        if(mode=="failure"){session->cancel();wait([&]{return !source->Error().empty();});check(!source->Complete(),"producer failure became source complete");}
        source->Cancel();const auto start=GetTickCount64();check(SUCCEEDED(control->Stop()),"stop waiting graph");stopSeconds=double(GetTickCount64()-start)/1000.;check(stopSeconds<2,"stop latency");
    }
    if(mode!="stop" && mode!="failure"){
        check(events->WaitForCompletion(15000,&event)==S_OK && event==EC_COMPLETE,"graph EOF");check(capture->Count()==600 && source->Complete(),"wrong decoded frame count");
        if(mode=="seek"){
            check(SUCCEEDED(control->Pause()),"pause completed graph");capture->Reset(argv[7]);REFERENCE_TIME target=100000000;
            check(SUCCEEDED(seeking->SetPositions(&target,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)),"seek same graph");check(SUCCEEDED(control->Run()),"resume seek");check(events->WaitForCompletion(15000,&event)==S_OK && event==EC_COMPLETE,"seek EOF");check(capture->Count()==350,"seek frame count");
        }
        check(source->Error().empty(),"source failure");check(SUCCEEDED(control->Stop()),"stop complete graph");
    }
    check(session->status().source.workerId==worker && source->RegisteredReaderId()==readerId,"worker/reader changed");const auto frames=capture->Count();auto released=source->ReleaseReceipt();source->Cancel();seeking.Release();clock.Release();events.Release();control.Release();graph->RemoveFilter(sink);graph->RemoveFilter(decoder);graph->RemoveFilter(split);sink.Release();decoder.Release();split.Release();async.Release();graph.Release();source.reset();
    check(p::SourceRetirement::waitEmpty(std::chrono::milliseconds(5000)) && released->load(),"graph did not release source");check(!decoded->stopped(),"graph close cancelled shared source");session->cancel();wait([&]{return session->status().done;});const auto final=session->status();session.reset();owner->collect();check(decoded->status().removed,"cache not retired");
    auto ids=final.source.readerIds;ids.insert(ids.end(),final.output.readerIds.begin(),final.output.readerIds.end());for(auto& id:ids){rapidjson::Document d;d.Parse(p::ReaderRequest(registry,"{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}").c_str());check(d["reader"]["released"].GetBool() && std::string(d["reader"]["reason"].GetString())=="files-closed","reader receipt");}
    std::cout<<"{\"result\":\"PASS\",\"frames\":"<<frames<<",\"framesBeforeWriterComplete\":"<<(mode=="complete" || mode=="seek"?0:before)<<",\"workerId\":"<<worker<<",\"stopSeconds\":"<<stopSeconds<<",\"released\":true,\"sharedSourceNotCancelled\":true}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
