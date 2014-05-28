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

#if 0 == MICROPROFILE_ENABLED

#define MICROPROFILE_DECLARE(var)
#define MICROPROFILE_DEFINE(var, group, name, color)
#define MICROPROFILE_DECLARE_GPU(var)
#define MICROPROFILE_DEFINE_GPU(var, group, name, color)
#define MICROPROFILE_SCOPE(var) do{}while(0)
#define MICROPROFILE_SCOPEI(group, name, color) do{}while(0)
#define MICROPROFILE_SCOPEGPU(var) do{}while(0)
#define MICROPROFILE_SCOPEGPUI(group, name, color) do{}while(0)
#define MICROPROFILE_META(name, count)
#define MICROPROFILE_FORCEENABLECPUGROUP(s) do{} while(0)
#define MICROPROFILE_FORCEDISABLECPUGROUP(s) do{} while(0)
#define MICROPROFILE_FORCEENABLEGPUGROUP(s) do{} while(0)
#define MICROPROFILE_FORCEDISABLEGPUGROUP(s) do{} while(0)

#define MicroProfileGetTime(group, name) 0.f
#define MicroProfileOnThreadCreate(foo) do{}while(0)
#define MicroProfileMouseButton(foo, bar) do{}while(0)
#define MicroProfileMousePosition(foo, bar) do{}while(0)
#define MicroProfileModKey(key) do{}while(0)
#define MicroProfileFlip() do{}while(0)
#define MicroProfileDraw(foo, bar) do{}while(0)
#define MicroProfileIsDrawing() 0
#define MicroProfileToggleDisplayMode() do{}while(0)
#define MicroProfileSetDisplayMode() do{}while(0)
#define MicroProfileTogglePause() do{}while(0)
#define MicroProfileDumpTimers() do{}while(0)

#else

#include <stdint.h>
#include <string.h>

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
int64_t MicroProfileTicksPerSecondCpu();
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

#ifndef MICROPROFILE_API
#define MICROPROFILE_API
#endif

#define MP_ASSERT(a) do{if(!(a)){MP_BREAK();} }while(0)
#define MICROPROFILE_DECLARE(var) extern MicroProfileToken g_mp_##var
#define MICROPROFILE_DEFINE(var, group, name, color) MicroProfileToken g_mp_##var = MicroProfileGetToken(group, name, color, MicroProfileTokenTypeCpu)
#define MICROPROFILE_DECLARE_GPU(var) extern MicroProfileToken g_mp_##var
#define MICROPROFILE_DEFINE_GPU(var, group, name, color) MicroProfileToken g_mp_##var = MicroProfileGetToken(group, name, color, MicroProfileTokenTypeGpu)
#define MICROPROFILE_TOKEN_PASTE0(a, b) a ## b
#define MICROPROFILE_TOKEN_PASTE(a, b)  MICROPROFILE_TOKEN_PASTE0(a,b)
#define MICROPROFILE_SCOPE(var) MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo, __LINE__)(g_mp_##var)
#define MICROPROFILE_SCOPEI(group, name, color) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__) = MicroProfileGetToken(group, name, color, MicroProfileTokenTypeCpu); MicroProfileScopeHandler MICROPROFILE_TOKEN_PASTE(foo,__LINE__)( MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__))
#define MICROPROFILE_SCOPEGPU(var) MicroProfileScopeGpuHandler MICROPROFILE_TOKEN_PASTE(foo, __LINE__)(g_mp_##var)
#define MICROPROFILE_SCOPEGPUI(group, name, color) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__) = MicroProfileGetToken(group, name, color,  MicroProfileTokenTypeGpu); MicroProfileScopeGpuHandler MICROPROFILE_TOKEN_PASTE(foo,__LINE__)( MICROPROFILE_TOKEN_PASTE(g_mp,__LINE__))
#define MICROPROFILE_META_CPU(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__) = MicroProfileGetMetaToken(name); MicroProfileMetaUpdate(MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__), count, MicroProfileTokenTypeCpu)
#define MICROPROFILE_META_GPU(name, count) static MicroProfileToken MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__) = MicroProfileGetMetaToken(name); MicroProfileMetaUpdate(MICROPROFILE_TOKEN_PASTE(g_mp_meta,__LINE__), count, MicroProfileTokenTypeGpu)

///configuration

#ifndef MICROPROFILE_TEXT_WIDTH
#define MICROPROFILE_TEXT_WIDTH 5
#endif

#ifndef MICROPROFILE_TEXT_HEIGHT
#define MICROPROFILE_TEXT_HEIGHT 8
#endif

#ifndef MICROPROFILE_DETAILED_BAR_HEIGHT
#define MICROPROFILE_DETAILED_BAR_HEIGHT 12
#endif

#ifndef MICROPROFILE_DETAILED_CONTEXT_SWITCH_HEIGHT
#define MICROPROFILE_DETAILED_CONTEXT_SWITCH_HEIGHT 7
#endif

#ifndef MICROPROFILE_GRAPH_WIDTH
#define MICROPROFILE_GRAPH_WIDTH 256
#endif

#ifndef MICROPROFILE_GRAPH_HEIGHT
#define MICROPROFILE_GRAPH_HEIGHT 256
#endif

#ifndef MICROPROFILE_BORDER_SIZE 
#define MICROPROFILE_BORDER_SIZE 1
#endif

#ifndef MICROPROFILE_USE_THREAD_NAME_CALLBACK
#define MICROPROFILE_USE_THREAD_NAME_CALLBACK 0
#endif

#ifndef MICROPROFILE_DRAWCURSOR
#define MICROPROFILE_DRAWCURSOR 0
#endif

#ifndef MICROPROFILE_DETAILED_BAR_NAMES
#define MICROPROFILE_DETAILED_BAR_NAMES 1
#endif

#ifndef MICROPROFILE_GPU_FRAME_DELAY
#define MICROPROFILE_GPU_FRAME_DELAY 3 //must be > 0
#endif

#ifndef MICROPROFILE_PER_THREAD_BUFFER_SIZE
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (2048<<10)
#endif

#ifndef MICROPROFILE_HELP_LEFT
#define MICROPROFILE_HELP_LEFT "Left-Click"
#endif

#ifndef MICROPROFILE_HELP_ALT
#define MICROPROFILE_HELP_ALT "Alt-Click"
#endif

#ifndef MICROPROFILE_HELP_MOD
#define MICROPROFILE_HELP_MOD "Mod"
#endif

#ifndef MICROPROFILE_PRINTF
#define MICROPROFILE_PRINTF printf
#endif

#ifndef MICROPROFILE_META_MAX
#define MICROPROFILE_META_MAX 8
#endif



#define MICROPROFILE_FORCEENABLECPUGROUP(s) MicroProfileForceEnableGroup(s, MicroProfileTokenTypeCpu)
#define MICROPROFILE_FORCEDISABLECPUGROUP(s) MicroProfileForceDisableGroup(s, MicroProfileTokenTypeCpu)
#define MICROPROFILE_FORCEENABLEGPUGROUP(s) MicroProfileForceEnableGroup(s, MicroProfileTokenTypeGpu)
#define MICROPROFILE_FORCEDISABLEGPUGROUP(s) MicroProfileForceDisableGroup(s, MicroProfileTokenTypeGpu)

#define MICROPROFILE_INVALID_TICK ((uint64_t)-1)
#define MICROPROFILE_GROUP_MASK_ALL 0xffffffffffff


typedef uint64_t MicroProfileToken;
typedef uint16_t MicroProfileGroupId;

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

struct MicroProfileState
{
	uint32_t nDisplay;
	uint32_t nMenuAllGroups;
	uint64_t nMenuActiveGroup;
	uint32_t nMenuAllThreads;
	uint32_t nAggregateFlip;
	uint32_t nBars;
	float fReferenceTime;
};


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
MICROPROFILE_API void MicroProfileDraw(uint32_t nWidth, uint32_t nHeight); //! call if drawing microprofilers
MICROPROFILE_API bool MicroProfileIsDrawing();
MICROPROFILE_API void MicroProfileToggleGraph(MicroProfileToken nToken);
MICROPROFILE_API bool MicroProfileDrawGraph(uint32_t nScreenWidth, uint32_t nScreenHeight);
MICROPROFILE_API void MicroProfileSetAggregateCount(uint32_t nCount); //!Set no. of frames to aggregate over. 0 for infinite
MICROPROFILE_API void MicroProfileToggleDisplayMode(); //switch between off, bars, detailed
MICROPROFILE_API void MicroProfileSetDisplayMode(int); //switch between off, bars, detailed
MICROPROFILE_API void MicroProfileClearGraph();
MICROPROFILE_API void MicroProfileTogglePause();
MICROPROFILE_API void MicroProfileGetState(MicroProfileState* pStateOut);
MICROPROFILE_API void MicroProfileSetState(MicroProfileState* pStateIn);
MICROPROFILE_API void MicroProfileForceEnableGroup(const char* pGroup, MicroProfileTokenType Type);
MICROPROFILE_API void MicroProfileForceDisableGroup(const char* pGroup, MicroProfileTokenType Type);
MICROPROFILE_API float MicroProfileGetTime(const char* pGroup, const char* pName);
MICROPROFILE_API void MicroProfileMousePosition(uint32_t nX, uint32_t nY, int nWheelDelta);
MICROPROFILE_API void MicroProfileModKey(uint32_t nKeyState);
MICROPROFILE_API void MicroProfileMouseButton(uint32_t nLeft, uint32_t nRight);
MICROPROFILE_API void MicroProfileOnThreadCreate(const char* pThreadName); //should be called from newly created threads
MICROPROFILE_API void MicroProfileOnThreadExit(); //call on exit to reuse log
MICROPROFILE_API void MicroProfileInitThreadLog();
MICROPROFILE_API void MicroProfileDrawLineVertical(int nX, int nTop, int nBottom, uint32_t nColor);
MICROPROFILE_API void MicroProfileDrawLineHorizontal(int nLeft, int nRight, int nY, uint32_t nColor);
MICROPROFILE_API void MicroProfileDumpTimers();



//UNDEFINED: MUST BE IMPLEMENTED ELSEWHERE
MICROPROFILE_API void MicroProfileDrawText(int nX, int nY, uint32_t nColor, const char* pText, uint32_t nNumCharacters);
MICROPROFILE_API void MicroProfileDrawBox(int nX, int nY, int nX1, int nY1, uint32_t nColor, MicroProfileBoxType = MicroProfileBoxTypeFlat);
MICROPROFILE_API void MicroProfileDrawLine2D(uint32_t nVertices, float* pVertices, uint32_t nColor);
MICROPROFILE_API uint32_t MicroProfileGpuInsertTimeStamp();
MICROPROFILE_API uint64_t MicroProfileGpuGetTimeStamp(uint32_t nKey);
MICROPROFILE_API uint64_t MicroProfileTicksPerSecondGpu();
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




#ifdef MICRO_PROFILE_IMPL

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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>


#define S g_MicroProfile
#define MICROPROFILE_MAX_TIMERS 1024
#define MICROPROFILE_MAX_GROUPS 48 //dont bump! no. of bits used it bitmask
#define MICROPROFILE_MAX_GRAPHS 5
#define MICROPROFILE_GRAPH_HISTORY 128
#define MICROPROFILE_BUFFER_SIZE ((MICROPROFILE_PER_THREAD_BUFFER_SIZE)/sizeof(MicroProfileLogEntry))
#define MICROPROFILE_MAX_THREADS 32
#define MICROPROFILE_MAX_CONTEXT_SWITCH_THREADS 256
#define MICROPROFILE_STACK_MAX 32
#define MICROPROFILE_MAX_PRESETS 5
#define MICROPROFILE_DEBUG 0
#define MICROPROFILE_TOOLTIP_MAX_STRINGS (32 + MICROPROFILE_MAX_GROUPS*2)
#define MICROPROFILE_TOOLTIP_STRING_BUFFER_SIZE 1024
#define MICROPROFILE_TOOLTIP_MAX_LOCKED 3
#define MICROPROFILE_MAX_FRAME_HISTORY 512
#define MICROPROFILE_ANIM_DELAY_PRC 0.5f
#define MICROPROFILE_GAP_TIME 50 //extra ms to fetch to close timers from earlier frames

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

enum MicroProfileDrawMask
{
	MP_DRAW_OFF			= 0x0,
	MP_DRAW_BARS		= 0x1,
	MP_DRAW_DETAILED	= 0x2,
	MP_DRAW_HIDDEN		= 0x3,
};

enum MicroProfileDrawBarsMask
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

struct MicroProfileTimer
{
	uint64_t nTicks;
	uint32_t nCount;
};

struct MicroProfileGroupInfo
{
	const char* pName;
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
	const char* pName;
	uint32_t nNameLen;
	uint32_t nColor;
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


#define MP_LOG_TICK_MASK  0x0000ffffffffffff
#define MP_LOG_INDEX_MASK 0x3fff000000000000
#define MP_LOG_BEGIN_MASK 0xc000000000000000
#define MP_LOG_META 0x1
#define MP_LOG_ENTER 0x2
#define MP_LOG_LEAVE 0x0
typedef uint64_t MicroProfileLogEntry;



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

struct MicroProfileFrameState
{
	int64_t nFrameStartCpu;
	int64_t nFrameStartGpu;
	uint32_t nLogStart[MICROPROFILE_MAX_GROUPS];
};

struct MicroProfileThreadLog
{
	MicroProfileThreadLog*  pNext;
	MicroProfileLogEntry	Log[MICROPROFILE_BUFFER_SIZE];

	std::atomic<uint32_t>	nPut;
	std::atomic<uint32_t>	nGet;
	uint32_t 				nActive;
	uint32_t 				nGpu;
	ThreadIdType			nThreadId;
	enum
	{
		THREAD_MAX_LEN = 64,
	};
	char					ThreadName[64];
	int 					nFreeListNext;
};

struct MicroProfileStringArray
{
	const char* ppStrings[MICROPROFILE_TOOLTIP_MAX_STRINGS];
	char Buffer[MICROPROFILE_TOOLTIP_STRING_BUFFER_SIZE];
	char* pBufferPos;
	uint32_t nNumStrings;
};


struct 
{
	uint32_t nTotalTimers;
	uint32_t nGroupCount;
	uint32_t nAggregateFlip;
	uint32_t nAggregateFlipCount;
	uint32_t nAggregateFrames;
	
	uint32_t nDisplay;
	uint32_t nBars;
	uint64_t nActiveGroup;
	uint32_t nActiveBars;

	uint64_t nForceGroup;

	//menu/mouse over stuff
	uint64_t nMenuActiveGroup;
	uint32_t nMenuAllGroups;
	uint32_t nMenuAllThreads;
	uint64_t nHoverToken;
	int64_t  nHoverTime;
	int 	 nHoverFrame;
#if MICROPROFILE_DEBUG
	uint64_t nHoverAddressEnter;
	uint64_t nHoverAddressLeave;
#endif
	uint32_t nOverflow;

	uint64_t nGroupMask;
	uint32_t nRunning;
	uint32_t nMaxGroupSize;

	float fGraphBaseTime; //old kill
	float fGraphBaseTimePos; //old kill
	float fReferenceTime;
	float fRcpReferenceTime;
	uint32_t nOpacityBackground;
	uint32_t nOpacityForeground;

	float fDetailedOffset; //display offset relative to start of latest displayable frame.
	float fDetailedRange; //no. of ms to display

	float fDetailedOffsetTarget;
	float fDetailedRangeTarget;

	int nOffsetY;

	uint32_t nWidth;
	uint32_t nHeight;

	uint32_t nBarWidth;
	uint32_t nBarHeight;


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

	uint32_t 				nMouseX;
	uint32_t 				nMouseY;
	int						nMouseWheelDelta;
	uint32_t				nMouseDownLeft;
	uint32_t				nMouseDownRight;
	uint32_t 				nMouseLeft;
	uint32_t 				nMouseRight;
	uint32_t 				nMouseLeftMod;
	uint32_t 				nMouseRightMod;
	uint32_t				nModDown;
	uint32_t 				nActiveMenu;

	uint32_t				nThreadActive[MICROPROFILE_MAX_THREADS];
	MicroProfileThreadLog* 	Pool[MICROPROFILE_MAX_THREADS];
	uint32_t				nNumLogs;
	uint32_t 				nMemUsage;
	int 					nFreeListHead;

	uint32_t 				nFrameCurrent;
	uint32_t 				nFramePut;

	MicroProfileFrameState Frames[MICROPROFILE_MAX_FRAME_HISTORY];
	
	MicroProfileLogEntry* pDisplayMouseOver;


	uint64_t				nFlipTicks;
	uint64_t				nFlipAggregate;
	uint64_t				nFlipMax;
	uint64_t				nFlipAggregateDisplay;
	uint64_t				nFlipMaxDisplay;
		

	MicroProfileStringArray LockedToolTips[MICROPROFILE_TOOLTIP_MAX_LOCKED];	
	uint32_t  				nLockedToolTipColor[MICROPROFILE_TOOLTIP_MAX_LOCKED];	
	int 					LockedToolTipFront;


