// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/settings.h"

#include "wx/msw/dark_mode.h"

#include "wx/msw/dark_mode/dark_mode.h"
#include "wx/msw/dark_mode/UAHMenuBar.h"

#include <Shlwapi.h>
#include "stdlib.h"

#ifdef __BORLANDC__
#pragma comment(lib, "uxtheme.lib")
#endif

namespace NppDarkMode
{
	bool IsEnabled()
	{
		return g_darkModeEnabled;
	}

	COLORREF InvertLightness(COLORREF c)
	{
		WORD h = 0;
		WORD s = 0;
		WORD l = 0;
		ColorRGBToHLS(c, &h, &l, &s);

		l = 240 - l;

		COLORREF invert_c = ColorHLSToRGB(h, l, s);

		return invert_c;
	}

	COLORREF InvertLightnessSofter(COLORREF c)
	{
		WORD h = 0;
		WORD s = 0;
		WORD l = 0;
		ColorRGBToHLS(c, &h, &l, &s);

		l = std::min(240 - l, 211);
		
		COLORREF invert_c = ColorHLSToRGB(h, l, s);

		return invert_c;
	}

	COLORREF GetBackgroundColor()
	{
		return /*wxSystemSettings::GetAppearance().IsDark()*/g_darkModeSupported ? RGB(0x2B, 0x2B, 0x2B) : wxSystemSettings::GetColour(wxSYS_COLOUR_MENUBAR).GetRGB();
	}

	COLORREF GetSofterBackgroundColor()
	{
		return g_darkModeSupported ? RGB(0x40, 0x40, 0x40) : RGB(0xD9, 0xD9, 0xD9); //RGB(0x78, 0x78, 0x78);
	}

