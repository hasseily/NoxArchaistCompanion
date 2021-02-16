/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2014, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: main
 *
 * Author: Various
 */

#include "pch.h"

#include "AppleWin.h"
#include "CardManager.h"
#include "CPU.h"
#include "DiskImage.h"
#include "Harddisk.h"
#include "Keyboard.h"
#include "Joystick.h"
#include "LanguageCard.h"
#include "Memory.h"
#include "Mockingboard.h"
#include "RemoteControl/RemoteControlManager.h"
#include "SoundCore.h"
#include "Speaker.h"
#include "SynchronousEventManager.h"
#include "Video.h"
#include "RGBMonitor.h"
#include "NTSC.h"
#include "../Game.h"

RemoteControlManager g_RemoteControlMgr;

eApple2Type	g_Apple2Type = A2TYPE_APPLE2EENHANCED;

bool      g_bFullSpeed = false;

//=================================================

// Win32
HWND g_hFrameWindow;

AppMode_e	g_nAppMode = AppMode_e::MODE_RUNNING;		// Default. Don't use MODE_LOGO, there's no point in having it
static bool g_bSysClkOK = false;

bool      g_bRestart = false;

DWORD		g_dwSpeed = SPEED_NORMAL;	// Affected by Config dialog's speed slider bar
double		g_fCurrentCLK6502 = CLK_6502_NTSC;	// Affected by Config dialog's speed slider bar
static double g_fMHz = 1.0;			// Affected by Config dialog's speed slider bar

int			g_nCpuCyclesFeedback = 0;
DWORD       g_dwCyclesThisFrame = 0;

bool		g_bDisableDirectSound = false;
bool		g_bDisableDirectSoundMockingboard = false;
int			g_nMemoryClearType = MIP_FF_FF_00_00; // Note: -1 = random MIP in Memory.cpp MemReset()

SynchronousEventManager g_SynchronousEventMgr;

//---------------------------------------------------------------------------

eApple2Type GetApple2Type(void)
{
	return g_Apple2Type;
}

void SetApple2Type(eApple2Type type)
{
	g_Apple2Type = type;
	SetMainCpuDefault(type);
}

CardManager& GetCardMgr(void)
{
	static CardManager g_CardMgr;	// singleton
	return g_CardMgr;
}

static void ResetToLogoMode(void)
{
	g_nAppMode = AppMode_e::MODE_LOGO;
}

//---------------------------------------------------------------------------

static bool g_bPriorityNormal = true;

// Make APPLEWIN process higher priority
void SetPriorityAboveNormal(void)
{
	if (!g_bPriorityNormal)
		return;

	if (SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS))
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
		g_bPriorityNormal = false;
	}
}

// Make APPLEWIN process normal priority
void SetPriorityNormal(void)
{
	if (g_bPriorityNormal)
		return;

	if (SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS))
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
		g_bPriorityNormal = true;
	}
}

//---------------------------------------------------------------------------

static UINT g_uModeStepping_Cycles = 0;
static bool g_uModeStepping_LastGetKey_ScrollLock = false;

