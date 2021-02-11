#pragma once
#include <string>

static INT_PTR CALLBACK LogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam);

class LogWindow
{
public:
	LogWindow::LogWindow(HINSTANCE app, HWND hMainWindow);

	void LoadFromFile();
	void SaveToFile();
	void ShowLogWindow();
	void HideLogWindow();
	bool IsLogWindowDisplayed();
	void ClearLog();
	void AppendLog(std::wstring str);

	HWND hwndLog;				// handle to log window
};

// defined in Main.cpp
extern std::shared_ptr<LogWindow> GetLogWindow();