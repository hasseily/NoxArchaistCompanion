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
		//////////////////////////////////////////////////////////////////////////
		// pulldown was changed
		if (HIWORD(wParam) == CBN_SELCHANGE)
		{
			if (HWND(lParam) == GetDlgItem(hwndDlg, IDC_COMBOMEMBER))	// The character pulldown
			{
				haw->Update();
				return true;
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// Manage specific unique UI controls
		switch (LOWORD(wParam))
		{
		case IDC_MEMLOC:
			// Unique piece for the "any memory byte change"
			// Need to show the existing value
		{
			if (HIWORD(wParam) == EN_CHANGE)
			{
				haw->RefreshCustomLocValue();
			}
			return true;
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
			return true;
			break;
		}
		default:
			break;
		};

		//////////////////////////////////////////////////////////////////////////
		// Regular field was modified
		int iConvertedVal = 0;
		wchar_t cFText[6] = L"00000";
		if (HIWORD(wParam) == EN_CHANGE)
		{
			HWND hField = HWND(lParam);
			if (hField == GetDlgItem(hwndDlg, IDC_EXP) ||
				hField == GetDlgItem(hwndDlg, IDC_FOOD) ||
				hField == GetDlgItem(hwndDlg, IDC_GOLD)
				)		// All 16 bits fields
			{
				GetWindowTextW(hField, cFText, 6);
				try {
					iConvertedVal = std::stoi(cFText, nullptr, haw->baseRadix);
					if (iConvertedVal > 0xffff)
					{
						iConvertedVal = 0xffff;
						if (haw->isHex)
							SetWindowText(hField, L"ffff");
						else
							SetWindowText(hField, L"65535");
					}
					goto ENABLE_SAVE_BUTTON;
				}
				catch (std::invalid_argument& e) {
					// std::cout << e.what();
					SetWindowText(hField, L"0");
					break;
				}
			}

			// From here on, the field is a single byte field
			// Ensure it doesn't flow over 255
			GetWindowTextW(hField, cFText, 6);
			try {
				iConvertedVal = std::stoi(cFText, nullptr, haw->baseRadix);
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
ENABLE_SAVE_BUTTON:
			// Now enable the correct button to save
			HWND b;
			if (hField == GetDlgItem(hwndDlg, IDC_TORCHES) ||
				hField == GetDlgItem(hwndDlg, IDC_PICKS) ||
				hField == GetDlgItem(hwndDlg, IDC_FOOD) ||
				hField == GetDlgItem(hwndDlg, IDC_GOLD)
				)
				b = GetDlgItem(hwndDlg, IDC_BUTTONSETTOOLS);
			else if (hField == GetDlgItem(hwndDlg, IDC_MEMNEWVAL))
				b = GetDlgItem(hwndDlg, IDC_BUTTONSETMEM);
			else
				b = GetDlgItem(hwndDlg, IDC_BUTTONSETMEMBER);
			EnableWindow(b, true);
			return true;
		}

		//////////////////////////////////////////////////////////////////////////
		// One of the save buttons was clicked
		if (HIWORD(wParam) == BN_CLICKED)
		{
			if (LOWORD(wParam) == IDC_BUTTONSETMEMBER)
			{
				int iNewVal = 0;
				HWND hField;
				UINT8* charPtr = haw->GetMemPtrCharacter();
				charPtr[0x19] = haw->GetFieldVal(GetDlgItem(hwndDlg, IDC_MELEE));
				charPtr[0x1c] = haw->GetFieldVal(GetDlgItem(hwndDlg, IDC_RANGED));
				charPtr[0x22] = haw->GetFieldVal(GetDlgItem(hwndDlg, IDC_CRIT));
				charPtr[0x1f] = haw->GetFieldVal(GetDlgItem(hwndDlg, IDC_DODGE));
				charPtr[0x25] = haw->GetFieldVal(GetDlgItem(hwndDlg, IDC_LOCKPICK));
				charPtr[0x1] = haw->GetFieldVal(GetDlgItem(hwndDlg, IDC_LEVEL));
				try {
					// exp is 2 bytes, set each one independently
					hField = GetDlgItem(hwndDlg, IDC_EXP);
					GetWindowTextW(hField, cFText, 6);
					iConvertedVal = std::stoi(cFText, nullptr, haw->baseRadix);
					charPtr[0x05] = iConvertedVal & 0xff;	// technically there's no need for the mask, we're getting the first byte
					charPtr[0x06] = iConvertedVal >> 8;
				}
				catch (std::invalid_argument& e) {
					haw->Update();
					return false;
				}
				haw->Update();
				return true;
			}
			else if (LOWORD(wParam) == IDC_BUTTONSETTOOLS)
			{
				auto memPtr = haw->GetMemPtrGenericStart(cpuconstants.MEM_TORCHES);
				memPtr[0] = haw->GetFieldVal(GetDlgItem(hwndDlg, IDC_TORCHES));
				memPtr = haw->GetMemPtrGenericStart(cpuconstants.MEM_PICKS);
				memPtr[0] = haw->GetFieldVal(GetDlgItem(hwndDlg, IDC_PICKS));

				HWND hField;
				try {
					// gold and food are 2 bytes each
					// gold
					memPtr = haw->GetMemPtrGenericStart(cpuconstants.MEM_GOLD);
					hField = GetDlgItem(hwndDlg, IDC_GOLD);
					GetWindowTextW(hField, cFText, 6);
					iConvertedVal = std::stoi(cFText, nullptr, haw->baseRadix);
					memPtr[0] = iConvertedVal & 0xff;
					memPtr[1] = iConvertedVal >> 8;
					// food
					memPtr = haw->GetMemPtrGenericStart(cpuconstants.MEM_FOOD);
					hField = GetDlgItem(hwndDlg, IDC_FOOD);
					GetWindowTextW(hField, cFText, 6);
					iConvertedVal = std::stoi(cFText, nullptr, haw->baseRadix);
					memPtr[0] = iConvertedVal & 0xff;
					memPtr[1] = iConvertedVal >> 8;
				}
				catch (std::invalid_argument& e) {
					haw->Update();
					return false;
				}

				haw->Update();
				return true;
			}
			else if (LOWORD(wParam) == IDC_BUTTONSETMEM)
			{
				HWND hdlMemLoc = GetDlgItem(hwndDlg, IDC_MEMLOC);
				HWND hdlMemValNew = GetDlgItem(hwndDlg, IDC_MEMNEWVAL);
				// Get the memory byte to change
				GetWindowTextW(hdlMemLoc, cFText, 6);
				int iMemLoc = 0;
				try {
					iMemLoc = std::stoi(cFText, nullptr, 16);
				}
				catch (std::invalid_argument& e) {
					// std::cout << e.what();
					SetWindowText(hdlMemValNew, L"");
					break;
				}

				haw->SetMem(iMemLoc, haw->GetFieldVal(hdlMemValNew));
				haw->Update();
				return true;
			}
		}
		break;	// WM_COMMAND
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

	// Show warning if we don't know the version of the game
	ShowWindow(GetDlgItem(hwndHack, IDC_HACK_STATIC_WARN), (cpuconstants.MEM_PARTY == 0));

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
	_itow_s(charPtr[0x1], cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_LEVEL), cMemVal);
	_itow_s(charPtr[0x05] + (charPtr[0x06] * 0x100), cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_EXP), cMemVal);
	_itow_s(GetMemPtrGenericStart(cpuconstants.MEM_FOOD)[0] + (GetMemPtrGenericStart(cpuconstants.MEM_FOOD)[1] * 0x100), cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_FOOD), cMemVal);
	_itow_s(GetMemPtrGenericStart(cpuconstants.MEM_GOLD)[0] + (GetMemPtrGenericStart(cpuconstants.MEM_GOLD)[1] * 0x100), cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_GOLD), cMemVal);
	_itow_s(GetMemPtrGenericStart(cpuconstants.MEM_TORCHES)[0], cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_TORCHES), cMemVal);
	_itow_s(GetMemPtrGenericStart(cpuconstants.MEM_PICKS)[0], cMemVal, baseRadix);
	SetWindowText(GetDlgItem(hwndHack, IDC_PICKS), cMemVal);

	RefreshCustomLocValue();

	EnableWindow(GetDlgItem(hwndHack, IDC_BUTTONSETMEMBER), false);
	EnableWindow(GetDlgItem(hwndHack, IDC_BUTTONSETTOOLS), false);
	EnableWindow(GetDlgItem(hwndHack, IDC_BUTTONSETMEM), false);
}

