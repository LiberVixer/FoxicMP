// Private Classic adapter fixture only. GPL-3.0-or-later.
#pragma once
#ifdef FOXICMP_PRIVATE_CLASSIC_READ_TEST
#include <functional>
#include <cstdint>
namespace foxicmp { namespace progressive { namespace classic_read_test {
// Install before the test read/graph runs; clear after all callbacks returned.
// Instrumented HarvesterSession/AsyncReader objects replace their normal objects
// in the fixture. No player target defines this macro or offers runtime control.
inline std::function<void(void*,bool,int,uint64_t,int64_t)> payload;
inline std::function<void(void*,long)> returned;
inline void Payload(void* source,bool after,int track,uint64_t offset,int64_t count){if(payload)payload(source,after,track,offset,count);}
inline void Returned(void* reader,long result){if(returned)returned(reader,result);}
}}}
#endif
