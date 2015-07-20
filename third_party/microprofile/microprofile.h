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
//
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
//  Gpu time stamps:
// 		uint32_t MicroProfileGpuInsertTimeStamp();
// 		uint64_t MicroProfileGpuGetTimeStamp(uint32_t nKey);
// 		uint64_t MicroProfileTicksPerSecondGpu();
//  threading:
//      const char* MicroProfileGetThreadName(); Threadnames in detailed view


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
#define MICROPROFILE_DEFINE_GPU(var, group, name, color)
#define MICROPROFILE_SCOPE(var) do{}while(0)
#define MICROPROFILE_SCOPEI(group, name, color) do{}while(0)
#define MICROPROFILE_SCOPEGPU(var) do{}while(0)
#define MICROPROFILE_SCOPEGPUI(group, name, color) do{}while(0)
#define MICROPROFILE_META_CPU(name, count)
#define MICROPROFILE_META_GPU(name, count)
#define MICROPROFILE_FORCEENABLECPUGROUP(s) do{} while(0)
#define MICROPROFILE_FORCEDISABLECPUGROUP(s) do{} while(0)
#define MICROPROFILE_FORCEENABLEGPUGROUP(s) do{} while(0)
#define MICROPROFILE_FORCEDISABLEGPUGROUP(s) do{} while(0)
#define MICROPROFILE_SCOPE_TOKEN(token)

#define MicroProfileGetTime(group, name) 0.f
#define MicroProfileOnThreadCreate(foo) do{}while(0)
#define MicroProfileFlip() do{}while(0)
#define MicroProfileSetAggregateFrames(a) do{}while(0)
#define MicroProfileGetAggregateFrames() 0
#define MicroProfileGetCurrentAggregateFrames() 0
#define MicroProfileTogglePause() do{}while(0)
#define MicroProfileToggleAllGroups() do{} while(0)
#define MicroProfileDumpTimers() do{}while(0)
#define MicroProfileShutdown() do{}while(0)
#define MicroProfileSetForceEnable(a) do{} while(0)
#define MicroProfileGetForceEnable() false
#define MicroProfileSetEnableAllGroups(a) do{} while(0)
#define MicroProfileGetEnableAllGroups() false
#define MicroProfileSetForceMetaCounters(a)
#define MicroProfileGetForceMetaCounters() 0
#define MicroProfileDumpHtml(c) do{} while(0)
#define MicroProfileWebServerPort() ((uint32_t)-1)

#else

#include <stdint.h>
#include <string.h>
#include <thread>
#include <mutex>
#include <atomic>

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
#if TARGET_OS_IPHONE
#define MICROPROFILE_IOS
#endif

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

#define MP_BREAK() __builtin_trap()
#define MP_THREAD_LOCAL __thread
#define MP_STRCASECMP strcasecmp
#define MP_GETCURRENTTHREADID() (uint64_t)pthread_self()
typedef uint64_t ThreadIdType;

#elif defined(_WIN32)
int64_t MicroProfileGetTick();
#define MP_TICK() MicroProfileGetTick()
#define MP_BREAK() __debugbreak()
#define MP_THREAD_LOCAL __declspec(thread)
#define MP_STRCASECMP _stricmp
#define MP_GETCURRENTTHREADID() GetCurrentThreadId()
typedef uint32_t ThreadIdType;

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
#define MP_THREAD_LOCAL __thread
#define MP_STRCASECMP strcasecmp
#define MP_GETCURRENTTHREADID() (uint64_t)pthread_self()
typedef uint64_t ThreadIdType;
#endif

#ifndef MP_GETCURRENTTHREADID
#define MP_GETCURRENTTHREADID() 0
typedef uint32_t ThreadIdType;
#endif


#define MP_ASSERT(a) do{if(!(a)){MP_BREAK();} }while(0)
#define MICROPROFILE_DECLARE(var) extern MicroProfileToken g_mp_##var
#define MICROPROFILE_DEFINE(var, group, name, color) MicroProfileToken g_mp_##var = MicroProfileGetToken(group, name, color, MicroProfileTokenTypeCpu)
#define MICROPROFILE_DECLARE_GPU(var) extern MicroProfileToken g_mp_##var
#define MICROPROFILE_DEFINE_GPU(var, group, name, color) MicroProfileToken g_mp_##var = MicroProfileGetToken(group, name, color, MicroProfileTokenTypeGpu)
#define MICROPROFILE_TOKEN_PASTE0(a, b) a ## b
#define MICROPROFILE_TOKEN_PASTE(a, b)  MICROPROFILE_TOKEN_PASTE0(a,b)
#define MICROPROFILE_SCOPE(var) MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo, __LINE__)(g_mp_##var)
#define MICROPROFILE_SCOPE_TOKEN(token) MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo, __LINE__)(token)
#define MICROPROFILE_SCOPEI(group, name, color) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__) = MicroProfileGetToken(group, name, color, MicroProfileTokenTypeCpu); MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo,__LINE__)( MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__))
#define MICROPROFILE_SCOPEGPU(var) MicroProfileScopeGpuHandler MICROPROFILE_TOKEN_PASTE(foo, __LINE__)(g_mp_##var)
#define MICROPROFILE_SCOPEGPUI(group, name, color) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__) = MicroProfileGetToken(group, name, color,  MicroProfileTokenTypeGpu); MicroProfileScopeGpuHandler MICROPROFILE_TOKEN_PASTE(foo,__LINE__)( MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__))
#define MICROPROFILE_META_CPU(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__) = MicroProfileGetMetaToken(name); MicroProfileMetaUpdate(MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__), count, MicroProfileTokenTypeCpu)
#define MICROPROFILE_META_GPU(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__) = MicroProfileGetMetaToken(name); MicroProfileMetaUpdate(MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__), count, MicroProfileTokenTypeGpu)


#ifndef MICROPROFILE_USE_THREAD_NAME_CALLBACK
#define MICROPROFILE_USE_THREAD_NAME_CALLBACK 0
#endif

#ifndef MICROPROFILE_GPU_FRAME_DELAY
#define MICROPROFILE_GPU_FRAME_DELAY 3 //must be > 0
#endif

#ifndef MICROPROFILE_PER_THREAD_BUFFER_SIZE
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (2048<<10)
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

#ifndef MICROPROFILE_WEBSERVER_MAXFRAMES
#define MICROPROFILE_WEBSERVER_MAXFRAMES 30
#endif

#ifndef MICROPROFILE_GPU_TIMERS
#define MICROPROFILE_GPU_TIMERS 1
#endif

#ifndef MICROPROFILE_NAME_MAX_LEN
#define MICROPROFILE_NAME_MAX_LEN 64
#endif

#define MICROPROFILE_FORCEENABLECPUGROUP(s) MicroProfileForceEnableGroup(s, MicroProfileTokenTypeCpu)
#define MICROPROFILE_FORCEDISABLECPUGROUP(s) MicroProfileForceDisableGroup(s, MicroProfileTokenTypeCpu)
#define MICROPROFILE_FORCEENABLEGPUGROUP(s) MicroProfileForceEnableGroup(s, MicroProfileTokenTypeGpu)
#define MICROPROFILE_FORCEDISABLEGPUGROUP(s) MicroProfileForceDisableGroup(s, MicroProfileTokenTypeGpu)

#define MICROPROFILE_INVALID_TICK ((uint64_t)-1)
#define MICROPROFILE_GROUP_MASK_ALL 0xffffffffffff


#define MICROPROFILE_INVALID_TOKEN (uint64_t)-1

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

// struct MicroProfileState
// {
// 	uint32_t nDisplay;
// 	uint32_t nAllGroupsWanted;
// 	uint64_t nActiveGroupWanted;
// 	uint32_t nAllThreadsWanted;
// 	uint32_t nAggregateFlip;
// 	uint32_t nBars;
// 	float fReferenceTime;
// };

struct MicroProfile;

MICROPROFILE_API void MicroProfileInit();
MICROPROFILE_API void MicroProfileShutdown();
MICROPROFILE_API MicroProfileToken MicroProfileFindToken(const char* sGroup, const char* sName);
MICROPROFILE_API MicroProfileToken MicroProfileGetToken(const char* sGroup, const char* sName, uint32_t nColor, MicroProfileTokenType Token = MicroProfileTokenTypeCpu);
MICROPROFILE_API MicroProfileToken MicroProfileGetMetaToken(const char* pName);
MICROPROFILE_API void MicroProfileMetaUpdate(MicroProfileToken, int nCount, MicroProfileTokenType eTokenType);
MICROPROFILE_API uint64_t MicroProfileEnter(MicroProfileToken nToken);
MICROPROFILE_API void MicroProfileLeave(MicroProfileToken nToken, uint64_t nTick);
MICROPROFILE_API uint64_t MicroProfileGpuEnter(MicroProfileToken nToken);
MICROPROFILE_API void MicroProfileGpuLeave(MicroProfileToken nToken, uint64_t nTick);
inline uint16_t MicroProfileGetTimerIndex(MicroProfileToken t){ return (t&0xffff); }
inline uint64_t MicroProfileGetGroupMask(MicroProfileToken t){ return ((t>>16)&MICROPROFILE_GROUP_MASK_ALL);}
inline MicroProfileToken MicroProfileMakeToken(uint64_t nGroupMask, uint16_t nTimer){ return (nGroupMask<<16) | nTimer;}

MICROPROFILE_API void MicroProfileFlip(); //! called once per frame.
MICROPROFILE_API void MicroProfileTogglePause();
// MICROPROFILE_API void MicroProfileGetState(MicroProfileState* pStateOut);
// MICROPROFILE_API void MicroProfileSetState(MicroProfileState* pStateIn);
MICROPROFILE_API void MicroProfileForceEnableGroup(const char* pGroup, MicroProfileTokenType Type);
MICROPROFILE_API void MicroProfileForceDisableGroup(const char* pGroup, MicroProfileTokenType Type);
MICROPROFILE_API float MicroProfileGetTime(const char* pGroup, const char* pName);
MICROPROFILE_API void MicroProfileOnThreadCreate(const char* pThreadName); //should be called from newly created threads
MICROPROFILE_API void MicroProfileOnThreadExit(); //call on exit to reuse log
MICROPROFILE_API void MicroProfileInitThreadLog();
MICROPROFILE_API void MicroProfileSetForceEnable(bool bForceEnable);
MICROPROFILE_API bool MicroProfileGetForceEnable();
MICROPROFILE_API void MicroProfileSetEnableAllGroups(bool bEnable);
MICROPROFILE_API bool MicroProfileGetEnableAllGroups();
MICROPROFILE_API void MicroProfileSetForceMetaCounters(bool bEnable);
MICROPROFILE_API bool MicroProfileGetForceMetaCounters();
MICROPROFILE_API void MicroProfileSetAggregateFrames(int frames);
MICROPROFILE_API int MicroProfileGetAggregateFrames();
MICROPROFILE_API int MicroProfileGetCurrentAggregateFrames();
MICROPROFILE_API MicroProfile* MicroProfileGet();
MICROPROFILE_API void MicroProfileGetRange(uint32_t nPut, uint32_t nGet, uint32_t nRange[2][2]);
MICROPROFILE_API std::recursive_mutex& MicroProfileGetMutex();
MICROPROFILE_API void MicroProfileStartContextSwitchTrace();
MICROPROFILE_API void MicroProfileStopContextSwitchTrace();
MICROPROFILE_API bool MicroProfileIsLocalThread(uint32_t nThreadId);


#if MICROPROFILE_WEBSERVER
MICROPROFILE_API void MicroProfileDumpHtml(const char* pFile);
MICROPROFILE_API uint32_t MicroProfileWebServerPort();
#else
#define MicroProfileDumpHtml(c) do{} while(0)
#define MicroProfileWebServerPort() ((uint32_t)-1)
#endif




#if MICROPROFILE_GPU_TIMERS
MICROPROFILE_API uint32_t MicroProfileGpuInsertTimeStamp();
MICROPROFILE_API uint64_t MicroProfileGpuGetTimeStamp(uint32_t nKey);
MICROPROFILE_API uint64_t MicroProfileTicksPerSecondGpu();
#else
#define MicroProfileGpuInsertTimeStamp() 1
#define MicroProfileGpuGetTimeStamp(a) 0
#define MicroProfileTicksPerSecondGpu() 1
#endif


#if MICROPROFILE_USE_THREAD_NAME_CALLBACK
MICROPROFILE_API const char* MicroProfileGetThreadName();
#else
#define MicroProfileGetThreadName() "<implement MicroProfileGetThreadName to get threadnames>"
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

struct MicroProfileScopeGpuHandler
{
	MicroProfileToken nToken;
	uint64_t nTick;
	MicroProfileScopeGpuHandler(MicroProfileToken Token):nToken(Token)
	{
		nTick = MicroProfileGpuEnter(nToken);
	}
	~MicroProfileScopeGpuHandler()
	{
		MicroProfileGpuLeave(nToken, nTick);
	}
};



#define MICROPROFILE_MAX_TIMERS 1024
#define MICROPROFILE_MAX_GROUPS 48 //dont bump! no. of bits used it bitmask
#define MICROPROFILE_MAX_GRAPHS 5
#define MICROPROFILE_GRAPH_HISTORY 128
#define MICROPROFILE_BUFFER_SIZE ((MICROPROFILE_PER_THREAD_BUFFER_SIZE)/sizeof(MicroProfileLogEntry))
#define MICROPROFILE_MAX_CONTEXT_SWITCH_THREADS 256
#define MICROPROFILE_STACK_MAX 32
//#define MICROPROFILE_MAX_PRESETS 5
#define MICROPROFILE_ANIM_DELAY_PRC 0.5f
#define MICROPROFILE_GAP_TIME 50 //extra ms to fetch to close timers from earlier frames


#ifndef MICROPROFILE_MAX_THREADS
#define MICROPROFILE_MAX_THREADS 64
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
#ifdef _WIN32
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

#ifdef _WIN32
#include <basetsd.h>
typedef UINT_PTR MpSocket;
#else
typedef int MpSocket;
#endif



enum MicroProfileDrawMask
{
	MP_DRAW_OFF			= 0x0,
	MP_DRAW_BARS		= 0x1,
	MP_DRAW_DETAILED	= 0x2,
	MP_DRAW_HIDDEN		= 0x3,
};

enum MicroProfileDrawBarsMask : uint32_t
{
	MP_DRAW_TIMERS 				= 0x1,
	MP_DRAW_AVERAGE				= 0x2,
	MP_DRAW_MAX					= 0x4,
	MP_DRAW_CALL_COUNT			= 0x8,
	MP_DRAW_TIMERS_EXCLUSIVE 	= 0x10,
	MP_DRAW_AVERAGE_EXCLUSIVE 	= 0x20,
	MP_DRAW_MAX_EXCLUSIVE		= 0x40,
	MP_DRAW_META_FIRST			= 0x80,
	MP_DRAW_ALL 				= 0xffffffff,

};

typedef uint64_t MicroProfileLogEntry;

struct MicroProfileTimer
{
	uint64_t nTicks;
	uint32_t nCount;
};

struct MicroProfileGroupInfo
{
	char pName[MICROPROFILE_NAME_MAX_LEN];
	uint32_t nNameLen;
	uint32_t nGroupIndex;
	uint32_t nNumTimers;
	uint32_t nMaxTimerNameLen;
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

struct MicroProfileGraphState
{
	int64_t nHistory[MICROPROFILE_GRAPH_HISTORY];
	MicroProfileToken nToken;
	int32_t nKey;
};

struct MicroProfileContextSwitch
{
	ThreadIdType nThreadOut;
	ThreadIdType nThreadIn;
	int64_t nCpu : 8;
	int64_t nTicks : 56;
};


struct MicroProfileFrameState
{
	int64_t nFrameStartCpu;
	int64_t nFrameStartGpu;
	uint32_t nLogStart[MICROPROFILE_MAX_THREADS];
};

struct MicroProfileThreadLog
{
	MicroProfileLogEntry	Log[MICROPROFILE_BUFFER_SIZE];

