// Bounded MP4 atom traversal. Jumps over declared mdat; never scans its payload.
// GPL-3.0-or-later.
#pragma once
#include "HarvesterMP4.h"
namespace harvester { namespace mp4index {
using Bytes=std::vector<uint8_t>;
struct NeedRange:std::runtime_error {
    uint64_t offset,length;std::string reason;
    NeedRange(uint64_t at,uint64_t count,const char* why):std::runtime_error("Wait for confirmed MP4 index metadata"),offset(at),length(count),reason(why){}
};
struct Located {Bytes ftyp,movie;uint64_t moovOffset=0,moovBytes=0,mdatOffset=0,payloadStart=0,payloadEnd=0;};
using Available=std::function<bool(uint64_t,size_t)>;
inline Located Locate(const FragmentIndex::Read& read,const Available& available,uint64_t total,bool tailAllowed) {
    const auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
    check(total>0 && total<=uint64_t(INT64_MAX),"MP4 index requires a bounded known length");
    Located out;uint64_t at=0,metadata=0;size_t atoms=0;
    while(at<total){++atoms;check(!tailAllowed || atoms<=16,"Too many MP4 top-level metadata atoms");
        check(total-at>=8,"Truncated final MP4 atom header");uint8_t raw[16]{};
        if(!available(at,8))throw NeedRange(at,(std::min)(uint64_t(16),total-at),"mp4-atom-header");
        check(read(at,raw,8),"MP4 atom-header read failed");BoxData header{raw,16};uint64_t size=header.U32(0);const auto type=header.U32(4);size_t width=8;
        if(size==1){check(total-at>=16,"Truncated extended MP4 atom header");if(!available(at,16))throw NeedRange(at,16,"mp4-atom-header");check(read(at+8,raw+8,8),"Extended MP4 atom-header read failed");size=header.U64(8);width=16;}
        check(size>=width && size<=total-at,"Invalid sized MP4 atom");
        if(type==FourCC("mdat")) {
            check(!out.ftyp.empty() && !out.payloadEnd,"Expected one mdat after ftyp");
            check(tailAllowed || (!out.movie.empty() && at+size==total),"Expected sized mdat after head moov without trailer");
            out.mdatOffset=at;out.payloadStart=at+width;out.payloadEnd=at+size;at+=size;
            if(!tailAllowed)break;continue;
        }
        check(type==FourCC("ftyp") || type==FourCC("moov") || type==FourCC("free"),"Unsupported MP4 top-level atom");
        check(size<=1048576 && metadata<=1048576-size,"MP4 index metadata exceeds 1 MiB");metadata+=size;
        if(!available(at,size_t(size)))throw NeedRange(at,size,"mp4-index");
        Bytes bytes(size_t(size-width));check(read(at+width,bytes.data(),bytes.size()),"MP4 metadata read failed");
        if(type==FourCC("ftyp")){check(at==0 && out.ftyp.empty() && size>=16,"Invalid MP4 ftyp placement");out.ftyp.assign(raw,raw+width);out.ftyp.insert(out.ftyp.end(),bytes.begin(),bytes.end());}
        if(type==FourCC("moov")){check(!out.ftyp.empty() && out.movie.empty(),"Repeated/misplaced MP4 moov");out.movie=std::move(bytes);out.moovOffset=at;out.moovBytes=size;}
        at+=size;
    }
    check(at==total && !out.movie.empty() && out.payloadEnd>out.payloadStart,"MP4 lacks a complete index or media envelope");return out;
}
}} // namespace harvester::mp4index
