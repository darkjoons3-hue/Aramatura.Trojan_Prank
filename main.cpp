#pragma execution_character_set("utf-8")
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:wWinMainCRTStartup")

// ============================================================
// КОНФИГ
// ============================================================
namespace Cfg {
    constexpr int kPhase1Ms = 7000;
    constexpr int kPhase2Ms = 10000;
    constexpr int kPhase3Ms = 10000;
    constexpr int kPhase4Ms = 10000;
    constexpr int kPhase5Ms = 10000;
    constexpr int kPhase6Ms = 10000;
    constexpr int kPhase7Ms = 10000;
    constexpr int kFinalMs  = 2000;
    constexpr int kMaxPrankMs = 180000;
}

// ============================================================
// ГЛОБАЛЫ
// ============================================================
std::atomic<bool> g_stop{false};
std::atomic<bool> g_audioStop{false};
std::atomic<int>  g_volume{100};
std::atomic<int>  g_soundMode{0};   // 0 = Quarknova, 1 = Synth Progression

HHOOK     g_kbHook = nullptr;
HINSTANCE g_hInst  = nullptr;

const wchar_t* ERR_CLASS   = L"ArmErr";
const wchar_t* BROWS_CLASS = L"ArmBrowser";

const wchar_t* kErrorTexts[] = {
    L"ты идиот",
    L"арматура",
    L"я арматура",
    L"позвони маме",
    L"эщкере",
    L"что происходит"
};
constexpr int kErrorCount = 6;

struct ErrWin {
    HWND hwnd;
    int  index;
    double angle;
    double radius;
    double speed;
};
std::vector<ErrWin> g_errWins;

struct BrowserWin {
    HWND hwnd;
    int  index;
};
std::vector<BrowserWin> g_browsers;

// ============================================================
// FORWARD
// ============================================================
void PumpMessages();
void SleepPump(int ms);
bool IsElevated();
void RelaunchElevated();
LRESULT CALLBACK LLKeyboardProc(int, WPARAM, LPARAM);
void InstallKbHook();
void RemoveKbHook();
void HideTaskbar();
void ShowTaskbar();
void AudioThread();
LRESULT CALLBACK ErrProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK BrowserProc(HWND, UINT, WPARAM, LPARAM);
void RegisterAllClasses();
void Phase1_FlyingErrors();
void Phase2_Ribbon();
void Phase3_Bend();
void Phase4_Browsers();
void Phase5_GlitchBlocks();
void Phase6_StretchBlocks();
void Phase7_IconSnake();
void DoReboot();
void WatchdogThread();

// ============================================================
// УТИЛИТЫ
// ============================================================
void PumpMessages() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m); DispatchMessageW(&m);
    }
}
void SleepPump(int ms) {
    DWORD s = GetTickCount();
    while ((int)(GetTickCount() - s) < ms && !g_stop.load()) {
        PumpMessages(); Sleep(10);
    }
}

// ============================================================
// ПРАВА
// ============================================================
bool IsElevated() {
    HANDLE t = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return false;
    TOKEN_ELEVATION e = {}; DWORD sz = sizeof(e);
    bool ok = GetTokenInformation(t, TokenElevation, &e, sz, &sz) && e.TokenIsElevated;
    CloseHandle(t);
    return ok;
}
void RelaunchElevated() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow  = SW_SHOW;
    ShellExecuteExW(&sei);
}

// ============================================================
// КЛАВИАТУРА
// ============================================================
LRESULT CALLBACK LLKeyboardProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN ||
                           w == WM_KEYUP   || w == WM_SYSKEYUP)) return 1;
    return CallNextHookEx(g_kbHook, n, w, l);
}
void InstallKbHook() {
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LLKeyboardProc, nullptr, 0);
}
void RemoveKbHook() {
    if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = nullptr; }
}

// ============================================================
// ПАНЕЛЬ ЗАДАЧ
// ============================================================
void HideTaskbar() {
    HWND t = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (t) ShowWindow(t, SW_HIDE);
}
void ShowTaskbar() {
    HWND t = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (t) ShowWindow(t, SW_SHOW);
}

// ============================================================
// BYTEBEAT — ДВЕ ФОРМУЛЫ
// ============================================================

