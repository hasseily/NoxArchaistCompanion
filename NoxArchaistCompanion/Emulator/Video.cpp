/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2010, Tom Charlesworth, Michael Pohoreski, Nick Westgate

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

/* Description: Emulation of video modes
 *
 * Author: Various
 */

#include "pch.h"

#include "Video.h"
#include "AppleWin.h"
#include "CPU.h"
#include "Frame.h"
#include "Keyboard.h"
#include "Log.h"
#include "Memory.h"
#include "NTSC.h"
#include "RGBMonitor.h"

#include "resource.h"
#include "RemoteControl/RemoteControlManager.h"	// RIK

	#define  SW_80COL         (g_uVideoMode & VF_80COL)
	#define  SW_DHIRES        (g_uVideoMode & VF_DHIRES)
	#define  SW_HIRES         (g_uVideoMode & VF_HIRES)
	#define  SW_80STORE       (g_uVideoMode & VF_80STORE)
	#define  SW_MIXED         (g_uVideoMode & VF_MIXED)
	#define  SW_PAGE2         (g_uVideoMode & VF_PAGE2)
	#define  SW_TEXT          (g_uVideoMode & VF_TEXT)

// Globals (Public)

    uint8_t      *g_pFramebufferbits = NULL; // last drawn frame
	int           g_nAltCharSetOffset  = 0; // alternate character set

// Globals (Private)

// video scanner constants
int const kHBurstClock      =    53; // clock when Color Burst starts
int const kHBurstClocks     =     4; // clocks per Color Burst duration
int const kHClock0State     =  0x18; // H[543210] = 011000
int const kHClocks          =    65; // clocks per horizontal scan (including HBL)
int const kHPEClock         =    40; // clock when HPE (horizontal preset enable) goes low
int const kHPresetClock     =    41; // clock when H state presets
int const kHSyncClock       =    49; // clock when HSync starts
int const kHSyncClocks      =     4; // clocks per HSync duration
int const kNTSCScanLines    =   262; // total scan lines including VBL (NTSC)
int const kNTSCVSyncLine    =   224; // line when VSync starts (NTSC)
int const kPALScanLines     =   312; // total scan lines including VBL (PAL)
int const kPALVSyncLine     =   264; // line when VSync starts (PAL)
int const kVLine0State      = 0x100; // V[543210CBA] = 100000000
int const kVPresetLine      =   256; // line when V state presets
int const kVSyncLines       =     4; // lines per VSync duration
int const kVDisplayableScanLines = 192; // max displayable scanlines

static COLORREF      customcolors[256];	// MONOCHROME is last custom color

static HBITMAP       g_hDeviceBitmap;
static HDC           g_hDeviceDC;
static LPBITMAPINFO  g_pFramebufferinfo = NULL;

       HBITMAP       g_hLogoBitmap;

COLORREF         g_nMonochromeRGB    = RGB(0xC0,0xC0,0xC0);

uint32_t  g_uVideoMode     = VF_TEXT; // Current Video Mode (this is the last set one as it may change mid-scan line!)

DWORD     g_eVideoType     = VT_DEFAULT;
static VideoStyle_e g_eVideoStyle = VS_HALF_SCANLINES;

static bool g_bHeadless = false;  // Headless Mode

static bool g_bVideoScannerNTSC = true;  // NTSC video scanning (or PAL)

static LPDIRECTDRAW g_lpDD = NULL;

//-------------------------------------

	// NOTE: KEEP IN SYNC: VideoType_e g_aVideoChoices g_apVideoModeDesc
	TCHAR g_aVideoChoices[] =
		TEXT("Monochrome (Custom)\0")
		TEXT("Color (Composite Idealized)\0")		// newly added
		TEXT("Color (RGB Card/Monitor)\0")	// was "Color (RGB Monitor)"
		TEXT("Color (Composite Monitor)\0")	// was "Color (NTSC Monitor)"
		TEXT("Color TV\0")
		TEXT("B&W TV\0")
		TEXT("Monochrome (Amber)\0")
		TEXT("Monochrome (Green)\0")
		TEXT("Monochrome (White)\0")
		;

	// NOTE: KEEP IN SYNC: VideoType_e g_aVideoChoices g_apVideoModeDesc
	// The window title will be set to this.
	const char *g_apVideoModeDesc[ NUM_VIDEO_MODES ] =
	{
		  "Monochrome (Custom)"
		, "Color (Composite Idealized)"
		, "Color (RGB Card/Monitor)"
		, "Color (Composite Monitor)"
		, "Color TV"
		, "B&W TV"
		, "Monochrome (Amber)"
		, "Monochrome (Green)"
		, "Monochrome (White)"
	};

// Prototypes (Private) _____________________________________________


	void videoCreateDIBSection();


