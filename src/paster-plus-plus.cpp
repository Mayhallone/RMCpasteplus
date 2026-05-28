// CMakeProject1.cpp : Defines the entry point for the application.
// Paster++ 
//
// Copyright (C) Daniel Alfredsson, daniel@alfredsson.nu, 2021-2026

#include "paster-plus-plus.h"



//using namespace std;

namespace constants {
    const std::string PRODUCT_NAME = "Paster++";
    const std::string PRODUCT_VERSION = "v1.0";
    const std::string PRODUCT_INFO = "https://github.com/hacke78/Paster-plus-plus";
    const std::string PRODUCT_AUTHOR = "Daniel Alfredsson";
    const std::string PRODUCT_AUTHOR_EMAIL = "daniel@alfredsson.nu";
    const std::string CONFIG_FILENAME = "paster-plus-plus.cfg";
}

// Paste-speed levels. SendKey sends each scancode as its own SendInput call
// (no atomic batching) and pauses three times per character:
//   pre_ms   - settle time before sending modifier-downs (between chars)
//   hold_ms  - between key-down and key-up of the actual character
//   post_ms  - between key-up and modifier-ups (also acts as inter-char gap)
// timeBeginPeriod(1) at startup makes Sleep accurate to ~1 ms.
// Level 3 (20, 30, 30) matches the original repo values that work well in
// practice. Adjust freely to experiment.
struct SpeedTiming { int pre_ms; int hold_ms; int post_ms; };
static const SpeedTiming kSpeedTimings[5] = {
    { 50, 60, 80 },   // 1 slowest  (~190 ms/char, ~5 c/s)
    { 30, 45, 50 },   // 2          (~125 ms/char, ~8 c/s)
    { 20, 30, 30 },   // 3 default  (~80 ms/char, ~12 c/s) - matches original
    { 10, 20, 20 },   // 4 fast     (~50 ms/char, ~20 c/s)
    {  8, 20, 12 },   // 5 fastest  (~40 ms/char, ~25 c/s) - experimental
};

static int g_speed_level = 3;  // 1..5

#define SPEED_INDICATOR_TIMER_ID    100

#define ProgressBarSize             0.03            // Size of the screen in percentage to use for progress bar to adopt for various screen resoultions.
#define ProgressBarFont             "Consolas"      // Font for the progress bar text
#define VK_V                        0x56            // Virtual-Key code for V
#define VK_Q                        0x51            // Virtual-Key code for Q

#define SS_SHIFT                    1               // Shift state mapping
#define SS_CTRL                     2               // "
#define SS_SHIFT_CTRL               3               // "
#define SS_ALT                      4               // "
#define SS_SHIFT_ALT                5               // "
#define SS_ALT_CTRL                 6               // "

#define SC_LSHIFT                   42              // Scan-code for Left Shift
#define SC_CTRL                     29              // Scan-code for CTRL
#define SC_ALT                      56              // Scan-code for ALT

#define INPUT_LOCALE_ENGLISH        "00000409"      // Force english keyboard layout

#define MOD_NOREPEAT 	    0x4000

void clear_message_queue() {
    MSG msg;
    
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

//################################################################################################################
//# Forces the keyboard layout to be English layout
//# This works when in a console which is the intended purpose of this utility
//################################################################################################################
void force_eng_kbd_layout(){
    LoadKeyboardLayoutA(INPUT_LOCALE_ENGLISH, KLF_ACTIVATE);

}

//################################################################################################################
// Config file: stores the current paste-speed level next to the .exe.
// Format: commented file with a `speed = N` line (legacy bare-integer files
// are still accepted and migrated to the new format on next launch).
//################################################################################################################
std::string get_config_path() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return constants::CONFIG_FILENAME;
    std::string p(buf, n);
    size_t pos = p.find_last_of("\\/");
    if (pos != std::string::npos) p = p.substr(0, pos + 1);
    return p + constants::CONFIG_FILENAME;
}

