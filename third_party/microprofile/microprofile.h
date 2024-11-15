#pragma once
// This is free and unencumbered software released into the public domain.
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
// For more information, please refer to <http://unlicense.org/>
//
// ***********************************************************************
//
//
//
//
// Howto:
// Call these functions from your code:
//  MicroProfileOnThreadCreate
//  MicroProfileMouseButton
//  MicroProfileMousePosition 
//  MicroProfileModKey
//  MicroProfileFlip  				<-- Call this once per frame
//  MicroProfileDraw  				<-- Call this once per frame
//  MicroProfileToggleDisplayMode 	<-- Bind to a key to toggle profiling
//  MicroProfileTogglePause			<-- Bind to a key to toggle pause
//
// Use these macros in your code in blocks you want to time:
//
// 	MICROPROFILE_DECLARE
// 	MICROPROFILE_DEFINE
// 	MICROPROFILE_DECLARE_GPU
// 	MICROPROFILE_DEFINE_GPU
// 	MICROPROFILE_SCOPE
// 	MICROPROFILE_SCOPEI
// 	MICROPROFILE_SCOPEGPU
// 	MICROPROFILE_SCOPEGPUI
//  MICROPROFILE_META
//  MICROPROFILE_LABEL
//  MICROPROFILE_LABELF
//
//	Usage:
//
//	{
//		MICROPROFILE_SCOPEI("GroupName", "TimerName", nColorRgb):
// 		..Code to be timed..
//  }
//
//	MICROPROFILE_DECLARE / MICROPROFILE_DEFINE allows defining groups in a shared place, to ensure sorting of the timers
//
//  (in global scope)
//  MICROPROFILE_DEFINE(g_ProfileFisk, "Fisk", "Skalle", nSomeColorRgb);
//
//  (in some other file)
//  MICROPROFILE_DECLARE(g_ProfileFisk);
//
//  void foo(){
//  	MICROPROFILE_SCOPE(g_ProfileFisk);
//  }
//
//  Once code is instrumented the gui is activeted by calling MicroProfileToggleDisplayMode or by clicking in the upper left corner of
//  the screen
//
// The following functions must be implemented before the profiler is usable
//  debug render:
// 		void MicroProfileDrawText(int nX, int nY, uint32_t nColor, const char* pText, uint32_t nNumCharacters);
// 		void MicroProfileDrawBox(int nX, int nY, int nX1, int nY1, uint32_t nColor, MicroProfileBoxType = MicroProfileBoxTypeFlat);
// 		void MicroProfileDrawLine2D(uint32_t nVertices, float* pVertices, uint32_t nColor);
//  Gpu time stamps: (See below for d3d/opengl helper)
// 		uint32_t MicroProfileGpuInsertTimer(void* pContext);
// 		uint64_t MicroProfileGpuGetTimeStamp(uint32_t nKey);
// 		uint64_t MicroProfileTicksPerSecondGpu();
//  threading:
//      const char* MicroProfileGetThreadName(); Threadnames in detailed view
//
// Default implementations of Gpu timestamp functions:
// 		OpenGL:
// 			in .c file where MICROPROFILE_IMPL is defined:
//   		#define MICROPROFILE_GPU_TIMERS_GL 1
// 			call MicroProfileGpuInitGL() on startup
//		D3D11:
// 			in .c file where MICROPROFILE_IMPL is defined:
//   		#define MICROPROFILE_GPU_TIMERS_D3D11 1
// 			call MicroProfileGpuInitD3D11(Device) on startup
//		D3D12:
// 			in .c file where MICROPROFILE_IMPL is defined:
//   		#define MICROPROFILE_GPU_TIMERS_D3D12 1
// 			call MicroProfileGpuInitD3D12(Device, CommandQueue) on startup
//			single-threaded: call MicroProfileGpuSetContext(CommandList) every frame before issuing GPU markers
//			multi-threaded:
//				#define MICROPROFILE_GPU_TIMERS_MULTITHREADED 1
//				on recording thread before using command list, call MicroProfileGpuBegin(CommandList)
//				on recording thread after you're done with command list, call work = MicroProfileGpuEnd()
//				when replaying, call MicroProfileGpuSubmit(work) in the same order as ExecuteCommandLists
//		Vulkan:
// 			in .c file where MICROPROFILE_IMPL is defined:
//   		#define MICROPROFILE_GPU_TIMERS_VK 1
// 			call MicroProfileGpuInitVK(Device, PhysicalDevice, Queue) on startup
// 			the rest is the same as for D3D12
//
// Limitations:
//  GPU timestamps can only be inserted from one thread.



#ifndef MICROPROFILE_ENABLED
#define MICROPROFILE_ENABLED 1
#endif

#include <stdint.h>
typedef uint64_t MicroProfileToken;
typedef uint16_t MicroProfileGroupId;

#if 0 == MICROPROFILE_ENABLED

#define MICROPROFILE_DECLARE(var)
#define MICROPROFILE_DEFINE(var, group, name, color)
#define MICROPROFILE_DECLARE_GPU(var)
#define MICROPROFILE_DEFINE_GPU(var, name, color)
#define MICROPROFILE_SCOPE(var) do{}while(0)
#define MICROPROFILE_SCOPE_TOKEN(token) do{} while(0)
#define MICROPROFILE_SCOPEI(group, name, color) do{}while(0)
#define MICROPROFILE_SCOPEGPU(var) do{}while(0)
#define MICROPROFILE_SCOPEGPUI(name, color) do{}while(0)
#define MICROPROFILE_META_CPU(name, count)
#define MICROPROFILE_META_GPU(name, count)
#define MICROPROFILE_LABEL(group, name) do{}while(0)
#define MICROPROFILE_LABELF(group, name, ...) do{}while(0)
#define MICROPROFILE_COUNTER_ADD(name, count) do{} while(0)
#define MICROPROFILE_COUNTER_SUB(name, count) do{} while(0)
#define MICROPROFILE_COUNTER_SET(name, count) do{} while(0)
#define MICROPROFILE_COUNTER_SET_LIMIT(name, count) do{} while(0)
#define MICROPROFILE_COUNTER_CONFIG(name, type, limit, flags) do{} while(0)

#define MICROPROFILE_FORCEENABLECPUGROUP(s) do{} while(0)
#define MICROPROFILE_FORCEDISABLECPUGROUP(s) do{} while(0)
#define MICROPROFILE_FORCEENABLEGPUGROUP(s) do{} while(0)
#define MICROPROFILE_FORCEDISABLEGPUGROUP(s) do{} while(0)

#define MicroProfileGetTime(group, name) 0.f
#define MicroProfileOnThreadCreate(foo) do{}while(0)
#define MicroProfileFlip() do{}while(0)
#define MicroProfileSetAggregateFrames(a) do{}while(0)
#define MicroProfileGetAggregateFrames() 0
#define MicroProfileGetCurrentAggregateFrames() 0
#define MicroProfileTogglePause() do{}while(0)
#define MicroProfileShutdown() do{}while(0)
#define MicroProfileSetForceEnable(a) do{} while(0)
#define MicroProfileGetForceEnable() false
#define MicroProfileSetEnableAllGroups(a) do{} while(0)
#define MicroProfileEnableCategory(a) do{} while(0)
#define MicroProfileDisableCategory(a) do{} while(0)
#define MicroProfileGetEnableAllGroups() false
#define MicroProfileSetForceMetaCounters(a)
#define MicroProfileGetForceMetaCounters() 0
#define MicroProfileEnableMetaCounter(c) do{} while(0)
#define MicroProfileDisableMetaCounter(c) do{} while(0)
#define MicroProfileContextSwitchTraceStart() do{} while(0)
#define MicroProfileContextSwitchTraceStop() do{} while(0)
#define MicroProfileDumpFile(path,type,frames) do{} while(0)
#define MicroProfileWebServerStart() do{} while(0)
#define MicroProfileWebServerStop() do{} while(0)
#define MicroProfileWebServerPort() 0

#define MicroProfileGpuSetContext(c) do{} while(0)
#define MicroProfileGpuBegin(c) do{} while(0)
#define MicroProfileGpuEnd() 0
#define MicroProfileGpuSubmit(w) do{} while(0)

#else

#include <stdint.h>
#include <string.h>

#ifndef MICROPROFILE_NOCXX11
#include <thread>
#include <mutex>
#include <atomic>
#endif

#ifndef MICROPROFILE_API
#define MICROPROFILE_API
#endif

MICROPROFILE_API int64_t MicroProfileTicksPerSecondCpu();


#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <unistd.h>
#include <libkern/OSAtomic.h>
#include <TargetConditionals.h>
#include <stdlib.h>
#include <alloca.h>

#define MP_TICK() mach_absolute_time()
inline int64_t MicroProfileTicksPerSecondCpu()
{
	static int64_t nTicksPerSecond = 0;	
	if(nTicksPerSecond == 0) 
	{
		mach_timebase_info_data_t sTimebaseInfo;	
		mach_timebase_info(&sTimebaseInfo);
		nTicksPerSecond = 1000000000ll * sTimebaseInfo.denom / sTimebaseInfo.numer;
	}
	return nTicksPerSecond;
}
inline uint64_t MicroProfileGetCurrentThreadId()
{	
	uint64_t tid;
	pthread_threadid_np(nullptr, &tid);
	return tid;
}

#define MP_BREAK() __builtin_trap()
#if __has_feature(tls)
#define MP_THREAD_LOCAL __thread
#endif
#define MP_STRCASECMP strcasecmp
#define MP_GETCURRENTTHREADID() MicroProfileGetCurrentThreadId()
typedef uint64_t MicroProfileThreadIdType;
#define MP_GETCURRENTPROCESSID() getpid()
typedef uint32_t MicroProfileProcessIdType;
#elif defined(_WIN32)
int64_t MicroProfileGetTick();
#define MP_TICK() MicroProfileGetTick()
#define MP_BREAK() __debugbreak()
#define MP_THREAD_LOCAL __declspec(thread)
#define MP_STRCASECMP _stricmp
#define MP_GETCURRENTTHREADID() GetCurrentThreadId()
typedef uint32_t MicroProfileThreadIdType;
#define MP_GETCURRENTPROCESSID() GetCurrentProcessId()
typedef uint32_t MicroProfileProcessIdType;

#elif defined(__linux__)
#include <unistd.h>
#include <time.h>
inline int64_t MicroProfileTicksPerSecondCpu()
{
	return 1000000000ll;
}

inline int64_t MicroProfileGetTick()
{
	timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return 1000000000ll * ts.tv_sec + ts.tv_nsec;
}
#define MP_TICK() MicroProfileGetTick()
#define MP_BREAK() __builtin_trap()
#ifndef __ANDROID__ // __thread is incompatible with ffunction-sections/fdata-sections
#define MP_THREAD_LOCAL __thread
#endif
#define MP_STRCASECMP strcasecmp
#define MP_GETCURRENTTHREADID() (uint64_t)pthread_self()
typedef uint64_t MicroProfileThreadIdType;
#define MP_GETCURRENTPROCESSID() getpid()
typedef uint32_t MicroProfileProcessIdType;
#endif


#ifndef MP_GETCURRENTTHREADID 
#define MP_GETCURRENTTHREADID() 0
typedef uint32_t MicroProfileThreadIdType;
#endif

#ifndef MP_GETCURRENTPROCESSID
#define MP_GETCURRENTPROCESSID() 0
typedef uint32_t MicroProfileProcessIdType;
#endif

#ifndef MP_ASSERT
#define MP_ASSERT(a) do{if(!(a)){MP_BREAK();} }while(0)
#endif

#define MICROPROFILE_DECLARE(var) extern MicroProfileToken g_mp_##var
#define MICROPROFILE_DEFINE(var, group, name, color) MicroProfileToken g_mp_##var = MicroProfileGetToken(group, name, color, MicroProfileTokenTypeCpu)
#define MICROPROFILE_DECLARE_GPU(var) extern MicroProfileToken g_mp_##var
#define MICROPROFILE_DEFINE_GPU(var, name, color) MicroProfileToken g_mp_##var = MicroProfileGetToken("GPU", name, color, MicroProfileTokenTypeGpu)
#define MICROPROFILE_TOKEN_PASTE0(a, b) a ## b
#define MICROPROFILE_TOKEN_PASTE(a, b)  MICROPROFILE_TOKEN_PASTE0(a,b)
#define MICROPROFILE_SCOPE(var) MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo, __LINE__)(g_mp_##var)
#define MICROPROFILE_SCOPE_TOKEN(token) MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo, __LINE__)(token)
#define MICROPROFILE_SCOPEI(group, name, color) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__) = MicroProfileGetToken(group, name, color, MicroProfileTokenTypeCpu); MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo,__LINE__)( MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__))
#define MICROPROFILE_SCOPEGPU(var) MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo, __LINE__)(g_mp_##var)
#define MICROPROFILE_SCOPEGPUI(name, color) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__) = MicroProfileGetToken("GPU", name, color,  MicroProfileTokenTypeGpu); MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo,__LINE__)( MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__))
#define MICROPROFILE_META_CPU(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__) = MicroProfileGetMetaToken(name); MicroProfileMetaUpdate(MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__), count, MicroProfileTokenTypeCpu)
#define MICROPROFILE_META_GPU(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__) = MicroProfileGetMetaToken(name); MicroProfileMetaUpdate(MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__), count, MicroProfileTokenTypeGpu)
#define MICROPROFILE_LABEL(group, name) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__) = MicroProfileGetLabelToken(group); MicroProfileLabel(MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__), name)
#define MICROPROFILE_LABELF(group, name, ...) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__) = MicroProfileGetLabelToken(group); MicroProfileLabelFormat(MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__), name, ## __VA_ARGS__)
#define MICROPROFILE_COUNTER_ADD(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_counter,__LINE__) = MicroProfileGetCounterToken(name); MicroProfileCounterAdd(MICROPROFILE_TOKEN_PASTE(g_mp_counter,__LINE__), count)
#define MICROPROFILE_COUNTER_SUB(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_counter,__LINE__) = MicroProfileGetCounterToken(name); MicroProfileCounterAdd(MICROPROFILE_TOKEN_PASTE(g_mp_counter,__LINE__), -(int64_t)count)
#define MICROPROFILE_COUNTER_SET(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_counter,__LINE__) = MicroProfileGetCounterToken(name); MicroProfileCounterSet(MICROPROFILE_TOKEN_PASTE(g_mp_counter,__LINE__), count)
#define MICROPROFILE_COUNTER_SET_LIMIT(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_counter,__LINE__) = MicroProfileGetCounterToken(name); MicroProfileCounterSetLimit(MICROPROFILE_TOKEN_PASTE(g_mp_counter,__LINE__), count)
#define MICROPROFILE_COUNTER_CONFIG(name, type, limit, flags) MicroProfileCounterConfig(name, type, limit, flags

#ifndef MICROPROFILE_USE_THREAD_NAME_CALLBACK
#define MICROPROFILE_USE_THREAD_NAME_CALLBACK 0
#endif

#ifndef MICROPROFILE_PER_THREAD_BUFFER_SIZE
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (2048<<10)
#endif

#ifndef MICROPROFILE_PER_THREAD_GPU_BUFFER_SIZE
#define MICROPROFILE_PER_THREAD_GPU_BUFFER_SIZE (1024<<10)
#endif

#ifndef MICROPROFILE_MAX_FRAME_HISTORY
#define MICROPROFILE_MAX_FRAME_HISTORY 512
#endif

#ifndef MICROPROFILE_PRINTF
#define MICROPROFILE_PRINTF printf
#endif

#ifndef MICROPROFILE_META_MAX
#define MICROPROFILE_META_MAX 8
#endif

#ifndef MICROPROFILE_WEBSERVER_PORT
#define MICROPROFILE_WEBSERVER_PORT 1338
#endif

#ifndef MICROPROFILE_WEBSERVER
#define MICROPROFILE_WEBSERVER 1
#endif

#ifndef MICROPROFILE_WEBSERVER_FRAMES
#define MICROPROFILE_WEBSERVER_FRAMES 30
#endif

#ifndef MICROPROFILE_WEBSERVER_SOCKET_BUFFER_SIZE
#define MICROPROFILE_WEBSERVER_SOCKET_BUFFER_SIZE (16<<10)
#endif

#ifndef MICROPROFILE_LABEL_BUFFER_SIZE
#define MICROPROFILE_LABEL_BUFFER_SIZE (1024<<10)
#endif

#ifndef MICROPROFILE_GPU_MAX_QUERIES
#define MICROPROFILE_GPU_MAX_QUERIES (8<<10)
#endif

#ifndef MICROPROFILE_GPU_FRAME_DELAY
#define MICROPROFILE_GPU_FRAME_DELAY 3
#endif

#ifndef MICROPROFILE_NAME_MAX_LEN
#define MICROPROFILE_NAME_MAX_LEN 64
#endif

#ifndef MICROPROFILE_LABEL_MAX_LEN
#define MICROPROFILE_LABEL_MAX_LEN 256
#endif

#ifndef MICROPROFILE_EMBED_HTML
#define MICROPROFILE_EMBED_HTML 1
#endif

#ifndef MICROPROFILE_GPU_TIMERS_MULTITHREADED
#define MICROPROFILE_GPU_TIMERS_MULTITHREADED 0
#endif

#define MICROPROFILE_FORCEENABLECPUGROUP(s) MicroProfileForceEnableGroup(s, MicroProfileTokenTypeCpu)
#define MICROPROFILE_FORCEDISABLECPUGROUP(s) MicroProfileForceDisableGroup(s, MicroProfileTokenTypeCpu)
#define MICROPROFILE_FORCEENABLEGPUGROUP(s) MicroProfileForceEnableGroup(s, MicroProfileTokenTypeGpu)
#define MICROPROFILE_FORCEDISABLEGPUGROUP(s) MicroProfileForceDisableGroup(s, MicroProfileTokenTypeGpu)

#define MICROPROFILE_INVALID_TICK ((uint64_t)-1)
#define MICROPROFILE_GROUP_MASK_ALL 0xffffffffffff


#define MICROPROFILE_INVALID_TOKEN (uint64_t)0

enum MicroProfileTokenType
{
	MicroProfileTokenTypeCpu,
	MicroProfileTokenTypeGpu,
};

enum MicroProfileBoxType
{
	MicroProfileBoxTypeBar,
	MicroProfileBoxTypeFlat,
};

enum MicroProfileDumpType
{
	MicroProfileDumpTypeHtml,
	MicroProfileDumpTypeCsv
};

#ifdef __GNUC__
#define MICROPROFILE_FORMAT(a, b) __attribute__((format(printf, a, b)))
#else
#define MICROPROFILE_FORMAT(a, b)
#endif

struct MicroProfile;

MICROPROFILE_API void MicroProfileInit();
MICROPROFILE_API void MicroProfileShutdown();
MICROPROFILE_API MicroProfileToken MicroProfileFindToken(const char* sGroup, const char* sName);
MICROPROFILE_API MicroProfileToken MicroProfileGetToken(const char* sGroup, const char* sName, uint32_t nColor, MicroProfileTokenType Token = MicroProfileTokenTypeCpu);
MICROPROFILE_API MicroProfileToken MicroProfileGetLabelToken(const char* sGroup, MicroProfileTokenType Token = MicroProfileTokenTypeCpu);
MICROPROFILE_API MicroProfileToken MicroProfileGetMetaToken(const char* pName);
MICROPROFILE_API MicroProfileToken MicroProfileGetCounterToken(const char* pName);
MICROPROFILE_API void MicroProfileMetaUpdate(MicroProfileToken, int nCount, MicroProfileTokenType eTokenType);
MICROPROFILE_API void MicroProfileCounterAdd(MicroProfileToken nToken, int64_t nCount);
MICROPROFILE_API void MicroProfileCounterSet(MicroProfileToken nToken, int64_t nCount);
MICROPROFILE_API void MicroProfileCounterSetLimit(MicroProfileToken nToken, int64_t nCount);
MICROPROFILE_API void MicroProfileCounterConfig(const char* pCounterName, uint32_t nFormat, int64_t nLimit, uint32_t nFlags);
MICROPROFILE_API uint64_t MicroProfileEnter(MicroProfileToken nToken);
MICROPROFILE_API void MicroProfileLeave(MicroProfileToken nToken, uint64_t nTick);
MICROPROFILE_API void MicroProfileLabel(MicroProfileToken nToken, const char* pName);
MICROPROFILE_FORMAT(2, 3) MICROPROFILE_API void MicroProfileLabelFormat(MicroProfileToken nToken, const char* pName, ...);
MICROPROFILE_API void MicroProfileLabelFormatV(MicroProfileToken nToken, const char* pName, va_list args);
inline uint16_t MicroProfileGetTimerIndex(MicroProfileToken t){ return (t&0xffff); }
inline uint64_t MicroProfileGetGroupMask(MicroProfileToken t){ return ((t>>16)&MICROPROFILE_GROUP_MASK_ALL);}
inline MicroProfileToken MicroProfileMakeToken(uint64_t nGroupMask, uint16_t nTimer){ return (nGroupMask<<16) | nTimer;}

MICROPROFILE_API void MicroProfileFlip(); //! call once per frame.
MICROPROFILE_API void MicroProfileTogglePause();
MICROPROFILE_API void MicroProfileForceEnableGroup(const char* pGroup, MicroProfileTokenType Type);
MICROPROFILE_API void MicroProfileForceDisableGroup(const char* pGroup, MicroProfileTokenType Type);
MICROPROFILE_API float MicroProfileGetTime(const char* pGroup, const char* pName);

MICROPROFILE_API void MicroProfileOnThreadCreate(const char* pThreadName); //should be called from newly created threads
MICROPROFILE_API void MicroProfileOnThreadExit(); //call on exit to reuse log
MICROPROFILE_API void MicroProfileSetForceEnable(bool bForceEnable);
MICROPROFILE_API bool MicroProfileGetForceEnable();
MICROPROFILE_API void MicroProfileSetEnableAllGroups(bool bEnable); 
MICROPROFILE_API void MicroProfileEnableCategory(const char* pCategory); 
MICROPROFILE_API void MicroProfileDisableCategory(const char* pCategory); 
MICROPROFILE_API bool MicroProfileGetEnableAllGroups();
MICROPROFILE_API void MicroProfileSetForceMetaCounters(bool bEnable); 
MICROPROFILE_API bool MicroProfileGetForceMetaCounters();
MICROPROFILE_API void MicroProfileEnableMetaCounter(const char* pMet);
MICROPROFILE_API void MicroProfileDisableMetaCounter(const char* pMet);
MICROPROFILE_API void MicroProfileSetAggregateFrames(int frames);
MICROPROFILE_API int MicroProfileGetAggregateFrames();
MICROPROFILE_API int MicroProfileGetCurrentAggregateFrames();
MICROPROFILE_API MicroProfile* MicroProfileGet();
MICROPROFILE_API void MicroProfileGetRange(uint32_t nPut, uint32_t nGet, uint32_t nRange[2][2]);
MICROPROFILE_API std::recursive_mutex& MicroProfileGetMutex();

MICROPROFILE_API void MicroProfileContextSwitchTraceStart();
MICROPROFILE_API void MicroProfileContextSwitchTraceStop();

struct MicroProfileThreadInfo
{
	MicroProfileProcessIdType nProcessId;
	MicroProfileThreadIdType nThreadId;
};

MICROPROFILE_API void MicroProfileContextSwitchSearch(uint32_t* pContextSwitchStart, uint32_t* pContextSwitchEnd, uint64_t nBaseTicksCpu, uint64_t nBaseTicksEndCpu);
MICROPROFILE_API uint32_t MicroProfileContextSwitchGatherThreads(uint32_t nContextSwitchStart, uint32_t nContextSwitchEnd, MicroProfileThreadInfo* Threads, uint32_t* nNumThreadsBase);

MICROPROFILE_API const char* MicroProfileGetProcessName(MicroProfileProcessIdType nId, char* Buffer, uint32_t nSize);

MICROPROFILE_API void MicroProfileDumpFile(const char* pPath, MicroProfileDumpType eType, uint32_t nFrames);
MICROPROFILE_API int MicroProfileFormatCounter(int eFormat, int64_t nCounter, char* pOut, uint32_t nBufferSize);

MICROPROFILE_API void MicroProfileWebServerStart();
MICROPROFILE_API void MicroProfileWebServerStop();
MICROPROFILE_API uint32_t MicroProfileWebServerPort();

MICROPROFILE_API void MicroProfileGpuInitGL();
MICROPROFILE_API void MicroProfileGpuInitD3D11(struct ID3D11Device* pDevice);
MICROPROFILE_API void MicroProfileGpuInitD3D12(struct ID3D12Device* pDevice, struct ID3D12CommandQueue* pCommandQueue);
MICROPROFILE_API void MicroProfileGpuInitVK(struct VkDevice_T* pDevice, struct VkPhysicalDevice_T* pPhysicalDevice, struct VkQueue_T* pQueue);
MICROPROFILE_API void MicroProfileGpuShutdown();

MICROPROFILE_API void MicroProfileGpuSetContext(void* pContext);
MICROPROFILE_API void MicroProfileGpuBegin(void* pContext);
MICROPROFILE_API uint64_t MicroProfileGpuEnd();
MICROPROFILE_API void MicroProfileGpuSubmit(uint64_t nWork);