	int64_t					nRangeBegin;
	int64_t					nRangeEnd;
	int64_t					nRangeBeginGpu;
	int64_t					nRangeEndGpu;
	uint32_t				nRangeBeginIndex;
	uint32_t 				nRangeEndIndex;
	MicroProfileThreadLog* 	pRangeLog;
	uint32_t				nHoverColor;
	uint32_t				nHoverColorShared;


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

} g_MicroProfile;

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
static uint32_t g_nMicroProfileBackColors[2] = {  0x474747, 0x313131 };

#define MICROPROFILE_NUM_CONTEXT_SWITCH_COLORS 16
static uint32_t g_nMicroProfileContextSwitchThreadColors[MICROPROFILE_NUM_CONTEXT_SWITCH_COLORS] = //palette generated by http://tools.medialab.sciences-po.fr/iwanthue/index.php
{
	0x63607B,
	0x755E2B,
	0x326A55,
	0x523135,
	0x904F42,
	0x87536B,
	0x346875,
	0x5E6046,
	0x35404C,
	0x224038,
	0x413D1E,
	0x5E3A26,
	0x5D6161,
	0x4C6234,
	0x7D564F,
	0x5C4352,
};
static uint32_t g_MicroProfileAggregatePresets[] = {0, 10, 20, 30, 60, 120};
static float g_MicroProfileReferenceTimePresets[] = {5.f, 10.f, 15.f,20.f, 33.33f, 66.66f, 100.f};
static uint32_t g_MicroProfileOpacityPresets[] = {0x40, 0x80, 0xc0, 0xff};
static const char* g_MicroProfilePresetNames[] = 
{
	"Default",
	"Render",
	"GPU",
	"Lighting",
	"AI",
	"Visibility",
	"Sound",
};


MICROPROFILE_DEFINE(g_MicroProfileDetailed, "MicroProfile", "Detailed View", 0x8888000);
MICROPROFILE_DEFINE(g_MicroProfileDrawGraph, "MicroProfile", "Draw Graph", 0xff44ee00);
MICROPROFILE_DEFINE(g_MicroProfileFlip, "MicroProfile", "MicroProfileFlip", 0x3355ee);
MICROPROFILE_DEFINE(g_MicroProfileThreadLoop, "MicroProfile", "ThreadLoop", 0x3355ee);
MICROPROFILE_DEFINE(g_MicroProfileClear, "MicroProfile", "Clear", 0x3355ee);
MICROPROFILE_DEFINE(g_MicroProfileAccumulate, "MicroProfile", "Accumulate", 0x3355ee);
MICROPROFILE_DEFINE(g_MicroProfileDrawBarView, "MicroProfile", "DrawBarView", 0x00dd77);
MICROPROFILE_DEFINE(g_MicroProfileDraw,"MicroProfile", "Draw", 0x737373);
MICROPROFILE_DEFINE(g_MicroProfileContextSwitchDraw, "MicroProfile", "ContextSwitchDraw", 0x730073);
MICROPROFILE_DEFINE(g_MicroProfileContextSwitchSearch,"MicroProfile", "ContextSwitchSearch", 0xDD7300);

void MicroProfileStartContextSwitchTrace();
void MicroProfileStopContextSwitchTrace();
bool MicroProfileIsLocalThread(uint32_t nThreadId);

inline std::recursive_mutex& MicroProfileMutex()
{
	static std::recursive_mutex Mutex;
	return Mutex;
}

template<typename T>
T MicroProfileMin(T a, T b)
{ return a < b ? a : b; }

template<typename T>
T MicroProfileMax(T a, T b)
{ return a > b ? a : b; }



void MicroProfileStringArrayClear(MicroProfileStringArray* pArray)
{
	pArray->nNumStrings = 0;
	pArray->pBufferPos = &pArray->Buffer[0];
}

void MicroProfileStringArrayAddLiteral(MicroProfileStringArray* pArray, const char* pLiteral)
{
	pArray->ppStrings[pArray->nNumStrings++] = pLiteral;
}

void MicroProfileStringArrayFormat(MicroProfileStringArray* pArray, const char* fmt, ...)
{
	pArray->ppStrings[pArray->nNumStrings++] = pArray->pBufferPos;
	va_list args;
	va_start (args, fmt);
	pArray->pBufferPos += 1 + vsprintf(pArray->pBufferPos, fmt, args);
	va_end(args);
	MP_ASSERT(pArray->pBufferPos < pArray->Buffer + MICROPROFILE_TOOLTIP_STRING_BUFFER_SIZE);
}
void MicroProfileStringArrayCopy(MicroProfileStringArray* pDest, MicroProfileStringArray* pSrc)
{
	memcpy(&pDest->ppStrings[0], &pSrc->ppStrings[0], sizeof(pDest->ppStrings));
	memcpy(&pDest->Buffer[0], &pSrc->Buffer[0], sizeof(pDest->Buffer));
	for(uint32_t i = 0; i < MICROPROFILE_TOOLTIP_MAX_STRINGS; ++i)
	{
		if(i < pSrc->nNumStrings)
		{
			if(pSrc->ppStrings[i] >= &pSrc->Buffer[0] && pSrc->ppStrings[i] < &pSrc->Buffer[0] + MICROPROFILE_TOOLTIP_STRING_BUFFER_SIZE)
			{
				pDest->ppStrings[i] += &pDest->Buffer[0] - &pSrc->Buffer[0];
			}
		}
	}
	pDest->nNumStrings = pSrc->nNumStrings;
}

MicroProfileThreadLog* MicroProfileCreateThreadLog(const char* pName);
void MicroProfileLoadPreset(const char* pSuffix);
void MicroProfileSavePreset(const char* pSuffix);


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
	return (uint16_t)S.TimerInfo[MicroProfileGetTimerIndex(t)].nGroupIndex;
}


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
		S.nGroupCount = 0;
		S.nBarWidth = 100;
		S.nBarHeight = MICROPROFILE_TEXT_HEIGHT;
		S.nActiveGroup = 0;
		S.nActiveBars = 0;
		S.nForceGroup = 0;
		S.nMenuAllGroups = 0;
		S.nMenuActiveGroup = 0;
		S.nMenuAllThreads = 1;
		S.nAggregateFlip = 30;
		S.nTotalTimers = 0;
		for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
		{
			S.Graph[i].nToken = MICROPROFILE_INVALID_TOKEN;
		}
		S.nBars = MP_DRAW_ALL;
		S.nRunning = 1;
		S.fGraphBaseTime = 40.f;
		S.nWidth = 100;
		S.nHeight = 100;
		S.nActiveMenu = (uint32_t)-1;
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


		S.fDetailedOffsetTarget = S.fDetailedOffset = 0.f;
		S.fDetailedRangeTarget = S.fDetailedRange = 50.f;

		S.nOpacityBackground = 0xff<<24;
		S.nOpacityForeground = 0xff<<24;
	}
	if(bUseLock)
		mutex.unlock();
}

void MicroProfileShutdown()
{
#if MICROPROFILE_CONTEXT_SWITCH_TRACE
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
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
		pLog->nThreadId = 0;
		S.nFreeListHead = nLogIndex;
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
	S.GroupInfo[S.nGroupCount].pName = pGroup;
	S.GroupInfo[S.nGroupCount].nNameLen = strlen(pGroup);
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
	S.TimerInfo[nTimerIndex].pName = pName;
	S.TimerInfo[nTimerIndex].nNameLen = strlen(pName);
	S.TimerInfo[nTimerIndex].nColor = nColor&0xffffff;
	S.TimerInfo[nTimerIndex].nGroupIndex = nGroupIndex;
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
		MicroProfileThreadLog* pLog = MicroProfileGetThreadLog();
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
	if(S.nRunning)
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

	{
		static int once = 0;
		if(0 == once)
		{
			uint32_t nDisplay = S.nDisplay;
			MicroProfileLoadPreset(g_MicroProfilePresetNames[0]);
			once++;
			S.nDisplay = nDisplay;// dont load display, just state
		}
	}

	if(S.nRunning)
	{
		S.nFramePut = (S.nFramePut+1) % MICROPROFILE_MAX_FRAME_HISTORY;
		S.nFrameCurrent = (S.nFramePut + MICROPROFILE_MAX_FRAME_HISTORY - MICROPROFILE_GPU_FRAME_DELAY - 1) % MICROPROFILE_MAX_FRAME_HISTORY;
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
		uint64_t nFrameStartGpu = pFrameCurrent->nFrameStartGpu;
		uint64_t nFrameEndGpu = pFrameNext->nFrameStartGpu;

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
				pFramePut->nLogStart[i] = pLog->nPut.load(std::memory_order_acquire);
				//need to keep last frame around to close timers. timers more than 1 frame old is ditched.
				pLog->nGet.store(pFrameCurrent->nLogStart[i], std::memory_order_relaxed);
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


					uint64_t nFrameStart = pLog->nGpu ? nFrameStartGpu : nFrameStartCpu;
					uint64_t nFrameEnd = pLog->nGpu ? nFrameEndGpu : nFrameEndCpu;
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
					uint32_t nStack[MICROPROFILE_STACK_MAX];
					int64_t nChildTickStack[MICROPROFILE_STACK_MAX];
					uint32_t nStackPos = 0;
					nChildTickStack[0] = 0;

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
								nStack[nStackPos++] = k;
								nChildTickStack[nStackPos] = 0;
							}
							else if(MP_LOG_META == nType)
							{
								if(nStackPos)
								{
									int64_t nMetaIndex = MicroProfileLogTimerIndex(LE);
									int64_t nMetaCount = MicroProfileLogGetTick(LE);
									MP_ASSERT(nMetaIndex < MICROPROFILE_META_MAX);
									int64_t nCounter = MicroProfileLogTimerIndex(pLog->Log[nStack[nStackPos-1]]);
									S.MetaCounters[nMetaIndex].nCounters[nCounter] += nMetaCount;
								}
							}
							else
							{
								MP_ASSERT(nType == MP_LOG_LEAVE);
								//todo: reconsider the fallback for Leaves without enters
								int64_t nTickStart = 0 != nStackPos ? pLog->Log[nStack[nStackPos-1]] : nFrameStart;
								int64_t nTicks = MicroProfileLogTickDifference(nTickStart, LE);
								int64_t nChildTicks = nChildTickStack[nStackPos];
								if(0 != nStackPos)
								{
									MP_ASSERT(MicroProfileLogTimerIndex(pLog->Log[nStack[nStackPos-1]]) == MicroProfileLogTimerIndex(LE));
									nStackPos--;
									nChildTickStack[nStackPos] += nTicks;
								}
								uint32_t nTimerIndex = MicroProfileLogTimerIndex(LE);
								S.Frame[nTimerIndex].nTicks += nTicks;								
								S.FrameExclusive[nTimerIndex] += (nTicks-nChildTicks);
								S.Frame[nTimerIndex].nCount += 1;
							}
						}
					}
					//todo: reconsider the fallback for enters without leaves
					for(uint32_t j = 0; j < nStackPos; ++j)
					{
						MicroProfileLogEntry LE = pLog->Log[nStack[j]];
						uint64_t nTicks = MicroProfileLogTickDifference(LE, nFrameEnd);
						uint32_t nTimerIndex = MicroProfileLogTimerIndex(LE);
						S.Frame[nTimerIndex].nTicks += nTicks;
					}
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
			memcpy(&S.Aggregate[0], &S.AggregateTimers[0], sizeof(S.Aggregate[0]) * S.nTotalTimers);
			memcpy(&S.AggregateMax[0], &S.MaxTimers[0], sizeof(S.AggregateMax[0]) * S.nTotalTimers);
			memcpy(&S.AggregateExclusive[0], &S.AggregateTimersExclusive[0], sizeof(S.AggregateExclusive[0]) * S.nTotalTimers);
			memcpy(&S.AggregateMaxExclusive[0], &S.MaxTimersExclusive[0], sizeof(S.AggregateMaxExclusive[0]) * S.nTotalTimers);
			
			S.nAggregateFrames = S.nAggregateFlipCount;
			S.nFlipAggregateDisplay = S.nFlipAggregate;
			S.nFlipMaxDisplay = S.nFlipMax;


			if(S.nAggregateFlip) // if 0 accumulate indefinitely
			{
				memset(&S.AggregateTimers[0], 0, sizeof(S.Aggregate[0]) * S.nTotalTimers);
				memset(&S.MaxTimers[0], 0, sizeof(S.MaxTimers[0]) * S.nTotalTimers);
				memset(&S.AggregateTimersExclusive[0], 0, sizeof(S.AggregateExclusive[0]) * S.nTotalTimers);
				memset(&S.MaxTimersExclusive[0], 0, sizeof(S.MaxTimersExclusive[0]) * S.nTotalTimers);
				S.nAggregateFlipCount = 0;
				S.nFlipAggregate = 0;
				S.nFlipMax = 0;
			}
		}
	}
	uint64_t nNewActiveGroup = 0;
	if(S.nDisplay && S.nRunning)
		nNewActiveGroup = S.nMenuAllGroups ? S.nGroupMask : S.nMenuActiveGroup;
	nNewActiveGroup |= S.nForceGroup;
	if(S.nActiveGroup != nNewActiveGroup)
		S.nActiveGroup = nNewActiveGroup;
	uint32_t nNewActiveBars = 0;
	if(S.nDisplay && S.nRunning)
		nNewActiveBars = S.nBars;
	if(nNewActiveBars != S.nActiveBars)
		S.nActiveBars = nNewActiveBars;

	S.fDetailedOffset = S.fDetailedOffset + (S.fDetailedOffsetTarget - S.fDetailedOffset) * MICROPROFILE_ANIM_DELAY_PRC;
	S.fDetailedRange = S.fDetailedRange + (S.fDetailedRangeTarget - S.fDetailedRange) * MICROPROFILE_ANIM_DELAY_PRC;

}

void MicroProfileSetDisplayMode(int nValue)
{
	nValue = nValue >= 0 && nValue < 4 ? nValue : S.nDisplay;
	S.nDisplay = nValue;
	S.fGraphBaseTime = 40.f;
	S.nOffsetY = 0;
}

void MicroProfileToggleDisplayMode()
{
	S.nDisplay = (S.nDisplay + 1) % 4;
	S.nOffsetY = 0;

}


void MicroProfileFloatWindowSize(const char** ppStrings, uint32_t nNumStrings, uint32_t* pColors, uint32_t& nWidth, uint32_t& nHeight, uint32_t* pStringLengths = 0)
{
	uint32_t* nStringLengths = pStringLengths ? pStringLengths : (uint32_t*)alloca(nNumStrings * sizeof(uint32_t));
	uint32_t nTextCount = nNumStrings/2;
	for(uint32_t i = 0; i < nTextCount; ++i)
	{
		uint32_t i0 = i * 2;
		uint32_t s0, s1;
		nStringLengths[i0] = s0 = (uint32_t)strlen(ppStrings[i0]);
		nStringLengths[i0+1] = s1 = (uint32_t)strlen(ppStrings[i0+1]);
		nWidth = MicroProfileMax(s0+s1, nWidth);
	}
	nWidth = (MICROPROFILE_TEXT_WIDTH+1) * (2+nWidth) + 2 * MICROPROFILE_BORDER_SIZE;
	if(pColors)
		nWidth += MICROPROFILE_TEXT_WIDTH + 1;
	nHeight = (MICROPROFILE_TEXT_HEIGHT+1) * nTextCount + 2 * MICROPROFILE_BORDER_SIZE;
}

void MicroProfileDrawFloatWindow(uint32_t nX, uint32_t nY, const char** ppStrings, uint32_t nNumStrings, uint32_t nColor, uint32_t* pColors = 0)
{
	uint32_t nWidth = 0, nHeight = 0;
	uint32_t* nStringLengths = (uint32_t*)alloca(nNumStrings * sizeof(uint32_t));
	MicroProfileFloatWindowSize(ppStrings, nNumStrings, pColors, nWidth, nHeight, nStringLengths);
	uint32_t nTextCount = nNumStrings/2;
	if(nX + nWidth > S.nWidth)
		nX = S.nWidth - nWidth;
	if(nY + nHeight > S.nHeight)
		nY = S.nHeight - nHeight;
	MicroProfileDrawBox(nX-1, nY-1, nX + nWidth+1, nY + nHeight+1, 0xff000000|nColor);
	MicroProfileDrawBox(nX, nY, nX + nWidth, nY + nHeight, 0xff000000);
	if(pColors)
	{
		nX += MICROPROFILE_TEXT_WIDTH+1;
		nWidth -= MICROPROFILE_TEXT_WIDTH+1;
	}
	for(uint32_t i = 0; i < nTextCount; ++i)
	{
		int i0 = i * 2;
		if(pColors)
		{
			MicroProfileDrawBox(nX-MICROPROFILE_TEXT_WIDTH, nY, nX, nY + MICROPROFILE_TEXT_WIDTH, pColors[i]|0xff000000);
		}
		MicroProfileDrawText(nX + 1, nY + 1, (uint32_t)-1, ppStrings[i0], strlen(ppStrings[i0]));
		MicroProfileDrawText(nX + nWidth - nStringLengths[i0+1] * (MICROPROFILE_TEXT_WIDTH+1), nY + 1, (uint32_t)-1, ppStrings[i0+1], strlen(ppStrings[i0+1]));
		nY += (MICROPROFILE_TEXT_HEIGHT+1);
	}
}

void MicroProfileDrawTextBox(uint32_t nX, uint32_t nY, const char** ppStrings, uint32_t nNumStrings, uint32_t nColor, uint32_t* pColors = 0)
{
	uint32_t nWidth = 0, nHeight = 0;
	uint32_t* nStringLengths = (uint32_t*)alloca(nNumStrings * sizeof(uint32_t));
	for(uint32_t i = 0; i < nNumStrings; ++i)
	{
		nStringLengths[i] = (uint32_t)strlen(ppStrings[i]);
		nWidth = MicroProfileMax(nWidth, nStringLengths[i]);
		nHeight++;
	}
	nWidth = (MICROPROFILE_TEXT_WIDTH+1) * (2+nWidth) + 2 * MICROPROFILE_BORDER_SIZE;
	nHeight = (MICROPROFILE_TEXT_HEIGHT+1) * nHeight + 2 * MICROPROFILE_BORDER_SIZE;
	if(nX + nWidth > S.nWidth)
		nX = S.nWidth - nWidth;
	if(nY + nHeight > S.nHeight)
		nY = S.nHeight - nHeight;
	MicroProfileDrawBox(nX, nY, nX + nWidth, nY + nHeight, 0xff000000);
	for(uint32_t i = 0; i < nNumStrings; ++i)
	{
		MicroProfileDrawText(nX + 1, nY + 1, (uint32_t)-1, ppStrings[i], strlen(ppStrings[i]));
		nY += (MICROPROFILE_TEXT_HEIGHT+1);
	}
}