//===========================================================================
void VideoInitialize ()
{
	// RESET THE VIDEO MODE SWITCHES AND THE CHARACTER SET OFFSET
	VideoResetState();

	// LOAD THE LOGO
	g_hLogoBitmap = LoadBitmap( g_hInstance, MAKEINTRESOURCE(IDB_APPLEWIN) );

	// CREATE A BITMAPINFO STRUCTURE FOR THE FRAME BUFFER
	g_pFramebufferinfo = (LPBITMAPINFO)VirtualAlloc(
		NULL,
		sizeof(BITMAPINFOHEADER) + 256*sizeof(RGBQUAD),
		MEM_COMMIT,
		PAGE_READWRITE);

	ZeroMemory(g_pFramebufferinfo,sizeof(BITMAPINFOHEADER)+256*sizeof(RGBQUAD));
	g_pFramebufferinfo->bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	g_pFramebufferinfo->bmiHeader.biWidth       = GetFrameBufferWidth();
	g_pFramebufferinfo->bmiHeader.biHeight      = GetFrameBufferHeight();
	g_pFramebufferinfo->bmiHeader.biPlanes      = 1;
	g_pFramebufferinfo->bmiHeader.biBitCount    = 32;
	g_pFramebufferinfo->bmiHeader.biCompression = BI_RGB;
	g_pFramebufferinfo->bmiHeader.biClrUsed     = 0;

	videoCreateDIBSection();
}

//===========================================================================

//
// ----- ALL GLOBALLY ACCESSIBLE FUNCTIONS ARE BELOW THIS LINE -----
//
        
// This is called from PageConfig
//===========================================================================
void VideoChooseMonochromeColor ()
{
	CHOOSECOLOR cc;
	ZeroMemory(&cc,sizeof(CHOOSECOLOR));
	cc.lStructSize     = sizeof(CHOOSECOLOR);
	cc.hwndOwner       = g_hFrameWindow;
	cc.rgbResult       = g_nMonochromeRGB;
	cc.lpCustColors    = customcolors + 1;
	cc.Flags           = CC_RGBINIT | CC_SOLIDCOLOR;
	if (ChooseColor(&cc))
	{
		g_nMonochromeRGB = cc.rgbResult;
		VideoReinitialize();
		if ((g_nAppMode != MODE_LOGO) && (g_nAppMode != MODE_DEBUG))
		{
			VideoRedrawScreen();
		}
		Config_Save_Video();
	}
}

//===========================================================================
void VideoDestroy () {

  // DESTROY BUFFERS
  VirtualFree(g_pFramebufferinfo,0,MEM_RELEASE);
  g_pFramebufferinfo = NULL;

  // DESTROY FRAME BUFFER
  DeleteDC(g_hDeviceDC);
  DeleteObject(g_hDeviceBitmap);
  g_hDeviceDC     = (HDC)0;
  g_hDeviceBitmap = (HBITMAP)0;

  // DESTROY LOGO
  if (g_hLogoBitmap) {
    DeleteObject(g_hLogoBitmap);
    g_hLogoBitmap = (HBITMAP)0;
  }

  NTSC_Destroy();
}

//===========================================================================

static void VideoDrawLogoBitmap(HDC hDstDC, int xoff, int yoff, int srcw, int srch, int scale)
{
	HDC hSrcDC = CreateCompatibleDC( hDstDC );
	SelectObject( hSrcDC, g_hLogoBitmap );
	StretchBlt(
		hDstDC,   // hdcDest
		xoff, yoff,  // nXDest, nYDest
		scale * srcw, scale * srch, // nWidth, nHeight
		hSrcDC,   // hdcSrc
		0, 0,     // nXSrc, nYSrc
		srcw, srch,
		SRCCOPY   // dwRop
	);

	DeleteObject( hSrcDC );
}

