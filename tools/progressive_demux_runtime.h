#pragma once
// FFmpeg demux through bounded parent-owned AVIO; no source paths or URLs. GPL-3.0-or-later.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include "ExtLib/rapidjson/include/rapidjson/document.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"
#include "../shared/progressivesha256.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
}
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <dlfcn.h>
#include <sys/resource.h>
#endif
namespace foxicmp { namespace demux_worker {
using Doc=rapidjson::Document;using Value=rapidjson::Value;using Clock=std::chrono::steady_clock;
inline void need(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
inline std::string encode(const Value& v){rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer> w(b);v.Accept(w);return {b.GetString(),b.GetSize()};}
inline void put(Doc& d,const char* k,const std::string& v){d.AddMember(Value(k,d.GetAllocator()),Value(v.c_str(),rapidjson::SizeType(v.size()),d.GetAllocator()),d.GetAllocator());}
inline std::string hash(const uint8_t* p,size_t n){foxicmp::progressive::Sha256 s;s.update(p,n);return s.hex();}
inline std::filesystem::path directory(){
#ifdef _WIN32
 std::vector<wchar_t> b(32768);auto n=GetModuleFileNameW(nullptr,b.data(),DWORD(b.size()));need(n && n<b.size(),"worker-path-error");return std::filesystem::path(std::wstring(b.data(),n)).parent_path();
#else
 return {};
#endif
}
class Library {
#ifdef _WIN32
 HMODULE module=nullptr;
#else
 void* module=nullptr;
#endif
public:
 Library(const char* base,int major){
#ifdef _WIN32
 auto path=directory()/(std::string(base)+"-"+std::to_string(major)+".dll");module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
 auto path="lib"+std::string(base)+".so."+std::to_string(major);module=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
 need(module!=nullptr,"backend-library-unavailable");}
 // This worker handles one request. Keep FFmpeg/dependency registries loaded
 // until process exit: unloading their DSOs invalidates global cleanup roots.
 ~Library()=default;
 Library(const Library&)=delete;Library& operator=(const Library&)=delete;
 template<class T>T symbol(const char* name){
#ifdef _WIN32
 auto p=GetProcAddress(module,name);
#else
 auto p=dlsym(module,name);
#endif
 need(p!=nullptr,"backend-symbol-unavailable");return reinterpret_cast<T>(p);}
};
struct Api {
 Library util{"avutil",LIBAVUTIL_VERSION_MAJOR},codec{"avcodec",LIBAVCODEC_VERSION_MAJOR},format{"avformat",LIBAVFORMAT_VERSION_MAJOR};
#define FN(lib,name) decltype(&::name) name=lib.symbol<decltype(&::name)>(#name)
 FN(util,avutil_version);FN(codec,avcodec_version);FN(format,avformat_version);FN(format,avformat_configuration);
 FN(util,av_dict_set);FN(util,av_dict_free);FN(util,av_malloc);FN(util,av_free);FN(util,av_log_set_level);FN(util,av_get_media_type_string);
 FN(format,avio_alloc_context);FN(format,avio_context_free);FN(format,avformat_alloc_context);FN(format,avformat_free_context);
 FN(format,av_find_input_format);FN(format,avformat_open_input);FN(format,avformat_close_input);FN(format,avformat_find_stream_info);FN(format,av_read_frame);
 FN(format,avformat_alloc_output_context2);FN(format,avformat_new_stream);FN(format,avformat_write_header);FN(format,av_interleaved_write_frame);FN(format,av_write_trailer);FN(format,avio_flush);
 FN(codec,avcodec_parameters_copy);FN(codec,av_packet_rescale_ts);
 FN(codec,av_packet_alloc);FN(codec,av_packet_free);FN(codec,av_packet_unref);FN(codec,avcodec_get_name);
#undef FN
 Api(){need((avformat_version()>>16)==LIBAVFORMAT_VERSION_MAJOR && (avcodec_version()>>16)==LIBAVCODEC_VERSION_MAJOR && (avutil_version()>>16)==LIBAVUTIL_VERSION_MAJOR,"backend-abi-mismatch");av_log_set_level(AV_LOG_QUIET);}
 Doc info(){Doc d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("avformatVersion",avformat_version(),a);d.AddMember("avcodecVersion",avcodec_version(),a);d.AddMember("avutilVersion",avutil_version(),a);auto config=avformat_configuration();put(d,"configurationSha256",hash(reinterpret_cast<const uint8_t*>(config),strlen(config)));return d;}
};
inline const char* formatName(const std::string& hint){if(hint=="mp4" || hint=="m4a" || hint=="mov")return "mov";if(hint=="webm" || hint=="matroska")return "matroska";if(hint=="mpegts")return "mpegts";if(hint=="adts")return "aac";if(hint=="mp3" || hint=="ogg" || hint=="flac" || hint=="wav")return hint=="mp3"?"mp3":hint=="ogg"?"ogg":hint=="flac"?"flac":"wav";throw std::runtime_error("unsupported-container-hint");}
}} // namespace foxicmp::demux_worker