// Формула 1 — из Quarknova (фазы 1-4)
static inline DWORD BytebeatQuarknova(DWORD t) {
    return ((t * (t >> 8 | t >> 13) & 63) +
            (t * (t >> 11 & t >> 15) & 127)) & 0xFF;
}

// Формула 2 — Synth Progression (фазы 5-7, авторская)
static inline DWORD BytebeatSynthProgression(DWORD t) {
    int synth1 = (int)(t * (t >> 8 | t >> 9) & 40 & t >> 6);
    int synth2 = (int)((t * 5 & t >> 7 | t >> 4) & 255);
    int bass   = (int)(t * (t >> 11 & t >> 8) & 128);
    int glitch = (int)(t * (t >> 4 & t >> 8) >> (t >> 16 & 3));
    int prog   = (int)((t >> 15) % 8);

    int result = 0;
    switch (prog) {
        case 0: result = bass; break;
        case 1: result = bass | synth1; break;
        case 2: result = bass | synth1 | (synth2 >> 2); break;
        case 3: result = synth1 ^ (int)(t >> 3); break;
        case 4: result = (bass * 2) | glitch; break;
        case 5: result = (int)((t * 3) * ((t * 2 >> 10 | t >> 7) & 32)); break;
        case 6: result = bass | synth1 | glitch | (synth2 >> 1); break;
        case 7: result = (synth1 & (int)(t >> 8)) >> 1; break;
        default: result = 0; break;
    }
    return (DWORD)(result & 0xFF);
}

// Общая функция — выбирает по режиму
static inline DWORD BytebeatSample(DWORD t) {
    if (g_soundMode.load() == 0)
        return BytebeatQuarknova(t);
    else
        return BytebeatSynthProgression(t);
}

