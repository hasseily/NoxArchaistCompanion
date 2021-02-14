//
// Main.cpp
//

#include "pch.h"
#include "Game.h"
#include "resource.h"
#include <WinUser.h>
#include "SidebarManager.h"
#include "SidebarContent.h"
#include "RemoteControl/GameLink.h"
#include "Keyboard.h"
#include "LogWindow.h"
#include "Emulator/AppleWin.h"
#include "Emulator/SoundCore.h"
#include "Emulator/Keyboard.h"

using namespace DirectX;

#ifdef __clang__
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

#pragma warning(disable : 4061)

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// extra window area compared to client area
// necessary to properly scale using the right aspect ratio
int m_extraWindowWidth = 0;
int m_extraWindowHeight = 0;
// Initial window width and height, so we can't reduce the size further, and
// user can always go back to "original" size
int m_initialWindowWidth = 0;
int m_initialWindowHeight = 0;


namespace
{
	std::unique_ptr<Game> g_game;
	std::shared_ptr<LogWindow> g_logW;
}

void ExitGame() noexcept;

static void ExceptionHandler(LPCSTR pError)
{
	// Need to convert char to wchar for MessageBox
	const size_t cSize = strlen(pError) + 1;
	wchar_t* wc = new wchar_t[cSize];
	mbstowcs(wc, pError, cSize);
	MessageBox(HWND_TOP,
		wc,
		TEXT("Runtime Exception"),
		MB_ICONEXCLAMATION | MB_SETFOREGROUND);
}

std::shared_ptr<LogWindow> GetLogWindow()
{
	return g_logW;
}

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

// Entry point
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	try {
		// Initialize global strings
		LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
		LoadStringW(hInstance, IDC_NOXARCHAISTCOMPANION, szWindowClass, MAX_LOADSTRING);

		if (!XMVerifyCPUSupport())
			return 1;

		Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);
		if (FAILED(initialize))
			return 1;

		g_game = std::make_unique<Game>();

		// Register class and create window
		{
			// Register class
			WNDCLASSEXW wcex = {};
			wcex.cbSize = sizeof(WNDCLASSEXW);
			wcex.style = CS_HREDRAW | CS_VREDRAW;
			wcex.lpfnWndProc = WndProc;
			wcex.hInstance = hInstance;
			wcex.hIcon = LoadIconW(hInstance, L"IDI_ICON");
			wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
			wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_NOXARCHAISTCOMPANION);
			wcex.lpszClassName = L"NoxArchaistCompanionWindowClass";
			wcex.hIconSm = LoadIconW(wcex.hInstance, L"IDI_ICON");
			if (!RegisterClassExW(&wcex))
				return 1;

			hInst = hInstance; // Store instance handle in our global variable

			// Create window
			int w, h;
			SidebarManager::GetBaseSize(w, h);

			RECT rc = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };

			AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
			m_initialWindowWidth = rc.right - rc.left;
			m_initialWindowHeight = rc.bottom - rc.top;

			HWND hwnd = CreateWindowExW(0, L"NoxArchaistCompanionWindowClass", L"Nox Archaist Companion", WS_OVERLAPPEDWINDOW,
				CW_USEDEFAULT, CW_USEDEFAULT, m_initialWindowWidth, m_initialWindowHeight, nullptr, nullptr, hInstance,
				nullptr);
			// TODO: Change to CreateWindowExW(WS_EX_TOPMOST, L"NoxArchaistCompanionWindowClass", L"NoxArchaistCompanion", WS_POPUP,
			// to default to fullscreen.

			if (!hwnd)
				return 1;

			ShowWindow(hwnd, nCmdShow);
			// TODO: Change nCmdShow to SW_SHOWMAXIMIZED to default to fullscreen.

			// create the log window at the start
			g_logW = std::make_unique<LogWindow>(hInst, hwnd);

			SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g_game.get()));

			WINDOWINFO wi;
			wi.cbSize = sizeof(WINDOWINFO);
			GetWindowInfo(hwnd, &wi);
			m_extraWindowWidth = (wi.rcWindow.right - wi.rcClient.right) + (wi.rcClient.left - wi.rcWindow.left);
			m_extraWindowHeight = (wi.rcWindow.bottom - wi.rcClient.bottom) + (wi.rcClient.top - wi.rcWindow.top);


			g_game->Initialize(hwnd, wi.rcClient.right - wi.rcClient.left, wi.rcClient.bottom - wi.rcClient.top);
		}

		// Main message loop
		MSG msg = {};
		while (WM_QUIT != msg.message)
		{
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				g_game->Tick();
			}
		}

		g_game.reset();

		return static_cast<int>(msg.wParam);
	}
	catch (std::runtime_error exception)
	{
		ExceptionHandler(exception.what());
	}
	catch (std::exception exception)
	{
		ExceptionHandler(exception.what());
	}

}