void save_speed_to_config(int level) {
    std::ofstream f(get_config_path());
    if (!f) return;
    f << "# Paster++ configuration\r\n";
    f << "#\r\n";
    f << "# speed: paste speed level (integer, 1..5)\r\n";
    f << "#   1 = slowest  (~5 chars/sec, ~190 ms per char) - for very laggy remote consoles\r\n";
    f << "#   2 = slow     (~8 chars/sec, ~125 ms per char)\r\n";
    f << "#   3 = normal   (~12 chars/sec, ~80 ms per char)  [default]\r\n";
    f << "#   4 = fast     (~20 chars/sec, ~50 ms per char)\r\n";
    f << "#   5 = fastest  (~25 chars/sec, ~40 ms per char) - upper limit for reliable scancode injection\r\n";
    f << "#\r\n";
    f << "# Speed can also be changed at runtime with CTRL+ALT++ / CTRL+ALT+-\r\n";
    f << "# This file is rewritten on every speed change.\r\n";
    f << "#\r\n";
    f << "# Lines starting with # are comments and are ignored.\r\n";
    f << "\r\n";
    f << "speed = " << level << "\r\n";
}

// Parse the speed value from the config file. Accepts both the new
// `speed = N` format and a legacy bare integer on its own line.
// Returns -1 if the file is missing or contains no valid value.
static int parse_speed_from_config() {
    std::ifstream f(get_config_path());
    if (!f) return -1;
    std::string line;
    while (std::getline(f, line)) {
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;   // blank line
        if (line[start] == '#') continue;           // comment

        // Try `key = value`.
        size_t eq = line.find('=', start);
        if (eq != std::string::npos) {
            std::string key = line.substr(start, eq - start);
            size_t kend = key.find_last_not_of(" \t");
            if (kend != std::string::npos) key.erase(kend + 1);
            if (key == "speed") {
                size_t vs = line.find_first_not_of(" \t", eq + 1);
                if (vs != std::string::npos) {
                    try {
                        int v = std::stoi(line.substr(vs));
                        if (v >= 1 && v <= 5) return v;
                    } catch (...) {}
                }
            }
            continue;  // unknown key — skip
        }

        // Legacy bare-integer line.
        try {
            int v = std::stoi(line.substr(start));
            if (v >= 1 && v <= 5) return v;
        } catch (...) {}
    }
    return -1;
}

// Load speed from config (or fall back to 3) and always rewrite the file so
// the on-disk format is upgraded with the commented header on first launch.
void init_speed_from_config() {
    int v = parse_speed_from_config();
    if (v < 1 || v > 5) v = 3;
    g_speed_level = v;
    save_speed_to_config(g_speed_level);
}


// Single-instance enforcement: a named mutex (session-local) is created at
// startup. If it already exists, another Paster++ process is running.
// The handle is intentionally kept open for the lifetime of the process —
// Windows releases it automatically on process exit.
static HANDLE g_instance_mutex = NULL;

bool ensure_single_instance() {
    HANDLE m = CreateMutexA(NULL, FALSE, "Paster++_SingleInstanceMutex_v1");
    if (m == NULL) return true;  // best-effort: on CreateMutex failure, allow start
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);
        return false;
    }
    g_instance_mutex = m;
    return true;
}


// debug print function


#ifdef NDEBUG
static inline void odprintf(const char*, ...) {}
#else
void __cdecl odprintf(const char* format, ...)
{
    char    buf[4096], * p = buf;
    va_list args;
    int     n;

    va_start(args, format);
    n = vsnprintf(p, sizeof buf - 3, format, args); // buf-3 is room for CR/LF/NUL
    va_end(args);

    p += (n < 0 || (size_t)n >= sizeof buf - 3) ? sizeof buf - 3 : n;

    while (p > buf && isspace((unsigned char)p[-1]))
        *--p = '\0';

    *p++ = '\r';
    *p++ = '\n';
    *p = '\0';

    OutputDebugStringA(buf);
}
#endif

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }
        case WM_CLOSE:
        {
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            //FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        {
            // Cache the background brush — WM_CTLCOLORSTATIC fires on every
            // paint and creating a fresh brush each time leaked GDI handles.
            // Process-lifetime ownership; freed by Windows at process exit.
            static HBRUSH s_bgBrush = CreateSolidBrush(RGB(0, 0, 255));
            HDC hdcStatic = (HDC)wParam;
            SetBkMode(hdcStatic, TRANSPARENT);
            SetTextColor(hdcStatic, RGB(255, 255, 255));
            return (LRESULT)s_bgBrush;
        }
        case WM_TIMER:
        {
            if (wParam == SPEED_INDICATOR_TIMER_ID) {
                KillTimer(hwnd, SPEED_INDICATOR_TIMER_ID);
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;
        }
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}
//################################################################################################################
// Progress bar.
//
// The window is created ONCE at full screen width, fixed height, hidden.
//   - Begin() / End() show or hide it.
//   - Update() sets internal progress state and InvalidateRect's.
//   - WM_PAINT draws a blue fill from x=0 to the progress fraction, dark
//     grey for the remainder, and an "N / M" text overlay.
//
// The previous bar called SetWindowPos with a new size on every update.
// That triggers Z-order / DWM composition work on a topmost window which
// empirically disrupted SendInput dispatch to the foreground (chars
// dropped, modifier state stuck). This version never calls SetWindowPos
// during paste — only InvalidateRect.
//################################################################################################################
class ProgressBar_handler {
public:
    ProgressBar_handler();
    ~ProgressBar_handler();
    void Begin(int max);
    void Update(int progress, int max);
    void End();
private:
    HWND hwnd;
    int ScreenWidth;
    int ScreenHeight;
    int height_px;
    DWORD last_invalidate_tick;
    LONG s_progress;
    LONG s_max;
    // 32bpp DIB section drawn into via a memory DC, pushed to DWM via
    // UpdateLayeredWindow each frame.
    HDC     m_hdcMem;
    HBITMAP m_hBitmap;
    HBRUSH  m_fgBrush;
    HBRUSH  m_bgBrush;
    HFONT   m_hFont;
    static LRESULT CALLBACK BarWndProc(HWND, UINT, WPARAM, LPARAM);
    void PaintFrame();
    void PushFrame();
};