void ContinueExecution(void)
{
	_ASSERT(g_nAppMode == AppMode_e::MODE_RUNNING);

	const double fUsecPerSec = 1.e6;
	const UINT nExecutionPeriodUsec = 1000;		// 1.0ms
	const double fExecutionPeriodClks = g_fCurrentCLK6502 * ((double)nExecutionPeriodUsec / fUsecPerSec);

	const bool bWasFullSpeed = g_bFullSpeed;
	g_bFullSpeed = (g_dwSpeed == SPEED_MAX);

	if (g_bFullSpeed)
	{
		if (!bWasFullSpeed)
			VideoRedrawScreenDuringFullSpeed(0, true);	// Init for full-speed mode

		// Don't call Spkr_Mute() - will get speaker clicks
		MB_Mute();
		SysClk_StopTimer();
		g_nCpuCyclesFeedback = 0;	// For the case when this is a big -ve number

		// Switch to normal priority so that APPLEWIN process doesn't hog machine!
		//. EG: No disk in Drive-1, and boot Apple: Windows will start to crawl!
		SetPriorityNormal();
	}
	else
	{
		if (bWasFullSpeed)
			VideoRedrawScreenAfterFullSpeed(g_dwCyclesThisFrame);

		// Don't call Spkr_Demute()
		MB_Demute();
		SysClk_StartTimerUsec(nExecutionPeriodUsec);

		// Switch to higher priority, eg. for audio (BUG #015394)
		SetPriorityAboveNormal();
	}

	//

	int nCyclesWithFeedback = (int)fExecutionPeriodClks + g_nCpuCyclesFeedback;
	const UINT uCyclesToExecuteWithFeedback = (nCyclesWithFeedback >= 0) ? nCyclesWithFeedback
		: 0;

	const DWORD uCyclesToExecute = (g_nAppMode == AppMode_e::MODE_RUNNING) ? uCyclesToExecuteWithFeedback
		/* AppMode_e::MODE_STEPPING */ : 0;

	const bool bVideoUpdate = !g_bFullSpeed;
	const DWORD uActualCyclesExecuted = CpuExecute(uCyclesToExecute, bVideoUpdate);
	g_dwCyclesThisFrame += uActualCyclesExecuted;

	JoyUpdateButtonLatch(nExecutionPeriodUsec);	// Button latch time is independent of CPU clock frequency
	MB_PeriodicUpdate(uActualCyclesExecuted);

	//

	DWORD uSpkrActualCyclesExecuted = uActualCyclesExecuted;

	if (g_nAppMode == AppMode_e::MODE_RUNNING)
		SpkrUpdate(uSpkrActualCyclesExecuted);

	//

	const UINT dwClksPerFrame = NTSC_GetCyclesPerFrame();
	if (g_dwCyclesThisFrame >= dwClksPerFrame && !VideoGetVblBarEx(g_dwCyclesThisFrame))
	{
		g_dwCyclesThisFrame -= dwClksPerFrame;

		if (g_bFullSpeed)
			VideoRedrawScreenDuringFullSpeed(g_dwCyclesThisFrame);
		else
			VideoRefreshScreen(); // Just copy the output of our Apple framebuffer to the system Back Buffer
	}

	if (g_nAppMode == AppMode_e::MODE_RUNNING && !g_bFullSpeed)
	{
		SysClk_WaitTimer();
	}
}

void SingleStep(bool bReinit)
{
	if (bReinit)
	{
		g_uModeStepping_Cycles = 0;
		g_uModeStepping_LastGetKey_ScrollLock = false;
	}

	ContinueExecution();
}

//===========================================================================

double Get6502BaseClock(void)
{
	return (GetVideoRefreshRate() == VR_50HZ) ? CLK_6502_PAL : CLK_6502_NTSC;
}

void UseClockMultiplier(double clockMultiplier)
{
	if (clockMultiplier == 0.0)
		return;

	if (clockMultiplier < 1.0)
	{
		if (clockMultiplier < 0.5)
			clockMultiplier = 0.5;
		g_dwSpeed = (ULONG)((clockMultiplier - 0.5) * 20);	// [0.5..0.9] -> [0..9]
	}
	else
	{
		g_dwSpeed = (ULONG)(clockMultiplier * 10);
		if (g_dwSpeed >= SPEED_MAX)
			g_dwSpeed = SPEED_MAX - 1;
	}

	SetCurrentCLK6502();
}

void SetCurrentCLK6502(void)
{
	static DWORD dwPrevSpeed = (DWORD)-1;
	static VideoRefreshRate_e prevVideoRefreshRate = VR_NONE;

	if (dwPrevSpeed == g_dwSpeed && GetVideoRefreshRate() == prevVideoRefreshRate)
		return;

	dwPrevSpeed = g_dwSpeed;
	prevVideoRefreshRate = GetVideoRefreshRate();

	// SPEED_MIN    =  0 = 0.50 MHz
	// SPEED_NORMAL = 10 = 1.00 MHz
	//                20 = 2.00 MHz
	// SPEED_MAX-1  = 39 = 3.90 MHz
	// SPEED_MAX    = 40 = ???? MHz (run full-speed, /g_fCurrentCLK6502/ is ignored)

	if (g_dwSpeed < SPEED_NORMAL)
		g_fMHz = 0.5 + (double)g_dwSpeed * 0.05;
	else
		g_fMHz = (double)g_dwSpeed / 10.0;

	g_fCurrentCLK6502 = Get6502BaseClock() * g_fMHz;

	//
	// Now re-init modules that are dependent on /g_fCurrentCLK6502/
	//

	SpkrReinitialize();
	MB_Reinitialize();
}

//===========================================================================


UINT GetFrameBufferBorderlessWidth()
{
	static const UINT uFrameBufferBorderlessW = 560;	// 560 = Double Hi-Res
	return uFrameBufferBorderlessW;
}