void MicroProfileToolTipMeta(MicroProfileStringArray* pToolTip)
{
	if(S.nRangeBeginIndex != S.nRangeEndIndex && S.pRangeLog)
	{
		uint64_t nMetaSum[MICROPROFILE_META_MAX] = {0};

		uint32_t nRange[2][2];
		MicroProfileThreadLog* pLog = S.pRangeLog;


		MicroProfileGetRange(S.nRangeEndIndex, S.nRangeBeginIndex, nRange);
		for(uint32_t i = 0; i < 2; ++i)
		{
			uint32_t nStart = nRange[i][0];
			uint32_t nEnd = nRange[i][1];
			for(uint32_t j = nStart; j < nEnd; ++j)
			{
				MicroProfileLogEntry LE = pLog->Log[j];
				int nType = MicroProfileLogType(LE);
				if(MP_LOG_META == nType)
				{
					int64_t nMetaIndex = MicroProfileLogTimerIndex(LE);
					int64_t nMetaCount = MicroProfileLogGetTick(LE);
					MP_ASSERT(nMetaIndex < MICROPROFILE_META_MAX);
					nMetaSum[nMetaIndex] += nMetaCount;
				}
			}
		}
		bool bSpaced = false;
		for(int i = 0; i < MICROPROFILE_META_MAX; ++i)
		{
			if(S.MetaCounters[i].pName && nMetaSum[i])
			{
				if(!bSpaced)
				{
					bSpaced = true;
					MicroProfileStringArrayAddLiteral(pToolTip, "");
					MicroProfileStringArrayAddLiteral(pToolTip, "");					
				}
				MicroProfileStringArrayFormat(pToolTip, "%s", S.MetaCounters[i].pName);
				MicroProfileStringArrayFormat(pToolTip, "%5d", nMetaSum[i]);
			}
		}
	}
}


void MicroProfileDrawFloatTooltip(uint32_t nX, uint32_t nY, uint32_t nToken, uint64_t nTime)
{
	uint32_t nIndex = MicroProfileGetTimerIndex(nToken);
	uint32_t nAggregateFrames = S.nAggregateFrames ? S.nAggregateFrames : 1;
	uint32_t nAggregateCount = S.Aggregate[nIndex].nCount ? S.Aggregate[nIndex].nCount : 1;

	uint32_t nGroupId = MicroProfileGetGroupIndex(nToken);
	uint32_t nTimerId = MicroProfileGetTimerIndex(nToken);
	bool bGpu = S.GroupInfo[nGroupId].Type == MicroProfileTokenTypeGpu;

	float fToMs = MicroProfileTickToMsMultiplier(bGpu ? MicroProfileTicksPerSecondGpu() : MicroProfileTicksPerSecondCpu());

	float fMs = fToMs * (nTime);
	float fFrameMs = fToMs * (S.Frame[nIndex].nTicks);
	float fAverage = fToMs * (S.Aggregate[nIndex].nTicks/nAggregateFrames);
	float fCallAverage = fToMs * (S.Aggregate[nIndex].nTicks / nAggregateCount);
	float fMax = fToMs * (S.AggregateMax[nIndex]);

	float fFrameMsExclusive = fToMs * (S.FrameExclusive[nIndex]);
	float fAverageExclusive = fToMs * (S.AggregateExclusive[nIndex]/nAggregateFrames);
	float fMaxExclusive = fToMs * (S.AggregateMaxExclusive[nIndex]);


	MicroProfileStringArray ToolTip;
	MicroProfileStringArrayClear(&ToolTip);
	const char* pGroupName = S.GroupInfo[nGroupId].pName;
	const char* pTimerName = S.TimerInfo[nTimerId].pName;
	MicroProfileStringArrayFormat(&ToolTip, "%s", pGroupName);
	MicroProfileStringArrayFormat(&ToolTip,"%s", pTimerName);

#if MICROPROFILE_DEBUG
	MicroProfileStringArrayFormat(&ToolTip,"0x%p", S.nHoverAddressEnter);
	MicroProfileStringArrayFormat(&ToolTip,"0x%p", S.nHoverAddressLeave);
#endif
	
	if(nTime != (uint64_t)0)
	{
		MicroProfileStringArrayAddLiteral(&ToolTip, "Time:");
		MicroProfileStringArrayFormat(&ToolTip,"%6.3fms",  fMs);
		MicroProfileStringArrayAddLiteral(&ToolTip, "");
		MicroProfileStringArrayAddLiteral(&ToolTip, "");
	}

	MicroProfileStringArrayAddLiteral(&ToolTip, "Frame Time:");
	MicroProfileStringArrayFormat(&ToolTip,"%6.3fms",  fFrameMs);

	MicroProfileStringArrayAddLiteral(&ToolTip, "Average:");
	MicroProfileStringArrayFormat(&ToolTip,"%6.3fms",  fAverage);

	MicroProfileStringArrayAddLiteral(&ToolTip, "Max:");
	MicroProfileStringArrayFormat(&ToolTip,"%6.3fms",  fMax);

	MicroProfileStringArrayAddLiteral(&ToolTip, "");
	MicroProfileStringArrayAddLiteral(&ToolTip, "");

	MicroProfileStringArrayAddLiteral(&ToolTip, "Frame Call Average:");
	MicroProfileStringArrayFormat(&ToolTip,"%6.3fms",  fCallAverage);

	MicroProfileStringArrayAddLiteral(&ToolTip, "Frame Call Count:");
	MicroProfileStringArrayFormat(&ToolTip, "%6d",  nAggregateCount / nAggregateFrames);

	MicroProfileStringArrayAddLiteral(&ToolTip, "");
	MicroProfileStringArrayAddLiteral(&ToolTip, "");

	MicroProfileStringArrayAddLiteral(&ToolTip, "Exclusive Frame Time:");
	MicroProfileStringArrayFormat(&ToolTip, "%6.3fms",  fFrameMsExclusive);

	MicroProfileStringArrayAddLiteral(&ToolTip, "Exclusive Average:");
	MicroProfileStringArrayFormat(&ToolTip, "%6.3fms",  fAverageExclusive);

	MicroProfileStringArrayAddLiteral(&ToolTip, "Exclusive Max:");
	MicroProfileStringArrayFormat(&ToolTip, "%6.3fms",  fMaxExclusive);

	MicroProfileToolTipMeta(&ToolTip);


	MicroProfileDrawFloatWindow(nX, nY+20, &ToolTip.ppStrings[0], ToolTip.nNumStrings, S.TimerInfo[nTimerId].nColor);

	if(S.nMouseLeftMod)
	{
		int nIndex = (S.LockedToolTipFront + MICROPROFILE_TOOLTIP_MAX_LOCKED - 1) % MICROPROFILE_TOOLTIP_MAX_LOCKED;
		S.nLockedToolTipColor[nIndex] = S.TimerInfo[nTimerId].nColor;
		MicroProfileStringArrayCopy(&S.LockedToolTips[nIndex], &ToolTip);
		S.LockedToolTipFront = nIndex;

	}
}

#define MICROPROFILE_FRAME_HISTORY_HEIGHT 50
#define MICROPROFILE_FRAME_HISTORY_WIDTH 7
#define MICROPROFILE_FRAME_HISTORY_COLOR_CPU 0xffff7f27 //255 127 39
#define MICROPROFILE_FRAME_HISTORY_COLOR_GPU 0xff37a0ee //55 160 238
#define MICROPROFILE_FRAME_HISTORY_COLOR_HIGHTLIGHT 0x7733bb44
#define MICROPROFILE_FRAME_COLOR_HIGHTLIGHT 0x20009900
#define MICROPROFILE_FRAME_COLOR_HIGHTLIGHT_GPU 0x20996600
#define MICROPROFILE_NUM_FRAMES (MICROPROFILE_MAX_FRAME_HISTORY - (MICROPROFILE_GPU_FRAME_DELAY+1))

void MicroProfileZoomTo(int64_t nTickStart, int64_t nTickEnd)
{
	int64_t nStart = S.Frames[S.nFrameCurrent].nFrameStartCpu;
	float fToMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
	S.fDetailedOffsetTarget = MicroProfileLogTickDifference(nStart, nTickStart) * fToMs;
	S.fDetailedRangeTarget = MicroProfileLogTickDifference(nTickStart, nTickEnd) * fToMs;


}

void MicroProfileCenter(int64_t nTickCenter)
{
	int64_t nStart = S.Frames[S.nFrameCurrent].nFrameStartCpu;
	float fToMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
	float fCenter = MicroProfileLogTickDifference(nStart, nTickCenter) * fToMs;
	S.fDetailedOffsetTarget = S.fDetailedOffset = fCenter - 0.5f * S.fDetailedRange;
}
#ifdef MICROPROFILE_DEBUG
uint64_t* g_pMicroProfileDumpStart = 0;
uint64_t* g_pMicroProfileDumpEnd = 0;
void MicroProfileDebugDumpRange()
{
	if(g_pMicroProfileDumpStart != g_pMicroProfileDumpEnd)
	{
		uint64_t* pStart = g_pMicroProfileDumpStart;
		uint64_t* pEnd = g_pMicroProfileDumpEnd;
		while(pStart != pEnd)
		{
			uint64_t nTick = MicroProfileLogGetTick(*pStart);
			uint64_t nToken = MicroProfileLogTimerIndex(*pStart);
			uint32_t nTimerId = MicroProfileGetTimerIndex(nToken);
	
			const char* pTimerName = S.TimerInfo[nTimerId].pName;
			char buffer[256];
			int type = MicroProfileLogType(*pStart);

			const char* pBegin = type == MP_LOG_LEAVE ? "END" : 
				(type == MP_LOG_ENTER ? "BEGIN" : "META");
			snprintf(buffer, 255, "DUMP 0x%p: %s :: %llx: %s\n", pStart, pBegin,  nTick, pTimerName);
#ifdef _WIN32
			OutputDebugString(buffer);
#else
			printf("%s", buffer);
#endif
			pStart++;
		}

		g_pMicroProfileDumpStart = g_pMicroProfileDumpEnd;
	}
}
#define MP_DEBUG_DUMP_RANGE() MicroProfileDebugDumpRange();
#else
#define MP_DEBUG_DUMP_RANGE() do{} while(0)
#endif

#define MICROPROFILE_HOVER_DIST 0.5f

void MicroProfileDrawDetailedContextSwitchBars(uint32_t nY, uint32_t nThreadId, uint32_t nContextSwitchStart, uint32_t nContextSwitchEnd, int64_t nBaseTicks, uint32_t nBaseY)
{
	MICROPROFILE_SCOPE(g_MicroProfileContextSwitchDraw);
	int64_t nTickIn = -1;
	uint32_t nThreadBefore = -1;
	float fToMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
	float fMsToScreen = S.nWidth / S.fDetailedRange;
	float fMouseX = (float)S.nMouseX;
	float fMouseY = (float)S.nMouseY;


	for(uint32_t j = nContextSwitchStart; j != nContextSwitchEnd; j = (j+1) % MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE)
	{
		MP_ASSERT(j < MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE);
		MicroProfileContextSwitch CS = S.ContextSwitch[j];

		if(nTickIn == -1)
		{
			if(CS.nThreadIn == nThreadId)
			{
				nTickIn = CS.nTicks;
				nThreadBefore = CS.nThreadOut;
			}
		}
		else
		{
			if(CS.nThreadOut == nThreadId)
			{
				int64_t nTickOut = CS.nTicks;
				float fMsStart = fToMs * MicroProfileLogTickDifference(nBaseTicks, nTickIn);
				float fMsEnd = fToMs * MicroProfileLogTickDifference(nBaseTicks, nTickOut);
				if(fMsStart <= fMsEnd)
				{
					float fXStart = fMsStart * fMsToScreen;
					float fXEnd = fMsEnd * fMsToScreen;
					float fYStart = (float)nY;
					float fYEnd = fYStart + (MICROPROFILE_DETAILED_CONTEXT_SWITCH_HEIGHT);
					uint32_t nColor = g_nMicroProfileContextSwitchThreadColors[CS.nCpu%MICROPROFILE_NUM_CONTEXT_SWITCH_COLORS];
					float fXDist = MicroProfileMax(fXStart - fMouseX, fMouseX - fXEnd);
					bool bHover = fXDist < MICROPROFILE_HOVER_DIST && fYStart <= fMouseY && fMouseY <= fYEnd && nBaseY < fMouseY;
					if(bHover)
					{
						S.nRangeBegin = nTickIn;
						S.nRangeEnd = nTickOut;
						S.nContextSwitchHoverTickIn = nTickIn;
						S.nContextSwitchHoverTickOut = nTickOut;
						S.nContextSwitchHoverThread = CS.nThreadOut;
						S.nContextSwitchHoverThreadBefore = nThreadBefore;
						S.nContextSwitchHoverThreadAfter = CS.nThreadIn;
						S.nContextSwitchHoverCpuNext = CS.nCpu;
						nColor = S.nHoverColor;
					}
					if(CS.nCpu == S.nContextSwitchHoverCpu)
					{
						nColor = S.nHoverColorShared;
					}
					MicroProfileDrawBox(fXStart, fYStart, fXEnd, fYEnd, nColor|S.nOpacityForeground, MicroProfileBoxTypeFlat);
				}
				nTickIn = -1;
			}
		}
	}
}

