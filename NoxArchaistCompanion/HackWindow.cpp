#include "pch.h"
#include "HackWindow.h"
#include "resource.h"
#include "HAUtils.h"
#include "Emulator/Memory.h"
#include "Emulator/CPU.h"

static HINSTANCE appInstance = nullptr;
static HWND hwndMain = nullptr;				// handle to main window

INT_PTR CALLBACK HackProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	std::shared_ptr<HackWindow>haw = GetHackWindow();

	switch (message)
	{
	case WM_INITDIALOG:
		return true;
		break;
	case WM_NCDESTROY:
		break;
	case WM_CLOSE:
	{
		haw->HideHackWindow();
		return 0;   // don't destroy the window
		break;
	}
	case WM_SIZING:
	{
		RECT rc;
		GetClientRect(hwndDlg, &rc);
		SetWindowPos(hwndDlg, HWND_TOP, rc.left, rc.top, rc.right, rc.bottom, 0);
		break;
	}
	case WM_COMMAND:
	{
		bool wasHandled = false;

		//////////////////////////////////////////////////////////////////////////
		// pulldown was changed
		if (HIWORD(wParam) == CBN_SELCHANGE)
		{
			if (HWND(lParam) == GetDlgItem(hwndDlg, IDC_COMBOMEMBER))	// The character pulldown
			{
				haw->Update();
				wasHandled = true;
			}
		}
		if (wasHandled)
			return true;

		switch (LOWORD(wParam))
		{
		case IDC_MEMLOC:
			// Unique piece for the "any memory byte change"
			// Need to show the existing value
		{
			if (HIWORD(wParam) == EN_CHANGE)
			{
				haw->UpdateCustomMemLocValue();
			}
			wasHandled = true;
			break;
		}
		case IDC_CHECKHEX:
			// Switch from hex to dec and vice versa
			// It should clear the new mem value if it's filled, because
			// we don't know what type it is.
		{
			if (IsDlgButtonChecked(hwndDlg, IDC_CHECKHEX) != haw->isHex)
			{
				HWND f = GetDlgItem(hwndDlg, IDC_MEMNEWVAL);
				SetWindowText(f, L"0");
				haw->Update();
			}
			wasHandled = true;
			break;
		}
		default:
			break;
		};
		if (wasHandled)
			return true;

		//////////////////////////////////////////////////////////////////////////
		// Regular field was modified
		int iConvertedVal = 0;
		wchar_t cExp[6] = L"00000";
		if (HIWORD(wParam) == EN_CHANGE)
		{
			HWND hField = HWND(lParam);
			if (hField == GetDlgItem(hwndDlg, IDC_EXP))		// Experience (16 bits)
			{
				// Experience is unique with its 2 bytes, so take care of it independently
				GetWindowTextW(hField, cExp, 6);
				try {
					iConvertedVal = std::stoi(cExp, nullptr, haw->baseRadix);
					if (iConvertedVal > 0xffff)
					{
						iConvertedVal = 0xffff;
						if (haw->isHex)
							SetWindowText(hField, L"ffff");
						else
							SetWindowText(hField, L"65535");
					}
					HWND b = GetDlgItem(hwndDlg, IDC_BUTTONSETMEMBER);
					EnableWindow(b, true);
					wasHandled = true;
				}
				catch (std::invalid_argument& e) {
					// std::cout << e.what();
					SetWindowText(hField, L"0");
					break;
				}
			}
			if (wasHandled)
				return true;

			// From here on, the field is a single byte field
			// Ensure it doesn't flow over 255
			GetWindowTextW(hField, cExp, 6);
			try {
				iConvertedVal = std::stoi(cExp, nullptr, haw->baseRadix);
				if (iConvertedVal > 0xFF)
				{
					iConvertedVal = 0xFF;
					if (haw->isHex)
						SetWindowText(hField, L"ff");
					else
						SetWindowText(hField, L"255");
				}
			}
			catch (std::invalid_argument& e) {
				// std::cout << e.what();
				SetWindowText(hField, L"0");
				break;
			}
			// Now enable the correct button to save
			HWND b;
			if (hField == GetDlgItem(hwndDlg, IDC_TORCHES) || hField == GetDlgItem(hwndDlg, IDC_PICKS))
				b = GetDlgItem(hwndDlg, IDC_BUTTONSETTOOLS);
			else if (hField == GetDlgItem(hwndDlg, IDC_MEMNEWVAL))
				b = GetDlgItem(hwndDlg, IDC_BUTTONSETMEM);
			else
				b = GetDlgItem(hwndDlg, IDC_BUTTONSETMEMBER);
			EnableWindow(b, true);
			return true;
		}
		break;
	}
	default:
		break;
	};

	return false;
}

/*
///////////////// HackWindow Class ////////////////////////
*/

HackWindow::HackWindow(HINSTANCE app, HWND hMainWindow)
{
	hwndMain = hMainWindow;

	hwndHack = CreateDialog(app,
		MAKEINTRESOURCE(IDD_HACKVIEW),
		hMainWindow,
		(DLGPROC)HackProc);
	HA::AlertIfError(hMainWindow);
}

