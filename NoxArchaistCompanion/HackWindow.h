#pragma once

static INT_PTR CALLBACK HackProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam);

class HackWindow
{
public:
	HackWindow::HackWindow(HINSTANCE app, HWND hMainWindow);

	void ShowHackWindow();
	void HideHackWindow();
	bool IsHackWindowDisplayed();
	void Update();
	void UpdateCustomMemLocValue();

	HWND hwndHack = NULL;				// handle to hack window
	bool isHex = false;					// display in hex
	UINT8 baseRadix = 10;				// base 10 or base 16 (hex)
private:
	bool isDisplayed = false;

	UINT8* GetMemPtrPartyStart();
	UINT8* GetMemPtrCharacter();	// Characters 0-5
};

// defined in Main.cpp
extern std::shared_ptr<HackWindow> GetHackWindow();