void MicroProfileDrawDetailedBars(uint32_t nWidth, uint32_t nHeight, int nBaseY, int nSelectedFrame)
{
	MP_DEBUG_DUMP_RANGE();
	int nY = nBaseY - S.nOffsetY;
	int64_t nNumBoxes = 0;
	int64_t nNumLines = 0;

	uint32_t nFrameNext = (S.nFrameCurrent+1) % MICROPROFILE_MAX_FRAME_HISTORY;
	MicroProfileFrameState* pFrameCurrent = &S.Frames[S.nFrameCurrent];
	MicroProfileFrameState* pFrameNext = &S.Frames[nFrameNext];

	S.nRangeBegin = 0; 
	S.nRangeEnd = 0;
	S.nRangeBeginGpu = 0;
	S.nRangeEndGpu = 0;
	S.nRangeBeginIndex = S.nRangeEndIndex = 0;
	S.pRangeLog = 0;
	uint64_t nFrameStartCpu = pFrameCurrent->nFrameStartCpu;
	uint64_t nFrameStartGpu = pFrameCurrent->nFrameStartGpu;
	float fToMsCpu = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
	float fToMsGpu = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondGpu());

	float fDetailedOffset = S.fDetailedOffset;
	float fDetailedRange = S.fDetailedRange;
	int64_t nDetailedOffsetTicksCpu = MicroProfileMsToTick(fDetailedOffset, MicroProfileTicksPerSecondCpu());
	int64_t nDetailedOffsetTicksGpu = MicroProfileMsToTick(fDetailedOffset, MicroProfileTicksPerSecondGpu());
	int64_t nBaseTicksCpu = nDetailedOffsetTicksCpu + nFrameStartCpu;
	int64_t nBaseTicksGpu = nDetailedOffsetTicksGpu + nFrameStartGpu;
	int64_t nBaseTicksEndCpu = nBaseTicksCpu + MicroProfileMsToTick(fDetailedRange, MicroProfileTicksPerSecondCpu());

	MicroProfileFrameState* pFrameFirst = pFrameCurrent;
	int64_t nGapTime = MicroProfileTicksPerSecondCpu() * MICROPROFILE_GAP_TIME / 1000;
	for(uint32_t i = 0; i < MICROPROFILE_MAX_FRAME_HISTORY - MICROPROFILE_GPU_FRAME_DELAY; ++i)
	{
		uint32_t nNextIndex = (S.nFrameCurrent + MICROPROFILE_MAX_FRAME_HISTORY - i) % MICROPROFILE_MAX_FRAME_HISTORY;
		pFrameFirst = &S.Frames[nNextIndex];
		if(pFrameFirst->nFrameStartCpu <= nBaseTicksCpu-nGapTime)
			break;
	}

	float fMsBase = fToMsCpu * nDetailedOffsetTicksCpu;
	float fMs = fDetailedRange;
	float fMsEnd = fMs + fMsBase;
	float fWidth = (float)nWidth;
	float fMsToScreen = fWidth / fMs;

	{
		float fRate = floor(2*(log10(fMs)-1))/2;
		float fStep = powf(10.f, fRate);
		float fRcpStep = 1.f / fStep;
		int nColorIndex = (int)(floor(fMsBase*fRcpStep));
		float fStart = floor(fMsBase*fRcpStep) * fStep;
		for(float f = fStart; f < fMsEnd; )
		{
			float fStart = f;
			float fNext = f + fStep;
			MicroProfileDrawBox(((fStart-fMsBase) * fMsToScreen), nBaseY, (fNext-fMsBase) * fMsToScreen+1, nBaseY + nHeight, S.nOpacityBackground | g_nMicroProfileBackColors[nColorIndex++ & 1]);
			f = fNext;
		}
	}

	nY += MICROPROFILE_TEXT_HEIGHT+1;
	MicroProfileLogEntry* pMouseOver = S.pDisplayMouseOver;
	MicroProfileLogEntry* pMouseOverNext = 0;
	uint64_t nMouseOverToken = pMouseOver ? MicroProfileLogTimerIndex(*pMouseOver) : MICROPROFILE_INVALID_TOKEN;
	float fMouseX = (float)S.nMouseX;
	float fMouseY = (float)S.nMouseY;
	uint64_t nHoverToken = MICROPROFILE_INVALID_TOKEN;
	int64_t nHoverTime = 0;

	static int nHoverCounter = 155;
	static int nHoverCounterDelta = 10;
	nHoverCounter += nHoverCounterDelta;
	if(nHoverCounter >= 245)
		nHoverCounterDelta = -10;
	else if(nHoverCounter < 100)
		nHoverCounterDelta = 10;
	S.nHoverColor = (nHoverCounter<<24)|(nHoverCounter<<16)|(nHoverCounter<<8)|nHoverCounter;
	uint32_t nHoverCounterShared = nHoverCounter>>2;
	S.nHoverColorShared = (nHoverCounterShared<<24)|(nHoverCounterShared<<16)|(nHoverCounterShared<<8)|nHoverCounterShared;

	uint32_t nLinesDrawn[MICROPROFILE_STACK_MAX]={0};

	uint32_t nContextSwitchHoverThreadAfter = S.nContextSwitchHoverThreadAfter;
	uint32_t nContextSwitchHoverThreadBefore = S.nContextSwitchHoverThreadBefore;
	S.nContextSwitchHoverThread = S.nContextSwitchHoverThreadAfter = S.nContextSwitchHoverThreadBefore = -1;

	uint32_t nContextSwitchStart = -1;
	uint32_t nContextSwitchEnd = -1;
	S.nContextSwitchHoverCpuNext = 0xff;
	S.nContextSwitchHoverTickIn = -1;
	S.nContextSwitchHoverTickOut = -1;
	if(S.bContextSwitchRunning)
	{
		MICROPROFILE_SCOPE(g_MicroProfileContextSwitchSearch);
		uint32_t nContextSwitchPut = S.nContextSwitchPut;
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
	}

	bool bSkipBarView = S.bContextSwitchRunning && S.bContextSwitchNoBars;

	if(!bSkipBarView)
	{
		for(uint32_t i = 0; i < MICROPROFILE_MAX_THREADS; ++i)
		{
			MicroProfileThreadLog* pLog = S.Pool[i];
			if(!pLog)
				continue;

			uint32_t nPut = pFrameNext->nLogStart[i];
			///note: this may display new samples as old data, but this will only happen when
			// 		 unpaused, where the detailed view is hardly perceptible
			uint32_t nFront = S.Pool[i]->nPut.load(std::memory_order_relaxed); 
			MicroProfileFrameState* pFrameLogFirst = pFrameCurrent;
			MicroProfileFrameState* pFrameLogLast = pFrameNext;
			uint32_t nGet = pFrameLogFirst->nLogStart[i];
			do
			{
				MP_ASSERT(pFrameLogFirst >= &S.Frames[0] && pFrameLogFirst < &S.Frames[MICROPROFILE_MAX_FRAME_HISTORY]);
				uint32_t nNewGet = pFrameLogFirst->nLogStart[i];
				bool bIsValid = false;
				if(nPut < nFront)
				{
					bIsValid = nNewGet <= nPut || nNewGet >= nFront;
				}
				else
				{
					bIsValid = nNewGet <= nPut && nNewGet >= nFront;
				}
				if(bIsValid)
				{
					nGet = nNewGet;
					if(pFrameLogFirst->nFrameStartCpu > nBaseTicksEndCpu)
					{
						pFrameLogLast = pFrameLogFirst;//pick the last frame that ends after 
					}


					pFrameLogFirst--;
					if(pFrameLogFirst < &S.Frames[0])
						pFrameLogFirst = &S.Frames[MICROPROFILE_MAX_FRAME_HISTORY-1]; 
				}
				else
				{
					break;
				}
			}while(pFrameLogFirst != pFrameFirst);


			if(nGet == (uint32_t)-1)
				continue;
			MP_ASSERT(nGet != (uint32_t)-1);

			nPut = pFrameLogLast->nLogStart[i];

			uint32_t nRange[2][2] = { {0, 0}, {0, 0}, };

			MicroProfileGetRange(nPut, nGet, nRange);
			if(nPut == nGet) 
				continue;
			if(0==S.nThreadActive[i] && 0==S.nMenuAllThreads)
				continue;
			uint32_t nMaxStackDepth = 0;

			bool bGpu = pLog->nGpu != 0;
			float fToMs = bGpu ? fToMsGpu : fToMsCpu;
			int64_t nBaseTicks = bGpu ? nBaseTicksGpu : nBaseTicksCpu;
			char ThreadName[MicroProfileThreadLog::THREAD_MAX_LEN + 16];
			uint64_t nThreadId = pLog->nThreadId;
			snprintf(ThreadName, sizeof(ThreadName)-1, "%04llx: %s", nThreadId, &pLog->ThreadName[0] );
			nY += 3;
			uint32_t nThreadColor = -1;
			if(pLog->nThreadId == nContextSwitchHoverThreadAfter || pLog->nThreadId == nContextSwitchHoverThreadBefore)
				nThreadColor = S.nHoverColorShared|0x906060;
			MicroProfileDrawText(0, nY, nThreadColor, &ThreadName[0], strlen(&ThreadName[0]));		
			nY += 3;
			nY += MICROPROFILE_TEXT_HEIGHT + 1;

			if(S.bContextSwitchRunning)
			{
				MicroProfileDrawDetailedContextSwitchBars(nY, pLog->nThreadId, nContextSwitchStart, nContextSwitchEnd, nBaseTicks, nBaseY);
				nY -= MICROPROFILE_DETAILED_BAR_HEIGHT;
				nY += MICROPROFILE_DETAILED_CONTEXT_SWITCH_HEIGHT+1;
			}

			uint32_t nYDelta = MICROPROFILE_DETAILED_BAR_HEIGHT;
			uint32_t nStack[MICROPROFILE_STACK_MAX];
			uint32_t nStackPos = 0;
			for(uint32_t j = 0; j < 2; ++j)
			{
				uint32_t nStart = nRange[j][0];
				uint32_t nEnd = nRange[j][1];
				for(uint32_t k = nStart; k < nEnd; ++k)
				{
					MicroProfileLogEntry* pEntry = pLog->Log + k;
					int nType = MicroProfileLogType(*pEntry);
					if(MP_LOG_ENTER == nType)
					{
						MP_ASSERT(nStackPos < MICROPROFILE_STACK_MAX);
						nStack[nStackPos++] = k;
					}
					else if(MP_LOG_META == nType)
					{

					}
					else if(MP_LOG_LEAVE == nType)
					{
						if(0 == nStackPos)
						{
							continue;
						}

						MicroProfileLogEntry* pEntryEnter = pLog->Log + nStack[nStackPos-1];
						if(MicroProfileLogTimerIndex(*pEntryEnter) != MicroProfileLogTimerIndex(*pEntry))
						{
							//uprintf("mismatch %llx %llx\n", pEntryEnter->nToken, pEntry->nToken);
							continue;
						}
						int64_t nTickStart = MicroProfileLogGetTick(*pEntryEnter);
						int64_t nTickEnd = MicroProfileLogGetTick(*pEntry);
						uint64_t nTimerIndex = MicroProfileLogTimerIndex(*pEntry);
						uint32_t nColor = S.TimerInfo[nTimerIndex].nColor;
						if(nMouseOverToken == nTimerIndex)
						{
							if(pEntry == pMouseOver)
							{
								nColor = S.nHoverColor;
								if(bGpu)
								{
									S.nRangeBeginGpu = *pEntryEnter;
									S.nRangeEndGpu = *pEntry;
									S.nRangeBeginIndex = nStack[nStackPos-1];
									S.nRangeEndIndex = k;
									S.pRangeLog = pLog;
								}
								else
								{
									S.nRangeBegin = *pEntryEnter;
									S.nRangeEnd = *pEntry;
									S.nRangeBeginIndex = nStack[nStackPos-1];
									S.nRangeEndIndex = k;
									S.pRangeLog = pLog;

								}
							}
							else
							{
								nColor = S.nHoverColorShared;
							}
						}

						nMaxStackDepth = MicroProfileMax(nMaxStackDepth, nStackPos);
						float fMsStart = fToMs * MicroProfileLogTickDifference(nBaseTicks, nTickStart);
						float fMsEnd = fToMs * MicroProfileLogTickDifference(nBaseTicks, nTickEnd);
						MP_ASSERT(fMsStart <= fMsEnd);
						float fXStart = fMsStart * fMsToScreen;
						float fXEnd = fMsEnd * fMsToScreen;
						float fYStart = (float)(nY + nStackPos * nYDelta);
						float fYEnd = fYStart + (MICROPROFILE_DETAILED_BAR_HEIGHT);
						float fXDist = MicroProfileMax(fXStart - fMouseX, fMouseX - fXEnd);
						bool bHover = fXDist < MICROPROFILE_HOVER_DIST && fYStart <= fMouseY && fMouseY <= fYEnd && nBaseY < fMouseY;
						uint32_t nIntegerWidth = (uint32_t)(fXEnd - fXStart);
						if(nIntegerWidth)
						{
							if(bHover && S.nActiveMenu == -1)
							{
								nHoverToken = MicroProfileLogTimerIndex(*pEntry);
	#if MICROPROFILE_DEBUG
								S.nHoverAddressEnter = (uint64_t)pEntryEnter;
								S.nHoverAddressLeave = (uint64_t)pEntry;
	#endif
								nHoverTime = MicroProfileLogTickDifference(nTickStart, nTickEnd);
								pMouseOverNext = pEntry;
							}

							MicroProfileDrawBox(fXStart, fYStart, fXEnd, fYEnd, nColor|S.nOpacityForeground, MicroProfileBoxTypeBar);
#if MICROPROFILE_DETAILED_BAR_NAMES
							if(nIntegerWidth>3*MICROPROFILE_TEXT_WIDTH)
							{
								int nCharacters = (nIntegerWidth - 2*MICROPROFILE_TEXT_WIDTH) / MICROPROFILE_TEXT_WIDTH;
								MicroProfileDrawText(fXStart+1, fYStart+1, -1, S.TimerInfo[nTimerIndex].pName, MicroProfileMin<uint32_t>(S.TimerInfo[nTimerIndex].nNameLen, nCharacters));
							}
#endif
							++nNumBoxes;
						}
						else
						{
							float fXAvg = 0.5f * (fXStart + fXEnd);
							int nLineX = (int)floor(fXAvg+0.5f);
							if(nLineX != (int)nLinesDrawn[nStackPos])
							{
								if(bHover && S.nActiveMenu == -1)
								{
									nHoverToken = (uint32_t)MicroProfileLogTimerIndex(*pEntry);
									nHoverTime = MicroProfileLogTickDifference(nTickStart, nTickEnd);
									pMouseOverNext = pEntry;
								}
								nLinesDrawn[nStackPos] = nLineX;
								MicroProfileDrawLineVertical(nLineX, fYStart + 0.5f, fYEnd + 0.5f, nColor|S.nOpacityForeground);
								++nNumLines;
							}
						}
						nStackPos--;
					}
				}
			}
			nY += nMaxStackDepth * nYDelta + MICROPROFILE_DETAILED_BAR_HEIGHT+1;
		}
	}
	if(S.bContextSwitchRunning && (S.bContextSwitchAllThreads||S.bContextSwitchNoBars))
	{
		uint32_t nNumThreads = 0;
		uint32_t nThreads[MICROPROFILE_MAX_CONTEXT_SWITCH_THREADS];
		for(uint32_t i = 0; i < MICROPROFILE_MAX_THREADS && S.Pool[i]; ++i)
			nThreads[nNumThreads++] = S.Pool[i]->nThreadId;
		uint32_t nNumThreadsBase = nNumThreads;
		if(S.bContextSwitchAllThreads)
		{
			for(uint32_t i = nContextSwitchStart; i != nContextSwitchEnd; i = (i+1) % MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE)
			{
				MicroProfileContextSwitch CS = S.ContextSwitch[i];
				uint32_t nThreadId = CS.nThreadIn;
				if(nThreadId)
				{
					bool bSeen = false;
					for(uint32_t j = 0; j < nNumThreads; ++j)
					{
						if(nThreads[j] == nThreadId)
						{
							bSeen = true;
							break;				
						}
					}
					if(!bSeen)
					{
						nThreads[nNumThreads++] = nThreadId;
					}
				}
				if(nNumThreads == MICROPROFILE_MAX_CONTEXT_SWITCH_THREADS)
				{
					S.nOverflow = 10;
					break;
				}
			}
			std::sort(&nThreads[nNumThreadsBase], &nThreads[nNumThreads]);
		}
		uint32_t nStart = nNumThreadsBase;
		if(S.bContextSwitchNoBars)
			nStart = 0;
		for(uint32_t i = nStart; i < nNumThreads; ++i)
		{
			uint32_t nThreadId = nThreads[i];
			if(nThreadId)
			{
				char ThreadName[MicroProfileThreadLog::THREAD_MAX_LEN + 16];
				const char* cLocal = MicroProfileIsLocalThread(nThreadId) ? "*": " ";
				int nStrLen = snprintf(ThreadName, sizeof(ThreadName)-1, "%04x: %s", nThreadId, i < nNumThreadsBase ? &S.Pool[i]->ThreadName[0] : cLocal );
				uint32_t nThreadColor = -1;
				if(nThreadId == nContextSwitchHoverThreadAfter || nThreadId == nContextSwitchHoverThreadBefore)
					nThreadColor = S.nHoverColorShared|0x906060;
				MicroProfileDrawDetailedContextSwitchBars(nY+2, nThreadId, nContextSwitchStart, nContextSwitchEnd, nBaseTicksCpu, nBaseY);
				MicroProfileDrawText(0, nY, nThreadColor, &ThreadName[0], nStrLen);					
				nY += MICROPROFILE_TEXT_HEIGHT+1;
			}
		}
	}

	S.nContextSwitchHoverCpu = S.nContextSwitchHoverCpuNext;




	S.pDisplayMouseOver = pMouseOverNext;

	if(!S.nRunning)
	{
		if(nHoverToken != MICROPROFILE_INVALID_TOKEN && nHoverTime)
		{
			S.nHoverToken = nHoverToken;
			S.nHoverTime = nHoverTime;
		}

		if(nSelectedFrame != -1)
		{
			S.nRangeBegin = S.Frames[nSelectedFrame].nFrameStartCpu;
			S.nRangeEnd = S.Frames[(nSelectedFrame+1)%MICROPROFILE_MAX_FRAME_HISTORY].nFrameStartCpu;
			S.nRangeBeginGpu = S.Frames[nSelectedFrame].nFrameStartGpu;
			S.nRangeEndGpu = S.Frames[(nSelectedFrame+1)%MICROPROFILE_MAX_FRAME_HISTORY].nFrameStartGpu;
		}
		if(S.nRangeBegin != S.nRangeEnd)
		{
			float fMsStart = fToMsCpu * MicroProfileLogTickDifference(nBaseTicksCpu, S.nRangeBegin);
			float fMsEnd = fToMsCpu * MicroProfileLogTickDifference(nBaseTicksCpu, S.nRangeEnd);
			float fXStart = fMsStart * fMsToScreen;
			float fXEnd = fMsEnd * fMsToScreen;	
			MicroProfileDrawBox(fXStart, nBaseY, fXEnd, nHeight, MICROPROFILE_FRAME_COLOR_HIGHTLIGHT, MicroProfileBoxTypeFlat);
			MicroProfileDrawLineVertical(fXStart, nBaseY, nHeight, MICROPROFILE_FRAME_COLOR_HIGHTLIGHT | 0x44000000);
			MicroProfileDrawLineVertical(fXEnd, nBaseY, nHeight, MICROPROFILE_FRAME_COLOR_HIGHTLIGHT | 0x44000000);

			fMsStart += fDetailedOffset;
			fMsEnd += fDetailedOffset;
			char sBuffer[32];
			uint32_t nLenStart = snprintf(sBuffer, sizeof(sBuffer)-1, "%.2fms", fMsStart);
			float fStartTextWidth = (float)((1+MICROPROFILE_TEXT_WIDTH) * nLenStart);
			float fStartTextX = fXStart - fStartTextWidth - 2;
			MicroProfileDrawBox(fStartTextX, nBaseY, fStartTextX + fStartTextWidth + 2, MICROPROFILE_TEXT_HEIGHT + 2 + nBaseY, 0x33000000, MicroProfileBoxTypeFlat);
			MicroProfileDrawText(fStartTextX+1, nBaseY, (uint32_t)-1, sBuffer, nLenStart);
			uint32_t nLenEnd = snprintf(sBuffer, sizeof(sBuffer)-1, "%.2fms", fMsEnd);
			MicroProfileDrawBox(fXEnd+1, nBaseY, fXEnd+1+(1+MICROPROFILE_TEXT_WIDTH) * nLenEnd + 3, MICROPROFILE_TEXT_HEIGHT + 2 + nBaseY, 0x33000000, MicroProfileBoxTypeFlat);
			MicroProfileDrawText(fXEnd+2, nBaseY+1, (uint32_t)-1, sBuffer, nLenEnd);		

			if(S.nMouseRight)
			{
				MicroProfileZoomTo(S.nRangeBegin, S.nRangeEnd);
			}
		}

		if(S.nRangeBeginGpu != S.nRangeEndGpu)
		{
			float fMsStart = fToMsGpu * MicroProfileLogTickDifference(nBaseTicksGpu, S.nRangeBeginGpu);
			float fMsEnd = fToMsGpu * MicroProfileLogTickDifference(nBaseTicksGpu, S.nRangeEndGpu);
			float fXStart = fMsStart * fMsToScreen;
			float fXEnd = fMsEnd * fMsToScreen;	
			MicroProfileDrawBox(fXStart, nBaseY, fXEnd, nHeight, MICROPROFILE_FRAME_COLOR_HIGHTLIGHT_GPU, MicroProfileBoxTypeFlat);
			MicroProfileDrawLineVertical(fXStart, nBaseY, nHeight, MICROPROFILE_FRAME_COLOR_HIGHTLIGHT_GPU | 0x44000000);
			MicroProfileDrawLineVertical(fXEnd, nBaseY, nHeight, MICROPROFILE_FRAME_COLOR_HIGHTLIGHT_GPU | 0x44000000);

			nBaseY += MICROPROFILE_TEXT_HEIGHT+1;

			fMsStart += fDetailedOffset;
			fMsEnd += fDetailedOffset;
			char sBuffer[32];
			uint32_t nLenStart = snprintf(sBuffer, sizeof(sBuffer)-1, "%.2fms", fMsStart);
			float fStartTextWidth = (float)((1+MICROPROFILE_TEXT_WIDTH) * nLenStart);
			float fStartTextX = fXStart - fStartTextWidth - 2;
			MicroProfileDrawBox(fStartTextX, nBaseY, fStartTextX + fStartTextWidth + 2, MICROPROFILE_TEXT_HEIGHT + 2 + nBaseY, 0x33000000, MicroProfileBoxTypeFlat);
			MicroProfileDrawText(fStartTextX+1, nBaseY, (uint32_t)-1, sBuffer, nLenStart);
			uint32_t nLenEnd = snprintf(sBuffer, sizeof(sBuffer)-1, "%.2fms", fMsEnd);
			MicroProfileDrawBox(fXEnd+1, nBaseY, fXEnd+1+(1+MICROPROFILE_TEXT_WIDTH) * nLenEnd + 3, MICROPROFILE_TEXT_HEIGHT + 2 + nBaseY, 0x33000000, MicroProfileBoxTypeFlat);
			MicroProfileDrawText(fXEnd+2, nBaseY+1, (uint32_t)-1, sBuffer, nLenEnd);
		}
	}
}