MICROPROFILE_API uint32_t MicroProfileGpuInsertTimer(void* pContext);
MICROPROFILE_API uint64_t MicroProfileGpuGetTimeStamp(uint32_t nKey);
MICROPROFILE_API uint64_t MicroProfileTicksPerSecondGpu();
MICROPROFILE_API uint32_t MicroProfileGpuFlip();
MICROPROFILE_API bool MicroProfileGetGpuTickReference(int64_t* pOutCpu, int64_t* pOutGpu);

#if MICROPROFILE_USE_THREAD_NAME_CALLBACK
MICROPROFILE_API const char* MicroProfileGetThreadName();
#else
#define MicroProfileGetThreadName() "<Unknown>"
#endif

#if !defined(MICROPROFILE_THREAD_NAME_FROM_ID)
#define MICROPROFILE_THREAD_NAME_FROM_ID(a) ""
#endif

struct MicroProfileScopeHandler
{
	MicroProfileToken nToken;
	uint64_t nTick;
	MicroProfileScopeHandler(MicroProfileToken Token):nToken(Token)
	{
		nTick = MicroProfileEnter(nToken);
	}
	~MicroProfileScopeHandler()
	{
		MicroProfileLeave(nToken, nTick);
	}
};

#define MICROPROFILE_MAX_COUNTERS 512
#define MICROPROFILE_MAX_COUNTER_NAME_CHARS (MICROPROFILE_MAX_COUNTERS*16)

#define MICROPROFILE_MAX_GROUPS 48 //dont bump! no. of bits used it bitmask
#define MICROPROFILE_MAX_CATEGORIES 16
#define MICROPROFILE_MAX_GRAPHS 5
#define MICROPROFILE_GRAPH_HISTORY 128
#define MICROPROFILE_BUFFER_SIZE ((MICROPROFILE_PER_THREAD_BUFFER_SIZE)/sizeof(MicroProfileLogEntry))
#define MICROPROFILE_GPU_BUFFER_SIZE ((MICROPROFILE_PER_THREAD_GPU_BUFFER_SIZE)/sizeof(MicroProfileLogEntry))
#define MICROPROFILE_GPU_FRAMES ((MICROPROFILE_GPU_FRAME_DELAY)+1)
#define MICROPROFILE_MAX_CONTEXT_SWITCH_THREADS 256
#define MICROPROFILE_STACK_MAX 32
//#define MICROPROFILE_MAX_PRESETS 5
#define MICROPROFILE_ANIM_DELAY_PRC 0.5f
#define MICROPROFILE_GAP_TIME 50 //extra ms to fetch to close timers from earlier frames


#ifndef MICROPROFILE_MAX_TIMERS
#define MICROPROFILE_MAX_TIMERS 1024
#endif

#ifndef MICROPROFILE_MAX_THREADS
#define MICROPROFILE_MAX_THREADS 32
#endif 

#ifndef MICROPROFILE_UNPACK_RED
#define MICROPROFILE_UNPACK_RED(c) ((c)>>16)
#endif

#ifndef MICROPROFILE_UNPACK_GREEN
#define MICROPROFILE_UNPACK_GREEN(c) ((c)>>8)
#endif

#ifndef MICROPROFILE_UNPACK_BLUE
#define MICROPROFILE_UNPACK_BLUE(c) ((c))
#endif

#ifndef MICROPROFILE_DEFAULT_PRESET
#define MICROPROFILE_DEFAULT_PRESET "Default"
#endif


#ifndef MICROPROFILE_CONTEXT_SWITCH_TRACE 
#if defined(_WIN32) 
#define MICROPROFILE_CONTEXT_SWITCH_TRACE 1
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
#define MICROPROFILE_CONTEXT_SWITCH_TRACE 1
#else
#define MICROPROFILE_CONTEXT_SWITCH_TRACE 0
#endif
#endif

#if MICROPROFILE_CONTEXT_SWITCH_TRACE
#define MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE (128*1024) //2mb with 16 byte entry size
#else
#define MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE (1)
#endif

#ifndef MICROPROFILE_MINIZ
#define MICROPROFILE_MINIZ 0
#endif

#ifndef MICROPROFILE_COUNTER_HISTORY
#define MICROPROFILE_COUNTER_HISTORY 1
#endif

#ifdef _WIN32
#include <basetsd.h>
typedef UINT_PTR MpSocket;
#else
typedef int MpSocket;
#endif

typedef std::thread* MicroProfileThread;

enum MicroProfileDrawMask
{
	MP_DRAW_OFF			= 0x0,
	MP_DRAW_BARS		= 0x1,
	MP_DRAW_DETAILED	= 0x2,
	MP_DRAW_COUNTERS	= 0x3,
	MP_DRAW_FRAME		= 0x4,
	MP_DRAW_HIDDEN		= 0x5,
	MP_DRAW_SIZE		= 0x6,
};

enum MicroProfileDrawBarsMask
{
	MP_DRAW_TIMERS 				= 0x1,	
	MP_DRAW_AVERAGE				= 0x2,	
	MP_DRAW_MAX					= 0x4,	
	MP_DRAW_MIN					= 0x8,
	MP_DRAW_CALL_COUNT			= 0x10,
	MP_DRAW_TIMERS_EXCLUSIVE 	= 0x20,
	MP_DRAW_AVERAGE_EXCLUSIVE 	= 0x40,	
	MP_DRAW_MAX_EXCLUSIVE		= 0x80,
	MP_DRAW_META_FIRST			= 0x100,
	MP_DRAW_ALL 				= 0xffffffff,

};

enum MicroProfileCounterFormat
{
	MICROPROFILE_COUNTER_FORMAT_DEFAULT,
	MICROPROFILE_COUNTER_FORMAT_BYTES,
};

enum MicroProfileCounterFlags
{
	MICROPROFILE_COUNTER_FLAG_NONE 			= 0,
	MICROPROFILE_COUNTER_FLAG_DETAILED		= 0x1,
	MICROPROFILE_COUNTER_FLAG_DETAILED_GRAPH= 0x2,
	//internal:
	MICROPROFILE_COUNTER_FLAG_INTERNAL_MASK = ~0x3,
	MICROPROFILE_COUNTER_FLAG_HAS_LIMIT 	= 0x4,
	MICROPROFILE_COUNTER_FLAG_CLOSED		= 0x8,
	MICROPROFILE_COUNTER_FLAG_MANUAL_SWAP	= 0x10,
	MICROPROFILE_COUNTER_FLAG_LEAF			= 0x20,
};

typedef uint64_t MicroProfileLogEntry;

struct MicroProfileTimer
{
	uint64_t nTicks;
	uint32_t nCount;
};

struct MicroProfileCategory
{
	char pName[MICROPROFILE_NAME_MAX_LEN];
	uint64_t nGroupMask;
};

struct MicroProfileGroupInfo
{
	char pName[MICROPROFILE_NAME_MAX_LEN];
	uint32_t nNameLen;
	uint32_t nGroupIndex;
	uint32_t nNumTimers;
	uint32_t nMaxTimerNameLen;
	uint32_t nColor;
	uint32_t nCategory;
	MicroProfileTokenType Type;
};

struct MicroProfileTimerInfo
{
	MicroProfileToken nToken;
	uint32_t nTimerIndex;
	uint32_t nGroupIndex;
	char pName[MICROPROFILE_NAME_MAX_LEN];
	uint32_t nNameLen;
	uint32_t nColor;
	bool bGraph;
};

struct MicroProfileCounterInfo
{
	int nParent;
	int nSibling;
	int nFirstChild;
	uint16_t nNameLen;
	uint8_t nLevel;
	char* pName;
	uint32_t nFlags;
	int64_t nLimit;
	MicroProfileCounterFormat eFormat;
};

struct MicroProfileCounterHistory
{
	uint32_t nPut;
	uint64_t nHistory[MICROPROFILE_GRAPH_HISTORY];
};

struct MicroProfileGraphState
{
	int64_t nHistory[MICROPROFILE_GRAPH_HISTORY];
	MicroProfileToken nToken;
	int32_t nKey;
};

struct MicroProfileContextSwitch
{
	MicroProfileThreadIdType nThreadOut;
	MicroProfileThreadIdType nThreadIn;
	MicroProfileProcessIdType nProcessIn;
	int64_t nCpu : 8;
	int64_t nTicks : 56;
};


struct MicroProfileFrameState
{
	int64_t nFrameStartCpu;
	int64_t nFrameStartGpu;
	uint32_t nFrameStartGpuTimer;
	uint32_t nLogStart[MICROPROFILE_MAX_THREADS];
};

struct MicroProfileThreadLog
{
	MicroProfileLogEntry*	Log;
	std::atomic<uint32_t>	nPut;
	std::atomic<uint32_t>	nGet;

	MicroProfileLogEntry*	LogGpu;
	std::atomic<uint32_t>	nPutGpu;
	uint32_t				nStartGpu;
	uint32_t				bActiveGpu;
	void*					pContextGpu;

	uint32_t 				nActive;
	uint32_t 				nGpu;
	MicroProfileThreadIdType nThreadId;
	uint32_t 				nLogIndex;

	uint32_t				nStack[MICROPROFILE_STACK_MAX];
	int64_t					nChildTickStack[MICROPROFILE_STACK_MAX];
	uint32_t				nStackPos;


	uint8_t					nGroupStackPos[MICROPROFILE_MAX_GROUPS];
	int64_t 				nGroupTicks[MICROPROFILE_MAX_GROUPS];
	int64_t 				nAggregateGroupTicks[MICROPROFILE_MAX_GROUPS];
	enum
	{
		THREAD_MAX_LEN = 64,
	};
	char					ThreadName[64];

	int 					nFreeListNext;
};

struct MicroProfileGpu
{
	void (*Shutdown)();
	uint32_t (*Flip)();
	uint32_t (*InsertTimer)(void* pContext);
	uint64_t (*GetTimeStamp)(uint32_t nIndex);
	uint64_t (*GetTicksPerSecond)();
	bool (*GetTickReference)(int64_t* pOutCpu, int64_t* pOutGpu);
};

struct MicroProfile
{
	uint32_t nTotalTimers;
	uint32_t nGroupCount;
	uint32_t nCategoryCount;
	uint32_t nAggregateClear;
	uint32_t nAggregateFlip;
	uint32_t nAggregateFlipCount;
	uint32_t nAggregateFrames;

	uint64_t nAggregateFlipTick;
	
	uint32_t nDisplay;
	uint32_t nBars;
	uint64_t nActiveGroup;
	uint32_t nActiveBars;

	uint64_t nForceGroup;
	uint32_t nForceEnable;
	uint32_t nForceMetaCounters;

	uint64_t nForceGroupUI;
	uint64_t nActiveGroupWanted;
	uint32_t nAllGroupsWanted;
	uint32_t nAllThreadsWanted;

	uint32_t nOverflow;

	uint64_t nGroupMask;
	uint64_t nGroupMaskGpu;
	uint32_t nRunning;
	uint32_t nToggleRunning;
	uint32_t nMaxGroupSize;
	uint32_t nDumpFileNextFrame;
	uint32_t nAutoClearFrames;
	MicroProfileDumpType eDumpType;
	uint32_t nDumpFrames;
	char DumpPath[512];

	int64_t nPauseTicks;

	float fReferenceTime;
	float fRcpReferenceTime;

	MicroProfileCategory	CategoryInfo[MICROPROFILE_MAX_CATEGORIES];
	MicroProfileGroupInfo 	GroupInfo[MICROPROFILE_MAX_GROUPS];
	MicroProfileTimerInfo 	TimerInfo[MICROPROFILE_MAX_TIMERS];
	uint8_t					TimerToGroup[MICROPROFILE_MAX_TIMERS];
	
	MicroProfileTimer 		AccumTimers[MICROPROFILE_MAX_TIMERS];
	uint64_t				AccumMaxTimers[MICROPROFILE_MAX_TIMERS];
	uint64_t				AccumMinTimers[MICROPROFILE_MAX_TIMERS];
	uint64_t				AccumTimersExclusive[MICROPROFILE_MAX_TIMERS];
	uint64_t				AccumMaxTimersExclusive[MICROPROFILE_MAX_TIMERS];

	MicroProfileTimer 		Frame[MICROPROFILE_MAX_TIMERS];
	uint64_t				FrameExclusive[MICROPROFILE_MAX_TIMERS];

	MicroProfileTimer 		Aggregate[MICROPROFILE_MAX_TIMERS];
	uint64_t				AggregateMax[MICROPROFILE_MAX_TIMERS];	
	uint64_t				AggregateMin[MICROPROFILE_MAX_TIMERS];
	uint64_t				AggregateExclusive[MICROPROFILE_MAX_TIMERS];
	uint64_t				AggregateMaxExclusive[MICROPROFILE_MAX_TIMERS];


	uint64_t 				FrameGroup[MICROPROFILE_MAX_GROUPS];
	uint64_t 				AccumGroup[MICROPROFILE_MAX_GROUPS];
	uint64_t 				AccumGroupMax[MICROPROFILE_MAX_GROUPS];
	
	uint64_t 				AggregateGroup[MICROPROFILE_MAX_GROUPS];
	uint64_t 				AggregateGroupMax[MICROPROFILE_MAX_GROUPS];


	struct 
	{
		uint64_t nCounters[MICROPROFILE_MAX_TIMERS];

		uint64_t nAccum[MICROPROFILE_MAX_TIMERS];
		uint64_t nAccumMax[MICROPROFILE_MAX_TIMERS];

		uint64_t nAggregate[MICROPROFILE_MAX_TIMERS];
		uint64_t nAggregateMax[MICROPROFILE_MAX_TIMERS];

		uint64_t nSum;
		uint64_t nSumAccum;
		uint64_t nSumAccumMax;
		uint64_t nSumAggregate;
		uint64_t nSumAggregateMax;

		const char* pName;
	} MetaCounters[MICROPROFILE_META_MAX];

	MicroProfileGraphState	Graph[MICROPROFILE_MAX_GRAPHS];
	uint32_t				nGraphPut;

	uint32_t				nThreadActive[MICROPROFILE_MAX_THREADS];
	MicroProfileThreadLog* 	Pool[MICROPROFILE_MAX_THREADS];
	uint32_t				nNumLogs;
	uint32_t 				nMemUsage;
	int 					nFreeListHead;

	uint32_t 				nFrameCurrent;
	uint32_t 				nFrameCurrentIndex;
	uint32_t 				nFramePut;
	uint64_t				nFramePutIndex;

	MicroProfileFrameState Frames[MICROPROFILE_MAX_FRAME_HISTORY];

	uint64_t				nFlipTicks;
	uint64_t				nFlipAggregate;
	uint64_t				nFlipMax;
	uint64_t				nFlipAggregateDisplay;
	uint64_t				nFlipMaxDisplay;

	MicroProfileThread 			ContextSwitchThread;
	bool  						bContextSwitchRunning;
	bool						bContextSwitchStart;
	bool						bContextSwitchStop;
	bool						bContextSwitchAllThreads;
	bool						bContextSwitchNoBars;
	uint32_t					nContextSwitchUsage;
	uint32_t					nContextSwitchLastPut;

	int64_t						nContextSwitchHoverTickIn;
	int64_t						nContextSwitchHoverTickOut;
	uint32_t					nContextSwitchHoverThread;
	uint32_t					nContextSwitchHoverThreadBefore;
	uint32_t					nContextSwitchHoverThreadAfter;
	uint8_t						nContextSwitchHoverCpu;
	uint8_t						nContextSwitchHoverCpuNext;

	uint32_t					nContextSwitchPut;	
	MicroProfileContextSwitch 	ContextSwitch[MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE];

	MicroProfileThread			WebServerThread;

	MpSocket 					WebServerSocket;
	uint32_t					nWebServerPort;

	char						WebServerBuffer[MICROPROFILE_WEBSERVER_SOCKET_BUFFER_SIZE];
	uint32_t					nWebServerPut;
	uint64_t 					nWebServerDataSent;

	std::atomic<char*>			LabelBuffer;
	std::atomic<uint64_t>		nLabelPut;

	char 						CounterNames[MICROPROFILE_MAX_COUNTER_NAME_CHARS];
	MicroProfileCounterInfo 	CounterInfo[MICROPROFILE_MAX_COUNTERS];
	uint32_t					nNumCounters;
	uint32_t					nCounterNamePos;
	std::atomic<int64_t> 		Counters[MICROPROFILE_MAX_COUNTERS];

#if MICROPROFILE_COUNTER_HISTORY // uses 1kb per allocated counter. 512kb for default counter count
	uint32_t					nCounterHistoryPut;
	int64_t 					nCounterHistory[MICROPROFILE_GRAPH_HISTORY][MICROPROFILE_MAX_COUNTERS]; //flipped to make swapping cheap, drawing more expensive.
	int64_t 					nCounterMax[MICROPROFILE_MAX_COUNTERS];
	int64_t 					nCounterMin[MICROPROFILE_MAX_COUNTERS];	
#endif

	uint32_t					nGpuFrameTimer;
	MicroProfileGpu				GPU;
};

#define MP_LOG_TICK_MASK  0x0000ffffffffffff
#define MP_LOG_INDEX_MASK 0x1fff000000000000
#define MP_LOG_BEGIN_MASK 0xe000000000000000
#define MP_LOG_GPU_EXTRA 0x4
#define MP_LOG_LABEL 0x3
#define MP_LOG_META 0x2
#define MP_LOG_ENTER 0x1
#define MP_LOG_LEAVE 0x0


inline uint64_t MicroProfileLogType(MicroProfileLogEntry Index)
{
	return ((MP_LOG_BEGIN_MASK & Index)>>61) & 0x7;
}

inline uint64_t MicroProfileLogTimerIndex(MicroProfileLogEntry Index)
{
	return (MP_LOG_INDEX_MASK & Index)>>48;
}

inline MicroProfileLogEntry MicroProfileMakeLogIndex(uint64_t nBegin, MicroProfileToken nToken, int64_t nTick)
{
	return (nBegin<<61) | (MP_LOG_INDEX_MASK&(nToken<<48)) | (MP_LOG_TICK_MASK&nTick);
} 

inline int64_t MicroProfileLogTickDifference(MicroProfileLogEntry Start, MicroProfileLogEntry End)
{
	uint64_t nStart = Start;
	uint64_t nEnd = End;
	int64_t nDifference = ((nEnd<<16) - (nStart<<16)); 
	return nDifference >> 16;
}

inline int64_t MicroProfileLogGetTick(MicroProfileLogEntry e)
{
	return MP_LOG_TICK_MASK & e;
}

inline int64_t MicroProfileLogSetTick(MicroProfileLogEntry e, int64_t nTick)
{
	return (MP_LOG_TICK_MASK & nTick) | (e & ~MP_LOG_TICK_MASK);
}

template<typename T>
T MicroProfileMin(T a, T b)
{ return a < b ? a : b; }

template<typename T>
T MicroProfileMax(T a, T b)
{ return a > b ? a : b; }
template<typename T>
T MicroProfileClamp(T a, T min_, T max_)
{ return MicroProfileMin(max_, MicroProfileMax(min_, a));  }

inline int64_t MicroProfileMsToTick(float fMs, int64_t nTicksPerSecond)
{
	return (int64_t)(fMs*0.001f*nTicksPerSecond);
}

inline float MicroProfileTickToMsMultiplier(int64_t nTicksPerSecond)
{
	return 1000.f / nTicksPerSecond;
}

inline uint16_t MicroProfileGetGroupIndex(MicroProfileToken t)
{
	return (uint16_t)MicroProfileGet()->TimerToGroup[MicroProfileGetTimerIndex(t)];
}



#ifdef MICROPROFILE_IMPL

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define snprintf _snprintf

#pragma warning(push)
#pragma warning(disable: 4244)
int64_t MicroProfileTicksPerSecondCpu()
{
	static int64_t nTicksPerSecond = 0;	
	if(nTicksPerSecond == 0) 
	{
		QueryPerformanceFrequency((LARGE_INTEGER*)&nTicksPerSecond);
	}
	return nTicksPerSecond;
}
int64_t MicroProfileGetTick()
{
	int64_t ticks;
	QueryPerformanceCounter((LARGE_INTEGER*)&ticks);
	return ticks;
}

#endif

#if MICROPROFILE_WEBSERVER || MICROPROFILE_CONTEXT_SWITCH_TRACE
typedef void* (*MicroProfileThreadFunc)(void*);

inline void MicroProfileThreadStart(MicroProfileThread* pThread, MicroProfileThreadFunc Func)
{
	*pThread = new std::thread(Func, nullptr);
}
inline void MicroProfileThreadJoin(MicroProfileThread* pThread)
{
	(*pThread)->join();
	delete *pThread;
	*pThread = nullptr;
}
#endif

#if MICROPROFILE_WEBSERVER

#ifdef _WIN32
#if defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#error WinSock.h has already been included; microprofile requires WinSock2
#endif
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")
#define MP_INVALID_SOCKET(f) (f == INVALID_SOCKET)
#endif

#if defined(__APPLE__) || defined(__linux__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <fcntl.h>
#define MP_INVALID_SOCKET(f) (f < 0)
#endif

#endif 

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <algorithm>
#include <strings.h>
#include <stdlib.h>


#ifndef MICROPROFILE_DEBUG
#define MICROPROFILE_DEBUG 0
#endif


#define S g_MicroProfile

MicroProfile g_MicroProfile;
MicroProfileThreadLog*			g_MicroProfileGpuLog = 0;

#ifndef MP_THREAD_LOCAL
static pthread_key_t g_MicroProfileThreadLogKey;
static pthread_once_t g_MicroProfileThreadLogKeyOnce = PTHREAD_ONCE_INIT;
static void MicroProfileCreateThreadLogKey()
{
	pthread_key_create(&g_MicroProfileThreadLogKey, NULL);
}
#else
MP_THREAD_LOCAL MicroProfileThreadLog* g_MicroProfileThreadLog = 0;
#endif

static bool g_bUseLock = false; /// This is used because windows does not support using mutexes under dll init(which is where global initialization is handled)


MICROPROFILE_DEFINE(g_MicroProfileFlip, "MicroProfile", "MicroProfileFlip", 0x3355ee);
MICROPROFILE_DEFINE(g_MicroProfileThreadLoop, "MicroProfile", "ThreadLoop", 0x3355ee);
MICROPROFILE_DEFINE(g_MicroProfileClear, "MicroProfile", "Clear", 0x3355ee);
MICROPROFILE_DEFINE(g_MicroProfileAccumulate, "MicroProfile", "Accumulate", 0x3355ee);
MICROPROFILE_DEFINE(g_MicroProfileContextSwitchSearch,"MicroProfile", "ContextSwitchSearch", 0xDD7300);
MICROPROFILE_DEFINE(g_MicroProfileWebServerUpdate,"MicroProfile", "WebServerUpdate", 0xDD7300);

inline std::recursive_mutex& MicroProfileMutex()
{
	static std::recursive_mutex Mutex;
	return Mutex;
}
std::recursive_mutex& MicroProfileGetMutex()
{
	return MicroProfileMutex();
}

MICROPROFILE_API MicroProfile* MicroProfileGet()
{
	return &g_MicroProfile;
}


MicroProfileThreadLog* MicroProfileCreateThreadLog(const char* pName);


void MicroProfileInit()
{
	std::recursive_mutex& mutex = MicroProfileMutex();
	bool bUseLock = g_bUseLock;
	if(bUseLock)
		mutex.lock();
	static bool bOnce = true;
	if(bOnce)
	{
		bOnce = false;
		memset(&S, 0, sizeof(S));
		S.nMemUsage = sizeof(S);
		for(int i = 0; i < MICROPROFILE_MAX_GROUPS; ++i)
		{
			S.GroupInfo[i].pName[0] = '\0';
		}
		for(int i = 0; i < MICROPROFILE_MAX_CATEGORIES; ++i)
		{
			S.CategoryInfo[i].pName[0] = '\0';
			S.CategoryInfo[i].nGroupMask = 0;
		}
		strcpy(&S.CategoryInfo[0].pName[0], "default");
		S.nCategoryCount = 1;
		for(int i = 0; i < MICROPROFILE_MAX_TIMERS; ++i)
		{
			S.TimerInfo[i].pName[0] = '\0';
		}
		S.nGroupCount = 0;
		S.nAggregateFlipTick = MP_TICK();
		S.nBars = MP_DRAW_AVERAGE | MP_DRAW_MAX | MP_DRAW_CALL_COUNT;
		S.nActiveGroup = 0;
		S.nActiveBars = 0;
		S.nForceGroup = 0;
		S.nAllGroupsWanted = 0;
		S.nActiveGroupWanted = 0;
		S.nAllThreadsWanted = 1;
		S.nAggregateFlip = 60;
		S.nTotalTimers = 0;
		for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
		{
			S.Graph[i].nToken = MICROPROFILE_INVALID_TOKEN;
		}
		S.nRunning = 1;
		S.fReferenceTime = 33.33f;
		S.fRcpReferenceTime = 1.f / S.fReferenceTime;
		S.nFreeListHead = -1;
		int64_t nTick = MP_TICK();
		for(int i = 0; i < MICROPROFILE_MAX_FRAME_HISTORY; ++i)
		{
			S.Frames[i].nFrameStartCpu = nTick;
			S.Frames[i].nFrameStartGpu = 0;
			S.Frames[i].nFrameStartGpuTimer = (uint32_t)-1;
		}

#if MICROPROFILE_COUNTER_HISTORY
		S.nCounterHistoryPut = 0;
		for(uint32_t i = 0; i < MICROPROFILE_MAX_COUNTERS; ++i)
		{
			S.nCounterMin[i] = 0x7fffffffffffffff;
			S.nCounterMax[i] = 0x8000000000000000;
		}
#endif

		S.nGpuFrameTimer = (uint32_t)-1;

		MicroProfileThreadLog* pGpu = MicroProfileCreateThreadLog("GPU");
		g_MicroProfileGpuLog = pGpu;
		MP_ASSERT(S.Pool[0] == pGpu);
		pGpu->nGpu = 1;
		pGpu->nThreadId = 0;
	}
	if(bUseLock)
		mutex.unlock();
}