// ============================================================
// AUDIO THREAD
// ============================================================
void AudioThread() {
    const int SR  = 8000;
    const int BUF = 1024;
    const int N   = 4;

    WAVEFORMATEX wf = {};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 1;
    wf.nSamplesPerSec  = SR;
    wf.wBitsPerSample  = 8;
    wf.nBlockAlign     = 1;
    wf.nAvgBytesPerSec = SR;

    HWAVEOUT h = nullptr;
    if (waveOutOpen(&h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;

    std::vector<unsigned char> samp(N * BUF);
    std::vector<WAVEHDR> hdr(N);
    for (int i = 0; i < N; ++i) {
        hdr[i] = {};
        hdr[i].lpData = (LPSTR)&samp[i * BUF];
        hdr[i].dwBufferLength = BUF;
        waveOutPrepareHeader(h, &hdr[i], sizeof(WAVEHDR));
    }

    DWORD t = 0;
    int cb = 0;
    while (!g_audioStop.load()) {
        int guard = 0;
        while (!(hdr[cb].dwFlags & WHDR_DONE)) {
            if (g_audioStop.load()) goto cleanup;
            Sleep(2);
            if (++guard > 500) break;
        }

        int v = g_volume.load();
        unsigned char* buf = (unsigned char*)hdr[cb].lpData;
        for (int i = 0; i < BUF; ++i) {
            DWORD raw = BytebeatSample(t++);
            int s = (int)raw - 128;
            s = (s * v) / 100;
            if (s > 127) s = 127;
            if (s < -128) s = -128;
            buf[i] = (unsigned char)(s + 128);
        }

        hdr[cb].dwFlags &= ~WHDR_DONE;
        hdr[cb].dwBufferLength = BUF;
        waveOutWrite(h, &hdr[cb], sizeof(WAVEHDR));
        cb = (cb + 1) % N;
    }

cleanup:
    waveOutReset(h);
    for (int i = 0; i < N; ++i) waveOutUnprepareHeader(h, &hdr[i], sizeof(WAVEHDR));
    waveOutClose(h);
}

// ============================================================
// ОКНО ОШИБКИ
// ============================================================
LRESULT CALLBACK ErrProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);

        HBRUSH bg = CreateSolidBrush(RGB(255, 255, 200));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        HBRUSH frame = CreateSolidBrush(RGB(0, 0, 0));
        FrameRect(dc, &rc, frame);
        DeleteObject(frame);

        HICON hIcon = LoadIconW(nullptr, IDI_WARNING);
        DrawIconEx(dc, 12, 12, hIcon, 32, 32, 0, nullptr, DI_NORMAL);

        int idx = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (idx < 0 || idx >= kErrorCount) idx = 0;

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));
        HFONT f = CreateFontW(24, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HFONT of = (HFONT)SelectObject(dc, f);
        RECT tr = { 60, 20, rc.right - 10, rc.bottom - 10 };
        DrawTextW(dc, kErrorTexts[idx], -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
        DeleteObject(f);

        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ============================================================
// ОКНО БРАУЗЕРА
// ============================================================
LRESULT CALLBACK BrowserProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);

        HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        HBRUSH top = CreateSolidBrush(RGB(230, 230, 230));
        RECT tr = { 0, 0, rc.right, 40 };
        FillRect(dc, &tr, top);
        DeleteObject(top);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(80, 80, 80));
        HFONT f = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        HFONT of = (HFONT)SelectObject(dc, f);
        TextOutW(dc, 20, 12, L"google.com/search?q=как+налить+воду", 36);

        SetTextColor(dc, RGB(0, 0, 0));
        HFONT fb = CreateFontW(28, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        SelectObject(dc, fb);

        int idx = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
        const wchar_t* q = (idx == 0) ? L"как налить воду" :
                           (idx == 1) ? L"как налить воду в стакан" :
                                        L"зачем наливать воду";
        RECT cr = { 20, 80, rc.right - 20, rc.bottom - 20 };
        DrawTextW(dc, q, -1, &cr, DT_CENTER | DT_TOP | DT_WORDBREAK);

        SelectObject(dc, of);
        DeleteObject(f);
        DeleteObject(fb);

        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void RegisterAllClasses() {
    WNDCLASSW wc = {};
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    wc.lpfnWndProc   = ErrProc;
    wc.lpszClassName = ERR_CLASS;
    RegisterClassW(&wc);

    wc.lpfnWndProc   = BrowserProc;
    wc.lpszClassName = BROWS_CLASS;
    RegisterClassW(&wc);
}

// ============================================================
// ФАЗА 1
// ============================================================
void Phase1_FlyingErrors() {
    g_errWins.clear();

    for (int i = 0; i < 8; ++i) {
        HWND hw = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            ERR_CLASS, L"Ошибка",
            WS_POPUP | WS_BORDER,
            100, 100, 250, 120,
            nullptr, nullptr, g_hInst, nullptr);
        if (!hw) continue;

        SetWindowLongPtrW(hw, GWLP_USERDATA, i % kErrorCount);
        ShowWindow(hw, SW_SHOWNOACTIVATE);

        ErrWin ew;
        ew.hwnd   = hw;
        ew.index  = i % kErrorCount;
        ew.angle  = (double)i * (3.14159 * 2.0 / 8.0);
        ew.radius = 200.0;
        ew.speed  = 0.08 + (rand() % 30) / 1000.0;
        g_errWins.push_back(ew);
    }

    DWORD start = GetTickCount();
    while (!g_stop.load() && (int)(GetTickCount() - start) < Cfg::kPhase1Ms) {
        POINT cur;
        GetCursorPos(&cur);

        for (auto& ew : g_errWins) {
            ew.angle += ew.speed;
            int x = cur.x + (int)(cos(ew.angle) * ew.radius) - 125;
            int y = cur.y + (int)(sin(ew.angle) * ew.radius) - 60;
            SetWindowPos(ew.hwnd, HWND_TOPMOST, x, y, 250, 120, SWP_NOACTIVATE);
        }

        Sleep(20);
    }

    for (auto& ew : g_errWins) {
        if (ew.hwnd) DestroyWindow(ew.hwnd);
    }
    g_errWins.clear();
    PumpMessages();
}