void MicroProfileDrawDetailedFrameHistory(uint32_t nWidth, uint32_t nHeight, uint32_t nBaseY, uint32_t nSelectedFrame)
{
	const uint32_t nBarHeight = MICROPROFILE_FRAME_HISTORY_HEIGHT;
	float fBaseX = (float)nWidth;
	float fDx = fBaseX / MICROPROFILE_NUM_FRAMES;

	uint32_t nLastIndex =  (S.nFrameCurrent+1) % MICROPROFILE_MAX_FRAME_HISTORY;
	MicroProfileDrawBox(0, nBaseY, nWidth, nBaseY+MICROPROFILE_FRAME_HISTORY_HEIGHT, 0xff000000 | g_nMicroProfileBackColors[0], MicroProfileBoxTypeFlat);
	float fToMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu()) * S.fRcpReferenceTime;
	float fToMsGpu = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondGpu()) * S.fRcpReferenceTime;

	
	MicroProfileFrameState* pFrameCurrent = &S.Frames[S.nFrameCurrent];
	uint64_t nFrameStartCpu = pFrameCurrent->nFrameStartCpu;
	int64_t nDetailedOffsetTicksCpu = MicroProfileMsToTick(S.fDetailedOffset, MicroProfileTicksPerSecondCpu());
	int64_t nCpuStart = nDetailedOffsetTicksCpu + nFrameStartCpu;
	int64_t nCpuEnd = nCpuStart + MicroProfileMsToTick(S.fDetailedRange, MicroProfileTicksPerSecondCpu());;


	float fSelectionStart = (float)nWidth;
	float fSelectionEnd = 0.f;
	for(uint32_t i = 0; i < MICROPROFILE_NUM_FRAMES; ++i)
	{
		uint32_t nIndex = (S.nFrameCurrent + MICROPROFILE_MAX_FRAME_HISTORY - i) % MICROPROFILE_MAX_FRAME_HISTORY;
		MicroProfileFrameState* pCurrent = &S.Frames[nIndex];
		MicroProfileFrameState* pNext = &S.Frames[nLastIndex];
		
		int64_t nTicks = pNext->nFrameStartCpu - pCurrent->nFrameStartCpu;
		int64_t nTicksGpu = pNext->nFrameStartGpu - pCurrent->nFrameStartGpu;
		float fScale = fToMs * nTicks;
		float fScaleGpu = fToMsGpu * nTicksGpu;
		fScale = fScale > 1.f ? 0.f : 1.f - fScale;
		fScaleGpu = fScaleGpu > 1.f ? 0.f : 1.f - fScaleGpu;
		float fXEnd = fBaseX;
		float fXStart = fBaseX - fDx;
		fBaseX = fXStart;
		uint32_t nColor = MICROPROFILE_FRAME_HISTORY_COLOR_CPU;
		if(nIndex == nSelectedFrame)
			nColor = (uint32_t)-1;
		MicroProfileDrawBox(fXStart, nBaseY + fScale * nBarHeight, fXEnd, nBaseY+MICROPROFILE_FRAME_HISTORY_HEIGHT, nColor, MicroProfileBoxTypeBar);
		if(pNext->nFrameStartCpu > nCpuStart)
		{
			fSelectionStart = fXStart;
		}
		if(pCurrent->nFrameStartCpu < nCpuEnd && fSelectionEnd == 0.f)
		{
			fSelectionEnd = fXEnd;
		}
		nLastIndex = nIndex;
	}
	MicroProfileDrawBox(fSelectionStart, nBaseY, fSelectionEnd, nBaseY+MICROPROFILE_FRAME_HISTORY_HEIGHT, MICROPROFILE_FRAME_HISTORY_COLOR_HIGHTLIGHT, MicroProfileBoxTypeFlat);
}
void MicroProfileDrawDetailedView(uint32_t nWidth, uint32_t nHeight)
{
	MICROPROFILE_SCOPE(g_MicroProfileDetailed);
	uint32_t nBaseY = S.nBarHeight + 1;

	int nSelectedFrame = -1;
	if(S.nMouseY > nBaseY && S.nMouseY <= nBaseY + MICROPROFILE_FRAME_HISTORY_HEIGHT && S.nActiveMenu == -1)
	{

		nSelectedFrame = ((MICROPROFILE_NUM_FRAMES) * (S.nWidth-S.nMouseX) / S.nWidth);
		nSelectedFrame = (S.nFrameCurrent + MICROPROFILE_MAX_FRAME_HISTORY - nSelectedFrame) % MICROPROFILE_MAX_FRAME_HISTORY;
		S.nHoverFrame = nSelectedFrame;
		if(S.nMouseRight)
		{
			int64_t nRangeBegin = S.Frames[nSelectedFrame].nFrameStartCpu;
			int64_t nRangeEnd = S.Frames[(nSelectedFrame+1)%MICROPROFILE_MAX_FRAME_HISTORY].nFrameStartCpu;
			MicroProfileZoomTo(nRangeBegin, nRangeEnd);
		}
		if(S.nMouseDownLeft)
		{
			uint64_t nFrac = (1024 * (MICROPROFILE_NUM_FRAMES) * (S.nMouseX) / S.nWidth) % 1024;
			int64_t nRangeBegin = S.Frames[nSelectedFrame].nFrameStartCpu;
			int64_t nRangeEnd = S.Frames[(nSelectedFrame+1)%MICROPROFILE_MAX_FRAME_HISTORY].nFrameStartCpu;
			MicroProfileCenter(nRangeBegin + (nRangeEnd-nRangeBegin) * nFrac / 1024);
		}
	}
	else
	{
		S.nHoverFrame = -1;
	}

	MicroProfileDrawDetailedBars(nWidth, nHeight, nBaseY + MICROPROFILE_FRAME_HISTORY_HEIGHT, nSelectedFrame);
	MicroProfileDrawDetailedFrameHistory(nWidth, nHeight, nBaseY, nSelectedFrame);
}

template<typename T>
void MicroProfileLoopActiveGroupsDraw(int32_t nX, int32_t nY, const char* pName, T CB)
{
	if(pName)
		MicroProfileDrawText(nX, nY, (uint32_t)-1, pName, strlen(pName));

	nY += S.nBarHeight + 2;
	uint64_t nGroup = S.nActiveGroup = S.nMenuAllGroups ? S.nGroupMask : S.nMenuActiveGroup;
	uint32_t nCount = 0;
	for(uint32_t j = 0; j < MICROPROFILE_MAX_GROUPS; ++j)
	{
		uint64_t nMask = 1ll << j;
		if(nMask & nGroup)
		{
			nY += S.nBarHeight + 1;
			for(uint32_t i = 0; i < S.nTotalTimers;++i)
			{
				uint64_t nTokenMask = MicroProfileGetGroupMask(S.TimerInfo[i].nToken);
				if(nTokenMask & nMask)
				{
					if(nY >= 0)
						CB(i, nCount, nMask, nX, nY);
					
					nCount += 2;
					nY += S.nBarHeight + 1;

					if(nY > (int)S.nHeight)
						return;
				}
			}
			
		}
	}
}


