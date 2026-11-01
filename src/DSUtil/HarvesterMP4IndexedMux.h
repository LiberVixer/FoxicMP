// Indexed ordinary MP4 -> immutable, single-track fMP4 views over confirmed bytes.
// GPL-3.0-or-later. Metadata projection only; payloads stay in the original file.
#pragma once
#include "HarvesterMP4Muxed.h"
namespace harvester { namespace indexedmux {
using namespace muxmp4;
constexpr size_t MaxSamples=250000;
class Layout {
public:
    using Read=FragmentIndex::Read;
    using Validate=muxmp4::Layout::Validate;
    std::array<View,2> tracks;
private:
    struct Packet {uint64_t offset=0,decode=0;uint32_t size=0,duration=0;int64_t composition=0;bool sync=false;};
    std::array<std::shared_ptr<const std::vector<Packet>>,2> packets;
    std::array<size_t,2> emitted{};
    bool audioRoll=false;
    Bytes audioDecoderInit;uint64_t audioTotal=0;
    uint64_t total=0,scanned=0,payloadStart=0,payloadEnd=0;
    size_t fragments=0;bool initialized=false;
    static void U64(Bytes& out,uint64_t value){U32(out,uint32_t(value>>32));U32(out,uint32_t(value));}
    static void Memory(View& view,Bytes bytes){const auto n=bytes.size();Check(n<=uint64_t(INT64_MAX)-view.bytes,"Indexed MP4 virtual size overflow");view.spans.push_back({view.bytes,n,0,std::make_shared<const Bytes>(std::move(bytes))});view.bytes+=n;}
    static Bytes Full(const char* type,uint32_t flags,const Bytes& body){Bytes data;U32(data,flags);Join(data,body);return Box(type,data);}
    static void Reference(BoxData minf) {
        const auto dref=minf.One("dinf").One("dref");Check(dref.U32(0)==0 && dref.U32(4)==1,"Expected one indexed MP4 data reference");
        const auto refs=dref.Slice(8,dref.size-8).Children();Check(refs.size()==1 && refs[0].first==FourCC("url ") && refs[0].second.size==4 && refs[0].second.U32(0)==1,"External indexed MP4 data references are forbidden");
    }
    std::vector<Packet> Index(BoxData track,unsigned k,uint32_t movieScale) {
        const auto media=track.One("mdia"),mdhd=media.One("mdhd"),minf=media.One("minf"),table=minf.One("stbl");
        Reference(minf);Check(!track.One("tref",true).data,"Indexed MP4 cross-track references are forbidden");
        const auto version=mdhd.U32(0)>>24;Check(version<=1,"Unsupported indexed MP4 clock version");
        const auto scale=mdhd.U32(version?20:12);const auto duration=version?mdhd.U64(24):mdhd.U32(16);Check(scale && duration && duration<=uint64_t(INT64_MAX),"Invalid indexed MP4 clock");
        for(const auto& child:table.Children())Check(child.first==FourCC("stsd") || child.first==FourCC("stts") || child.first==FourCC("ctts") || child.first==FourCC("stsc") || child.first==FourCC("stsz") || child.first==FourCC("stco") || child.first==FourCC("co64") || child.first==FourCC("stss") || child.first==FourCC("sdtp") || child.first==FourCC("sgpd") || child.first==FourCC("sbgp"),"Unsupported/encrypted indexed MP4 sample table");

        const auto stsd=table.One("stsd");Check(stsd.U32(0)==0 && stsd.U32(4)==1,"Expected one indexed sample description");
        const auto entries=stsd.Slice(8,stsd.size-8).Children();Check(entries.size()==1 && entries[0].first==FourCC(k?"mp4a":"avc1"),"Expected AVC/AAC indexed MP4");
        Check(entries[0].second.U32(4)==1,"Invalid indexed sample data reference");
        const auto sizes=table.One("stsz");const uint32_t fixed=sizes.U32(4),count=sizes.U32(8);
        Check(sizes.U32(0)==0 && count && count<=MaxSamples && sizes.size==12+(fixed?0:size_t(count)*4),"Invalid bounded MP4 sample-size table");
        const auto sgpd=table.One("sgpd",true),sbgp=table.One("sbgp",true);
        if(sgpd.data || sbgp.data){
            Check(k==1 && sgpd.data && sbgp.data && sgpd.size==18 && sgpd.U32(0)==0x01000000 && sgpd.U32(4)==FourCC("roll") && sgpd.U32(8)==2 && sgpd.U32(12)==1 && sgpd.data[16]==0xff && sgpd.data[17]==0xff,"Unsupported indexed audio sample-group description");
            Check(sbgp.size==20 && sbgp.U32(0)==0 && sbgp.U32(4)==FourCC("roll") && sbgp.U32(8)==1 && sbgp.U32(12)==count && sbgp.U32(16)==1,"Unsupported indexed audio roll mapping");audioRoll=true;
        }
        std::vector<Packet> out(count);
        for(uint32_t i=0;i<count;++i){out[i].size=fixed?fixed:sizes.U32(12+size_t(i)*4);out[i].sync=k!=0;Check(out[i].size && out[i].size<=16777216,"Invalid indexed MP4 sample size");}
        const auto times=table.One("stts");const auto runs=times.U32(4);Check(times.U32(0)==0 && runs && runs<=count && times.size==8+size_t(runs)*8,"Invalid indexed MP4 time table");
        size_t at=0;uint64_t ticks=0;
        for(uint32_t i=0;i<runs;++i){const auto n=times.U32(8+size_t(i)*8),delta=times.U32(12+size_t(i)*8);Check(n && n<=count-at && delta,"Invalid indexed MP4 time run");
            for(uint32_t j=0;j<n;++j){Check(ticks<=uint64_t(INT64_MAX)-delta,"Indexed MP4 decode clock overflow");out[at].decode=ticks;out[at++].duration=delta;ticks+=delta;}}
        Check(at==count,"Indexed MP4 samples do not cover the time table");
        const auto ctts=table.One("ctts",true);
        if(ctts.data){const auto v=ctts.U32(0)>>24,nruns=ctts.U32(4);Check(v<=1 && (ctts.U32(0)&0xffffff)==0 && nruns && nruns<=count && ctts.size==8+size_t(nruns)*8,"Invalid indexed MP4 composition table");at=0;
            for(uint32_t i=0;i<nruns;++i){const auto n=ctts.U32(8+size_t(i)*8),value=ctts.U32(12+size_t(i)*8);Check(n && n<=count-at,"Invalid indexed MP4 composition run");
                const auto offset=v?int64_t(int32_t(value)):int64_t(value);Check(offset>=INT32_MIN && offset<=INT32_MAX,"Indexed composition offset exceeds signed fragment field");for(uint32_t j=0;j<n;++j)out[at++].composition=offset;}
            Check(at==count,"Incomplete indexed MP4 composition table");}
        // Edit duration is a presentation boundary, not a decode-clock sum.
        // VFR/B-frame MP4 can legitimately have sum(stts) != mdhd.duration.
        uint64_t mediaEnd=duration;
        const auto edits=track.One("edts",true);
        if(edits.data){const auto list=edits.One("elst");const auto v=list.U32(0)>>24,n=list.U32(4);const size_t width=v?20:12;
            Check(v<=1 && (list.U32(0)&0xffffff)==0 && n>0 && n<=2 && list.size==8+n*width && movieScale,"Unsupported indexed edit list");bool found=false;
            for(uint32_t i=0;i<n;++i){const size_t offset=8+i*width;const auto segment=v?list.U64(offset):list.U32(offset);const int64_t time=v?int64_t(list.U64(offset+8)):int32_t(list.U32(offset+4));
                Check(segment>0 && list.U32(offset+(v?16:8))==0x10000,"Unsupported indexed edit rate/duration");if(time==-1 && !found)continue;
                Check(time>=0 && !found,"Unsupported indexed media-edit sequence");const auto whole=segment/movieScale,remainder=segment%movieScale;
                Check(whole<=uint64_t(INT64_MAX)/scale,"Indexed edit range overflow");const auto length=whole*scale+(remainder*uint64_t(scale))/movieScale;
                Check(length && length<=uint64_t(INT64_MAX) && uint64_t(time)<=uint64_t(INT64_MAX)-length,"Indexed edit end overflow");mediaEnd=uint64_t(time)+length;found=true;}
            Check(found,"Indexed MP4 contains only empty edits");}
        if(k==0)for(auto& packet:out){const auto pts=AddTime(int64_t(packet.decode),packet.composition);Check(pts<int64_t(mediaEnd),"Non-presenting terminal samples are outside the indexed profile");
            if(pts>=0 && uint64_t(packet.duration)>mediaEnd-uint64_t(pts))packet.duration=uint32_t(mediaEnd-uint64_t(pts));}
        const auto sync=table.One("stss",true);
        if(sync.data){const auto n=sync.U32(4);Check(sync.U32(0)==0 && n && n<=count && sync.size==8+size_t(n)*4,"Invalid indexed MP4 sync table");uint32_t previous=0;
            for(uint32_t i=0;i<n;++i){const auto number=sync.U32(8+size_t(i)*4);Check(number>previous && number<=count,"Invalid indexed MP4 sync ordering");out[number-1].sync=true;previous=number;}}
        else for(auto& packet:out)packet.sync=true;
        Check(out.front().sync,"Indexed MP4 starts without an independent sample");
        const auto dependency=table.One("sdtp",true);if(dependency.data)Check(dependency.U32(0)==0 && dependency.size==4+count,"Invalid indexed dependency table");
        const auto map=table.One("stsc"),narrow=table.One("stco",true),wide=table.One("co64",true);Check(bool(narrow.data)!=bool(wide.data),"Expected one chunk-offset table");
        const auto offsets=narrow.data?narrow:wide;const auto chunks=offsets.U32(4),nruns=map.U32(4);
        Check(offsets.U32(0)==0 && map.U32(0)==0 && chunks && chunks<=count && nruns && nruns<=chunks && offsets.size==8+size_t(chunks)*(wide.data?8:4) && map.size==8+size_t(nruns)*12,"Invalid indexed chunk table");
        uint32_t previous=0;for(uint32_t i=0;i<nruns;++i){const auto first=map.U32(8+size_t(i)*12);Check(first>previous && first<=chunks && (i || first==1) && map.U32(12+size_t(i)*12)>0 && map.U32(16+size_t(i)*12)==1,"Invalid indexed chunk mapping");previous=first;}
        at=0;uint32_t run=0;uint64_t previousEnd=payloadStart;
        for(uint32_t chunk=1;chunk<=chunks;++chunk){if(run+1<nruns && chunk==map.U32(8+size_t(run+1)*12))++run;
            auto offset=wide.data?offsets.U64(8+size_t(chunk-1)*8):offsets.U32(8+size_t(chunk-1)*4);const auto n=map.U32(12+size_t(run)*12);
            Check(n<=count-at && offset>=previousEnd,"Overlapping/unordered indexed chunks");for(uint32_t j=0;j<n;++j){auto& packet=out[at++];Check(offset>=payloadStart && RangeReady(offset,packet.size,payloadEnd),"Indexed sample exceeds its mdat");packet.offset=offset;offset+=packet.size;}previousEnd=offset;}
        Check(at==count,"Incomplete indexed chunk mapping");return out;
    }
    static Bytes ProjectTrack(BoxData track) {
        const auto stbl=track.One("mdia").One("minf").One("stbl");
        Bytes table=Box(FourCC("stsd"),stbl.One("stsd"));const auto groups=stbl.One("sgpd",true);if(groups.data)Join(table,Box(FourCC("sgpd"),groups));Bytes empty;U32(empty,0);
        for(const auto* name:{"stts","stsc","stco"})Join(table,Full(name,0,empty));Bytes sizes;U32(sizes,0);U32(sizes,0);Join(table,Full("stsz",0,sizes));
        return WithTable(track,table);
    }
    static Bytes WithTable(BoxData track,const Bytes& table) {
        const auto media=track.One("mdia"),minf=media.One("minf");
        Bytes minfBody;for(const auto& child:minf.Children())Join(minfBody,child.first==FourCC("stbl")?Box("stbl",table):Box(child.first,child.second));
        Bytes mediaBody;for(const auto& child:media.Children())Join(mediaBody,child.first==FourCC("minf")?Box("minf",minfBody):Box(child.first,child.second));
        Bytes trackBody;for(const auto& child:track.Children())Join(trackBody,child.first==FourCC("mdia")?Box("mdia",mediaBody):Box(child.first,child.second));
        return Box("trak",trackBody);
    }
    Bytes Moof(unsigned k,size_t first,size_t last,uint32_t sequence) const {
        const auto& all=*packets[k];Bytes tfhd;U32(tfhd,tracks[k].id);Bytes tfdt;U64(tfdt,all[first].decode);
        Bytes entries;U32(entries,uint32_t(last-first));U32(entries,0);
        for(size_t i=first;i<last;++i){const auto& p=all[i];U32(entries,p.duration);U32(entries,p.size);U32(entries,p.sync?0x02000000:0x01010000);U32(entries,uint32_t(p.composition));}
        Bytes groups;if(k && audioRoll){Bytes mapping;U32(mapping,FourCC("roll"));U32(mapping,1);U32(mapping,uint32_t(last-first));U32(mapping,1);Join(groups,Full("sbgp",0,mapping));}
        const auto build=[&]{Bytes traf=Full("tfhd",0x020000,tfhd);Join(traf,Full("tfdt",0x01000000,tfdt));Join(traf,Full("trun",0x01000f01,entries));Join(traf,groups);Bytes seq;U32(seq,sequence);Bytes body=Full("mfhd",0,seq);Join(body,Box("traf",traf));return Box("moof",body);};
        auto result=build();Check(result.size()+8<=INT32_MAX,"Indexed fragment offset overflow");Put32(entries,4,uint32_t(result.size()+8));return build();
    }
    void AudioDecoderInit(BoxData track,BoxData mvhd,const Bytes& ftyp) {
        const auto table=track.One("mdia").One("minf").One("stbl");const auto& all=*packets[1];
        const auto movie=[&](const std::vector<uint64_t>& offsets){Bytes mapped;
            for(const auto& child:table.Children()) {
                if(child.first==FourCC("stco") || child.first==FourCC("co64"))continue;
                if(child.first==FourCC("stsc")){Bytes body;U32(body,1);U32(body,1);U32(body,1);U32(body,1);Join(mapped,Full("stsc",0,body));}
                else Join(mapped,Box(child.first,child.second));
            }
            Bytes chunks;U32(chunks,uint32_t(offsets.size()));for(auto offset:offsets)U64(chunks,offset);Join(mapped,Full("co64",0,chunks));
            Bytes body=Box(FourCC("mvhd"),mvhd);Join(body,WithTable(track,mapped));Bytes result=ftyp;Join(result,Box("moov",body));return result;
        };
        std::vector<uint64_t> offsets(all.size());auto decoder=movie(offsets);auto& view=tracks[1];
        Check(view.spans.size()==1 && view.spans[0].memory,"Invalid indexed audio initialization");
        uint64_t header=(std::max)(uint64_t(decoder.size()),view.bytes)+8;
        Check(header<=4194304,"Projected audio initialization exceeds 4 MiB");const auto fragmentSize=Moof(1,0,1,1).size();uint64_t cursor=header;
        for(size_t i=0;i<all.size();++i){Check(cursor<=uint64_t(INT64_MAX)-fragmentSize-8-all[i].size,"Indexed audio projection exceeds byte limits");offsets[i]=cursor+fragmentSize+8;cursor=offsets[i]+all[i].size;}
        audioTotal=cursor;decoder=movie(offsets);Check(decoder.size()+8<=header,"Projected audio initialization changed size");
        Bytes padding(size_t(header-decoder.size()-8));Join(decoder,Box("free",padding));audioDecoderInit=std::move(decoder);
        Bytes index=*view.spans[0].memory;Check(index.size()+8<=header,"Indexed audio header padding overflow");padding.assign(size_t(header-index.size()-8),0);Join(index,Box("free",padding));
        view.spans.clear();view.bytes=0;Memory(view,std::move(index));
    }
    bool Initialize(const Read& read,uint64_t prefix,const Validate& validate) {
        uint64_t pos=0;Bytes ftyp,movie;
        while(pos<prefix){uint8_t raw[16]{};if(!RangeReady(pos,8,prefix))return false;Check(read(pos,raw,8),"Indexed MP4 header read failed");BoxData header{raw,16};uint64_t size=header.U32(0);const auto type=header.U32(4);size_t width=8;
            if(size==1){if(!RangeReady(pos,16,prefix))return false;Check(read(pos+8,raw+8,8),"Indexed extended MP4 header read failed");size=header.U64(8);width=16;}
            Check(size>=width && pos<=total && size<=total-pos,"Invalid bounded indexed MP4 atom");
            if(type==FourCC("mdat")){Check(!ftyp.empty() && !movie.empty() && pos+size==total,"Expected one sized mdat after head moov without trailer");payloadStart=pos+width;payloadEnd=pos+size;break;}
            Check(type==FourCC("ftyp") || type==FourCC("moov") || type==FourCC("free"),"Unsupported indexed MP4 top-level atom");Check(size<=MaxInit && pos+size<=MaxInit,"Indexed MP4 initialization exceeds 1 MiB");if(!RangeReady(pos,size,prefix))return false;
            Bytes bytes(size_t(size-width));Check(read(pos+width,bytes.data(),bytes.size()),"Indexed metadata read failed");
            if(type==FourCC("ftyp")){Check(pos==0 && ftyp.empty() && size>=16,"Invalid indexed MP4 ftyp");ftyp=Box(type,{bytes.data(),bytes.size()});}
            if(type==FourCC("moov")){Check(!ftyp.empty() && movie.empty(),"Repeated/misplaced indexed moov");movie=std::move(bytes);}pos+=size;
        }
        if(!payloadEnd)return false;
        const BoxData moov{movie.data(),movie.size()};Check(!moov.One("mvex",true).data && !moov.One("pssh",true).data,"Fragmented/encrypted MP4 declared as ordinary indexed MP4");
        const auto mvhd=moov.One("mvhd");Check((mvhd.U32(0)>>24)<=1,"Invalid indexed movie clock");const auto movieScale=mvhd.U32(mvhd.data[0]?20:12);Check(movieScale,"Zero indexed movie scale");std::array<BoxData,2> selected{};size_t count=0;
        for(const auto& child:moov.Children())if(child.first==FourCC("trak")){Check(++count<=2,"Indexed MP4 needs exactly two tracks");const auto type=child.second.One("mdia").One("hdlr").U32(8);Check(type==FourCC("vide") || type==FourCC("soun"),"Unsupported indexed MP4 track kind");const unsigned k=type==FourCC("vide")?0:1;Check(!selected[k].data,"Repeated indexed MP4 track kind");selected[k]=child.second;}
        Check(count==2 && selected[0].data && selected[1].data,"Indexed MP4 requires video and audio");
        std::vector<std::pair<uint64_t,uint64_t>> ranges;
        for(unsigned k=0;k<2;++k){const auto tkhd=selected[k].One("tkhd");Check((tkhd.U32(0)>>24)<=1,"Invalid indexed tkhd version");tracks[k].id=tkhd.U32(tkhd.data[0]?20:12);Check(tracks[k].id,"Zero indexed MP4 track ID");
            packets[k]=std::make_shared<const std::vector<Packet>>(Index(selected[k],k,movieScale));for(const auto& p:*packets[k])ranges.emplace_back(p.offset,p.offset+p.size);
            Bytes defaults;U32(defaults,tracks[k].id);U32(defaults,1);U32(defaults,packets[k]->front().duration);U32(defaults,0);U32(defaults,0);
            Bytes body=Box(FourCC("mvhd"),mvhd);Join(body,ProjectTrack(selected[k]));Join(body,Box("mvex",Full("trex",0,defaults)));validate(k,{body.data(),body.size()});
            Bytes init=ftyp;Join(init,Box("moov",body));Memory(tracks[k],std::move(init));}
        AudioDecoderInit(selected[1],mvhd,ftyp);
        Check(tracks[0].id!=tracks[1].id,"Duplicate indexed MP4 track ID");std::sort(ranges.begin(),ranges.end());uint64_t end=payloadStart;
        for(const auto& range:ranges){Check(range.first>=end,"Indexed video/audio sample payloads overlap");end=range.second;}
        initialized=true;return true;
    }
    void Append(unsigned k,const Read& read,uint64_t prefix) {
        auto& view=tracks[k];const auto& all=*packets[k];auto& first=emitted[k];
        while(first<all.size() && RangeReady(all[first].offset,all[first].size,prefix)) {
            // Bound each projected fragment's packet/metadata count. Its timing
            // is copied from sample tables, never independently rebased.
            size_t last=first;uint64_t bytes=0;
            while(last<all.size() && last-first<(k?1:256) && (last==first || all[last-1].decode+all[last-1].duration==all[last].decode) && RangeReady(all[last].offset,all[last].size,prefix) && bytes+all[last].size<=16777216){bytes+=all[last].size;++last;}
            Check(last>first,"Indexed fragment exceeds bounds");Memory(view,Moof(k,first,last,uint32_t(++fragments)));
            Bytes header;U32(header,uint32_t(bytes+8));U32(header,FourCC("mdat"));Memory(view,std::move(header));
            for(size_t i=first;i<last;++i){const auto& p=all[i];Check(p.size<=uint64_t(INT64_MAX)-view.bytes,"Indexed virtual offset overflow");view.spans.push_back({view.bytes,p.size,p.offset,{}});view.bytes+=p.size;}
            view.index.Update([&](uint64_t at,void* out,size_t n){return ReadAt(read,k,at,out,n);},view.bytes,int(k));first=last;
        }
    }
public:
    void SetLength(uint64_t value){Check(value && value<=uint64_t(INT64_MAX) && (!total || value==total),"Indexed MP4 requires an immutable known source length");total=value;}
    bool Initialized() const{return initialized;}
    size_t Fragments() const{return fragments;}
    uint64_t Position() const{return scanned;}
    bool ReadAt(const Read& read,unsigned k,uint64_t at,void* out,size_t count)const{
        Check(k<2 && (out || !count),"Invalid indexed MP4 virtual read");const auto& view=tracks[k];if(!RangeReady(at,count,view.bytes))return false;
        auto* bytes=static_cast<uint8_t*>(out);size_t copied=0;auto it=std::upper_bound(view.spans.begin(),view.spans.end(),at,[](uint64_t p,const Span& s){return p<s.offset;});if(it!=view.spans.begin())--it;
        for(;copied<count && it!=view.spans.end();++it){if(at>=it->offset+it->length)continue;Check(at>=it->offset,"Indexed virtual layout gap");const auto n=size_t((std::min)(uint64_t(count-copied),it->length-(at-it->offset)));
            if(it->memory)std::memcpy(bytes+copied,it->memory->data()+size_t(at-it->offset),n);else if(!read(it->physical+at-it->offset,bytes+copied,n))return false;copied+=n;at+=n;}return copied==count;
    }
    uint64_t Total(unsigned k,bool complete)const{return k?audioTotal:complete?tracks[k].bytes:0;}
    bool ReadDecoderAt(const Read& read,unsigned k,uint64_t at,void* out,size_t count)const {
        if(!ReadAt(read,k,at,out,count))return false;if(!k || !count)return true;
        auto* bytes=static_cast<uint8_t*>(out);const uint64_t end=at+count;
        if(at<audioDecoderInit.size()){const auto n=size_t((std::min)(uint64_t(count),uint64_t(audioDecoderInit.size())-at));std::memcpy(bytes,audioDecoderInit.data()+size_t(at),n);}
        const auto& spans=tracks[1].spans;auto i=std::upper_bound(spans.begin(),spans.end(),at,[](uint64_t p,const Span& s){return p<s.offset;});if(i!=spans.begin())--i;
        for(;i!=spans.end() && i->offset<end;++i)if(i->memory && i->memory->size()>=8 && std::memcmp(i->memory->data()+4,"moof",4)==0){
            const auto left=(std::max)(at,i->offset+4),right=(std::min)(end,i->offset+8);
            if(left<right)std::memcpy(bytes+size_t(left-at),"free"+size_t(left-i->offset-4),size_t(right-left));
        }return true;
    }
    void Update(const Read& read,uint64_t prefix,bool complete,const Validate& validate){Check(total && prefix>=scanned && prefix<=total && prefix-scanned<=16777216,"Indexed MP4 prefix/append exceeds bounds");scanned=prefix;
        if(!initialized && !Initialize(read,prefix,validate)){Check(!complete,"Completed indexed MP4 lacks initialization");return;}
        for(unsigned k=0;k<2;++k)Append(k,read,prefix);
        if(complete)Check(prefix==total && emitted[0]==packets[0]->size() && emitted[1]==packets[1]->size(),"Completed indexed MP4 has unavailable samples");
    }
};
}} // namespace harvester::indexedmux