// Windows procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static bool s_in_sizemove = false;
	static bool s_in_suspend = false;
	static bool s_minimized = false;
	static bool s_fullscreen = false;
	// TODO: Set s_fullscreen to true if defaulting to fullscreen.

	auto game = reinterpret_cast<Game*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (message)
	{
	case WM_PAINT:
		if (game)
		{
			// redraw while the window is being resized or get artifacts
			game->Tick();
		}
		else
		{
			PAINTSTRUCT ps;
			(void)BeginPaint(hWnd, &ps);
			// Don't bother filling the window background as it'll be all replaced by Direct3D
			EndPaint(hWnd, &ps);
		}
		break;

	case WM_MOVE:
		// Gives UPPER LEFT CORNER of client Rect
		if (game)
		{
			game->OnWindowMoved(LOWORD(lParam), HIWORD(lParam));
		}
		break;

	case WM_SIZE:
		// Gives WIDTH and HEIGHT of client area
		if (wParam == SIZE_MINIMIZED)
		{
			if (!s_minimized)
			{
				s_minimized = true;
				if (!s_in_suspend && game)
					game->OnSuspending();
				s_in_suspend = true;
			}
		}
		else if (s_minimized)
		{
			s_minimized = false;
			if (s_in_suspend && game)
				game->OnResuming();
			s_in_suspend = false;
		}
		else if (game)
		{
			// char buf[500];
			// sprintf_s(buf, "In Main WM_SIZE Width %d, Height %d\n", LOWORD(lParam), HIWORD(lParam));
			// OutputDebugStringA(buf);
			game->OnWindowSizeChanged(LOWORD(lParam), HIWORD(lParam));
		}

		break;

	case WM_SIZING:
		// Force the size to have a fixed aspect ratio
	{
		WINDOWINFO wi;
		wi.cbSize = sizeof(WINDOWINFO);
		GetWindowInfo(hWnd, &wi);
		m_extraWindowWidth = (wi.rcWindow.right - wi.rcClient.right) + (wi.rcClient.left - wi.rcWindow.left);
		m_extraWindowHeight = (wi.rcWindow.bottom - wi.rcClient.bottom) + (wi.rcClient.top - wi.rcWindow.top);
		auto* pWR = (RECT*)lParam;  // Wanted Rect

		if (wi.rcClient.right == pWR->right)
		{
			pWR->right = static_cast<ULONG>(SidebarManager::GetAspectRatio() * (wi.rcClient.bottom - wi.rcClient.top)) + pWR->left + m_extraWindowWidth;
		}
		else
		{
			pWR->bottom = static_cast<ULONG>((wi.rcClient.right - wi.rcClient.left) / SidebarManager::GetAspectRatio()) + pWR->top + m_extraWindowHeight;
		}
		int bw, bh;
		game->GetBaseSize(bw, bh);
		if (((pWR->right - pWR->left) < bw + m_extraWindowWidth) ||
			((pWR->bottom - pWR->top) < bh + m_extraWindowHeight))
		{
			pWR->right = pWR->left + bw + m_extraWindowWidth;
			pWR->bottom = pWR->top + bh + m_extraWindowHeight;
		}
		// char buf[500];
		// sprintf_s(buf, "In Main WM_SIZING Left %d, Top %d\n", pWR->left, pWR->top);
		// OutputDebugStringA(buf);
		break;
	}

	case WM_ENTERSIZEMOVE:
		s_in_sizemove = true;
		break;

	case WM_EXITSIZEMOVE:
		s_in_sizemove = false;
		break;

	case WM_GETMINMAXINFO:
		if (lParam)
		{
			auto info = reinterpret_cast<MINMAXINFO*>(lParam);
			info->ptMinTrackSize.x = m_initialWindowWidth;
			info->ptMinTrackSize.y = m_initialWindowHeight;
		}
		break;

	case WM_ACTIVATEAPP:
		if (game)
		{
			Keyboard::ProcessMessage(message, wParam, lParam);
			Mouse::ProcessMessage(message, wParam, lParam);

			if (wParam)
			{
				game->OnActivated();
			}
			else
			{
				game->OnDeactivated();
			}
		}
		break;

	case WM_POWERBROADCAST:
		switch (wParam)
		{
		case PBT_APMQUERYSUSPEND:
			if (!s_in_suspend && game)
				game->OnSuspending();
			s_in_suspend = true;
			return TRUE;

		case PBT_APMRESUMESUSPEND:
			if (!s_minimized)
			{
				if (s_in_suspend && game)
					game->OnResuming();
				s_in_suspend = false;
			}
			return TRUE;
		}
		break;

	case WM_INPUT:
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEWHEEL:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_MOUSEHOVER:
		Mouse::ProcessMessage(message, wParam, lParam);
		break;
	case WM_CHAR:		// Send to the applewin emulator
		KeybQueueKeypress(wParam, ASCII);
		break;
	case WM_KEYDOWN:	// Send to the applewin emulator
		KeybQueueKeypress(wParam, NOT_ASCII);
		break;
	case WM_KEYUP:
		break;
	case WM_SYSKEYUP:
		Keyboard::ProcessMessage(message, wParam, lParam);
		break;
	case WM_SYSKEYDOWN:
		if (wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000)
		{
			// Implements the classic ALT+ENTER fullscreen toggle
			if (s_fullscreen)
			{
				SetWindowLongPtr(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
				SetWindowLongPtr(hWnd, GWL_EXSTYLE, 0);

				int width = 0;
				int height = 0;
				SidebarManager::GetBaseSize(width, height);

				ShowWindow(hWnd, SW_SHOWNORMAL);

				SetWindowPos(hWnd, HWND_TOP, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
			}
			else
			{
				SetWindowLongPtr(hWnd, GWL_STYLE, 0);
				SetWindowLongPtr(hWnd, GWL_EXSTYLE, WS_EX_TOPMOST);

				SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

				ShowWindow(hWnd, SW_SHOWMAXIMIZED);
			}

			s_fullscreen = !s_fullscreen;
		}
		Keyboard::ProcessMessage(message, wParam, lParam);
		break;

	case WM_MENUCHAR:
		// A menu is active and the user presses a key that does not correspond
		// to any mnemonic or accelerator key. Ignore so we don't produce an error beep.
		return MAKELRESULT(0, MNC_CLOSE);

	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case ID_FILE_ACTIVATEPROFILE:
		{
			if (game)
			{
				game->MenuActivateProfile();
			}
			break;
		}
		case ID_FILE_DEACTIVATEPROFILE:
		{
			if (game)
			{
				game->MenuDeactivateProfile();
			}
			break;
		}
		case ID_EMULATOR_SELECTNOXHDV:
		{

		}
		case ID_EMULATOR_PAUSE:
		{
			switch (g_nAppMode)
			{
			case AppMode_e::MODE_RUNNING:
				g_nAppMode = AppMode_e::MODE_PAUSED;
				SoundCore_SetFade(FADE_OUT);
				break;
			case AppMode_e::MODE_PAUSED:
				g_nAppMode = AppMode_e::MODE_RUNNING;
				SoundCore_SetFade(FADE_IN);
				break;
			}
			break;
		}
		case ID_EMULATOR_RESET:
		{
			switch (g_nAppMode)
			{
				// TODO: There is absolutely no need for a AppMode_e::MODE_LOGO. When the game is launched, if the HDV can't be found then
				//			open the file dialog to select an hdv
				//		Need to code the load/save config (in json since we have it) and store the path to the hdv
			case AppMode_e::MODE_LOGO:
				g_nAppMode = AppMode_e::MODE_RUNNING;
				break;
			case AppMode_e::MODE_RUNNING:
				[[fallthrough]];
			case AppMode_e::MODE_PAUSED:
				if (lParam == 0)
				{
					if (MessageBox(HWND_TOP, TEXT("Are you sure you want to reboot?\nUnsaved progress will be lost!"), TEXT("Reboot Nox Archaist"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SYSTEMMODAL) == IDYES)
						EmulatorReboot();
				}
				else	// gamelink requests will pass 1 here, so no message box
				{
					EmulatorReboot();
				}
				break;
			}
			VideoRedrawScreen();
			break;
		}
		case ID_LOGWINDOW_SHOW:
		{
			if (game)
			{
				game->MenuShowLogWindow(true);
			}
			break;
		}
		case ID_LOGWINDOW_LOAD:
		{
			g_logW->LoadFromFile();
			if (game)
			{
				game->MenuShowLogWindow(true);
			}
			break;
		}
		case ID_LOGWINDOW_SAVE:
		{
			g_logW->SaveToFile();
			break;
		}
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			ExitGame();
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;

	case WM_CLOSE:
	case WM_DESTROY:
		ExitGame();
		return (LRESULT)0;
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);

		break;

	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

// Exit helper
void ExitGame() noexcept
{
	// Display exit confirmation
	if (MessageBox(HWND_TOP, TEXT("Are you sure you want to quit?\nSave your game first!"), TEXT("Quit Nox Archaist"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SYSTEMMODAL) == IDYES)
	{
		GameLink::Term();
		PostQuitMessage(0);
	}
}