//===========================================================================
void VideoDisplayLogo () 
{
	int nLogoX = 0, nLogoY = 0;
	int scale = GetViewportScale();

	HDC hFrameDC = FrameGetDC();

	// DRAW THE LOGO
	SelectObject(hFrameDC, GetStockObject(NULL_PEN));

	if (g_hLogoBitmap)
	{
		BITMAP bm;
		if (GetObject(g_hLogoBitmap, sizeof(bm), &bm))
		{
			nLogoX = (g_nViewportCX - scale*bm.bmWidth )/2;
			nLogoY = (g_nViewportCY - scale*bm.bmHeight)/2;

			if( IsFullScreen() )
			{
				nLogoX += GetFullScreenOffsetX();
				nLogoY += GetFullScreenOffsetY();
			}

			VideoDrawLogoBitmap( hFrameDC, nLogoX, nLogoY, bm.bmWidth, bm.bmHeight, scale );
		}
	}

	// DRAW THE VERSION NUMBER
	TCHAR sFontName[] = TEXT("Arial");
	HFONT font = CreateFont(-20,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,
							OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,
							VARIABLE_PITCH | 4 | FF_SWISS,
							sFontName );
	SelectObject(hFrameDC,font);
	SetTextAlign(hFrameDC,TA_RIGHT | TA_TOP);
	SetBkMode(hFrameDC,TRANSPARENT);

	TCHAR szVersion[ 64 ];
	StringCbPrintf(szVersion, 64, "Version %s - Nox Archaist 1.4", VERSIONSTRING);
	int xoff = GetFullScreenOffsetX(), yoff = GetFullScreenOffsetY();

#define  DRAWVERSION(x,y,c)                 \
	SetTextColor(hFrameDC,c);               \
	TextOut(hFrameDC,                       \
		scale*540+x+xoff,scale*358+y+yoff,  \
		szVersion,                          \
		strlen(szVersion));

	if (GetDeviceCaps(hFrameDC,PLANES) * GetDeviceCaps(hFrameDC,BITSPIXEL) <= 4) {
		DRAWVERSION( 2, 2,RGB(0x00,0x00,0x00));
		DRAWVERSION( 1, 1,RGB(0x00,0x00,0x00));
		DRAWVERSION( 0, 0,RGB(0xFF,0x00,0xFF));
	} else {
		DRAWVERSION( 1, 1,PALETTERGB(0x30,0x30,0x70));
		DRAWVERSION(-1,-1,PALETTERGB(0xC0,0x70,0xE0));
		DRAWVERSION( 0, 0,PALETTERGB(0x70,0x30,0xE0));
	}

#if _DEBUG
	StringCbPrintf(szVersion, 64, "DEBUG");
	DRAWVERSION( 2, -358*scale,RGB(0x00,0x00,0x00));
	DRAWVERSION( 1, -357*scale,RGB(0x00,0x00,0x00));
	DRAWVERSION( 0, -356*scale,RGB(0xFF,0x00,0xFF));
#endif

#undef  DRAWVERSION

	DeleteObject(font);
}

//===========================================================================

void VideoRedrawScreenDuringFullSpeed(DWORD dwCyclesThisFrame, bool bInit /*=false*/)
{
	static DWORD dwFullSpeedStartTime = 0;
//	static bool bValid = false;

	if (bInit)
	{
		// Just entered full-speed mode
//		bValid = false;
		dwFullSpeedStartTime = GetTickCount();
		return;
	}

	DWORD dwFullSpeedDuration = GetTickCount() - dwFullSpeedStartTime;
	if (dwFullSpeedDuration <= 16)	// Only update after every realtime ~17ms of *continuous* full-speed
		return;

	dwFullSpeedStartTime += dwFullSpeedDuration;

	//

#if 0
	static BYTE text_main[1024*2] = {0};	// page1 & 2
	static BYTE text_aux[1024*2] = {0};		// page1 & 2
	static BYTE hgr_main[8192*2] = {0};		// page1 & 2
	static BYTE hgr_aux[8192*2] = {0};		// page1 & 2

	bool bRedraw = true;	// Always redraw for bValid==false (ie. just entered full-speed mode)

	if (bValid)
	{
		if ((g_uVideoMode&(VF_DHIRES|VF_HIRES|VF_TEXT|VF_MIXED)) == VF_HIRES)
		{
			// HIRES (not MIXED) - eg. AZTEC.DSK
			if ((g_uVideoMode&VF_PAGE2) == 0)
				bRedraw = memcmp(&hgr_main[0x0000],  MemGetMainPtr(0x2000), 8192) != 0;
			else
				bRedraw = memcmp(&hgr_main[0x2000],  MemGetMainPtr(0x4000), 8192) != 0;
		}
		else
		{
			bRedraw =
				(memcmp(text_main, MemGetMainPtr(0x400),  sizeof(text_main)) != 0) ||
				(memcmp(text_aux,  MemGetAuxPtr(0x400),   sizeof(text_aux))  != 0) ||
				(memcmp(hgr_main,  MemGetMainPtr(0x2000), sizeof(hgr_main))  != 0) ||
				(memcmp(hgr_aux,   MemGetAuxPtr(0x2000),  sizeof(hgr_aux))   != 0);
		}
	}

	if (bRedraw)
		VideoRedrawScreenAfterFullSpeed(dwCyclesThisFrame);

	// Copy all video memory (+ screen holes)
	memcpy(text_main, MemGetMainPtr(0x400),  sizeof(text_main));
	memcpy(text_aux,  MemGetAuxPtr(0x400),   sizeof(text_aux));
	memcpy(hgr_main,  MemGetMainPtr(0x2000), sizeof(hgr_main));
	memcpy(hgr_aux,   MemGetAuxPtr(0x2000),  sizeof(hgr_aux));

	bValid = true;
#else
	VideoRedrawScreenAfterFullSpeed(dwCyclesThisFrame);
#endif
}