void MicroProfileCalcTimers(float* pTimers, float* pAverage, float* pMax, float* pCallAverage, float* pExclusive, float* pAverageExclusive, float* pMaxExclusive, uint64_t nGroup, uint32_t nSize)
{
	uint32_t nCount = 0;
	uint64_t nMask = 1;

	for(uint32_t j = 0; j < MICROPROFILE_MAX_GROUPS; ++j)
	{
		if(nMask & nGroup)
		{
			const float fToMs = MicroProfileTickToMsMultiplier(S.GroupInfo[j].Type == MicroProfileTokenTypeGpu ? MicroProfileTicksPerSecondGpu() : MicroProfileTicksPerSecondCpu());
			for(uint32_t i = 0; i < S.nTotalTimers;++i)
			{
				uint64_t nTokenMask = MicroProfileGetGroupMask(S.TimerInfo[i].nToken);
				if(nTokenMask & nMask)
				{
					{
						uint32_t nTimer = i;
						uint32_t nIdx = nCount;
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
					nCount += 2;
				}
			}
		}
		nMask <<= 1ll;
	}
}

#define SBUF_MAX 32

uint32_t MicroProfileDrawBarArray(int32_t nX, int32_t nY, float* pTimers, const char* pName, uint32_t nTotalHeight)
{
	const uint32_t nHeight = S.nBarHeight;
	const uint32_t nWidth = S.nBarWidth;
	const uint32_t nTextWidth = 6 * (1+MICROPROFILE_TEXT_WIDTH);
	const float fWidth = (float)S.nBarWidth;

	MicroProfileDrawLineVertical(nX-5, nY, nTotalHeight, S.nOpacityBackground|g_nMicroProfileBackColors[0]|g_nMicroProfileBackColors[1]);

	MicroProfileLoopActiveGroupsDraw(nX, nY, pName, 
		[=](uint32_t nTimer, uint32_t nIdx, uint64_t nGroupMask, uint32_t nX, uint32_t nY){
			char sBuffer[SBUF_MAX];
			int nLen = snprintf(sBuffer, SBUF_MAX-1, "%5.2f", pTimers[nIdx]);
			MicroProfileDrawBox(nX + nTextWidth, nY, nX + nTextWidth + fWidth * pTimers[nIdx+1], nY + nHeight, S.nOpacityForeground|S.TimerInfo[nTimer].nColor, MicroProfileBoxTypeBar);
			MicroProfileDrawText(nX, nY, (uint32_t)-1, sBuffer, nLen);		
		});
	return nWidth + 5 + nTextWidth;

}

uint32_t MicroProfileDrawBarCallCount(int32_t nX, int32_t nY, const char* pName)
{
	MicroProfileLoopActiveGroupsDraw(nX, nY, pName, 
		[](uint32_t nTimer, uint32_t nIdx, uint64_t nGroupMask, uint32_t nX, uint32_t nY){
			char sBuffer[SBUF_MAX];
			int nLen = snprintf(sBuffer, SBUF_MAX-1, "%5d", S.Frame[nTimer].nCount);//fix
			MicroProfileDrawText(nX, nY, (uint32_t)-1, sBuffer, nLen);
		});
	uint32_t nTextWidth = 6 * MICROPROFILE_TEXT_WIDTH;
	return 5 + nTextWidth;
}

uint32_t MicroProfileDrawBarMetaCount(int32_t nX, int32_t nY, uint64_t* pCounters, const char* pName, uint32_t nTotalHeight)
{
	MicroProfileDrawLineVertical(nX-5, nY, nTotalHeight, S.nOpacityBackground|g_nMicroProfileBackColors[0]|g_nMicroProfileBackColors[1]);
	uint32_t nTextWidth = (1+MICROPROFILE_TEXT_WIDTH) * MicroProfileMax<uint32_t>(6, strlen(pName));


	MicroProfileLoopActiveGroupsDraw(nX, nY, pName, 
		[=](uint32_t nTimer, uint32_t nIdx, uint64_t nGroupMask, uint32_t nX, uint32_t nY){
			char sBuffer[SBUF_MAX];
			int nLen = snprintf(sBuffer, SBUF_MAX-1, "%5llu", pCounters[nTimer]);
			MicroProfileDrawText(nX + nTextWidth - nLen * (MICROPROFILE_TEXT_WIDTH+1), nY, (uint32_t)-1, sBuffer, nLen);
		});
	return 5 + nTextWidth;
}


uint32_t MicroProfileDrawBarLegend(int32_t nX, int32_t nY, uint32_t nTotalHeight)
{
	MicroProfileDrawLineVertical(nX-5, nY, nTotalHeight, S.nOpacityBackground | g_nMicroProfileBackColors[0]|g_nMicroProfileBackColors[1]);
	MicroProfileLoopActiveGroupsDraw(nX, nY, 0, 
		[](uint32_t nTimer, uint32_t nIdx, uint64_t nGroupMask, uint32_t nX, uint32_t nY){
			MicroProfileDrawText(nX, nY, S.TimerInfo[nTimer].nColor, S.TimerInfo[nTimer].pName, strlen(S.TimerInfo[nTimer].pName));
			if(S.nMouseY >= nY && S.nMouseY < nY + MICROPROFILE_TEXT_HEIGHT+1  && S.nMouseX < nX + 20 * (MICROPROFILE_TEXT_WIDTH+1))
			{
				S.nHoverToken = nTimer;
				S.nHoverTime = 0;
			}
		});
	return nX;
}

bool MicroProfileDrawGraph(uint32_t nScreenWidth, uint32_t nScreenHeight)
{
	MICROPROFILE_SCOPE(g_MicroProfileDrawGraph);
	bool bEnabled = false;
	for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
		if(S.Graph[i].nToken != MICROPROFILE_INVALID_TOKEN)
			bEnabled = true;
	if(!bEnabled)
		return false;
	
	uint32_t nX = nScreenWidth - MICROPROFILE_GRAPH_WIDTH;
	uint32_t nY = nScreenHeight - MICROPROFILE_GRAPH_HEIGHT;
	MicroProfileDrawBox(nX, nY, nX + MICROPROFILE_GRAPH_WIDTH, nY + MICROPROFILE_GRAPH_HEIGHT, S.nOpacityBackground | g_nMicroProfileBackColors[0]|g_nMicroProfileBackColors[1]);
	bool bMouseOver = S.nMouseX >= nX && S.nMouseY >= nY;
	float fMouseXPrc =(float(S.nMouseX - nX)) / MICROPROFILE_GRAPH_WIDTH;
	if(bMouseOver)
	{
		float fXAvg = fMouseXPrc * MICROPROFILE_GRAPH_WIDTH + nX;
		MicroProfileDrawLineVertical(fXAvg, nY, nY + MICROPROFILE_GRAPH_HEIGHT, (uint32_t)-1);
	}

	
	float fY = (float)nScreenHeight;
	float fDX = MICROPROFILE_GRAPH_WIDTH * 1.f / MICROPROFILE_GRAPH_HISTORY;  
	float fDY = MICROPROFILE_GRAPH_HEIGHT;
	uint32_t nPut = S.nGraphPut;
	float* pGraphData = (float*)alloca(sizeof(float)* MICROPROFILE_GRAPH_HISTORY*2);
	for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
	{
		if(S.Graph[i].nToken != MICROPROFILE_INVALID_TOKEN)
		{
			uint32_t nGroupId = MicroProfileGetGroupIndex(S.Graph[i].nToken);
			bool bGpu = S.GroupInfo[nGroupId].Type == MicroProfileTokenTypeGpu;
			float fToMs = MicroProfileTickToMsMultiplier(bGpu ? MicroProfileTicksPerSecondGpu() : MicroProfileTicksPerSecondCpu());
			float fToPrc = fToMs * S.fRcpReferenceTime * 3 / 4;

			float fX = (float)nX;
			for(uint32_t j = 0; j < MICROPROFILE_GRAPH_HISTORY; ++j)
			{
				float fWeigth = MicroProfileMin(fToPrc * (S.Graph[i].nHistory[(j+nPut)%MICROPROFILE_GRAPH_HISTORY]), 1.f);
				pGraphData[(j*2)] = fX;
				pGraphData[(j*2)+1] = fY - fDY * fWeigth;
				fX += fDX;
			}
			MicroProfileDrawLine2D(MICROPROFILE_GRAPH_HISTORY, pGraphData, S.TimerInfo[MicroProfileGetTimerIndex(S.Graph[i].nToken)].nColor);
		}
	}
	{
		float fY1 = 0.25f * MICROPROFILE_GRAPH_HEIGHT + nY;
		float fY2 = 0.50f * MICROPROFILE_GRAPH_HEIGHT + nY;
		float fY3 = 0.75f * MICROPROFILE_GRAPH_HEIGHT + nY;
		MicroProfileDrawLineHorizontal(nX, nX + MICROPROFILE_GRAPH_WIDTH, fY1, 0xffdd4444);
		MicroProfileDrawLineHorizontal(nX, nX + MICROPROFILE_GRAPH_WIDTH, fY2, 0xff000000| g_nMicroProfileBackColors[0]);
		MicroProfileDrawLineHorizontal(nX, nX + MICROPROFILE_GRAPH_WIDTH, fY3, 0xff000000|g_nMicroProfileBackColors[0]);

		char buf[32];
		int nLen = snprintf(buf, sizeof(buf)-1, "%5.2fms", S.fReferenceTime);
		MicroProfileDrawText(nX+1, fY1 - (2+MICROPROFILE_TEXT_HEIGHT), (uint32_t)-1, buf, nLen);
	}



	if(bMouseOver)
	{
		uint32_t pColors[MICROPROFILE_MAX_GRAPHS];
		MicroProfileStringArray Strings;
		MicroProfileStringArrayClear(&Strings);
		uint32_t nTextCount = 0;
		uint32_t nGraphIndex = (S.nGraphPut + MICROPROFILE_GRAPH_HISTORY - int(MICROPROFILE_GRAPH_HISTORY*(1.f - fMouseXPrc))) % MICROPROFILE_GRAPH_HISTORY;

		uint32_t nX = S.nMouseX;
		uint32_t nY = S.nMouseY + 20;

		for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
		{
			if(S.Graph[i].nToken != MICROPROFILE_INVALID_TOKEN)
			{
				uint32_t nGroupId = MicroProfileGetGroupIndex(S.Graph[i].nToken);
				bool bGpu = S.GroupInfo[nGroupId].Type == MicroProfileTokenTypeGpu;
				float fToMs = MicroProfileTickToMsMultiplier(bGpu ? MicroProfileTicksPerSecondGpu() : MicroProfileTicksPerSecondCpu());
				uint32_t nIndex = MicroProfileGetTimerIndex(S.Graph[i].nToken);
				uint32_t nColor = S.TimerInfo[nIndex].nColor;
				const char* pName = S.TimerInfo[nIndex].pName;
				pColors[nTextCount++] = nColor;
				MicroProfileStringArrayAddLiteral(&Strings, pName);
				MicroProfileStringArrayFormat(&Strings, "%5.2fms", fToMs * (S.Graph[i].nHistory[nGraphIndex]));
			}
		}
		if(nTextCount)
		{
			MicroProfileDrawFloatWindow(nX, nY, Strings.ppStrings, Strings.nNumStrings, 0, pColors);
		}

		if(S.nMouseRight)
		{
			for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
			{
				S.Graph[i].nToken = MICROPROFILE_INVALID_TOKEN;
			}
		}
	}

	return bMouseOver;
}

void MicroProfileDumpTimers()
{
	uint64_t nActiveGroup = S.nGroupMask;

	uint32_t nNumTimers = S.nTotalTimers;
	uint32_t nBlockSize = 2 * nNumTimers;
	float* pTimers = (float*)alloca(nBlockSize * 7 * sizeof(float));
	float* pAverage = pTimers + nBlockSize;
	float* pMax = pTimers + 2 * nBlockSize;
	float* pCallAverage = pTimers + 3 * nBlockSize;
	float* pTimersExclusive = pTimers + 4 * nBlockSize;
	float* pAverageExclusive = pTimers + 5 * nBlockSize;
	float* pMaxExclusive = pTimers + 6 * nBlockSize;
	MicroProfileCalcTimers(pTimers, pAverage, pMax, pCallAverage, pTimersExclusive, pAverageExclusive, pMaxExclusive, nActiveGroup, nNumTimers);

	MICROPROFILE_PRINTF("%11s, ", "Time");
	MICROPROFILE_PRINTF("%11s, ", "Average");
	MICROPROFILE_PRINTF("%11s, ", "Max");
	MICROPROFILE_PRINTF("%11s, ", "Call Avg");
	MICROPROFILE_PRINTF("%9s, ", "Count");
	MICROPROFILE_PRINTF("%11s, ", "Excl");
	MICROPROFILE_PRINTF("%11s, ", "Avg Excl");
	MICROPROFILE_PRINTF("%11s, \n", "Max Excl");

	for(uint32_t j = 0; j < MICROPROFILE_MAX_GROUPS; ++j)
	{
		uint64_t nMask = 1ll << j;
		if(nMask & nActiveGroup)
		{
			MICROPROFILE_PRINTF("%s\n", S.GroupInfo[j].pName);
			for(uint32_t i = 0; i < S.nTotalTimers;++i)
			{
				uint64_t nTokenMask = MicroProfileGetGroupMask(S.TimerInfo[i].nToken);
				if(nTokenMask & nMask)
				{
					uint32_t nIdx = i * 2;
					MICROPROFILE_PRINTF("%9.2fms, ", pTimers[nIdx]);
					MICROPROFILE_PRINTF("%9.2fms, ", pAverage[nIdx]);
					MICROPROFILE_PRINTF("%9.2fms, ", pMax[nIdx]);
					MICROPROFILE_PRINTF("%9.2fms, ", pCallAverage[nIdx]);
					MICROPROFILE_PRINTF("%9d, ", S.Frame[i].nCount);
					MICROPROFILE_PRINTF("%9.2fms, ", pTimersExclusive[nIdx]);
					MICROPROFILE_PRINTF("%9.2fms, ", pAverageExclusive[nIdx]);
					MICROPROFILE_PRINTF("%9.2fms, ", pMaxExclusive[nIdx]);
					MICROPROFILE_PRINTF("%s\n", S.TimerInfo[i].pName);
				}
			}
		}
	}
}

void MicroProfileDrawBarView(uint32_t nScreenWidth, uint32_t nScreenHeight)
{
	uint64_t nActiveGroup = S.nMenuAllGroups ? S.nGroupMask : S.nMenuActiveGroup;
	if(!nActiveGroup)
		return;
	MICROPROFILE_SCOPE(g_MicroProfileDrawBarView);

	const uint32_t nHeight = S.nBarHeight;
	int nColorIndex = 0;
	uint32_t nX = 0;
	uint32_t nY = nHeight + 1 - S.nOffsetY;	
	uint32_t nNumTimers = 0;
	uint32_t nNumGroups = 0;
	uint32_t nMaxTimerNameLen = 1;
	for(uint32_t j = 0; j < MICROPROFILE_MAX_GROUPS; ++j)
	{
		if(nActiveGroup & (1ll << j))
		{
			nNumTimers += S.GroupInfo[j].nNumTimers;
			nNumGroups += 1;
			nMaxTimerNameLen = MicroProfileMax(nMaxTimerNameLen, S.GroupInfo[j].nMaxTimerNameLen);
		}
	}
	uint32_t nBlockSize = 2 * nNumTimers;
	float* pTimers = (float*)alloca(nBlockSize * 7 * sizeof(float));
	float* pAverage = pTimers + nBlockSize;
	float* pMax = pTimers + 2 * nBlockSize;
	float* pCallAverage = pTimers + 3 * nBlockSize;
	float* pTimersExclusive = pTimers + 4 * nBlockSize;
	float* pAverageExclusive = pTimers + 5 * nBlockSize;
	float* pMaxExclusive = pTimers + 6 * nBlockSize;
	MicroProfileCalcTimers(pTimers, pAverage, pMax, pCallAverage, pTimersExclusive, pAverageExclusive, pMaxExclusive, nActiveGroup, nNumTimers);
	{
		uint32_t nWidth = 0;
		for(uint32_t i = 1; i ; i <<= 1)
		{
			if(S.nBars & i)
			{
				nWidth += S.nBarWidth + 5 + 6 * (1+MICROPROFILE_TEXT_WIDTH);
				if(i & MP_DRAW_CALL_COUNT)
					nWidth += 5 + 6 * MICROPROFILE_TEXT_WIDTH;
			}
		}
		nWidth += (1+nMaxTimerNameLen) * (MICROPROFILE_TEXT_WIDTH+1);
		for(uint32_t i = 0; i < nNumTimers+nNumGroups+1; ++i)
		{
			int nY0 = nY + i * (nHeight + 1);
			MicroProfileDrawBox(nX, nY0, nWidth, nY0 + (nHeight+1)+1, S.nOpacityBackground | g_nMicroProfileBackColors[nColorIndex++ & 1]);
		}
	}
	int nTotalHeight = (nNumTimers+nNumGroups+2) * (nHeight+1);
	uint32_t nLegendOffset = 1;
	for(uint32_t j = 0; j < MICROPROFILE_MAX_GROUPS; ++j)
	{
		if(nActiveGroup & (1ll << j))
		{
			MicroProfileDrawText(nX, nY + (1+nHeight) * nLegendOffset, (uint32_t)-1, S.GroupInfo[j].pName, S.GroupInfo[j].nNameLen);
			nLegendOffset += S.GroupInfo[j].nNumTimers+1;
		}
	}
	if(S.nBars & MP_DRAW_TIMERS)		
		nX += MicroProfileDrawBarArray(nX, nY, pTimers, "Time", nTotalHeight) + 1;
	if(S.nBars & MP_DRAW_AVERAGE)		
		nX += MicroProfileDrawBarArray(nX, nY, pAverage, "Average", nTotalHeight) + 1;
	if(S.nBars & MP_DRAW_MAX)		
		nX += MicroProfileDrawBarArray(nX, nY, pMax, "Max Time", nTotalHeight) + 1;
	if(S.nBars & MP_DRAW_CALL_COUNT)		
	{
		nX += MicroProfileDrawBarArray(nX, nY, pCallAverage, "Call Average", nTotalHeight) + 1;
		nX += MicroProfileDrawBarCallCount(nX, nY, "Count") + 1; 
	}
	if(S.nBars & MP_DRAW_TIMERS_EXCLUSIVE)		
		nX += MicroProfileDrawBarArray(nX, nY, pTimersExclusive, "Exclusive Time", nTotalHeight) + 1;
	if(S.nBars & MP_DRAW_AVERAGE_EXCLUSIVE)		
		nX += MicroProfileDrawBarArray(nX, nY, pAverageExclusive, "Exclusive Average", nTotalHeight) + 1;
	if(S.nBars & MP_DRAW_MAX_EXCLUSIVE)		
		nX += MicroProfileDrawBarArray(nX, nY, pMaxExclusive, "Exclusive Max Time", nTotalHeight) + 1;

	for(int i = 0; i < MICROPROFILE_META_MAX; ++i)
	{
		if(0 != (S.nBars & (MP_DRAW_META_FIRST<<i)) && nullptr != S.MetaCounters[i].pName)
		{
			nX += MicroProfileDrawBarMetaCount(nX, nY, &S.MetaCounters[i].nCounters[0], S.MetaCounters[i].pName, nTotalHeight) + 1; 
		}
	}
	nX += MicroProfileDrawBarLegend(nX, nY, nTotalHeight) + 1;
}

void MicroProfileDrawMenu(uint32_t nWidth, uint32_t nHeight)
{
	uint32_t nX = 0;
	uint32_t nY = 0;
	bool bMouseOver = S.nMouseY < MICROPROFILE_TEXT_HEIGHT + 1;
#define SBUF_SIZE 256
	char buffer[256];
	MicroProfileDrawBox(nX, nY, nX + nWidth, nY + (S.nBarHeight+1)+1, 0xff000000|g_nMicroProfileBackColors[1]);

#define MICROPROFILE_MENU_MAX 16
	const char* pMenuText[MICROPROFILE_MENU_MAX] = {0};
	uint32_t 	nMenuX[MICROPROFILE_MENU_MAX] = {0};
	uint32_t nNumMenuItems = 0;

	int nLen = snprintf(buffer, 127, "MicroProfile");
	MicroProfileDrawText(nX, nY, (uint32_t)-1, buffer, nLen);
	nX += (sizeof("MicroProfile")+2) * (MICROPROFILE_TEXT_WIDTH+1);
	pMenuText[nNumMenuItems++] = "Mode";
	pMenuText[nNumMenuItems++] = "Groups";
	char AggregateText[64];
	snprintf(AggregateText, sizeof(AggregateText)-1, "Aggregate[%d]", S.nAggregateFlip ? S.nAggregateFlip : S.nAggregateFlipCount);
	pMenuText[nNumMenuItems++] = &AggregateText[0];
	pMenuText[nNumMenuItems++] = "Timers";
	pMenuText[nNumMenuItems++] = "Options";
	pMenuText[nNumMenuItems++] = "Preset";
	const int nPauseIndex = nNumMenuItems;
	pMenuText[nNumMenuItems++] = S.nRunning ? "Pause" : "Unpause";
	pMenuText[nNumMenuItems++] = "Help";

	if(S.nOverflow)
	{
		pMenuText[nNumMenuItems++] = "!BUFFERSFULL!";
	}


	struct SOptionDesc
	{
		SOptionDesc(){}
		SOptionDesc(uint8_t nSubType, uint8_t nIndex, const char* fmt, ...):nSubType(nSubType), nIndex(nIndex)
		{
			va_list args;
			va_start (args, fmt);
			vsprintf(Text, fmt, args);
			va_end(args);
		}
		char Text[32];
		uint8_t nSubType;
		uint8_t nIndex;
		bool bSelected;
	};
	static const int nNumReferencePresets = sizeof(g_MicroProfileReferenceTimePresets)/sizeof(g_MicroProfileReferenceTimePresets[0]);
	static const int nNumOpacityPresets = sizeof(g_MicroProfileOpacityPresets)/sizeof(g_MicroProfileOpacityPresets[0]);

#if MICROPROFILE_CONTEXT_SWITCH_TRACE
	static const int nOptionSize = nNumReferencePresets + nNumOpacityPresets * 2 + 7;
#else
	static const int nOptionSize = nNumReferencePresets + nNumOpacityPresets * 2 + 3;
#endif

	static SOptionDesc Options[nOptionSize];
	static bool bOptionInit = false;
	if(!bOptionInit)
	{
		bOptionInit = true;
		int nIndex = 0;
		Options[nIndex++] = SOptionDesc(0xff, 0, "%s", "Reference");
		for(int i = 0; i < nNumReferencePresets; ++i)
		{
			Options[nIndex++] = SOptionDesc(0, i, "  %6.2fms", g_MicroProfileReferenceTimePresets[i]);
		}
		Options[nIndex++] = SOptionDesc(0xff, 0, "%s", "BG Opacity");		
		for(int i = 0; i < nNumOpacityPresets; ++i)
		{
			Options[nIndex++] = SOptionDesc(1, i, "  %7d%%", (i+1)*25);
		}
		Options[nIndex++] = SOptionDesc(0xff, 0, "%s", "FG Opacity");		
		for(int i = 0; i < nNumOpacityPresets; ++i)
		{
			Options[nIndex++] = SOptionDesc(2, i, "  %7d%%", (i+1)*25);
		}
#if MICROPROFILE_CONTEXT_SWITCH_TRACE
		Options[nIndex++] = SOptionDesc(0xff, 0, "%s", "CSwitch Trace");		
		Options[nIndex++] = SOptionDesc(3, 0, "%s", "  Enable");
		Options[nIndex++] = SOptionDesc(3, 1, "%s", "  All Threads");
		Options[nIndex++] = SOptionDesc(3, 2, "%s", "  No Bars");
#endif


		MP_ASSERT(nIndex == nOptionSize);
	}



	typedef std::function<const char* (int, bool&)> SubmenuCallback; 
	typedef std::function<void(int)> ClickCallback;
	SubmenuCallback GroupCallback[] = 
	{	[] (int index, bool& bSelected) -> const char*{
			switch(index)
			{
				case 0: 
					bSelected = S.nDisplay == MP_DRAW_DETAILED;
					return "Detailed";
				case 1:
					bSelected = S.nDisplay == MP_DRAW_BARS; 
					return "Timers";
				case 2:
					bSelected = S.nDisplay == MP_DRAW_HIDDEN; 
					return "Hidden";
				case 3:
					bSelected = false; 
					return "Off";

				default: return 0;
			}
		},
		[] (int index, bool& bSelected) -> const char*{
			if(index == 0)
			{
				bSelected = S.nMenuAllGroups != 0;
				return "ALL";
			}
			else
			{
				index = index-1;
				bSelected = 0 != (S.nMenuActiveGroup & (1ll << index));
				if(index < MICROPROFILE_MAX_GROUPS && S.GroupInfo[index].pName)
					return S.GroupInfo[index].pName;
				else
					return 0;
			}
		},
		[] (int index, bool& bSelected) -> const char*{
			if(index < sizeof(g_MicroProfileAggregatePresets)/sizeof(g_MicroProfileAggregatePresets[0]))
			{
				int val = g_MicroProfileAggregatePresets[index];
				bSelected = (int)S.nAggregateFlip == val;
				if(0 == val)
					return "Infinite";
				else
				{
					static char buf[128];
					snprintf(buf, sizeof(buf)-1, "%7d", val);
					return buf;
				}
			}
			return 0;
		},
		[] (int index, bool& bSelected) -> const char*{
			bSelected = 0 != (S.nBars & (1 << index));
			switch(index)
			{
				case 0: return "Time";				
				case 1: return "Average";				
				case 2: return "Max";
				case 3: return "Call Count";
				case 4: return "Exclusive Timers";
				case 5: return "Exclusive Average";
				case 6: return "Exclusive Max";
			}
			int nMetaIndex = index - 7;
			if(nMetaIndex < MICROPROFILE_META_MAX)
			{
				return S.MetaCounters[nMetaIndex].pName;
			}
			return 0;
		},
		[] (int index, bool& bSelected) -> const char*{
			if(index >= nOptionSize) return 0;
			switch(Options[index].nSubType)
			{
			case 0:
				bSelected = S.fReferenceTime == g_MicroProfileReferenceTimePresets[Options[index].nIndex];
				break;
			case 1:
				bSelected = S.nOpacityBackground>>24 == g_MicroProfileOpacityPresets[Options[index].nIndex];
				break;
			case 2:
				bSelected = S.nOpacityForeground>>24 == g_MicroProfileOpacityPresets[Options[index].nIndex];				
				break;
#if MICROPROFILE_CONTEXT_SWITCH_TRACE
			case 3:
				{
					switch(Options[index].nIndex)
					{
					case 0:
						bSelected = S.bContextSwitchRunning;
						break;
					case 1:
						bSelected = S.bContextSwitchAllThreads;
						break;
					case 2: 
						bSelected = S.bContextSwitchNoBars;
						break;
					}
				}
				break;
#endif
			}
			return Options[index].Text;
		},

		[] (int index, bool& bSelected) -> const char*{
			static char buf[128];
			bSelected = false;
			int nNumPresets = sizeof(g_MicroProfilePresetNames) / sizeof(g_MicroProfilePresetNames[0]);
			int nIndexSave = index - nNumPresets - 1;
			if(index == nNumPresets)
				return "--";
			else if(nIndexSave >=0 && nIndexSave <nNumPresets)
			{
				snprintf(buf, sizeof(buf)-1, "Save '%s'", g_MicroProfilePresetNames[nIndexSave]);
				return buf;
			}
			else if(index < nNumPresets)
			{
				snprintf(buf, sizeof(buf)-1, "Load '%s'", g_MicroProfilePresetNames[index]);
				return buf;
			}
			else
			{
				return 0;
			}
		},

		[] (int index, bool& bSelected) -> const char*{
			return 0;
		},
		[] (int index, bool& bSelected) -> const char*{
			return 0;
		},
		[] (int index, bool& bSelected) -> const char*{
			return 0;
		},


	};
	ClickCallback CBClick[] = 
	{
		[](int nIndex)
		{
			switch(nIndex)
			{
				case 0:
					S.nDisplay = MP_DRAW_DETAILED;
					break;
				case 1:
					S.nDisplay = MP_DRAW_BARS;
					break;
				case 2:
					S.nDisplay = MP_DRAW_HIDDEN;
					break;
				case 3:
					S.nDisplay = 0;
					break;
			}
		},
		[](int nIndex)
		{
			if(nIndex == 0)
				S.nMenuAllGroups = 1-S.nMenuAllGroups;
			else
				S.nMenuActiveGroup ^= (1ll << (nIndex-1));
		},
		[](int nIndex)
		{
			S.nAggregateFlip = g_MicroProfileAggregatePresets[nIndex];
			if(0 == S.nAggregateFlip)
			{
				memset(S.AggregateTimers, 0, sizeof(S.AggregateTimers));
				memset(S.MaxTimers, 0, sizeof(S.MaxTimers));
				memset(S.AggregateTimersExclusive, 0, sizeof(S.AggregateTimersExclusive));
				memset(S.MaxTimersExclusive, 0, sizeof(S.MaxTimersExclusive));
				S.nFlipAggregate = 0;
				S.nFlipMax = 0;
				S.nAggregateFlipCount = 0;
			}
		},
		[](int nIndex)
		{
			S.nBars ^= (1 << nIndex);
		},
		[](int nIndex)
		{
			switch(Options[nIndex].nSubType)
			{
			case 0:
				S.fReferenceTime = g_MicroProfileReferenceTimePresets[Options[nIndex].nIndex];
				S.fRcpReferenceTime = 1.f / S.fReferenceTime;
				break;
			case 1:
				S.nOpacityBackground = g_MicroProfileOpacityPresets[Options[nIndex].nIndex]<<24;
				break;
			case 2:
				S.nOpacityForeground = g_MicroProfileOpacityPresets[Options[nIndex].nIndex]<<24;
				break;
#if MICROPROFILE_CONTEXT_SWITCH_TRACE
			case 3:
				{
					switch(Options[nIndex].nIndex)
					{
					case 0:
						if(S.bContextSwitchRunning)
						{
							MicroProfileStopContextSwitchTrace();
						}
						else
						{
							MicroProfileStartContextSwitchTrace();
						}
						break;
					case 1:
						S.bContextSwitchAllThreads = !S.bContextSwitchAllThreads;
						break;
					case 2:
						S.bContextSwitchNoBars= !S.bContextSwitchNoBars;
						break;

					}
				}
				break;
#endif
			}
		},
		[](int nIndex)
		{
			int nNumPresets = sizeof(g_MicroProfilePresetNames) / sizeof(g_MicroProfilePresetNames[0]);
			int nIndexSave = nIndex - nNumPresets - 1;
			if(nIndexSave >= 0 && nIndexSave < nNumPresets)
			{
				MicroProfileSavePreset(g_MicroProfilePresetNames[nIndexSave]);
			}
			else if(nIndex >= 0 && nIndex < nNumPresets)
			{
				MicroProfileLoadPreset(g_MicroProfilePresetNames[nIndex]);
			}
		},
		[](int nIndex)
		{
		},
		[](int nIndex)
		{
		},
		[](int nIndex)
		{
		},
	};

	uint32_t nSelectMenu = (uint32_t)-1;
	for(uint32_t i = 0; i < nNumMenuItems; ++i)
	{
		nMenuX[i] = nX;
		uint32_t nLen = (uint32_t)strlen(pMenuText[i]);
		uint32_t nEnd = nX + nLen * (MICROPROFILE_TEXT_WIDTH+1);
		if(S.nMouseY <= MICROPROFILE_TEXT_HEIGHT && S.nMouseX <= nEnd && S.nMouseX >= nX)
		{
			MicroProfileDrawBox(nX-1, nY, nX + nLen * (MICROPROFILE_TEXT_WIDTH+1), nY +(S.nBarHeight+1)+1, 0xff888888);
			nSelectMenu = i;
			if((S.nMouseLeft || S.nMouseRight) && i == (int)nPauseIndex)
			{
				S.nRunning = !S.nRunning;
			}
		}
		MicroProfileDrawText(nX, nY, (uint32_t)-1, pMenuText[i], strlen(pMenuText[i]));
		nX += (nLen+1) * (MICROPROFILE_TEXT_WIDTH+1);
	}
	uint32_t nMenu = nSelectMenu != (uint32_t)-1 ? nSelectMenu : S.nActiveMenu;
	S.nActiveMenu = nMenu;
	if((uint32_t)-1 != nMenu)
	{
		nX = nMenuX[nMenu];
		nY += MICROPROFILE_TEXT_HEIGHT+1;
		SubmenuCallback CB = GroupCallback[nMenu];
		int nNumLines = 0;
		bool bSelected = false;
		const char* pString = CB(nNumLines, bSelected);
		uint32_t nWidth = 0, nHeight = 0;
		while(pString)
		{
			nWidth = MicroProfileMax<int>(nWidth, (int)strlen(pString));
			nNumLines++;
			pString = CB(nNumLines, bSelected);
		}
		nWidth = (2+nWidth) * (MICROPROFILE_TEXT_WIDTH+1);
		nHeight = nNumLines * (MICROPROFILE_TEXT_HEIGHT+1);
		if(S.nMouseY <= nY + nHeight+0 && S.nMouseY >= nY-0 && S.nMouseX <= nX + nWidth + 0 && S.nMouseX >= nX - 0)
		{
			S.nActiveMenu = nMenu;
		}
		else if(nSelectMenu == (uint32_t)-1)
		{
			S.nActiveMenu = (uint32_t)-1;
		}
		MicroProfileDrawBox(nX, nY, nX + nWidth, nY + nHeight, 0xff000000|g_nMicroProfileBackColors[1]);
		for(int i = 0; i < nNumLines; ++i)
		{
			bool bSelected = false;
			const char* pString = CB(i, bSelected);
			if(S.nMouseY >= nY && S.nMouseY < nY + MICROPROFILE_TEXT_HEIGHT + 1)
			{
				bMouseOver = true;
				if(S.nMouseLeft || S.nMouseRight)
				{
					CBClick[nMenu](i);
				}
				MicroProfileDrawBox(nX, nY, nX + nWidth, nY + MICROPROFILE_TEXT_HEIGHT + 1, 0xff888888);
			}
			int nLen = snprintf(buffer, SBUF_SIZE-1, "%c %s", bSelected ? '*' : ' ' ,pString);
			MicroProfileDrawText(nX, nY, (uint32_t)-1, buffer, nLen);
			nY += MICROPROFILE_TEXT_HEIGHT+1;
		}
	}


	{
		static char FrameTimeMessage[64];
		float fToMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
		uint32_t nAggregateFrames = S.nAggregateFrames ? S.nAggregateFrames : 1;
		float fMs = fToMs * (S.nFlipTicks);
		float fAverageMs = fToMs * (S.nFlipAggregateDisplay / nAggregateFrames);
		float fMaxMs = fToMs * S.nFlipMaxDisplay;
		int nLen = snprintf(FrameTimeMessage, sizeof(FrameTimeMessage)-1, "Time[%6.2f] Avg[%6.2f] Max[%6.2f]", fMs, fAverageMs, fMaxMs);
		pMenuText[nNumMenuItems++] = &FrameTimeMessage[0];
		MicroProfileDrawText(nWidth - nLen * (MICROPROFILE_TEXT_WIDTH+1), 0, -1, FrameTimeMessage, nLen);
	}
}


void MicroProfileMoveGraph()
{
	int nZoom = S.nMouseWheelDelta;
	int nPanX = 0;
	int nPanY = 0;
	static int X = 0, Y = 0;
	if(S.nMouseDownLeft && !S.nModDown)
	{
		nPanX = S.nMouseX - X;
		nPanY = S.nMouseY - Y;
	}
	X = S.nMouseX;
	Y = S.nMouseY;

	if(nZoom)
	{
		float fOldRange = S.fDetailedRange;
		if(nZoom>0)
		{
			S.fDetailedRangeTarget = S.fDetailedRange *= S.nModDown ? 1.40 : 1.05f;
		}
		else
		{
			S.fDetailedRangeTarget = S.fDetailedRange /= S.nModDown ? 1.40 : 1.05f;
		}

		float fDiff = fOldRange - S.fDetailedRange;
		float fMousePrc = MicroProfileMax((float)S.nMouseX / S.nWidth ,0.f);
		S.fDetailedOffsetTarget = S.fDetailedOffset += fDiff * fMousePrc;

	}
	if(nPanX)
	{
		S.fDetailedOffsetTarget = S.fDetailedOffset += -nPanX * S.fDetailedRange / S.nWidth;
	}
	S.nOffsetY -= nPanY;
	if(S.nOffsetY<0)
		S.nOffsetY = 0;
}

bool MicroProfileIsDrawing()
{
	return S.nDisplay != 0;
}
void MicroProfileDraw(uint32_t nWidth, uint32_t nHeight)
{
	MICROPROFILE_SCOPE(g_MicroProfileDraw);

	if(S.nDisplay)
	{
		MicroProfileScopeLock L(MicroProfileMutex());
		S.nWidth = nWidth;
		S.nHeight = nHeight;
		S.nHoverToken = MICROPROFILE_INVALID_TOKEN;
		S.nHoverTime = 0;
		S.nHoverFrame = -1;
		if(S.nDisplay != MP_DRAW_DETAILED)
			S.nContextSwitchHoverThread = S.nContextSwitchHoverThreadAfter = S.nContextSwitchHoverThreadBefore = -1;
		MicroProfileMoveGraph();


		if(S.nDisplay == MP_DRAW_DETAILED)
		{
			MicroProfileDrawDetailedView(nWidth, nHeight);
		}
		else if(S.nDisplay == MP_DRAW_BARS && S.nBars)
		{
			MicroProfileDrawBarView(nWidth, nHeight);
		}
		
		MicroProfileDrawMenu(nWidth, nHeight);
		bool bMouseOverGraph = MicroProfileDrawGraph(nWidth, nHeight);
		bool bHidden = S.nDisplay == MP_DRAW_HIDDEN;
		if(!bHidden)
		{
			uint32_t nLockedToolTipX = 3;
			bool bDeleted = false;
			for(int i = 0; i < MICROPROFILE_TOOLTIP_MAX_LOCKED; ++i)
			{
				int nIndex = (S.LockedToolTipFront + i) % MICROPROFILE_TOOLTIP_MAX_LOCKED;
				if(S.LockedToolTips[nIndex].ppStrings[0])
				{
					uint32_t nToolTipWidth = 0, nToolTipHeight = 0;
					MicroProfileFloatWindowSize(S.LockedToolTips[nIndex].ppStrings, S.LockedToolTips[nIndex].nNumStrings, 0, nToolTipWidth, nToolTipHeight, 0);
					uint32_t nStartY = nHeight - nToolTipHeight - 2;
					if(!bDeleted && S.nMouseY > nStartY && S.nMouseX > nLockedToolTipX && S.nMouseX <= nLockedToolTipX + nToolTipWidth && (S.nMouseLeft || S.nMouseRight) )
					{
						bDeleted = true;
						int j = i;
						for(; j < MICROPROFILE_TOOLTIP_MAX_LOCKED-1; ++j)
						{
							int nIndex0 = (S.LockedToolTipFront + j) % MICROPROFILE_TOOLTIP_MAX_LOCKED;
							int nIndex1 = (S.LockedToolTipFront + j+1) % MICROPROFILE_TOOLTIP_MAX_LOCKED;
							MicroProfileStringArrayCopy(&S.LockedToolTips[nIndex0], &S.LockedToolTips[nIndex1]);
						}
						MicroProfileStringArrayClear(&S.LockedToolTips[(S.LockedToolTipFront + j) % MICROPROFILE_TOOLTIP_MAX_LOCKED]);
					}
					else
					{
						MicroProfileDrawFloatWindow(nLockedToolTipX, nHeight-nToolTipHeight-2, &S.LockedToolTips[nIndex].ppStrings[0], S.LockedToolTips[nIndex].nNumStrings, S.nLockedToolTipColor[nIndex]);
						nLockedToolTipX += nToolTipWidth + 4;
					}
				}
			}

			if(S.nActiveMenu == 7)
			{
				if(S.nDisplay & MP_DRAW_DETAILED)
				{
					MicroProfileStringArray DetailedHelp;
					MicroProfileStringArrayClear(&DetailedHelp);
					MicroProfileStringArrayFormat(&DetailedHelp, "%s", MICROPROFILE_HELP_LEFT);
					MicroProfileStringArrayAddLiteral(&DetailedHelp, "Toggle Graph");
					MicroProfileStringArrayFormat(&DetailedHelp, "%s", MICROPROFILE_HELP_ALT);
					MicroProfileStringArrayAddLiteral(&DetailedHelp, "Zoom");
					MicroProfileStringArrayFormat(&DetailedHelp, "%s + %s", MICROPROFILE_HELP_MOD, MICROPROFILE_HELP_LEFT);
					MicroProfileStringArrayAddLiteral(&DetailedHelp, "Lock Tooltip");
					MicroProfileStringArrayAddLiteral(&DetailedHelp, "Drag");
					MicroProfileStringArrayAddLiteral(&DetailedHelp, "Pan View");
					MicroProfileStringArrayAddLiteral(&DetailedHelp, "Mouse Wheel");
					MicroProfileStringArrayAddLiteral(&DetailedHelp, "Zoom");
					MicroProfileDrawFloatWindow(nWidth, MICROPROFILE_FRAME_HISTORY_HEIGHT+20, DetailedHelp.ppStrings, DetailedHelp.nNumStrings, 0xff777777);

					MicroProfileStringArray DetailedHistoryHelp;
					MicroProfileStringArrayClear(&DetailedHistoryHelp);
					MicroProfileStringArrayFormat(&DetailedHistoryHelp, "%s", MICROPROFILE_HELP_LEFT);
					MicroProfileStringArrayAddLiteral(&DetailedHistoryHelp, "Center View");
					MicroProfileStringArrayFormat(&DetailedHistoryHelp, "%s", MICROPROFILE_HELP_ALT);
					MicroProfileStringArrayAddLiteral(&DetailedHistoryHelp, "Zoom to frame");
					MicroProfileDrawFloatWindow(nWidth, 20, DetailedHistoryHelp.ppStrings, DetailedHistoryHelp.nNumStrings, 0xff777777);



				}
				else if(0 != (S.nDisplay & MP_DRAW_BARS) && S.nBars)
				{
					MicroProfileStringArray BarHelp;
					MicroProfileStringArrayClear(&BarHelp);
					MicroProfileStringArrayFormat(&BarHelp, "%s", MICROPROFILE_HELP_LEFT);
					MicroProfileStringArrayAddLiteral(&BarHelp, "Toggle Graph");
					MicroProfileStringArrayFormat(&BarHelp, "%s + %s", MICROPROFILE_HELP_MOD, MICROPROFILE_HELP_LEFT);
					MicroProfileStringArrayAddLiteral(&BarHelp, "Lock Tooltip");
					MicroProfileStringArrayAddLiteral(&BarHelp, "Drag");
					MicroProfileStringArrayAddLiteral(&BarHelp, "Pan View");
					MicroProfileDrawFloatWindow(nWidth, MICROPROFILE_FRAME_HISTORY_HEIGHT+20, BarHelp.ppStrings, BarHelp.nNumStrings, 0xff777777);

				}
				MicroProfileStringArray Debug;
				MicroProfileStringArrayClear(&Debug);
				MicroProfileStringArrayAddLiteral(&Debug, "Memory Usage");
				MicroProfileStringArrayFormat(&Debug, "%4.2fmb", S.nMemUsage / (1024.f * 1024.f));
				uint32_t nFrameNext = (S.nFrameCurrent+1) % MICROPROFILE_MAX_FRAME_HISTORY;
				MicroProfileFrameState* pFrameCurrent = &S.Frames[S.nFrameCurrent];
				MicroProfileFrameState* pFrameNext = &S.Frames[nFrameNext];


				MicroProfileStringArrayAddLiteral(&Debug, "");
				MicroProfileStringArrayAddLiteral(&Debug, "");
				MicroProfileStringArrayAddLiteral(&Debug, "Usage");
				MicroProfileStringArrayAddLiteral(&Debug, "markers [frames] ");

#if MICROPROFILE_CONTEXT_SWITCH_TRACE
				MicroProfileStringArrayAddLiteral(&Debug, "Context Switch");
				MicroProfileStringArrayFormat(&Debug, "%9d [%7d]", S.nContextSwitchUsage, MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE / S.nContextSwitchUsage );
#endif

				for(int i = 0; i < MICROPROFILE_MAX_GROUPS; ++i)
				{
					if(pFrameCurrent->nLogStart[i] && S.Pool[i])
					{
						uint32_t nEnd = pFrameNext->nLogStart[i];
						uint32_t nStart = pFrameCurrent->nLogStart[i];
						uint32_t nUsage = nStart < nEnd ? (nEnd - nStart) : (nEnd + MICROPROFILE_BUFFER_SIZE - nStart);
						uint32_t nFrameSupport = MICROPROFILE_BUFFER_SIZE / nUsage;
						MicroProfileStringArrayFormat(&Debug, "%s", &S.Pool[i]->ThreadName[0]);
						MicroProfileStringArrayFormat(&Debug, "%9d [%7d]", nUsage, nFrameSupport);
					}
				}

				MicroProfileDrawFloatWindow(0, nHeight-10, Debug.ppStrings, Debug.nNumStrings, 0xff777777);
			}



			if(S.nActiveMenu == -1 && !bMouseOverGraph)
			{
				if(S.nHoverToken != MICROPROFILE_INVALID_TOKEN)
				{
					MicroProfileDrawFloatTooltip(S.nMouseX, S.nMouseY, S.nHoverToken, S.nHoverTime);
				}
				else if(S.nContextSwitchHoverThreadAfter != -1 && S.nContextSwitchHoverThreadBefore != -1)
				{
					float fToMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
					MicroProfileStringArray ToolTip;
					MicroProfileStringArrayClear(&ToolTip);
					MicroProfileStringArrayAddLiteral(&ToolTip, "Context Switch");
					MicroProfileStringArrayFormat(&ToolTip, "%04x", S.nContextSwitchHoverThread);					
					MicroProfileStringArrayAddLiteral(&ToolTip, "Before");
					MicroProfileStringArrayFormat(&ToolTip, "%04x", S.nContextSwitchHoverThreadBefore);
					MicroProfileStringArrayAddLiteral(&ToolTip, "After");
					MicroProfileStringArrayFormat(&ToolTip, "%04x", S.nContextSwitchHoverThreadAfter);					
					MicroProfileStringArrayAddLiteral(&ToolTip, "Duration");
					int64_t nDifference = MicroProfileLogTickDifference(S.nContextSwitchHoverTickIn, S.nContextSwitchHoverTickOut);
					MicroProfileStringArrayFormat(&ToolTip, "%6.2fms", fToMs * nDifference );
					MicroProfileStringArrayAddLiteral(&ToolTip, "CPU");
					MicroProfileStringArrayFormat(&ToolTip, "%d", S.nContextSwitchHoverCpu);
					MicroProfileDrawFloatWindow(S.nMouseX, S.nMouseY+20, &ToolTip.ppStrings[0], ToolTip.nNumStrings, -1);


				}
				else if(S.nHoverFrame != -1)
				{
					uint32_t nNextFrame = (S.nHoverFrame+1)%MICROPROFILE_MAX_FRAME_HISTORY;
					int64_t nTick = S.Frames[S.nHoverFrame].nFrameStartCpu;
					int64_t nTickNext = S.Frames[nNextFrame].nFrameStartCpu;
					int64_t nTickGpu = S.Frames[S.nHoverFrame].nFrameStartGpu;
					int64_t nTickNextGpu = S.Frames[nNextFrame].nFrameStartGpu;

					float fToMs = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondCpu());
					float fToMsGpu = MicroProfileTickToMsMultiplier(MicroProfileTicksPerSecondGpu());
					float fMs = fToMs * (nTickNext - nTick);
					float fMsGpu = fToMsGpu * (nTickNextGpu - nTickGpu);
					MicroProfileStringArray ToolTip;
					MicroProfileStringArrayClear(&ToolTip);
					MicroProfileStringArrayFormat(&ToolTip, "Frame %d", S.nHoverFrame);
	#if MICROPROFILE_DEBUG
					MicroProfileStringArrayFormat(&ToolTip, "%p", &S.Frames[S.nHoverFrame]);
	#else
					MicroProfileStringArrayAddLiteral(&ToolTip, "");
	#endif
					MicroProfileStringArrayAddLiteral(&ToolTip, "CPU Time");
					MicroProfileStringArrayFormat(&ToolTip, "%6.2fms", fMs);
					MicroProfileStringArrayAddLiteral(&ToolTip, "GPU Time");
					MicroProfileStringArrayFormat(&ToolTip, "%6.2fms", fMsGpu);
					#if MICROPROFILE_DEBUG
					for(int i = 0; i < MICROPROFILE_MAX_GROUPS; ++i)
					{
						if(S.Frames[S.nHoverFrame].nLogStart[i])
						{
							MicroProfileStringArrayFormat(&ToolTip, "%d", i);
							MicroProfileStringArrayFormat(&ToolTip, "%d", S.Frames[S.nHoverFrame].nLogStart[i]);
						}
					}
					#endif
					MicroProfileDrawFloatWindow(S.nMouseX, S.nMouseY+20, &ToolTip.ppStrings[0], ToolTip.nNumStrings, -1);
				}
				if(S.nMouseLeft)
				{
					if(S.nHoverToken != MICROPROFILE_INVALID_TOKEN)
						MicroProfileToggleGraph(S.nHoverToken);
				}
			}
		}
#if MICROPROFILE_DRAWCURSOR
		{
			float fCursor[8] = 
			{
				MicroProfileMax(0, (int)S.nMouseX-3), S.nMouseY,
				MicroProfileMin(nWidth, S.nMouseX+3), S.nMouseY,
				S.nMouseX, MicroProfileMax((int)S.nMouseY-3, 0),
				S.nMouseX, MicroProfileMin(nHeight, S.nMouseY+3),
			};
			MicroProfileDrawLine2D(2, &fCursor[0], 0xff00ff00);
			MicroProfileDrawLine2D(2, &fCursor[4], 0xff00ff00);
		}
#endif

	}
	S.nMouseLeft = S.nMouseRight = 0;
	S.nMouseLeftMod = S.nMouseRightMod = 0;
	S.nMouseWheelDelta = 0;
	if(S.nOverflow)
		S.nOverflow--;

}
void MicroProfileMousePosition(uint32_t nX, uint32_t nY, int nWheelDelta)
{
	S.nMouseX = nX;
	S.nMouseY = nY;
	S.nMouseWheelDelta = nWheelDelta;
}

