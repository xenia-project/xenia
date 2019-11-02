/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XAUDIO2_XAUDIO2_API_H_
#define XENIA_APU_XAUDIO2_XAUDIO2_API_H_

#include <Audioclient.h>

#include "xenia/base/platform_win.h"

namespace xe {
namespace apu {
namespace xaudio2 {
namespace api {

// Parts of XAudio2.h needed for Xenia. The header in the Windows SDK (for 2.8+)
// or the DirectX SDK (for 2.7 and below) is for a single version that is chosen
// for the target OS version at compile time, and cannot be useful for different
// DLL versions.
//
// XAudio 2.8 is also not available on Windows 7, but including the 2.7 header
// is complicated because it has the same name and include guard as the 2.8 one,
// and also 2.7 is outdated - support already dropped in Microsoft Store apps,
// for instance.
//
// Copyright (c) Microsoft Corporation.  All rights reserved.

class __declspec(uuid("5a508685-a254-4fba-9b82-9a24b00306af")) XAudio2_7;
interface __declspec(uuid("8bcf1f58-9fe7-4583-8ac6-e2adc465c8bb")) IXAudio2_7;
interface __declspec(uuid("60d8dac8-5aa1-4e8e-b597-2f5e2883d484")) IXAudio2_8;

#pragma pack(push, 1)

static constexpr UINT32 XE_XAUDIO2_MAX_QUEUED_BUFFERS = 64;
static constexpr float XE_XAUDIO2_MAX_FREQ_RATIO = 1024.0f;
static constexpr float XE_XAUDIO2_DEFAULT_FREQ_RATIO = 2.0f;

static constexpr UINT32 XE_XAUDIO2_COMMIT_NOW = 0;
static constexpr UINT32 XE_XAUDIO2_NO_LOOP_REGION = 0;
static constexpr UINT32 XE_XAUDIO2_DEFAULT_CHANNELS = 0;
static constexpr UINT32 XE_XAUDIO2_DEFAULT_SAMPLERATE = 0;

// 2.8+ only.
static constexpr UINT32 XE_XAUDIO2_VOICE_NOSAMPLESPLAYED = 0x0100;

interface IXAudio2_7;
interface IXAudio2_8;
interface IXAudio2Voice;
interface IXAudio2_7SourceVoice;
interface IXAudio2_8SourceVoice;
interface IXAudio2SubmixVoice;
interface IXAudio2_7MasteringVoice;
interface IXAudio2_8MasteringVoice;
interface IXAudio2EngineCallback;
interface IXAudio2VoiceCallback;

typedef UINT32 XAUDIO2_PROCESSOR;
static constexpr XAUDIO2_PROCESSOR XE_XAUDIO2_ANY_PROCESSOR = 0xFFFFFFFF;
static constexpr XAUDIO2_PROCESSOR XE_XAUDIO2_7_DEFAULT_PROCESSOR =
    XE_XAUDIO2_ANY_PROCESSOR;
static constexpr XAUDIO2_PROCESSOR XE_XAUDIO2_8_DEFAULT_PROCESSOR = 0x00000001;

// 2.7 only.
struct XAUDIO2_DEVICE_DETAILS;

struct XAUDIO2_VOICE_DETAILS;
struct XAUDIO2_VOICE_SENDS;
struct XAUDIO2_EFFECT_CHAIN;
struct XAUDIO2_FILTER_PARAMETERS;

struct XAUDIO2_BUFFER {
  UINT32 Flags;
  UINT32 AudioBytes;
  const BYTE* pAudioData;
  UINT32 PlayBegin;
  UINT32 PlayLength;
  UINT32 LoopBegin;
  UINT32 LoopLength;
  UINT32 LoopCount;
  void* pContext;
};

struct XAUDIO2_BUFFER_WMA;

struct XAUDIO2_VOICE_STATE {
  void* pCurrentBufferContext;
  UINT32 BuffersQueued;
  UINT64 SamplesPlayed;
};

struct XAUDIO2_PERFORMANCE_DATA;

struct XAUDIO2_DEBUG_CONFIGURATION {
  UINT32 TraceMask;
  UINT32 BreakMask;
  BOOL LogThreadID;
  BOOL LogFileline;
  BOOL LogFunctionName;
  BOOL LogTiming;
};

static constexpr UINT32 XE_XAUDIO2_LOG_ERRORS = 0x0001;
static constexpr UINT32 XE_XAUDIO2_LOG_WARNINGS = 0x0002;

// clang-format off

// IXAudio2 2.7.
DECLARE_INTERFACE_(IXAudio2_7, IUnknown) {
  STDMETHOD(QueryInterface)(REFIID riid, void** ppvInterface) = 0;
  STDMETHOD_(ULONG, AddRef)() = 0;
  STDMETHOD_(ULONG, Release)() = 0;
  // 2.7 only.
  STDMETHOD(GetDeviceCount)(UINT32* pCount) = 0;
  // 2.7 only.
  STDMETHOD(GetDeviceDetails)(UINT32 Index,
                              XAUDIO2_DEVICE_DETAILS* pDeviceDetails) = 0;
  // 2.7 only.
  STDMETHOD(Initialize)(
      UINT32 Flags = 0,
      XAUDIO2_PROCESSOR XAudio2Processor = XE_XAUDIO2_7_DEFAULT_PROCESSOR) = 0;
  STDMETHOD(RegisterForCallbacks)(IXAudio2EngineCallback* pCallback) = 0;
  STDMETHOD_(void, UnregisterForCallbacks)(
      IXAudio2EngineCallback* pCallback) = 0;
  STDMETHOD(CreateSourceVoice)(
      IXAudio2_7SourceVoice** ppSourceVoice, const WAVEFORMATEX* pSourceFormat,
      UINT32 Flags = 0, float MaxFrequencyRatio = XE_XAUDIO2_DEFAULT_FREQ_RATIO,
      IXAudio2VoiceCallback* pCallback = nullptr,
      const XAUDIO2_VOICE_SENDS* pSendList = nullptr,
      const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) = 0;
  STDMETHOD(CreateSubmixVoice)(
      IXAudio2SubmixVoice** ppSubmixVoice, UINT32 InputChannels,
      UINT32 InputSampleRate, UINT32 Flags = 0, UINT32 ProcessingStage = 0,
      const XAUDIO2_VOICE_SENDS* pSendList = nullptr,
      const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) = 0;
  // 2.7: Device index instead of device ID, no stream category.
  STDMETHOD(CreateMasteringVoice)(
      IXAudio2_7MasteringVoice** ppMasteringVoice,
      UINT32 InputChannels = XE_XAUDIO2_DEFAULT_CHANNELS,
      UINT32 InputSampleRate = XE_XAUDIO2_DEFAULT_SAMPLERATE, UINT32 Flags = 0,
      UINT32 DeviceIndex = 0,
      const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) = 0;
  STDMETHOD(StartEngine)() = 0;
  STDMETHOD_(void, StopEngine)() = 0;
  STDMETHOD(CommitChanges)(UINT32 OperationSet) = 0;
  STDMETHOD_(void, GetPerformanceData)(XAUDIO2_PERFORMANCE_DATA* pPerfData) = 0;
  STDMETHOD_(void, SetDebugConfiguration)(
      const XAUDIO2_DEBUG_CONFIGURATION* pDebugConfiguration,
      void* pReserved = nullptr) = 0;
};

// IXAudio2 2.8.
DECLARE_INTERFACE_(IXAudio2_8, IUnknown) {
  STDMETHOD(QueryInterface)(REFIID riid, void** ppvInterface) = 0;
  STDMETHOD_(ULONG, AddRef)() = 0;
  STDMETHOD_(ULONG, Release)() = 0;
  // 2.8: No GetDeviceCount, GetDeviceDetails and Initialize.
  STDMETHOD(RegisterForCallbacks)(IXAudio2EngineCallback* pCallback) = 0;
  STDMETHOD_(void, UnregisterForCallbacks)(
      IXAudio2EngineCallback* pCallback) = 0;
  STDMETHOD(CreateSourceVoice)(
      IXAudio2_8SourceVoice** ppSourceVoice, const WAVEFORMATEX* pSourceFormat,
      UINT32 Flags = 0, float MaxFrequencyRatio = XE_XAUDIO2_DEFAULT_FREQ_RATIO,
      IXAudio2VoiceCallback* pCallback = nullptr,
      const XAUDIO2_VOICE_SENDS* pSendList = nullptr,
      const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) = 0;
  STDMETHOD(CreateSubmixVoice)(
      IXAudio2SubmixVoice** ppSubmixVoice, UINT32 InputChannels,
      UINT32 InputSampleRate, UINT32 Flags = 0, UINT32 ProcessingStage = 0,
      const XAUDIO2_VOICE_SENDS* pSendList = nullptr,
      const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) = 0;
  // 2.8: Device ID instead of device index, added stream category.
  STDMETHOD(CreateMasteringVoice)(
      IXAudio2_8MasteringVoice** ppMasteringVoice,
      UINT32 InputChannels = XE_XAUDIO2_DEFAULT_CHANNELS,
      UINT32 InputSampleRate = XE_XAUDIO2_DEFAULT_SAMPLERATE, UINT32 Flags = 0,
      LPCWSTR szDeviceId = nullptr,
      const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr,
      AUDIO_STREAM_CATEGORY StreamCategory = AudioCategory_GameEffects) = 0;
  STDMETHOD(StartEngine)() = 0;
  STDMETHOD_(void, StopEngine)() = 0;
  STDMETHOD(CommitChanges)(UINT32 OperationSet) = 0;
  STDMETHOD_(void, GetPerformanceData)(XAUDIO2_PERFORMANCE_DATA* pPerfData) = 0;
  STDMETHOD_(void, SetDebugConfiguration)(
      const XAUDIO2_DEBUG_CONFIGURATION* pDebugConfiguration,
      void* pReserved = nullptr) = 0;
};

DECLARE_INTERFACE(IXAudio2Voice) {
#define XE_APU_XAUDIO2_API_DECLARE_IXAUDIO2VOICE_METHODS \
  STDMETHOD_(void, GetVoiceDetails)(XAUDIO2_VOICE_DETAILS* pVoiceDetails) = 0; \
  STDMETHOD(SetOutputVoices)(const XAUDIO2_VOICE_SENDS* pSendList) = 0; \
  STDMETHOD(SetEffectChain)(const XAUDIO2_EFFECT_CHAIN* pEffectChain) = 0; \
  STDMETHOD(EnableEffect)(UINT32 EffectIndex, \
                          UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0; \
  STDMETHOD(DisableEffect)(UINT32 EffectIndex, \
                           UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0; \
  STDMETHOD_(void, GetEffectState)(UINT32 EffectIndex, BOOL* pEnabled) = 0; \
  STDMETHOD(SetEffectParameters)( \
      UINT32 EffectIndex, const void* pParameters, UINT32 ParametersByteSize, \
      UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0; \
  STDMETHOD(GetEffectParameters)(UINT32 EffectIndex, void* pParameters, \
                                 UINT32 ParametersByteSize) = 0; \
  STDMETHOD(SetFilterParameters)( \
      const XAUDIO2_FILTER_PARAMETERS* pParameters, \
      UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0; \
  STDMETHOD_(void, GetFilterParameters)( \
      XAUDIO2_FILTER_PARAMETERS* pParameters) = 0; \
  STDMETHOD(SetOutputFilterParameters)( \
      IXAudio2Voice* pDestinationVoice, \
      const XAUDIO2_FILTER_PARAMETERS* pParameters, \
      UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0; \
  STDMETHOD_(void, GetOutputFilterParameters)( \
      IXAudio2Voice* pDestinationVoice, \
      XAUDIO2_FILTER_PARAMETERS* pParameters) = 0; \
  STDMETHOD(SetVolume)(float Volume, \
                       UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0; \
  STDMETHOD_(void, GetVolume)(float* pVolume) = 0; \
  STDMETHOD(SetChannelVolumes)( \
      UINT32 Channels, const float* pVolumes, \
      UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0; \
  STDMETHOD_(void, GetChannelVolumes)(UINT32 Channels, float* pVolumes) = 0; \
  STDMETHOD(SetOutputMatrix)(IXAudio2Voice* pDestinationVoice, \
                             UINT32 SourceChannels, \
                             UINT32 DestinationChannels, \
                             const float* pLevelMatrix, \
                             UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0; \
  STDMETHOD_(void, GetOutputMatrix)(IXAudio2Voice* pDestinationVoice, \
                                    UINT32 SourceChannels, \
                                    UINT32 DestinationChannels, \
                                    float* pLevelMatrix) = 0; \
  STDMETHOD_(void, DestroyVoice)() = 0;
  XE_APU_XAUDIO2_API_DECLARE_IXAUDIO2VOICE_METHODS
};

// IXAudio2SourceVoice 2.7.
DECLARE_INTERFACE_(IXAudio2_7SourceVoice, IXAudio2Voice) {
  XE_APU_XAUDIO2_API_DECLARE_IXAUDIO2VOICE_METHODS
  STDMETHOD(Start)(UINT32 Flags = 0,
                   UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0;
  STDMETHOD(Stop)(UINT32 Flags = 0,
                  UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0;
  STDMETHOD(SubmitSourceBuffer)(
      const XAUDIO2_BUFFER* pBuffer,
      const XAUDIO2_BUFFER_WMA* pBufferWMA = nullptr) = 0;
  STDMETHOD(FlushSourceBuffers)() = 0;
  STDMETHOD(Discontinuity)() = 0;
  STDMETHOD(ExitLoop)(UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0;
  // 2.7: No Flags.
  STDMETHOD_(void, GetState)(XAUDIO2_VOICE_STATE* pVoiceState) = 0;
  STDMETHOD(SetFrequencyRatio)(float Ratio,
                               UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0;
  STDMETHOD_(void, GetFrequencyRatio)(float* pRatio) = 0;
  STDMETHOD(SetSourceSampleRate)(UINT32 NewSourceSampleRate) = 0;
};

// IXAudio2SourceVoice 2.8.
DECLARE_INTERFACE_(IXAudio2_8SourceVoice, IXAudio2Voice) {
  XE_APU_XAUDIO2_API_DECLARE_IXAUDIO2VOICE_METHODS
  STDMETHOD(Start)(UINT32 Flags = 0,
                   UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0;
  STDMETHOD(Stop)(UINT32 Flags = 0,
                  UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0;
  STDMETHOD(SubmitSourceBuffer)(
      const XAUDIO2_BUFFER* pBuffer,
      const XAUDIO2_BUFFER_WMA* pBufferWMA = nullptr) = 0;
  STDMETHOD(FlushSourceBuffers)() = 0;
  STDMETHOD(Discontinuity)() = 0;
  STDMETHOD(ExitLoop)(UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0;
  // 2.8: Flags added.
  STDMETHOD_(void, GetState)(XAUDIO2_VOICE_STATE* pVoiceState,
                             UINT32 Flags = 0) = 0;
  STDMETHOD(SetFrequencyRatio)(float Ratio,
                               UINT32 OperationSet = XE_XAUDIO2_COMMIT_NOW) = 0;
  STDMETHOD_(void, GetFrequencyRatio)(float* pRatio) = 0;
  STDMETHOD(SetSourceSampleRate)(UINT32 NewSourceSampleRate) = 0;
};

// IXAudio2MasteringVoice 2.7.
DECLARE_INTERFACE_(IXAudio2_7MasteringVoice, IXAudio2Voice) {
  XE_APU_XAUDIO2_API_DECLARE_IXAUDIO2VOICE_METHODS
  // 2.7: No GetChannelMask.
};

// IXAudio2MasteringVoice 2.8.
DECLARE_INTERFACE_(IXAudio2_8MasteringVoice, IXAudio2Voice) {
  XE_APU_XAUDIO2_API_DECLARE_IXAUDIO2VOICE_METHODS
  // 2.8: GetChannelMask added.
  STDMETHOD(GetChannelMask)(DWORD* pChannelmask) = 0;
};

DECLARE_INTERFACE(IXAudio2VoiceCallback) {
  // Called just before this voice's processing pass begins.
  STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32 BytesRequired) = 0;
  STDMETHOD_(void, OnVoiceProcessingPassEnd)() = 0;
  STDMETHOD_(void, OnStreamEnd)() = 0;
  STDMETHOD_(void, OnBufferStart)(void* pBufferContext) = 0;
  STDMETHOD_(void, OnBufferEnd)(void* pBufferContext) = 0;
  STDMETHOD_(void, OnLoopEnd)(void* pBufferContext) = 0;
  STDMETHOD_(void, OnVoiceError)(void* pBufferContext, HRESULT Error) = 0;
};

#pragma pack(pop)

}  // namespace api
}  // namespace xaudio2
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XAUDIO2_XAUDIO2_API_H_