void MicroProfileShutdown()
{
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
	MicroProfileWebServerStop();
	MicroProfileContextSwitchTraceStop();
	MicroProfileGpuShutdown();
}

#ifndef MP_THREAD_LOCAL
inline MicroProfileThreadLog* MicroProfileGetThreadLog()
{
	pthread_once(&g_MicroProfileThreadLogKeyOnce, MicroProfileCreateThreadLogKey);
	return (MicroProfileThreadLog*)pthread_getspecific(g_MicroProfileThreadLogKey);
}

inline void MicroProfileSetThreadLog(MicroProfileThreadLog* pLog)
{
	pthread_once(&g_MicroProfileThreadLogKeyOnce, MicroProfileCreateThreadLogKey);
	pthread_setspecific(g_MicroProfileThreadLogKey, pLog);
}
#else
MicroProfileThreadLog* MicroProfileGetThreadLog()
{
	return g_MicroProfileThreadLog;
}
inline void MicroProfileSetThreadLog(MicroProfileThreadLog* pLog)
{
	g_MicroProfileThreadLog = pLog;
}
#endif


MicroProfileThreadLog* MicroProfileCreateThreadLog(const char* pName)
{
	MicroProfileThreadLog* pLog = 0;
	uint32_t nLogIndex = 0;
	if(S.nFreeListHead != -1)
	{
		nLogIndex = S.nFreeListHead;
		pLog = S.Pool[nLogIndex];
		MP_ASSERT(pLog->nPut.load() == 0);
		MP_ASSERT(pLog->nGet.load() == 0);
		S.nFreeListHead = S.Pool[S.nFreeListHead]->nFreeListNext;
	}
	else if(S.nNumLogs == MICROPROFILE_MAX_THREADS)
	{
		return nullptr;
	}
	else
	{
		nLogIndex = S.nNumLogs;
		pLog = new MicroProfileThreadLog;
		S.nMemUsage += sizeof(MicroProfileThreadLog);
		S.Pool[S.nNumLogs++] = pLog;	
	}
	memset(pLog, 0, sizeof(*pLog));
	pLog->nLogIndex = nLogIndex;
	int len = (int)strlen(pName);
	int maxlen = sizeof(pLog->ThreadName)-1;
	len = len < maxlen ? len : maxlen;
	memcpy(&pLog->ThreadName[0], pName, len);
	pLog->ThreadName[len] = '\0';
	pLog->nThreadId = MP_GETCURRENTTHREADID();
	pLog->nFreeListNext = -1;
	pLog->nActive = 1;
	return pLog;
}

void MicroProfileOnThreadCreate(const char* pThreadName)
{
	g_bUseLock = true;
	MicroProfileInit();
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
	if(MicroProfileGetThreadLog() == 0)
	{
		MicroProfileThreadLog* pLog = MicroProfileCreateThreadLog(pThreadName ? pThreadName : MicroProfileGetThreadName());
		MP_ASSERT(pLog);
		MicroProfileSetThreadLog(pLog);
	}
}

void MicroProfileOnThreadExit()
{
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
	MicroProfileThreadLog* pLog = MicroProfileGetThreadLog();
	if(pLog)
	{
		int32_t nLogIndex = -1;
		for(int i = 0; i < MICROPROFILE_MAX_THREADS; ++i)
		{
			if(pLog == S.Pool[i])
			{
				nLogIndex = i;
				break;
			}
		}
		MP_ASSERT(nLogIndex < MICROPROFILE_MAX_THREADS && nLogIndex > 0);
		pLog->nFreeListNext = S.nFreeListHead;
		pLog->nActive = 0;
		pLog->nPut.store(0);
		pLog->nGet.store(0);
		pLog->nPutGpu.store(0);
		S.nFreeListHead = nLogIndex;
		for(int i = 0; i < MICROPROFILE_MAX_FRAME_HISTORY; ++i)
		{
			S.Frames[i].nLogStart[nLogIndex] = 0;
		}
		memset(pLog->nGroupStackPos, 0, sizeof(pLog->nGroupStackPos));
		memset(pLog->nGroupTicks, 0, sizeof(pLog->nGroupTicks));

		if(pLog->Log)
		{
			delete[] pLog->Log;
			pLog->Log = 0;
			S.nMemUsage -= sizeof(MicroProfileLogEntry) * MICROPROFILE_BUFFER_SIZE;
		}

		if(pLog->LogGpu)
		{
			delete[] pLog->LogGpu;
			pLog->LogGpu = 0;
			S.nMemUsage -= sizeof(MicroProfileLogEntry) * MICROPROFILE_GPU_BUFFER_SIZE;
		}

		MicroProfileSetThreadLog(0);
	}
}

MicroProfileThreadLog* MicroProfileGetOrCreateThreadLog()
{
	MicroProfileThreadLog* pLog = MicroProfileGetThreadLog();

	if (!pLog)
	{
		MicroProfileOnThreadCreate(nullptr);
		pLog = MicroProfileGetThreadLog();
	}

	return pLog;
}

struct MicroProfileScopeLock
{
	bool bUseLock;
	std::recursive_mutex& m;
	MicroProfileScopeLock(std::recursive_mutex& m) : bUseLock(g_bUseLock), m(m)
	{
		if(bUseLock)
			m.lock();
	}
	~MicroProfileScopeLock()
	{
		if(bUseLock)
			m.unlock();
	}
};

MicroProfileToken MicroProfileFindToken(const char* pGroup, const char* pName)
{
	MicroProfileInit();
	MicroProfileScopeLock L(MicroProfileMutex());
	for(uint32_t i = 0; i < S.nTotalTimers; ++i)
	{
		if(!MP_STRCASECMP(pName, S.TimerInfo[i].pName) && !MP_STRCASECMP(pGroup, S.GroupInfo[S.TimerToGroup[i]].pName))
		{
			return S.TimerInfo[i].nToken;
		}
	}
	return MICROPROFILE_INVALID_TOKEN;
}

uint16_t MicroProfileGetGroup(const char* pGroup, MicroProfileTokenType Type)
{
	for(uint32_t i = 0; i < S.nGroupCount; ++i)
	{
		if(!MP_STRCASECMP(pGroup, S.GroupInfo[i].pName))
		{
			return i;
		}
	}

	uint16_t nGroupIndex = S.nGroupCount++;
	MP_ASSERT(nGroupIndex < MICROPROFILE_MAX_GROUPS);

	size_t nLen = strlen(pGroup);
	if(nLen > MICROPROFILE_NAME_MAX_LEN-1)
		nLen = MICROPROFILE_NAME_MAX_LEN-1;
	memcpy(&S.GroupInfo[nGroupIndex].pName[0], pGroup, nLen);
	S.GroupInfo[nGroupIndex].pName[nLen] = '\0';
	S.GroupInfo[nGroupIndex].nNameLen = (uint32_t)nLen;

	S.GroupInfo[nGroupIndex].nNumTimers = 0;
	S.GroupInfo[nGroupIndex].nGroupIndex = nGroupIndex;
	S.GroupInfo[nGroupIndex].Type = Type;
	S.GroupInfo[nGroupIndex].nMaxTimerNameLen = 0;
	S.GroupInfo[nGroupIndex].nColor = 0x88888888;
	S.GroupInfo[nGroupIndex].nCategory = 0;

	S.CategoryInfo[0].nGroupMask |= 1ll << nGroupIndex;
	S.nGroupMask |= 1ll << nGroupIndex;
	S.nGroupMaskGpu |= uint64_t(Type == MicroProfileTokenTypeGpu) << nGroupIndex;

	if ((S.nRunning || S.nForceEnable) && S.nAllGroupsWanted)
		S.nActiveGroup |= 1ll << nGroupIndex;

	return nGroupIndex;
}

MicroProfileToken MicroProfileGetToken(const char* pGroup, const char* pName, uint32_t nColor, MicroProfileTokenType Type)
{
	MicroProfileInit();
	MicroProfileScopeLock L(MicroProfileMutex());
	MicroProfileToken ret = MicroProfileFindToken(pGroup, pName);
	if(ret != MICROPROFILE_INVALID_TOKEN)
		return ret;
	if(S.nTotalTimers == MICROPROFILE_MAX_TIMERS)
		return MICROPROFILE_INVALID_TOKEN;
	uint16_t nGroupIndex = MicroProfileGetGroup(pGroup, Type);
	uint16_t nTimerIndex = (uint16_t)(S.nTotalTimers++);
	uint64_t nGroupMask = 1ll << nGroupIndex;
	MicroProfileToken nToken = MicroProfileMakeToken(nGroupMask, nTimerIndex);
	S.GroupInfo[nGroupIndex].nNumTimers++;
	S.GroupInfo[nGroupIndex].nMaxTimerNameLen = MicroProfileMax(S.GroupInfo[nGroupIndex].nMaxTimerNameLen, (uint32_t)strlen(pName));
	MP_ASSERT(S.GroupInfo[nGroupIndex].Type == Type); //dont mix cpu & gpu timers in the same group
	S.nMaxGroupSize = MicroProfileMax(S.nMaxGroupSize, S.GroupInfo[nGroupIndex].nNumTimers);
	S.TimerInfo[nTimerIndex].nToken = nToken;
	uint32_t nLen = (uint32_t)strlen(pName);
	if(nLen > MICROPROFILE_NAME_MAX_LEN-1)
		nLen = MICROPROFILE_NAME_MAX_LEN-1;
	memcpy(&S.TimerInfo[nTimerIndex].pName, pName, nLen);

	if(nColor == 0xffffffff)
	{
		// http://www.two4u.com/color/small-txt.html with some omissions
		static const int kDebugColors[] =
		{
			0x70DB93, 0xB5A642, 0x5F9F9F, 0xB87333, 0x4F6F4F, 0x9932CD,
			0x871F78, 0x855E42, 0x545454, 0x8E2323, 0x238E23, 0xCD7F32,
			0xDBDB70, 0x527F76, 0x9F9F5F, 0x8E236B, 0xFF2F4F, 0xCFB53B,
			0xFF7F00, 0xDB70DB, 0x5959AB, 0x8C1717, 0x238E68, 0x6B4226,
			0x8E6B23, 0x007FFF, 0x00FF7F, 0x236B8E, 0x38B0DE, 0xDB9370,
			0xCC3299, 0x99CC32,
		};

		// djb2
		unsigned int result = 5381;
		for (const char* i = pGroup; *i; ++i)
			result = result * 33 ^ *i;
		for (const char* i = pName; *i; ++i)
			result = result * 33 ^ *i;

		nColor = kDebugColors[result % (sizeof(kDebugColors) / sizeof(kDebugColors[0]))];
	}

	S.TimerInfo[nTimerIndex].pName[nLen] = '\0';
	S.TimerInfo[nTimerIndex].nNameLen = nLen;
	S.TimerInfo[nTimerIndex].nColor = nColor&0xffffff;
	S.TimerInfo[nTimerIndex].nGroupIndex = nGroupIndex;
	S.TimerInfo[nTimerIndex].nTimerIndex = nTimerIndex;
	S.TimerToGroup[nTimerIndex] = nGroupIndex;
	return nToken;
}

MicroProfileToken MicroProfileGetLabelToken(const char* pGroup, MicroProfileTokenType Type)
{
	MicroProfileInit();
	MicroProfileScopeLock L(MicroProfileMutex());

	uint16_t nGroupIndex = MicroProfileGetGroup(pGroup, Type);
	uint64_t nGroupMask = 1ll << nGroupIndex;
	MicroProfileToken nToken = MicroProfileMakeToken(nGroupMask, 0);

	return nToken;
}

MicroProfileToken MicroProfileGetMetaToken(const char* pName)
{
	MicroProfileInit();
	MicroProfileScopeLock L(MicroProfileMutex());
	for(uint32_t i = 0; i < MICROPROFILE_META_MAX; ++i)
	{
		if(!S.MetaCounters[i].pName)
		{
			S.MetaCounters[i].pName = pName;
			return i;
		}
		else if(!MP_STRCASECMP(pName, S.MetaCounters[i].pName))
		{
			return i;
		}
	}
	MP_ASSERT(0);//out of slots, increase MICROPROFILE_META_MAX
	return (MicroProfileToken)-1;
}

const char* MicroProfileNextName(const char* pName, char* pNameOut, uint32_t* nSubNameLen)
{
	uint32_t nMaxLen = MICROPROFILE_NAME_MAX_LEN-1;
	const char* pRet = 0;
	bool bDone = false;
	uint32_t nChars = 0;
	for(uint32_t i = 0; i < nMaxLen && !bDone; ++i)
	{
		char c = *pName++;
		switch(c)
		{
			case 0:
				bDone = true;
				break;
			case '\\':
			case '/':
				if(nChars)
				{
					bDone = true;
					pRet = pName;
				}
				break;
			default:
				nChars++;
				*pNameOut++ = c;
		}
	}
	*nSubNameLen = nChars;
	*pNameOut = '\0';
	return pRet;
}


const char* MicroProfileCounterFullName(int nCounter)
{
	static char Buffer[1024];
	int nNodes[32];
	int nIndex = 0;
	do
	{
		nNodes[nIndex++] = nCounter;
		nCounter = S.CounterInfo[nCounter].nParent;
	}while(nCounter >= 0);
	int nOffset = 0;
	while(nIndex >= 0 && nOffset < (int)sizeof(Buffer)-2)
	{
		uint32_t nLen = S.CounterInfo[nNodes[nIndex]].nNameLen + nOffset;// < sizeof(Buffer)-1 
		nLen = MicroProfileMin((uint32_t)(sizeof(Buffer) - 2 - nOffset), nLen);
		memcpy(&Buffer[nOffset], S.CounterInfo[nNodes[nIndex]].pName, nLen);

		nOffset += S.CounterInfo[nNodes[nIndex]].nNameLen+1;
		if(nIndex)
		{
			Buffer[nOffset++] = '/';
		}
		nIndex--;
	}
	return &Buffer[0];
}

int MicroProfileGetCounterTokenByParent(int nParent, const char* pName)
{
	for(uint32_t i = 0; i < S.nNumCounters; ++i)
	{
		if(nParent == S.CounterInfo[i].nParent && !MP_STRCASECMP(S.CounterInfo[i].pName, pName))
		{
			return i;
		}
	}
	MicroProfileToken nResult = S.nNumCounters++;
	S.CounterInfo[nResult].nParent = nParent;
	S.CounterInfo[nResult].nSibling = -1;
	S.CounterInfo[nResult].nFirstChild = -1;
	S.CounterInfo[nResult].nFlags = 0;
	S.CounterInfo[nResult].eFormat = MICROPROFILE_COUNTER_FORMAT_DEFAULT;
	S.CounterInfo[nResult].nLimit = 0;
	int nLen = (int)strlen(pName)+1;

	MP_ASSERT(nLen + S.nCounterNamePos <= MICROPROFILE_MAX_COUNTER_NAME_CHARS);
	uint32_t nPos = S.nCounterNamePos;
	S.nCounterNamePos += nLen;
	memcpy(&S.CounterNames[nPos], pName, nLen);
	S.CounterInfo[nResult].nNameLen = nLen-1;
	S.CounterInfo[nResult].pName = &S.CounterNames[nPos];
	if(nParent >= 0)
	{
		S.CounterInfo[nResult].nSibling = S.CounterInfo[nParent].nFirstChild;
		S.CounterInfo[nResult].nLevel = S.CounterInfo[nParent].nLevel + 1;
		S.CounterInfo[nParent].nFirstChild = nResult;
	}
	else
	{
		S.CounterInfo[nResult].nLevel = 0;
	}

	return nResult;
}

MicroProfileToken MicroProfileGetCounterToken(const char* pName)
{
	MicroProfileInit();
	MicroProfileScopeLock L(MicroProfileMutex());
	char SubName[MICROPROFILE_NAME_MAX_LEN];
	int nResult = -1;
	do
	{
		uint32_t nLen = 0;
		pName = MicroProfileNextName(pName, &SubName[0], &nLen);
		if(0 == nLen)
		{
			break;
		}
		nResult = MicroProfileGetCounterTokenByParent(nResult, SubName);

	}while(pName != 0);
	S.CounterInfo[nResult].nFlags |= MICROPROFILE_COUNTER_FLAG_LEAF;

	MP_ASSERT(nResult >= 0);
	return nResult;
}

inline void MicroProfileLogPut(MicroProfileToken nToken_, uint64_t nTick, uint64_t nBegin, MicroProfileThreadLog* pLog)
{
	MP_ASSERT(pLog != 0); //this assert is hit if MicroProfileOnCreateThread is not called
	MP_ASSERT(pLog->nActive);
	uint32_t nPos = pLog->nPut.load(std::memory_order_relaxed);
	uint32_t nNextPos = (nPos+1) % MICROPROFILE_BUFFER_SIZE;
	if(nNextPos == pLog->nGet.load(std::memory_order_relaxed))
	{
		S.nOverflow = 100;
	}
	else
	{
		if(!pLog->Log)
		{
			pLog->Log = new MicroProfileLogEntry[MICROPROFILE_BUFFER_SIZE];
			memset(pLog->Log, 0, sizeof(MicroProfileLogEntry) * MICROPROFILE_BUFFER_SIZE);
			S.nMemUsage += sizeof(MicroProfileLogEntry) * MICROPROFILE_BUFFER_SIZE;
		}
		pLog->Log[nPos] = MicroProfileMakeLogIndex(nBegin, nToken_, nTick);
		pLog->nPut.store(nNextPos, std::memory_order_release);
	}
}

inline void MicroProfileLogPutGpu(MicroProfileToken nToken_, uint64_t nTick, uint64_t nBegin, MicroProfileThreadLog* pLog)
{
#if MICROPROFILE_GPU_TIMERS_MULTITHREADED
	MP_ASSERT(pLog != 0); //this assert is hit if MicroProfileOnCreateThread is not called
	MP_ASSERT(pLog->nActive);
	uint32_t nPos = pLog->nPutGpu.load(std::memory_order_relaxed);
	if(nPos >= MICROPROFILE_GPU_BUFFER_SIZE)
	{
		S.nOverflow = 100;
	}
	else
	{
		if(!pLog->LogGpu)
		{
			pLog->LogGpu = new MicroProfileLogEntry[MICROPROFILE_GPU_BUFFER_SIZE];
			memset(pLog->LogGpu, 0, sizeof(MicroProfileLogEntry) * MICROPROFILE_GPU_BUFFER_SIZE);
			S.nMemUsage += sizeof(MicroProfileLogEntry) * MICROPROFILE_GPU_BUFFER_SIZE;
		}
		pLog->LogGpu[nPos] = MicroProfileMakeLogIndex(nBegin, nToken_, nTick);
		pLog->nPutGpu.store(nPos + 1, std::memory_order_release);
	}
#else
	(void)pLog;

	MicroProfileLogPut(nToken_, nTick, nBegin, g_MicroProfileGpuLog);
#endif
}

uint64_t MicroProfileEnter(MicroProfileToken nToken_)
{
	uint64_t nGroupMask = MicroProfileGetGroupMask(nToken_);
	if(nGroupMask & S.nActiveGroup)
	{
		if (MicroProfileThreadLog* pLog = MicroProfileGetOrCreateThreadLog())
		{
			if (nGroupMask & S.nGroupMaskGpu)
			{
				uint32_t nTimer = MicroProfileGpuInsertTimer(pLog->pContextGpu);
				if (nTimer != (uint32_t)-1)
				{
					MicroProfileLogPutGpu(nToken_, nTimer, MP_LOG_ENTER, pLog);
					MicroProfileLogPutGpu(pLog->nLogIndex, MP_TICK(), MP_LOG_GPU_EXTRA, pLog);
					return 0;
				}
			}
			else
			{
				uint64_t nTick = MP_TICK();
				MicroProfileLogPut(nToken_, nTick, MP_LOG_ENTER, pLog);
				return nTick;
			}
		}
	}
	return MICROPROFILE_INVALID_TICK;
}

uint64_t MicroProfileAllocateLabel(const char* pName)
{
	char* pLabelBuffer = S.LabelBuffer.load(std::memory_order_consume);
	if(!pLabelBuffer)
	{
		MicroProfileScopeLock L(MicroProfileMutex());

		pLabelBuffer = S.LabelBuffer.load(std::memory_order_consume);
		if(!pLabelBuffer)
		{
			pLabelBuffer = new char[MICROPROFILE_LABEL_BUFFER_SIZE + MICROPROFILE_LABEL_MAX_LEN];
			memset(pLabelBuffer, 0, MICROPROFILE_LABEL_BUFFER_SIZE + MICROPROFILE_LABEL_MAX_LEN);
			S.nMemUsage += MICROPROFILE_LABEL_BUFFER_SIZE + MICROPROFILE_LABEL_MAX_LEN;

			S.LabelBuffer.store(pLabelBuffer, std::memory_order_release);
		}
	}

	size_t nLen = strlen(pName);

	if(nLen > MICROPROFILE_LABEL_MAX_LEN - 1)
		nLen = MICROPROFILE_LABEL_MAX_LEN - 1;

	uint64_t nLabel = S.nLabelPut.fetch_add(nLen + 1, std::memory_order_relaxed);
	char* pLabel = &pLabelBuffer[nLabel % MICROPROFILE_LABEL_BUFFER_SIZE];

	memcpy(pLabel, pName, nLen);
	pLabel[nLen] = 0;

	return nLabel;
}

void MicroProfilePutLabel(MicroProfileToken nToken_, const char* pName)
{
	if (MicroProfileThreadLog* pLog = MicroProfileGetThreadLog())
	{
		uint64_t nLabel = MicroProfileAllocateLabel(pName);
		uint64_t nGroupMask = MicroProfileGetGroupMask(nToken_);

		if (nGroupMask & S.nGroupMaskGpu)
			MicroProfileLogPutGpu(nToken_, nLabel, MP_LOG_LABEL, pLog);
		else
			MicroProfileLogPut(nToken_, nLabel, MP_LOG_LABEL, pLog);
	}
}

void MicroProfileCounterAdd(MicroProfileToken nToken, int64_t nCount)
{
	MP_ASSERT(nToken < S.nNumCounters);
	S.Counters[nToken].fetch_add(nCount);
}
void MicroProfileCounterSet(MicroProfileToken nToken, int64_t nCount)
{
	MP_ASSERT(nToken < S.nNumCounters);
	S.Counters[nToken].store(nCount);
}
void MicroProfileCounterSetLimit(MicroProfileToken nToken, int64_t nCount)
{
	MP_ASSERT(nToken < S.nNumCounters);
	S.CounterInfo[nToken].nLimit = nCount;
}

void MicroProfileCounterConfig(const char* pName, uint32_t eFormat, int64_t nLimit, uint32_t nFlags)
{
	MicroProfileToken nToken = MicroProfileGetCounterToken(pName);
	S.CounterInfo[nToken].eFormat = (MicroProfileCounterFormat)eFormat;
	S.CounterInfo[nToken].nLimit = nLimit;
	S.CounterInfo[nToken].nFlags |= (nFlags & ~MICROPROFILE_COUNTER_FLAG_INTERNAL_MASK);
}

const char* MicroProfileGetLabel(uint64_t nLabel)
{
	char* pLabelBuffer = S.LabelBuffer.load(std::memory_order_relaxed);
	uint64_t nLabelPut = S.nLabelPut.load(std::memory_order_relaxed);

	MP_ASSERT(pLabelBuffer && nLabel < nLabelPut);

	if (nLabelPut - nLabel > MICROPROFILE_LABEL_BUFFER_SIZE)
		return 0;
	else
		return &pLabelBuffer[nLabel % MICROPROFILE_LABEL_BUFFER_SIZE];
}

void MicroProfileLabel(MicroProfileToken nToken_, const char* pName)
{
	if(MicroProfileGetGroupMask(nToken_) & S.nActiveGroup)
	{
		MicroProfilePutLabel(nToken_, pName);
	}
}

void MicroProfileLabelFormat(MicroProfileToken nToken_, const char* pName, ...)
{
	va_list args;
	va_start(args, pName);
	MicroProfileLabelFormatV(nToken_, pName, args);
	va_end(args);
}

void MicroProfileLabelFormatV(MicroProfileToken nToken_, const char* pName, va_list args)
{
	if(MicroProfileGetGroupMask(nToken_) & S.nActiveGroup)
	{
		char buffer[MICROPROFILE_LABEL_MAX_LEN];
		vsnprintf(buffer, sizeof(buffer)-1, pName, args);

		buffer[sizeof(buffer)-1] = 0;

		MicroProfilePutLabel(nToken_, buffer);
	}
}

void MicroProfileMetaUpdate(MicroProfileToken nToken, int nCount, MicroProfileTokenType eTokenType)
{
	if((MP_DRAW_META_FIRST<<nToken) & S.nActiveBars)
	{
		if (MicroProfileThreadLog* pLog = MicroProfileGetThreadLog())
		{
			MP_ASSERT(nToken < MICROPROFILE_META_MAX);

			if (MicroProfileTokenTypeGpu == eTokenType)
				MicroProfileLogPutGpu(nToken, nCount, MP_LOG_META, pLog);
			else
				MicroProfileLogPut(nToken, nCount, MP_LOG_META, pLog);
		}
	}
}