LRESULT CALLBACK ProgressBar_handler::BarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void ProgressBar_handler::PaintFrame() {
    if (!m_hdcMem) return;
    int width  = ScreenWidth;
    int height = height_px;
    LONG p = s_progress;
    LONG m = (s_max > 0) ? s_max : 1;
    int fill_w = (int)((LONGLONG)width * p / m);
    if (fill_w < 0) fill_w = 0;
    if (fill_w > width) fill_w = width;

    RECT fg_rc = { 0, 0, fill_w, height };
    FillRect(m_hdcMem, &fg_rc, m_fgBrush);
    RECT bg_rc = { fill_w, 0, width, height };
    FillRect(m_hdcMem, &bg_rc, m_bgBrush);

    SetBkMode(m_hdcMem, TRANSPARENT);
    SetTextColor(m_hdcMem, RGB(255, 255, 255));
    HFONT oldFont = (HFONT)SelectObject(m_hdcMem, m_hFont);
    char text[64];
    snprintf(text, sizeof(text), " %ld / %ld", (long)p, (long)m);
    RECT rc = { 0, 0, width, height };
    DrawTextA(m_hdcMem, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(m_hdcMem, oldFont);
}

void ProgressBar_handler::PushFrame() {
    if (!hwnd || !m_hdcMem) return;
    SIZE  size = { ScreenWidth, height_px };
    POINT src  = { 0, 0 };
    UpdateLayeredWindow(hwnd, NULL, NULL, &size, m_hdcMem, &src,
                        0, NULL, ULW_OPAQUE);
}

ProgressBar_handler::ProgressBar_handler() {
    ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
    height_px = (int)(ScreenHeight * ProgressBarSize);
    last_invalidate_tick = 0;

    s_progress = 0;
    s_max = 1;
    m_hdcMem = NULL;
    m_hBitmap = NULL;
    m_fgBrush = CreateSolidBrush(RGB(0, 0, 255));
    m_bgBrush = CreateSolidBrush(RGB(40, 40, 40));
    m_hFont   = CreateFontA(height_px - 2, 0, 0, 0, FW_BOLD,
                            FALSE, FALSE, FALSE,
                            ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, ProgressBarFont);

    // Register the legacy "myWindowClass" — SpeedIndicator_handler still uses
    // it and historically depends on this class being registered first.
    const char legacyCls[] = "myWindowClass";
    WNDCLASSEXA wcLegacy = {0};
    wcLegacy.cbSize = sizeof(wcLegacy);
    wcLegacy.lpfnWndProc = WindowProc;
    wcLegacy.hInstance = GetModuleHandle(NULL);
    wcLegacy.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcLegacy.hbrBackground = CreateSolidBrush(RGB(0, 0, 255));
    wcLegacy.lpszClassName = legacyCls;
    RegisterClassExA(&wcLegacy);

    // Dedicated class for the focus-safe progress bar.
    const char barCls[] = "PasterProgressBarClass";
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = BarWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;  // we paint everything
    wc.lpszClassName = barCls;
    RegisterClassExA(&wc);

    hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                           barCls, "Paster++ progress",
                           WS_POPUP,
                           0, 0, ScreenWidth, height_px,
                           NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!hwnd) return;

    HDC hdcScreen = GetDC(NULL);
    m_hdcMem = CreateCompatibleDC(hdcScreen);
    ReleaseDC(NULL, hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ScreenWidth;
    bmi.bmiHeader.biHeight = -height_px;   // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pPixels = NULL;
    m_hBitmap = CreateDIBSection(m_hdcMem, &bmi, DIB_RGB_COLORS, &pPixels, NULL, 0);
    if (m_hBitmap) SelectObject(m_hdcMem, m_hBitmap);
}

void ProgressBar_handler::Begin(int max) {
    s_progress = 0;
    s_max = (max > 0) ? (LONG)max : 1;
    if (!hwnd) return;
    HWND fg = GetForegroundWindow();
    if (fg && fg != hwnd) {
        SetWindowLongPtrA(hwnd, GWLP_HWNDPARENT, (LONG_PTR)fg);
    }
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, ScreenWidth, height_px,
                 SWP_NOACTIVATE);
    PaintFrame();
    PushFrame();
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    PushFrame();
}