	COLORREF GetTextColor()
	{
		return /*wxSystemSettings::GetAppearance().IsDark()*/g_darkModeSupported ? RGB(0xF0, 0xF0, 0xF0) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT).GetRGB();
	}

	COLORREF GetDarkerTextColor()
	{
		return RGB(0xC0, 0xC0, 0xC0);
	}

	COLORREF GetEdgeColor()
	{
		return RGB(0x80, 0x80, 0x80);
	}

	HBRUSH GetBackgroundBrush()
	{
		static HBRUSH g_hbrBackground = ::CreateSolidBrush(GetBackgroundColor());
		return g_hbrBackground;
	}

	HBRUSH GetSofterBackgroundBrush()
	{
		static HBRUSH g_hbrSofterBackground = ::CreateSolidBrush(GetSofterBackgroundColor());
		return g_hbrSofterBackground;
	}

	// handle events

	bool OnSettingChange(HWND hwnd, LPARAM lParam) // true if dark mode toggled
	{
		bool toggled = false;
		if (IsColorSchemeChangeMessage(lParam))
		{
			bool darkModeWasEnabled = g_darkModeEnabled;
			g_darkModeEnabled = _ShouldAppsUseDarkMode() && !IsHighContrast();

			NppDarkMode::RefreshTitleBarThemeColor(hwnd);

			if (!!darkModeWasEnabled != !!g_darkModeEnabled) {
				toggled = true;
				RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
			}
		}

		return toggled;
	}

	// processes messages related to UAH / custom menubar drawing.
	// return true if handled, false to continue with normal processing in your wndproc
	bool UAHWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* lr)
	{
		if (!IsDarkMode()/*wxSystemSettings::GetAppearance().IsDark()//NppDarkMode::IsEnabled()*/) {
			return false;
		}

		static HTHEME g_menuTheme = nullptr;

		UNREFERENCED_PARAMETER(wParam);
		switch (message)
		{
		case WM_UAHDRAWMENU:
		{
			UAHMENU* pUDM = (UAHMENU*)lParam;
			RECT rc = { 0 };

			// get the menubar rect
			{
				MENUBARINFO mbi = { sizeof(mbi) };
				GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi);

				RECT rcWindow;
				GetWindowRect(hWnd, &rcWindow);

				// the rcBar is offset by the window rect
				rc = mbi.rcBar;
				OffsetRect(&rc, -rcWindow.left, -rcWindow.top);

				rc.top -= 1;
			}

			FillRect(pUDM->hdc, &rc, NppDarkMode::GetBackgroundBrush());

			*lr = 0;

			return true;
		}
		case WM_UAHDRAWMENUITEM:
		{
			UAHDRAWMENUITEM* pUDMI = (UAHDRAWMENUITEM*)lParam;

			// get the menu item string
			wchar_t menuString[256] = { 0 };
			MENUITEMINFO mii = { sizeof(mii), MIIM_STRING };
			{
				mii.dwTypeData = menuString;
				mii.cch = (sizeof(menuString) / 2) - 1;

				GetMenuItemInfo(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);
			}

			// get the item state for drawing

			DWORD dwFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;

			int iTextStateID = MPI_NORMAL;
			int iBackgroundStateID = MPI_NORMAL;
			{
				if ((pUDMI->dis.itemState & ODS_INACTIVE) | (pUDMI->dis.itemState & ODS_DEFAULT)) {
					// normal display
					iTextStateID = MPI_NORMAL;
					iBackgroundStateID = MPI_NORMAL;
				}
				if (pUDMI->dis.itemState & ODS_HOTLIGHT) {
					// hot tracking
					iTextStateID = MPI_HOT;
					iBackgroundStateID = MPI_HOT;
				}
				if (pUDMI->dis.itemState & ODS_SELECTED) {
					// clicked -- MENU_POPUPITEM has no state for this, though MENU_BARITEM does
					iTextStateID = MPI_HOT;
					iBackgroundStateID = MPI_HOT;
				}
				if ((pUDMI->dis.itemState & ODS_GRAYED) || (pUDMI->dis.itemState & ODS_DISABLED)) {
					// disabled / grey text
					iTextStateID = MPI_DISABLED;
					iBackgroundStateID = MPI_DISABLED;
				}
				if (pUDMI->dis.itemState & ODS_NOACCEL) {
					dwFlags |= DT_HIDEPREFIX;
				}
			}

			if (!g_menuTheme) {
				g_menuTheme = OpenThemeData(hWnd, L"Menu");
			}

			if (iBackgroundStateID == MPI_NORMAL || iBackgroundStateID == MPI_DISABLED) {
				FillRect(pUDMI->um.hdc, &pUDMI->dis.rcItem, NppDarkMode::GetBackgroundBrush());
			}
			else if (iBackgroundStateID == MPI_HOT) {
                FillRect(pUDMI->um.hdc, &pUDMI->dis.rcItem, NppDarkMode::GetSofterBackgroundBrush());
            }
            else {
				DrawThemeBackground(g_menuTheme, pUDMI->um.hdc, MENU_POPUPITEM, iBackgroundStateID, &pUDMI->dis.rcItem, nullptr);
			}
			DTTOPTS dttopts = { sizeof(dttopts) };
			if (iTextStateID == MPI_NORMAL || iTextStateID == MPI_HOT) {
				dttopts.dwFlags |= DTT_TEXTCOLOR;
				dttopts.crText = NppDarkMode::GetTextColor();
			}
			DrawThemeTextEx(g_menuTheme, pUDMI->um.hdc, MENU_POPUPITEM, iTextStateID, menuString, mii.cch, dwFlags, &pUDMI->dis.rcItem, &dttopts);

			*lr = 0;

			return true;
		}
		case WM_THEMECHANGED:
		{
			if (g_menuTheme) {
				CloseThemeData(g_menuTheme);
				g_menuTheme = nullptr;
			}
			// continue processing in main wndproc
			return false;
		}
		default:
			return false;
		}
	}

	// from DarkMode.h

	void InitDarkMode()
	{
		::InitDarkMode();
	}

    bool IsDarkMode()
    {
        return g_darkModeSupported;
    }

    void SetDarkMode(bool dark_mode)
	{
        g_darkModeSupported = dark_mode;
	}

    void AllowDarkModeForApp(bool allow)
	{
		::AllowDarkModeForApp(allow);
	}

	bool AllowDarkModeForWindow(HWND hWnd, bool allow)
	{
		return ::AllowDarkModeForWindow(hWnd, allow);
	}

	void RefreshTitleBarThemeColor(HWND hWnd)
	{
		::RefreshTitleBarThemeColor(hWnd);
	}

	void EnableDarkScrollBarForWindowAndChildren(HWND hwnd)
	{
		::EnableDarkScrollBarForWindowAndChildren(hwnd);
	}
}

