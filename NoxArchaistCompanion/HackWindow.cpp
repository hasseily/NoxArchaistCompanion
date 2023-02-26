#include "pch.h"
#include "HackWindow.h"
#include "resource.h"
#include "HAUtils.h"

static HINSTANCE appInstance = nullptr;
static HWND hwndMain = nullptr;				// handle to main window

INT_PTR CALLBACK HackProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	std::shared_ptr<HackWindow>haw = GetHackWindow();

	switch (message)
	{
	case WM_INITDIALOG:
	{
		return TRUE;
	}
	case WM_NCDESTROY:
		break;
	case WM_CLOSE:
	{
		haw->HideHackWindow();
		return false;   // don't destroy the window
		break;
	}
	case WM_SIZING:
		break;
	}
	// return DefWindowProc(hwndDlg, message, wParam, lParam);
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

	if (hwndHack != nullptr)
	{
		HideHackWindow();
	}
}

void HackWindow::ShowHackWindow()
{
	HMENU hm = GetMenu(hwndMain);
	SetForegroundWindow(hwndHack);
	ShowWindow(hwndHack, SW_SHOWNORMAL);
	CheckMenuItem(hm, ID_HACKWINDOW_SHOW, MF_BYCOMMAND | MF_CHECKED);
	isDisplayed = true;
}

void HackWindow::HideHackWindow()
{
	HMENU hm = GetMenu(hwndMain);
	ShowWindow(hwndHack, SW_HIDE);
	CheckMenuItem(hm, ID_HACKWINDOW_SHOW, MF_BYCOMMAND | MF_UNCHECKED);
	isDisplayed = false;
}

bool HackWindow::IsHackWindowDisplayed()
{
	return isDisplayed;
}