	std::atomic<uint32_t>	nPut;
	std::atomic<uint32_t>	nGet;
	uint32_t 				nActive;
	uint32_t 				nGpu;
	ThreadIdType			nThreadId;

	uint32_t				nStack[MICROPROFILE_STACK_MAX];
	int64_t					nChildTickStack[MICROPROFILE_STACK_MAX];
	uint32_t				nStackPos;

	enum
	{
		THREAD_MAX_LEN = 64,
	};
	char					ThreadName[64];
	int 					nFreeListNext;
};


struct MicroProfile
{
	uint32_t nTotalTimers;
	uint32_t nGroupCount;
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

	uint64_t nActiveGroupWanted;
	uint32_t nAllGroupsWanted;
	uint32_t nAllThreadsWanted;

	uint32_t nOverflow;

	uint64_t nGroupMask;
	uint32_t nRunning;
	uint32_t nToggleRunning;
	uint32_t nMaxGroupSize;
	uint32_t nDumpHtmlNextFrame;
	char HtmlDumpPath[512];

	int64_t nPauseTicks;

	float fReferenceTime;
	float fRcpReferenceTime;

	MicroProfileGroupInfo 	GroupInfo[MICROPROFILE_MAX_GROUPS];
	MicroProfileTimerInfo 	TimerInfo[MICROPROFILE_MAX_TIMERS];

	MicroProfileTimer 		AggregateTimers[MICROPROFILE_MAX_TIMERS];
	uint64_t				MaxTimers[MICROPROFILE_MAX_TIMERS];
	uint64_t				AggregateTimersExclusive[MICROPROFILE_MAX_TIMERS];
	uint64_t				MaxTimersExclusive[MICROPROFILE_MAX_TIMERS];

	MicroProfileTimer 		Frame[MICROPROFILE_MAX_TIMERS];
	uint64_t				FrameExclusive[MICROPROFILE_MAX_TIMERS];

	MicroProfileTimer 		Aggregate[MICROPROFILE_MAX_TIMERS];
	uint64_t				AggregateMax[MICROPROFILE_MAX_TIMERS];
	uint64_t				AggregateExclusive[MICROPROFILE_MAX_TIMERS];
	uint64_t				AggregateMaxExclusive[MICROPROFILE_MAX_TIMERS];

	struct
	{
		uint64_t nCounters[MICROPROFILE_MAX_TIMERS];
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

	std::thread* 				pContextSwitchThread;
	bool  						bContextSwitchRunning;
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


	MpSocket 					ListenerSocket;
	uint32_t					nWebServerPort;

};

#define MP_LOG_TICK_MASK  0x0000ffffffffffff
#define MP_LOG_INDEX_MASK 0x3fff000000000000
#define MP_LOG_BEGIN_MASK 0xc000000000000000
#define MP_LOG_META 0x2
#define MP_LOG_ENTER 0x1
#define MP_LOG_LEAVE 0x0


inline int MicroProfileLogType(MicroProfileLogEntry Index)
{
	return ((MP_LOG_BEGIN_MASK & Index)>>62) & 0x3;
}

inline uint64_t MicroProfileLogTimerIndex(MicroProfileLogEntry Index)
{
	return (0x3fff&(Index>>48));
}

inline MicroProfileLogEntry MicroProfileMakeLogIndex(uint64_t nBegin, MicroProfileToken nToken, int64_t nTick)
{
	MicroProfileLogEntry Entry =  (nBegin<<62) | ((0x3fff&nToken)<<48) | (MP_LOG_TICK_MASK&nTick);
	int t = MicroProfileLogType(Entry);
	uint64_t nTimerIndex = MicroProfileLogTimerIndex(Entry);
	MP_ASSERT(t == nBegin);
	MP_ASSERT(nTimerIndex == (nToken&0x3fff));
	return Entry;

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
	return (uint16_t)MicroProfileGet()->TimerInfo[MicroProfileGetTimerIndex(t)].nGroupIndex;
}



#ifdef MICROPROFILE_IMPL

#ifdef _WIN32
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

#if MICROPROFILE_WEBSERVER

#ifdef _WIN32
#define MP_INVALID_SOCKET(f) (f == INVALID_SOCKET)
#endif

#if defined(__APPLE__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#define MP_INVALID_SOCKET(f) (f < 0)
#endif

void MicroProfileWebServerStart();
void MicroProfileWebServerStop();
bool MicroProfileWebServerUpdate();
void MicroProfileDumpHtmlToFile();

#else

#define MicroProfileWebServerStart() do{}while(0)
#define MicroProfileWebServerStop() do{}while(0)
#define MicroProfileWebServerUpdate() false
#define MicroProfileDumpHtmlToFile() do{} while(0)
#endif


#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>


#ifndef MICROPROFILE_DEBUG
#define MICROPROFILE_DEBUG 0
#endif


#define S g_MicroProfile

MicroProfile g_MicroProfile;
MicroProfileThreadLog*			g_MicroProfileGpuLog = 0;
#ifdef MICROPROFILE_IOS
// iOS doesn't support __thread
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
		S.nMemUsage += sizeof(S);
		bOnce = false;
		memset(&S, 0, sizeof(S));
		for(int i = 0; i < MICROPROFILE_MAX_GROUPS; ++i)
		{
			S.GroupInfo[i].pName[0] = '\0';
		}
		for(int i = 0; i < MICROPROFILE_MAX_TIMERS; ++i)
		{
			S.TimerInfo[i].pName[0] = '\0';
		}
		S.nGroupCount = 0;
		S.nAggregateFlipTick = MP_TICK();
		S.nActiveGroup = 0;
		S.nActiveBars = 0;
		S.nForceGroup = 0;
		S.nAllGroupsWanted = 0;
		S.nActiveGroupWanted = 0;
		S.nAllThreadsWanted = 1;
		S.nAggregateFlip = 0;
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
			S.Frames[i].nFrameStartGpu = -1;
		}

		MicroProfileThreadLog* pGpu = MicroProfileCreateThreadLog("GPU");
		g_MicroProfileGpuLog = pGpu;
		MP_ASSERT(S.Pool[0] == pGpu);
		pGpu->nGpu = 1;
		pGpu->nThreadId = 0;


		MicroProfileWebServerStart();
	}
	if(bUseLock)
		mutex.unlock();
}

void MicroProfileShutdown()
{
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
	MicroProfileWebServerStop();

#if MICROPROFILE_CONTEXT_SWITCH_TRACE
	if(S.pContextSwitchThread)
	{
		if(S.pContextSwitchThread->joinable())
		{
			S.bContextSwitchStop = true;
			S.pContextSwitchThread->join();
		}
		delete S.pContextSwitchThread;
	}
#endif


}

#ifdef MICROPROFILE_IOS
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
	if(S.nFreeListHead != -1)
	{
		pLog = S.Pool[S.nFreeListHead];
		MP_ASSERT(pLog->nPut.load() == 0);
		MP_ASSERT(pLog->nGet.load() == 0);
		S.nFreeListHead = S.Pool[S.nFreeListHead]->nFreeListNext;
	}
	else
	{
		pLog = new MicroProfileThreadLog;
		S.nMemUsage += sizeof(MicroProfileThreadLog);
		S.Pool[S.nNumLogs++] = pLog;
	}
	memset(pLog, 0, sizeof(*pLog));
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
	MP_ASSERT(MicroProfileGetThreadLog() == 0);
	MicroProfileThreadLog* pLog = MicroProfileCreateThreadLog(pThreadName ? pThreadName : MicroProfileGetThreadName());
	MP_ASSERT(pLog);
	MicroProfileSetThreadLog(pLog);
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
		S.nFreeListHead = nLogIndex;
		for(int i = 0; i < MICROPROFILE_MAX_FRAME_HISTORY; ++i)
		{
			S.Frames[i].nLogStart[nLogIndex] = 0;
		}
	}
}

void MicroProfileInitThreadLog()
{
	MicroProfileOnThreadCreate(nullptr);
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
		if(!MP_STRCASECMP(pName, S.TimerInfo[i].pName) && !MP_STRCASECMP(pGroup, S.GroupInfo[S.TimerInfo[i].nGroupIndex].pName))
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
	uint16_t nGroupIndex = 0xffff;
	uint32_t nLen = (uint32_t)strlen(pGroup);
	if(nLen > MICROPROFILE_NAME_MAX_LEN-1)
		nLen = MICROPROFILE_NAME_MAX_LEN-1;
	memcpy(&S.GroupInfo[S.nGroupCount].pName[0], pGroup, nLen);
	S.GroupInfo[S.nGroupCount].pName[nLen] = '\0';
	S.GroupInfo[S.nGroupCount].nNameLen = nLen;
	S.GroupInfo[S.nGroupCount].nGroupIndex = S.nGroupCount;
	S.GroupInfo[S.nGroupCount].nNumTimers = 0;
	S.GroupInfo[S.nGroupCount].Type = Type;
	S.GroupInfo[S.nGroupCount].nMaxTimerNameLen = 0;
	nGroupIndex = S.nGroupCount++;
	S.nGroupMask = (S.nGroupMask<<1)|1;
	MP_ASSERT(nGroupIndex < MICROPROFILE_MAX_GROUPS);
	return nGroupIndex;
}


MicroProfileToken MicroProfileGetToken(const char* pGroup, const char* pName, uint32_t nColor, MicroProfileTokenType Type)
{
	MicroProfileInit();
	MicroProfileScopeLock L(MicroProfileMutex());
	MicroProfileToken ret = MicroProfileFindToken(pGroup, pName);
	if(ret != MICROPROFILE_INVALID_TOKEN)
		return ret;
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
	S.TimerInfo[nTimerIndex].pName[nLen] = '\0';
	S.TimerInfo[nTimerIndex].nNameLen = nLen;
	S.TimerInfo[nTimerIndex].nColor = nColor&0xffffff;
	S.TimerInfo[nTimerIndex].nGroupIndex = nGroupIndex;
	S.TimerInfo[nTimerIndex].nTimerIndex = nTimerIndex;
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
		int64_t test = MicroProfileMakeLogIndex(nBegin, nToken_, nTick);;
		MP_ASSERT(MicroProfileLogType(test) == nBegin);
		MP_ASSERT(MicroProfileLogTimerIndex(test) == MicroProfileGetTimerIndex(nToken_));
		pLog->Log[nPos] = MicroProfileMakeLogIndex(nBegin, nToken_, nTick);
		pLog->nPut.store(nNextPos, std::memory_order_release);
	}
}

uint64_t MicroProfileEnter(MicroProfileToken nToken_)
{
	if(MicroProfileGetGroupMask(nToken_) & S.nActiveGroup)
	{
		if(!MicroProfileGetThreadLog())
		{
			MicroProfileInitThreadLog();
		}
		uint64_t nTick = MP_TICK();
		MicroProfileLogPut(nToken_, nTick, MP_LOG_ENTER, MicroProfileGetThreadLog());
		return nTick;
	}
	return MICROPROFILE_INVALID_TICK;
}

void MicroProfileMetaUpdate(MicroProfileToken nToken, int nCount, MicroProfileTokenType eTokenType)
{
	if((MP_DRAW_META_FIRST<<nToken) & S.nActiveBars)
	{
		MicroProfileThreadLog* pLog = MicroProfileTokenTypeCpu == eTokenType ? MicroProfileGetThreadLog() : g_MicroProfileGpuLog;
		if(pLog)
		{
			MP_ASSERT(nToken < MICROPROFILE_META_MAX);
			MicroProfileLogPut(nToken, nCount, MP_LOG_META, pLog);
		}
	}
}


void MicroProfileLeave(MicroProfileToken nToken_, uint64_t nTickStart)
{
	if(MICROPROFILE_INVALID_TICK != nTickStart)
	{
		if(!MicroProfileGetThreadLog())
		{
			MicroProfileInitThreadLog();
		}
		uint64_t nTick = MP_TICK();
		MicroProfileThreadLog* pLog = MicroProfileGetThreadLog();
		MicroProfileLogPut(nToken_, nTick, MP_LOG_LEAVE, pLog);
	}
}


uint64_t MicroProfileGpuEnter(MicroProfileToken nToken_)
{
	if(MicroProfileGetGroupMask(nToken_) & S.nActiveGroup)
	{
		uint64_t nTimer = MicroProfileGpuInsertTimeStamp();
		MicroProfileLogPut(nToken_, nTimer, MP_LOG_ENTER, g_MicroProfileGpuLog);
		return 1;
	}
	return 0;
}

