// Complete UTF-8 SRT materialization for bounded progressive VOD. GPL-3.0-or-later.
#pragma once
#include "progressivereaderlease.h"
#include <fstream>
namespace foxicmp { namespace progressive {
inline std::string CheckedSrt(std::string text,int64_t duration) {
    auto check=[](bool ok,const char* why){Require(ok,"subtitles",why,"unsupported_media");};
    check(!text.empty() && text.size()<=524288,"SRT must contain 1..512 KiB");
    for(size_t i=0;i<text.size();){const auto first=uint8_t(text[i++]);check(first!=0,"NUL in SRT");if(first<128)continue;
        const unsigned count=first>=0xc2 && first<=0xdf?1:first>=0xe0 && first<=0xef?2:first>=0xf0 && first<=0xf4?3:0;
        check(count && count<=text.size()-i,"Invalid UTF-8 SRT");uint32_t value=first&((1u<<(6-count))-1);
        for(unsigned n=0;n<count;++n){const auto c=uint8_t(text[i++]);check((c&0xc0)==0x80,"Invalid UTF-8 continuation");value=(value<<6)|(c&63);}
        check(value>=(count==1?128u:count==2?2048u:65536u) && value<=0x10ffff && !(value>=0xd800 && value<=0xdfff),"Invalid UTF-8 scalar");
    }
    if(text.compare(0,3,"\xef\xbb\xbf")==0)text.erase(0,3);
    std::string normalized;for(size_t i=0;i<text.size();++i){if(text[i]=='\r'){check(i+1<text.size() && text[i+1]=='\n',"Bare CR in SRT");continue;}normalized+=text[i];}
    std::vector<std::string> lines;std::istringstream input(normalized);std::string line;while(std::getline(input,line)){check(line.size()<=4096,"SRT line too long");lines.push_back(line);}
    auto time=[&](const std::string& s){check(s.size()==12 && s[2]==':' && s[5]==':' && s[8]==',',"Expected HH:MM:SS,mmm");unsigned digits[9]{},at=0;for(size_t i=0;i<s.size();++i)if(i!=2 && i!=5 && i!=8){check(s[i]>='0' && s[i]<='9',"Invalid SRT time");digits[at++]=unsigned(s[i]-'0');}const auto hours=digits[0]*10+digits[1],minutes=digits[2]*10+digits[3],seconds=digits[4]*10+digits[5];check(minutes<60 && seconds<60,"Invalid SRT clock");return uint64_t(hours)*3600000+minutes*60000+seconds*1000+digits[6]*100+digits[7]*10+digits[8];};
    size_t at=0,count=0;uint64_t previous=0;
    while(at<lines.size()){
        if(lines[at].empty()){++at;continue;}check(++count<=4096,"Too many SRT cues");const auto id=lines[at++];check(!id.empty() && id.size()<=10 && id.find_first_not_of("0123456789")==std::string::npos,"SRT cue needs a numeric index");
        check(at<lines.size(),"Missing SRT cue clock");const auto clock=lines[at++];check(clock.size()==29 && clock.substr(12,5)==" --> ","Invalid SRT cue clock");const auto start=time(clock.substr(0,12)),end=time(clock.substr(17));check(start>=previous && end>start && end<=uint64_t(duration/10000)+1,"SRT cue outside VOD timeline");previous=start;
        check(at<lines.size() && !lines[at].empty(),"Empty SRT cue");while(at<lines.size() && !lines[at].empty())++at;
    }
    check(count>0,"SRT has no cues");return normalized;
}
struct SubtitleCache {
    std::filesystem::path folder;std::unique_ptr<lease_detail::Directory> directory;std::vector<std::filesystem::path> files;
    explicit SubtitleCache(const std::filesystem::path& registry):folder(registry/ std::filesystem::u8path("subtitles-"+lease_detail::RandomId())){directory=std::make_unique<lease_detail::Directory>(folder,true);}
    std::filesystem::path write(const std::string& content){
        const std::string text="\xef\xbb\xbf"+content;
        const auto path=folder/std::filesystem::u8path(lease_detail::RandomId()+".srt");
#ifdef _WIN32
        lease_detail::Security security;lease_detail::Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security.attributes,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Require(file.value!=INVALID_HANDLE_VALUE,"subtitles","Cannot create subtitle copy","resource-io-error");files.push_back(path);DWORD n=0;Require(WriteFile(file.value,text.data(),DWORD(text.size()),&n,nullptr) && n==text.size(),"subtitles","Cannot write subtitle copy","resource-io-error");
#else
        lease_detail::Handle file(openat(directory->handle.value,path.filename().c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));Require(file.value>=0,"subtitles","Cannot create subtitle copy","resource-io-error");files.push_back(path);size_t at=0;while(at<text.size()){const auto n=::write(file.value,text.data()+at,text.size()-at);Require(n>0,"subtitles","Cannot write subtitle copy","resource-io-error");at+=size_t(n);}
#endif
        return path;
    }
    ~SubtitleCache(){directory.reset();std::error_code error;for(const auto& path:files)std::filesystem::remove(path,error);std::filesystem::remove(folder,error);}
};
}} // namespace foxicmp::progressive