void MicroProfileModKey(uint32_t nKeyState)
{
	S.nModDown = nKeyState ? 1 : 0;
}

void MicroProfileClearGraph()
{
	for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
	{
		if(S.Graph[i].nToken != 0)
		{
			S.Graph[i].nToken = MICROPROFILE_INVALID_TOKEN;
		}
	}
}
void MicroProfileTogglePause()
{
	S.nRunning = !S.nRunning;
}

void MicroProfileGetState(MicroProfileState* pStateOut)
{
	pStateOut->nDisplay = S.nDisplay;
	pStateOut->nMenuAllGroups = S.nMenuAllGroups;
	pStateOut->nMenuActiveGroup = S.nMenuActiveGroup;
	pStateOut->nMenuAllThreads = S.nMenuAllThreads;
	pStateOut->nAggregateFlip = S.nAggregateFlip;
	pStateOut->nBars = S.nBars;
	pStateOut->fReferenceTime = S.fReferenceTime;
}

void MicroProfileSetState(MicroProfileState* pStateOut)
{
	MicroProfileScopeLock L(MicroProfileMutex());
	S.nDisplay = pStateOut->nDisplay;
	S.nMenuAllGroups = pStateOut->nMenuAllGroups;
	S.nMenuActiveGroup = pStateOut->nMenuActiveGroup;
	S.nMenuAllThreads = pStateOut->nMenuAllThreads;
	S.nAggregateFlip = pStateOut->nAggregateFlip;
	S.nBars = pStateOut->nBars;
	S.fReferenceTime = pStateOut->fReferenceTime;
	S.fRcpReferenceTime = 1.f / S.fReferenceTime;
}