void MicroProfileLeave(MicroProfileToken nToken_, uint64_t nTickStart)
{
	if(MICROPROFILE_INVALID_TICK != nTickStart)
	{
		if (MicroProfileThreadLog* pLog = MicroProfileGetOrCreateThreadLog())
		{
			uint64_t nGroupMask = MicroProfileGetGroupMask(nToken_);

			if (nGroupMask & S.nGroupMaskGpu)
			{
				uint32_t nTimer = MicroProfileGpuInsertTimer(pLog->pContextGpu);
				MicroProfileLogPutGpu(nToken_, nTimer, MP_LOG_LEAVE, pLog);
				MicroProfileLogPutGpu(pLog->nLogIndex, MP_TICK(), MP_LOG_GPU_EXTRA, pLog);
			}
			else
			{
				uint64_t nTick = MP_TICK();
				MicroProfileLogPut(nToken_, nTick, MP_LOG_LEAVE, pLog);
			}
		}
	}
}

void MicroProfileContextSwitchPut(MicroProfileContextSwitch* pContextSwitch)
{
	if(S.nRunning || pContextSwitch->nTicks <= S.nPauseTicks)
	{
		uint32_t nPut = S.nContextSwitchPut;
		S.ContextSwitch[nPut] = *pContextSwitch;
		S.nContextSwitchPut = (S.nContextSwitchPut+1) % MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE;
	}
}


void MicroProfileGetRange(uint32_t nPut, uint32_t nGet, uint32_t nRange[2][2])
{
	if(nPut > nGet)
	{
		nRange[0][0] = nGet;
		nRange[0][1] = nPut;
		nRange[1][0] = nRange[1][1] = 0;
	}
	else if(nPut != nGet)
	{
		MP_ASSERT(nGet != MICROPROFILE_BUFFER_SIZE);
		uint32_t nCountEnd = MICROPROFILE_BUFFER_SIZE - nGet;
		nRange[0][0] = nGet;
		nRange[0][1] = nGet + nCountEnd;
		nRange[1][0] = 0;
		nRange[1][1] = nPut;
	}
}

void MicroProfileDumpToFile();

void MicroProfileFlipGpu()
{
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());

	for (uint32_t i = 0; i < MICROPROFILE_MAX_THREADS; ++i)
	{
		if (S.Pool[i])
		{
			MP_ASSERT(!S.Pool[i]->bActiveGpu);

			S.Pool[i]->nPutGpu.store(0);
		}
	}

	S.nGpuFrameTimer = MicroProfileGpuFlip();
}

void MicroProfileFlipCpu()
{
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());


	if(S.nToggleRunning)
	{
		S.nRunning = !S.nRunning;
		if(!S.nRunning)
			S.nPauseTicks = MP_TICK();
		S.nToggleRunning = 0;
		for(uint32_t i = 0; i < MICROPROFILE_MAX_THREADS; ++i)
		{
			MicroProfileThreadLog* pLog = S.Pool[i];
			if(pLog)
			{
				pLog->nStackPos = 0;
			}
		}
	}
	uint32_t nAggregateClear = S.nAggregateClear || S.nAutoClearFrames, nAggregateFlip = 0;
	if(S.nDumpFileNextFrame)
	{
		MicroProfileDumpToFile();
		S.nDumpFileNextFrame = 0;
		S.nAutoClearFrames = MICROPROFILE_GPU_FRAME_DELAY + 3; //hide spike from dumping webpage
	}

	if(S.nAutoClearFrames)
	{
		nAggregateClear = 1;
		nAggregateFlip = 1;
		S.nAutoClearFrames -= 1;
	}


	if(S.nRunning || S.nForceEnable)
	{
		S.nFramePutIndex++;
		S.nFramePut = (S.nFramePut+1) % MICROPROFILE_MAX_FRAME_HISTORY;
		MP_ASSERT((S.nFramePutIndex % MICROPROFILE_MAX_FRAME_HISTORY) == S.nFramePut);
		S.nFrameCurrent = (S.nFramePut + MICROPROFILE_MAX_FRAME_HISTORY - MICROPROFILE_GPU_FRAME_DELAY - 1) % MICROPROFILE_MAX_FRAME_HISTORY;
		S.nFrameCurrentIndex++;
		uint32_t nFrameNext = (S.nFrameCurrent+1) % MICROPROFILE_MAX_FRAME_HISTORY;

		uint32_t nContextSwitchPut = S.nContextSwitchPut;
		if(S.nContextSwitchLastPut < nContextSwitchPut)
		{
			S.nContextSwitchUsage = (nContextSwitchPut - S.nContextSwitchLastPut);
		}
		else
		{
			S.nContextSwitchUsage = MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE - S.nContextSwitchLastPut + nContextSwitchPut;
		}
		S.nContextSwitchLastPut = nContextSwitchPut;

		MicroProfileFrameState* pFramePut = &S.Frames[S.nFramePut];
		MicroProfileFrameState* pFrameCurrent = &S.Frames[S.nFrameCurrent];
		MicroProfileFrameState* pFrameNext = &S.Frames[nFrameNext];
		
		pFramePut->nFrameStartCpu = MP_TICK();
		pFramePut->nFrameStartGpuTimer = S.nGpuFrameTimer;

		if(pFrameCurrent->nFrameStartGpuTimer != (uint32_t)-1)
		{
			uint64_t nTick = MicroProfileGpuGetTimeStamp(pFrameCurrent->nFrameStartGpuTimer);

			pFrameCurrent->nFrameStartGpu = (nTick == MICROPROFILE_INVALID_TICK) ? 0 : nTick;
		}

		uint64_t nFrameStartCpu = pFrameCurrent->nFrameStartCpu;
		uint64_t nFrameEndCpu = pFrameNext->nFrameStartCpu;

		{
			uint64_t nTick = nFrameEndCpu - nFrameStartCpu;
			S.nFlipTicks = nTick;
			S.nFlipAggregate += nTick;
			S.nFlipMax = MicroProfileMax(S.nFlipMax, nTick);
		}

		uint8_t* pTimerToGroup = &S.TimerToGroup[0];
		for(uint32_t i = 0; i < MICROPROFILE_MAX_THREADS; ++i)
		{
			MicroProfileThreadLog* pLog = S.Pool[i];
			if(!pLog)
			{
				pFramePut->nLogStart[i] = 0;
			}
			else
			{
				uint32_t nPut = pLog->nPut.load(std::memory_order_acquire);
				pFramePut->nLogStart[i] = nPut;
				MP_ASSERT(nPut< MICROPROFILE_BUFFER_SIZE);
				//need to keep last frame around to close timers. timers more than 1 frame old is ditched.
				pLog->nGet.store(nPut, std::memory_order_relaxed);
			}
		}

		if(S.nRunning)
		{
			uint64_t* pFrameGroup = &S.FrameGroup[0];
			{
				MICROPROFILE_SCOPE(g_MicroProfileClear);
				for(uint32_t i = 0; i < S.nTotalTimers; ++i)
				{
					S.Frame[i].nTicks = 0;
					S.Frame[i].nCount = 0;
					S.FrameExclusive[i] = 0;
				}
				for(uint32_t i = 0; i < MICROPROFILE_MAX_GROUPS; ++i)
				{
					pFrameGroup[i] = 0;
				}
				for(uint32_t j = 0; j < MICROPROFILE_META_MAX; ++j)
				{
					if(S.MetaCounters[j].pName && 0 != (S.nActiveBars & (MP_DRAW_META_FIRST<<j)))
					{
						auto& Meta = S.MetaCounters[j];
						for(uint32_t i = 0; i < S.nTotalTimers; ++i)
						{
							Meta.nCounters[i] = 0;
						}
					}
				}

			}
			{
				MICROPROFILE_SCOPE(g_MicroProfileThreadLoop);
				for(uint32_t i = 0; i < MICROPROFILE_MAX_THREADS; ++i)
				{
					MicroProfileThreadLog* pLog = S.Pool[i];
					if(!pLog) 
						continue;

					uint8_t* pGroupStackPos = &pLog->nGroupStackPos[0];
					int64_t nGroupTicks[MICROPROFILE_MAX_GROUPS] = {0};


					uint32_t nPut = pFrameNext->nLogStart[i];
					uint32_t nGet = pFrameCurrent->nLogStart[i];
					uint32_t nRange[2][2] = { {0, 0}, {0, 0}, };
					MicroProfileGetRange(nPut, nGet, nRange);


					//fetch gpu results.
					if(pLog->nGpu)
					{
						uint64_t nLastTick = pFrameCurrent->nFrameStartGpu;

						for(uint32_t j = 0; j < 2; ++j)
						{
							uint32_t nStart = nRange[j][0];
							uint32_t nEnd = nRange[j][1];
							for(uint32_t k = nStart; k < nEnd; ++k)
							{
								MicroProfileLogEntry L = pLog->Log[k];

								int Type = MicroProfileLogType(L);

								if(Type == MP_LOG_ENTER || Type == MP_LOG_LEAVE)
								{
									uint32_t nTimer = MicroProfileLogGetTick(L);
									uint64_t nTick = MicroProfileGpuGetTimeStamp(nTimer);

									if(nTick != MICROPROFILE_INVALID_TICK)
										nLastTick = nTick;

									pLog->Log[k] = MicroProfileLogSetTick(L, nLastTick);
								}
							}
						}
					}
					
					
					uint32_t* pStack = &pLog->nStack[0];
					int64_t* pChildTickStack = &pLog->nChildTickStack[0];
					uint32_t nStackPos = pLog->nStackPos;

					for(uint32_t j = 0; j < 2; ++j)
					{
						uint32_t nStart = nRange[j][0];
						uint32_t nEnd = nRange[j][1];
						for(uint32_t k = nStart; k < nEnd; ++k)
						{
							MicroProfileLogEntry LE = pLog->Log[k];
							uint64_t nType = MicroProfileLogType(LE);

							if(MP_LOG_ENTER == nType)
							{
								int nTimer = MicroProfileLogTimerIndex(LE);
								uint8_t nGroup = pTimerToGroup[nTimer];
								MP_ASSERT(nStackPos < MICROPROFILE_STACK_MAX);
								MP_ASSERT(nGroup < MICROPROFILE_MAX_GROUPS);
								pGroupStackPos[nGroup]++;
								pStack[nStackPos++] = k;
								pChildTickStack[nStackPos] = 0;

							}
							else if(MP_LOG_META == nType)
							{
								if(nStackPos)
								{
									int64_t nMetaIndex = MicroProfileLogTimerIndex(LE);
									int64_t nMetaCount = MicroProfileLogGetTick(LE);
									MP_ASSERT(nMetaIndex < MICROPROFILE_META_MAX);
									int64_t nCounter = MicroProfileLogTimerIndex(pLog->Log[pStack[nStackPos-1]]);
									S.MetaCounters[nMetaIndex].nCounters[nCounter] += nMetaCount;
								}
							}
							else if(MP_LOG_LEAVE == nType)
							{
								int nTimer = MicroProfileLogTimerIndex(LE);
								uint8_t nGroup = pTimerToGroup[nTimer];
								MP_ASSERT(nGroup < MICROPROFILE_MAX_GROUPS);
								if(nStackPos)
								{									
									int64_t nTickStart = pLog->Log[pStack[nStackPos-1]];
									int64_t nTicks = MicroProfileLogTickDifference(nTickStart, LE);
									int64_t nChildTicks = pChildTickStack[nStackPos];
									nStackPos--;
									pChildTickStack[nStackPos] += nTicks;

									uint32_t nTimerIndex = MicroProfileLogTimerIndex(LE);
									S.Frame[nTimerIndex].nTicks += nTicks;
									S.FrameExclusive[nTimerIndex] += (nTicks-nChildTicks);
									S.Frame[nTimerIndex].nCount += 1;

									MP_ASSERT(nGroup < MICROPROFILE_MAX_GROUPS);
									uint8_t nGroupStackPos = pGroupStackPos[nGroup];
									if(nGroupStackPos)
									{
										nGroupStackPos--;
										if(0 == nGroupStackPos)
										{
											nGroupTicks[nGroup] += nTicks;
										}
										pGroupStackPos[nGroup] = nGroupStackPos;
									}
								}
							}
						}
					}
					for(uint32_t i = 0; i < MICROPROFILE_MAX_GROUPS; ++i)
					{
						pLog->nGroupTicks[i] += nGroupTicks[i];
						pFrameGroup[i] += nGroupTicks[i];
					}
					pLog->nStackPos = nStackPos;
				}
			}
			{
				MICROPROFILE_SCOPE(g_MicroProfileAccumulate);
				for(uint32_t i = 0; i < S.nTotalTimers; ++i)
				{
					S.AccumTimers[i].nTicks += S.Frame[i].nTicks;				
					S.AccumTimers[i].nCount += S.Frame[i].nCount;
					S.AccumMaxTimers[i] = MicroProfileMax(S.AccumMaxTimers[i], S.Frame[i].nTicks);
					S.AccumMinTimers[i] = MicroProfileMin(S.AccumMinTimers[i], S.Frame[i].nTicks);
					S.AccumTimersExclusive[i] += S.FrameExclusive[i];				
					S.AccumMaxTimersExclusive[i] = MicroProfileMax(S.AccumMaxTimersExclusive[i], S.FrameExclusive[i]);
				}

				for(uint32_t i = 0; i < MICROPROFILE_MAX_GROUPS; ++i)
				{
					S.AccumGroup[i] += pFrameGroup[i];
					S.AccumGroupMax[i] = MicroProfileMax(S.AccumGroupMax[i], pFrameGroup[i]);
				}

				for(uint32_t j = 0; j < MICROPROFILE_META_MAX; ++j)
				{
					if(S.MetaCounters[j].pName && 0 != (S.nActiveBars & (MP_DRAW_META_FIRST<<j)))
					{
						auto& Meta = S.MetaCounters[j];
						uint64_t nSum = 0;;
						for(uint32_t i = 0; i < S.nTotalTimers; ++i)
						{
							uint64_t nCounter = Meta.nCounters[i];
							Meta.nAccumMax[i] = MicroProfileMax(Meta.nAccumMax[i], nCounter);
							Meta.nAccum[i] += nCounter;
							nSum += nCounter;
						}
						Meta.nSumAccum += nSum;
						Meta.nSumAccumMax = MicroProfileMax(Meta.nSumAccumMax, nSum);
					}
				}			
			}
			for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
			{
				if(S.Graph[i].nToken != MICROPROFILE_INVALID_TOKEN)
				{
					MicroProfileToken nToken = S.Graph[i].nToken;
					S.Graph[i].nHistory[S.nGraphPut] = S.Frame[MicroProfileGetTimerIndex(nToken)].nTicks;
				}
			}
			S.nGraphPut = (S.nGraphPut+1) % MICROPROFILE_GRAPH_HISTORY;

		}


		if(S.nRunning && S.nAggregateFlip <= ++S.nAggregateFlipCount)
		{
			nAggregateFlip = 1;
			if(S.nAggregateFlip) // if 0 accumulate indefinitely
			{
				nAggregateClear = 1;
			}
		}
	}
	if(nAggregateFlip)
	{
		memcpy(&S.Aggregate[0], &S.AccumTimers[0], sizeof(S.Aggregate[0]) * S.nTotalTimers);
		memcpy(&S.AggregateMax[0], &S.AccumMaxTimers[0], sizeof(S.AggregateMax[0]) * S.nTotalTimers);
		memcpy(&S.AggregateMin[0], &S.AccumMinTimers[0], sizeof(S.AggregateMin[0]) * S.nTotalTimers);
		memcpy(&S.AggregateExclusive[0], &S.AccumTimersExclusive[0], sizeof(S.AggregateExclusive[0]) * S.nTotalTimers);
		memcpy(&S.AggregateMaxExclusive[0], &S.AccumMaxTimersExclusive[0], sizeof(S.AggregateMaxExclusive[0]) * S.nTotalTimers);

		memcpy(&S.AggregateGroup[0], &S.AccumGroup[0], sizeof(S.AggregateGroup));
		memcpy(&S.AggregateGroupMax[0], &S.AccumGroupMax[0], sizeof(S.AggregateGroup));		

		for(uint32_t i = 0; i < MICROPROFILE_MAX_THREADS; ++i)
		{
			MicroProfileThreadLog* pLog = S.Pool[i];
			if(!pLog) 
				continue;
			
			memcpy(&pLog->nAggregateGroupTicks[0], &pLog->nGroupTicks[0], sizeof(pLog->nAggregateGroupTicks));
			
			if(nAggregateClear)
			{
				memset(&pLog->nGroupTicks[0], 0, sizeof(pLog->nGroupTicks));
			}
		}

		for(uint32_t j = 0; j < MICROPROFILE_META_MAX; ++j)
		{
			if(S.MetaCounters[j].pName && 0 != (S.nActiveBars & (MP_DRAW_META_FIRST<<j)))
			{
				auto& Meta = S.MetaCounters[j];
				memcpy(&Meta.nAggregateMax[0], &Meta.nAccumMax[0], sizeof(Meta.nAggregateMax[0]) * S.nTotalTimers);
				memcpy(&Meta.nAggregate[0], &Meta.nAccum[0], sizeof(Meta.nAggregate[0]) * S.nTotalTimers);
				Meta.nSumAggregate = Meta.nSumAccum;
				Meta.nSumAggregateMax = Meta.nSumAccumMax;
				if(nAggregateClear)
				{
					memset(&Meta.nAccumMax[0], 0, sizeof(Meta.nAccumMax[0]) * S.nTotalTimers);
					memset(&Meta.nAccum[0], 0, sizeof(Meta.nAccum[0]) * S.nTotalTimers);
					Meta.nSumAccum = 0;
					Meta.nSumAccumMax = 0;
				}
			}
		}





		S.nAggregateFrames = S.nAggregateFlipCount;
		S.nFlipAggregateDisplay = S.nFlipAggregate;
		S.nFlipMaxDisplay = S.nFlipMax;
		if(nAggregateClear)
		{
			memset(&S.AccumTimers[0], 0, sizeof(S.Aggregate[0]) * S.nTotalTimers);
			memset(&S.AccumMaxTimers[0], 0, sizeof(S.AccumMaxTimers[0]) * S.nTotalTimers);
			memset(&S.AccumMinTimers[0], 0xFF, sizeof(S.AccumMinTimers[0]) * S.nTotalTimers);
			memset(&S.AccumTimersExclusive[0], 0, sizeof(S.AggregateExclusive[0]) * S.nTotalTimers);
			memset(&S.AccumMaxTimersExclusive[0], 0, sizeof(S.AccumMaxTimersExclusive[0]) * S.nTotalTimers);
			memset(&S.AccumGroup[0], 0, sizeof(S.AggregateGroup));
			memset(&S.AccumGroupMax[0], 0, sizeof(S.AggregateGroup));		

			S.nAggregateFlipCount = 0;
			S.nFlipAggregate = 0;
			S.nFlipMax = 0;

			S.nAggregateFlipTick = MP_TICK();
		}

		#if MICROPROFILE_COUNTER_HISTORY
		int64_t* pDest = &S.nCounterHistory[S.nCounterHistoryPut][0];
		S.nCounterHistoryPut = (S.nCounterHistoryPut+1) % MICROPROFILE_GRAPH_HISTORY;
		for(uint32_t i = 0; i < S.nNumCounters; ++i)
		{
			if(0 != (S.CounterInfo[i].nFlags & MICROPROFILE_COUNTER_FLAG_DETAILED))
			{
				uint64_t nValue = S.Counters[i].load(std::memory_order_relaxed);
				pDest[i] = nValue;
				S.nCounterMin[i] = MicroProfileMin(S.nCounterMin[i], (int64_t)nValue);
				S.nCounterMax[i] = MicroProfileMax(S.nCounterMax[i], (int64_t)nValue);
			}
		}
		#endif
	}
	S.nAggregateClear = 0;

	uint64_t nNewActiveGroup = 0;
	if (S.nRunning || S.nForceEnable)
		nNewActiveGroup = S.nAllGroupsWanted ? S.nGroupMask : S.nActiveGroupWanted;
	nNewActiveGroup |= S.nForceGroup;
	nNewActiveGroup |= S.nForceGroupUI;
	if(S.nActiveGroup != nNewActiveGroup)
		S.nActiveGroup = nNewActiveGroup;

	uint32_t nNewActiveBars = 0;
	if (S.nRunning || S.nForceEnable)
		nNewActiveBars = S.nBars;
	if(S.nForceMetaCounters)
	{
		for(int i = 0; i < MICROPROFILE_META_MAX; ++i)
		{
			if(S.MetaCounters[i].pName)
			{
				nNewActiveBars |= (MP_DRAW_META_FIRST<<i);
			}
		}
	}
	if(nNewActiveBars != S.nActiveBars)
		S.nActiveBars = nNewActiveBars;
}

void MicroProfileFlip()
{
	MICROPROFILE_SCOPE(g_MicroProfileFlip);

	MicroProfileFlipGpu();
	MicroProfileFlipCpu();
}

void MicroProfileGpuSetContext(void* pContext)
{
	if(MicroProfileThreadLog* pLog = MicroProfileGetOrCreateThreadLog())
	{
		pLog->pContextGpu = pContext;
	}
}

void MicroProfileGpuBegin(void* pContext)
{
	if(MicroProfileThreadLog* pLog = MicroProfileGetOrCreateThreadLog())
	{
		MP_ASSERT(!pLog->bActiveGpu);

		pLog->pContextGpu = pContext;
		pLog->nStartGpu = pLog->nPutGpu.load();
		pLog->bActiveGpu = 1;
	}
}

uint64_t MicroProfileGpuEnd()
{
	if(MicroProfileThreadLog* pLog = MicroProfileGetThreadLog())
	{
		MP_ASSERT(pLog->bActiveGpu);

		uint32_t nStartGpu = pLog->nStartGpu;
		uint32_t nPutGpu = pLog->nPutGpu.load();
		MP_ASSERT(nPutGpu >= nStartGpu);

		pLog->pContextGpu = 0;
		pLog->nStartGpu = nPutGpu;
		pLog->bActiveGpu = 0;

		MP_ASSERT(MICROPROFILE_GPU_BUFFER_SIZE <= 1 << 24);

		return (uint64_t(pLog->nLogIndex) << 48) | (uint64_t(nStartGpu) << 24) | uint64_t(nPutGpu);
	}
	return 0;
}

void MicroProfileGpuSubmit(uint64_t nWork)
{
	if (!nWork)
		return;

	uint32_t nLogIndex = (nWork >> 48) & 0xffff;
	uint32_t nStart = (nWork >> 24) & 0xffffff;
	uint32_t nEnd = nWork & 0xffffff;

	MP_ASSERT(nLogIndex < MICROPROFILE_MAX_THREADS);
	MP_ASSERT(nStart <= nEnd);

	MicroProfileThreadLog* pLog = S.Pool[nLogIndex];
	if (!pLog)
		return;

	MP_ASSERT(nEnd <= pLog->nStartGpu);

	for (uint32_t i = nStart; i < nEnd; ++i)
	{
		MicroProfileLogEntry LE = pLog->LogGpu[i];

		MicroProfileLogPut(MicroProfileLogTimerIndex(LE), MicroProfileLogGetTick(LE), MicroProfileLogType(LE), g_MicroProfileGpuLog);
	}
}

void MicroProfileSetForceEnable(bool bEnable)
{
	S.nForceEnable = bEnable ? 1 : 0;
}
bool MicroProfileGetForceEnable()
{
	return S.nForceEnable != 0;
}

void MicroProfileSetEnableAllGroups(bool bEnableAllGroups)
{
	S.nAllGroupsWanted = bEnableAllGroups ? 1 : 0;
}

void MicroProfileEnableCategory(const char* pCategory, bool bEnabled)
{
	int nCategoryIndex = -1;
	for(uint32_t i = 0; i < S.nCategoryCount; ++i)
	{
		if(!MP_STRCASECMP(pCategory, S.CategoryInfo[i].pName))
		{
			nCategoryIndex = (int)i;
			break;
		}
	}
	if(nCategoryIndex >= 0)
	{
		if(bEnabled)
		{
			S.nActiveGroupWanted |= S.CategoryInfo[nCategoryIndex].nGroupMask;
		}
		else
		{
			S.nActiveGroupWanted &= ~S.CategoryInfo[nCategoryIndex].nGroupMask;
		}
	}
}


void MicroProfileEnableCategory(const char* pCategory)
{
	MicroProfileEnableCategory(pCategory, true);
}
void MicroProfileDisableCategory(const char* pCategory)
{
	MicroProfileEnableCategory(pCategory, false);
}

bool MicroProfileGetEnableAllGroups()
{
	return 0 != S.nAllGroupsWanted;
}

void MicroProfileSetForceMetaCounters(bool bForce)
{
	S.nForceMetaCounters = bForce ? 1 : 0;
}

bool MicroProfileGetForceMetaCounters()
{
	return 0 != S.nForceMetaCounters;
}

