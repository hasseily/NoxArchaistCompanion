//
// XAudioCore.cpp
//

#include "pch.h"
#include "XAudioCore.h"

//-----------------------------------------------------------------------------

#define MAX_SOUND_DEVICES 10
const DWORD SPKR_SAMPLE_RATE = 44100;

static wchar_t* sound_devices[MAX_SOUND_DEVICES];
static GUID sound_device_guid[MAX_SOUND_DEVICES];
static int num_sound_devices = 0;

//-------------------------------------

// Used for muting & fading:

static const UINT uMAX_VOICES = 66;	// 64 phonemes + spkr + mockingboard
static UINT g_uNumVoices = 0;
static XAVOICE* g_pVoices[uMAX_VOICES] = { NULL };
static XAVOICE* g_pSpeakerVoice = NULL;

//-------------------------------------

bool g_bXAAvailable = false;

//-----------------------------------------------------------------------------


// Forward declarations
// TODO: Remove them as this is just for loading wav files that we don't need
#ifdef _XBOX //Big-Endian
#define fourccRIFF 'RIFF'
#define fourccDATA 'data'
#define fourccFMT 'fmt '
#define fourccWAVE 'WAVE'
#define fourccXWMA 'XWMA'
#define fourccDPDS 'dpds'
#endif

#ifndef _XBOX //Little-Endian
#define fourccRIFF 'FFIR'
#define fourccDATA 'atad'
#define fourccFMT ' tmf'
#define fourccWAVE 'EVAW'
#define fourccXWMA 'AMWX'
#define fourccDPDS 'sdpd'
#endif
HRESULT FindChunk(HANDLE hFile, DWORD fourcc, DWORD& dwChunkSize, DWORD& dwChunkDataPosition);
HRESULT ReadChunkData(HANDLE hFile, void* buffer, DWORD buffersize, DWORD bufferoffset);
//

HRESULT XAudioCore::Init(HWND window)
{
	HRESULT hr;
	hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr))
		return hr;

	pXAudio2 = nullptr;
	if (FAILED(hr = XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR)))
		return hr;
	pMasterVoice = nullptr;
	if (FAILED(hr = pXAudio2->CreateMasteringVoice(&pMasterVoice)))
		return hr;

	WAVEFORMATEXTENSIBLE wfx = { 0 };
	// THIS IS A TEST - See DSGetSoundBuffer()
	WAVEFORMATEX wavfmt;
	wavfmt.wFormatTag = WAVE_FORMAT_PCM;
	wavfmt.nChannels = 1;
	wavfmt.nSamplesPerSec = SPKR_SAMPLE_RATE;
	wavfmt.wBitsPerSample = 16;
	wavfmt.nBlockAlign = wavfmt.nChannels == 1 ? 2 : 4;
	wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * wavfmt.nSamplesPerSec;

	wfx.Format = wavfmt;


	if (FAILED(hr = pXAudio2->CreateSourceVoice(&pSourceVoice, (WAVEFORMATEX*)&wfx)))
		return hr;
	/*
	// TEST PLAY
	buffer = { 0 };
	HANDLE hFile = CreateFile(
		TEXT("Assets/Sample.wav"),
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);

	if (INVALID_HANDLE_VALUE == hFile)
		return HRESULT_FROM_WIN32(GetLastError());

	if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, 0, NULL, FILE_BEGIN))
		return HRESULT_FROM_WIN32(GetLastError());

	DWORD dwChunkSize;
	DWORD dwChunkPosition;
	//check the file type, should be fourccWAVE or 'XWMA'
	FindChunk(hFile, fourccRIFF, dwChunkSize, dwChunkPosition);
	DWORD filetype;
	ReadChunkData(hFile, &filetype, sizeof(DWORD), dwChunkPosition);
	if (filetype != fourccWAVE)
		return S_FALSE;
	FindChunk(hFile, fourccFMT, dwChunkSize, dwChunkPosition);
	ReadChunkData(hFile, &wfx, dwChunkSize, dwChunkPosition);
	//fill out the audio data buffer with the contents of the fourccDATA chunk
	FindChunk(hFile, fourccDATA, dwChunkSize, dwChunkPosition);
	BYTE* pDataBuffer = new BYTE[dwChunkSize];
	ReadChunkData(hFile, pDataBuffer, dwChunkSize, dwChunkPosition);
	buffer.AudioBytes = dwChunkSize;  //size of the audio buffer in bytes
	buffer.pAudioData = pDataBuffer;  //buffer containing audio data
	buffer.Flags = XAUDIO2_END_OF_STREAM; // tell the source voice not to expect any data after this buffer

	//https://docs.microsoft.com/en-us/windows/win32/xaudio2/how-to--play-a-sound-with-xaudio2

	if (FAILED(hr = pSourceVoice->SubmitSourceBuffer(&buffer)))
		return hr;
	if (FAILED(hr = pSourceVoice->Start(0)))
		return hr;
		*/
	return S_OK;
}

void XAudioCore::Uninit()
{
	pMasterVoice->DestroyVoice();
	pMasterVoice = nullptr;
	pXAudio2->Release();
	pXAudio2 = nullptr;
}