UINT8* HackWindow::GetMemPtrGenericStart(unsigned int memPos)
{
	UINT8* memPtr;
	if (memPos > 0xFFFF)	// Aux Mem
		memPtr = MemGetBankPtr(1) + (memPos - (unsigned int)0x10000);
	else
		memPtr = MemGetBankPtr(0) + memPos;

	return memPtr;
}

UINT8* HackWindow::GetMemPtrPartyStart()
{
	return GetMemPtrGenericStart(cpuconstants.MEM_PARTY);
}

UINT8* HackWindow::GetMemPtrCharacter()
{
	HWND chwnd = GetDlgItem(hwndHack, IDC_COMBOMEMBER);
	int pos = SendMessage(chwnd, (UINT)CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
	UINT8* memPtr = GetMemPtrPartyStart();
	return (memPtr + ((long)pos) * 0x80);
}

void HackWindow::RefreshCustomLocValue()
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

UINT8 HackWindow::GetFieldVal(HWND valField)
{
	wchar_t cMemVal[4] = L"000";
	GetWindowTextW(valField, cMemVal, 4);
	UINT8 iMemVal = 0;
	try {
		iMemVal = std::stoi(cMemVal, nullptr, baseRadix);
	}
	catch (std::invalid_argument& e) {
		return 0;
	}
	return iMemVal;
}

void HackWindow::SetMem(int loc, UINT8 val)
{
	if (loc > 0xFFFF)
		MemGetBankPtr(1)[loc - 0x10000] = val;
	else
		MemGetBankPtr(0)[loc] = val;
	return;
}