void MicroProfileEnableMetaCounter(const char* pMeta)
{
	for(uint32_t i = 0; i < MICROPROFILE_META_MAX; ++i)
	{
		if(S.MetaCounters[i].pName && 0 == MP_STRCASECMP(S.MetaCounters[i].pName, pMeta))
		{
			S.nBars |= (MP_DRAW_META_FIRST<<i);
			return;
		}
	}
}
void MicroProfileDisableMetaCounter(const char* pMeta)
{
	for(uint32_t i = 0; i < MICROPROFILE_META_MAX; ++i)
	{
		if(S.MetaCounters[i].pName && 0 == MP_STRCASECMP(S.MetaCounters[i].pName, pMeta))
		{
			S.nBars &= ~(MP_DRAW_META_FIRST<<i);
			return;
		}
	}
}


void MicroProfileSetAggregateFrames(int nFrames)
{
	S.nAggregateFlip = (uint32_t)nFrames;
	if(0 == nFrames)
	{
		S.nAggregateClear = 1;
	}
}

int MicroProfileGetAggregateFrames()
{
	return S.nAggregateFlip;
}

int MicroProfileGetCurrentAggregateFrames()
{
	return int(S.nAggregateFlip ? S.nAggregateFlip : S.nAggregateFlipCount);
}


void MicroProfileForceEnableGroup(const char* pGroup, MicroProfileTokenType Type)
{
	MicroProfileInit();
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
	uint16_t nGroup = MicroProfileGetGroup(pGroup, Type);
	S.nForceGroup |= (1ll << nGroup);
}

void MicroProfileForceDisableGroup(const char* pGroup, MicroProfileTokenType Type)
{
	MicroProfileInit();
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
	uint16_t nGroup = MicroProfileGetGroup(pGroup, Type);
	S.nForceGroup &= ~(1ll << nGroup);
}


void MicroProfileCalcAllTimers(float* pTimers, float* pAverage, float* pMax, float* pMin, float* pCallAverage, float* pExclusive, float* pAverageExclusive, float* pMaxExclusive, float* pTotal, uint32_t nSize)
{
	for(uint32_t i = 0; i < S.nTotalTimers && i < nSize; ++i)
	{
		const uint32_t nGroupId = S.TimerInfo[i].nGroupIndex;
		const float fToMs = MicroProfileTickToMsMultiplier(S.GroupInfo[nGroupId].Type == MicroProfileTokenTypeGpu ? MicroProfileTicksPerSecondGpu() : MicroProfileTicksPerSecondCpu());
		uint32_t nTimer = i;
		uint32_t nIdx = i * 2;
		uint32_t nAggregateFrames = S.nAggregateFrames ? S.nAggregateFrames : 1;
		uint32_t nAggregateCount = S.Aggregate[nTimer].nCount ? S.Aggregate[nTimer].nCount : 1;
		float fToPrc = S.fRcpReferenceTime;
		float fMs = fToMs * (S.Frame[nTimer].nTicks);
		float fPrc = MicroProfileMin(fMs * fToPrc, 1.f);
		float fAverageMs = fToMs * (S.Aggregate[nTimer].nTicks / nAggregateFrames);
		float fAveragePrc = MicroProfileMin(fAverageMs * fToPrc, 1.f);
		float fMaxMs = fToMs * (S.AggregateMax[nTimer]);
		float fMaxPrc = MicroProfileMin(fMaxMs * fToPrc, 1.f);
		float fMinMs = fToMs * (S.AggregateMin[nTimer] != uint64_t(-1) ? S.AggregateMin[nTimer] : 0);
		float fMinPrc = MicroProfileMin(fMinMs * fToPrc, 1.f);
		float fCallAverageMs = fToMs * (S.Aggregate[nTimer].nTicks / nAggregateCount);
		float fCallAveragePrc = MicroProfileMin(fCallAverageMs * fToPrc, 1.f);
		float fMsExclusive = fToMs * (S.FrameExclusive[nTimer]);
		float fPrcExclusive = MicroProfileMin(fMsExclusive * fToPrc, 1.f);
		float fAverageMsExclusive = fToMs * (S.AggregateExclusive[nTimer] / nAggregateFrames);
		float fAveragePrcExclusive = MicroProfileMin(fAverageMsExclusive * fToPrc, 1.f);
		float fMaxMsExclusive = fToMs * (S.AggregateMaxExclusive[nTimer]);
		float fMaxPrcExclusive = MicroProfileMin(fMaxMsExclusive * fToPrc, 1.f);
		float fTotalMs = fToMs * S.Aggregate[nTimer].nTicks;
		pTimers[nIdx] = fMs;
		pTimers[nIdx+1] = fPrc;
		pAverage[nIdx] = fAverageMs;
		pAverage[nIdx+1] = fAveragePrc;
		pMax[nIdx] = fMaxMs;
		pMax[nIdx+1] = fMaxPrc;
		pMin[nIdx] = fMinMs;
		pMin[nIdx + 1] = fMinPrc;
		pCallAverage[nIdx] = fCallAverageMs;
		pCallAverage[nIdx+1] = fCallAveragePrc;
		pExclusive[nIdx] = fMsExclusive;
		pExclusive[nIdx+1] = fPrcExclusive;
		pAverageExclusive[nIdx] = fAverageMsExclusive;
		pAverageExclusive[nIdx+1] = fAveragePrcExclusive;
		pMaxExclusive[nIdx] = fMaxMsExclusive;
		pMaxExclusive[nIdx+1] = fMaxPrcExclusive;
		pTotal[nIdx] = fTotalMs;
		pTotal[nIdx+1] = 0.f;
	}
}

void MicroProfileTogglePause()
{
	S.nToggleRunning = 1;
}

float MicroProfileGetTime(const char* pGroup, const char* pName)
{
	MicroProfileToken nToken = MicroProfileFindToken(pGroup, pName);
	if(nToken == MICROPROFILE_INVALID_TOKEN)
	{
		return 0.f;
	}
	uint32_t nTimerIndex = MicroProfileGetTimerIndex(nToken);
	uint32_t nGroupIndex = MicroProfileGetGroupIndex(nToken);
	float fToMs = MicroProfileTickToMsMultiplier(S.GroupInfo[nGroupIndex].Type == MicroProfileTokenTypeGpu ? MicroProfileTicksPerSecondGpu() : MicroProfileTicksPerSecondCpu());
	return S.Frame[nTimerIndex].nTicks * fToMs;
}


int MicroProfileFormatCounter(int eFormat, int64_t nCounter, char* pOut, uint32_t nBufferSize)
{
	if (!nCounter)
	{
		pOut[0] = '0';
		pOut[1] = '\0';
		return 1;
	}
	int nLen = 0;
	char* pBase = pOut;
	char* pTmp = pOut;
	char* pEnd = pOut + nBufferSize;

	switch (eFormat)
	{
	case MICROPROFILE_COUNTER_FORMAT_DEFAULT:
	{
		int nNegative = 0;
		if(nCounter < 0)
		{
			nCounter = -nCounter;
			nNegative = 1;
		}
		int nSeperate = 0;
		while (nCounter)
		{
			if (nSeperate)
			{
				*pTmp++ = ' ';
			}
			nSeperate = 1;
			for (uint32_t i = 0; nCounter && i < 3; ++i)
			{
				int nDigit = nCounter % 10;
				nCounter /= 10;
				*pTmp++ = '0' + nDigit;
			}
		}
		if(nNegative)
		{
			*pTmp++ = '-';
		}
		nLen = pTmp - pOut;
		--pTmp;
		MP_ASSERT(pTmp <= pEnd);
		while (pTmp > pOut) //reverse string
		{
			char c = *pTmp;
			*pTmp = *pOut;
			*pOut = c;
			pTmp--;
			pOut++;
		}
	}
	break;
	case MICROPROFILE_COUNTER_FORMAT_BYTES:
	{
		const char* pExt[] = { "b","kb","mb","gb","tb","pb", "eb","zb", "yb" };
		size_t nNumExt = sizeof(pExt) / sizeof(pExt[0]);
		int64_t nShift = 0;
		int64_t nDivisor = 1;
		int64_t nCountShifted = nCounter >> 10;
		while (nCountShifted)
		{
			nDivisor <<= 10;
			nCountShifted >>= 10;
			nShift++;
		}
		MP_ASSERT(nShift < (int64_t)nNumExt);
		if (nShift)
		{
			nLen = snprintf(pOut, nBufferSize - 1, "%3.2f%s", (double)nCounter / nDivisor, pExt[nShift]);
		}
		else
		{
			nLen = snprintf(pOut, nBufferSize - 1, "%lld%s", (long long)nCounter, pExt[nShift]);
		}
	}
	break;
	}
	pBase[nLen] = '\0';

	return nLen;
}

typedef void MicroProfileWriteCallback(void* Handle, size_t size, const char* pData);

void MicroProfileDumpFile(const char* pPath, MicroProfileDumpType eType, uint32_t nFrames)
{
	size_t nLen = strlen(pPath);
	if(nLen > sizeof(S.DumpPath)-1)
	{
		return;
	}
	memcpy(S.DumpPath, pPath, nLen+1);
	S.nDumpFileNextFrame = 1;
	S.eDumpType = eType;
	S.nDumpFrames = nFrames;
}

MICROPROFILE_FORMAT(3, 4) void MicroProfilePrintf(MicroProfileWriteCallback CB, void* Handle, const char* pFmt, ...)
{
	char buffer[4096];
	va_list args;
	va_start (args, pFmt);
#ifdef _WIN32
	size_t size = vsprintf_s(buffer, pFmt, args);
#else
	size_t size = vsnprintf(buffer, sizeof(buffer)-1,  pFmt, args);
#endif
	CB(Handle, size, &buffer[0]);
	va_end (args);
}

void MicroProfilePrintUIntComma(MicroProfileWriteCallback CB, void* Handle, uint64_t nData)
{
	char Buffer[32];

	uint32_t nOffset = sizeof(Buffer);
	Buffer[--nOffset] = ',';

	if(nData < 10)
	{
		Buffer[--nOffset] = '0' + nData;
	}
	else
	{
		do
		{
			Buffer[--nOffset] = "0123456789abcdef"[nData & 0xf];
			nData >>= 4;
		}
		while(nData);

		Buffer[--nOffset] = 'x';
		Buffer[--nOffset] = '0';
	}

	CB(Handle, sizeof(Buffer) - nOffset, &Buffer[nOffset]);
}

void MicroProfilePrintString(MicroProfileWriteCallback CB, void* Handle, const char* pData)
{
	CB(Handle, strlen(pData), pData);
}

void MicroProfileDumpCsv(MicroProfileWriteCallback CB, void* Handle, int nMaxFrames)
{
	(void)nMaxFrames;

	uint32_t nAggregateFrames = S.nAggregateFrames ? S.nAggregateFrames : 1;
	float fToMsCPU = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
	float fToMsGPU = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondGpu());

	MicroProfilePrintf(CB, Handle, "frames,%d\n", nAggregateFrames);
	MicroProfilePrintf(CB, Handle, "group,name,average,max,callaverage\n");

	uint32_t nNumTimers = S.nTotalTimers;
	uint32_t nBlockSize = 2 * nNumTimers;
	float* pTimers = (float*)alloca(nBlockSize * 9 * sizeof(float));
	float* pAverage = pTimers + nBlockSize;
	float* pMax = pTimers + 2 * nBlockSize;
	float* pMin = pTimers + 3 * nBlockSize;
	float* pCallAverage = pTimers + 4 * nBlockSize;
	float* pTimersExclusive = pTimers + 5 * nBlockSize;
	float* pAverageExclusive = pTimers + 6 * nBlockSize;
	float* pMaxExclusive = pTimers + 7 * nBlockSize;
	float* pTotal = pTimers + 8 * nBlockSize;

	MicroProfileCalcAllTimers(pTimers, pAverage, pMax, pMin, pCallAverage, pTimersExclusive, pAverageExclusive, pMaxExclusive, pTotal, nNumTimers);

	for(uint32_t i = 0; i < S.nTotalTimers; ++i)
	{
		uint32_t nIdx = i * 2;
		MicroProfilePrintf(CB, Handle, "\"%s\",\"%s\",%f,%f,%f\n", S.TimerInfo[i].pName, S.GroupInfo[S.TimerInfo[i].nGroupIndex].pName, pAverage[nIdx], pMax[nIdx], pCallAverage[nIdx]);
	}

	MicroProfilePrintf(CB, Handle, "\n\n");

	MicroProfilePrintf(CB, Handle, "group,average,max,total\n");
	for(uint32_t j = 0; j < MICROPROFILE_MAX_GROUPS; ++j)
	{
		const char* pGroupName = S.GroupInfo[j].pName;
		float fToMs =  S.GroupInfo[j].Type == MicroProfileTokenTypeGpu ? fToMsGPU : fToMsCPU;
		if(pGroupName[0] != '\0')
		{
			MicroProfilePrintf(CB, Handle, "\"%s\",%.3f,%.3f,%.3f\n", pGroupName, fToMs * S.AggregateGroup[j] / nAggregateFrames, fToMs * S.AggregateGroup[j] / nAggregateFrames, fToMs * S.AggregateGroup[j]);
		}
	}

	MicroProfilePrintf(CB, Handle, "\n\n");
	MicroProfilePrintf(CB, Handle, "group,thread,average,total\n");
	for(uint32_t j = 0; j < MICROPROFILE_MAX_GROUPS; ++j)
	{
		for(uint32_t i = 0; i < S.nNumLogs; ++i)
		{
			if(S.Pool[i])
			{
				const char* pThreadName = &S.Pool[i]->ThreadName[0];
				// MicroProfilePrintf(CB, Handle, "var ThreadGroupTime%d = [", i);
				float fToMs = S.Pool[i]->nGpu ? fToMsGPU : fToMsCPU;
				{
					uint64_t nTicks = S.Pool[i]->nAggregateGroupTicks[j];
					float fTime = nTicks / nAggregateFrames * fToMs;
					float fTimeTotal = nTicks * fToMs;
					if(fTimeTotal > 0.01f)
					{
						const char* pGroupName = S.GroupInfo[j].pName;
						MicroProfilePrintf(CB, Handle, "\"%s\",\"%s\",%.3f,%.3f\n", pGroupName, pThreadName, fTime, fTimeTotal);
					}
				}
			}
		}
	}

	MicroProfilePrintf(CB, Handle, "\n\n");
	MicroProfilePrintf(CB, Handle, "frametimecpu\n");

	const uint32_t nCount = MICROPROFILE_MAX_FRAME_HISTORY - MICROPROFILE_GPU_FRAME_DELAY - 3;
	const uint32_t nStart = S.nFrameCurrent;
	for(uint32_t i = nCount; i > 0; i--)
	{
		uint32_t nFrame = (nStart + MICROPROFILE_MAX_FRAME_HISTORY - i) % MICROPROFILE_MAX_FRAME_HISTORY;
		uint32_t nFrameNext = (nStart + MICROPROFILE_MAX_FRAME_HISTORY - i + 1) % MICROPROFILE_MAX_FRAME_HISTORY;
		uint64_t nTicks = S.Frames[nFrameNext].nFrameStartCpu - S.Frames[nFrame].nFrameStartCpu;
		MicroProfilePrintf(CB, Handle, "%f,", nTicks * fToMsCPU);
	}
	MicroProfilePrintf(CB, Handle, "\n");

	MicroProfilePrintf(CB, Handle, "\n\n");
	MicroProfilePrintf(CB, Handle, "frametimegpu\n");

	for(uint32_t i = nCount; i > 0; i--)
	{
		uint32_t nFrame = (nStart + MICROPROFILE_MAX_FRAME_HISTORY - i) % MICROPROFILE_MAX_FRAME_HISTORY;
		uint32_t nFrameNext = (nStart + MICROPROFILE_MAX_FRAME_HISTORY - i + 1) % MICROPROFILE_MAX_FRAME_HISTORY;
		uint64_t nTicks = S.Frames[nFrameNext].nFrameStartGpu - S.Frames[nFrame].nFrameStartGpu;
		MicroProfilePrintf(CB, Handle, "%f,", nTicks * fToMsGPU);
	}
	MicroProfilePrintf(CB, Handle, "\n\n");
	MicroProfilePrintf(CB, Handle, "Meta\n");//only single frame snapshot
	MicroProfilePrintf(CB, Handle, "name,average,max,total\n");
	for(int j = 0; j < MICROPROFILE_META_MAX; ++j)
	{
		if(S.MetaCounters[j].pName)
		{
			MicroProfilePrintf(CB, Handle, "\"%s\",%f,%lld,%lld\n",S.MetaCounters[j].pName, S.MetaCounters[j].nSumAggregate / (float)nAggregateFrames, (long long)S.MetaCounters[j].nSumAggregateMax, (long long)S.MetaCounters[j].nSumAggregate);
		}
	}
}

#if MICROPROFILE_EMBED_HTML
extern const char* g_MicroProfileHtml_begin[];
extern size_t g_MicroProfileHtml_begin_sizes[];
extern size_t g_MicroProfileHtml_begin_count;
extern const char* g_MicroProfileHtml_end[];
extern size_t g_MicroProfileHtml_end_sizes[];
extern size_t g_MicroProfileHtml_end_count;


void MicroProfileDumpHtml(MicroProfileWriteCallback CB, void* Handle, int nMaxFrames, const char* pHost)
{
	uint32_t nRunning = S.nRunning;
	S.nRunning = 0;
	//stall pushing of timers
	uint64_t nActiveGroup = S.nActiveGroup;
	S.nActiveGroup = 0;
	S.nPauseTicks = MP_TICK();


	for(size_t i = 0; i < g_MicroProfileHtml_begin_count; ++i)
	{
		CB(Handle, g_MicroProfileHtml_begin_sizes[i]-1, g_MicroProfileHtml_begin[i]);
	}
	//dump info
	uint64_t nTicks = MP_TICK();

	float fToMsCPU = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
	float fToMsGPU = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondGpu());
	float fAggregateMs = fToMsCPU * (nTicks - S.nAggregateFlipTick);
	MicroProfilePrintf(CB, Handle, "var DumpHost = '%s';\n", pHost ? pHost : "");
	time_t CaptureTime;
	time(&CaptureTime);
	MicroProfilePrintf(CB, Handle, "var DumpUtcCaptureTime = %ld;\n", CaptureTime);
	MicroProfilePrintf(CB, Handle, "var AggregateInfo = {'Frames':%d, 'Time':%f};\n", S.nAggregateFrames, fAggregateMs);

	//categories
	MicroProfilePrintf(CB, Handle, "var CategoryInfo = Array(%d);\n",S.nCategoryCount);
	for(uint32_t i = 0; i < S.nCategoryCount; ++i)
	{
		MicroProfilePrintf(CB, Handle, "CategoryInfo[%d] = \"%s\";\n", i, S.CategoryInfo[i].pName);
	}

	//groups
	MicroProfilePrintf(CB, Handle, "var GroupInfo = Array(%d);\n\n",S.nGroupCount);
	uint32_t nAggregateFrames = S.nAggregateFrames ? S.nAggregateFrames : 1;

	for(uint32_t i = 0; i < S.nGroupCount; ++i)
	{
		MP_ASSERT(i == S.GroupInfo[i].nGroupIndex);
		float fToMs = S.GroupInfo[i].Type == MicroProfileTokenTypeCpu ? fToMsCPU : fToMsGPU;
		uint32_t nColor = S.TimerInfo[i].nColor;
		MicroProfilePrintf(CB, Handle, "GroupInfo[%d] = MakeGroup(%d, \"%s\", %d, %d, %d, %f, %f, %f, '#%06x');\n", 
			S.GroupInfo[i].nGroupIndex, 
			S.GroupInfo[i].nGroupIndex, 
			S.GroupInfo[i].pName, 
			S.GroupInfo[i].nCategory, 
			S.GroupInfo[i].nNumTimers, 
			S.GroupInfo[i].Type == MicroProfileTokenTypeGpu?1:0, 
			fToMs * S.AggregateGroup[i], 
			fToMs * S.AggregateGroup[i] / nAggregateFrames, 
			fToMs * S.AggregateGroupMax[i],
			((MICROPROFILE_UNPACK_RED(nColor) & 0xff) << 16) | ((MICROPROFILE_UNPACK_GREEN(nColor) & 0xff) << 8) | (MICROPROFILE_UNPACK_BLUE(nColor) & 0xff));
	}
	//timers

	uint32_t nNumTimers = S.nTotalTimers;
	uint32_t nBlockSize = 2 * nNumTimers;
	float* pTimers = (float*)alloca(nBlockSize * 9 * sizeof(float));
	float* pAverage = pTimers + nBlockSize;
	float* pMax = pTimers + 2 * nBlockSize;
	float* pMin = pTimers + 3 * nBlockSize;
	float* pCallAverage = pTimers + 4 * nBlockSize;
	float* pTimersExclusive = pTimers + 5 * nBlockSize;
	float* pAverageExclusive = pTimers + 6 * nBlockSize;
	float* pMaxExclusive = pTimers + 7 * nBlockSize;
	float* pTotal = pTimers + 8 * nBlockSize;

	MicroProfileCalcAllTimers(pTimers, pAverage, pMax, pMin, pCallAverage, pTimersExclusive, pAverageExclusive, pMaxExclusive, pTotal, nNumTimers);

	MicroProfilePrintf(CB, Handle, "\nvar TimerInfo = Array(%d);\n\n", S.nTotalTimers);
	for(uint32_t i = 0; i < S.nTotalTimers; ++i)
	{
		uint32_t nIdx = i * 2;
		MP_ASSERT(i == S.TimerInfo[i].nTimerIndex);

		uint32_t nColor = S.TimerInfo[i].nColor;
		uint32_t nColorDark = (nColor >> 1) & ~0x80808080;
		MicroProfilePrintf(CB, Handle, "TimerInfo[%d] = MakeTimer(%d, \"%s\", %d, '#%06x','#%06x', %f, %f, %f, %f, %f, %f, %d, %f,\n",
			S.TimerInfo[i].nTimerIndex, S.TimerInfo[i].nTimerIndex, S.TimerInfo[i].pName, S.TimerInfo[i].nGroupIndex, 
			((MICROPROFILE_UNPACK_RED(nColor) & 0xff) << 16) | ((MICROPROFILE_UNPACK_GREEN(nColor) & 0xff) << 8) | (MICROPROFILE_UNPACK_BLUE(nColor) & 0xff),
			((MICROPROFILE_UNPACK_RED(nColorDark) & 0xff) << 16) | ((MICROPROFILE_UNPACK_GREEN(nColorDark) & 0xff) << 8) | (MICROPROFILE_UNPACK_BLUE(nColorDark) & 0xff),
			pAverage[nIdx],
			pMax[nIdx],
			pMin[nIdx],
			pAverageExclusive[nIdx],
			pMaxExclusive[nIdx],
			pCallAverage[nIdx],
			S.Aggregate[i].nCount,
			pTotal[nIdx]);

		MicroProfilePrintString(CB, Handle, "\t[");
		for(int j = 0; j < MICROPROFILE_META_MAX; ++j)
		{
			if(S.MetaCounters[j].pName)
			{
				MicroProfilePrintUIntComma(CB, Handle, S.MetaCounters[j].nCounters[i]);
			}
		}
		MicroProfilePrintString(CB, Handle, "],[");
		for(int j = 0; j < MICROPROFILE_META_MAX; ++j)
		{
			if(S.MetaCounters[j].pName)
			{
				MicroProfilePrintUIntComma(CB, Handle, S.MetaCounters[j].nAggregate[i]);
			}
		}
		MicroProfilePrintString(CB, Handle, "],[");
		for(int j = 0; j < MICROPROFILE_META_MAX; ++j)
		{
			if(S.MetaCounters[j].pName)
			{
				MicroProfilePrintUIntComma(CB, Handle, S.MetaCounters[j].nAggregateMax[i]);
			}
		}
		MicroProfilePrintString(CB, Handle, "]);\n");
	}

	MicroProfilePrintString(CB, Handle, "\nvar ThreadNames = [");
	for(uint32_t i = 0; i < S.nNumLogs; ++i)
	{
		if(S.Pool[i])
		{
			MicroProfilePrintf(CB, Handle, "'%s',", S.Pool[i]->ThreadName);
		}
		else
		{
			MicroProfilePrintf(CB, Handle, "'Thread %d',", i);
		}
	}
	MicroProfilePrintString(CB, Handle, "];\n\n");


	MicroProfilePrintString(CB, Handle, "\nvar ThreadIds = [");
	for(uint32_t i = 0; i < S.nNumLogs; ++i)
	{
		MicroProfileThreadIdType nThreadId = S.Pool[i] ? S.Pool[i]->nThreadId : 0;
		MicroProfilePrintUIntComma(CB, Handle, nThreadId);
	}
	MicroProfilePrintString(CB, Handle, "];\n\n");


	MicroProfilePrintString(CB, Handle, "\nvar ThreadGpu = [");
	for(uint32_t i = 0; i < S.nNumLogs; ++i)
	{
		uint32_t nGpu = S.Pool[i] ? S.Pool[i]->nGpu : 0;
		MicroProfilePrintUIntComma(CB, Handle, nGpu);
	}
	MicroProfilePrintString(CB, Handle, "];\n\n");


	MicroProfilePrintString(CB, Handle, "\nvar ThreadGroupTimeArray = [\n");
	for(uint32_t i = 0; i < S.nNumLogs; ++i)
	{
		if(S.Pool[i])
		{
			float fToMs = S.Pool[i]->nGpu ? fToMsGPU : fToMsCPU;
			MicroProfilePrintf(CB, Handle, "MakeTimes(%e,[", fToMs);
			for(uint32_t j = 0; j < MICROPROFILE_MAX_GROUPS; ++j)
			{
				MicroProfilePrintUIntComma(CB, Handle, S.Pool[i]->nAggregateGroupTicks[j]);
			}
			MicroProfilePrintString(CB, Handle, "]),\n");
		}
	}
	MicroProfilePrintString(CB, Handle, "];");


	MicroProfilePrintString(CB, Handle, "\nvar MetaNames = [");
	for(int i = 0; i < MICROPROFILE_META_MAX; ++i)
	{
		if(S.MetaCounters[i].pName)
		{
			MicroProfilePrintf(CB, Handle, "'%s',", S.MetaCounters[i].pName);
		}
	}
	MicroProfilePrintString(CB, Handle, "];\n\n");

	MicroProfilePrintString(CB, Handle, "\nvar CounterInfo = [");
	for(uint32_t i = 0; i < S.nNumCounters; ++i)
	{
		int64_t nCounter = S.Counters[i].load();
		int64_t nLimit = S.CounterInfo[i].nLimit;
		float fCounterPrc = 0.f;
		float fBoxPrc = 1.f;
		if(nLimit)
		{
			fCounterPrc = (float)nCounter / nLimit;
			if(fCounterPrc>1.f)
			{
				fBoxPrc = 1.f / fCounterPrc;
				fCounterPrc = 1.f;
			}
		}

		int64_t nCounterMin = 0, nCounterMax = 0;

	#if MICROPROFILE_COUNTER_HISTORY
		nCounterMin = S.nCounterMin[i];
		nCounterMax = S.nCounterMax[i];
	#endif

		char Formatted[64];
		char FormattedLimit[64];
		MicroProfileFormatCounter(S.CounterInfo[i].eFormat, nCounter, Formatted, sizeof(Formatted)-1);
		MicroProfileFormatCounter(S.CounterInfo[i].eFormat, S.CounterInfo[i].nLimit, FormattedLimit, sizeof(FormattedLimit)-1);
		MicroProfilePrintf(CB, Handle, "MakeCounter(%d, %d, %d, %d, %d, '%s', %lld, %lld, %lld, '%s', %lld, '%s', %d, %f, %f, [",
			i,
			S.CounterInfo[i].nParent,
			S.CounterInfo[i].nSibling,
 	 		S.CounterInfo[i].nFirstChild,
 	 		S.CounterInfo[i].nLevel,
			S.CounterInfo[i].pName,
			(long long)nCounter,
			(long long)nCounterMin,
			(long long)nCounterMax,
			Formatted,
			(long long)nLimit,
			FormattedLimit,
			S.CounterInfo[i].eFormat == MICROPROFILE_COUNTER_FORMAT_BYTES ? 1 : 0, 
			fCounterPrc,
			fBoxPrc
			);

	#if MICROPROFILE_COUNTER_HISTORY
		if(0 != (S.CounterInfo[i].nFlags & MICROPROFILE_COUNTER_FLAG_DETAILED))
		{
			uint32_t nBaseIndex = S.nCounterHistoryPut;
			for(uint32_t j = 0; j < MICROPROFILE_GRAPH_HISTORY; ++j)
			{
				uint32_t nHistoryIndex = (nBaseIndex + j) % MICROPROFILE_GRAPH_HISTORY;
				int64_t nValue = MicroProfileClamp(S.nCounterHistory[nHistoryIndex][i], nCounterMin, nCounterMax);
				MicroProfilePrintUIntComma(CB, Handle, nValue - nCounterMin);
			}
		}
	#endif

		MicroProfilePrintString(CB, Handle, "]),\n");
	}
	MicroProfilePrintString(CB, Handle, "];\n\n");


	uint32_t nNumFrames = (MICROPROFILE_MAX_FRAME_HISTORY - MICROPROFILE_GPU_FRAME_DELAY - 3); //leave a few to not overwrite
	nNumFrames = MicroProfileMin(nNumFrames, (uint32_t)nMaxFrames);


	uint32_t nFirstFrame = (S.nFrameCurrent + MICROPROFILE_MAX_FRAME_HISTORY - nNumFrames) % MICROPROFILE_MAX_FRAME_HISTORY;
	uint32_t nLastFrame = (nFirstFrame + nNumFrames) % MICROPROFILE_MAX_FRAME_HISTORY;
	MP_ASSERT(nLastFrame == (S.nFrameCurrent % MICROPROFILE_MAX_FRAME_HISTORY));
	MP_ASSERT(nFirstFrame < MICROPROFILE_MAX_FRAME_HISTORY);
	MP_ASSERT(nLastFrame  < MICROPROFILE_MAX_FRAME_HISTORY);
	const int64_t nTickStart = S.Frames[nFirstFrame].nFrameStartCpu;
	const int64_t nTickEnd = S.Frames[nLastFrame].nFrameStartCpu;
	int64_t nTickStartGpu = S.Frames[nFirstFrame].nFrameStartGpu;

	int64_t nTicksPerSecondCpu = MicroProfileTicksPerSecondCpu();
	int64_t nTicksPerSecondGpu = MicroProfileTicksPerSecondGpu();

	int64_t nTickReferenceCpu = 0, nTickReferenceGpu = 0;
	// Can't just call GetGpuTickReference off main thread...
	if(0 && MicroProfileGetGpuTickReference(&nTickReferenceCpu, &nTickReferenceGpu))
	{
		nTickStartGpu = (nTickStart - nTickReferenceCpu) * (double(nTicksPerSecondGpu) / double(nTicksPerSecondCpu)) + nTickReferenceGpu;
	}

