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
	void RefreshCustomLocValue();
	UINT8 GetFieldVal(HWND valField);
	void SetMem(int loc, UINT8 val);

	HWND hwndHack = NULL;				// handle to hack window
	bool isHex = false;					// display in hex
	UINT8 baseRadix = 10;				// base 10 or base 16 (hex)
	UINT8* GetMemPtrGenericStart(unsigned int memPos);
	UINT8* GetMemPtrPartyStart();
	UINT8* GetMemPtrCharacter();		// Characters 0-5
private:
	bool isDisplayed = false;

};

// defined in Main.cpp
extern std::shared_ptr<HackWindow> GetHackWindow();