ULONG XAudioCore::PlayBuffer(const short* pSpeakerBuffer, ULONG nNumSamples)
{
	if (pSpeakerBuffer == NULL)
	{
		return 0;
	}
	WAVEFORMATEXTENSIBLE wfx = { 0 };
	WAVEFORMATEX wavfmt;
	wavfmt.wFormatTag = WAVE_FORMAT_PCM;
	wavfmt.nChannels = 2;
	wavfmt.nSamplesPerSec = SPKR_SAMPLE_RATE;
	wavfmt.wBitsPerSample = 16;
	wavfmt.nBlockAlign = wavfmt.nChannels == 1 ? 2 : 4;
	wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * wavfmt.nSamplesPerSec;
	wfx.Format = wavfmt;

	buffer.AudioBytes = nNumSamples*sizeof(short);
	BYTE* buf = (BYTE*)malloc(buffer.AudioBytes);
	memcpy_s(buf, buffer.AudioBytes, pSpeakerBuffer, buffer.AudioBytes);
	buffer.pAudioData = buf;
	buffer.PlayBegin = 0;
	buffer.PlayLength = 0;
	buffer.LoopBegin = 0;
	buffer.LoopLength = 0;
	buffer.LoopCount = 1;
	//pXAudio2->CreateSourceVoice(&pSourceVoice, (WAVEFORMATEX*)&wfx);
	pSourceVoice->SetVolume(1.f);
	pSourceVoice->SubmitSourceBuffer(&buffer);
	pSourceVoice->Start(0);
	return nNumSamples;
}

HRESULT XAudioCore::GetSoundBuffer(XAVOICE* pVoice, DWORD dwFlags, DWORD dwBufferSize, DWORD nSampleRate, int nChannels)
{
	WAVEFORMATEXTENSIBLE wfx = { 0 };
	WAVEFORMATEX wavfmt;
	wavfmt.wFormatTag = WAVE_FORMAT_PCM;
	wavfmt.nChannels = 1;
	wavfmt.nSamplesPerSec = SPKR_SAMPLE_RATE;
	wavfmt.wBitsPerSample = 16;
	wavfmt.nBlockAlign = wavfmt.nChannels == 1 ? 2 : 4;
	wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * wavfmt.nSamplesPerSec;
	wfx.Format = wavfmt;

	buffer.AudioBytes = dwBufferSize;
	BYTE* buf = (BYTE *)malloc(dwBufferSize);
	buffer.pAudioData = buf;
	buffer.PlayBegin = 0;
	buffer.PlayLength = 0;
	buffer.LoopBegin = 0;
	buffer.LoopLength = 0;
	buffer.LoopCount = 1;
	pVoice->pXABvoice = &buffer;
	return S_FALSE;
}

//
//
// TODO: Everything below should be deleted as we don't need to read sound files
// This is just for testing
// https://docs.microsoft.com/en-us/windows/win32/xaudio2/how-to--load-audio-data-files-in-xaudio2

HRESULT FindChunk(HANDLE hFile, DWORD fourcc, DWORD& dwChunkSize, DWORD& dwChunkDataPosition)
{
	HRESULT hr = S_OK;
	if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, 0, NULL, FILE_BEGIN))
		return HRESULT_FROM_WIN32(GetLastError());

	DWORD dwChunkType;
	DWORD dwChunkDataSize;
	DWORD dwRIFFDataSize = 0;
	DWORD dwFileType;
	DWORD bytesRead = 0;
	DWORD dwOffset = 0;

	while (hr == S_OK)
	{
		DWORD dwRead;
		if (0 == ReadFile(hFile, &dwChunkType, sizeof(DWORD), &dwRead, NULL))
			hr = HRESULT_FROM_WIN32(GetLastError());

		if (0 == ReadFile(hFile, &dwChunkDataSize, sizeof(DWORD), &dwRead, NULL))
			hr = HRESULT_FROM_WIN32(GetLastError());

		switch (dwChunkType)
		{
		case fourccRIFF:
			dwRIFFDataSize = dwChunkDataSize;
			dwChunkDataSize = 4;
			if (0 == ReadFile(hFile, &dwFileType, sizeof(DWORD), &dwRead, NULL))
				hr = HRESULT_FROM_WIN32(GetLastError());
			break;

		default:
			if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, dwChunkDataSize, NULL, FILE_CURRENT))
				return HRESULT_FROM_WIN32(GetLastError());
		}

		dwOffset += sizeof(DWORD) * 2;

		if (dwChunkType == fourcc)
		{
			dwChunkSize = dwChunkDataSize;
			dwChunkDataPosition = dwOffset;
			return S_OK;
		}

		dwOffset += dwChunkDataSize;

		if (bytesRead >= dwRIFFDataSize) return S_FALSE;

	}

	return S_OK;

}

HRESULT ReadChunkData(HANDLE hFile, void* buffer, DWORD buffersize, DWORD bufferoffset)
{
	HRESULT hr = S_OK;
	if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, bufferoffset, NULL, FILE_BEGIN))
		return HRESULT_FROM_WIN32(GetLastError());
	DWORD dwRead;
	if (0 == ReadFile(hFile, buffer, buffersize, &dwRead, NULL))
		hr = HRESULT_FROM_WIN32(GetLastError());
	return hr;
}