//===========================================================================

void VideoRedrawScreenAfterFullSpeed(DWORD dwCyclesThisFrame)
{
	NTSC_VideoClockResync(dwCyclesThisFrame);
	VideoRedrawScreen();	// Better (no flicker) than using: NTSC_VideoReinitialize() or VideoReinitialize()
}

//===========================================================================

void VideoRedrawScreen (void)
{
	// NB. Can't rely on g_uVideoMode being non-zero (ie. so it can double up as a flag) since 'GR,PAGE1,non-mixed' mode == 0x00.
	VideoRefreshScreen( g_uVideoMode, true );
}

void VideoRefreshScreen(uint32_t uRedrawWholeScreenVideoMode /* =0*/, bool bRedrawWholeScreen /* =false*/)
{
	g_RemoteControlMgr.getInput();	// RIK

	if (bRedrawWholeScreen || g_nAppMode == MODE_PAUSED)
	{
		// uVideoModeForWholeScreen set if:
		// . MODE_DEBUG   : always
		// . MODE_RUNNING : called from VideoRedrawScreen(), eg. during full-speed
		if (bRedrawWholeScreen)
			NTSC_SetVideoMode(uRedrawWholeScreenVideoMode);
		NTSC_VideoRedrawWholeScreen();

		// MODE_DEBUG|PAUSED: Need to refresh a 2nd time if changing video-type, otherwise could have residue from prev image!
		// . eg. Amber -> B&W TV
		if (g_nAppMode == MODE_DEBUG || g_nAppMode == MODE_PAUSED)
			NTSC_VideoRedrawWholeScreen();
	}
	g_RemoteControlMgr.sendOutput(g_pFramebufferinfo, g_pFramebufferbits);	// RIK
	if (g_bHeadless)	// Oooh headless mode, we don't write video to the frame
		return;
	HDC hFrameDC = FrameGetDC();

	if (hFrameDC)
	{
		int xSrc = GetFrameBufferBorderWidth();
		int ySrc = GetFrameBufferBorderHeight();

		int xdest = IsFullScreen() ? GetFullScreenOffsetX() : 0;
		int ydest = IsFullScreen() ? GetFullScreenOffsetY() : 0;
		int wdest = g_nViewportCX;
		int hdest = g_nViewportCY;

		SetStretchBltMode(hFrameDC, COLORONCOLOR);
		StretchBlt(
			hFrameDC,
			xdest, ydest,
			wdest, hdest,
			g_hDeviceDC,
			xSrc, ySrc,
			GetFrameBufferBorderlessWidth(), GetFrameBufferBorderlessHeight(),
			SRCCOPY);
	}


#ifdef NO_DIRECT_X
#else
	//if (g_lpDD) g_lpDD->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, NULL);
#endif // NO_DIRECT_X

	GdiFlush();
}

//===========================================================================
void VideoReinitialize (bool bInitVideoScannerAddress /*= true*/)
{
	NTSC_VideoReinitialize( g_dwCyclesThisFrame, bInitVideoScannerAddress );
	NTSC_VideoInitAppleType();
	NTSC_SetVideoStyle();
	NTSC_SetVideoTextMode( g_uVideoMode &  VF_80COL ? 80 : 40 );
	NTSC_SetVideoMode( g_uVideoMode );	// Pre-condition: g_nVideoClockHorz (derived from g_dwCyclesThisFrame)
	VideoSwitchVideocardPalette(RGB_GetVideocard(), GetVideoType());
}

//===========================================================================
void VideoResetState ()
{
	g_nAltCharSetOffset    = 0;
	g_uVideoMode           = VF_TEXT;

	NTSC_SetVideoTextMode( 40 );
	NTSC_SetVideoMode( g_uVideoMode );

	RGB_ResetState();
}

//===========================================================================