void ProgressBar_handler::Update(int progress, int max) {
    s_progress = (LONG)progress + 1;
    s_max = (LONG)max;
    DWORD now = GetTickCount();
    bool is_last = (progress + 1 >= max);
    if (!is_last && (now - last_invalidate_tick) < 33) return;  // ~30 fps cap
    last_invalidate_tick = now;
    PaintFrame();
    PushFrame();
}

void ProgressBar_handler::End() {
    if (hwnd) ShowWindow(hwnd, SW_HIDE);
}

ProgressBar_handler::~ProgressBar_handler() {
    if (hwnd)     DestroyWindow(hwnd);
    if (m_hBitmap) DeleteObject(m_hBitmap);
    if (m_hdcMem)  DeleteDC(m_hdcMem);
    if (m_fgBrush) DeleteObject(m_fgBrush);
    if (m_bgBrush) DeleteObject(m_bgBrush);
    if (m_hFont)   DeleteObject(m_hFont);
}

//################################################################################################################
// Speed indicator: a small top-right popup shown for 2 seconds when the user
// changes paste speed via CTRL+ALT+'+' / CTRL+ALT+'-'.
// Reuses the "myWindowClass" registered by ProgressBar_handler — must be
// constructed AFTER ProgressBar_handler.
//################################################################################################################
class SpeedIndicator_handler {
public:
    SpeedIndicator_handler();
    ~SpeedIndicator_handler() { if (hwnd) DestroyWindow(hwnd); }
    void Show(int level);
private:
    HWND hwnd;
    HWND hwndText;
    int w, h;
};

SpeedIndicator_handler::SpeedIndicator_handler() : hwnd(NULL), hwndText(NULL) {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    w = screenW / 7;
    h = (int)(screenH * 0.09);   // taller to fit two lines
    int x = screenW - w;
    int y = 0;

    HINSTANCE hInst = GetModuleHandle(NULL);
    hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                           "myWindowClass", "Speed", WS_POPUP | WS_DLGFRAME,
                           x, y, w, h, NULL, NULL, hInst, NULL);
    if (!hwnd) return;

    hwndText = CreateWindowExA(WS_EX_TRANSPARENT, "STATIC", "",
                               WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
                               0, 0, w, h, hwnd, NULL, hInst, NULL);
    HFONT hFont = CreateFontA((int)(h * 0.32), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, ProgressBarFont);
    // SendMessageA explicitly: under -municode the unsuffixed SendMessage resolves
    // to SendMessageW and would mangle our char* text into garbled wide chars.
    SendMessageA(hwndText, WM_SETFONT, (WPARAM)hFont, TRUE);
}

void SpeedIndicator_handler::Show(int level) {
    if (!hwnd) return;
    std::string text = constants::PRODUCT_NAME + " speed:\r\n" + std::to_string(level);
    SendMessageA(hwndText, WM_SETTEXT, 0, (LPARAM)text.c_str());
    HWND fg = GetForegroundWindow();
    if (fg && fg != hwnd) {
        SetWindowLongPtrA(hwnd, GWLP_HWNDPARENT, (LONG_PTR)fg);
    }
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    // Restart the auto-hide timer (KillTimer is a no-op if it's not running).
    KillTimer(hwnd, SPEED_INDICATOR_TIMER_ID);
    SetTimer(hwnd, SPEED_INDICATOR_TIMER_ID, 2000, NULL);
}

