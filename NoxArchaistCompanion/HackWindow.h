#pragma once

static INT_PTR CALLBACK HackProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam);

class HackWindow
{
public:
	HackWindow::HackWindow(HINSTANCE app, HWND hMainWindow);

	void ShowHackWindow();
	void HideHackWindow();
	bool IsHackWindowDisplayed();

	HWND hwndHack;				// handle to hack window
private:
	bool isDisplayed = false;
};

// defined in Main.cpp
extern std::shared_ptr<HackWindow> GetHackWindow();