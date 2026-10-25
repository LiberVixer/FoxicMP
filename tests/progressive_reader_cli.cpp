// Native process/file lifecycle fixture, with no desktop or media backend.
#include "../shared/progressivereaderlease.h"
#include <iostream>
namespace p=foxicmp::progressive;
namespace n=p::lease_detail;
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--request") {
            std::string line;while(std::getline(std::cin,line))std::cout<<p::ReaderRequest(std::filesystem::u8path(argv[2]),line)<<std::endl;return 0;
        }
        n::Check(argc==5 && std::string(argv[1])=="--hold","Expected --hold ROOT SESSION MEDIA");
        const auto path=std::filesystem::u8path(argv[4]);uint64_t device=0,fileId=0;
#ifdef _WIN32
        n::Handle media(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
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