#if MICROPROFILE_DEBUG
	printf("dumping %d frames\n", nNumFrames);
	printf("dumping frame %d to %d\n", nFirstFrame, nLastFrame);
#endif


	uint32_t* nTimerCounter = (uint32_t*)alloca(sizeof(uint32_t)* S.nTotalTimers);
	memset(nTimerCounter, 0, sizeof(uint32_t) * S.nTotalTimers);

	MicroProfilePrintf(CB, Handle, "var Frames = Array(%d);\n", nNumFrames);
	for(uint32_t i = 0; i < nNumFrames; ++i)
	{
		uint32_t nFrameIndex = (nFirstFrame + i) % MICROPROFILE_MAX_FRAME_HISTORY;
		uint32_t nFrameIndexNext = (nFrameIndex + 1) % MICROPROFILE_MAX_FRAME_HISTORY;

		MicroProfilePrintf(CB, Handle, "var tt%d = [\n", i);
		for(uint32_t j = 0; j < S.nNumLogs; ++j)
		{
			MicroProfileThreadLog* pLog = S.Pool[j];
			uint32_t nLogStart = S.Frames[nFrameIndex].nLogStart[j];
			uint32_t nLogEnd = S.Frames[nFrameIndexNext].nLogStart[j];

			MicroProfilePrintString(CB, Handle, "[");
			for(uint32_t k = nLogStart; k != nLogEnd; k = (k+1) % MICROPROFILE_BUFFER_SIZE)
			{
				uint32_t nLogType = MicroProfileLogType(pLog->Log[k]);
				if(nLogType == MP_LOG_META)
				{
					//for meta, store the count + 8, which is the tick part
					nLogType = 8 + MicroProfileLogGetTick(pLog->Log[k]);
				}
				MicroProfilePrintUIntComma(CB, Handle, nLogType);
			}
			MicroProfilePrintString(CB, Handle, "],\n");
		}
		MicroProfilePrintString(CB, Handle, "];\n");

		MicroProfilePrintf(CB, Handle, "var ts%d = [\n", i);
		for(uint32_t j = 0; j < S.nNumLogs; ++j)
		{
			MicroProfileThreadLog* pLog = S.Pool[j];
			uint32_t nLogStart = S.Frames[nFrameIndex].nLogStart[j];
			uint32_t nLogEnd = S.Frames[nFrameIndexNext].nLogStart[j];

			int64_t nStartTick = pLog->nGpu ? nTickStartGpu : nTickStart;
			float fToMs = pLog->nGpu ? fToMsGPU : fToMsCPU;

			if(pLog->nGpu)
				MicroProfilePrintf(CB, Handle, "MakeTimesExtra(%e,%e,tt%d[%d],[", fToMs, fToMsCPU, i, j);
			else
				MicroProfilePrintf(CB, Handle, "MakeTimes(%e,[", fToMs);
			for(uint32_t k = nLogStart; k != nLogEnd; k = (k+1) % MICROPROFILE_BUFFER_SIZE)
			{
				uint32_t nLogType = MicroProfileLogType(pLog->Log[k]);
				uint64_t nTick =
					(nLogType == MP_LOG_ENTER || nLogType == MP_LOG_LEAVE)
					? MicroProfileLogTickDifference(nStartTick, pLog->Log[k])
					: (nLogType == MP_LOG_GPU_EXTRA)
					? MicroProfileLogTickDifference(nTickStart, pLog->Log[k])
					: 0;
				MicroProfilePrintUIntComma(CB, Handle, nTick);
			}
			MicroProfilePrintString(CB, Handle, "]),\n");
		}
		MicroProfilePrintString(CB, Handle, "];\n");

		MicroProfilePrintf(CB, Handle, "var ti%d = [\n", i);
		for(uint32_t j = 0; j < S.nNumLogs; ++j)
		{
			MicroProfileThreadLog* pLog = S.Pool[j];
			uint32_t nLogStart = S.Frames[nFrameIndex].nLogStart[j];
			uint32_t nLogEnd = S.Frames[nFrameIndexNext].nLogStart[j];

			uint32_t nLabelIndex = 0;
			MicroProfilePrintString(CB, Handle, "[");
			for(uint32_t k = nLogStart; k != nLogEnd; k = (k+1) % MICROPROFILE_BUFFER_SIZE)
			{
				uint32_t nLogType = MicroProfileLogType(pLog->Log[k]);
				uint32_t nTimerIndex = (uint32_t)MicroProfileLogTimerIndex(pLog->Log[k]);
				uint32_t nIndex = (nLogType == MP_LOG_LABEL) ? nLabelIndex++ : nTimerIndex;
				MicroProfilePrintUIntComma(CB, Handle, nIndex);

				if(nLogType == MP_LOG_ENTER)
					nTimerCounter[nTimerIndex]++;
			}
			MicroProfilePrintString(CB, Handle, "],\n");
		}
		MicroProfilePrintString(CB, Handle, "];\n");

		MicroProfilePrintf(CB, Handle, "var tl%d = [\n", i);
		for(uint32_t j = 0; j < S.nNumLogs; ++j)
		{
			MicroProfileThreadLog* pLog = S.Pool[j];
			uint32_t nLogStart = S.Frames[nFrameIndex].nLogStart[j];
			uint32_t nLogEnd = S.Frames[nFrameIndexNext].nLogStart[j];

			MicroProfilePrintString(CB, Handle, "[");
			for(uint32_t k = nLogStart; k != nLogEnd; k = (k+1) % MICROPROFILE_BUFFER_SIZE)
			{
				uint32_t nLogType = MicroProfileLogType(pLog->Log[k]);
				if(nLogType == MP_LOG_LABEL)
				{
					uint64_t nLabel = MicroProfileLogGetTick(pLog->Log[k]);
					const char* pLabelName = MicroProfileGetLabel(nLabel);

					if(pLabelName)
					{
						MicroProfilePrintString(CB, Handle, "\"");
						MicroProfilePrintString(CB, Handle, pLabelName);
						MicroProfilePrintString(CB, Handle, "\",");
					}
					else
						MicroProfilePrintString(CB, Handle, "null,");
				}
			}
			MicroProfilePrintString(CB, Handle, "],\n");
		}
		MicroProfilePrintString(CB, Handle, "];\n");

		int64_t nFrameStart = S.Frames[nFrameIndex].nFrameStartCpu;
		int64_t nFrameEnd = S.Frames[nFrameIndexNext].nFrameStartCpu;
		int64_t nFrameStartGpu = S.Frames[nFrameIndex].nFrameStartGpu;
		int64_t nFrameEndGpu = S.Frames[nFrameIndexNext].nFrameStartGpu;

		float fToMs = MicroProfileTickToMsMultiplier(nTicksPerSecondCpu);
		float fFrameMs = MicroProfileLogTickDifference(nTickStart, nFrameStart) * fToMs;
		float fFrameEndMs = MicroProfileLogTickDifference(nTickStart, nFrameEnd) * fToMs;
		float fFrameGpuMs = MicroProfileLogTickDifference(nTickStartGpu, nFrameStartGpu) * fToMsGPU;
		float fFrameGpuEndMs = MicroProfileLogTickDifference(nTickStartGpu, nFrameEndGpu) * fToMsGPU;

		MicroProfilePrintf(CB, Handle, "Frames[%d] = MakeFrame(%d, %f, %f, %f, %f, ts%d, tt%d, ti%d, tl%d);\n", i, 0, fFrameMs, fFrameEndMs, fFrameGpuMs, fFrameGpuEndMs, i, i, i, i);
	}


	uint32_t nContextSwitchStart = 0;
	uint32_t nContextSwitchEnd = 0;
	MicroProfileContextSwitchSearch(&nContextSwitchStart, &nContextSwitchEnd, nTickStart, nTickEnd);

	MicroProfilePrintString(CB, Handle, "var CSwitchThreadInOutCpu = [\n");
	for(uint32_t j = nContextSwitchStart; j != nContextSwitchEnd; j = (j+1) % MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE)
	{
		MicroProfileContextSwitch CS = S.ContextSwitch[j];
		int nCpu = CS.nCpu;
		MicroProfilePrintUIntComma(CB, Handle, CS.nThreadIn);
		MicroProfilePrintUIntComma(CB, Handle, CS.nThreadOut);
		MicroProfilePrintUIntComma(CB, Handle, nCpu);
	}
	MicroProfilePrintString(CB, Handle, "];\n");

	MicroProfilePrintString(CB, Handle, "var CSwitchTime = [\n");
	float fToMsCpu = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
	for(uint32_t j = nContextSwitchStart; j != nContextSwitchEnd; j = (j+1) % MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE)
	{
		MicroProfileContextSwitch CS = S.ContextSwitch[j];
		float fTime = MicroProfileLogTickDifference(nTickStart, CS.nTicks) * fToMsCpu;
		MicroProfilePrintf(CB, Handle, "%f,", fTime);
	}
	MicroProfilePrintString(CB, Handle, "];\n");

	MicroProfileThreadInfo Threads[MICROPROFILE_MAX_CONTEXT_SWITCH_THREADS];
	uint32_t nNumThreadsBase = 0;
	uint32_t nNumThreads = MicroProfileContextSwitchGatherThreads(nContextSwitchStart, nContextSwitchEnd, Threads, &nNumThreadsBase);

	MicroProfilePrintString(CB, Handle, "var CSwitchThreads = {");

	for (uint32_t i = 0; i < nNumThreads; ++i)
	{
		char Name[256];
		const char* pProcessName = MicroProfileGetProcessName(Threads[i].nProcessId, Name, sizeof(Name));

		const char* p1 = i < nNumThreadsBase ? S.Pool[i]->ThreadName : "?";
		const char* p2 = pProcessName ? pProcessName : "?";

		MicroProfilePrintf(CB, Handle, "%lld:{\'tid\':%lld,\'pid\':%lld,\'t\':\'%s\',\'p\':\'%s\'},",
			(long long)Threads[i].nThreadId,
			(long long)Threads[i].nThreadId,
			(long long)Threads[i].nProcessId,
			p1, p2
			);
	}

	MicroProfilePrintString(CB, Handle, "};\n");

	for(size_t i = 0; i < g_MicroProfileHtml_end_count; ++i)
	{
		CB(Handle, g_MicroProfileHtml_end_sizes[i]-1, g_MicroProfileHtml_end[i]);
	}

	uint32_t* nGroupCounter = (uint32_t*)alloca(sizeof(uint32_t)* S.nGroupCount);

	memset(nGroupCounter, 0, sizeof(uint32_t) * S.nGroupCount);
	for(uint32_t i = 0; i < S.nTotalTimers; ++i)
	{
		uint32_t nGroupIndex = S.TimerInfo[i].nGroupIndex;
		nGroupCounter[nGroupIndex] += nTimerCounter[i];
	}

	uint32_t* nGroupCounterSort = (uint32_t*)alloca(sizeof(uint32_t)* S.nGroupCount);
	uint32_t* nTimerCounterSort = (uint32_t*)alloca(sizeof(uint32_t)* S.nTotalTimers);
	for(uint32_t i = 0; i < S.nGroupCount; ++i)
	{
		nGroupCounterSort[i] = i;
	}
	for(uint32_t i = 0; i < S.nTotalTimers; ++i)
	{
		nTimerCounterSort[i] = i;
	}
	std::sort(nGroupCounterSort, nGroupCounterSort + S.nGroupCount, 
		[nGroupCounter](const uint32_t l, const uint32_t r)
		{
			return nGroupCounter[l] > nGroupCounter[r];
		}
	);

	std::sort(nTimerCounterSort, nTimerCounterSort + S.nTotalTimers, 
		[nTimerCounter](const uint32_t l, const uint32_t r)
		{
			return nTimerCounter[l] > nTimerCounter[r];
		}
	);

	MicroProfilePrintf(CB, Handle, "\n<!--\nMarker Per Group\n");
	for(uint32_t i = 0; i < S.nGroupCount; ++i)
	{
		uint32_t idx = nGroupCounterSort[i];
		MicroProfilePrintf(CB, Handle, "%8d:%s\n", nGroupCounter[idx], S.GroupInfo[idx].pName);
	}
	MicroProfilePrintf(CB, Handle, "Marker Per Timer\n");
	for(uint32_t i = 0; i < S.nTotalTimers; ++i)
	{
		uint32_t idx = nTimerCounterSort[i];
		MicroProfilePrintf(CB, Handle, "%8d:%s(%s)\n", nTimerCounter[idx], S.TimerInfo[idx].pName, S.GroupInfo[S.TimerInfo[idx].nGroupIndex].pName);
	}
	MicroProfilePrintf(CB, Handle, "\n-->\n");

	S.nActiveGroup = nActiveGroup;
	S.nRunning = nRunning;

#if MICROPROFILE_DEBUG
	int64_t nTicksEnd = MP_TICK();
	float fMs = fToMsCpu * (nTicksEnd - S.nPauseTicks);
	printf("html dump took %6.2fms\n", fMs);
#endif
}
#else
void MicroProfileDumpHtml(MicroProfileWriteCallback CB, void* Handle, int nMaxFrames, const char* pHost)
{
	MicroProfilePrintString(CB, Handle, "HTML output is disabled because MICROPROFILE_EMBED_HTML is 0\n");
}
#endif

void MicroProfileWriteFile(void* Handle, size_t nSize, const char* pData)
{
	fwrite(pData, nSize, 1, (FILE*)Handle);
}

void MicroProfileDumpToFile()
{
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());

	FILE* F = fopen(S.DumpPath, "w");
	if(F)
	{
		if(S.eDumpType == MicroProfileDumpTypeHtml)
			MicroProfileDumpHtml(MicroProfileWriteFile, F, S.nDumpFrames, 0);
		else if(S.eDumpType == MicroProfileDumpTypeCsv)
			MicroProfileDumpCsv(MicroProfileWriteFile, F, S.nDumpFrames);

		fclose(F);
	}
}

#if MICROPROFILE_WEBSERVER
uint32_t MicroProfileWebServerPort()
{
	return S.nWebServerPort;
}

void MicroProfileSendSocket(MpSocket Socket, const char* pData, size_t nSize)
{
#ifdef MSG_NOSIGNAL
	int nFlags = MSG_NOSIGNAL;
#else
	int nFlags = 0;
#endif

	send(Socket, pData, (int)nSize, nFlags);
}

void MicroProfileFlushSocket(MpSocket Socket)
{
	if(S.nWebServerPut)
	{
		MicroProfileSendSocket(Socket, &S.WebServerBuffer[0], S.nWebServerPut);
		S.nWebServerPut = 0;
	}
}

void MicroProfileWriteSocket(void* Handle, size_t nSize, const char* pData)
{
	MpSocket Socket = *(MpSocket*)Handle;
	if(nSize > MICROPROFILE_WEBSERVER_SOCKET_BUFFER_SIZE / 2)
	{
		MicroProfileFlushSocket(Socket);
		MicroProfileSendSocket(Socket, pData, nSize);
	}
	else
	{
		memcpy(&S.WebServerBuffer[S.nWebServerPut], pData, nSize);
		S.nWebServerPut += (uint32_t)nSize;
		if(S.nWebServerPut > MICROPROFILE_WEBSERVER_SOCKET_BUFFER_SIZE/2)
		{
			MicroProfileFlushSocket(Socket);
		}
	}

	S.nWebServerDataSent += nSize;
}

#if MICROPROFILE_MINIZ
#ifndef MICROPROFILE_COMPRESS_BUFFER_SIZE
#define MICROPROFILE_COMPRESS_BUFFER_SIZE (256<<10)
#endif

#define MICROPROFILE_COMPRESS_CHUNK (MICROPROFILE_COMPRESS_BUFFER_SIZE/2)
struct MicroProfileCompressedSocketState
{
	unsigned char DeflateOut[MICROPROFILE_COMPRESS_CHUNK];
	unsigned char DeflateIn[MICROPROFILE_COMPRESS_CHUNK];
	mz_stream Stream;
	MpSocket Socket;
	uint32_t nSize;
	uint32_t nCompressedSize;
	uint32_t nFlushes;
	uint32_t nMemmoveBytes;
};

void MicroProfileCompressedSocketFlush(MicroProfileCompressedSocketState* pState)
{
	mz_stream& Stream = pState->Stream;
	unsigned char* pSendStart = &pState->DeflateOut[0];
	unsigned char* pSendEnd = &pState->DeflateOut[MICROPROFILE_COMPRESS_CHUNK - Stream.avail_out];
	if(pSendStart != pSendEnd)
	{
		MicroProfileSendSocket(pState->Socket, (char*)pSendStart, pSendEnd - pSendStart);
		pState->nCompressedSize += pSendEnd - pSendStart;
	}
	Stream.next_out = &pState->DeflateOut[0];
	Stream.avail_out = MICROPROFILE_COMPRESS_CHUNK;

}
void MicroProfileCompressedSocketStart(MicroProfileCompressedSocketState* pState, MpSocket Socket)
{
	mz_stream& Stream = pState->Stream;
	memset(&Stream, 0, sizeof(Stream));
	Stream.next_out = &pState->DeflateOut[0];
	Stream.avail_out = MICROPROFILE_COMPRESS_CHUNK;
	Stream.next_in = &pState->DeflateIn[0];
	Stream.avail_in = 0;
	mz_deflateInit(&Stream, MZ_DEFAULT_COMPRESSION);
	pState->Socket = Socket;
	pState->nSize = 0;
	pState->nCompressedSize = 0;
	pState->nFlushes = 0;
	pState->nMemmoveBytes = 0;

}
void MicroProfileCompressedSocketFinish(MicroProfileCompressedSocketState* pState)
{
	mz_stream& Stream = pState->Stream;
	MicroProfileCompressedSocketFlush(pState);
	int r = mz_deflate(&Stream, MZ_FINISH);
	MP_ASSERT(r == MZ_STREAM_END);
	MicroProfileCompressedSocketFlush(pState);
	r = mz_deflateEnd(&Stream);
	MP_ASSERT(r == MZ_OK);
}

void MicroProfileCompressedWriteSocket(void* Handle, size_t nSize, const char* pData)
{
	MicroProfileCompressedSocketState* pState = (MicroProfileCompressedSocketState*)Handle;
	mz_stream& Stream = pState->Stream;
	const unsigned char* pDeflateInEnd = Stream.next_in + Stream.avail_in;
	const unsigned char* pDeflateInStart = &pState->DeflateIn[0];
	const unsigned char* pDeflateInRealEnd = &pState->DeflateIn[MICROPROFILE_COMPRESS_CHUNK];	
	pState->nSize += nSize;
	if(nSize <= pDeflateInRealEnd - pDeflateInEnd)
	{
		memcpy((void*)pDeflateInEnd, pData, nSize);
		Stream.avail_in += nSize;
		MP_ASSERT(Stream.next_in + Stream.avail_in <= pDeflateInRealEnd);
		return;
	}
	int Flush = 0;
	while(nSize)
	{
		pDeflateInEnd = Stream.next_in + Stream.avail_in;
		if(Flush)
		{
			pState->nFlushes++;
			MicroProfileCompressedSocketFlush(pState);
			pDeflateInRealEnd = &pState->DeflateIn[MICROPROFILE_COMPRESS_CHUNK];	
			if(pDeflateInEnd == pDeflateInRealEnd)
			{
				if(Stream.avail_in)
				{
					MP_ASSERT(pDeflateInStart != Stream.next_in);
					memmove((void*)pDeflateInStart, Stream.next_in, Stream.avail_in);
					pState->nMemmoveBytes += Stream.avail_in;
				}
				Stream.next_in = pDeflateInStart;
				pDeflateInEnd = Stream.next_in + Stream.avail_in;
			}
		}
		size_t nSpace = pDeflateInRealEnd - pDeflateInEnd;
		size_t nBytes = MicroProfileMin(nSpace, nSize);
		MP_ASSERT(nBytes + pDeflateInEnd <= pDeflateInRealEnd);
		memcpy((void*)pDeflateInEnd, pData, nBytes); 
		Stream.avail_in += nBytes;
		nSize -= nBytes;
		pData += nBytes;
		int r = mz_deflate(&Stream, MZ_NO_FLUSH);
		Flush = r == MZ_BUF_ERROR || nBytes == 0 || Stream.avail_out == 0 ? 1 : 0;
		MP_ASSERT(r == MZ_BUF_ERROR || r == MZ_OK);
		if(r == MZ_BUF_ERROR)
		{
			r = mz_deflate(&Stream, MZ_SYNC_FLUSH);
		}
	}
}
#endif

