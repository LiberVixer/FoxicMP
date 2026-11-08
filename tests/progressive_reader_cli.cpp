// Native process/file lifecycle fixture, with no desktop or media backend.
#include "../shared/progressivereaderlease.h"
#include <iostream>
#include <vector>
#ifdef _WIN32
#include "../shared/progressivetemppath_win.h"
#endif
namespace p=foxicmp::progressive;
namespace n=p::lease_detail;
static int run(int argc,const char* const* argv) {
    try {
#ifdef _WIN32
        if(argc==2 && std::string(argv[1])=="--temp-info-safe") {
            const auto path=std::filesystem::path(p::WindowsTemporaryDirectory()).u8string();
            rapidjson::Document result;result.SetObject();auto& allocator=result.GetAllocator();
            rapidjson::Value directory(rapidjson::kObjectType);directory.AddMember("path",rapidjson::Value(path.c_str(),allocator),allocator);
            result.AddMember("progressiveTempDirectory",directory,allocator);
            rapidjson::StringBuffer output;rapidjson::Writer<rapidjson::StringBuffer> writer(output);result.Accept(writer);
            std::cout<<output.GetString()<<std::endl;return 0;
        }
        if(argc==2 && std::string(argv[1])=="--temp-info") {
            rapidjson::Document result;result.SetObject();auto& allocator=result.GetAllocator();
            using TempFunction=DWORD(WINAPI*)(DWORD,LPWSTR);
            auto add=[&](const char* name,TempFunction function) {
                if(!function){result.AddMember(rapidjson::Value(name,allocator),rapidjson::Value(),allocator);return;}
                std::wstring buffer(32768,L'\0');
                std::cerr<<"temp-info:before-"<<name<<std::endl;
                const auto count=function(DWORD(buffer.size()),buffer.data());
                std::cerr<<"temp-info:after-"<<name<<":"<<count<<std::endl;
                n::Check(count>0 && count<buffer.size(),"Cannot obtain bounded temporary path");buffer.resize(count);
                const auto utf8=std::filesystem::path(buffer).u8string();
                rapidjson::Value value(rapidjson::kObjectType);value.AddMember("path",rapidjson::Value(utf8.c_str(),allocator),allocator);
                value.AddMember("length",uint32_t(count),allocator);result.AddMember(rapidjson::Value(name,allocator),value,allocator);
            };
            add("GetTempPathW",GetTempPathW);
            add("GetTempPath2W",reinterpret_cast<TempFunction>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetTempPath2W")));
            rapidjson::StringBuffer output;rapidjson::Writer<rapidjson::StringBuffer> writer(output);result.Accept(writer);
            std::cout<<output.GetString()<<std::endl;return 0;
        }
#endif
        if(argc==3 && std::string(argv[1])=="--request") {
            std::string line;while(std::getline(std::cin,line))std::cout<<p::ReaderRequest(std::filesystem::u8path(argv[2]),line)<<std::endl;return 0;
        }
        n::Check(argc==5 && std::string(argv[1])=="--hold","Expected --hold ROOT SESSION MEDIA");
        const auto path=std::filesystem::u8path(argv[4]);uint64_t device=0,fileId=0;
#ifdef _WIN32
        n::Handle media(CreateFileW(n::NativePath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        BY_HANDLE_FILE_INFORMATION info{};n::Check(media.value!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(media.value,&info),"Cannot open fixture media");
        device=info.dwVolumeSerialNumber;fileId=(uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
#else
        n::Handle media(::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));struct stat info{};
        n::Check(media.value>=0 && !fstat(media.value,&info),"Cannot open fixture media");device=uint64_t(info.st_dev);fileId=uint64_t(info.st_ino);
#endif
        auto lease=p::ReaderLease::Create(std::filesystem::u8path(argv[2]),argv[3],1,{{"media",device,fileId}});
        std::cout<<"{\"readerId\":\""<<lease->id()<<"\"}"<<std::endl;
        std::string command;
        while(std::getline(std::cin,command)) {
            if(command=="abandon")lease.reset(); // Broken IPC/unlocked receipt alone cannot prove release.
            else if(command=="close") {
#ifdef _WIN32
                CloseHandle(media.value);media.value=INVALID_HANDLE_VALUE;
#else
                ::close(media.value);media.value=-1;
#endif
                if(lease){lease->filesClosed();lease.reset();}
            }else if(command=="quit")break;
            std::cout<<"{\"ok\":true}"<<std::endl;
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}

#ifdef _WIN32
// Windows CRT narrow argv uses the active code page, not UTF-8. Preserve the
// real Unicode command line before passing strings to filesystem::u8path.
int wmain(int argc,wchar_t** wide) {
    if(argc==2 && std::wstring(wide[1])==L"--temp-info")std::cerr<<"temp-info:wide-main"<<std::endl;
    try {
        std::vector<std::string> storage;
        for(int i=0;i<argc;++i) {
            const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,nullptr,0,nullptr,nullptr);
            n::Check(size>0,"Invalid Unicode argument");
            std::string text(size,'\0');
            n::Check(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,text.data(),size,nullptr,nullptr)==size,"Cannot encode Unicode argument");
            text.pop_back();storage.push_back(std::move(text));
        }
        std::vector<const char*> args;
        for(const auto& text:storage)args.push_back(text.c_str());
        return run(argc,args.data());
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
#else
int main(int argc,char** argv) { return run(argc,argv); }
#endif