// ============================================================
// ФАЗА 2
// ============================================================
void Phase2_Ribbon() {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreen = GetDC(nullptr);
    HDC hMem = CreateCompatibleDC(hScreen);
    HBITMAP bmp = CreateCompatibleBitmap(hScreen, sw, sh);
    HBITMAP old = (HBITMAP)SelectObject(hMem, bmp);

    BitBlt(hMem, 0, 0, sw, sh, hScreen, 0, 0, SRCCOPY);

    const int STRIPS = 50;
    int stripH = sh / STRIPS;
    std::vector<int> offs(STRIPS, 0);
    std::vector<int> spd(STRIPS);
    for (int i = 0; i < STRIPS; ++i)
        spd[i] = (rand() % 2 ? 1 : -1) * (2 + rand() % 5);

    DWORD start = GetTickCount();
    while (!g_stop.load() && (int)(GetTickCount() - start) < Cfg::kPhase2Ms) {
        for (int i = 0; i < STRIPS; ++i) {
            int y = i * stripH;
            offs[i] += spd[i];
            if (abs(offs[i]) > sw / 3) spd[i] = -spd[i];
            BitBlt(hScreen, offs[i], y, sw, stripH, hMem, 0, y, SRCCOPY);
        }
        Sleep(30);
    }

    SelectObject(hMem, old);
    DeleteObject(bmp);
    DeleteDC(hMem);
    ReleaseDC(nullptr, hScreen);
}

// ============================================================
// ФАЗА 3
// ============================================================
void Phase3_Bend() {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreen = GetDC(nullptr);
    HDC hMem = CreateCompatibleDC(hScreen);
    HBITMAP bmp = CreateCompatibleBitmap(hScreen, sw, sh);
    HBITMAP old = (HBITMAP)SelectObject(hMem, bmp);

    BitBlt(hMem, 0, 0, sw, sh, hScreen, 0, 0, SRCCOPY);

    DWORD start = GetTickCount();
    while (!g_stop.load() && (int)(GetTickCount() - start) < Cfg::kPhase3Ms) {
        DWORD elapsed = GetTickCount() - start;
        double t = (double)elapsed / Cfg::kPhase3Ms;
        double force = t;

        for (int y = 0; y < sh; y += 4) {
            int shift = (int)(force * 150.0 * y / sh);
            BitBlt(hScreen, shift, y, sw - shift, 4, hMem, 0, y, SRCCOPY);
            if (shift > 0) {
                RECT r = { 0, y, shift, y + 4 };
                HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(hScreen, &r, black);
                DeleteObject(black);
            }
        }
        Sleep(40);
    }

    BitBlt(hScreen, 0, 0, sw, sh, hMem, 0, 0, SRCCOPY);

    SelectObject(hMem, old);
    DeleteObject(bmp);
    DeleteDC(hMem);
    ReleaseDC(nullptr, hScreen);
}

// ============================================================
// ФАЗА 4
// ============================================================
void Phase4_Browsers() {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    g_browsers.clear();

    const wchar_t* queries[] = {
        L"как налить воду",
        L"как налить воду в стакан",
        L"зачем наливать воду"
    };

    int positions[3][4] = {
        {100, 100, 600, 400},
        {500, 250, 600, 400},
        {300, 400, 600, 400}
    };

    for (int i = 0; i < 3; ++i) {
        HWND hw = CreateWindowExW(
            WS_EX_TOPMOST,
            BROWS_CLASS, queries[i],
            WS_OVERLAPPEDWINDOW,
            positions[i][0], positions[i][1],
            positions[i][2], positions[i][3],
            nullptr, nullptr, g_hInst, nullptr);
        if (!hw) continue;

        SetWindowLongPtrW(hw, GWLP_USERDATA, i);
        ShowWindow(hw, SW_SHOW);
        UpdateWindow(hw);

        BrowserWin bw;
        bw.hwnd = hw;
        bw.index = i;
        g_browsers.push_back(bw);
    }

    const wchar_t* urls[] = {
        L"https://www.google.com/search?q=%D0%BA%D0%B0%D0%BA+%D0%BD%D0%B0%D0%BB%D0%B8%D1%82%D1%8C+%D0%B2%D0%BE%D0%B4%D1%83",
        L"https://www.google.com/search?q=%D0%BA%D0%B0%D0%BA+%D0%BD%D0%B0%D0%BB%D0%B8%D1%82%D1%8C+%D0%B2%D0%BE%D0%B4%D1%83+%D0%B2+%D1%81%D1%82%D0%B0%D0%BA%D0%B0%D0%BD",
        L"https://www.google.com/search?q=%D0%B7%D0%B0%D1%87%D0%B5%D0%BC+%D0%BD%D0%B0%D0%BB%D0%B8%D0%B2%D0%B0%D1%82%D1%8C+%D0%B2%D0%BE%D0%B4%D1%83"
    };
    for (int i = 0; i < 3; ++i) {
        ShellExecuteW(nullptr, L"open", urls[i], nullptr, nullptr, SW_SHOW);
    }

    HDC hScreen = GetDC(nullptr);

    DWORD start = GetTickCount();
    while (!g_stop.load() && (int)(GetTickCount() - start) < Cfg::kPhase4Ms) {
        int srcX = rand() % (sw - 300);
        int srcY = rand() % (sh - 300);
        int srcW = 100 + rand() % 200;
        int srcH = 100 + rand() % 200;

        int destX = rand() % (sw - 400);
        int destY = rand() % (sh - 400);
        int destW = srcW + (rand() % 200) - 50;
        int destH = srcH + (rand() % 200) - 50;

        StretchBlt(hScreen, destX, destY, destW, destH,
                   hScreen, srcX, srcY, srcW, srcH, SRCCOPY);

        Sleep(60);
    }

    ReleaseDC(nullptr, hScreen);

    for (auto& bw : g_browsers) {
        if (bw.hwnd) DestroyWindow(bw.hwnd);
    }
    g_browsers.clear();
    PumpMessages();
}

