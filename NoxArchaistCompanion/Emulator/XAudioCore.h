//
// XAudioCore.h
//

#pragma once
#include <xapo.h>
#include <hrtfapoapi.h>
#include <x3daudio.h>
#include <xapobase.h>
#include <xapofx.h>
#include <xaudio2.h>
#include <xaudio2fx.h>

// These are the volume limits for the UI to use
//constexpr long VOLUME_MIN = 0;
//constexpr long VOLUME_MAX = 59;
//constexpr long MAX_SAMPLES = (16 * 1024);

extern bool g_bXAudioAvailable;

// These are the volume limits for the UI to use
// Simple 0-100!
constexpr long XAVOLUME_MIN = 0;
constexpr long XAVOLUME_MAX = 100;
constexpr long XAMAX_SAMPLES = 16 * 1024;

typedef struct
{
	XAUDIO2_BUFFER* pXABvoice;
	IXAudio2VoiceCallback* pCallback;
	bool bActive;			// Playback is active
	bool bMute;
	LONG nVolume;			// Current volume
	LONG nFadeVolume;		// Current fade volume
	DWORD dwUserVolume;		// Volume selected by user (TODO: Useless now that we have volume 0-100?)
	bool bIsSpeaker;
	bool bRecentlyActive;	// (Speaker only) false after 0.2s of speaker inactivity
} XAVOICE, * PXAVOICE;


class XAudioCore
{
public:
	enum eFADE { FADE_NONE, FADE_IN, FADE_OUT };

	HRESULT Init(HWND window);
	void Uninit();
	ULONG PlayBuffer(const short* pSpeakerBuffer, ULONG nNumSamples);

	HRESULT GetSoundBuffer(XAVOICE* pVoice, DWORD dwFlags, DWORD dwBufferSize, DWORD nSampleRate, int nChannels);
	void ReleaseSoundBuffer(XAVOICE* pVoice);
	bool ZeroVoiceBuffer(XAVOICE Voice, const char* pszDevName, DWORD dwBufferSize);
	bool ZeroVoiceWritableBuffer(XAVOICE Voice, const char* pszDevName, DWORD dwBufferSize);

	void SetFade(XAudioCore::eFADE FadeType);
	bool GetTimerState();
	void TweakVolumes();
	LONG NewVolume(DWORD dwVolume, DWORD dwVolumeMax);

	int GetErrorInc();
	void SetErrorInc(const int nErrorInc);
	int GetErrorMax();
	void SetErrorMax(const int nErrorMax);

	void SysClk_WaitTimer();
	bool SysClk_InitTimer();
	void SysClk_UninitTimer();
	void SysClk_StartTimerUsec(DWORD dwUsecPeriod);
	void SysClk_StopTimer();

private:
	IXAudio2* pXAudio2;
	IXAudio2MasteringVoice* pMasterVoice;
	IXAudio2SourceVoice* pSourceVoice;
	XAUDIO2_BUFFER buffer;
};