UINT GetFrameBufferBorderlessHeight()
{
	static const UINT uFrameBufferBorderlessH = 384;	// 384 = Double Scan Line
	return uFrameBufferBorderlessH;
}

// NB. These border areas are not visible (... and these border areas are unrelated to the 3D border below)
UINT GetFrameBufferBorderWidth()
{
	static const UINT uBorderW = 20;
	return uBorderW;
}

UINT GetFrameBufferBorderHeight()
{
	static const UINT uBorderH = 18;
	return uBorderH;
}

UINT GetFrameBufferWidth()
{
	return GetFrameBufferBorderlessWidth() + 2 * GetFrameBufferBorderWidth();
}

UINT GetFrameBufferHeight()
{
	return GetFrameBufferBorderlessHeight() + 2 * GetFrameBufferBorderHeight();
}

//===========================================================================

static bool DoHardDiskInsert(const int nDrive, LPCWSTR szPathName)
{
	if (szPathName[0] == '\0')
	{
		HD_Unplug(nDrive);
		return true;
	}

	BOOL bRes = HD_Insert(nDrive, szPathName);
	return bRes;
}

//===========================================================================

// This method should be called from within the main message loop

void EmulatorMessageLoopProcessing()
{
	if (g_nAppMode == AppMode_e::MODE_RUNNING)
	{
		ContinueExecution();
	}
	if (g_nAppMode == AppMode_e::MODE_PAUSED)
	{
		g_RemoteControlMgr.sendOutput(NULL, NULL);
		Sleep(1);		// Stop process hogging CPU - 1ms, as need to fade-out speaker sound buffer
	}
	else if (g_nAppMode == AppMode_e::MODE_LOGO)
		Sleep(1);		// Stop process hogging CPU (NB. don't delay for too long otherwise key input can be slow in other apps - GH#569)
}


// DO ONE-TIME INITIALIZATION
void EmulatorOneTimeInitialization(HWND window)
{
	g_hFrameWindow = window;
	DSInit(g_hFrameWindow);
	g_bSysClkOK = SysClk_InitTimer();
	MB_Initialize();
	SpkrInitialize();
	SetApple2Type(A2TYPE_APPLE2EENHANCED);
	GetCardMgr().Insert(SLOT4, CT_MockingboardC);
	GetCardMgr().Insert(SLOT5, CT_MockingboardC);
	HD_SetEnabled(true);

	// TODO: load these as defaults or override from config file
	RGB_SetVideocard(Video7_SL7, 15, 0);
	SetVideoType(VT_COLOR_IDEALIZED);
	SetVideoStyle(VS_NONE);
	SetVideoRefreshRate(VR_60HZ);
	SetCurrentCLK6502();
	MB_Reset();
	g_RemoteControlMgr.setRemoteControlEnabled(true);
	g_RemoteControlMgr.setTrackOnlyEnabled(false);
}

// DO INITIALIZATION THAT MUST BE REPEATED FOR A RESTART
void EmulatorRepeatInitialization()
{
	SpkrSetVolume(g_nonVolatile.volumeSpeaker, VOLUME_MAX);
	MB_SetVolume(g_nonVolatile.volumeMockingBoard, VOLUME_MAX);

	UseClockMultiplier(1.0f);
	if (!VideoInitialize())
	{
		MessageBox(g_hFrameWindow, L"Fatal Error: Can't initialize Apple video memory!.", L"Alert", MB_ICONASTERISK | MB_OK);
	}

	// Init palette color
	VideoSwitchVideocardPalette(RGB_GetVideocard(), GetVideoType());

	bool bRes = DoHardDiskInsert(HARDDISK_1, g_nonVolatile.hdvPath.c_str());
	if (!bRes)
	{
		MessageBox(g_hFrameWindow, L"Can't find last used Nox Archaist HDV file.\nPlease choose another.", L"Warning", MB_ICONASTERISK | MB_OK);
		return;
	}
	MemInitialize();
	// Now boot the HD!
	SoundCore_TweakVolumes();
	VideoRedrawScreen();
}

void EmulatorReboot()
{
	g_nAppMode = AppMode_e::MODE_RUNNING;
	HD_Reset();
	g_bFullSpeed = 0;	// Might've hit reset in middle of InternalCpuExecute() - so beep may get (partially) muted
	MemReset();	// calls CpuInitialize(), CNoSlotClock.Reset()
	VideoResetState();
	KeybReset();
	MB_Reset();
	SpkrReset();
	SetActiveCpu(GetMainCpu());
	SoundCore_SetFade(FADE_NONE);
}