// ============================================================
// ФАЗА 5
// ============================================================
void Phase5_GlitchBlocks() {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    HDC hScreen = GetDC(nullptr);

    DWORD start = GetTickCount();
    while (!g_stop.load() && (int)(GetTickCount() - start) < Cfg::kPhase5Ms) {
        int srcX = rand() % (sw - 150);
        int srcY = rand() % (sh - 150);
        int bw = 50 + rand() % 150;
        int bh = 50 + rand() % 150;
        int destX = srcX + (rand() % 61) - 30;
        int destY = srcY + (rand() % 61) - 30;
        BitBlt(hScreen, destX, destY, bw, bh,
               hScreen, srcX, srcY, SRCINVERT);
        Sleep(5);
    }
    ReleaseDC(nullptr, hScreen);
}

// ============================================================
// ФАЗА 6
// ============================================================
void Phase6_StretchBlocks() {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    HDC hScreen = GetDC(nullptr);

    DWORD start = GetTickCount();
    while (!g_stop.load() && (int)(GetTickCount() - start) < Cfg::kPhase6Ms) {
        int srcX = rand() % (sw - 300);
        int srcY = rand() % (sh - 300);
        int srcW = 100 + rand() % 200;
        int srcH = 100 + rand() % 200;
        int destX = rand() % (sw - 400);
        int destY = rand() % (sh - 400);
        int destW = srcW + (rand() % 200) - 50;
        int destH = srcH + (rand() % 200) - 50;
        StretchBlt(hScreen, destX, destY, destW, destH,
                   hScreen, srcX, srcY, srcW, srcH, SRCCOPY);
        Sleep(15);
    }
    ReleaseDC(nullptr, hScreen);
}