void* MicroProfileWebServerUpdate(void*);
void MicroProfileWebServerUpdateStop();

void MicroProfileWebServerHello(int nPort)
{
	uint32_t nInterfaces = 0;

	// getifaddrs hangs on some versions of Android so disable IP address scanning
#if (defined(__APPLE__) || defined(__linux__)) && !defined(__ANDROID__)
	struct ifaddrs* ifal;
	if(getifaddrs(&ifal) == 0 && ifal)
	{
		for(struct ifaddrs* ifa = ifal; ifa; ifa = ifa->ifa_next)
		{
			if(ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET)
			{
				void* pAddress = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
				char Ip[INET_ADDRSTRLEN];
				if(inet_ntop(AF_INET, pAddress, Ip, sizeof(Ip)))
				{
					MICROPROFILE_PRINTF("MicroProfile: Web server started on %s:%d\n", Ip, nPort);
					nInterfaces++;
				}
			}
		}

		freeifaddrs(ifal);
	}
#endif

	if(nInterfaces == 0)
	{
		MICROPROFILE_PRINTF("MicroProfile: Web server started on port %d\n", nPort);
	}
}

void MicroProfileWebServerStart()
{
	if(!S.WebServerThread)
	{
		MicroProfileThreadStart(&S.WebServerThread, MicroProfileWebServerUpdate);
	}
}

void MicroProfileWebServerStop()
{
	if(S.WebServerThread)
	{
		MicroProfileWebServerUpdateStop();
		MicroProfileThreadJoin(&S.WebServerThread);
	}
}

const char* MicroProfileParseHeader(char* pRequest, const char* pPrefix)
{
	size_t nRequestSize = strlen(pRequest);
	size_t nPrefixSize = strlen(pPrefix);

	for(uint32_t i = 0; i < nRequestSize; ++i)
	{
		if((i == 0 || pRequest[i-1] == '\n') && strncmp(&pRequest[i], pPrefix, nPrefixSize) == 0)
		{
			char* pResult = &pRequest[i + nPrefixSize];
			size_t nResultSize = strcspn(pResult, " \r\n");

			pResult[nResultSize] = '\0';
			return pResult;
		}
	}

	return 0;
}

int MicroProfileParseGet(const char* pGet)
{
	const char* pStart = pGet;
	while(*pGet != '\0')
	{
		if(*pGet < '0' || *pGet > '9')
			return 0;
		pGet++;
	}
	int nFrames = atoi(pStart);
	if(nFrames)
	{
		return nFrames;
	}
	else
	{
		return MICROPROFILE_WEBSERVER_FRAMES;
	}
}

void MicroProfileWebServerHandleRequest(MpSocket Connection)
{
	char Request[8192];
	int nReceived = recv(Connection, Request, sizeof(Request)-1, 0);
	if(nReceived <= 0)
		return;
	Request[nReceived] = 0;

	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());

	MICROPROFILE_SCOPE(g_MicroProfileWebServerUpdate);

#if MICROPROFILE_MINIZ
#define MICROPROFILE_HTML_HEADER "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Encoding: deflate\r\nExpires: Tue, 01 Jan 2199 16:00:00 GMT\r\n\r\n"
#else
#define MICROPROFILE_HTML_HEADER "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nExpires: Tue, 01 Jan 2199 16:00:00 GMT\r\n\r\n"
#endif

	const char* pUrl = MicroProfileParseHeader(Request, "GET /");
	if(!pUrl)
		return;

	int nFrames = MicroProfileParseGet(pUrl);
	if(nFrames <= 0)
		return;

	const char* pHost = MicroProfileParseHeader(Request, "Host: ");

	uint64_t nTickStart = MP_TICK();
	MicroProfileSendSocket(Connection, MICROPROFILE_HTML_HEADER, sizeof(MICROPROFILE_HTML_HEADER)-1);
	uint64_t nDataStart = S.nWebServerDataSent;
	S.nWebServerPut = 0;
#if 0 == MICROPROFILE_MINIZ
	MicroProfileDumpHtml(MicroProfileWriteSocket, &Connection, nFrames, pHost);
	uint64_t nDataEnd = S.nWebServerDataSent;
	uint64_t nTickEnd = MP_TICK();
	uint64_t nDiff = (nTickEnd - nTickStart);
	float fMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu()) * nDiff;
	int nKb = ((nDataEnd-nDataStart)>>10) + 1;
	MicroProfilePrintf(MicroProfileWriteSocket, &Connection, "\n<!-- Sent %dkb in %.2fms-->\n\n",nKb, fMs);
	MicroProfileFlushSocket(Connection);
#else
	MicroProfileCompressedSocketState CompressState;
	MicroProfileCompressedSocketStart(&CompressState, Connection);
	MicroProfileDumpHtml(MicroProfileCompressedWriteSocket, &CompressState, nFrames, pHost);
	S.nWebServerDataSent += CompressState.nSize;
	uint64_t nDataEnd = S.nWebServerDataSent;
	uint64_t nTickEnd = MP_TICK();
	uint64_t nDiff = (nTickEnd - nTickStart);
	float fMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu()) * nDiff;
	int nKb = ((nDataEnd-nDataStart)>>10) + 1;
	int nCompressedKb = ((CompressState.nCompressedSize)>>10) + 1;
	MicroProfilePrintf(MicroProfileCompressedWriteSocket, &CompressState, "\n<!-- Sent %dkb(compressed %dkb) in %.2fms-->\n\n", nKb, nCompressedKb, fMs);
	MicroProfileCompressedSocketFinish(&CompressState);
	MicroProfileFlushSocket(Connection);
#endif
}

void MicroProfileWebServerCloseSocket(MpSocket Connection)
{
#ifdef _WIN32
	closesocket(Connection);
#else
	shutdown(Connection,SHUT_RDWR);
	close(Connection);
#endif
}

void* MicroProfileWebServerUpdate(void*)
{
#ifdef _WIN32
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2, 2), &wsa))
		return 0;
#endif

	S.WebServerSocket = socket(PF_INET, SOCK_STREAM, 6);
	MP_ASSERT(!MP_INVALID_SOCKET(S.WebServerSocket));

	uint32_t nPortBegin = MICROPROFILE_WEBSERVER_PORT;
	uint32_t nPortEnd = nPortBegin + 20;

	struct sockaddr_in Addr; 
	Addr.sin_family = AF_INET; 
	Addr.sin_addr.s_addr = INADDR_ANY; 
	for(uint32_t nPort = nPortBegin; nPort < nPortEnd; ++nPort)
	{
		Addr.sin_port = htons(nPort);
		if(0 == ::bind(S.WebServerSocket, (sockaddr*)&Addr, sizeof(Addr)))
		{
			S.nWebServerPort = nPort;
			break;
		}
	}

	if(S.nWebServerPort)
	{
		MicroProfileWebServerHello(S.nWebServerPort);

		listen(S.WebServerSocket, 8);

		for (;;)
		{
			MpSocket Connection = accept(S.WebServerSocket, 0, 0);
			if(MP_INVALID_SOCKET(Connection)) break;

		#ifdef SO_NOSIGPIPE
			int nConnectionOption = 1;
			setsockopt(Connection, SOL_SOCKET, SO_NOSIGPIPE, &nConnectionOption, sizeof(nConnectionOption));
		#endif

			MicroProfileWebServerHandleRequest(Connection);

			MicroProfileWebServerCloseSocket(Connection);
		}

		S.nWebServerPort = 0;
	}
	else
	{
		MICROPROFILE_PRINTF("MicroProfile: Web server could not start: no free ports in range [%d..%d)\n", nPortBegin, nPortEnd);
	}

#ifdef _WIN32
	WSACleanup();
#endif

	return 0;
}

void MicroProfileWebServerUpdateStop()
{
	MicroProfileWebServerCloseSocket(S.WebServerSocket);
}
#else
void MicroProfileWebServerStart()
{
}

void MicroProfileWebServerStop()
{
}

uint32_t MicroProfileWebServerPort()
{
	return 0;
}
#endif


#if MICROPROFILE_CONTEXT_SWITCH_TRACE
//functions that need to be implemented per platform.
void* MicroProfileTraceThread(void* unused);

void MicroProfileContextSwitchTraceStart()
{
	if(!S.ContextSwitchThread)
	{
		MicroProfileThreadStart(&S.ContextSwitchThread, MicroProfileTraceThread);
	}
}

void MicroProfileContextSwitchTraceStop()
{
	if(S.ContextSwitchThread)
	{
		S.bContextSwitchStop = true;
		MicroProfileThreadJoin(&S.ContextSwitchThread);
		S.bContextSwitchStop = false;
	}
}

void MicroProfileContextSwitchSearch(uint32_t* pContextSwitchStart, uint32_t* pContextSwitchEnd, uint64_t nBaseTicksCpu, uint64_t nBaseTicksEndCpu)
{
	MICROPROFILE_SCOPE(g_MicroProfileContextSwitchSearch);
	uint32_t nContextSwitchPut = S.nContextSwitchPut;
	uint64_t nContextSwitchStart, nContextSwitchEnd;
	nContextSwitchStart = nContextSwitchEnd = (nContextSwitchPut + MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE - 1) % MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE;		
	int64_t nSearchEnd = nBaseTicksEndCpu + MicroProfileMsToTick(30.f, MicroProfileTicksPerSecondCpu());
	int64_t nSearchBegin = nBaseTicksCpu - MicroProfileMsToTick(30.f, MicroProfileTicksPerSecondCpu());
	for(uint32_t i = 0; i < MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE; ++i)
	{
		uint32_t nIndex = (nContextSwitchPut + MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE - (i+1)) % MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE;
		MicroProfileContextSwitch& CS = S.ContextSwitch[nIndex];
		if(CS.nTicks > nSearchEnd)
		{
			nContextSwitchEnd = nIndex;
		}
		if(CS.nTicks > nSearchBegin)
		{
			nContextSwitchStart = nIndex;
		}
	}
	*pContextSwitchStart = nContextSwitchStart;
	*pContextSwitchEnd = nContextSwitchEnd;
}

uint32_t MicroProfileContextSwitchGatherThreads(uint32_t nContextSwitchStart, uint32_t nContextSwitchEnd, MicroProfileThreadInfo* Threads, uint32_t* nNumThreadsBase)
{
	MicroProfileProcessIdType nCurrentProcessId = MP_GETCURRENTPROCESSID();

	uint32_t nNumThreads = 0;
	for(uint32_t i = 0; i < MICROPROFILE_MAX_THREADS && S.Pool[i]; ++i)
	{
		Threads[nNumThreads].nProcessId = nCurrentProcessId;
		Threads[nNumThreads].nThreadId = S.Pool[i]->nThreadId;
		nNumThreads++;
	}

	*nNumThreadsBase = nNumThreads;

	for(uint32_t i = nContextSwitchStart; i != nContextSwitchEnd; i = (i+1) % MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE)
	{
		MicroProfileContextSwitch CS = S.ContextSwitch[i];
		MicroProfileThreadIdType nThreadId = CS.nThreadIn;
		if(nThreadId)
		{
			MicroProfileProcessIdType nProcessId = CS.nProcessIn;

			bool bSeen = false;
			for(uint32_t j = 0; j < nNumThreads; ++j)
			{
				if(Threads[j].nThreadId == nThreadId && Threads[j].nProcessId == nProcessId)
				{
					bSeen = true;
					break;
				}
			}
			if(!bSeen)
			{
				Threads[nNumThreads].nProcessId = nProcessId;
				Threads[nNumThreads].nThreadId = nThreadId;
				nNumThreads++;
			}
		}
		if(nNumThreads == MICROPROFILE_MAX_CONTEXT_SWITCH_THREADS)
		{
			break;
		}
	}

	return nNumThreads;
}

#if defined(_WIN32)
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

const char* MicroProfileGetProcessName(MicroProfileProcessIdType nId, char* Buffer, uint32_t nSize)
{
	if(HANDLE Handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, nId))
	{
		DWORD nResult = GetModuleBaseNameA(Handle, nullptr, Buffer, nSize);
		CloseHandle(Handle);

		return nResult ? Buffer : nullptr;
	}
	return nullptr;
}

void* MicroProfileTraceThread(void* unused)
{
	while(!S.bContextSwitchStop)
	{
		FILE* pFile = fopen("\\\\.\\pipe\\microprofile-contextswitch", "rb");
		if(!pFile)
		{
			Sleep(1000);
			continue;
		}

		S.bContextSwitchRunning = true;

		MicroProfileContextSwitch Buffer[1024];

		while(!ferror(pFile) && !S.bContextSwitchStop)
		{
			size_t nCount = fread(Buffer, sizeof(MicroProfileContextSwitch), ARRAYSIZE(Buffer), pFile);

			for(size_t i = 0; i < nCount; ++i)
				MicroProfileContextSwitchPut(&Buffer[i]);
		}

		fclose(pFile);

		S.bContextSwitchRunning = false;
	}

	return 0;
}
#elif defined(__APPLE__)
#include <sys/time.h>
#include <libproc.h>

const char* MicroProfileGetProcessName(MicroProfileProcessIdType nId, char* Buffer, uint32_t nSize)
{
	char Path[PATH_MAX];
	if(proc_pidpath(nId, Path, sizeof(Path)) == 0)
		return nullptr;

	char* pSlash = strrchr(Path, '/');
	char* pName = pSlash ? pSlash + 1 : Path;

	strncpy(Buffer, pName, nSize-1);
	Buffer[nSize-1] = 0;

	return Buffer;
}

void* MicroProfileTraceThread(void*)
{
	while(!S.bContextSwitchStop)
	{
		FILE* pFile = fopen("/tmp/microprofile-contextswitch", "r");
		if(!pFile)
		{
			usleep(1000000);
			continue;
		}

		S.bContextSwitchRunning = true;

		char* pLine = 0;
		size_t cap = 0;
		size_t len = 0;

		uint32_t nLastThread[MICROPROFILE_MAX_CONTEXT_SWITCH_THREADS] = {0};

		while((len = getline(&pLine, &cap, pFile))>0 && !S.bContextSwitchStop)
		{
			if(strncmp(pLine, "MPTD ", 5) != 0)
				continue;

			char* pos = pLine + 4;
			uint32_t cpu = strtol(pos + 1, &pos, 16);
			uint32_t pid = strtol(pos + 1, &pos, 16);
			uint32_t tid = strtol(pos + 1, &pos, 16);
			int64_t timestamp = strtoll(pos + 1, &pos, 16);

			if(cpu < MICROPROFILE_MAX_CONTEXT_SWITCH_THREADS)
			{
				MicroProfileContextSwitch Switch;

				Switch.nThreadOut = nLastThread[cpu];
				Switch.nThreadIn = tid;
				Switch.nProcessIn = pid;
				Switch.nCpu = cpu;
				Switch.nTicks = timestamp;
				MicroProfileContextSwitchPut(&Switch);

				nLastThread[cpu] = tid;
			}
		}

		fclose(pFile);

		S.bContextSwitchRunning = false;
	}

	return 0;
}
#endif
#else
void MicroProfileContextSwitchTraceStart()
{
}

void MicroProfileContextSwitchTraceStop()
{
}

void MicroProfileContextSwitchSearch(uint32_t* pContextSwitchStart, uint32_t* pContextSwitchEnd, uint64_t nBaseTicksCpu, uint64_t nBaseTicksEndCpu)
{
	(void)nBaseTicksCpu;
	(void)nBaseTicksEndCpu;

	*pContextSwitchStart = 0;
	*pContextSwitchEnd = 0;
}

uint32_t MicroProfileContextSwitchGatherThreads(uint32_t nContextSwitchStart, uint32_t nContextSwitchEnd, MicroProfileThreadInfo* Threads, uint32_t* nNumThreadsBase)
{
	(void)nContextSwitchStart;
	(void)nContextSwitchEnd;
	(void)Threads;

	*nNumThreadsBase = 0;
	return 0;
}

const char* MicroProfileGetProcessName(MicroProfileProcessIdType nId, char* Buffer, uint32_t nSize)
{
	(void)nId;
	(void)Buffer;
	(void)nSize;

	return nullptr;
}
#endif

void MicroProfileGpuShutdown()
{
	if(!S.GPU.Shutdown)
		return;

	S.GPU.Shutdown();

	memset(&S.GPU, 0, sizeof(S.GPU));
}

uint32_t MicroProfileGpuFlip()
{
	if(!S.GPU.Flip)
		return (uint32_t)-1;

	return S.GPU.Flip();
}

uint32_t MicroProfileGpuInsertTimer(void* pContext)
{
	if(!S.GPU.InsertTimer)
		return (uint32_t)-1;

	return S.GPU.InsertTimer(pContext);
}

uint64_t MicroProfileGpuGetTimeStamp(uint32_t nIndex)
{
	if(!S.GPU.GetTimeStamp)
		return MICROPROFILE_INVALID_TICK;

	if(nIndex == (uint32_t)-1)
		return MICROPROFILE_INVALID_TICK;

	return S.GPU.GetTimeStamp(nIndex);
}

uint64_t MicroProfileTicksPerSecondGpu()
{
	if (!S.GPU.GetTicksPerSecond)
		return 1000000000ll;

	return S.GPU.GetTicksPerSecond();
}

bool MicroProfileGetGpuTickReference(int64_t* pOutCpu, int64_t* pOutGpu)
{
	if(!S.GPU.GetTickReference)
		return false;

	return S.GPU.GetTickReference(pOutCpu, pOutGpu);
}

#define MICROPROFILE_GPU_STATE_DECL(API) \
	void MicroProfileGpuInitState##API(); \
	MicroProfileGpuTimerState##API g_MicroProfileGPU_##API;

#define MICROPROFILE_GPU_STATE_IMPL(API) \
	void MicroProfileGpuInitState##API() \
	{ \
		MP_ASSERT(!S.GPU.Shutdown); \
		memset(&g_MicroProfileGPU_##API, 0, sizeof(g_MicroProfileGPU_##API)); \
		S.GPU.Shutdown = MicroProfileGpuShutdown##API; \
		S.GPU.Flip = MicroProfileGpuFlip##API; \
		S.GPU.InsertTimer = MicroProfileGpuInsertTimer##API; \
		S.GPU.GetTimeStamp = MicroProfileGpuGetTimeStamp##API; \
		S.GPU.GetTicksPerSecond = MicroProfileTicksPerSecondGpu##API; \
		S.GPU.GetTickReference = MicroProfileGetGpuTickReference##API; \
	}

#if MICROPROFILE_GPU_TIMERS_D3D11
#ifndef D3D11_SDK_VERSION
#include <d3d11.h>
#endif

struct MicroProfileGpuTimerStateD3D11
{
	ID3D11DeviceContext* pDeviceContext;
	ID3D11Query* pQueries[MICROPROFILE_GPU_MAX_QUERIES];
	ID3D11Query* pRateQuery;
	ID3D11Query* pSyncQuery;

	uint64_t nFrame;
	std::atomic<uint32_t> nFramePut;

	uint32_t nSubmitted[MICROPROFILE_GPU_FRAMES];
	uint64_t nResults[MICROPROFILE_GPU_MAX_QUERIES];

	uint32_t nRateQueryIssue;
	uint64_t nQueryFrequency;
};

MICROPROFILE_GPU_STATE_DECL(D3D11)

void MicroProfileGpuInitD3D11(ID3D11Device* pDevice)
{
	MicroProfileGpuInitStateD3D11();

	MicroProfileGpuTimerStateD3D11& GPU = g_MicroProfileGPU_D3D11;

	pDevice->GetImmediateContext(&GPU.pDeviceContext);

	D3D11_QUERY_DESC Desc;
	Desc.MiscFlags = 0;
	Desc.Query = D3D11_QUERY_TIMESTAMP;
	for(uint32_t i = 0; i < MICROPROFILE_GPU_MAX_QUERIES; ++i)
	{
		HRESULT hr = pDevice->CreateQuery(&Desc, &GPU.pQueries[i]);
		MP_ASSERT(hr == S_OK);
	}

	HRESULT hr = pDevice->CreateQuery(&Desc, &GPU.pSyncQuery);
	MP_ASSERT(hr == S_OK);

	Desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
	hr = pDevice->CreateQuery(&Desc, &GPU.pRateQuery);
	MP_ASSERT(hr == S_OK);
}

void MicroProfileGpuShutdownD3D11()
{
	MicroProfileGpuTimerStateD3D11& GPU = g_MicroProfileGPU_D3D11;

	for(uint32_t i = 0; i < MICROPROFILE_GPU_MAX_QUERIES; ++i)
	{
		GPU.pQueries[i]->Release();
		GPU.pQueries[i] = 0;
	}

	GPU.pRateQuery->Release();
	GPU.pRateQuery = 0;

	GPU.pSyncQuery->Release();
	GPU.pSyncQuery = 0;

	GPU.pDeviceContext->Release();
	GPU.pDeviceContext = 0;
}

uint32_t MicroProfileGpuFlipD3D11()
{
	MicroProfileGpuTimerStateD3D11& GPU = g_MicroProfileGPU_D3D11;

	if (!GPU.pDeviceContext) return (uint32_t)-1;

	uint32_t nFrameQueries = MICROPROFILE_GPU_MAX_QUERIES / MICROPROFILE_GPU_FRAMES;

	// Submit current frame
	uint32_t nFrameIndex = GPU.nFrame % MICROPROFILE_GPU_FRAMES;
	uint32_t nFramePut = MicroProfileMin(GPU.nFramePut.load(), nFrameQueries);

	GPU.nSubmitted[nFrameIndex] = nFramePut;
	GPU.nFramePut.store(0);
	GPU.nFrame++;

	// Fetch frame results
	if (GPU.nFrame >= MICROPROFILE_GPU_FRAMES)
	{
		uint64_t nPendingFrame = GPU.nFrame - MICROPROFILE_GPU_FRAMES;
		uint32_t nPendingFrameIndex = nPendingFrame % MICROPROFILE_GPU_FRAMES;

		for(uint32_t i = 0; i < GPU.nSubmitted[nPendingFrameIndex]; ++i)
		{
			uint32_t nQueryIndex = nPendingFrameIndex * nFrameQueries + i;
			MP_ASSERT(nQueryIndex < MICROPROFILE_GPU_MAX_QUERIES);

			uint64_t nResult = 0;

			HRESULT hr;
			do hr = GPU.pDeviceContext->GetData(GPU.pQueries[nQueryIndex], &nResult, sizeof(nResult), 0);
			while(hr == S_FALSE);

			GPU.nResults[nQueryIndex] = (hr == S_OK) ? nResult : MICROPROFILE_INVALID_TICK;
		}
	}

	// Update timestamp frequency
	if(GPU.nRateQueryIssue == 0)
	{
		GPU.pDeviceContext->Begin(GPU.pRateQuery);
		GPU.nRateQueryIssue = 1;
	}
	else if(GPU.nRateQueryIssue == 1)
	{
		GPU.pDeviceContext->End(GPU.pRateQuery);
		GPU.nRateQueryIssue = 2;
	}
	else
	{
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT Result;
		if(S_OK == GPU.pDeviceContext->GetData(GPU.pRateQuery, &Result, sizeof(Result), D3D11_ASYNC_GETDATA_DONOTFLUSH))
		{
			GPU.nQueryFrequency = Result.Frequency;
			GPU.nRateQueryIssue = 0;
		}
	}

	return MicroProfileGpuInsertTimer(0);
}

uint32_t MicroProfileGpuInsertTimerD3D11(void* pContext)
{
	MicroProfileGpuTimerStateD3D11& GPU = g_MicroProfileGPU_D3D11;

	uint32_t nFrameQueries = MICROPROFILE_GPU_MAX_QUERIES / MICROPROFILE_GPU_FRAMES;

	uint32_t nIndex = GPU.nFramePut.fetch_add(1);
	if(nIndex >= nFrameQueries)
		return (uint32_t)-1;

	uint32_t nQueryIndex = (GPU.nFrame % MICROPROFILE_GPU_FRAMES) * nFrameQueries + nIndex;

	GPU.pDeviceContext->End(GPU.pQueries[nQueryIndex]);

	return nQueryIndex;
}

uint64_t MicroProfileGpuGetTimeStampD3D11(uint32_t nIndex)
{
	MicroProfileGpuTimerStateD3D11& GPU = g_MicroProfileGPU_D3D11;

	return GPU.nResults[nIndex];
}

uint64_t MicroProfileTicksPerSecondGpuD3D11()
{
	MicroProfileGpuTimerStateD3D11& GPU = g_MicroProfileGPU_D3D11;

	return GPU.nQueryFrequency ? GPU.nQueryFrequency : 1000000000ll;
}

bool MicroProfileGetGpuTickReferenceD3D11(int64_t* pOutCpu, int64_t* pOutGpu)
{
	MicroProfileGpuTimerStateD3D11& GPU = g_MicroProfileGPU_D3D11;

	GPU.pDeviceContext->End(GPU.pSyncQuery);

	uint64_t nResult = 0;

	HRESULT hr;
	do hr = GPU.pDeviceContext->GetData(GPU.pSyncQuery, &nResult, sizeof(nResult), 0);
	while(hr == S_FALSE);

	if (hr != S_OK) return false;

	*pOutCpu = MP_TICK();
	*pOutGpu = nResult;

	return true;
}

MICROPROFILE_GPU_STATE_IMPL(D3D11)
#endif

#if MICROPROFILE_GPU_TIMERS_D3D12
#ifndef D3D12_MAJOR_VERSION
#include <d3d12.h>
#endif

struct MicroProfileGpuTimerStateD3D12
{
	ID3D12CommandQueue* pCommandQueue;
	ID3D12QueryHeap* pHeap;
	ID3D12Resource* pBuffer;
	ID3D12GraphicsCommandList* pCommandLists[MICROPROFILE_GPU_FRAMES];
	ID3D12CommandAllocator* pCommandAllocators[MICROPROFILE_GPU_FRAMES];
	ID3D12Fence* pFence;
	void* pFenceEvent;

	uint64_t nFrame;
	std::atomic<uint32_t> nFramePut;

	uint32_t nSubmitted[MICROPROFILE_GPU_FRAMES];
	uint64_t nResults[MICROPROFILE_GPU_MAX_QUERIES];
	uint64_t nQueryFrequency;
};

MICROPROFILE_GPU_STATE_DECL(D3D12)

void MicroProfileGpuInitD3D12(ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue)
{
	MicroProfileGpuInitStateD3D12();

	MicroProfileGpuTimerStateD3D12& GPU = g_MicroProfileGPU_D3D12;

	GPU.pCommandQueue = pCommandQueue;

	HRESULT hr;

	D3D12_QUERY_HEAP_DESC HeapDesc;
	HeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	HeapDesc.Count = MICROPROFILE_GPU_MAX_QUERIES;
	HeapDesc.NodeMask = 0;

	D3D12_HEAP_PROPERTIES HeapProperties;
	HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
	HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProperties.CreationNodeMask = 1;
	HeapProperties.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC ResourceDesc;
	ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	ResourceDesc.Alignment = 0;
	ResourceDesc.Width = MICROPROFILE_GPU_MAX_QUERIES * sizeof(uint64_t);
	ResourceDesc.Height = 1;
	ResourceDesc.DepthOrArraySize = 1;
	ResourceDesc.MipLevels = 1;
	ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	ResourceDesc.SampleDesc.Count = 1;
	ResourceDesc.SampleDesc.Quality = 0;
	ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	hr = pDevice->CreateQueryHeap(&HeapDesc, IID_PPV_ARGS(&GPU.pHeap));
	MP_ASSERT(hr == S_OK);
	hr = pDevice->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&GPU.pBuffer));
	MP_ASSERT(hr == S_OK);
	hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&GPU.pFence));
	MP_ASSERT(hr == S_OK);
	GPU.pFenceEvent = CreateEvent(nullptr, false, false, nullptr);
	MP_ASSERT(GPU.pFenceEvent != INVALID_HANDLE_VALUE);

	for (uint32_t i = 0; i < MICROPROFILE_GPU_FRAMES; ++i)
	{
		hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&GPU.pCommandAllocators[i]));
		MP_ASSERT(hr == S_OK);
		hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, GPU.pCommandAllocators[i], nullptr, IID_PPV_ARGS(&GPU.pCommandLists[i]));
		MP_ASSERT(hr == S_OK);
		hr = GPU.pCommandLists[i]->Close();
		MP_ASSERT(hr == S_OK);
	}

	hr = pCommandQueue->GetTimestampFrequency(&GPU.nQueryFrequency);
	MP_ASSERT(hr == S_OK);
}