void MicroProfileToggleGraph(MicroProfileToken nToken)
{
	nToken &= 0xffff;
	int32_t nMinSort = 0x7fffffff;
	int32_t nFreeIndex = -1;
	int32_t nMinIndex = 0;
	int32_t nMaxSort = 0x80000000;
	for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
	{
		if(S.Graph[i].nToken == MICROPROFILE_INVALID_TOKEN)
			nFreeIndex = i;
		if(S.Graph[i].nToken == nToken)
		{
			S.Graph[i].nToken = MICROPROFILE_INVALID_TOKEN;
			return;
		}
		if(S.Graph[i].nKey < nMinSort)
		{
			nMinSort = S.Graph[i].nKey;
			nMinIndex = i;
		}
		if(S.Graph[i].nKey > nMaxSort)
		{
			nMaxSort = S.Graph[i].nKey;
		}
	}
	int nIndex = nFreeIndex > -1 ? nFreeIndex : nMinIndex;
	S.Graph[nIndex].nToken = nToken;
	S.Graph[nIndex].nKey = nMaxSort+1;
	memset(&S.Graph[nIndex].nHistory[0], 0, sizeof(S.Graph[nIndex].nHistory));
}
void MicroProfileMouseButton(uint32_t nLeft, uint32_t nRight)
{
	if(0 == nLeft && S.nMouseDownLeft)
	{
		if(S.nModDown)
			S.nMouseLeftMod = 1;
		else
			S.nMouseLeft = 1;
	}

	if(0 == nRight && S.nMouseDownRight)
	{
		if(S.nModDown)
			S.nMouseRightMod = 1;
		else
			S.nMouseRight = 1;
	}

	S.nMouseDownLeft = nLeft;
	S.nMouseDownRight = nRight;
	
}

#include <stdio.h>

#define MICROPROFILE_PRESET_HEADER_MAGIC 0x28586813
#define MICROPROFILE_PRESET_HEADER_VERSION 0x00000100
struct MicroProfilePresetHeader
{
	uint32_t nMagic;
	uint32_t nVersion;
	//groups, threads, aggregate, reference frame, graphs timers
	uint32_t nGroups[MICROPROFILE_MAX_GROUPS];
	uint32_t nThreads[MICROPROFILE_MAX_THREADS];
	uint32_t nGraphName[MICROPROFILE_MAX_GRAPHS];
	uint32_t nGraphGroupName[MICROPROFILE_MAX_GRAPHS];
	uint32_t nMenuAllGroups;
	uint32_t nMenuAllThreads;
	uint32_t nAggregateFlip;
	float fReferenceTime;
	uint32_t nBars;
	uint32_t nDisplay;
	uint32_t nOpacityBackground;
	uint32_t nOpacityForeground;
};

#ifndef MICROPROFILE_PRESET_FILENAME_FUNC
#define MICROPROFILE_PRESET_FILENAME_FUNC MicroProfilePresetFilename
static const char* MicroProfilePresetFilename(const char* pSuffix)
{
	static char filename[512];
	snprintf(filename, sizeof(filename)-1, ".microprofilepreset.%s", pSuffix);
	return filename;
}
#endif

void MicroProfileSavePreset(const char* pPresetName)
{
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
	FILE* F = fopen(MICROPROFILE_PRESET_FILENAME_FUNC(pPresetName), "w");
	if(!F) return;

	MicroProfilePresetHeader Header;
	memset(&Header, 0, sizeof(Header));
	Header.nAggregateFlip = S.nAggregateFlip;
	Header.nBars = S.nBars;
	Header.fReferenceTime = S.fReferenceTime;
	Header.nMenuAllGroups = S.nMenuAllGroups;
	Header.nMenuAllThreads = S.nMenuAllThreads;
	Header.nMagic = MICROPROFILE_PRESET_HEADER_MAGIC;
	Header.nVersion = MICROPROFILE_PRESET_HEADER_VERSION;
	Header.nDisplay = S.nDisplay;
	Header.nOpacityBackground = S.nOpacityBackground;
	Header.nOpacityForeground = S.nOpacityForeground;
	fwrite(&Header, sizeof(Header), 1, F);
	uint64_t nMask = 1;
	for(uint32_t i = 0; i < MICROPROFILE_MAX_GROUPS; ++i)
	{
		if(S.nMenuActiveGroup & nMask)
		{
			uint32_t offset = ftell(F);
			const char* pName = S.GroupInfo[i].pName;
			int nLen = (int)strlen(pName)+1;
			fwrite(pName, nLen, 1, F);
			Header.nGroups[i] = offset;
		}
		nMask <<= 1;
	}
	for(uint32_t i = 0; i < MICROPROFILE_MAX_THREADS; ++i)
	{
		MicroProfileThreadLog* pLog = S.Pool[i];
		if(pLog && S.nThreadActive[i])
		{
			uint32_t nOffset = ftell(F);
			const char* pName = &pLog->ThreadName[0];
			int nLen = (int)strlen(pName)+1;
			fwrite(pName, nLen, 1, F);
			Header.nThreads[i] = nOffset;
		}
	}
	for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
	{
		MicroProfileToken nToken = S.Graph[i].nToken;
		if(nToken != MICROPROFILE_INVALID_TOKEN)
		{
			uint32_t nGroupIndex = MicroProfileGetGroupIndex(nToken);
			uint32_t nTimerIndex = MicroProfileGetTimerIndex(nToken);
			const char* pGroupName = S.GroupInfo[nGroupIndex].pName;
			const char* pTimerName = S.TimerInfo[nTimerIndex].pName;
			MP_ASSERT(pGroupName);
			MP_ASSERT(pTimerName);
			int nGroupLen = (int)strlen(pGroupName)+1;
			int nTimerLen = (int)strlen(pTimerName)+1;

			uint32_t nOffsetGroup = ftell(F);
			fwrite(pGroupName, nGroupLen, 1, F);
			uint32_t nOffsetTimer = ftell(F);
			fwrite(pTimerName, nTimerLen, 1, F);
			Header.nGraphName[i] = nOffsetTimer;
			Header.nGraphGroupName[i] = nOffsetGroup;
		}
	}
	fseek(F, 0, SEEK_SET);
	fwrite(&Header, sizeof(Header), 1, F);

	fclose(F);

}



void MicroProfileLoadPreset(const char* pSuffix)
{
	std::lock_guard<std::recursive_mutex> Lock(MicroProfileMutex());
	FILE* F = fopen(MICROPROFILE_PRESET_FILENAME_FUNC(pSuffix), "r");
	if(!F)
	{
	 	return;
	}
	fseek(F, 0, SEEK_END);
	int nSize = ftell(F);
	char* const pBuffer = (char*)alloca(nSize);
	fseek(F, 0, SEEK_SET);
	int nRead = (int)fread(pBuffer, nSize, 1, F);
	fclose(F);
	if(1 != nRead)
		return;
	
	MicroProfilePresetHeader& Header = *(MicroProfilePresetHeader*)pBuffer;

	if(Header.nMagic != MICROPROFILE_PRESET_HEADER_MAGIC || Header.nVersion != MICROPROFILE_PRESET_HEADER_VERSION)
	{
		return;
	}

	S.nAggregateFlip = Header.nAggregateFlip;
	S.nBars = Header.nBars;
	S.fReferenceTime = Header.fReferenceTime;
	S.fRcpReferenceTime = 1.f / Header.fReferenceTime;
	S.nMenuAllGroups = Header.nMenuAllGroups;
	S.nMenuAllThreads = Header.nMenuAllThreads;
	S.nDisplay = Header.nDisplay;
	S.nMenuActiveGroup = 0;
	S.nOpacityBackground = Header.nOpacityBackground;
	S.nOpacityForeground = Header.nOpacityForeground;

	memset(&S.nThreadActive[0], 0, sizeof(S.nThreadActive));

	for(uint32_t i = 0; i < MICROPROFILE_MAX_GROUPS; ++i)
	{
		if(Header.nGroups[i])
		{
			const char* pGroupName = pBuffer + Header.nGroups[i];	
			for(uint32_t j = 0; j < MICROPROFILE_MAX_GROUPS; ++j)
			{
				if(S.GroupInfo[j].pName && 0 == MP_STRCASECMP(pGroupName, S.GroupInfo[j].pName))
				{
					S.nMenuActiveGroup |= (1ll << j);
				}
			}
		}
	}
	for(uint32_t i = 0; i < MICROPROFILE_MAX_THREADS; ++i)
	{
		if(Header.nThreads[i])
		{
			const char* pThreadName = pBuffer + Header.nThreads[i];
			for(uint32_t j = 0; j < MICROPROFILE_MAX_THREADS; ++j)
			{
				MicroProfileThreadLog* pLog = S.Pool[j];
				if(pLog && 0 == MP_STRCASECMP(pThreadName, &pLog->ThreadName[0]))
				{
					S.nThreadActive[j] = 1;
				}
			}
		}
	}
	for(uint32_t i = 0; i < MICROPROFILE_MAX_GRAPHS; ++i)
	{
		MicroProfileToken nPrevToken = S.Graph[i].nToken;
		S.Graph[i].nToken = MICROPROFILE_INVALID_TOKEN;
		if(Header.nGraphName[i] && Header.nGraphGroupName[i])
		{
			const char* pGraphName = pBuffer + Header.nGraphName[i];
			const char* pGraphGroupName = pBuffer + Header.nGraphGroupName[i];
			for(uint32_t j = 0; j < S.nTotalTimers; ++j)
			{
				uint64_t nGroupIndex = S.TimerInfo[j].nGroupIndex;
				if(0 == MP_STRCASECMP(pGraphName, S.TimerInfo[j].pName) && 0 == MP_STRCASECMP(pGraphGroupName, S.GroupInfo[nGroupIndex].pName))
				{
					MicroProfileToken nToken = MicroProfileMakeToken(1ll << nGroupIndex, (uint16_t)j);
					S.Graph[i].nToken = nToken;
					if(nToken != nPrevToken)
					{
						memset(&S.Graph[i].nHistory, 0, sizeof(S.Graph[i].nHistory));
					}
					break;
				}
			}
		}
	}
}

void MicroProfileDrawLineVertical(int nX, int nTop, int nBottom, uint32_t nColor)
{
	MicroProfileDrawBox(nX, nTop, nX + 1, nBottom, nColor);
}

void MicroProfileDrawLineHorizontal(int nLeft, int nRight, int nY, uint32_t nColor)
{
	MicroProfileDrawBox(nLeft, nY, nRight, nY + 1, nColor);
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



#if MICROPROFILE_CONTEXT_SWITCH_TRACE
#ifdef _WIN32
#define INITGUID
#include <evntrace.h>
#include <evntcons.h>
#include <strsafe.h>


static GUID g_MicroProfileThreadClassGuid = { 0x3d6fa8d1, 0xfe05, 0x11d0, 0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c };

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


void MicroProfileTraceThread(int unused)
{

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
		log.LoggerName = KERNEL_LOGGER_NAME;
		log.ProcessTraceMode = 0;
		TRACEHANDLE hLog = OpenTrace(&log);
		if (hLog)
		{
			ControlTrace(SessionHandle, KERNEL_LOGGER_NAME, &sessionProperties, EVENT_TRACE_CONTROL_STOP);
		}
		CloseTrace(hLog);

			
	}
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

	log.LoggerName = KERNEL_LOGGER_NAME;
	log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
	log.EventCallback = MicroProfileContextSwitchCallback;
	log.BufferCallback = MicroProfileBufferCallback;

	TRACEHANDLE hLog = OpenTrace(&log);
	ProcessTrace(&hLog, 1, 0, 0);
	CloseTrace(hLog);
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