// ============================================================
// ФАЗА 7
// ============================================================
void Phase7_IconSnake() {
    HWND hProgman = FindWindowW(L"Progman", nullptr);
    HWND hShellView = nullptr;
    HWND hListView = nullptr;

    if (hProgman) {
        hShellView = FindWindowExW(hProgman, nullptr, L"SHELLDLL_DefView", nullptr);
        if (hShellView) {
            hListView = FindWindowExW(hShellView, nullptr, L"SysListView32", nullptr);
        }
    }

    if (!hListView) {
        HWND hWorkerW = nullptr;
        while ((hWorkerW = FindWindowExW(nullptr, hWorkerW, L"WorkerW", nullptr)) != nullptr) {
            hShellView = FindWindowExW(hWorkerW, nullptr, L"SHELLDLL_DefView", nullptr);
            if (hShellView) {
                hListView = FindWindowExW(hShellView, nullptr, L"SysListView32", nullptr);
                if (hListView) break;
            }
        }
    }

    if (!hListView) {
        SleepPump(Cfg::kPhase7Ms);
        return;
    }

    DWORD exStyle = (DWORD)SendMessageW(hListView, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
    SendMessageW(hListView, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 exStyle & ~LVS_AUTOARRANGE);

    int count = (int)SendMessageW(hListView, LVM_GETITEMCOUNT, 0, 0);
    if (count <= 0) {
        SleepPump(Cfg::kPhase7Ms);
        return;
    }

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    double headX = sw / 2.0;
    double headY = sh / 2.0;
    double angle = 0.0;

    std::vector<double> trailX(count), trailY(count);
    for (int i = 0; i < count; ++i) {
        trailX[i] = headX - i * 40;
        trailY[i] = headY;
    }

    DWORD start = GetTickCount();
    while (!g_stop.load() && (int)(GetTickCount() - start) < Cfg::kPhase7Ms) {
        angle += 0.05;
        headX = sw / 2.0 + cos(angle) * (sw / 3.0);
        headY = sh / 2.0 + sin(angle) * (sh / 3.0);

        POINT p0 = { (LONG)headX, (LONG)headY };
        SendMessageW(hListView, LVM_SETITEMPOSITION, 0,
                     MAKELPARAM(p0.x, p0.y));

        for (int i = 1; i < count && i < 30; ++i) {
            POINT pi = { (LONG)trailX[i-1], (LONG)trailY[i-1] };
            SendMessageW(hListView, LVM_SETITEMPOSITION, i,
                         MAKELPARAM(pi.x, pi.y));

            POINT prev;
            SendMessageW(hListView, LVM_GETITEMPOSITION, i-1, (LPARAM)&prev);
            trailX[i-1] = prev.x;
            trailY[i-1] = prev.y;
        }

        Sleep(40);
    }

    SleepPump(500);
}

// ============================================================
// WATCHDOG
// ============================================================
void WatchdogThread() {
    DWORD s = GetTickCount();
    while (true) {
        if (g_stop.load()) return;
        if ((int)(GetTickCount() - s) > Cfg::kMaxPrankMs) {
            RemoveKbHook();
            ShowTaskbar();
            g_audioStop = true;
            DoReboot();
            return;
        }
        Sleep(1000);
    }
}

// ============================================================
// РЕБУТ
// ============================================================
void DoReboot() {
    HANDLE t;
    TOKEN_PRIVILEGES tp;
    if (OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &t)) {
        LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(t, FALSE, &tp, 0, nullptr, nullptr);
        CloseHandle(t);
    }
    ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_APPLICATION);
}

// ============================================================
// ТОЧКА ВХОДА
// ============================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDPIAware();
    g_hInst = hInst;
    srand((unsigned)time(nullptr));

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"ArmaturaMutex_v1");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    if (!IsElevated()) {
        RelaunchElevated();
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        return 0;
    }

    // ===== ФАЗА 0: Предупреждение =====
    int r = MessageBoxW(nullptr,
        L"Armatura.exe\n\n"
        L"Это ПРАНК. Ничего не удаляется, не вредит.\n"
        L"Просто сейчас будет АРМАТУРА.\n\n"
        L"Запустить?",
        L"Armatura.exe",
        MB_YESNO | MB_ICONWARNING | MB_TOPMOST);
    if (r != IDYES) {
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        return 0;
    }

    InstallKbHook();
    HideTaskbar();
    RegisterAllClasses();

    std::thread tAudio(AudioThread);
    std::thread tWatch(WatchdogThread);

    // Фазы 1-4 — звук Quarknova
    g_soundMode = 0;
    g_volume    = 90;

    Phase1_FlyingErrors();
    Phase2_Ribbon();
    Phase3_Bend();
    Phase4_Browsers();

    // Переключаемся на Synth Progression перед фазой 5
    g_soundMode = 1;
    g_volume    = 100;

    // Фазы 5-7 — авторский звук
    Phase5_GlitchBlocks();
    Phase6_StretchBlocks();
    Phase7_IconSnake();

    // ===== ФИНАЛ =====
    g_stop = true;
    Sleep(2000);

    RemoveKbHook();
    ShowTaskbar();
    g_audioStop = true;
    if (tAudio.joinable()) tAudio.join();
    if (tWatch.joinable()) tWatch.join();

    DoReboot();

    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return 0;
}