void MicroProfileGpuShutdownD3D12()
{
	MicroProfileGpuTimerStateD3D12& GPU = g_MicroProfileGPU_D3D12;

	if(!GPU.pCommandQueue)
		return;

	if (GPU.nFrame > 0)
	{
		GPU.pFence->SetEventOnCompletion(GPU.nFrame, GPU.pFenceEvent);
		WaitForSingleObject(GPU.pFenceEvent, INFINITE);
	}

	for (uint32_t i = 0; i < MICROPROFILE_GPU_FRAMES; ++i)
	{
		GPU.pCommandLists[i]->Release();
		GPU.pCommandLists[i] = 0;

		GPU.pCommandAllocators[i]->Release();
		GPU.pCommandAllocators[i] = 0;
	}

	GPU.pHeap->Release();
	GPU.pHeap = 0;

	GPU.pBuffer->Release();
	GPU.pBuffer = 0;

	GPU.pFence->Release();
	GPU.pFence = 0;

	CloseHandle(GPU.pFenceEvent);
	GPU.pFenceEvent = 0;

	GPU.pCommandQueue = 0;
}

uint32_t MicroProfileGpuFlipD3D12()
{
	MicroProfileGpuTimerStateD3D12& GPU = g_MicroProfileGPU_D3D12;

	uint32_t nFrameQueries = MICROPROFILE_GPU_MAX_QUERIES / MICROPROFILE_GPU_FRAMES;

	// Submit current frame
	uint32_t nFrameIndex = GPU.nFrame % MICROPROFILE_GPU_FRAMES;
	uint32_t nFrameStart = nFrameIndex * nFrameQueries;

	ID3D12CommandAllocator* pCommandAllocator = GPU.pCommandAllocators[nFrameIndex];
	ID3D12GraphicsCommandList* pCommandList = GPU.pCommandLists[nFrameIndex];

	pCommandAllocator->Reset();
	pCommandList->Reset(pCommandAllocator, nullptr);

	uint32_t nFrameTimeStamp = MicroProfileGpuInsertTimer(pCommandList);

	uint32_t nFramePut = MicroProfileMin(GPU.nFramePut.load(), nFrameQueries);

	if (nFramePut)
		pCommandList->ResolveQueryData(GPU.pHeap, D3D12_QUERY_TYPE_TIMESTAMP, nFrameStart, nFramePut, GPU.pBuffer, nFrameStart * sizeof(int64_t));

	pCommandList->Close();

	ID3D12CommandList* pList = pCommandList;
	GPU.pCommandQueue->ExecuteCommandLists(1, &pList);
	GPU.pCommandQueue->Signal(GPU.pFence, GPU.nFrame + 1);

	GPU.nSubmitted[nFrameIndex] = nFramePut;
	GPU.nFramePut.store(0);
	GPU.nFrame++;

	// Fetch frame results
	if (GPU.nFrame >= MICROPROFILE_GPU_FRAMES)
	{
		uint64_t nPendingFrame = GPU.nFrame - MICROPROFILE_GPU_FRAMES;
		uint32_t nPendingFrameIndex = nPendingFrame % MICROPROFILE_GPU_FRAMES;

		GPU.pFence->SetEventOnCompletion(nPendingFrame + 1, GPU.pFenceEvent);
		WaitForSingleObject(GPU.pFenceEvent, INFINITE);

		uint32_t nPendingFrameStart = nPendingFrameIndex * nFrameQueries;
		uint32_t nPendingFrameCount = GPU.nSubmitted[nPendingFrameIndex];

		if (nPendingFrameCount)
		{
			void* pData = 0;
			D3D12_RANGE Range = { nPendingFrameStart * sizeof(uint64_t), (nPendingFrameStart + nPendingFrameCount) * sizeof(uint64_t) };

			HRESULT hr = GPU.pBuffer->Map(0, &Range, &pData);
			MP_ASSERT(hr == S_OK);

			memcpy(&GPU.nResults[nPendingFrameStart], (uint64_t*)pData + nPendingFrameStart, nPendingFrameCount * sizeof(uint64_t));

			GPU.pBuffer->Unmap(0, 0);
		}
	}

	return nFrameTimeStamp;
}

uint32_t MicroProfileGpuInsertTimerD3D12(void* pContext)
{
	MicroProfileGpuTimerStateD3D12& GPU = g_MicroProfileGPU_D3D12;

	if (!pContext) return (uint32_t)-1;

	uint32_t nFrameQueries = MICROPROFILE_GPU_MAX_QUERIES / MICROPROFILE_GPU_FRAMES;

	uint32_t nIndex = GPU.nFramePut.fetch_add(1);
	if(nIndex >= nFrameQueries) return (uint32_t)-1;

	uint32_t nQueryIndex = (GPU.nFrame % MICROPROFILE_GPU_FRAMES) * nFrameQueries + nIndex;

	((ID3D12GraphicsCommandList*)pContext)->EndQuery(GPU.pHeap, D3D12_QUERY_TYPE_TIMESTAMP, nQueryIndex);

	return nQueryIndex;
}

uint64_t MicroProfileGpuGetTimeStampD3D12(uint32_t nIndex)
{
	MicroProfileGpuTimerStateD3D12& GPU = g_MicroProfileGPU_D3D12;

	return GPU.nResults[nIndex];
}

uint64_t MicroProfileTicksPerSecondGpuD3D12()
{
	MicroProfileGpuTimerStateD3D12& GPU = g_MicroProfileGPU_D3D12;

	return GPU.nQueryFrequency ? GPU.nQueryFrequency : 1000000000ll;
}

bool MicroProfileGetGpuTickReferenceD3D12(int64_t* pOutCpu, int64_t* pOutGpu)
{
	MicroProfileGpuTimerStateD3D12& GPU = g_MicroProfileGPU_D3D12;

	return SUCCEEDED(GPU.pCommandQueue->GetClockCalibration((uint64_t*)pOutGpu, (uint64_t*)pOutCpu));
}

MICROPROFILE_GPU_STATE_IMPL(D3D12)
#endif

#if MICROPROFILE_GPU_TIMERS_GL
#ifndef GL_TIMESTAMP
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#error You must include OpenGL headers for GPU timers to work
#endif
#endif

struct MicroProfileGpuTimerStateGL
{
	int32_t nTimestampBits;
	uint32_t nQueries[MICROPROFILE_GPU_MAX_QUERIES];

	uint64_t nFrame;
	std::atomic<uint32_t> nFramePut;

	uint64_t nTimerOffset[MICROPROFILE_GPU_FRAMES];
	uint32_t nSubmitted[MICROPROFILE_GPU_FRAMES];
	uint64_t nResults[MICROPROFILE_GPU_MAX_QUERIES];
};

MICROPROFILE_GPU_STATE_DECL(GL)

void MicroProfileGpuInitGL()
{
	MicroProfileGpuInitStateGL();

	MicroProfileGpuTimerStateGL& GPU = g_MicroProfileGPU_GL;

	glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &GPU.nTimestampBits);

#ifdef __APPLE__
	// OSX GL driver (incorrectly) issues GL_INVALID_ENUM when querying the timestamp bits
	glGetError();
#endif

	glGenQueries(MICROPROFILE_GPU_MAX_QUERIES, &GPU.nQueries[0]);
}

void MicroProfileGpuShutdownGL()
{
	MicroProfileGpuTimerStateGL& GPU = g_MicroProfileGPU_GL;

	glDeleteQueries(MICROPROFILE_GPU_MAX_QUERIES, &GPU.nQueries[0]);
}

uint32_t MicroProfileGpuFlipGL()
{
	MicroProfileGpuTimerStateGL& GPU = g_MicroProfileGPU_GL;

	uint32_t nFrameQueries = MICROPROFILE_GPU_MAX_QUERIES / MICROPROFILE_GPU_FRAMES;

	// Submit current frame
	uint32_t nFrameIndex = GPU.nFrame % MICROPROFILE_GPU_FRAMES;
	uint32_t nFramePut = MicroProfileMin(GPU.nFramePut.load(), nFrameQueries);

	if(!GPU.nTimestampBits && nFramePut > 0)
		glEndQuery(GL_TIME_ELAPSED);

	GPU.nTimerOffset[nFrameIndex] = MP_TICK() * (double(MicroProfileTicksPerSecondGpu()) / double(MicroProfileTicksPerSecondCpu()));
	GPU.nSubmitted[nFrameIndex] = nFramePut;
	GPU.nFramePut.store(0);
	GPU.nFrame++;

	// Fetch frame results
	if (GPU.nFrame >= MICROPROFILE_GPU_FRAMES)
	{
		uint64_t nPendingFrame = GPU.nFrame - MICROPROFILE_GPU_FRAMES;
		uint32_t nPendingFrameIndex = nPendingFrame % MICROPROFILE_GPU_FRAMES;
		uint64_t nTimerOffset = GPU.nTimerOffset[nPendingFrameIndex];

		for(uint32_t i = 0; i < GPU.nSubmitted[nPendingFrameIndex]; ++i)
		{
			uint32_t nQueryIndex = nPendingFrameIndex * nFrameQueries + i;
			MP_ASSERT(nQueryIndex < MICROPROFILE_GPU_MAX_QUERIES);

			uint64_t nResult = 0;
			glGetQueryObjectui64v(GPU.nQueries[nQueryIndex], GL_QUERY_RESULT, &nResult);

			if(GPU.nTimestampBits)
			{
				GPU.nResults[nQueryIndex] = nResult;
			}
			else
			{
				GPU.nResults[nQueryIndex] = nTimerOffset;
				nTimerOffset += nResult;
			}
		}
	}

	return MicroProfileGpuInsertTimer(0);
}

uint32_t MicroProfileGpuInsertTimerGL(void* pContext)
{
	MicroProfileGpuTimerStateGL& GPU = g_MicroProfileGPU_GL;

	(void)pContext;

	uint32_t nFrameQueries = MICROPROFILE_GPU_MAX_QUERIES / MICROPROFILE_GPU_FRAMES;

	uint32_t nIndex = GPU.nFramePut.fetch_add(1);
	if(nIndex >= nFrameQueries) return (uint32_t)-1;

	uint32_t nQueryIndex = (GPU.nFrame % MICROPROFILE_GPU_FRAMES) * nFrameQueries + nIndex;

	if(!GPU.nTimestampBits && nIndex > 0)
		glEndQuery(GL_TIME_ELAPSED);

	if(GPU.nTimestampBits)
		glQueryCounter(GPU.nQueries[nQueryIndex], GL_TIMESTAMP);
	else
		glBeginQuery(GL_TIME_ELAPSED, GPU.nQueries[nQueryIndex]);

	return nQueryIndex;
}

uint64_t MicroProfileGpuGetTimeStampGL(uint32_t nIndex)
{
	MicroProfileGpuTimerStateGL& GPU = g_MicroProfileGPU_GL;

	return GPU.nResults[nIndex];
}

uint64_t MicroProfileTicksPerSecondGpuGL()
{
	return 1000000000ll;
}

bool MicroProfileGetGpuTickReferenceGL(int64_t* pOutCpu, int64_t* pOutGpu)
{
	MicroProfileGpuTimerStateGL& GPU = g_MicroProfileGPU_GL;

	if(GPU.nTimestampBits)
	{
		int64_t nGpuTimeStamp = 0;
		glGetInteger64v(GL_TIMESTAMP, &nGpuTimeStamp);

		if(nGpuTimeStamp)
		{
			*pOutCpu = MP_TICK();
			*pOutGpu = nGpuTimeStamp;
			return true;
		}

		return false;
	}
	else
	{
		*pOutCpu = MP_TICK();
		*pOutGpu = MP_TICK() * (double(MicroProfileTicksPerSecondGpu()) / double(MicroProfileTicksPerSecondCpu()));
		return true;
	}
}

MICROPROFILE_GPU_STATE_IMPL(GL)
#endif

#if MICROPROFILE_GPU_TIMERS_VK
#ifndef VK_HEADER_VERSION
#include <vulkan/vulkan.h>
#endif

struct MicroProfileGpuTimerStateVK
{
	VkDevice pDevice;
	VkQueue pQueue;
	VkQueryPool pQueryPool;
	VkCommandPool pCommandPool;
	VkCommandBuffer pCommandBuffers[MICROPROFILE_GPU_FRAMES];
	VkFence pFences[MICROPROFILE_GPU_FRAMES];

	VkCommandBuffer pReferenceCommandBuffer;
	uint32_t nReferenceQuery;

	uint64_t nFrame;
	std::atomic<uint32_t> nFramePut;

	uint32_t nSubmitted[MICROPROFILE_GPU_FRAMES];
	uint64_t nResults[MICROPROFILE_GPU_MAX_QUERIES];
	uint64_t nQueryFrequency;
};

MICROPROFILE_GPU_STATE_DECL(VK)

void MicroProfileGpuInitVK(VkDevice pDevice, VkPhysicalDevice pPhysicalDevice, VkQueue pQueue)
{
	MicroProfileGpuInitStateVK();

	MicroProfileGpuTimerStateVK& GPU = g_MicroProfileGPU_VK;

	VkPhysicalDeviceProperties Properties;
	vkGetPhysicalDeviceProperties(pPhysicalDevice, &Properties);

	GPU.pDevice = pDevice;
	GPU.pQueue = pQueue;

	VkQueryPoolCreateInfo queryPoolInfo = {};
	queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryPoolInfo.queryCount = MICROPROFILE_GPU_MAX_QUERIES + 1; // reference query

	VkResult res = vkCreateQueryPool(pDevice, &queryPoolInfo, nullptr, &GPU.pQueryPool);
	MP_ASSERT(res == VK_SUCCESS);

	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolInfo.queueFamilyIndex = 0;

	res = vkCreateCommandPool(pDevice, &commandPoolInfo, nullptr, &GPU.pCommandPool);
	MP_ASSERT(res == VK_SUCCESS);

	VkCommandBuffer pCommandBuffers[MICROPROFILE_GPU_FRAMES + 1] = {};

	VkCommandBufferAllocateInfo commandBufferInfo = {};
	commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferInfo.commandPool = GPU.pCommandPool;
	commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferInfo.commandBufferCount = sizeof(pCommandBuffers) / sizeof(pCommandBuffers[0]);

	res = vkAllocateCommandBuffers(pDevice, &commandBufferInfo, pCommandBuffers);
	MP_ASSERT(res == VK_SUCCESS);

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	for (uint32_t i = 0; i < MICROPROFILE_GPU_FRAMES; ++i)
	{
		GPU.pCommandBuffers[i] = pCommandBuffers[i];

		res = vkCreateFence(pDevice, &fenceInfo, nullptr, &GPU.pFences[i]);
		MP_ASSERT(res == VK_SUCCESS);
	}

	GPU.pReferenceCommandBuffer = pCommandBuffers[MICROPROFILE_GPU_FRAMES];
	GPU.nReferenceQuery = MICROPROFILE_GPU_MAX_QUERIES; // reference query

	GPU.nQueryFrequency = 1e9 / Properties.limits.timestampPeriod;
}

void MicroProfileGpuShutdownVK()
{
	MicroProfileGpuTimerStateVK& GPU = g_MicroProfileGPU_VK;

	if (GPU.nFrame > 0)
	{
		uint32_t nFrameIndex = (GPU.nFrame - 1) % MICROPROFILE_GPU_FRAMES;

		VkResult res = vkWaitForFences(GPU.pDevice, 1, &GPU.pFences[nFrameIndex], VK_TRUE, UINT64_MAX);
		MP_ASSERT(res == VK_SUCCESS);
	}

	for (uint32_t i = 0; i < MICROPROFILE_GPU_FRAMES; ++i)
	{
		vkDestroyFence(GPU.pDevice, GPU.pFences[i], nullptr);
		GPU.pFences[i] = 0;
	}

	vkDestroyCommandPool(GPU.pDevice, GPU.pCommandPool, nullptr);
	memset(GPU.pCommandBuffers, 0, sizeof(GPU.pCommandBuffers));
	GPU.pCommandPool = 0;

	vkDestroyQueryPool(GPU.pDevice, GPU.pQueryPool, nullptr);
	GPU.pQueryPool = 0;

	GPU.pQueue = 0;
	GPU.pDevice = 0;
}

uint32_t MicroProfileGpuFlipVK()
{
	MicroProfileGpuTimerStateVK& GPU = g_MicroProfileGPU_VK;

	uint32_t nFrameQueries = MICROPROFILE_GPU_MAX_QUERIES / MICROPROFILE_GPU_FRAMES;

	// Submit current frame
	uint32_t nFrameIndex = GPU.nFrame % MICROPROFILE_GPU_FRAMES;
	uint32_t nFrameStart = nFrameIndex * nFrameQueries;

	VkCommandBuffer pCommandBuffer = GPU.pCommandBuffers[nFrameIndex];

	VkCommandBufferBeginInfo commandBufferBeginInfo = {};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkResult res = vkBeginCommandBuffer(pCommandBuffer, &commandBufferBeginInfo);
	MP_ASSERT(res == VK_SUCCESS);

	uint32_t nFrameTimeStamp = MicroProfileGpuInsertTimer(pCommandBuffer);
	uint32_t nFramePut = MicroProfileMin(GPU.nFramePut.load(), nFrameQueries);

	res = vkEndCommandBuffer(pCommandBuffer);
	MP_ASSERT(res == VK_SUCCESS);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &pCommandBuffer;

	res = vkQueueSubmit(GPU.pQueue, 1, &submitInfo, GPU.pFences[nFrameIndex]);
	MP_ASSERT(res == VK_SUCCESS);

	GPU.nSubmitted[nFrameIndex] = nFramePut;
	GPU.nFramePut.store(0);
	GPU.nFrame++;

	// Fetch frame results
	if (GPU.nFrame >= MICROPROFILE_GPU_FRAMES)
	{
		uint64_t nPendingFrame = GPU.nFrame - MICROPROFILE_GPU_FRAMES;
		uint32_t nPendingFrameIndex = nPendingFrame % MICROPROFILE_GPU_FRAMES;

		res = vkWaitForFences(GPU.pDevice, 1, &GPU.pFences[nPendingFrameIndex], VK_TRUE, UINT64_MAX);
		MP_ASSERT(res == VK_SUCCESS);

		res = vkResetFences(GPU.pDevice, 1, &GPU.pFences[nPendingFrameIndex]);
		MP_ASSERT(res == VK_SUCCESS);

		uint32_t nPendingFrameStart = nPendingFrameIndex * nFrameQueries;
		uint32_t nPendingFrameCount = GPU.nSubmitted[nPendingFrameIndex];

		if (nPendingFrameCount)
		{
			res = vkGetQueryPoolResults(GPU.pDevice, GPU.pQueryPool,
				nPendingFrameStart, nPendingFrameCount,
				nPendingFrameCount * sizeof(uint64_t), &GPU.nResults[nPendingFrameStart],
				sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
			MP_ASSERT(res == VK_SUCCESS);
		}
	}

	return nFrameTimeStamp;
}

uint32_t MicroProfileGpuInsertTimerVK(void* pContext)
{
	MicroProfileGpuTimerStateVK& GPU = g_MicroProfileGPU_VK;

	uint32_t nFrameQueries = MICROPROFILE_GPU_MAX_QUERIES / MICROPROFILE_GPU_FRAMES;

	uint32_t nIndex = GPU.nFramePut.fetch_add(1);
	if(nIndex >= nFrameQueries) return (uint32_t)-1;

	uint32_t nQueryIndex = (GPU.nFrame % MICROPROFILE_GPU_FRAMES) * nFrameQueries + nIndex;

	vkCmdWriteTimestamp((VkCommandBuffer)pContext, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, GPU.pQueryPool, nQueryIndex);

	return nQueryIndex;
}

uint64_t MicroProfileGpuGetTimeStampVK(uint32_t nIndex)
{
	MicroProfileGpuTimerStateVK& GPU = g_MicroProfileGPU_VK;

	return GPU.nResults[nIndex];
}

uint64_t MicroProfileTicksPerSecondGpuVK()
{
	MicroProfileGpuTimerStateVK& GPU = g_MicroProfileGPU_VK;

	return GPU.nQueryFrequency ? GPU.nQueryFrequency : 1000000000ll;
}

bool MicroProfileGetGpuTickReferenceVK(int64_t* pOutCpu, int64_t* pOutGpu)
{
	MicroProfileGpuTimerStateVK& GPU = g_MicroProfileGPU_VK;

	VkCommandBuffer pCommandBuffer = GPU.pReferenceCommandBuffer;
	uint32_t nQueryIndex = GPU.nReferenceQuery;

	VkCommandBufferBeginInfo commandBufferBeginInfo = {};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkResult res = vkBeginCommandBuffer(pCommandBuffer, &commandBufferBeginInfo);
	MP_ASSERT(res == VK_SUCCESS);

	vkCmdWriteTimestamp(pCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, GPU.pQueryPool, nQueryIndex);

	res = vkEndCommandBuffer(pCommandBuffer);
	MP_ASSERT(res == VK_SUCCESS);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &pCommandBuffer;

	res = vkQueueSubmit(GPU.pQueue, 1, &submitInfo, VK_NULL_HANDLE);
	MP_ASSERT(res == VK_SUCCESS);

	res = vkQueueWaitIdle(GPU.pQueue);
	MP_ASSERT(res == VK_SUCCESS);

	*pOutCpu = MP_TICK();

	res = vkGetQueryPoolResults(GPU.pDevice, GPU.pQueryPool, nQueryIndex, 1, sizeof(uint64_t), pOutGpu, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
	MP_ASSERT(res == VK_SUCCESS);

	return true;
}

MICROPROFILE_GPU_STATE_IMPL(VK)
#endif

#undef S

#ifdef _WIN32
#pragma warning(pop)
#endif

#if MICROPROFILE_EMBED_HTML
#include "microprofilehtml.h"
#endif //MICROPROFILE_EMBED_HTML

#endif
#endif