void MicroProfileGpuLeave(MicroProfileToken nToken_, uint64_t nTickStart)
{
	if(nTickStart)
	{
		uint64_t nTimer = MicroProfileGpuInsertTimeStamp();
		MicroProfileLogPut(nToken_, nTimer, MP_LOG_LEAVE, g_MicroProfileGpuLog);
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

void MicroProfileFlip()
{
	#if 0
	//verify LogEntry wraps correctly
	MicroProfileLogEntry c = MP_LOG_TICK_MASK-5000;
	for(int i = 0; i < 10000; ++i, c += 1)
	{
		MicroProfileLogEntry l2 = (c+2500) & MP_LOG_TICK_MASK;
		MP_ASSERT(2500 == MicroProfileLogTickDifference(c, l2));
	}
	#endif
	MICROPROFILE_SCOPE(g_MicroProfileFlip);
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
	uint32_t nAggregateClear = S.nAggregateClear, nAggregateFlip = 0;
	if(S.nDumpHtmlNextFrame)
	{
		S.nDumpHtmlNextFrame = 0;
		MicroProfileDumpHtmlToFile();
	}
	if(MicroProfileWebServerUpdate())
	{
		nAggregateClear = 1;
		nAggregateFlip = 1;
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
		pFramePut->nFrameStartGpu = (uint32_t)MicroProfileGpuInsertTimeStamp();
		if(pFrameNext->nFrameStartGpu != (uint64_t)-1)
			pFrameNext->nFrameStartGpu = MicroProfileGpuGetTimeStamp((uint32_t)pFrameNext->nFrameStartGpu);

		if(pFrameCurrent->nFrameStartGpu == (uint64_t)-1)
			pFrameCurrent->nFrameStartGpu = pFrameNext->nFrameStartGpu + 1;

		uint64_t nFrameStartCpu = pFrameCurrent->nFrameStartCpu;
		uint64_t nFrameEndCpu = pFrameNext->nFrameStartCpu;

		{
			uint64_t nTick = nFrameEndCpu - nFrameStartCpu;
			S.nFlipTicks = nTick;
			S.nFlipAggregate += nTick;
			S.nFlipMax = MicroProfileMax(S.nFlipMax, nTick);
		}

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
			{
				MICROPROFILE_SCOPE(g_MicroProfileClear);
				for(uint32_t i = 0; i < S.nTotalTimers; ++i)
				{
					S.Frame[i].nTicks = 0;
					S.Frame[i].nCount = 0;
					S.FrameExclusive[i] = 0;
				}
				for(uint32_t j = 0; j < MICROPROFILE_META_MAX; ++j)
				{
					if(S.MetaCounters[j].pName)
					{
						for(uint32_t i = 0; i < S.nTotalTimers; ++i)
						{
							S.MetaCounters[j].nCounters[i] = 0;
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

					uint32_t nPut = pFrameNext->nLogStart[i];
					uint32_t nGet = pFrameCurrent->nLogStart[i];
					uint32_t nRange[2][2] = { {0, 0}, {0, 0}, };
					MicroProfileGetRange(nPut, nGet, nRange);


					//fetch gpu results.
					if(pLog->nGpu)
					{
						for(uint32_t j = 0; j < 2; ++j)
						{
							uint32_t nStart = nRange[j][0];
							uint32_t nEnd = nRange[j][1];
							for(uint32_t k = nStart; k < nEnd; ++k)
							{
								MicroProfileLogEntry L = pLog->Log[k];
								pLog->Log[k] = MicroProfileLogSetTick(L, MicroProfileGpuGetTimeStamp((uint32_t)MicroProfileLogGetTick(L)));
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
							int nType = MicroProfileLogType(LE);
							if(MP_LOG_ENTER == nType)
							{
								MP_ASSERT(nStackPos < MICROPROFILE_STACK_MAX);
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
							else
							{
								MP_ASSERT(nType == MP_LOG_LEAVE);
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
								}
							}
						}
					}
					pLog->nStackPos = nStackPos;
				}
			}
			{
				MICROPROFILE_SCOPE(g_MicroProfileAccumulate);
				for(uint32_t i = 0; i < S.nTotalTimers; ++i)
				{
					S.AggregateTimers[i].nTicks += S.Frame[i].nTicks;
					S.AggregateTimers[i].nCount += S.Frame[i].nCount;
					S.MaxTimers[i] = MicroProfileMax(S.MaxTimers[i], S.Frame[i].nTicks);
					S.AggregateTimersExclusive[i] += S.FrameExclusive[i];
					S.MaxTimersExclusive[i] = MicroProfileMax(S.MaxTimersExclusive[i], S.FrameExclusive[i]);
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
		memcpy(&S.Aggregate[0], &S.AggregateTimers[0], sizeof(S.Aggregate[0]) * S.nTotalTimers);
		memcpy(&S.AggregateMax[0], &S.MaxTimers[0], sizeof(S.AggregateMax[0]) * S.nTotalTimers);
		memcpy(&S.AggregateExclusive[0], &S.AggregateTimersExclusive[0], sizeof(S.AggregateExclusive[0]) * S.nTotalTimers);
		memcpy(&S.AggregateMaxExclusive[0], &S.MaxTimersExclusive[0], sizeof(S.AggregateMaxExclusive[0]) * S.nTotalTimers);

		S.nAggregateFrames = S.nAggregateFlipCount;
		S.nFlipAggregateDisplay = S.nFlipAggregate;
		S.nFlipMaxDisplay = S.nFlipMax;
		if(nAggregateClear)
		{
			memset(&S.AggregateTimers[0], 0, sizeof(S.Aggregate[0]) * S.nTotalTimers);
			memset(&S.MaxTimers[0], 0, sizeof(S.MaxTimers[0]) * S.nTotalTimers);
			memset(&S.AggregateTimersExclusive[0], 0, sizeof(S.AggregateExclusive[0]) * S.nTotalTimers);
			memset(&S.MaxTimersExclusive[0], 0, sizeof(S.MaxTimersExclusive[0]) * S.nTotalTimers);
			S.nAggregateFlipCount = 0;
			S.nFlipAggregate = 0;
			S.nFlipMax = 0;

			S.nAggregateFlipTick = MP_TICK();
		}
	}
	S.nAggregateClear = 0;

	uint64_t nNewActiveGroup = 0;
	if(S.nForceEnable || (S.nDisplay && S.nRunning))
		nNewActiveGroup = S.nAllGroupsWanted ? S.nGroupMask : S.nActiveGroupWanted;
	nNewActiveGroup |= S.nForceGroup;
	if(S.nActiveGroup != nNewActiveGroup)
		S.nActiveGroup = nNewActiveGroup;
	uint32_t nNewActiveBars = 0;
	if(S.nDisplay && S.nRunning)
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


void MicroProfileCalcAllTimers(float* pTimers, float* pAverage, float* pMax, float* pCallAverage, float* pExclusive, float* pAverageExclusive, float* pMaxExclusive, uint32_t nSize)
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
		float fCallAverageMs = fToMs * (S.Aggregate[nTimer].nTicks / nAggregateCount);
		float fCallAveragePrc = MicroProfileMin(fCallAverageMs * fToPrc, 1.f);
		float fMsExclusive = fToMs * (S.FrameExclusive[nTimer]);
		float fPrcExclusive = MicroProfileMin(fMsExclusive * fToPrc, 1.f);
		float fAverageMsExclusive = fToMs * (S.AggregateExclusive[nTimer] / nAggregateFrames);
		float fAveragePrcExclusive = MicroProfileMin(fAverageMsExclusive * fToPrc, 1.f);
		float fMaxMsExclusive = fToMs * (S.AggregateMaxExclusive[nTimer]);
		float fMaxPrcExclusive = MicroProfileMin(fMaxMsExclusive * fToPrc, 1.f);
		pTimers[nIdx] = fMs;
		pTimers[nIdx+1] = fPrc;
		pAverage[nIdx] = fAverageMs;
		pAverage[nIdx+1] = fAveragePrc;
		pMax[nIdx] = fMaxMs;
		pMax[nIdx+1] = fMaxPrc;
		pCallAverage[nIdx] = fCallAverageMs;
		pCallAverage[nIdx+1] = fCallAveragePrc;
		pExclusive[nIdx] = fMsExclusive;
		pExclusive[nIdx+1] = fPrcExclusive;
		pAverageExclusive[nIdx] = fAverageMsExclusive;
		pAverageExclusive[nIdx+1] = fAveragePrcExclusive;
		pMaxExclusive[nIdx] = fMaxMsExclusive;
		pMaxExclusive[nIdx+1] = fMaxPrcExclusive;
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

#if MICROPROFILE_WEBSERVER

#define MICROPROFILE_EMBED_HTML
extern const char g_MicroProfileHtml_begin[];
extern const char g_MicroProfileHtml_end[];
extern const size_t g_MicroProfileHtml_begin_size;
extern const size_t g_MicroProfileHtml_end_size;


typedef void MicroProfileWriteCallback(void* Handle, size_t size, const char* pData);

uint32_t MicroProfileWebServerPort()
{
	return S.nWebServerPort;
}

void MicroProfileDumpHtml(const char* pFile)
{
	uint32_t nLen = uint32_t(strlen(pFile));
	if(nLen > sizeof(S.HtmlDumpPath)-1)
	{
		return;
	}
	memcpy(S.HtmlDumpPath, pFile, nLen+1);
	S.nDumpHtmlNextFrame = 1;
}

void MicroProfilePrintf(MicroProfileWriteCallback CB, void* Handle, const char* pFmt, ...)
{
	char buffer[32*1024];
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

void MicroProfileDumpHtml(MicroProfileWriteCallback CB, void* Handle, int nMaxFrames)
{
	CB(Handle, g_MicroProfileHtml_begin_size-1, &g_MicroProfileHtml_begin[0]);

	//dump info
	uint64_t nTicks = MP_TICK();
	float fAggregateMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu()) * (nTicks - S.nAggregateFlipTick);
	MicroProfilePrintf(CB, Handle, "var AggregateInfo = {'Frames':%d, 'Time':%f};\n", S.nAggregateFrames, fAggregateMs);


	//groups
	MicroProfilePrintf(CB, Handle, "var GroupInfo = Array(%d);\n\n",S.nGroupCount);
	for(uint32_t i = 0; i < S.nGroupCount; ++i)
	{
		MP_ASSERT(i == S.GroupInfo[i].nGroupIndex);
		MicroProfilePrintf(CB, Handle, "GroupInfo[%d] = MakeGroup(%d, \"%s\", %d, %d);\n", S.GroupInfo[i].nGroupIndex, S.GroupInfo[i].nGroupIndex, S.GroupInfo[i].pName, S.GroupInfo[i].nNumTimers, S.GroupInfo[i].Type == MicroProfileTokenTypeGpu?1:0);
	}
	//timers

	uint32_t nNumTimers = S.nTotalTimers;
	uint32_t nBlockSize = 2 * nNumTimers;
	float* pTimers = (float*)alloca(nBlockSize * 7 * sizeof(float));
	float* pAverage = pTimers + nBlockSize;
	float* pMax = pTimers + 2 * nBlockSize;
	float* pCallAverage = pTimers + 3 * nBlockSize;
	float* pTimersExclusive = pTimers + 4 * nBlockSize;
	float* pAverageExclusive = pTimers + 5 * nBlockSize;
	float* pMaxExclusive = pTimers + 6 * nBlockSize;

	MicroProfileCalcAllTimers(pTimers, pAverage, pMax, pCallAverage, pTimersExclusive, pAverageExclusive, pMaxExclusive, nNumTimers);

	MicroProfilePrintf(CB, Handle, "\nvar TimerInfo = Array(%d);\n\n", S.nTotalTimers);
	for(uint32_t i = 0; i < S.nTotalTimers; ++i)
	{
		uint32_t nIdx = i * 2;
		MP_ASSERT(i == S.TimerInfo[i].nTimerIndex);
		MicroProfilePrintf(CB, Handle, "var Meta%d = [", i);
		bool bOnce = true;
		for(int j = 0; j < MICROPROFILE_META_MAX; ++j)
		{
			if(S.MetaCounters[j].pName)
			{
				uint32_t lala = S.MetaCounters[j].nCounters[i];
				MicroProfilePrintf(CB, Handle, bOnce ? "%d" : ",%d", lala);
				bOnce = false;
			}
		}
		MicroProfilePrintf(CB, Handle, "];\n");
		MicroProfilePrintf(CB, Handle, "TimerInfo[%d] = MakeTimer(%d, \"%s\", %d, '#%02x%02x%02x', %f, %f, %f, %f, %f, %d, Meta%d);\n", S.TimerInfo[i].nTimerIndex, S.TimerInfo[i].nTimerIndex, S.TimerInfo[i].pName, S.TimerInfo[i].nGroupIndex,
		MICROPROFILE_UNPACK_RED(S.TimerInfo[i].nColor) & 0xff,
		MICROPROFILE_UNPACK_GREEN(S.TimerInfo[i].nColor) & 0xff,
		MICROPROFILE_UNPACK_BLUE(S.TimerInfo[i].nColor) & 0xff,
		pAverage[nIdx],
		pMax[nIdx],
		pAverageExclusive[nIdx],
		pMaxExclusive[nIdx],
		pCallAverage[nIdx],
		S.Aggregate[i].nCount,
		i
		);

	}

	MicroProfilePrintf(CB, Handle, "\nvar ThreadNames = [");
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
	MicroProfilePrintf(CB, Handle, "];\n\n");
	MicroProfilePrintf(CB, Handle, "\nvar MetaNames = [");
	for(int i = 0; i < MICROPROFILE_META_MAX; ++i)
	{
		if(S.MetaCounters[i].pName)
		{
			MicroProfilePrintf(CB, Handle, "'%s',", S.MetaCounters[i].pName);
		}
	}


	MicroProfilePrintf(CB, Handle, "];\n\n");



	uint32_t nNumFrames = (MICROPROFILE_MAX_FRAME_HISTORY - MICROPROFILE_GPU_FRAME_DELAY - 1);
	if(S.nFrameCurrentIndex < nNumFrames)
		nNumFrames = S.nFrameCurrentIndex;
	if((int)nNumFrames > nMaxFrames)
	{
		nNumFrames = nMaxFrames;
	}



#if MICROPROFILE_DEBUG
	printf("dumping %d frames\n", nNumFrames);
#endif
	uint32_t nFirstFrame = (S.nFrameCurrent + MICROPROFILE_MAX_FRAME_HISTORY - nNumFrames) % MICROPROFILE_MAX_FRAME_HISTORY;
	uint32_t nFirstFrameIndex = S.nFrameCurrentIndex - nNumFrames;
	int64_t nTickStart = S.Frames[nFirstFrame].nFrameStartCpu;
	int64_t nTickStartGpu = S.Frames[nFirstFrame].nFrameStartGpu;



	MicroProfilePrintf(CB, Handle, "var Frames = Array(%d);\n", nNumFrames);
	for(uint32_t i = 0; i < nNumFrames; ++i)
	{
		uint32_t nFrameIndex = (nFirstFrame + i) % MICROPROFILE_MAX_FRAME_HISTORY;
		uint32_t nFrameIndexNext = (nFrameIndex + 1) % MICROPROFILE_MAX_FRAME_HISTORY;

		for(uint32_t j = 0; j < S.nNumLogs; ++j)
		{
			MicroProfileThreadLog* pLog = S.Pool[j];
			int64_t nStartTick = pLog->nGpu ? nTickStartGpu : nTickStart;
			uint32_t nLogStart = S.Frames[nFrameIndex].nLogStart[j];
			uint32_t nLogEnd = S.Frames[nFrameIndexNext].nLogStart[j];

			float fToMs = MicroProfileTickToMsMultiplier(pLog->nGpu ? MicroProfileTicksPerSecondGpu() : MicroProfileTicksPerSecondCpu());
			MicroProfilePrintf(CB, Handle, "var ts_%d_%d = [", i, j);
			if(nLogStart != nLogEnd)
			{
				uint32_t k = nLogStart;
				uint32_t nLogType = MicroProfileLogType(pLog->Log[k]);
				float fTime = nLogType == MP_LOG_META ? 0.f : MicroProfileLogTickDifference(nStartTick, pLog->Log[k]) * fToMs;
				MicroProfilePrintf(CB, Handle, "%f", fTime);
				for(k = (k+1) % MICROPROFILE_BUFFER_SIZE; k != nLogEnd; k = (k+1) % MICROPROFILE_BUFFER_SIZE)
				{
					uint32_t nLogType = MicroProfileLogType(pLog->Log[k]);
					float fTime = nLogType == MP_LOG_META ? 0.f : MicroProfileLogTickDifference(nStartTick, pLog->Log[k]) * fToMs;
					MicroProfilePrintf(CB, Handle, ",%f", fTime);
				}
			}
			MicroProfilePrintf(CB, Handle, "];\n");
			MicroProfilePrintf(CB, Handle, "var tt_%d_%d = [", i, j);
			if(nLogStart != nLogEnd)
			{
				uint32_t k = nLogStart;
				MicroProfilePrintf(CB, Handle, "%d", MicroProfileLogType(pLog->Log[k]));
				for(k = (k+1) % MICROPROFILE_BUFFER_SIZE; k != nLogEnd; k = (k+1) % MICROPROFILE_BUFFER_SIZE)
				{
					uint32_t nLogType = MicroProfileLogType(pLog->Log[k]);
					if(nLogType == MP_LOG_META)
					{
						//for meta, store the count + 2, which is the tick part
						nLogType = 2 + MicroProfileLogGetTick(pLog->Log[k]);
					}
					MicroProfilePrintf(CB, Handle, ",%d", nLogType);
				}
			}
			MicroProfilePrintf(CB, Handle, "];\n");

			MicroProfilePrintf(CB, Handle, "var ti_%d_%d = [", i, j);
			if(nLogStart != nLogEnd)
			{
				uint32_t k = nLogStart;
				MicroProfilePrintf(CB, Handle, "%d", (uint32_t)MicroProfileLogTimerIndex(pLog->Log[k]));
				for(k = (k+1) % MICROPROFILE_BUFFER_SIZE; k != nLogEnd; k = (k+1) % MICROPROFILE_BUFFER_SIZE)
				{
					MicroProfilePrintf(CB, Handle, ",%d", (uint32_t)MicroProfileLogTimerIndex(pLog->Log[k]));
				}
			}
			MicroProfilePrintf(CB, Handle, "];\n");

		}

		MicroProfilePrintf(CB, Handle, "var ts%d = [", i);
		for(uint32_t j = 0; j < S.nNumLogs; ++j)
		{
			MicroProfilePrintf(CB, Handle, "ts_%d_%d,", i, j);
		}
		MicroProfilePrintf(CB, Handle, "];\n");
		MicroProfilePrintf(CB, Handle, "var tt%d = [", i);
		for(uint32_t j = 0; j < S.nNumLogs; ++j)
		{
			MicroProfilePrintf(CB, Handle, "tt_%d_%d,", i, j);
		}
		MicroProfilePrintf(CB, Handle, "];\n");

		MicroProfilePrintf(CB, Handle, "var ti%d = [", i);
		for(uint32_t j = 0; j < S.nNumLogs; ++j)
		{
			MicroProfilePrintf(CB, Handle, "ti_%d_%d,", i, j);
		}
		MicroProfilePrintf(CB, Handle, "];\n");


		int64_t nFrameStart = S.Frames[nFrameIndex].nFrameStartCpu;
		int64_t nFrameEnd = S.Frames[nFrameIndexNext].nFrameStartCpu;

		float fToMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
		float fFrameMs = MicroProfileLogTickDifference(nTickStart, nFrameStart) * fToMs;
		float fFrameEndMs = MicroProfileLogTickDifference(nTickStart, nFrameEnd) * fToMs;
		MicroProfilePrintf(CB, Handle, "Frames[%d] = MakeFrame(%d, %f, %f, ts%d, tt%d, ti%d);\n", i, nFirstFrameIndex, fFrameMs,fFrameEndMs, i, i, i);
	}

	CB(Handle, g_MicroProfileHtml_end_size-1, &g_MicroProfileHtml_end[0]);
}

void MicroProfileWriteFile(void* Handle, size_t nSize, const char* pData)
{
	fwrite(pData, nSize, 1, (FILE*)Handle);
}

void MicroProfileDumpHtmlToFile()
{
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
	FILE* F = fopen(S.HtmlDumpPath, "w");
	if(F)
	{
		MicroProfileDumpHtml(MicroProfileWriteFile, F, MICROPROFILE_WEBSERVER_MAXFRAMES);
		fclose(F);
	}
}

static uint64_t g_nMicroProfileDataSent = 0;
void MicroProfileWriteSocket(void* Handle, size_t nSize, const char* pData)
{
	g_nMicroProfileDataSent += nSize;
	send(*(MpSocket*)Handle, pData, int(nSize), 0);
}


#ifndef MicroProfileSetNonBlocking //fcntl doesnt work on a some unix like platforms..
void MicroProfileSetNonBlocking(MpSocket Socket, int NonBlocking)
{
#ifdef _WIN32
	u_long nonBlocking = NonBlocking ? 1 : 0;
	ioctlsocket(Socket, FIONBIO, &nonBlocking);
#else
	int Options = fcntl(Socket, F_GETFL);
	if(NonBlocking)
	{
		fcntl(Socket, F_SETFL, Options|O_NONBLOCK);
	}
	else
	{
		fcntl(Socket, F_SETFL, Options&(~O_NONBLOCK));
	}
#endif
}
#endif

void MicroProfileWebServerStart()
{
#ifdef _WIN32
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2, 2), &wsa))
	{
		S.ListenerSocket = -1;
		return;
	}
#endif

	S.ListenerSocket = socket(PF_INET, SOCK_STREAM, 6);
	MP_ASSERT(!MP_INVALID_SOCKET(S.ListenerSocket));
	MicroProfileSetNonBlocking(S.ListenerSocket, 1);

	S.nWebServerPort = (uint32_t)-1;
	struct sockaddr_in Addr;
	Addr.sin_family = AF_INET;
	Addr.sin_addr.s_addr = INADDR_ANY;
	for(int i = 0; i < 20; ++i)
	{
		Addr.sin_port = htons(MICROPROFILE_WEBSERVER_PORT+i);
		if(0 == bind(S.ListenerSocket, (sockaddr*)&Addr, sizeof(Addr)))
		{
			S.nWebServerPort = MICROPROFILE_WEBSERVER_PORT+i;
			break;
		}
	}
	listen(S.ListenerSocket, 8);
}

void MicroProfileWebServerStop()
{
#ifdef _WIN32
	closesocket(S.ListenerSocket);
	WSACleanup();
#else
	close(S.ListenerSocket);
#endif
}
bool MicroProfileWebServerUpdate()
{
	MICROPROFILE_SCOPEI("MicroProfile", "Webserver-update", -1);
	MpSocket Connection = accept(S.ListenerSocket, 0, 0);
	bool bServed = false;
	if(!MP_INVALID_SOCKET(Connection))
	{
    int timeout = 100;
    setsockopt(Connection, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&timeout), sizeof(timeout));

    std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
		char Req[8192];
		MicroProfileSetNonBlocking(Connection, 0);
		int nReceived = recv(Connection, Req, sizeof(Req)-1, 0);
		if(nReceived > 0)
		{
			Req[nReceived] = '\0';
#if MICROPROFILE_DEBUG
			printf("got request \n%s\n", Req);
#endif
#define MICROPROFILE_HTML_HEADER "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"
			char* pHttp = strstr(Req, "HTTP/");
			char* pGet = strstr(Req, "GET / ");
			char* pGetParam = strstr(Req, "GET /?");
			if(pHttp && (pGet || pGetParam))
			{
				int nMaxFrames = MICROPROFILE_WEBSERVER_MAXFRAMES;
				if(pGetParam)
				{
					*pHttp = '\0';
					pGetParam += sizeof("GET /?")-1;
					while(pGetParam) //split url pairs foo=bar&lala=lele etc
					{
						char* pSplit = strstr(pGetParam, "&");
						if(pSplit)
						{
							*pSplit++ = '\0';
						}
						char* pKey = pGetParam;
						char* pValue = strstr(pGetParam, "=");
						if(pValue)
						{
							*pValue++ = '\0';
						}
						if(0 == MP_STRCASECMP(pKey, "frames"))
						{
							if(pValue)
							{
								nMaxFrames = atoi(pValue);
							}
						}
						pGetParam = pSplit;
					}
				}
				uint64_t nTickStart = MP_TICK();
				send(Connection, MICROPROFILE_HTML_HEADER, sizeof(MICROPROFILE_HTML_HEADER)-1, 0);
				uint64_t nDataStart = g_nMicroProfileDataSent;
				MicroProfileDumpHtml(MicroProfileWriteSocket, &Connection, nMaxFrames);
				uint64_t nDataEnd = g_nMicroProfileDataSent;
				uint64_t nTickEnd = MP_TICK();
				float fMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu()) * (nTickEnd - nTickStart);
				MicroProfilePrintf(MicroProfileWriteSocket, &Connection, "\n<!-- Sent %dkb in %6.2fms-->\n\n",((nDataEnd-nDataStart)>>10) + 1, fMs);
#if MICROPROFILE_DEBUG
				printf("\nSent %lldkb, in %6.3fms\n\n", ((nDataEnd-nDataStart)>>10) + 1, fMs);
#endif
				bServed = true;
			}
		}
#ifdef _WIN32
		closesocket(Connection);
#else
		close(Connection);
#endif
	}
	return bServed;
}
#endif




#if MICROPROFILE_CONTEXT_SWITCH_TRACE
#ifdef _WIN32
#define INITGUID
#include <evntrace.h>
#include <evntcons.h>
#include <strsafe.h>


static GUID g_MicroProfileThreadClassGuid = { 0x3d6fa8d1, 0xfe05, 0x11d0, {0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c } };

struct MicroProfileSCSwitch
{
	uint32_t NewThreadId;
	uint32_t OldThreadId;
	int8_t   NewThreadPriority;
	int8_t   OldThreadPriority;
	uint8_t  PreviousCState;
	int8_t   SpareByte;
	int8_t   OldThreadWaitReason;
	int8_t   OldThreadWaitMode;
	int8_t   OldThreadState;
	int8_t   OldThreadWaitIdealProcessor;
	uint32_t NewThreadWaitTime;
	uint32_t Reserved;
};


VOID WINAPI MicroProfileContextSwitchCallback(PEVENT_TRACE pEvent)
{
	if (pEvent->Header.Guid == g_MicroProfileThreadClassGuid)
	{
		if (pEvent->Header.Class.Type == 36)
		{
			MicroProfileSCSwitch* pCSwitch = (MicroProfileSCSwitch*) pEvent->MofData;
			if ((pCSwitch->NewThreadId != 0) || (pCSwitch->OldThreadId != 0))
			{
				MicroProfileContextSwitch Switch;
				Switch.nThreadOut = pCSwitch->OldThreadId;
				Switch.nThreadIn = pCSwitch->NewThreadId;
				Switch.nCpu = pEvent->BufferContext.ProcessorNumber;
				Switch.nTicks = pEvent->Header.TimeStamp.QuadPart;
				MicroProfileContextSwitchPut(&Switch);
			}
		}
	}
}

ULONG WINAPI MicroProfileBufferCallback(PEVENT_TRACE_LOGFILE Buffer)
{
	return (S.bContextSwitchStop || !S.bContextSwitchRunning) ? FALSE : TRUE;
}


struct MicroProfileKernelTraceProperties : public EVENT_TRACE_PROPERTIES
{
	char dummy[sizeof(KERNEL_LOGGER_NAME)];
};

void MicroProfileContextSwitchStopTrace()
{
	TRACEHANDLE SessionHandle = 0;
	MicroProfileKernelTraceProperties sessionProperties;

	ZeroMemory(&sessionProperties, sizeof(sessionProperties));
	sessionProperties.Wnode.BufferSize = sizeof(sessionProperties);
	sessionProperties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
	sessionProperties.Wnode.ClientContext = 1; //QPC clock resolution
	sessionProperties.Wnode.Guid = SystemTraceControlGuid;
	sessionProperties.BufferSize = 1;
	sessionProperties.NumberOfBuffers = 128;
	sessionProperties.EnableFlags = EVENT_TRACE_FLAG_CSWITCH;
	sessionProperties.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
	sessionProperties.MaximumFileSize = 0;
	sessionProperties.LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
	sessionProperties.LogFileNameOffset = 0;

	EVENT_TRACE_LOGFILE log;
	ZeroMemory(&log, sizeof(log));
	log.LoggerName = (LPWSTR)KERNEL_LOGGER_NAME;
	log.ProcessTraceMode = 0;
	TRACEHANDLE hLog = OpenTrace(&log);
	if (hLog)
	{
		ControlTrace(SessionHandle, KERNEL_LOGGER_NAME, &sessionProperties, EVENT_TRACE_CONTROL_STOP);
	}
	CloseTrace(hLog);


}

void MicroProfileTraceThread(int unused)
{

	MicroProfileContextSwitchStopTrace();
	ULONG status = ERROR_SUCCESS;
	TRACEHANDLE SessionHandle = 0;
	MicroProfileKernelTraceProperties sessionProperties;

	ZeroMemory(&sessionProperties, sizeof(sessionProperties));
	sessionProperties.Wnode.BufferSize = sizeof(sessionProperties);
	sessionProperties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
	sessionProperties.Wnode.ClientContext = 1; //QPC clock resolution
	sessionProperties.Wnode.Guid = SystemTraceControlGuid;
	sessionProperties.BufferSize = 1;
	sessionProperties.NumberOfBuffers = 128;
	sessionProperties.EnableFlags = EVENT_TRACE_FLAG_CSWITCH|EVENT_TRACE_FLAG_PROCESS;
	sessionProperties.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
	sessionProperties.MaximumFileSize = 0;
	sessionProperties.LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
	sessionProperties.LogFileNameOffset = 0;


	status = StartTrace((PTRACEHANDLE) &SessionHandle, KERNEL_LOGGER_NAME, &sessionProperties);

	if (ERROR_SUCCESS != status)
	{
		S.bContextSwitchRunning = false;
		return;
	}

	EVENT_TRACE_LOGFILE log;
	ZeroMemory(&log, sizeof(log));

	log.LoggerName = (LPWSTR)KERNEL_LOGGER_NAME;
	log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
	log.EventCallback = MicroProfileContextSwitchCallback;
	log.BufferCallback = MicroProfileBufferCallback;

	TRACEHANDLE hLog = OpenTrace(&log);
	ProcessTrace(&hLog, 1, 0, 0);
	CloseTrace(hLog);
	MicroProfileContextSwitchStopTrace();

	S.bContextSwitchRunning = false;
}

void MicroProfileStartContextSwitchTrace()
{
	if(!S.bContextSwitchRunning)
	{
		if(!S.pContextSwitchThread)
			S.pContextSwitchThread = new std::thread();
		if(S.pContextSwitchThread->joinable())
		{
			S.bContextSwitchStop = true;
			S.pContextSwitchThread->join();
		}
		S.bContextSwitchRunning	= true;
		S.bContextSwitchStop = false;
		*S.pContextSwitchThread = std::thread(&MicroProfileTraceThread, 0);
	}
}

void MicroProfileStopContextSwitchTrace()
{
	if(S.bContextSwitchRunning && S.pContextSwitchThread)
	{
		S.bContextSwitchStop = true;
		S.pContextSwitchThread->join();
	}
}

bool MicroProfileIsLocalThread(uint32_t nThreadId)
{
	HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, nThreadId);
	if(h == NULL)
		return false;
	DWORD hProcess = GetProcessIdOfThread(h);
	CloseHandle(h);
	return GetCurrentProcessId() == hProcess;
}

#else
#error "context switch trace not supported/implemented on platform"
#endif
#else

bool MicroProfileIsLocalThread(uint32_t nThreadId){return false;}
void MicroProfileStopContextSwitchTrace(){}
void MicroProfileStartContextSwitchTrace(){}

#endif


#undef S

#ifdef _WIN32
#pragma warning(pop)
#endif
#endif
#endif



///start embedded file from microprofile.html
#ifdef MICROPROFILE_EMBED_HTML
const char g_MicroProfileHtml_begin[] =
"<!DOCTYPE HTML>\n"
"<html>\n"
"<head>\n"
"<title>MicroProfile Capture</title>\n"
"<style>\n"
"/* about css: http://bit.ly/1eMQ42U */\n"
"body {margin: 0px;padding: 0px; font: 12px Courier New;background-color:#474747; color:white;overflow:hidden;}\n"
"ul {list-style-type: none;margin: 0;padding: 0;}\n"
"li{display: inline; float:left;border:5px; position:relative;text-align:center;}\n"
"a {\n"
"    float:left;\n"
"    text-decoration:none;\n"
"    display: inline;\n"
"    text-align: center;\n"
"	padding:5px;\n"
"	padding-bottom:0px;\n"
"	padding-top:0px;\n"
"    color: #FFFFFF;\n"
"    background-color: #474747;\n"
"}\n"
"a:hover, a:active{\n"
"	background-color: #000000;\n"
"}\n"
"\n"
"ul ul {\n"
"    position:absolute;\n"
"    left:0;\n"
"    top:100%;\n"
"    margin-left:-999em;\n"
"}\n"
"li:hover ul {\n"
"    margin-left:0;\n"
"    margin-right:0;\n"
"}\n"
"ul li ul{ display:block;float:none;width:100%;}\n"
"ul li ul li{ display:block;float:none;width:100%;}\n"
"li li a{ display:block;float:none;width:100%;text-align:left;}\n"
"#nav li:hover div {margin-left:0;}\n"
".help {position:absolute;z-index:5;text-align:left;padding:2px;margin-left:-999em;background-color: #313131;}\n"
".root {z-index:1;position:absolute;top:0px;left:0px;}\n"
"</style>\n"
"</head>\n"
"<body style=\"\">\n"
"<canvas id=\"History\" height=\"70\" style=\"background-color:#474747;margin:0px;padding:0px;\"></canvas><canvas id=\"DetailedView\" height=\"200\" style=\"background-color:#474747;margin:0px;padding:0px;\"></canvas>\n"
"<div id=\"root\" class=\"root\">\n"
"<ul id=\"nav\">\n"
"<li><a href=\"#\">?</a>\n"
"<div class=\"help\" style=\"left:20px;top:20px;width:220px;\">\n"
"Use Cursor to Inspect<br>\n"
"Shift-Drag to Pan view<br>\n"
"Ctrl-Drag to Zoom view<br>\n"
"Click to Zoom to selected range<br>\n"
"</div>\n"
"\n"
"<div class=\"help\" id=\"divFrameInfo\" style=\"left:20px;top:300px;width:auto;\">\n"
"</div>\n"
"\n"
"</li>\n"
"<li><a href=\"#\" onclick=\"SetMode(\'timers\');\" id=\"buttonTimers\">Timers</a> \n"
"<li><a href=\"#\" onclick=\"SetMode(\'detailed\');\" id=\"buttonDetailed\">Detailed</a> \n"
"</li>\n"
"<li><a href=\"#\">Reference</a>\n"
"    <ul id=\'ReferenceSubMenu\'>\n"
"        <li><a href=\"#\" onclick=\"SetReferenceTime(\'5ms\');\">5ms</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetReferenceTime(\'10ms\');\">10ms</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetReferenceTime(\'15ms\');\">15ms</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetReferenceTime(\'20ms\');\">20ms</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetReferenceTime(\'33ms\');\">33ms</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetReferenceTime(\'50ms\');\">50ms</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetReferenceTime(\'100ms\');\">100ms</a></li>\n"
"    </ul>\n"
"</li>\n"
"<li id=\"ilThreads\"><a href=\"#\">Threads</a>\n"
"    <ul id=\"ThreadSubMenu\">\n"
"        <li><a href=\"#\" onclick=\"ToggleThread();\">All</a></li>\n"
"        <li><a href=\"#\">---</a></li>\n"
"    </ul>\n"
"</li>\n"
"<li id=\"ilGroups\"><a href=\"#\">Groups</a>\n"
"    <ul id=\"GroupSubMenu\">\n"
"        <li><a href=\"#\" onclick=\"ToggleGroup();\">All</a></li>\n"
"        <li><a href=\"#\">---</a></li>\n"
"    </ul>\n"
"</li>\n"
"<li id=\"ilWidth\"><a href=\"#\">Width</a>\n"
"    <ul id=\'WidthMenu\'>\n"
"        <li><a href=\"#\" onclick=\"SetMinWidth(\'1\');\">1</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetMinWidth(\'0.5\');\">0.5</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetMinWidth(\'0.25\');\">0.25</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetMinWidth(\'0.1\');\">0.1</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetMinWidth(\'0.05\');\">0.05</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetMinWidth(\'0.025\');\">0.025</a></li>\n"
"        <li><a href=\"#\" onclick=\"SetMinWidth(\'0.01\');\">0.01</a></li>\n"
"    </ul>\n"
"</li>\n"
"</ul>\n"
"\n"
"</div>\n"
"\n"
"<script>\n"
"function InvertColor(hexTripletColor) {\n"
"	var color = hexTripletColor;\n"
"	color = color.substring(1); // remove #\n"
"	color = parseInt(color, 16); // convert to integer\n"
"	var R = ((color >> 16) % 256)/255.0;\n"
"	var G = ((color >> 8) % 256)/255.0;\n"
"	var B = ((color >> 0) % 256)/255.0;\n"
"	var lum = (0.2126*R + 0.7152*G + 0.0722*B);\n"
"	if(lum < 0.7)\n"
"	{\n"
"		return \'#ffffff\';\n"
"	}\n"
"	else\n"
"	{\n"
"		return \'#333333\';\n"
"	}\n"
"}\n"
"function MakeGroup(id, name, numtimers, isgpu)\n"
"{\n"
"	var group = {\"id\":id, \"name\":name, \"numtimers\":numtimers, \"isgpu\":isgpu};\n"
"	return group;\n"
"}\n"
"\n"
"function MakeTimer(id, name, group, color, average, max, exclaverage, exclmax, callaverage, callcount, meta)\n"
"{\n"
"	var timer = {\"id\":id, \"name\":name, \"len\":name.length, \"color\":color, \"textcolor\":InvertColor(color), \"group\":group, \"average\":average, \"max\":max, \"exclaverage\":exclaverage, \"exclmax\":exclmax, \"callaverage\":callaverage, \"callcount\":callcount, \"meta\":meta};\n"
"	return timer;\n"
"}\n"
"function MakeFrame(id, framestart, frameend, ts, tt, ti)\n"
"{\n"
"	var frame = {\"id\":id, \"framestart\":framestart, \"frameend\":frameend, \"ts\":ts, \"tt\":tt, \"ti\":ti};\n"
"	return frame;\n"
"}\n"
"\n"
"";

const size_t g_MicroProfileHtml_begin_size = sizeof(g_MicroProfileHtml_begin);
const char g_MicroProfileHtml_end[] =
"\n"
"\n"
"\n"
"var CanvasDetailedView = document.getElementById(\'DetailedView\');\n"
"var CanvasHistory = document.getElementById(\'History\');\n"
"\n"
"var fDetailedOffset = Frames[0].framestart;\n"
"var fDetailedRange = Frames[0].frameend - fDetailedOffset;\n"
"var nWidth = CanvasDetailedView.width;\n"
"var nHeight = CanvasDetailedView.height;\n"
"var ReferenceTime = 33;\n"
"var nHistoryHeight = 70;\n"
"var nOffsetY = 0;\n"
"var nOffsetBarsY = 0;\n"
"var nBarsWidth = 80;\n"
"var MouseButtonState = [0,0,0,0,0,0,0,0];\n"
"var MouseDrag = 0;\n"
"var MouseZoom = 0;\n"
"var DetailedViewMouseX = 0;\n"
"var DetailedViewMouseY = 0;\n"
"var HistoryViewMouseX = -1;\n"
"var HistoryViewMouseY = -1;\n"
"var MouseHistory = 0;\n"
"var MouseDetailed = 0;\n"
"var FontHeight = 10;\n"
"var FontWidth = 1;\n"
"var FontAscent = 3; //Set manually\n"
"var Font = \'Bold \' + FontHeight + \'px Courier New\';\n"
"var BoxHeight = FontHeight + 2;\n"
"var ThreadsActive = new Object();\n"
"var ThreadsAllActive = 1;\n"
"var GroupsActive = new Object();\n"
"var GroupsAllActive = 1;\n"
"var nMinWidth = 0.5;\n"
"\n"
"var nModDown = 0;\n"
"var g_MSG = \'no\';\n"
"var nDrawCount = 0;\n"
"var nBackColors = [\'#474747\', \'#313131\' ];\n"
"var nBackColorOffset = \'#606060\';\n"
"var FRAME_HISTORY_COLOR_CPU = \'#ff7f27\';\n"
"var FRAME_HISTORY_COLOR_GPU = \'#ffffff\';\n"
"var ZOOM_TIME = 0.5;\n"
"var AnimationActive = false;\n"
"var nHoverToken = -1;\n"
"var nHoverFrame = -1;\n"
"var nHoverTokenIndex = -1;\n"
"var nHoverTokenLogIndex = -1;\n"
"var nHoverCounter = 0;\n"
"var nHoverCounterDelta = 8;\n"
"\n"
"var fFrameScale = 33.33;\n"
"var fRangeBegin = 0;\n"
"var fRangeEnd = -1;\n"
"\n"
"var ModeDetailed = 0;\n"
"var ModeTimers = 1;\n"
"var Mode = ModeDetailed;\n"
"\n"
"var DebugDrawQuadCount = 0;\n"
"var DebugDrawTextCount = 0;\n"
"\n"
"\n"
"function InitFrameInfo()\n"
"{\n"
"	var DetailedTotal = 0;\n"
"	for(var i = 0; i < Frames.length; i++)\n"
"	{\n"
"		var frfr = Frames[i];\n"
"		DetailedTotal += frfr.frameend - frfr.framestart;\n"
"	}\n"
"\n"
"	var div = document.getElementById(\'divFrameInfo\');\n"
"	var txt = \'\';\n"
"	txt = txt + \'Timers View\' + \'<br>\';\n"
"	txt = txt + \'Frames:\' + AggregateInfo.Frames +\'<br>\';\n"
"	txt = txt + \'Time:\' + AggregateInfo.Time.toFixed(2) +\'ms<br>\';\n"
"	txt = txt + \'<hr>\';\n"
"	txt = txt + \'Detailed View\' + \'<br>\';\n"
"	txt = txt + \'Frames:\' + Frames.length +\'<br>\';\n"
"	txt = txt + \'Time:\' + DetailedTotal.toFixed(2) +\'ms<br>\';\n"
"	div.innerHTML = txt;\n"
"}\n"
"function InitGroups()\n"
"{\n"
"	for(groupid in GroupInfo)\n"
"	{\n"
"		var TimerArray = Array();\n"
"		for(timerid in TimerInfo)\n"
"		{\n"
"			if(TimerInfo[timerid].group == groupid)\n"
"			{\n"
"				TimerArray.push(timerid);\n"
"			}\n"
"		}\n"
"		GroupInfo[groupid].TimerArray = TimerArray;\n"
"	}\n"
"}\n"
"\n"
"function InitThreadMenu()\n"
"{\n"
"	var ulThreadMenu = document.getElementById(\'ThreadSubMenu\');\n"
"	var MaxLen = 7;\n"
"	for(var idx in ThreadNames)\n"
"	{\n"
"		var name = ThreadNames[idx];\n"
"		var li = document.createElement(\'li\');\n"
"		if(name.length > MaxLen)\n"
"		{\n"
"			MaxLen = name.length;\n"
"		}\n"
"		li.innerText = name;\n"
"		var asText = li.innerHTML;\n"
"		var html = \'<a href=\"#\" onclick=\"ToggleThread(\\'\' + name + \'\\');\">\' + asText + \'</a>\';\n"
"		li.innerHTML = html;\n"
"		ulThreadMenu.appendChild(li);\n"
"	}\n"
"	var LenStr = (5+(1+MaxLen) * FontWidth) + \'px\';\n"
"	var Lis = ulThreadMenu.getElementsByTagName(\'li\');\n"
"	for(var i = 0; i < Lis.length; ++i)\n"
"	{\n"
"		Lis[i].style[\'width\'] = LenStr;\n"
"	}\n"
"}\n"
"\n"
"function UpdateThreadMenu()\n"
"{\n"
"	var ulThreadMenu = document.getElementById(\'ThreadSubMenu\');\n"
"	var as = ulThreadMenu.getElementsByTagName(\'a\');\n"
"	for(var i = 0; i < as.length; ++i)\n"
"	{\n"
"		var elem = as[i];\n"
"		var inner = elem.innerText;\n"
"		var bActive = false;\n"
"		if(i < 2)\n"
"		{\n"
"			if(inner == \'All\')\n"
"			{\n"
"				bActive = ThreadsAllActive;\n"
"			}\n"
"		}\n"
"		else\n"
"		{\n"
"			bActive = ThreadsActive[inner];\n"
"		}\n"
"		if(bActive)\n"
"		{\n"
"			elem.style[\'text-decoration\'] = \'underline\';\n"
"		}\n"
"		else\n"
"		{\n"
"			elem.style[\'text-decoration\'] = \'none\';\n"
"		}\n"
"	}\n"
"}\n"
"\n"
"function ToggleThread(ThreadName)\n"
"{\n"
"	if(ThreadName)\n"
"	{\n"
"		if(ThreadsActive[ThreadName])\n"
"		{\n"
"			ThreadsActive[ThreadName] = false;\n"
"		}\n"
"		else\n"
"		{\n"
"			ThreadsActive[ThreadName] = true;\n"
"		}\n"
"	}\n"
"	else\n"
"	{\n"
"		if(ThreadsAllActive)\n"
"		{\n"
"			ThreadsAllActive = 0;\n"
"		}\n"
"		else\n"
"		{\n"
"			ThreadsAllActive = 1;\n"
"		}\n"
"	}\n"
"	UpdateThreadMenu();\n"
"	WriteCookie();\n"
"	Draw();\n"
"}\n"
"\n"
"\n"
"function InitGroupMenu()\n"
"{\n"
"	var ulGroupMenu = document.getElementById(\'GroupSubMenu\');\n"
"	var MaxLen = 7;\n"
"	for(var idx in GroupInfo)\n"
"	{\n"
"		var name = GroupInfo[idx].name;\n"
"		var li = document.createElement(\'li\');\n"
"		if(name.length > MaxLen)\n"
"		{\n"
"			MaxLen = name.length;\n"
"		}\n"
"		li.innerText = name;\n"
"		var asText = li.innerHTML;\n"
"		var html = \'<a href=\"#\" onclick=\"ToggleGroup(\\'\' + name + \'\\');\">\' + asText + \'</a>\';\n"
"		li.innerHTML = html;\n"
"		ulGroupMenu.appendChild(li);\n"
"	}\n"
"	var LenStr = (5+(1+MaxLen) * FontWidth) + \'px\';\n"
"	var Lis = ulGroupMenu.getElementsByTagName(\'li\');\n"
"	for(var i = 0; i < Lis.length; ++i)\n"
"	{\n"
"		Lis[i].style[\'width\'] = LenStr;\n"
"	}\n"
"	UpdateGroupMenu();\n"
"}\n"
"\n"
"function UpdateGroupMenu()\n"
"{\n"
"	var ulThreadMenu = document.getElementById(\'GroupSubMenu\');\n"
"	var as = ulThreadMenu.getElementsByTagName(\'a\');\n"
"	for(var i = 0; i < as.length; ++i)\n"
"	{\n"
"		var elem = as[i];\n"
"		var inner = elem.innerText;\n"
"		var bActive = false;\n"
"		if(i < 2)\n"
"		{\n"
"			if(inner == \'All\')\n"
"			{\n"
"				bActive = GroupsAllActive;\n"
"			}\n"
"		}\n"
"		else\n"
"		{\n"
"			bActive = GroupsActive[inner];\n"
"		}\n"
"		if(bActive)\n"
"		{\n"
"			elem.style[\'text-decoration\'] = \'underline\';\n"
"		}\n"
"		else\n"
"		{\n"
"			elem.style[\'text-decoration\'] = \'none\';\n"
"		}\n"
"	}\n"
"}\n"
"\n"
"\n"
"function ToggleGroup(GroupName)\n"
"{\n"
"	if(GroupName)\n"
"	{\n"
"		if(GroupsActive[GroupName])\n"
"		{\n"
"			GroupsActive[GroupName] = false;\n"
"		}\n"
"		else\n"
"		{\n"
"			GroupsActive[GroupName] = true;\n"
"		}\n"
"	}\n"
"	else\n"
"	{\n"
"		if(GroupsAllActive)\n"
"		{\n"
"			GroupsAllActive = 0;\n"
"		}\n"
"		else\n"
"		{\n"
"			GroupsAllActive = 1;\n"
"		}\n"
"	}\n"
"	UpdateGroupMenu();\n"
"	WriteCookie();\n"
"	Draw();\n"
"}\n"
"\n"
"\n"
"\n"
"function SetMode(NewMode)\n"
"{\n"
"	console.log(\'setmodemodemode \' + NewMode);\n"
"	console.log(\'!!!mode is \' + NewMode);\n"
"	var buttonTimers = document.getElementById(\'buttonTimers\');\n"
"	var buttonDetailed = document.getElementById(\'buttonDetailed\');\n"
"	var ilWidth = document.getElementById(\'ilWidth\');\n"
"	var ilThreads = document.getElementById(\'ilThreads\');\n"
"	var ilGroups = document.getElementById(\'ilGroups\');\n"
"	if(NewMode == \'timers\' || NewMode == ModeTimers)\n"
"	{\n"
"		buttonTimers.style[\'text-decoration\'] = \'underline\';\n"
"		buttonDetailed.style[\'text-decoration\'] = \'none\';\n"
"		ilWidth.style[\'display\'] = \'none\';\n"
"		ilThreads.style[\'display\'] = \'none\';\n"
"		ilGroups.style[\'display\'] = \'block\';\n"
"		Mode = ModeTimers;\n"
"	}\n"
"	else if(NewMode == \'detailed\' || NewMode == ModeDetailed)\n"
"	{\n"
"		buttonTimers.style[\'text-decoration\'] = \'none\';\n"
"		buttonDetailed.style[\'text-decoration\'] = \'underline\';\n"
"		ilWidth.style[\'display\'] = \'block\';\n"
"		ilThreads.style[\'display\'] = \'block\';\n"
"		ilGroups.style[\'display\'] = \'none\';\n"
"		Mode = ModeDetailed;\n"
"	}\n"
"	WriteCookie();\n"
"	Draw();\n"
"}\n"
"\n"
"function SetReferenceTime(TimeString)\n"
"{\n"
"	ReferenceTime = parseInt(TimeString);\n"
"	var ReferenceMenu = document.getElementById(\'ReferenceSubMenu\');\n"
"	var Links = ReferenceMenu.getElementsByTagName(\'a\');\n"
"	for(var i = 0; i < Links.length; ++i)\n"
"	{\n"
"		if(Links[i].innerHTML.match(\'^\' + TimeString))\n"
"		{\n"
"			Links[i].style[\'text-decoration\'] = \'underline\';\n"
"		}\n"
"		else\n"
"		{\n"
"			Links[i].style[\'text-decoration\'] = \'none\';\n"
"		}\n"
"	}\n"
"	WriteCookie();\n"
"	Draw();\n"
"}\n"
"\n"
"function SetMinWidth(TimeString)\n"
"{\n"
"	nMinWidth = parseFloat(TimeString);\n"
"	var ReferenceMenu = document.getElementById(\'WidthMenu\');\n"
"	var Links = ReferenceMenu.getElementsByTagName(\'a\');\n"
"	for(var i = 0; i < Links.length; ++i)\n"
"	{\n"
"		if(Links[i].innerHTML.match(\'^\' + TimeString))\n"
"		{\n"
"			Links[i].style[\'text-decoration\'] = \'underline\';\n"
"		}\n"
"		else\n"
"		{\n"
"			Links[i].style[\'text-decoration\'] = \'none\';\n"
"		}\n"
"	}\n"
"	WriteCookie();\n"
"	Draw();\n"
"}\n"
"\n"
"function GatherHoverMetaCounters(TimerIndex, StartIndex, nLog, nFrameLast)\n"
"{\n"
"	var HoverInfo = new Object();\n"
"	var StackPos = 1;\n"
"	//search backwards, count meta counters \n"
"	for(var i = nFrameLast; i >= 0; i--)\n"
"	{\n"
"		var fr = Frames[i];\n"
"		var ts = fr.ts[nLog];\n"
"		var ti = fr.ti[nLog];\n"
"		var tt = fr.tt[nLog];\n"
"		var start = i == nFrameLast ? StartIndex-1 : ts.length-1;\n"
"\n"
"		for(var j = start; j >= 0; j--)\n"
"		{\n"
"			var type = tt[j];\n"
"			var index = ti[j];\n"
"			var time = ts[j];\n"
"			if(type == 1)\n"
"			{\n"
"				StackPos--;\n"
"				if(StackPos == 0 && index == TimerIndex)\n"
"				{\n"
"					return HoverInfo;\n"
"				}\n"
"			}\n"
"			else if(type == 0)\n"
"			{\n"
"				StackPos++;\n"
"			}\n"
"			else\n"
"			{\n"
"				var nMetaCount = type - 2;\n"
"				var nMetaIndex = MetaNames[index];\n"
"				if(nMetaIndex in HoverInfo)\n"
"				{\n"
"					HoverInfo[nMetaIndex] += nMetaCount;\n"
"				}\n"
"				else\n"
"				{\n"
"					HoverInfo[nMetaIndex] = nMetaCount;\n"
"				}\n"
"			}\n"
"		}\n"
"	}\n"
"\n"
"}\n"
"function CalculateTimers(Result, TimerIndex, nFrameFirst, nFrameLast)\n"
"{\n"
"	//var Result = new Object();\n"
"	if(!nFrameFirst || nFrameFirst < 0)\n"
"		nFrameFirst = 0;\n"
"	if(!nFrameLast || nFrameLast > Frames.length)\n"
"		nFrameLast = Frames.length;\n"
"	var FrameCount = nFrameLast - nFrameFirst;\n"
"	if(0 == FrameCount)\n"
"		return;\n"
"	var CallCount = 0;\n"
"	var Sum = 0;\n"
"	var Max = 0;\n"
"	var FrameMax = 0;\n"
"\n"
"	var nNumLogs = Frames[0].ts.length;\n"
"	var StackPosArray = Array(nNumLogs);\n"
"	var StackArray = Array(nNumLogs);\n"
"	for(var i = 0; i < nNumLogs; ++i)\n"
"	{\n"
"		StackPosArray[i] = 0;\n"
"		StackArray[i] = Array(20);\n"
"	}\n"
"\n"
"	for(var i = nFrameFirst; i < nFrameLast; i++)\n"
"	{\n"
"		var FrameSum = 0;\n"
"		var fr = Frames[i];\n"
"		for(nLog = 0; nLog < nNumLogs; nLog++)\n"
"		{\n"
"			var StackPos = StackPosArray[nLog];\n"
"			var Stack = StackArray[nLog];\n"
"			var ts = fr.ts[nLog];\n"
"			var ti = fr.ti[nLog];\n"
"			var tt = fr.tt[nLog];\n"
"			var count = ts.length;\n"
"			for(j = 0; j < count; j++)\n"
"			{\n"
"				var type = tt[j];\n"
"				var index = ti[j];\n"
"				var time = ts[j];\n"
"				if(type == 1) //enter\n"
"				{\n"
"					//push\n"
"					Stack[StackPos] = time;\n"
"					if(StackArray[nLog][StackPos] != time)\n"
"					{\n"
"						console.log(\'fail fail fail\');\n"
"					}\n"
"					StackPos++;\n"
"				}\n"
"				else if(type == 0) // leave\n"
"				{\n"
"					var timestart;\n"
"					var timeend = time;\n"
"					if(StackPos>0)\n"
"					{\n"
"						StackPos--;\n"
"						timestart = Stack[StackPos];\n"
"					}\n"
"					else\n"
"					{\n"
"						timestart = Frames[nFrameFirst].framestart;\n"
"					}\n"
"					if(index == TimerIndex)\n"
"					{\n"
"						var TimeDelta = timeend - timestart;\n"
"						CallCount++;\n"
"						FrameSum += TimeDelta;\n"
"						Sum += TimeDelta;\n"
"						if(TimeDelta > Max)\n"
"							Max = TimeDelta;\n"
"					}\n"
"				}\n"
"				else\n"
"				{\n"
"					//meta\n"
"				}\n"
"			}\n"
"			if(FrameSum > FrameMax)\n"
"			{\n"
"				FrameMax = FrameSum;\n"
"			}\n"
"			StackPosArray[nLog] = StackPos;\n"
"		}\n"
"	}\n"
"\n"
"	Result.CallCount = CallCount;\n"
"	Result.Sum = Sum.toFixed(3);\n"
"	Result.Max = Max.toFixed(3);\n"
"	Result.Average = (Sum / CallCount).toFixed(3);\n"
"	Result.FrameAverage = (Sum / FrameCount).toFixed(3);\n"
"	Result.FrameCallAverage = (CallCount / FrameCount).toFixed(3);\n"
"	Result.FrameMax = FrameMax.toFixed(3);\n"
"	return Result;\n"
"\n"
"}\n"
"\n"
"function DrawDetailedFrameHistory()\n"
"{\n"
"	var x = HistoryViewMouseX;\n"
"\n"
"	var context = CanvasHistory.getContext(\'2d\');\n"
"	context.clearRect(0, 0, CanvasHistory.width, CanvasHistory.height);\n"
"\n"
"	var fHeight = nHistoryHeight;\n"
"	var fWidth = nWidth / Frames.length;\n"
"	var fHeightScale = fHeight / ReferenceTime;\n"
"	var fX = 0;\n"
"	var FrameIndex = -1;\n"
"\n"
"	for(i = 0; i < Frames.length; i++)\n"
"	{\n"
"		var fMs = Frames[i].frameend - Frames[i].framestart;\n"
"		var fH = fHeightScale * fMs;\n"
"		var bMouse = x > fX && x < fX + fWidth;\n"
"		if(bMouse)\n"
"		{\n"
"			context.fillStyle = FRAME_HISTORY_COLOR_GPU;\n"
"			fRangeBegin = Frames[i].framestart;\n"
"			fRangeEnd = Frames[i].frameend;\n"
"			FrameIndex = i;\n"
"		}\n"
"		else\n"
"		{\n"
"			context.fillStyle = FRAME_HISTORY_COLOR_CPU;\n"
"		}\n"
"		context.fillRect(fX, fHeight - fH, fWidth-1, fH);\n"
"		fX += fWidth;\n"
"	}\n"
"\n"
"	if(FrameIndex>=0)\n"
"	{\n"
"		var StringArray = [];\n"
"		StringArray.push(\"Frame\");\n"
"		StringArray.push(\"\" + FrameIndex);\n"
"		StringArray.push(\"Time\");\n"
"		StringArray.push(\"\" + (Frames[FrameIndex].frameend - Frames[FrameIndex].framestart).toFixed(3));\n"
"\n"
"		DrawToolTip(StringArray, CanvasHistory, HistoryViewMouseX, HistoryViewMouseY+20);\n"
"\n"
"	}\n"
"\n"
"}\n"
"\n"
"function DrawDetailedBackground()\n"
"{\n"
"	var context = CanvasDetailedView.getContext(\'2d\');\n"
"	var fMs = fDetailedRange;\n"
"	var fMsEnd = fMs + fDetailedOffset;\n"
"	var fMsToScreen = nWidth / fMs;\n"
"	var fRate = Math.floor(2*((Math.log(fMs)/Math.log(10))-1))/2;\n"
"	var fStep = Math.pow(10, fRate);\n"
"	var fRcpStep = 1.0 / fStep;\n"
"	var nColorIndex = Math.floor(fDetailedOffset * fRcpStep) % 2;\n"
"	if(nColorIndex < 0)\n"
"		nColorIndex = -nColorIndex;\n"
"	var fStart = Math.floor(fDetailedOffset * fRcpStep) * fStep;\n"
"	var fHeight = CanvasDetailedView.height;\n"
"	var fScaleX = nWidth / fDetailedRange; \n"
"	for(f = fStart; f < fMsEnd; )\n"
"	{\n"
"		var fNext = f + fStep;\n"
"		var X = (f - fDetailedOffset) * fScaleX;\n"
"		var W = (fNext-f)*fScaleX;\n"
"		context.fillStyle = nBackColors[nColorIndex];\n"
"		context.fillRect(X, 0, W+2, fHeight);\n"
"		nColorIndex = 1 - nColorIndex;\n"
"		f = fNext;\n"
"	}\n"
"	var fScaleX = nWidth / fDetailedRange; \n"
"	context.globalAlpha = 0.5;\n"
"	context.strokeStyle = \'#bbbbbb\';\n"
"	context.beginPath();\n"
"	for(var i = 0; i < Frames.length; i++)\n"
"	{\n"
"		var frfr = Frames[i];\n"
"		if(frfr.frameend < fDetailedOffset || frfr.framestart > fDetailedOffset + fDetailedRange)\n"
"		{\n"
"			continue;\n"
"		}\n"
"		var X = (frfr.framestart - fDetailedOffset) * fScaleX;\n"
"		if(X >= 0 && X < nWidth)\n"
"		{\n"
"			context.moveTo(X, 0);\n"
"			context.lineTo(X, nHeight);\n"
"		}\n"
"	}\n"
"	context.stroke();\n"
"	context.globalAlpha = 1;\n"
"\n"
"}\n"
"function DrawToolTip(StringArray, Canvas, x, y)\n"
"{\n"
"	var context = Canvas.getContext(\'2d\');\n"
"	context.font = Font;\n"
"	var WidthArray = Array(StringArray.length);\n"
"	var nMaxWidth = 0;\n"
"	var nHeight = 0;\n"
"	for(i = 0; i < StringArray.length; i += 2)\n"
"	{\n"
"		var nWidth0 = context.measureText(StringArray[i]).width;\n"
"		var nWidth1 = context.measureText(StringArray[i+1]).width;\n"
"		var nSum = nWidth0 + nWidth1;\n"
"		WidthArray[i] = nWidth0;\n"
"		WidthArray[i+1] = nWidth1;\n"
"		if(nSum > nMaxWidth)\n"
"		{\n"
"			nMaxWidth = nSum;\n"
"		}\n"
"		nHeight += BoxHeight;\n"
"	}\n"
"	nMaxWidth += 15;\n"
"	//bounds check.\n"
"	var CanvasRect = Canvas.getBoundingClientRect();\n"
"	if(y + nHeight > CanvasRect.height)\n"
"	{\n"
"		y = CanvasRect.height - nHeight;\n"
"		x += 20;\n"
"	}\n"
"	if(x + nMaxWidth > CanvasRect.width)\n"
"	{\n"
"		x = CanvasRect.width - nMaxWidth;\n"
"	}\n"
"\n"
"	context.fillStyle = \'black\';\n"
"	context.fillRect(x-1, y, nMaxWidth+2, nHeight);\n"
"	context.fillStyle = \'white\';\n"
"\n"
"	var XPos = x;\n"
"	var XPosRight = x + nMaxWidth;\n"
"	var YPos = y + BoxHeight-2;\n"
"	for(i = 0; i < StringArray.length; i += 2)\n"
"	{\n"
"		context.fillText(StringArray[i], XPos, YPos);\n"
"		context.fillText(StringArray[i+1], XPosRight - WidthArray[i+1], YPos);\n"
"		YPos += BoxHeight;\n"
"	}\n"
"}\n"
"function DrawHoverToolTip()\n"
"{\n"
"	if(nHoverToken != -1)\n"
"	{\n"
"		var StringArray = [];\n"
"		var groupid = TimerInfo[nHoverToken].group;\n"
"		StringArray.push(GroupInfo[groupid].name);\n"
"		StringArray.push(TimerInfo[nHoverToken].name);\n"
"		StringArray.push(\"\");\n"
"		StringArray.push(\"\");\n"
"		StringArray.push(\"Time\");\n"
"		StringArray.push((fRangeEnd-fRangeBegin).toFixed(3));\n"
"		StringArray.push(\"\");\n"
"		StringArray.push(\"\");\n"
"		StringArray.push(\"Total\");\n"
"		StringArray.push(\"\" + TimerInfo[nHoverToken].Sum);\n"
"		StringArray.push(\"Max\");\n"
"		StringArray.push(\"\" + TimerInfo[nHoverToken].Max);\n"
"		StringArray.push(\"Average\");\n"
"		StringArray.push(\"\" + TimerInfo[nHoverToken].Average);\n"
"		StringArray.push(\"Count\");\n"
"		StringArray.push(\"\" + TimerInfo[nHoverToken].CallCount);\n"
"\n"
"		StringArray.push(\"\");\n"
"		StringArray.push(\"\");\n"
"\n"
"		StringArray.push(\"Max/Frame\");\n"
"		StringArray.push(\"\" + TimerInfo[nHoverToken].FrameMax);\n"
"\n"
"		StringArray.push(\"Average Time/Frame\");\n"
"		StringArray.push(\"\" + TimerInfo[nHoverToken].FrameAverage);\n"
"\n"
"		StringArray.push(\"Average Count/Frame\");\n"
"		StringArray.push(\"\" + TimerInfo[nHoverToken].FrameCallAverage);\n"
"\n"
"	\n"
"		if(nHoverFrame != -1)\n"
"		{\n"
"			StringArray.push(\"\");\n"
"			StringArray.push(\"\");\n"
"			StringArray.push(\"Frame \" + nHoverFrame);\n"
"			StringArray.push(\"\");\n"
"\n"
"			var FrameTime = new Object();\n"
"			CalculateTimers(FrameTime, nHoverToken, nHoverFrame, nHoverFrame+1);\n"
"			StringArray.push(\"Total\");\n"
"			StringArray.push(\"\" + FrameTime.Sum);\n"
"			StringArray.push(\"Count\");\n"
"			StringArray.push(\"\" + FrameTime.CallCount);\n"
"			StringArray.push(\"Average\");\n"
"			StringArray.push(\"\" + FrameTime.Average);\n"
"			StringArray.push(\"Max\");\n"
"			StringArray.push(\"\" + FrameTime.Max);\n"
"		}\n"
"\n"
"		var HoverInfo = GatherHoverMetaCounters(nHoverToken, nHoverTokenIndex, nHoverTokenLogIndex, nHoverFrame);\n"
"		var Header = 0;\n"
"		for(index in HoverInfo)\n"
"		{\n"
"			if(0 == Header)\n"
"			{\n"
"				Header = 1;\n"
"				StringArray.push(\"\");\n"
"				StringArray.push(\"\");\n"
"				StringArray.push(\"Meta\");\n"
"				StringArray.push(\"\");\n"
"\n"
"			}\n"
"			StringArray.push(\"\"+index);\n"
"			StringArray.push(\"\"+HoverInfo[index]);\n"
"		}\n"
"		DrawToolTip(StringArray, CanvasDetailedView, DetailedViewMouseX, DetailedViewMouseY+20);\n"
"\n"
"	}\n"
"}\n"
"\n"
"\n"
"function DrawBarView()\n"
"{\n"
"	console.log(\'bar view view\');\n"
"	nHoverToken = -1;\n"
"	nHoverFrame = -1;\n"
"	var context = CanvasDetailedView.getContext(\'2d\');\n"
"	context.clearRect(0, 0, nWidth, nHeight);\n"
"\n"
"	var Height = BoxHeight;\n"
"	var Width = nWidth;\n"
"\n"
"	//clamp offset to prevent scrolling into the void\n"
"	var nTotalRows = 0;\n"
"	for(var groupid in GroupInfo)\n"
"	{\n"
"		if(GroupsAllActive || GroupsActive[GroupInfo[groupid].name])\n"
"		{\n"
"			nTotalRows += GroupInfo[groupid].TimerArray.length + 1;\n"
"		}\n"
"	}\n"
"	var nTotalRowPixels = nTotalRows * Height;\n"
"	var nFrameRows = nHeight - BoxHeight;\n"
"	if(nOffsetBarsY + nFrameRows > nTotalRowPixels && nTotalRowPixels > nFrameRows)\n"
"	{\n"
"		nOffsetBarsY = nTotalRowPixels - nFrameRows;\n"
"	}\n"
"\n"
"\n"
"	var Y = -nOffsetBarsY + BoxHeight;\n"
"	var nColorIndex = 0;\n"
"\n"
"	context.fillStyle = \'white\';\n"
"	context.font = Font;\n"
"	var bMouseIn = 0;\n"
"	var RcpReferenceTime = 1.0 / ReferenceTime;\n"
"	var CountWidth = 8 * FontWidth;\n"
"	var nMetaLen = TimerInfo[0].meta.length;\n"
"	var nMetaCharacters = 10;\n"
"	for(var i = 0; i < nMetaLen; ++i)\n"
"	{\n"
"		if(nMetaCharacters < MetaNames[i].length)\n"
"			nMetaCharacters = MetaNames[i].length;\n"
"	}\n"
"	var nWidthMeta = nMetaCharacters * FontWidth + 6;\n"
"\n"
"\n"
"\n"
"	for(var groupid in GroupInfo)\n"
"	{\n"
"		var Group = GroupInfo[groupid];\n"
"		if(GroupsAllActive || GroupsActive[Group.name])\n"
"		{\n"
"			//write header\n"
"			nColorIndex = 1-nColorIndex;\n"
"			bMouseIn = DetailedViewMouseY >= Y && DetailedViewMouseY < Y + BoxHeight;\n"
"			context.fillStyle = bMouseIn ? nBackColorOffset : nBackColors[nColorIndex];\n"
"			context.fillRect(0, Y, Width, Height);\n"
"			context.fillStyle = \'white\';\n"
"			context.fillText(Group.name, 1, Y+Height-FontAscent);\n"
"\n"
"\n"
"			Y += Height;\n"
"			var TimerArray = Group.TimerArray;\n"
"			var InnerBoxHeight = BoxHeight-2;\n"
"			var TimerLen = 6;\n"
"			var TimerWidth = TimerLen * FontWidth;\n"
"			var nWidthBars = nBarsWidth+2;\n"
"			var nWidthMs = TimerWidth + 2 + 10;\n"
"\n"
"\n"
"			for(var timerindex in TimerArray)\n"
"			{\n"
"				var timerid = TimerArray[timerindex];\n"
"				var X = 0;\n"
"				nColorIndex = 1-nColorIndex;\n"
"				bMouseIn = DetailedViewMouseY >= Y && DetailedViewMouseY < Y + BoxHeight;\n"
"				if(bMouseIn)\n"
"				{\n"
"					nHoverToken = timerid;\n"
"				}\n"
"				context.fillStyle = bMouseIn ? nBackColorOffset : nBackColors[nColorIndex];\n"
"				context.fillRect(X, Y, Width, Height);\n"
"\n"
"				var Timer = TimerInfo[timerid];\n"
"\n"
"				var Average = Timer.average;\n"
"				var Max = Timer.max;\n"
"				var ExclusiveMax = Timer.exclmax;\n"
"				var ExclusiveAverage = Timer.exclaverage;\n"
"				var CallAverage = Timer.callaverage;\n"
"				var CallCount = Timer.callcount;\n"
"				var YText = Y+Height-FontAscent;\n"
"				function DrawTimer(Value)\n"
"				{\n"
"					var Prc = Value * RcpReferenceTime;\n"
"					if(Prc > 1)\n"
"					{\n"
"						Prc = 1;\n"
"					}\n"
"					context.fillStyle = Timer.color;\n"
"					context.fillRect(X+1, Y+1, Prc * nBarsWidth, InnerBoxHeight);\n"
"					X += nWidthBars;\n"
"					context.fillStyle = \'white\';\n"
"					context.fillText((\"      \" + Value.toFixed(2)).slice(-TimerLen), X, YText);\n"
"					X += nWidthMs;\n"
"				}\n"
"				function DrawMeta(Value)\n"
"				{\n"
"					if(!Value)\n"
"					{\n"
"						Value = \"0\";\n"
"					}\n"
"					else\n"
"					{\n"
"						Value = \'\' + Value;\n"
"					}\n"
"					context.fillText(Value, X + nWidthMeta - 6 - Value.length * FontWidth, YText);\n"
"					X += nWidthMeta;\n"
"				}\n"
"\n"
"				DrawTimer(Average);\n"
"				DrawTimer(Max);\n"
"				DrawTimer(CallAverage);\n"
"				context.fillStyle = \'white\';\n"
"				context.fillText(CallCount, X, YText);\n"
"				X += CountWidth;\n"
"				DrawTimer(ExclusiveAverage);\n"
"				DrawTimer(ExclusiveMax);\n"
"\n"
"				context.fillStyle = \'white\';\n"
"				for(var j = 0; j < nMetaLen; ++j)\n"
"				{\n"
"					DrawMeta(Timer.meta[j]);\n"
"				}\n"
"\n"
"\n"
"				context.fillStyle = Timer.color;\n"
"				context.fillText(Timer.name, X+1, YText);\n"
"				Y += Height;\n"
"\n"
"			}\n"
"		}\n"
"	}\n"
"	X = 0;\n"
"	context.fillStyle = nBackColorOffset;\n"
"	context.fillRect(0, 0, Width, Height);\n"
"	context.fillStyle = \'white\';\n"
"\n"
"\n"
"	function DrawHeaderSplit(Header)\n"
"	{\n"
"		context.fillStyle = \'white\';\n"
"		context.fillText(Header, X, Height-FontAscent);\n"
"		X += nWidthBars;\n"
"		context.fillStyle = nBackColorOffset;\n"
"		X += nWidthMs;\n"
"		context.fillRect(X-3, 0, 1, nHeight);\n"
"	}\n"
"	function DrawHeaderSplitSingle(Header, Width)\n"
"	{\n"
"		context.fillStyle = \'white\';\n"
"		context.fillText(Header, X, Height-FontAscent);\n"
"		X += Width;\n"
"		context.fillStyle = nBackColorOffset;\n"
"		context.fillRect(X-3, 0, 1, nHeight);\n"
"	}\n"
"\n"
"\n"
"	DrawHeaderSplit(\'Average\');\n"
"	DrawHeaderSplit(\'Max\');\n"
"	DrawHeaderSplit(\'Call Average\');\n"
"	DrawHeaderSplitSingle(\'Count\', CountWidth);\n"
"	DrawHeaderSplit(\'Excl Average\');\n"
"	DrawHeaderSplit(\'Excl Max\');\n"
"	for(var i = 0; i < nMetaLen; ++i)\n"
"	{\n"
"		DrawHeaderSplitSingle(MetaNames[i], nWidthMeta);\n"
"	}\n"
"	DrawHeaderSplit(\'Name\');\n"
"\n"
"}\n"
"\n"
"function DrawDetailed(Animation)\n"
"{\n"
"	if(AnimationActive != Animation)\n"
"	{\n"
"		return;\n"
"	}\n"
"	DebugDrawQuadCount = 0;\n"
"	DebugDrawTextCount = 0;\n"
"\n"
"\n"
"	var start = new Date();\n"
"	nDrawCount++;\n"
"\n"
"	var context = CanvasDetailedView.getContext(\'2d\');\n"
"	context.clearRect(0, 0, CanvasDetailedView.width, CanvasDetailedView.height);\n"
"\n"
"	DrawDetailedBackground();\n"
"\n"
"	var colors = [ \'#ff0000\', \'#ff00ff\', \'#ffff00\'];\n"
"\n"
"	var fScaleX = nWidth / fDetailedRange; \n"
"	var fOffsetY = -nOffsetY + BoxHeight;\n"
"	var nHoverTokenNext = -1;\n"
"	var nHoverTokenLogIndexNext = -1;\n"
"	var nHoverTokenIndexNext = -1;\n"
"	nHoverCounter += nHoverCounterDelta;\n"
"	if(nHoverCounter >= 255) \n"
"	{\n"
"		nHoverCounter = 255;\n"
"		nHoverCounterDelta = -nHoverCounterDelta;\n"
"	}\n"
"	if(nHoverCounter < 128) \n"
"	{\n"
"		nHoverCounter = 128;\n"
"		nHoverCounterDelta = -nHoverCounterDelta;\n"
"	}\n"
"	var nHoverHigh = nHoverCounter.toString(16);\n"
"	var nHoverLow = (127+255-nHoverCounter).toString(16);\n"
"	var nHoverColor = \'#\' + nHoverHigh + nHoverHigh + nHoverHigh;\n"
"\n"
"	context.fillStyle = \'black\';\n"
"	context.font = Font;\n"
"	var nNumLogs = Frames[0].ts.length;\n"
"	for(nLog = 0; nLog < nNumLogs; nLog++)\n"
"	{\n"
"		var ThreadName = ThreadNames[nLog];\n"
"		if(ThreadsAllActive || ThreadsActive[ThreadName])\n"
"		{\n"
"			context.fillStyle = \'white\';\n"
"			context.fillText(ThreadName, 0, fOffsetY);\n"
"			fOffsetY += BoxHeight;\n"
"\n"
"			var MaxDepth = 1;\n"
"			var StackPos = 0;\n"
"			var Stack = Array(20);\n"
"\n"
"			for(var i = 0; i < Frames.length; i++)\n"
"			{\n"
"				var frfr = Frames[i];\n"
"				if(0 == StackPos && frfr.framestart > fDetailedOffset + fDetailedRange) \n"
"				{\n"
"					continue;\n"
"				}\n"
"				var ts = frfr.ts[nLog];\n"
"				var ti = frfr.ti[nLog];\n"
"				var tt = frfr.tt[nLog];\n"
"				var count = ts.length;\n"
"\n"
"				for(j = 0; j < count; j++)\n"
"				{\n"
"					var type = tt[j];\n"
"					var index = ti[j];\n"
"					var time = ts[j];\n"
"					if(type == 1)\n"
"					{\n"
"						//push\n"
"						Stack[StackPos] = time;\n"
"						StackPos++;\n"
"						if(StackPos > MaxDepth)\n"
"						{\n"
"							MaxDepth = StackPos;\n"
"						}\n"
"					}\n"
"					else if(type == 0)\n"
"					{\n"
"						if(StackPos>0)\n"
"						{\n"
"							StackPos--;\n"
"							var timestart = Stack[StackPos];\n"
"							var timeend = time;\n"
"							var X = (timestart - fDetailedOffset) * fScaleX;\n"
"							var Y = fOffsetY + StackPos * BoxHeight;\n"
"							var W = (timeend-timestart)*fScaleX;\n"
"\n"
"							if(W > nMinWidth && X < nWidth && X+W > 0)\n"
"							{\n"
"								if(index == nHoverToken)\n"
"								{\n"
"									context.fillStyle = nHoverColor;\n"
"									context.fillRect(X, Y, W, BoxHeight-1);\n"
"								}\n"
"								else\n"
"								{\n"
"									context.fillStyle = TimerInfo[index].color;\n"
"									context.fillRect(X, Y, W, BoxHeight-1);\n"
"								}\n"
"								DebugDrawQuadCount++;\n"
"\n"
"								var XText = X < 0 ? 0 : X;\n"
"								var WText = W - (XText-X);\n"
"								var name = TimerInfo[index].name;\n"
"								var len = TimerInfo[index].len;\n"
"								var sublen = Math.floor((WText-2)/FontWidth);\n"
"								if(sublen >= 2)\n"
"								{\n"
"									if(sublen < len)\n"
"										name = name.substr(0, sublen);\n"
"									context.fillStyle = InvertColor(TimerInfo[index].color);\n"
"									context.fillText(name, XText+1, Y+BoxHeight-FontAscent);\n"
"									DebugDrawTextCount++;\n"
"								}\n"
"								if(DetailedViewMouseX >= X && DetailedViewMouseX <= X+W && DetailedViewMouseY < Y+BoxHeight && DetailedViewMouseY >= Y)\n"
"								{\n"
"									fRangeBegin = timestart;\n"
"									fRangeEnd = timeend;\n"
"									nHoverTokenNext = index;\n"
"									nHoverTokenIndexNext = j;\n"
"									nHoverTokenLogIndexNext = nLog;\n"
"									nHoverFrame = i;\n"
"								}\n"
"							}\n"
"						}\n"
"					}\n"
"				}\n"
"			}\n"
"			fOffsetY += (1+MaxDepth) * BoxHeight;\n"
"		}\n"
"	}\n"
"	if(MouseDrag || MouseZoom)\n"
"	{\n"
"		nHoverToken = -1;\n"
"		nHoverTokenIndex = -1;\n"
"		nHoverTokenLogIndex = -1;\n"
"\n"
"		fRangeBegin = fRangeEnd = -1;\n"
"	}\n"
"	else\n"
"	{\n"
"		nHoverToken = nHoverTokenNext;\n"
"		nHoverTokenIndex = nHoverTokenIndexNext;\n"
"		nHoverTokenLogIndex = nHoverTokenLogIndexNext;\n"
"\n"
"	}\n"
"\n"
"\n"
"\n"
"	if(fRangeBegin < fRangeEnd)\n"
"	{\n"
"		var X = (fRangeBegin - fDetailedOffset) * fScaleX;\n"
"		var Y = 0;\n"
"		var W = (fRangeEnd - fRangeBegin) * fScaleX;\n"
"		context.globalAlpha = 0.1;\n"
"		context.fillStyle = \'#009900\';\n"
"		context.fillRect(X, Y, W, nHeight);\n"
"		context.globalAlpha = 1;\n"
"		context.strokeStyle = \'#00ff00\';\n"
"		context.beginPath();\n"
"		context.moveTo(X, Y);\n"
"		context.lineTo(X, Y+nHeight);\n"
"		context.moveTo(X+W, Y);\n"
"		context.lineTo(X+W, Y+nHeight);\n"
"		context.stroke();\n"
"		var tRangeBeginWidth = context.measureText(\'\' + fRangeBegin).width;\n"
"\n"
"		context.fillStyle = \'white\';\n"
"		context.fillText(\'\' + fRangeBegin, X - tRangeBeginWidth-2, 9);\n"
"		context.fillText(\'\' + fRangeEnd, X + W, 9);\n"
"	}\n"
"\n"
"\n"
"\n"
"\n"
"\n"
"	var end = new Date();\n"
"	var time = end - start;\n"
"	var timeTaken = \'TIME \' + time + \'ms \';\n"
"	if(false)\n"
"	{\n"
"		context.fillStyle = \'white\';\n"
"		context.font = \'italic 12pt Calibri\';\n"
"		context.fillText(timeTaken+\" \" + nDrawCount + \"...\" + g_MSG + \" Q:\" + DebugDrawQuadCount + \" T:\" + DebugDrawTextCount, 20, 20);\n"
"		g_MSG = \'\';\n"
"	}\n"
"}\n"
"\n"
"function ZoomTo(fZoomBegin, fZoomEnd)\n"
"{\n"
"	if(fZoomBegin < fZoomEnd)\n"
"	{\n"
"		AnimationActive = true;\n"
"		var fDetailedOffsetOriginal = fDetailedOffset;\n"
"		var fDetailedRangeOriginal = fDetailedRange;\n"
"		var fDetailedOffsetTarget = fZoomBegin;\n"
"		var fDetailedRangeTarget = fZoomEnd - fZoomBegin;\n"
"		var TimestampStart = new Date();\n"
"		var count = 0;\n"
"		function ZoomFunc(Timestamp)\n"
"		{\n"
"			var fPrc = (new Date() - TimestampStart) / (ZOOM_TIME * 1000.0);\n"
"			if(fPrc > 1.0)\n"
"			{\n"
"				fPrc = 1.0;\n"
"			}\n"
"			fPrc = Math.pow(fPrc, 0.3);\n"
"			fDetailedOffset = fDetailedOffsetOriginal + (fDetailedOffsetTarget - fDetailedOffsetOriginal) * fPrc;\n"
"			fDetailedRange = fDetailedRangeOriginal + (fDetailedRangeTarget - fDetailedRangeOriginal) * fPrc;\n"
"			DrawDetailed(true);\n"
"			if(fPrc >= 1.0)\n"
"			{\n"
"				AnimationActive = false;\n"
"				fDetailedOffset = fDetailedOffsetTarget;\n"
"				fDetailedRange = fDetailedRangeTarget;\n"
"			}\n"
"			else\n"
"			{\n"
"				requestAnimationFrame(ZoomFunc);\n"
"			}\n"
"		}\n"
"		requestAnimationFrame(ZoomFunc);\n"
"	}\n"
"}\n"
"function Draw()\n"
"{\n"
"\n"
"		console.log(\'drawing \' + Mode);\n"
"	if(Mode == ModeTimers)\n"
"	{\n"
"		DrawBarView();\n"
"	}\n"
"	else if(Mode == ModeDetailed)\n"
"	{\n"
"		DrawDetailed(false);\n"
"		DrawHoverToolTip();\n"
"	}\n"
"	DrawDetailedFrameHistory();\n"
"}\n"
"function AutoRedraw(Timestamp)\n"
"{\n"
"	if(Mode == ModeDetailed)\n"
"	{\n"
"		if(nHoverToken != -1 && !MouseZoom && !MouseDrag)\n"
"		{\n"
"			DrawDetailed(false);\n"
"			DrawHoverToolTip();\n"
"		}\n"
"	}\n"
"	requestAnimationFrame(AutoRedraw);\n"
"}\n"
"\n"
"\n"
"function ZoomGraph(nZoom)\n"
"{\n"
"	var fOldRange = fDetailedRange;\n"
"	if(nZoom>0)\n"
"	{\n"
"		fDetailedRange *= Math.pow(nModDown ? 1.40 : 1.03, nZoom);\n"
"	}\n"
"	else\n"
"	{\n"
"		var fNewDetailedRange = fDetailedRange / Math.pow((nModDown ? 1.40 : 1.03), -nZoom);\n"
"		if(fNewDetailedRange < 0.0001) //100ns\n"
"			fNewDetailedRange = 0.0001;\n"
"		fDetailedRange = fNewDetailedRange;\n"
"	}\n"
"\n"
"	var fDiff = fOldRange - fDetailedRange;\n"
"	var fMousePrc = DetailedViewMouseX / nWidth;\n"
"	if(fMousePrc < 0)\n"
"	{\n"
"		fMousePrc = 0;\n"
"	}\n"
"	fDetailedOffset += fDiff * fMousePrc;\n"
"\n"
"}\n"
"\n"
"function MeasureFont()\n"
"{\n"
"	var context = CanvasDetailedView.getContext(\'2d\');\n"
"	context.font = Font;\n"
"	FontWidth = context.measureText(\'W\').width;\n"
"\n"
"}\n"
"function ResizeCanvas() \n"
"{\n"
"	nWidth = window.innerWidth;\n"
"	nHeight = window.innerHeight - CanvasHistory.height-2;\n"
"	var DPR = window.devicePixelRatio;\n"
"	if(DPR)\n"
"	{\n"
"		CanvasDetailedView.style.width = nWidth + \'px\'; \n"
"		CanvasDetailedView.style.height = nHeight + \'px\';\n"
"		CanvasDetailedView.width = nWidth * DPR;\n"
"		CanvasDetailedView.height = nHeight * DPR;\n"
"		CanvasHistory.style.width = window.innerWidth + \'px\';\n"
"		CanvasHistory.style.height = 70 + \'px\';\n"
"		CanvasHistory.width = window.innerWidth * DPR;\n"
"		CanvasHistory.height = 70 * DPR;\n"
"		CanvasHistory.getContext(\'2d\').scale(DPR,DPR);\n"
"		CanvasDetailedView.getContext(\'2d\').scale(DPR,DPR);\n"
"\n"
"\n"
"	}\n"
"	else\n"
"	{\n"
"		CanvasDetailedView.width = nWidth;\n"
"		CanvasDetailedView.height = nHeight;\n"
"		CanvasHistory.width = window.innerWidth;\n"
"	}\n"
"	Draw();\n"
"}\n"
"\n"
"function MouseMove(evt)\n"
"{\n"
"	MouseHistory = 0;\n"
"	MouseDetailed = 0;\n"
"	HistoryViewMouseX = HistoryViewMouseY = -1;\n"
"	if(evt.target == CanvasDetailedView)\n"
"	{\n"
"		fRangeBegin = fRangeEnd = -1;\n"
"		var rect = CanvasDetailedView.getBoundingClientRect();\n"
"		var x = evt.clientX - rect.left;\n"
"		var y = evt.clientY - rect.top;\n"
"		if(Mode == ModeDetailed)\n"
"		{\n"
"			if(MouseDrag)\n"
"			{\n"
"				var X = x - DetailedViewMouseX;\n"
"				var Y = y - DetailedViewMouseY;\n"
"				if(X)\n"
"				{\n"
"					fDetailedOffset += -X * fDetailedRange / nWidth;\n"
"				}\n"
"				nOffsetY -= Y;\n"
"				if(nOffsetY < 0)\n"
"				{\n"
"					nOffsetY = 0;\n"
"				}\n"
"			}\n"
"			if(MouseZoom)\n"
"			{\n"
"				if(y != DetailedViewMouseY)\n"
"				{\n"
"					ZoomGraph(y - DetailedViewMouseY);\n"
"				}\n"
"			}\n"
"		}\n"
"		else if(Mode == ModeTimers)\n"
"		{\n"
"			if(MouseDrag)\n"
"			{\n"
"				var X = x - DetailedViewMouseX;\n"
"				var Y = y - DetailedViewMouseY;\n"
"				nOffsetBarsY -= Y;\n"
"				if(nOffsetBarsY < 0)\n"
"				{\n"
"					nOffsetBarsY = 0;\n"
"				}\n"
"			}\n"
"		}\n"
"		DetailedViewMouseX = x;\n"
"		DetailedViewMouseY = y;\n"
"	}\n"
"	else if(evt.target = CanvasHistory)\n"
"	{\n"
"		var Rect = CanvasHistory.getBoundingClientRect();\n"
"		HistoryViewMouseX = evt.clientX - Rect.left;\n"
"		HistoryViewMouseY = evt.clientY - Rect.top;\n"
"	}\n"
"	Draw();\n"
"}\n"
"function MouseButton(bPressed, evt)\n"
"{\n"
"	if(evt.target == CanvasHistory)\n"
"	{\n"
"		if(!bPressed)\n"
"		{\n"
"			if(evt.button == 0)\n"
"			{\n"
"				ZoomTo(fRangeBegin, fRangeEnd);\n"
"			}\n"
"		}\n"
"	}\n"
"	else if(evt.target == CanvasDetailedView)\n"
"	{\n"
"		var rect = CanvasDetailedView.getBoundingClientRect();\n"
"		var x = evt.clientX - rect.left;\n"
"		var y = evt.clientY - rect.top;\n"
"		if(bPressed)\n"
"		{\n"
"			MouseButtonState[evt.button]=1;\n"
"			if(evt.button == 0)\n"
"			{\n"
"			}\n"
"		}\n"
"		else\n"
"		{\n"
"			MouseButtonState[evt.button]=1;\n"
"			if(evt.button == 0)\n"
"			{\n"
"				ZoomTo(fRangeBegin, fRangeEnd);\n"
"			}\n"
"		}\n"
"	}\n"
"}\n"
"function MouseOut(evt)\n"
"{\n"
"	MouseZoom = 0;\n"
"	MouseDrag = 0;\n"
"	nHoverToken = -1;\n"
"	fRangeBegin = fRangeEnd = -1;\n"
"}\n"
"\n"
"function KeyUp(evt)\n"
"{\n"
"	if(evt.keyCode == 17)\n"
"	{\n"
"		MouseZoom = 0;\n"
"	}\n"
"	else if(evt.keyCode == 16)\n"
"	{\n"
"		MouseDrag = 0;\n"
"	}\n"
"}\n"
"\n"
"function KeyDown(evt)\n"
"{\n"
"	g_MSG = \' keycode\' + evt.keyCode;\n"
"	if(evt.keyCode == 17)\n"
"	{\n"
"		MouseDrag = 0;\n"
"		MouseZoom = 1;\n"
"	}\n"
"	else if(evt.keyCode == 16)\n"
"	{\n"
"		MouseDrag = 1;\n"
"	}\n"
"}\n"
"\n"
"function ReadCookie()\n"
"{\n"
"	var result = document.cookie.match(/fisk=([^;]+)/);\n"
"	var NewMode = ModeDetailed;\n"
"	var ReferenceTimeString = \'33ms\';\n"
"	var nMinWidth = 0.1;\n"
"	if(result && result.length > 0)\n"
"	{\n"
"		var Obj = JSON.parse(result[1]);\n"
"		if(Obj.Mode)\n"
"		{\n"
"			NewMode = Obj.Mode;\n"
"		}\n"
"		if(Obj.ReferenceTime)\n"
"		{\n"
"			ReferenceTimeString = Obj.ReferenceTime;\n"
"		}\n"
"		if(Obj.ThreadsAllActive || Obj.ThreadsAllActive == 0 || Obj.ThreadsAllActive == false)\n"
"		{\n"
"			ThreadsAllActive = Obj.ThreadsAllActive;\n"
"		}\n"
"		else\n"
"		{\n"
"			ThreadsAllActive = 1;\n"
"		}\n"
"		if(Obj.ThreadsActive)\n"
"		{\n"
"			ThreadsActive = Obj.ThreadsActive;\n"
"		}\n"
"		if(Obj.nMinWidth)\n"
"		{\n"
"			nMinWidth = Obj.nMinWidth;\n"
"		}\n"
"		if(Obj.GroupsAllActive || Obj.GroupsAllActive == 0 || Obj.GroupsAllActive)\n"
"		{\n"
"			GroupsAllActive = Obj.GroupsAllActive;\n"
"		}\n"
"		else\n"
"		{\n"
"			GroupsAllActive = 1;\n"
"		}\n"
"		if(Obj.GroupsActive)\n"
"		{\n"
"			GroupsActive = Obj.GroupsActive;\n"
"		}\n"
"	}\n"
"	SetMode(NewMode);\n"
"	SetReferenceTime(ReferenceTimeString);\n"
"	SetMinWidth(\'\' + nMinWidth);\n"
"\n"
"}\n"
"function WriteCookie()\n"
"{\n"
"	var Obj = new Object();\n"
"	Obj.Mode = Mode;\n"
"	Obj.ReferenceTime = ReferenceTime + \'ms\';\n"
"	Obj.ThreadsActive = ThreadsActive;\n"
"	Obj.ThreadsAllActive = ThreadsAllActive;\n"
"	Obj.GroupsActive = GroupsActive;\n"
"	Obj.GroupsAllActive = GroupsAllActive;\n"
"	Obj.nMinWidth = nMinWidth;\n"
"	var date = new Date();\n"
"	date.setFullYear(2099);\n"
"	var cookie = \'fisk=\' + JSON.stringify(Obj) + \';expires=\' + date;\n"
"	document.cookie = cookie;\n"
"}\n"
"\n"
"CanvasDetailedView.addEventListener(\'mousemove\', MouseMove, false);\n"
"CanvasDetailedView.addEventListener(\'mousedown\', function(evt) { MouseButton(true, evt); });\n"
"CanvasDetailedView.addEventListener(\'mouseup\', function(evt) { MouseButton(false, evt); } );\n"
"CanvasDetailedView.addEventListener(\'mouseout\', MouseOut);\n"
"CanvasHistory.addEventListener(\'mousemove\', MouseMove);\n"
"CanvasHistory.addEventListener(\'mousedown\', function(evt) { MouseButton(true, evt); });\n"
"CanvasHistory.addEventListener(\'mouseup\', function(evt) { MouseButton(false, evt); } );\n"
"CanvasHistory.addEventListener(\'mouseout\', MouseOut);\n"
"window.addEventListener(\'keydown\', KeyDown);\n"
"window.addEventListener(\'keyup\', KeyUp);\n"
"window.addEventListener(\'resize\', ResizeCanvas, false);\n"
"\n"
"\n"
"var start = new Date();\n"
"for(var i = 0; i < TimerInfo.length; i++)\n"
"{\n"
"	var v = CalculateTimers(TimerInfo[i], i);\n"
"\n"
"}\n"
"var end = new Date();\n"
"var time = end - start;\n"
"console.log(\'setup :: \' + time + \'ms \');\n"
"\n"
"InitGroups();\n"
"ReadCookie();\n"
"MeasureFont()\n"
"InitThreadMenu();\n"
"InitGroupMenu();\n"
"InitFrameInfo();\n"
"UpdateThreadMenu();\n"
"ResizeCanvas();\n"
"Draw();\n"
"AutoRedraw();\n"
"\n"
"</script>\n"
"</body>\n"
"</html>      ";

const size_t g_MicroProfileHtml_end_size = sizeof(g_MicroProfileHtml_end);
#endif //MICROPROFILE_EMBED_HTML


///end embedded file from microprofile.html