BYTE VideoSetMode(WORD, WORD address, BYTE write, BYTE, ULONG uExecutedCycles)
{
	address &= 0xFF;

	const uint32_t oldVideoMode = g_uVideoMode;

	switch (address)
	{
		case 0x00:                 g_uVideoMode &= ~VF_80STORE;                            break;
		case 0x01:                 g_uVideoMode |=  VF_80STORE;                            break;
		case 0x0C: if (!IS_APPLE2){g_uVideoMode &= ~VF_80COL; NTSC_SetVideoTextMode(40);}; break;
		case 0x0D: if (!IS_APPLE2){g_uVideoMode |=  VF_80COL; NTSC_SetVideoTextMode(80);}; break;
		case 0x0E: if (!IS_APPLE2) g_nAltCharSetOffset = 0;           break;	// Alternate char set off
		case 0x0F: if (!IS_APPLE2) g_nAltCharSetOffset = 256;         break;	// Alternate char set on
		case 0x50: g_uVideoMode &= ~VF_TEXT;    break;
		case 0x51: g_uVideoMode |=  VF_TEXT;    break;
		case 0x52: g_uVideoMode &= ~VF_MIXED;   break;
		case 0x53: g_uVideoMode |=  VF_MIXED;   break;
		case 0x54: g_uVideoMode &= ~VF_PAGE2;   break;
		case 0x55: g_uVideoMode |=  VF_PAGE2;   break;
		case 0x56: g_uVideoMode &= ~VF_HIRES;   break;
		case 0x57: g_uVideoMode |=  VF_HIRES;   break;
		case 0x5E: if (!IS_APPLE2) g_uVideoMode |=  VF_DHIRES;  break;
		case 0x5F: if (!IS_APPLE2) g_uVideoMode &= ~VF_DHIRES;  break;
	}

	if (!IS_APPLE2)
		RGB_SetVideoMode(address);

	// Only 1-cycle delay for VF_TEXT & VF_MIXED mode changes (GH#656)
	bool delay = false;
	if ((oldVideoMode ^ g_uVideoMode) & (VF_TEXT|VF_MIXED))
		delay = true;

	NTSC_SetVideoMode( g_uVideoMode, delay );

	return MemReadFloatingBus(uExecutedCycles);
}

//===========================================================================

bool VideoGetSW80COL(void)
{
	return SW_80COL ? true : false;
}

bool VideoGetSWDHIRES(void)
{
	return SW_DHIRES ? true : false;
}

bool VideoGetSWHIRES(void)
{
	return SW_HIRES ? true : false;
}

bool VideoGetSW80STORE(void)
{
	return SW_80STORE ? true : false;
}

bool VideoGetSWMIXED(void)
{
	return SW_MIXED ? true : false;
}

bool VideoGetSWPAGE2(void)
{
	return SW_PAGE2 ? true : false;
}

bool VideoGetSWTEXT(void)
{
	return SW_TEXT ? true : false;
}

bool VideoGetSWAltCharSet(void)
{
	return g_nAltCharSetOffset != 0;
}

//===========================================================================
//
// References to Jim Sather's books are given as eg:
// UTAIIe:5-7,P3 (Understanding the Apple IIe, chapter 5, page 7, Paragraph 3)
//
WORD VideoGetScannerAddress(DWORD nCycles, VideoScanner_e videoScannerAddr /*= VS_FullAddr*/)
{
    // machine state switches
    //
    bool bHires   = VideoGetSWHIRES() && !VideoGetSWTEXT();
    bool bPage2   = VideoGetSWPAGE2();
    bool b80Store = VideoGetSW80STORE();

    // calculate video parameters according to display standard
    //
    const int kScanLines  = g_bVideoScannerNTSC ? kNTSCScanLines : kPALScanLines;
    const int kScanCycles = kScanLines * kHClocks;
    _ASSERT(nCycles < (UINT)kScanCycles);
    nCycles %= kScanCycles;

    // calculate horizontal scanning state
    //
    int nHClock = (nCycles + kHPEClock) % kHClocks; // which horizontal scanning clock
    int nHState = kHClock0State + nHClock; // H state bits
    if (nHClock >= kHPresetClock) // check for horizontal preset
    {
        nHState -= 1; // correct for state preset (two 0 states)
    }
    int h_0 = (nHState >> 0) & 1; // get horizontal state bits
    int h_1 = (nHState >> 1) & 1;
    int h_2 = (nHState >> 2) & 1;
    int h_3 = (nHState >> 3) & 1;
    int h_4 = (nHState >> 4) & 1;
    int h_5 = (nHState >> 5) & 1;

    // calculate vertical scanning state (UTAIIe:3-15,T3.2)
    //
    int nVLine  = nCycles / kHClocks; // which vertical scanning line
    int nVState = kVLine0State + nVLine; // V state bits
    if (nVLine >= kVPresetLine) // check for previous vertical state preset
    {
        nVState -= kScanLines; // compensate for preset
    }
    int v_A = (nVState >> 0) & 1; // get vertical state bits
    int v_B = (nVState >> 1) & 1;
    int v_C = (nVState >> 2) & 1;
    int v_0 = (nVState >> 3) & 1;
    int v_1 = (nVState >> 4) & 1;
    int v_2 = (nVState >> 5) & 1;
    int v_3 = (nVState >> 6) & 1;
    int v_4 = (nVState >> 7) & 1;
    int v_5 = (nVState >> 8) & 1;

    // calculate scanning memory address
    //
    if (bHires && SW_MIXED && v_4 && v_2) // HIRES TIME signal (UTAIIe:5-7,P3)
    {
        bHires = false; // address is in text memory for mixed hires
    }

    int nAddend0 = 0x0D; // 1            1            0            1
    int nAddend1 =              (h_5 << 2) | (h_4 << 1) | (h_3 << 0);
    int nAddend2 = (v_4 << 3) | (v_3 << 2) | (v_4 << 1) | (v_3 << 0);
    int nSum     = (nAddend0 + nAddend1 + nAddend2) & 0x0F; // SUM (UTAIIe:5-9)

    WORD nAddressH = 0; // build address from video scanner equations (UTAIIe:5-8,T5.1)
    nAddressH |= h_0  << 0; // a0
    nAddressH |= h_1  << 1; // a1
    nAddressH |= h_2  << 2; // a2
    nAddressH |= nSum << 3; // a3 - a6

    WORD nAddressV = 0;
    nAddressV |= v_0  << 7; // a7
    nAddressV |= v_1  << 8; // a8
    nAddressV |= v_2  << 9; // a9

    int p2a = !(bPage2 && !b80Store) ? 1 : 0;
    int p2b =  (bPage2 && !b80Store) ? 1 : 0;

    WORD nAddressP = 0;	// Page bits
    if (bHires) // hires?
    {
        // Y: insert hires-only address bits
        //
        nAddressV |= v_A << 10; // a10
        nAddressV |= v_B << 11; // a11
        nAddressV |= v_C << 12; // a12
        nAddressP |= p2a << 13; // a13
        nAddressP |= p2b << 14; // a14
    }
    else
    {
        // N: insert text-only address bits
        //
        nAddressP |= p2a << 10; // a10
        nAddressP |= p2b << 11; // a11
	}

	// VBL' = v_4' | v_3' = (v_4 & v_3)' (UTAIIe:5-10,#3),  (UTAIIe:3-15,T3.2)

	if (videoScannerAddr == VS_PartialAddrH)
		return nAddressH;

	if (videoScannerAddr == VS_PartialAddrV)
		return nAddressV;

    return nAddressP | nAddressV | nAddressH;
}

