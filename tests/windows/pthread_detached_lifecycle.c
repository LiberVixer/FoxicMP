// Regression for the bundled winpthreads runtime; no player/profile/system changes.
#include <windows.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static pthread_key_t key;
static HANDLE start,done;
static void destruct(void* value){if(value)SetEvent(done);}
static void* work(void* value){(void)value;if(WaitForSingleObject(start,5000)!=WAIT_OBJECT_0)return (void*)1;pthread_setspecific(key,(void*)1);return 0;}
int main(int argc,char** argv){
 const int joined=argc>1 && !strcmp(argv[1],"--joined");const int expectGrowth=argc>1 && !strcmp(argv[1],"--expect-growth");DWORD baseline=0,last=0;
 start=CreateEventW(0,TRUE,FALSE,0);done=CreateEventW(0,TRUE,FALSE,0);if(!start||!done||pthread_key_create(&key,destruct))return 2;
 for(int i=0;i<104;++i){pthread_t thread;ResetEvent(start);ResetEvent(done);if(pthread_create(&thread,0,work,0))return 3;if(!joined && pthread_detach(thread))return 3;SetEvent(start);if(joined && pthread_join(thread,0))return 6;if(WaitForSingleObject(done,5000)!=WAIT_OBJECT_0)return 4;Sleep(10);if(!GetProcessHandleCount(GetCurrentProcess(),&last))return 5;if(i==3)baseline=last;}
 const int growth=(int)last-(int)baseline;const int passed=expectGrowth?growth>=90:growth<=2;
 printf("{\"result\":\"%s\",\"detachedThreads\":%d,\"joinedThreads\":%d,\"warmup\":4,\"baselineHandles\":%lu,\"finalHandles\":%lu,\"growth\":%d,\"expectedOldLeak\":%s}\n",passed?(expectGrowth?"REPRODUCED":"PASS"):"FAIL",joined?0:100,joined?100:0,baseline,last,growth,expectGrowth?"true":"false");
 pthread_key_delete(key);CloseHandle(done);CloseHandle(start);return passed?0:1;
}
