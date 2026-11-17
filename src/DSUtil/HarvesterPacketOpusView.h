// Decoder-only microsecond audio clock. Original cache bytes/packet hashes stay
// unchanged; metadata uses cumulative Opus samples, avoiding 1ms seek rounding.
#pragma once
#include "HarvesterWebMMuxed.h"
namespace harvester {
class PacketOpusView {
    using Bytes=muxwebm::Bytes;
    static Bytes element(uint32_t id,const Bytes& bytes){return muxwebm::Element(id,{bytes.data(),bytes.size()});}
    static Bytes number(uint32_t id,uint64_t value){Bytes b;do{b.insert(b.begin(),uint8_t(value));value>>=8;}while(value);return element(id,b);}
    void memory(Bytes bytes){const auto n=bytes.size();webm::Check(view.bytes+n<=67108864,"opus-view-budget");view.spans.push_back({view.bytes,n,0,std::make_shared<const Bytes>(std::move(bytes))});view.bytes+=n;}
public:
    muxwebm::View view;
    void Initialize(const Bytes& source,uint32_t id){
        if(view.bytes)return;
        webm::Element eb,segment;webm::Check(webm::Header(source.data(),source.size(),eb) && webm::Header(source.data()+eb.end,source.size()-eb.end,segment),"opus-view-header");
        Bytes head(source.begin(),source.begin()+size_t(eb.end));muxwebm::Join(head,Bytes{0x18,0x53,0x80,0x67,1,255,255,255,255,255,255,255});
        const webm::Data body{source.data()+eb.end+segment.data,source.size()-size_t(eb.end+segment.data)};
        for(const auto& child:body.Children()){
            Bytes value;
            if(child.id==0x1549a966){
                for(const auto& f:child.body.Children())if(f.id!=0x2ad7b1 && f.id!=0x4489 && f.id!=0xbf)muxwebm::Join(value,muxwebm::Element(f.id,f.body));
                muxwebm::Join(value,number(0x2ad7b1,1000));
            }else if(child.id==0x1654ae6b){
                const auto entry=child.body.One(0xae);Bytes fields;
                for(const auto& f:entry.Children())if(f.id!=0x23e383 && f.id!=0xbf)muxwebm::Join(fields,muxwebm::Element(f.id,f.body));
                value=element(0xae,fields);
            }else continue;
            muxwebm::Join(head,element(child.id,value));
        }
        view.id=id;memory(std::move(head));
    }
    void Add(uint64_t startSample,uint64_t physical,size_t size,int64_t padding){
        webm::Check(view.bytes && size && size<=16777216 && padding>=0,"opus-view-packet");
        auto clock=number(0xe7,(startSample*1000000+24000)/48000);
        Bytes track;uint64_t id=view.id;unsigned width=1;while(width<5 && id>=(uint64_t(1)<<(7*width))-1)++width;
        for(unsigned n=width;n;--n)track.push_back(uint8_t(id>>(8*(n-1)) | (n==width?(0x80u>>(width-1)):0)));
        track.insert(track.end(),{0,0,0});
        auto block=muxwebm::Header(0xa1,track.size()+size);muxwebm::Join(block,track);
        // DiscardPadding is a signed EBML integer. A minimal unsigned encoding
        // with its top bit set would turn positive end trim into negative trim.
        Bytes tail;if(padding){Bytes signedPadding(8);for(unsigned i=0;i<8;++i)signedPadding[7-i]=uint8_t(uint64_t(padding)>>(8*i));tail=element(0x75a2,signedPadding);}
        auto group=muxwebm::Header(0xa0,block.size()+size+tail.size());muxwebm::Join(group,block);
        auto header=muxwebm::Header(0x1f43b675,clock.size()+group.size()+size+tail.size());muxwebm::Join(header,clock);muxwebm::Join(header,group);
        webm::Check(view.bytes+header.size()+size+tail.size()<=67108864,"opus-view-budget");
        memory(std::move(header));view.spans.push_back({view.bytes,size,physical,{}});view.bytes+=size;
        if(!tail.empty())memory(std::move(tail));
    }
};
}