//===========================================================================

// Called when *outside* of CpuExecute()
bool VideoGetVblBarEx(const DWORD dwCyclesThisFrame)
{
	if (g_bFullSpeed)
	{
		// Ensure that NTSC video-scanner gets updated during full-speed, so video screen can be redrawn during Apple II VBL
		NTSC_VideoClockResync(dwCyclesThisFrame);
	}

	return g_nVideoClockVert < kVDisplayableScanLines;
}

// Called when *inside* CpuExecute()
bool VideoGetVblBar(const DWORD uExecutedCycles)
{
	if (g_bFullSpeed)
	{
		// Ensure that NTSC video-scanner gets updated during full-speed, so video-dependent Apple II code doesn't hang
		NTSC_VideoClockResync(CpuGetCyclesThisVideoFrame(uExecutedCycles));
	}

	return g_nVideoClockVert < kVDisplayableScanLines;
}

//===========================================================================

#define MAX_DRAW_DEVICES 10

static char *draw_devices[MAX_DRAW_DEVICES];
static GUID draw_device_guid[MAX_DRAW_DEVICES];
static int num_draw_devices = 0;

static BOOL CALLBACK DDEnumProc(LPGUID lpGUID, LPCTSTR lpszDesc, LPCTSTR lpszDrvName,  LPVOID lpContext)
{
	int i = num_draw_devices;
	if (i == MAX_DRAW_DEVICES)
		return TRUE;
	if (lpGUID != NULL)
		memcpy(&draw_device_guid[i], lpGUID, sizeof (GUID));
	draw_devices[i] = _strdup(lpszDesc);

	if (g_fh) fprintf(g_fh, "%d: %s - %s\n",i,lpszDesc,lpszDrvName);

	num_draw_devices++;
	return TRUE;
}

bool DDInit(void)
{
#ifdef NO_DIRECT_X

	return false;

#else
	HRESULT hr = DirectDrawEnumerate((LPDDENUMCALLBACK)DDEnumProc, NULL);
	if (FAILED(hr))
	{
		LogFileOutput(L"DSEnumerate failed (%08X)\n", hr);
		return false;
	}

	LogFileOutput(L"Number of draw devices = %d\n", num_draw_devices);

	bool bCreatedOK = false;
	for (int x=0; x<num_draw_devices; x++)
	{
		hr = DirectDrawCreate(&draw_device_guid[x], &g_lpDD, NULL);
		if (SUCCEEDED(hr))
		{
			LogFileOutput(L"DSCreate succeeded for draw device #%d\n", x);
			bCreatedOK = true;
			break;
		}

		LogFileOutput(L"DSCreate failed for draw device #%d (%08X)\n", x, hr);
	}

	if (!bCreatedOK)
	{
		LogFileOutput(L"DSCreate failed for all draw devices\n");
		return false;
	}

	return true;
#endif // NO_DIRECT_X
}

// From SoundCore.h
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=NULL; } }

void DDUninit(void)
{
	SAFE_RELEASE(g_lpDD);
}

#undef SAFE_RELEASE

