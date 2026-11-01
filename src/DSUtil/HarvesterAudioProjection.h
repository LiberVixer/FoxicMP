// One-track virtual fMP4 framing shared by elementary audio adapters. GPL-3.0-or-later.
#pragma once
#include "HarvesterMP4Muxed.h"
namespace harvester { namespace audiomp4 {
using Bytes=muxmp4::Bytes;
inline Bytes Z(size_t n){return Bytes(n,0);}inline void P(Bytes& b,size_t at,uint32_t v){muxmp4::Put32(b,at,v);}inline Bytes B(const char* n,const Bytes& b){return muxmp4::Box(n,b);}inline void J(Bytes& a,const Bytes& b){muxmp4::Join(a,b);}
inline Bytes Init(unsigned rate,const Bytes& sampleEntry){Bytes ftyp;for(auto v:{FourCC("isom"),0u,FourCC("iso6"),FourCC("mp41")})muxmp4::U32(ftyp,v);Bytes mvhd=Z(100);P(mvhd,12,1000);P(mvhd,20,0x10000);P(mvhd,24,0x01000000);P(mvhd,36,0x10000);P(mvhd,52,0x10000);P(mvhd,68,0x40000000);P(mvhd,96,2);
    Bytes tkhd=Z(84);P(tkhd,0,7);P(tkhd,12,1);P(tkhd,36,0x01000000);P(tkhd,40,0x10000);P(tkhd,56,0x10000);P(tkhd,72,0x40000000);Bytes mdhd=Z(24);P(mdhd,12,rate);P(mdhd,20,0x55c40000);Bytes hdlr=Z(25);P(hdlr,8,FourCC("soun"));
    Bytes stsd=Z(8);P(stsd,4,1);J(stsd,sampleEntry);Bytes stbl;J(stbl,B("stsd",stsd));for(const auto* n:{"stts","stsc","stco"})J(stbl,B(n,Z(8)));J(stbl,B("stsz",Z(12)));Bytes dref=Z(8);P(dref,4,1);Bytes url=Z(4);P(url,0,1);J(dref,B("url ",url));Bytes minf;J(minf,B("smhd",Z(8)));J(minf,B("dinf",B("dref",dref)));J(minf,B("stbl",stbl));Bytes mdia;J(mdia,B("mdhd",mdhd));J(mdia,B("hdlr",hdlr));J(mdia,B("minf",minf));Bytes trak;J(trak,B("tkhd",tkhd));J(trak,B("mdia",mdia));Bytes trex=Z(24);P(trex,4,1);P(trex,8,1);Bytes moov;J(moov,B("mvhd",mvhd));J(moov,B("trak",trak));J(moov,B("mvex",B("trex",trex)));Bytes out=B("ftyp",ftyp);J(out,B("moov",moov));return out;}
class Projection {
protected:
    void memory(Bytes b){const auto n=b.size();view.spans.push_back({view.bytes,n,0,std::make_shared<const Bytes>(std::move(b))});view.bytes+=n;}
    void append(uint64_t sequence,uint64_t samples,unsigned count,uint64_t at,size_t n){Bytes mfhd=Z(8);P(mfhd,4,uint32_t(sequence));Bytes tfhd=Z(8);P(tfhd,0,0x20000);P(tfhd,4,1);Bytes tfdt=Z(12);P(tfdt,0,0x01000000);P(tfdt,4,uint32_t(samples>>32));P(tfdt,8,uint32_t(samples));Bytes run=Z(20);P(run,0,0x301);P(run,4,1);P(run,12,count);P(run,16,uint32_t(n));auto moof=[&]{Bytes traf;J(traf,B("tfhd",tfhd));J(traf,B("tfdt",tfdt));J(traf,B("trun",run));Bytes m;J(m,B("mfhd",mfhd));J(m,B("traf",traf));return B("moof",m);};P(run,8,uint32_t(moof().size()+8));memory(moof());Bytes mdat; muxmp4::U32(mdat,uint32_t(n+8));muxmp4::U32(mdat,FourCC("mdat"));memory(std::move(mdat));view.spans.push_back({view.bytes,n,at,{}});view.bytes+=n;}
public:
    muxmp4::View view;
    bool ReadAt(const FragmentIndex::Read& raw,uint64_t at,void* out,size_t count)const{if(!RangeReady(at,count,view.bytes))return false;auto* p=static_cast<uint8_t*>(out);auto it=std::upper_bound(view.spans.begin(),view.spans.end(),at,[](uint64_t x,const muxmp4::Span& s){return x<s.offset;});if(it!=view.spans.begin())--it;while(count){if(it==view.spans.end() || at<it->offset || at>=it->offset+it->length)return false;const auto delta=at-it->offset,n=size_t((std::min)(uint64_t(count),it->length-delta));if(it->memory)std::memcpy(p,it->memory->data()+delta,n);else if(!raw(it->physical+delta,p,n))return false;at+=n;p+=n;count-=n;++it;}return true;}
};
}}
