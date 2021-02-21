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


class XAudioCore
{
public:
	enum eFADE { FADE_NONE, FADE_IN, FADE_OUT };

	HRESULT Init(HWND window);
	void Uninit();

	HRESULT GetSoundBuffer();
	void ReleaseSoundBuffer();
	bool ZeroVoiceBuffer();
	bool ZeroVoiceWritableBuffer();

	void SetFade(XAudioCore::eFADE FadeType);
	bool GetTimerState();
	void TweakVolumes();
	LONG NewVolume(DWORD dwVolume, DWORD dwVolumeMax);

	int GetErrorInc();
	void SetErrorInc(const int nErrorInc);
	int GetErrorMax();
	void SetErrorMax(const int nErrorMax);

private:
	IXAudio2* pXAudio2;
	IXAudio2MasteringVoice* pMasterVoice;
	IXAudio2SourceVoice* pSourceVoice;
	WAVEFORMATEXTENSIBLE wfx;
	XAUDIO2_BUFFER buffer;
};


/*
* // TODO: These are defined in SoundCore
void SysClk_WaitTimer();
bool SysClk_InitTimer();
void SysClk_UninitTimer();
void SysClk_StartTimerUsec(DWORD dwUsecPeriod);
void SysClk_StopTimer();
*/

//