//===========================================================================

static const UINT kVideoRomSize8K = kVideoRomSize4K*2;
static const UINT kVideoRomSize16K = kVideoRomSize8K*2;
static const UINT kVideoRomSizeMax = kVideoRomSize16K;
static BYTE g_videoRom[kVideoRomSizeMax];
static UINT g_videoRomSize = 0;
static bool g_videoRomRockerSwitch = false;

bool ReadVideoRomFile(const TCHAR* pRomFile)
{
	g_videoRomSize = 0;

	HANDLE h = CreateFile(pRomFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;

	const ULONG size = GetFileSize(h, NULL);
	if (size == kVideoRomSize2K || size == kVideoRomSize4K || size == kVideoRomSize8K || size == kVideoRomSize16K)
	{
		DWORD bytesRead;
		if (ReadFile(h, g_videoRom, size, &bytesRead, NULL) && bytesRead == size)
			g_videoRomSize = size;
	}

	if (g_videoRomSize == kVideoRomSize16K)
	{
		// Use top 8K (assume bottom 8K is all 0xFF's)
		memcpy(&g_videoRom[0], &g_videoRom[kVideoRomSize8K], kVideoRomSize8K);
		g_videoRomSize = kVideoRomSize8K;
	}

	CloseHandle(h);

	return g_videoRomSize != 0;
}

UINT GetVideoRom(const BYTE*& pVideoRom)
{
	pVideoRom = &g_videoRom[0];
	return g_videoRomSize;
}

bool GetVideoRomRockerSwitch(void)
{
	return g_videoRomRockerSwitch;
}

void SetVideoRomRockerSwitch(bool state)
{
	g_videoRomRockerSwitch = state;
}

bool IsVideoRom4K(void)
{
	return g_videoRomSize <= kVideoRomSize4K;
}

//===========================================================================

enum VideoType127_e
{
	  VT127_MONO_CUSTOM
	, VT127_COLOR_MONITOR_NTSC
	, VT127_MONO_TV
	, VT127_COLOR_TV
	, VT127_MONO_AMBER
	, VT127_MONO_GREEN
	, VT127_MONO_WHITE
	, VT127_NUM_VIDEO_MODES
};

void Config_Load_Video()
{
	DWORD dwTmp;

	REGLOAD_DEFAULT(TEXT(REGVALUE_VIDEO_MODE), &dwTmp, (DWORD)VT_DEFAULT);
	g_eVideoType = dwTmp;

	REGLOAD_DEFAULT(TEXT(REGVALUE_VIDEO_STYLE), &dwTmp, (DWORD)VS_HALF_SCANLINES);
	g_eVideoStyle = (VideoStyle_e)dwTmp;

	REGLOAD_DEFAULT(TEXT(REGVALUE_VIDEO_MONO_COLOR), &dwTmp, (DWORD)RGB(0xC0, 0xC0, 0xC0));
	g_nMonochromeRGB = (COLORREF)dwTmp;

	REGLOAD_DEFAULT(TEXT(REGVALUE_VIDEO_REFRESH_RATE), &dwTmp, (DWORD)VR_60HZ);
	SetVideoRefreshRate((VideoRefreshRate_e)dwTmp);

	// RIK START
	REGLOAD_DEFAULT(TEXT(REGVALUE_VIDEO_REMOTECONTROL), &dwTmp, (DWORD)false);
	RemoteControlManager::setRemoteControlEnabled(dwTmp);
	REGLOAD_DEFAULT(TEXT(REGVALUE_VIDEO_RC_TRACKONLY), &dwTmp, (DWORD)false);
	RemoteControlManager::setTrackOnlyEnabled(dwTmp);
	// RIK END


	//

	const UINT16* pOldVersion = GetOldAppleWinVersion();
	if (pOldVersion[0] == 1 && pOldVersion[1] <= 28 && pOldVersion[2] <= 1)
	{
		DWORD dwHalfScanLines;
		REGLOAD_DEFAULT(TEXT(REGVALUE_VIDEO_HALF_SCAN_LINES), &dwHalfScanLines, 0);

		if (dwHalfScanLines)
			g_eVideoStyle = (VideoStyle_e) ((DWORD)g_eVideoStyle | VS_HALF_SCANLINES);
		else
			g_eVideoStyle = (VideoStyle_e) ((DWORD)g_eVideoStyle & ~VS_HALF_SCANLINES);

		REGSAVE(TEXT(REGVALUE_VIDEO_STYLE), g_eVideoStyle);
	}

	//

	if (pOldVersion[0] == 1 && pOldVersion[1] <= 27 && pOldVersion[2] <= 13)
	{
		switch (g_eVideoType)
		{
		case VT127_MONO_CUSTOM:			g_eVideoType = VT_MONO_CUSTOM; break;
		case VT127_COLOR_MONITOR_NTSC:	g_eVideoType = VT_COLOR_MONITOR_NTSC; break;
		case VT127_MONO_TV:				g_eVideoType = VT_MONO_TV; break;
		case VT127_COLOR_TV:			g_eVideoType = VT_COLOR_TV; break;
		case VT127_MONO_AMBER:			g_eVideoType = VT_MONO_AMBER; break;
		case VT127_MONO_GREEN:			g_eVideoType = VT_MONO_GREEN; break;
		case VT127_MONO_WHITE:			g_eVideoType = VT_MONO_WHITE; break;
		default:						g_eVideoType = VT_DEFAULT; break;
		}

		REGSAVE(TEXT(REGVALUE_VIDEO_MODE), g_eVideoType);
	}

	if (g_eVideoType >= NUM_VIDEO_MODES)
		g_eVideoType = VT_DEFAULT;
}

void Config_Save_Video()
{
	REGSAVE(TEXT(REGVALUE_VIDEO_MODE)      ,g_eVideoType);
	REGSAVE(TEXT(REGVALUE_VIDEO_STYLE)     ,g_eVideoStyle);
	REGSAVE(TEXT(REGVALUE_VIDEO_MONO_COLOR),g_nMonochromeRGB);
	REGSAVE(TEXT(REGVALUE_VIDEO_REFRESH_RATE), GetVideoRefreshRate());
	REGSAVE(TEXT(REGVALUE_VIDEO_REMOTECONTROL), RemoteControlManager::isRemoteControlEnabled());		// RIK
	REGSAVE(TEXT(REGVALUE_VIDEO_RC_TRACKONLY), RemoteControlManager::isTrackOnlyEnabled());		// RIK
}

//===========================================================================

VideoType_e GetVideoType(void)
{
	return (VideoType_e) g_eVideoType;
}

// TODO: Can only do this at start-up (mid-emulation requires a more heavy-weight video reinit)
void SetVideoType(VideoType_e newVideoType)
{
	g_eVideoType = newVideoType;
}

VideoStyle_e GetVideoStyle(void)
{
	return g_eVideoStyle;
}

void SetVideoStyle(VideoStyle_e newVideoStyle)
{
	g_eVideoStyle = newVideoStyle;
}

bool IsVideoStyle(VideoStyle_e mask)
{
	return (g_eVideoStyle & mask) != 0;
}

//===========================================================================

bool IsHeadlessMode(void)
{
	return g_bHeadless;
}

void SetHeadlessMode(bool b)
{
	g_bHeadless = b;
}

//===========================================================================

//===========================================================================

VideoRefreshRate_e GetVideoRefreshRate(void)
{
	return (g_bVideoScannerNTSC == false) ? VR_50HZ : VR_60HZ;
}

void SetVideoRefreshRate(VideoRefreshRate_e rate)
{
	if (rate != VR_50HZ)
		rate = VR_60HZ;

	g_bVideoScannerNTSC = (rate == VR_60HZ);
	NTSC_SetRefreshRate(rate);
}

//===========================================================================
static void videoCreateDIBSection()
{
	// CREATE THE DEVICE CONTEXT
	HWND window  = GetDesktopWindow();
	HDC dc       = GetDC(window);
	if (g_hDeviceDC)
	{
		DeleteDC(g_hDeviceDC);
	}
	g_hDeviceDC = CreateCompatibleDC(dc);

	// CREATE THE FRAME BUFFER DIB SECTION
	if (g_hDeviceBitmap)
		DeleteObject(g_hDeviceBitmap);
		g_hDeviceBitmap = CreateDIBSection(
			dc,
			g_pFramebufferinfo,
			DIB_RGB_COLORS,
			(LPVOID *)&g_pFramebufferbits,0,0
		);
	SelectObject(g_hDeviceDC,g_hDeviceBitmap);

	// DRAW THE SOURCE IMAGE INTO THE SOURCE BIT BUFFER
	UINT fbSize = GetFrameBufferWidth() * GetFrameBufferHeight() * sizeof(bgra_t);
	ZeroMemory(g_pFramebufferbits, fbSize);

	// CREATE THE OFFSET TABLE FOR EACH SCAN LINE IN THE FRAME BUFFER
	NTSC_VideoInit( g_pFramebufferbits );
}

//===========================================================================

const char* VideoGetAppWindowTitle(void)
{
	static const char *apVideoMonitorModeDesc[ 2 ] =
	{
		"Color (NTSC Monitor)",
		"Color (PAL Monitor)"
	};

	const VideoType_e videoType = GetVideoType();
	if ( videoType != VT_COLOR_MONITOR_NTSC)
		return g_apVideoModeDesc[ videoType ];
	else
		return apVideoMonitorModeDesc[ GetVideoRefreshRate() == VR_60HZ ? 0 : 1 ];	// NTSC or PAL
}