class HotKey_handler {
public:
    enum {
        PASTE_KEYID         = 1,
        QUIT_KEYID          = 2,
        FASTER_KEYID        = 3,
        SLOWER_KEYID        = 4,
        FASTER_NUM_KEYID    = 5,
        SLOWER_NUM_KEYID    = 6,
    };
    HotKey_handler();

//private:

};


class ClipBoard_handler {
public:
    ClipBoard_handler() {
        CharPosition = 0;
        ClipBoardLength = 0;
        // Try opening the clipboard
        if (!OpenClipboard(nullptr)) {
            odprintf("OpenClipboard failed: %lu", GetLastError());
            return;
        }
        // Get handle of clipboard object for ANSI text
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData == NULL) {
            CloseClipboard();
            return;
        }

        // Lock the handle to get the actual text pointer
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (pszText == NULL) {
            CloseClipboard();
            return;
        }

        // Save text in a string class instance
        ClipBoardContent = std::string(pszText);
        ClipBoardLength = ClipBoardContent.length();
        
        // Replace \n\r with \n
        std::regex newlines_re("\r\n+");
        ClipBoardContent = std::regex_replace(ClipBoardContent, newlines_re, "\n");
        odprintf("Clipboard content: %s", ClipBoardContent.c_str());
        odprintf("Clipboard length: %d", ClipBoardLength);

        // Release the lock
        GlobalUnlock(hData);

        // Release the clipboard
        CloseClipboard();
    }
    
    size_t GetClipBoardSize() {
        return ClipBoardLength;
    }
    
    char GetNextChar() {
        return ClipBoardContent[CharPosition++];
    };
private:
    size_t CharPosition;
    size_t ClipBoardLength;
    std::string ClipBoardContent;

};

#ifdef PASTER_PASTE_LOG
// Diagnostic per-character paste log. Written next to the .exe as
// "paster-paste.log". Opened/closed by HotKey_handler at paste boundaries.
static std::ofstream g_paste_log;
static DWORD g_paste_log_t0 = 0;

static std::string paste_log_path() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return "paster-paste.log";
    std::string p(buf, n);
    size_t pos = p.find_last_of("\\/");
    if (pos != std::string::npos) p = p.substr(0, pos + 1);
    return p + "paster-paste.log";
}
#endif