void HackWindow::ShowHackWindow()
{
	SetForegroundWindow(hwndHack);
	if (isDisplayed)
		return;

	PostMessageW(hwndMain, WM_COMMAND, (WPARAM)ID_EMULATOR_PAUSE, 0);
	ShowWindow(hwndHack, SW_SHOWNORMAL);
	HMENU hm = GetMenu(hwndMain);
	CheckMenuItem(hm, ID_HACKWINDOW_SHOW, MF_BYCOMMAND | MF_CHECKED);
	isDisplayed = true;

	UINT8* memPtr = GetMemPtrPartyStart();

	// Fill member combo box
	HWND chwnd = GetDlgItem(hwndHack, IDC_COMBOMEMBER);
	SendMessage(chwnd, (UINT)CB_RESETCONTENT, (WPARAM)0, (LPARAM)0);	// removes content
	char charName[16];
	std::wstring wcharName(16, L'#');
	for (int k = 0; k < 6; k++)				// get the name of each character
	{
		for (size_t i = 0; i < (sizeof(charName) - 1); i++)
		{
			char c = (char)*(memPtr + 0x4b + i + (k * 0x80));
			if (c == '\0')
			{
				charName[i] = '\0';
				break;
			}
			charName[i] = c - 0x80;	// high ascii
		}
		charName[15] = '\0';
		mbstowcs(&wcharName[0], charName, 16);
		SendMessage(chwnd, (UINT)CB_INSERTSTRING, (WPARAM)k, (LPARAM)wcharName.c_str());
	}
	// Send the CB_SETCURSEL message to display an initial item (as WPARAM index)
	SendMessage(chwnd, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);

	Update();
}

void HackWindow::HideHackWindow()
{
	HMENU hm = GetMenu(hwndMain);
	ShowWindow(hwndHack, SW_HIDE);
	PostMessageW(hwndMain, WM_COMMAND, (WPARAM)ID_EMULATOR_PAUSE, 0);
	CheckMenuItem(hm, ID_HACKWINDOW_SHOW, MF_BYCOMMAND | MF_UNCHECKED);
	isDisplayed = false;
}

bool HackWindow::IsHackWindowDisplayed()
{
	return isDisplayed;
}

void HackWindow::Update()
{
	if (!isDisplayed)
		return;

	UINT8* memPtr = GetMemPtrPartyStart();
	UINT8* charPtr = GetMemPtrCharacter();
	
	isHex = IsDlgButtonChecked(hwndHack, IDC_CHECKHEX);
	baseRadix = (isHex ? 16 : 10);

	wchar_t cMemVal[6] = L"00000";

	_itow_s(charPtr[0x19], cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_MELEE), cMemVal);
	_itow_s(charPtr[0x1c], cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_RANGED), cMemVal);
	_itow_s(charPtr[0x22], cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_CRIT), cMemVal);
	_itow_s(charPtr[0x1f], cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_DODGE), cMemVal);
	_itow_s(charPtr[0x25], cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_LOCKPICK), cMemVal);
	_itow_s(charPtr[0x05] + (charPtr[0x06] * 0x100), cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_EXP), cMemVal);
	_itow_s(*(MemGetBankPtr(0) + 0x420), cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_TORCHES), cMemVal);
	_itow_s(*(MemGetBankPtr(0) + 0x421), cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_PICKS), cMemVal);

	UpdateCustomMemLocValue();

	EnableWindow(GetDlgItem(hwndHack, IDC_BUTTONSETMEMBER), false);
	EnableWindow(GetDlgItem(hwndHack, IDC_BUTTONSETTOOLS), false);
	EnableWindow(GetDlgItem(hwndHack, IDC_BUTTONSETMEM), false);
}

UINT8* HackWindow::GetMemPtrPartyStart()
{
	UINT8* memPtr;
	if (cpuconstants.MEM_PARTY > 0xFFFF)	// Aux Mem
		memPtr = MemGetBankPtr(1) + (cpuconstants.MEM_PARTY - 0x10000);
	else
		memPtr = MemGetBankPtr(0) + (cpuconstants.MEM_PARTY);
	
	return memPtr;
}

UINT8* HackWindow::GetMemPtrCharacter()
{
	HWND chwnd = GetDlgItem(hwndHack, IDC_COMBOMEMBER);
	int pos = SendMessage(chwnd, (UINT)CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
	UINT8* memPtr = GetMemPtrPartyStart();
	return (memPtr + ((long)pos) * 0x80);
}

void HackWindow::UpdateCustomMemLocValue()
{
	HWND hdlMemLoc = GetDlgItem(hwndHack, IDC_MEMLOC);
	HWND hdlMemVal = GetDlgItem(hwndHack, IDC_MEMCURRENTVAL);
	wchar_t cMemLoc[6] = L"00000";
	GetWindowTextW(hdlMemLoc, cMemLoc, 6);
	int iMemLoc = 0;
	try {
		iMemLoc = std::stoi(cMemLoc, nullptr, 16);
	}
	catch (std::invalid_argument& e) {
		// std::cout << e.what();
		SetWindowText(hdlMemVal, L"");
		return;
	}
	wchar_t cMemVal[4] = L"000";
	if (iMemLoc > 0xFFFF)	// if in aux mem
		_itow_s(*(MemGetBankPtr(1) + iMemLoc - 0x10000), cMemVal, baseRadix);
	else
		_itow_s(*(MemGetBankPtr(0) + iMemLoc), cMemVal, baseRadix);
	SetWindowText(hdlMemVal, cMemVal);
	return;
}
