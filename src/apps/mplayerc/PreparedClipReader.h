#pragma once
#include "../../../shared/preparedclip.h"
#include "filters/parser/BaseSplitter/AsyncReader.h"
// The normal MP4 splitter consumes this synchronous, seekable memory reader.
// No Harvester stream/packet interface and no filesystem/network fallback.
class CPreparedClipReader final:public CUnknown,public IAsyncReader,public ISyncReader {
    std::shared_ptr<foxicmp::progressive::PreparedClip> source;
    std::atomic<bool> flushing{false},error{false};std::atomic<HANDLE> breaker{nullptr};
public:
    CPreparedClipReader(std::shared_ptr<foxicmp::progressive::PreparedClip> clip,HRESULT& hr):CUnknown(L"FoxicMP prepared MP4",nullptr,&hr),source(std::move(clip)){hr=source?S_OK:E_INVALIDARG;if(source)++source->readers;}
    ~CPreparedClipReader(){if(source)--source->readers;}
    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid,void** ppv){CheckPointer(ppv,E_POINTER);return QI(IAsyncReader) QI(ISyncReader) __super::NonDelegatingQueryInterface(riid,ppv);}
    STDMETHODIMP RequestAllocator(IMemAllocator*,ALLOCATOR_PROPERTIES*,IMemAllocator**){return E_NOTIMPL;}
    STDMETHODIMP Request(IMediaSample*,DWORD_PTR){return E_NOTIMPL;}
    STDMETHODIMP WaitForNext(DWORD,IMediaSample**,DWORD_PTR*){return E_NOTIMPL;}
    STDMETHODIMP SyncReadAligned(IMediaSample*){return E_NOTIMPL;}
    STDMETHODIMP SyncRead(LONGLONG offset,LONG count,BYTE* buffer){
        if(offset<0 || count<0 || (!buffer && count))return E_INVALIDARG;
        auto interrupt=breaker.load();if(flushing.load() || source->cancelled.load() || (interrupt && WaitForSingleObject(interrupt,0)==WAIT_OBJECT_0))return E_ABORT;
        if(uint64_t(offset)>source->bytes->size() || uint64_t(count)>source->bytes->size()-uint64_t(offset))return E_FAIL;
        uint64_t done=0;while(done<uint64_t(count)){auto n=source->read(uint64_t(offset)+done,buffer+done,uint64_t(count)-done);if(n<=0){error=true;return E_ABORT;}done+=uint64_t(n);}return S_OK;
    }
    STDMETHODIMP Length(LONGLONG* total,LONGLONG* available){if(total)*total=source->bytes->size();if(available)*available=source->bytes->size();return S_OK;}
    STDMETHODIMP BeginFlush(){flushing=true;return S_OK;}
    STDMETHODIMP EndFlush(){flushing=false;return S_OK;}
    STDMETHODIMP_(void) SetBreakEvent(HANDLE value){breaker=value;}
    STDMETHODIMP_(bool) HasErrors(){return error;}
    STDMETHODIMP_(void) ClearErrors(){error=false;}
    STDMETHODIMP_(void) SetPTSOffset(REFERENCE_TIME*){}
    STDMETHODIMP_(int) GetSourceType(){return CAsyncFileReader::LOCAL;}
    STDMETHODIMP ReOpen(CHdmvClipInfo::CPlaylist&){return E_NOTIMPL;}
};