class kbdEmulator_handler {
public:
    // Load the English keyboard layout ONCE (not per-char). Calling
    // LoadKeyboardLayoutA per character at 100 chars/sec causes layout
    // thrashing and a WM_INPUTLANGCHANGE broadcast storm, which can race
    // with input dispatch.
    kbdEmulator_handler() {
        hkl = LoadKeyboardLayoutA(INPUT_LOCALE_ENGLISH, KLF_ACTIVATE);
    }
    ~kbdEmulator_handler() {}
    // Dispatch one character — matches the original repo's pattern:
    // each scancode is sent as its own SendInput call (NOT batched), with
    // configurable pre/hold/post sleeps between events. This appears to
    // interact better with low-level keyboard hooks (AV, password managers,
    // remote-console drivers) than batched SendInput, which can have entire
    // batches partially intercepted.
    void SendKey(TCHAR ch) {
        if (ch == 0) return;
        int idx = g_speed_level - 1;
        if (idx < 0) idx = 0;
        if (idx > 4) idx = 4;
        const SpeedTiming& t = kSpeedTimings[idx];

        short raw = VkKeyScanW(ch);
        if (raw == -1) return;  // no mapping in current layout — skip
        short vk = raw & 0xFF;
        short shift_state = (raw & 0xFF00) >> 8;
        UINT scancode = MapVirtualKeyEx(vk, MAPVK_VK_TO_VSC, hkl);

        Sleep(t.pre_ms);

        switch (shift_state) {
            case SS_SHIFT:      SendScanCode(SC_LSHIFT); break;
            case SS_CTRL:       SendScanCode(SC_CTRL); break;
            case SS_ALT:        SendScanCode(SC_ALT); break;
            case SS_SHIFT_CTRL: SendScanCode(SC_LSHIFT); SendScanCode(SC_CTRL); break;
            case SS_SHIFT_ALT:  SendScanCode(SC_LSHIFT); SendScanCode(SC_ALT); break;
            case SS_ALT_CTRL:   SendScanCode(SC_ALT); SendScanCode(SC_CTRL); break;
        }
        SendScanCode(scancode);
        Sleep(t.hold_ms);
        SendScanCode(scancode, KEYEVENTF_KEYUP);
        Sleep(t.post_ms);
        switch (shift_state) {
            case SS_SHIFT:      SendScanCode(SC_LSHIFT, KEYEVENTF_KEYUP); break;
            case SS_CTRL:       SendScanCode(SC_CTRL, KEYEVENTF_KEYUP); break;
            case SS_ALT:        SendScanCode(SC_ALT, KEYEVENTF_KEYUP); break;
            case SS_SHIFT_CTRL: SendScanCode(SC_LSHIFT, KEYEVENTF_KEYUP); SendScanCode(SC_CTRL, KEYEVENTF_KEYUP); break;
            case SS_SHIFT_ALT:  SendScanCode(SC_LSHIFT, KEYEVENTF_KEYUP); SendScanCode(SC_ALT, KEYEVENTF_KEYUP); break;
            case SS_ALT_CTRL:   SendScanCode(SC_ALT, KEYEVENTF_KEYUP); SendScanCode(SC_CTRL, KEYEVENTF_KEYUP); break;
        }

#ifdef PASTER_PASTE_LOG
        if (g_paste_log.is_open()) {
            DWORD now = GetTickCount();
            char printable = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
            g_paste_log << (now - g_paste_log_t0) << "\t"
                        << "ch=0x" << std::hex << (unsigned)(unsigned char)ch << std::dec
                        << " '" << printable << "'"
                        << "\tsc=0x" << std::hex << scancode << std::dec
                        << "\tshift=" << shift_state
                        << "\tt=" << t.pre_ms << "/" << t.hold_ms << "/" << t.post_ms
                        << "\n";
            g_paste_log.flush();
        }
#endif
    }
    void ResetShiftKeys() {
        SendScanCode(SC_LSHIFT, KEYEVENTF_KEYUP);
        SendScanCode(SC_CTRL, KEYEVENTF_KEYUP);
        SendScanCode(SC_ALT, KEYEVENTF_KEYUP);
    }
private:
    HKL hkl;
    static void MakeKbdEvent(INPUT& in, DWORD scancode, bool keyup) {
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = (WORD)scancode;
        in.ki.time = 0;
        in.ki.wVk = 0;
        in.ki.dwExtraInfo = (ULONG_PTR)NULL;
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (keyup ? KEYEVENTF_KEYUP : 0);
    }
    void SendScanCode(DWORD code, int state=0) {
        INPUT inputs[1];
        MakeKbdEvent(inputs[0], code, state == KEYEVENTF_KEYUP);
        UINT sent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
        if (sent != ARRAYSIZE(inputs)) {
            odprintf("SendInput failed: sc=0x%X state=%d sent=%u err=%lu",
                     (unsigned)code, state, (unsigned)sent, GetLastError());
        }
    }
};
HotKey_handler::HotKey_handler() {

    ProgressBar_handler progress_bar;
    SpeedIndicator_handler speed_ind;

    if (RegisterHotKey(NULL, PASTE_KEYID, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_V))
    {
        odprintf("Hotkey 'ALT+CTRL+v' registered\n");
    }
    if (RegisterHotKey(NULL, QUIT_KEYID, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_Q))
    {
        odprintf("Hotkey 'ALT+CTRL+q' registered\n");
    }
    // Speed adjust: main-row +/- and numpad +/- (numpad is layout-independent).
    RegisterHotKey(NULL, FASTER_KEYID,     MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_OEM_PLUS);
    RegisterHotKey(NULL, SLOWER_KEYID,     MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_OEM_MINUS);
    RegisterHotKey(NULL, FASTER_NUM_KEYID, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_ADD);
    RegisterHotKey(NULL, SLOWER_NUM_KEYID, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SUBTRACT);

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0) != 0)
    {
        if (msg.message == WM_HOTKEY)
        {
            odprintf("key recieved: %d", msg.wParam);
            if (msg.wParam == PASTE_KEYID) {
                odprintf("WM_HOTKEY paste received\n");
                ClipBoard_handler* clipboard = new ClipBoard_handler();
                kbdEmulator_handler* kbdEmulator = new kbdEmulator_handler;
                kbdEmulator->ResetShiftKeys();
                // kbdEmulator's ctor already activated INPUT_LOCALE_ENGLISH;
                // no need to re-call force_eng_kbd_layout() here.
                size_t size = clipboard->GetClipBoardSize();
#ifndef PASTER_NO_PROGRESS
                progress_bar.Begin((int)size);   // show bar with correct max before settle Sleep
#endif
                Sleep(500);  // give the user time to release CTRL+ALT+V
#ifdef PASTER_PASTE_LOG
                g_paste_log.open(paste_log_path(), std::ios::out | std::ios::trunc);
                g_paste_log_t0 = GetTickCount();
                if (g_paste_log.is_open()) {
                    const SpeedTiming& tlog = kSpeedTimings[g_speed_level - 1];
                    g_paste_log << "# Paster++ paste log\n";
                    g_paste_log << "# speed_level=" << g_speed_level
                                << " size=" << size
                                << " pre_ms=" << tlog.pre_ms
                                << " hold_ms=" << tlog.hold_ms
                                << " post_ms=" << tlog.post_ms
                                << "\n";
                    g_paste_log << "# t_ms\tch\tsc\tshift\tpre/hold/post\n";
                }
#endif
                // Prime GetAsyncKeyState's "pressed since last call" bit so
                // a stale pre-paste ESC press doesn't trigger immediately.
                GetAsyncKeyState(VK_ESCAPE);
                for (size_t i = 0; i < size; i++) {
#ifndef PASTER_NO_PROGRESS
                    progress_bar.Update((int) i, (int) size);
#endif
                    kbdEmulator->SendKey(clipboard->GetNextChar());
                    // ESC = abort. Never injected by SendKey (VkKeyScanW returns
                    // -1 for control chars), so safe regardless of clipboard.
                    // Mask 0x8001 = currently down OR pressed-since-last-poll,
                    // catches fast taps that happen between two polls.
                    if (GetAsyncKeyState(VK_ESCAPE) & 0x8001) {
                        kbdEmulator->ResetShiftKeys();
                        break;
                    }
                }
#ifndef PASTER_NO_PROGRESS
                progress_bar.End();
#endif
                delete clipboard;
                delete kbdEmulator;
#ifdef PASTER_PASTE_LOG
                if (g_paste_log.is_open()) {
                    g_paste_log << "# done t_ms=" << (GetTickCount() - g_paste_log_t0) << "\n";
                    g_paste_log.close();
                }
#endif
            }
            else if (msg.wParam == FASTER_KEYID || msg.wParam == FASTER_NUM_KEYID) {
                if (g_speed_level < 5) {
                    g_speed_level++;
                    save_speed_to_config(g_speed_level);
                }
                speed_ind.Show(g_speed_level);
            }
            else if (msg.wParam == SLOWER_KEYID || msg.wParam == SLOWER_NUM_KEYID) {
                if (g_speed_level > 1) {
                    g_speed_level--;
                    save_speed_to_config(g_speed_level);
                }
                speed_ind.Show(g_speed_level);
            }
            else if (msg.wParam == QUIT_KEYID) {
                odprintf("WM_HOTKEY quit received\n");
                odprintf("program quit");
                std::string MsgBoxText = constants::PRODUCT_NAME + " terminated";
                std::string MsxBoxCaption = constants::PRODUCT_NAME + " " + constants::PRODUCT_VERSION + " by " + constants::PRODUCT_AUTHOR + "<" + constants::PRODUCT_AUTHOR_EMAIL + ">";
                MessageBoxA(NULL, MsgBoxText.c_str(), MsxBoxCaption.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND | MB_DEFAULT_DESKTOP_ONLY);
                PostQuitMessage(0);
            }
            continue;
        }
        // Dispatch other messages (e.g. WM_TIMER for the speed indicator hide-timer)
        // so they reach WindowProc.
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

//################################################################################################################
// Startup dialog: modal popup with a dropdown for selecting paste speed at launch.
// Pre-selects current_level. Returns the chosen level (or current_level on Cancel).
//################################################################################################################
#define IDC_SPEED_COMBO 101

struct StartupDialogState {
    int chosen_level;
    bool done;
};

LRESULT CALLBACK StartupDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StartupDialogState* st = (StartupDialogState*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lParam;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return 0;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == IDOK && st) {
                HWND combo = GetDlgItem(hwnd, IDC_SPEED_COMBO);
                LRESULT sel = SendMessageA(combo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel <= 4) st->chosen_level = (int)sel + 1;
                st->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == IDCANCEL && st) {
                st->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (st) st->done = true;
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static BOOL CALLBACK set_child_font(HWND child, LPARAM lp) {
    SendMessageA(child, WM_SETFONT, (WPARAM)lp, TRUE);
    return TRUE;
}

int show_startup_speed_dialog(int current_level) {
    HINSTANCE hInst = GetModuleHandle(NULL);
    const char dlgClass[] = "PasterStartupDialog";

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StartupDialogProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = dlgClass;
    RegisterClassExA(&wc);

    const int dlgW = 400, dlgH = 175;
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int x = (scrW - dlgW) / 2;
    int y = (scrH - dlgH) / 2;

    StartupDialogState st = { current_level, false };
    std::string title = constants::PRODUCT_NAME + " - Select paste speed";
    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        dlgClass, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH, NULL, NULL, hInst, &st);
    if (!hDlg) return current_level;

    CreateWindowExA(0, "STATIC", "Select paste speed (will be saved to config):",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        15, 15, dlgW - 30, 20, hDlg, NULL, hInst, NULL);

    HWND hCombo = CreateWindowExA(0, "COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        15, 42, dlgW - 30, 200, hDlg, (HMENU)(UINT_PTR)IDC_SPEED_COMBO, hInst, NULL);

    CreateWindowExA(0, "BUTTON", "OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        dlgW - 200, 95, 85, 28, hDlg, (HMENU)(UINT_PTR)IDOK, hInst, NULL);

    CreateWindowExA(0, "BUTTON", "Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        dlgW - 105, 95, 85, 28, hDlg, (HMENU)(UINT_PTR)IDCANCEL, hInst, NULL);

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    EnumChildWindows(hDlg, set_child_font, (LPARAM)hFont);

    static const char* items[5] = {
        "1 - Slowest  (~5 chars/sec)",
        "2 - Slow     (~8 chars/sec)",
        "3 - Normal   (~12 chars/sec) [default]",
        "4 - Fast     (~20 chars/sec)",
        "5 - Fastest  (~25 chars/sec)"
    };
    for (int i = 0; i < 5; i++) {
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)items[i]);
    }
    int idx = current_level - 1;
    if (idx < 0) idx = 0;
    if (idx > 4) idx = 4;
    SendMessageA(hCombo, CB_SETCURSEL, idx, 0);

    SetForegroundWindow(hDlg);
    SetFocus(hCombo);

    MSG msg;
    while (!st.done && GetMessage(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return st.chosen_level;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    odprintf("Program start");
    // Raise Sleep / timer resolution to ~1 ms so the inter-character and
    // key-hold delays mean what they claim. Without this, the OS default
    // (~15.6 ms tick) collapses all the faster speed levels into roughly the
    // same effective rate, AND introduces sporadic timing variance that
    // causes the receiving remote console to drop scancodes.
    timeBeginPeriod(1);
    if (!ensure_single_instance()) {
        std::string title = constants::PRODUCT_NAME + " - already running";
        std::string text  = constants::PRODUCT_NAME + " is already running.\n\n"
                            "Only one instance can be active at a time.\n"
                            "Check the system tray or Task Manager to find the running instance.";
        MessageBoxA(NULL, text.c_str(), title.c_str(),
                    MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        timeEndPeriod(1);
        return 0;
    }
    init_speed_from_config();
    int chosen = show_startup_speed_dialog(g_speed_level);
    if (chosen != g_speed_level) {
        g_speed_level = chosen;
        save_speed_to_config(g_speed_level);
    }
    std::string MsgBoxText = "This utility lets you paste content from clipboard into a remote console\n\nUsage:\nCTRL+ALT+V to paste.\nCTRL+ALT+Q to quit.\nCTRL+ALT++ / CTRL+ALT+- to adjust paste speed (1=slowest, 5=fastest, default=3).\nESC to abort a paste in progress.\n\nFor more info: " + constants::PRODUCT_INFO;
    std::string MsxBoxCaption = constants::PRODUCT_NAME + " " + constants::PRODUCT_VERSION + " by " + constants::PRODUCT_AUTHOR;

    MessageBoxA(NULL, MsgBoxText.c_str(), MsxBoxCaption.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND | MB_DEFAULT_DESKTOP_ONLY);

    clear_message_queue();
    force_eng_kbd_layout();
    {
        // Stack-scoped: dtor runs when the message loop exits via PostQuitMessage,
        // which cleans up the progress bar and speed indicator windows.
        HotKey_handler hot_key;
    }
    timeEndPeriod(1);
    return 0;
}
