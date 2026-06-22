#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <string>
#include <vector>
#include <map>

// ═══════════════════════════════════════════════════
//  MODERN DARK COLOR PALETTE
// ═══════════════════════════════════════════════════
#define CLR_BG      RGB(15,  16,  23)
#define CLR_SURFACE RGB(22,  23,  32)
#define CLR_SURFACE2 RGB(28, 29,  40)
#define CLR_BORDER  RGB(40,  41,  56)
#define CLR_ACCENT  RGB(129,115, 250)
#define CLR_ACCENT2 RGB(64,  55, 136)
#define CLR_GREEN   RGB(74, 222, 128)
#define CLR_RED     RGB(248,113, 113)
#define CLR_TEXT    RGB(230,232, 245)
#define CLR_DIM     RGB(120,123, 148)
#define CLR_MUTED   RGB(58,  60,  82)

// ═══════════════════════════════════════════════════
//  DATA
// ═══════════════════════════════════════════════════
struct KeyMap { int id; DWORD src; DWORD tgt; bool active; };

static HHOOK          g_hook      = nullptr;
static HWND           g_hwnd      = nullptr;
static HINSTANCE      g_inst      = nullptr;
static bool           g_active    = true;
static int            g_nextId    = 1;
static int            g_hover     = -1;
static int            g_hBtn      = -1;
static int            g_scrollY   = 0;
static int            g_scrollMax = 0;
static NOTIFYICONDATA g_nid       = {};
static std::vector<KeyMap>      g_maps;
static std::map<DWORD,DWORD>    g_map;
static UINT           g_hkMod     = MOD_CONTROL;
static UINT           g_hkVk      = VK_F12;

#define WM_TRAY     (WM_USER+1)
#define ID_SHOW     4001
#define ID_EXIT     4002
#define HK_TOGGLE   2003
#define CARD_H      68
#define CARD_GAP    8
#define LIST_TOP    138

// Timer used to resolve an ambiguous Left-Ctrl event (see HookProc) if no
// confirming/denying keyboard event arrives within a short window.
#define TIMER_CTRL_RESOLVE      9001
// Same purpose, scoped to the key-picker dialog's own message loop.
#define TIMER_PICK_CTRL_RESOLVE 9002

// ═══════════════════════════════════════════════════
//  KEY NAMES
// ═══════════════════════════════════════════════════
static const std::map<DWORD,std::wstring> g_keyNames = {
    {VK_LEFT,L"Left Arrow"},{VK_RIGHT,L"Right Arrow"},
    {VK_UP,L"Up Arrow"},{VK_DOWN,L"Down Arrow"},
    {0xA5,L"Right Alt"},{0xA4,L"Left Alt"},
    {0xA3,L"Right Ctrl"},{0xA2,L"Left Ctrl"},
    {0xA1,L"Right Shift"},{0xA0,L"Left Shift"},
    {VK_APPS,L"Menu"},
    {VK_DELETE,L"Delete"},{VK_INSERT,L"Insert"},
    {VK_HOME,L"Home"},{VK_END,L"End"},
    {VK_PRIOR,L"Page Up"},{VK_NEXT,L"Page Down"},
    {VK_BACK,L"Backspace"},{VK_RETURN,L"Enter"},
    {VK_TAB,L"Tab"},{VK_ESCAPE,L"Escape"},
    {VK_SPACE,L"Space"},{VK_CAPITAL,L"Caps Lock"},
    {VK_NUMLOCK,L"Num Lock"},{VK_SCROLL,L"Scroll Lock"},
    {VK_SNAPSHOT,L"Print Screen"},{VK_PAUSE,L"Pause"},
    {VK_F1,L"F1"},{VK_F2,L"F2"},{VK_F3,L"F3"},{VK_F4,L"F4"},
    {VK_F5,L"F5"},{VK_F6,L"F6"},{VK_F7,L"F7"},{VK_F8,L"F8"},
    {VK_F9,L"F9"},{VK_F10,L"F10"},{VK_F11,L"F11"},{VK_F12,L"F12"},
    {VK_NUMPAD0,L"Num 0"},{VK_NUMPAD1,L"Num 1"},{VK_NUMPAD2,L"Num 2"},
    {VK_NUMPAD3,L"Num 3"},{VK_NUMPAD4,L"Num 4"},{VK_NUMPAD5,L"Num 5"},
    {VK_NUMPAD6,L"Num 6"},{VK_NUMPAD7,L"Num 7"},{VK_NUMPAD8,L"Num 8"},
    {VK_NUMPAD9,L"Num 9"},{VK_MULTIPLY,L"Num *"},{VK_ADD,L"Num +"},
    {VK_SUBTRACT,L"Num -"},{VK_DIVIDE,L"Num /"},
    {0xBE,L"Period (.)"},{0xBC,L"Comma (,)"},{0xBF,L"Slash (/)"},
    {0xBD,L"Minus (-)"},{0xBB,L"Equals (=)"},{0xC0,L"Tilde (~)"},
    {0xDB,L"Left Bracket ["},{0xDD,L"Right Bracket ]"},
    {0xDC,L"Backslash"},{0xDE,L"Quote"},{0xBA,L"Semicolon"},
    {VK_SHIFT,L"Shift"},{VK_CONTROL,L"Ctrl"},{VK_MENU,L"Alt"},
    {VK_LWIN,L"Left Win"},{VK_RWIN,L"Right Win"},
};

std::wstring GetKeyName(DWORD vk) {
    auto it = g_keyNames.find(vk);
    if (it != g_keyNames.end()) return it->second;
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1,(wchar_t)vk);
    if (vk >= '0' && vk <= '9') return std::wstring(1,(wchar_t)vk);
    wchar_t buf[16]; swprintf(buf,16,L"VK_%02X",vk); return buf;
}

std::wstring GetHotkeyString() {
    std::wstring s;
    if (g_hkMod & MOD_CONTROL) s += L"Ctrl+";
    if (g_hkMod & MOD_SHIFT)   s += L"Shift+";
    if (g_hkMod & MOD_ALT)     s += L"Alt+";
    if (g_hkMod & MOD_WIN)     s += L"Win+";
    s += GetKeyName(g_hkVk);
    return s;
}

// ═══════════════════════════════════════════════════
//  CONFIG
// ═══════════════════════════════════════════════════
std::wstring GetCfgPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr,path,MAX_PATH);
    std::wstring s(path);
    auto pos = s.rfind(L'\\');
    return (pos != std::wstring::npos ? s.substr(0,pos) : s) + L"\\keymorph.cfg";
}

void RebuildMap() {
    g_map.clear();
    for (const auto& m : g_maps)
        if (m.active) g_map[m.src] = m.tgt;
}

void SaveConfig() {
    FILE* f = _wfopen(GetCfgPath().c_str(),L"w");
    if (!f) return;
    fwprintf(f,L"%d\n%u %u\n",(int)g_active,g_hkMod,g_hkVk);
    for (const auto& m : g_maps)
        fwprintf(f,L"%u %u %d\n",m.src,m.tgt,(int)m.active);
    fclose(f);
}

void LoadConfig() {
    FILE* f = _wfopen(GetCfgPath().c_str(),L"r");
    if (!f) return;
    int activeFlag;
    fwscanf(f,L"%d",&activeFlag); g_active=(activeFlag==1);
    fwscanf(f,L"%u %u",&g_hkMod,&g_hkVk);
    unsigned s,t; int e;
    while (fwscanf(f,L"%u %u %d",&s,&t,&e)==3)
        g_maps.push_back({g_nextId++,(DWORD)s,(DWORD)t,(e==1)});
    fclose(f);
    RebuildMap();
}

// ═══════════════════════════════════════════════════
//  HOOK — scan-code injection + deferred AltGr resolution
// ═══════════════════════════════════════════════════
//
// THE PROBLEM
// -----------
// Physically pressing AltGr (Right Alt on most non-US keyboards) makes
// Windows synthesize TWO separate low-level keyboard events in this exact
// order:
//   1) a "fake" Left-Ctrl keydown   (vkCode == VK_LCONTROL)
//   2) the real Right-Alt keydown   (vkCode == VK_RMENU)
// This is a legacy artifact of how AltGr is encoded at the hardware/driver
// level (it has always been "Ctrl+Alt" under the hood). If a user maps
// Left Ctrl to something else, that mapping would incorrectly fire every
// time AltGr is used to type a special character (e.g. @, €), because the
// hook cannot tell the fake Ctrl apart from a real one just by looking at
// vkCode alone.
//
// WHY A SIMPLE GetAsyncKeyState(VK_RMENU) CHECK DOES NOT WORK
// -------------------------------------------------------------
// At the exact moment the hook receives the fake Left-Ctrl event, the
// Right-Alt hardware event usually has NOT been processed yet (it is the
// very next event in the queue, not a prior one), so GetAsyncKeyState
// reports Right Alt as "not pressed" and the check fails — this is a race
// condition, not an occasional fluke, which is why the previous attempt
// kept misfiring.
//
// THE FIX: DEFERRED RESOLUTION
// -----------------------------
// Instead of judging the Left-Ctrl event the instant it arrives, we HOLD
// it (swallow it temporarily, inject nothing yet) and decide what it was
// only once we see what happens next:
//   • If the very next keyboard event is Right-Alt with the SAME
//     KBDLLHOOKSTRUCT::time timestamp  -> confirmed AltGr. We re-inject a
//     plain, unmapped Left-Ctrl keystroke (so the OS keyboard-layout
//     translator still sees Ctrl+Alt and can produce the correct special
//     character everywhere else), then let the Right-Alt event continue
//     through the normal pipeline below.
//   • Otherwise -> it was a genuine standalone Left-Ctrl press. We apply
//     the user's mapping for it now (or pass it through unmapped), then
//     continue processing the new event normally.
// If the user simply taps Left Ctrl alone and nothing else follows
// immediately, a short Windows timer (30 ms — far below human perception)
// flushes the held event as a real press so it never hangs indefinitely.

bool IsExtendedKey(DWORD vk) {
    switch (vk) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT:
        case VK_RCONTROL: case VK_RMENU:
        case VK_NUMLOCK: case VK_SNAPSHOT:
        case VK_APPS:
        case VK_LWIN: case VK_RWIN:
        case VK_DIVIDE: // Num /
            return true;
        default:
            return false;
    }
}

// Sends a single mapped keystroke via scan code (see comment block above
// IsExtendedKey's first use for why scan codes are preferred over VKs).
// Returns true if the key was actually mapped and injected (caller should
// swallow the original), false if it isn't mapped (caller should let the
// original event pass through normally).
bool InjectMappedKey(DWORD vk, bool isDown) {
    auto it = g_map.find(vk);
    if (it == g_map.end()) return false;

    DWORD targetVK = it->second;
    UINT  scan = MapVirtualKeyW(targetVK, MAPVK_VK_TO_VSC);

    INPUT inp = {};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk    = 0;                 // must be zero in scan-code mode
    inp.ki.wScan  = (WORD)scan;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (IsExtendedKey(targetVK))
        inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (!isDown)
        inp.ki.dwFlags |= KEYEVENTF_KEYUP;

    SendInput(1,&inp,sizeof(INPUT));
    return true;
}

// Injects a plain, completely unmapped keystroke. Used to compensate for
// an event we had to swallow while resolving the AltGr ambiguity, so the
// rest of the system still perceives the key exactly as if our hook had
// never intercepted it in the first place.
void InjectPlainKey(DWORD vk, bool isDown) {
    INPUT inp = {};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk    = 0;
    inp.ki.wScan  = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    inp.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (IsExtendedKey(vk))
        inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (!isDown)
        inp.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1,&inp,sizeof(INPUT));
}

// State for the single Left-Ctrl event currently awaiting resolution.
static bool  g_ctrlPending     = false;
static bool  g_ctrlPendingDown = true;
static DWORD g_ctrlPendingTime = 0;

// Called from WM_TIMER when no resolving event arrived in time: treats the
// held Left-Ctrl event as a genuine standalone press.
void FlushPendingCtrl() {
    if (!g_ctrlPending) return;
    g_ctrlPending = false;
    if (!InjectMappedKey(VK_LCONTROL, g_ctrlPendingDown))
        InjectPlainKey(VK_LCONTROL, g_ctrlPendingDown);
}

LRESULT CALLBACK HookProc(int code,WPARAM wp,LPARAM lp) {
    if (code >= 0 && g_active &&
        (wp==WM_KEYDOWN||wp==WM_SYSKEYDOWN||wp==WM_KEYUP||wp==WM_SYSKEYUP)) {
        auto* k = (KBDLLHOOKSTRUCT*)lp;
        if (!(k->flags & LLKHF_INJECTED)) {

            bool isDown = (wp==WM_KEYDOWN || wp==WM_SYSKEYDOWN);

            // Resolve a previously buffered Left-Ctrl event using the
            // event that just arrived, before doing anything else.
            if (g_ctrlPending) {
                if (g_hwnd) KillTimer(g_hwnd, TIMER_CTRL_RESOLVE);
                bool isAltGrPair = (k->vkCode == VK_RMENU) &&
                                    (k->time   == g_ctrlPendingTime);
                bool pendingWasDown = g_ctrlPendingDown;
                g_ctrlPending = false;

                if (isAltGrPair) {
                    // Confirmed AltGr: restore the OS's expected Ctrl+Alt
                    // state with a plain (unmapped) compensating keystroke,
                    // then fall through to process Right-Alt normally below.
                    InjectPlainKey(VK_LCONTROL, pendingWasDown);
                } else {
                    // Genuine standalone Ctrl press: apply its mapping now.
                    if (!InjectMappedKey(VK_LCONTROL, pendingWasDown))
                        InjectPlainKey(VK_LCONTROL, pendingWasDown);
                }
                // Either way, continue below to also handle the CURRENT event.
            }

            if (k->vkCode == VK_LCONTROL) {
                // Don't decide yet — Windows may still be about to deliver
                // the Right-Alt half of an AltGr combo. Hold this event and
                // resolve it on the next keyboard event or after a short
                // timeout, whichever comes first.
                g_ctrlPending     = true;
                g_ctrlPendingDown = isDown;
                g_ctrlPendingTime = k->time;
                if (g_hwnd) SetTimer(g_hwnd, TIMER_CTRL_RESOLVE, 30, nullptr);
                return 1;
            }

            if (InjectMappedKey(k->vkCode, isDown))
                return 1;
        }
    }
    return CallNextHookEx(g_hook,code,wp,lp);
}

// ═══════════════════════════════════════════════════
//  KEY PICKER — same AltGr ambiguity, resolved the same way
// ═══════════════════════════════════════════════════
// The picker only needs to tell whether a *single* press was Left Ctrl or
// the start of an AltGr combo, so a simpler heuristic suffices here: hold
// the Left-Ctrl press, and check whether the very next key event is Right
// Alt. A short timer (30 ms) finalizes the pick as Left Ctrl if nothing
// else follows, so tapping Ctrl alone to assign it never hangs the dialog.

static DWORD g_pickedKey = 0;
static UINT  g_pickedMod = 0;
static bool  g_pickCtrlPending = false;

void FinalizePick(DWORD vk) {
    g_pickedMod = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) g_pickedMod |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) g_pickedMod |= MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) g_pickedMod |= MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
        g_pickedMod |= MOD_WIN;
    g_pickedKey = vk;
}

LRESULT CALLBACK PickProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if ((DWORD)wp == VK_ESCAPE) {
            if (g_pickCtrlPending) { KillTimer(hw, TIMER_PICK_CTRL_RESOLVE); g_pickCtrlPending = false; }
            DestroyWindow(hw);
            return 0;
        }

        // Resolve a previously held Left-Ctrl press using the key that was
        // just pressed.
        if (g_pickCtrlPending) {
            KillTimer(hw, TIMER_PICK_CTRL_RESOLVE);
            g_pickCtrlPending = false;
            if ((DWORD)wp == VK_RMENU) {
                // Confirmed AltGr: fall through, the pick becomes Right Alt.
            } else {
                // Confirmed standalone Left Ctrl: finalize and ignore the
                // new key, it belongs to a separate, later press.
                FinalizePick(VK_LCONTROL);
                DestroyWindow(hw);
                return 0;
            }
        }

        if ((DWORD)wp == VK_LCONTROL) {
            g_pickCtrlPending = true;
            SetTimer(hw, TIMER_PICK_CTRL_RESOLVE, 30, nullptr);
            return 0;
        }

        UINT scanCode   = (lp >> 16) & 0xFF;
        bool isExtended = (lp & 0x01000000) != 0;

        // MapVirtualKey with MAPVK_VSC_TO_VK_EX can be unreliable for extended scan codes
        // on some driver/layout combinations. We handle Ctrl (0x1D) and Alt (0x38) explicitly.
        DWORD picked;
        if (scanCode == 0x1D) {
            picked = isExtended ? VK_RCONTROL : VK_LCONTROL;
        } else if (scanCode == 0x38) {
            picked = isExtended ? VK_RMENU : VK_LMENU;
        } else {
            UINT fullScan = isExtended ? (0xE000 | scanCode) : scanCode;
            DWORD specific = MapVirtualKeyW(fullScan, MAPVK_VSC_TO_VK_EX);
            picked = specific ? specific : (DWORD)wp;
        }

        FinalizePick(picked);
        DestroyWindow(hw);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_PICK_CTRL_RESOLVE && g_pickCtrlPending) {
            KillTimer(hw, TIMER_PICK_CTRL_RESOLVE);
            g_pickCtrlPending = false;
            FinalizePick(VK_LCONTROL);
            DestroyWindow(hw);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hw,&ps);
        RECT rc; GetClientRect(hw,&rc);
        HBRUSH bg = CreateSolidBrush(CLR_BG);
        FillRect(dc,&rc,bg); DeleteObject(bg);
        HPEN pen = CreatePen(PS_SOLID,2,CLR_ACCENT);
        HPEN op  = (HPEN)SelectObject(dc,pen);
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH ob = (HBRUSH)SelectObject(dc,nb);
        Rectangle(dc,1,1,rc.right-1,rc.bottom-1);
        SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(pen);
        SetBkMode(dc,TRANSPARENT);
        HFONT fBig = CreateFontW(24,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HFONT fSm = CreateFontW(14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HFONT of = (HFONT)SelectObject(dc,fBig);
        SetTextColor(dc,CLR_TEXT);
        RECT r1={0,50,rc.right,90}; DrawTextW(dc,L"Press a key",-1,&r1,DT_CENTER);
        SelectObject(dc,fSm);
        SetTextColor(dc,CLR_DIM);
        RECT r2={0,90,rc.right,115}; DrawTextW(dc,L"ESC to cancel",-1,&r2,DT_CENTER);
        SelectObject(dc,of); DeleteObject(fBig); DeleteObject(fSm);
        EndPaint(hw,&ps); return 0;
    }
    case WM_DESTROY:
        if (g_pickCtrlPending) { KillTimer(hw, TIMER_PICK_CTRL_RESOLVE); g_pickCtrlPending = false; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hw,msg,wp,lp);
}

DWORD PickKey(HWND parent,const wchar_t* title) {
    WNDCLASSEXW wc={sizeof(wc)};
    wc.lpfnWndProc=PickProc; wc.hInstance=g_inst;
    wc.lpszClassName=L"PickKW";
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    RegisterClassExW(&wc);
    g_pickedKey=0; g_pickedMod=0;
    int W=300,H=160;
    RECT pr; GetWindowRect(parent,&pr);
    int px=pr.left+(pr.right-pr.left-W)/2;
    int py=pr.top+(pr.bottom-pr.top-H)/2;
    HWND hw=CreateWindowExW(WS_EX_TOPMOST,L"PickKW",title,
        WS_POPUP|WS_CAPTION,px,py,W,H,parent,nullptr,g_inst,nullptr);
    BOOL dark=TRUE;
    DwmSetWindowAttribute(hw,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corner=DWMWCP_ROUND;
    DwmSetWindowAttribute(hw,DWMWA_WINDOW_CORNER_PREFERENCE,&corner,sizeof(corner));
    ShowWindow(hw,SW_SHOW); UpdateWindow(hw);
    MSG msg={};
    while (GetMessage(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}
    UnregisterClassW(L"PickKW",g_inst);
    return g_pickedKey;
}

// ═══════════════════════════════════════════════════
//  GDI HELPERS
// ═══════════════════════════════════════════════════
void FillRoundRect(HDC dc,int x,int y,int w,int h,int r,COLORREF c) {
    HBRUSH br=CreateSolidBrush(c);
    HPEN   pen=CreatePen(PS_SOLID,0,c);
    HBRUSH ob=(HBRUSH)SelectObject(dc,br);
    HPEN   op=(HPEN)SelectObject(dc,pen);
    RoundRect(dc,x,y,x+w,y+h,r,r);
    SelectObject(dc,ob); SelectObject(dc,op);
    DeleteObject(br); DeleteObject(pen);
}

void DrawRoundBorder(HDC dc,int x,int y,int w,int h,int r,COLORREF c,int thick=1) {
    HPEN   pen=CreatePen(PS_SOLID,thick,c);
    HBRUSH nb=(HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN   op=(HPEN)SelectObject(dc,pen);
    HBRUSH ob=(HBRUSH)SelectObject(dc,nb);
    RoundRect(dc,x,y,x+w,y+h,r,r);
    SelectObject(dc,op); SelectObject(dc,ob);
    DeleteObject(pen);
}

void DrawTextModern(HDC dc,const std::wstring& text,int x,int y,int w,int h,
                    COLORREF color,int fontSize=14,bool bold=false,
                    UINT fmt=DT_CENTER|DT_VCENTER|DT_SINGLELINE) {
    HFONT font=CreateFontW(fontSize,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,0,0,0,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HFONT of=(HFONT)SelectObject(dc,font);
    SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT);
    RECT r={x,y,x+w,y+h};
    DrawTextW(dc,text.c_str(),-1,&r,fmt|DT_END_ELLIPSIS);
    SelectObject(dc,of); DeleteObject(font);
}

// ═══════════════════════════════════════════════════
//  SCROLL
// ═══════════════════════════════════════════════════
void UpdateScroll(HWND hw) {
    RECT rc; GetClientRect(hw,&rc);
    int listH = rc.bottom - LIST_TOP - 10;
    int total = (int)g_maps.size() * (CARD_H + CARD_GAP);
    g_scrollMax = std::max(0,total-listH);
    g_scrollY   = std::min(g_scrollY,g_scrollMax);
}

// ═══════════════════════════════════════════════════
//  PAINT
// ═══════════════════════════════════════════════════
void OnPaint(HWND hw) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hw,&ps);
    RECT rc; GetClientRect(hw,&rc);
    int W=rc.right, H=rc.bottom;

    HDC mdc=CreateCompatibleDC(hdc);
    HBITMAP bmp=CreateCompatibleBitmap(hdc,W,H);
    HBITMAP oldBmp=(HBITMAP)SelectObject(mdc,bmp);

    // ── Background
    HBRUSH bg=CreateSolidBrush(CLR_BG);
    FillRect(mdc,&rc,bg); DeleteObject(bg);

    // ── Header
    FillRoundRect(mdc,0,0,W,110,0,CLR_SURFACE);
    HPEN lp=CreatePen(PS_SOLID,1,CLR_BORDER);
    HPEN olp=(HPEN)SelectObject(mdc,lp);
    MoveToEx(mdc,0,110,nullptr); LineTo(mdc,W,110);
    SelectObject(mdc,olp); DeleteObject(lp);

    // Logo – "KM"
    FillRoundRect(mdc,20,20,48,48,12,CLR_ACCENT2);
    FillRoundRect(mdc,23,23,42,42,10,CLR_ACCENT);
    DrawTextModern(mdc,L"KM",23,23,42,42,RGB(255,255,255),18,true);

    // Title
    DrawTextModern(mdc,L"KeyMorph",80,22,200,24,CLR_TEXT,18,true,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    DrawTextModern(mdc,L"Universal Keyboard Remapper  \x2022  Made by VryxTech",80,48,400,18,CLR_DIM,13,false,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    // Status badge (clickable — toggles active)
    bool act=g_active;
    COLORREF badgeClr = act ? CLR_GREEN : CLR_RED;
    COLORREF badgeBg  = act ? RGB(15,40,22) : RGB(45,15,15);
    int bx=W-145, by=24, bw=125, bh=34;
    FillRoundRect(mdc,bx,by,bw,bh,17,badgeBg);
    DrawRoundBorder(mdc,bx,by,bw,bh,17,badgeClr);
    // Dot
    HBRUSH db=CreateSolidBrush(badgeClr);
    HPEN   dp=CreatePen(PS_SOLID,0,badgeClr);
    HBRUSH odb=(HBRUSH)SelectObject(mdc,db);
    HPEN   odp=(HPEN)SelectObject(mdc,dp);
    Ellipse(mdc,bx+12,by+12,bx+24,by+24);
    SelectObject(mdc,odb); SelectObject(mdc,odp);
    DeleteObject(db); DeleteObject(dp);
    DrawTextModern(mdc,act?L"ACTIVE":L"PASSIVE",bx+28,by,bw-32,bh,badgeClr,13,true);

    // Hotkey hint (clickable — change shortcut)
    bool hkHov=(g_hover==-4 && g_hBtn==1);
    std::wstring hkStr = L"[" + GetHotkeyString() + L"] toggle";
    DrawTextModern(mdc,hkStr,bx-135,by+4,130,26,hkHov?CLR_ACCENT:CLR_DIM,13,false,
                   DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    // "+ New Mapping" button (enlarged)
    int nw=150, nh=28, nx=W-24-nw, ny=78;
    bool newHov=(g_hover==-2);
    FillRoundRect(mdc,nx,ny,nw,nh,8,newHov?CLR_ACCENT:CLR_ACCENT2);
    DrawRoundBorder(mdc,nx,ny,nw,nh,8,CLR_ACCENT);
    DrawTextModern(mdc,L"+ New Mapping",nx,ny,nw,nh,CLR_TEXT,13,true);

    // Section header
    wchar_t hdr[64];
    swprintf(hdr,64,L"Mappings  (%d)",(int)g_maps.size());
    DrawTextModern(mdc,hdr,20,110,180,20,CLR_DIM,12,true,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    // ── Card list with clipping
    HRGN clip=CreateRectRgn(0,LIST_TOP,W,H-22);
    SelectClipRgn(mdc,clip);

    int cardW = W - 40;
    int y0    = LIST_TOP - g_scrollY;

    if (g_maps.empty()) {
        SelectClipRgn(mdc,nullptr);
        int ex=W/2-180, ey=LIST_TOP+40, ew=360, eh=100;
        FillRoundRect(mdc,ex,ey,ew,eh,12,CLR_SURFACE);
        DrawRoundBorder(mdc,ex,ey,ew,eh,12,CLR_BORDER);
        DrawTextModern(mdc,L"No mappings yet",ex,ey,ew,54,CLR_DIM,16,true);
        DrawTextModern(mdc,L"Click  \"+ New Mapping\"  to add one",ex,ey+44,ew,56,CLR_MUTED,13,false);
    } else {
        for (int i=0;i<(int)g_maps.size();i++) {
            auto& m=g_maps[i];
            int cy=y0+i*(CARD_H+CARD_GAP);
            if (cy+CARD_H<LIST_TOP||cy>H) continue;
            bool hov=(g_hover==i);

            FillRoundRect(mdc,20,cy,cardW,CARD_H,10,hov?RGB(32,34,48):CLR_SURFACE);
            DrawRoundBorder(mdc,20,cy,cardW,CARD_H,10,
                m.active?(hov?CLR_ACCENT:CLR_BORDER):CLR_BORDER, hov?2:1);

            if (m.active) {
                HBRUSH sb=CreateSolidBrush(CLR_ACCENT);
                HBRUSH osb=(HBRUSH)SelectObject(mdc,sb);
                RECT sr={21,cy+10,24,cy+CARD_H-10};
                FillRect(mdc,&sr,sb);
                SelectObject(mdc,osb); DeleteObject(sb);
            }

            int chipW = std::max(80, std::min(160,(cardW-230)/2));

            FillRoundRect(mdc,36,cy+10,chipW,CARD_H-20,8,CLR_SURFACE2);
            DrawRoundBorder(mdc,36,cy+10,chipW,CARD_H-20,8,CLR_BORDER);
            DrawTextModern(mdc,GetKeyName(m.src),38,cy+10,chipW-4,CARD_H-20,
                           CLR_TEXT,13,true,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

            DrawTextModern(mdc,L"\x2192",36+chipW+10,cy,30,CARD_H,CLR_ACCENT,16,true);

            int tgtX=36+chipW+44;
            FillRoundRect(mdc,tgtX,cy+10,chipW,CARD_H-20,8,CLR_SURFACE2);
            DrawRoundBorder(mdc,tgtX,cy+10,chipW,CARD_H-20,8,CLR_BORDER);
            DrawTextModern(mdc,GetKeyName(m.tgt),tgtX+2,cy+10,chipW-4,CARD_H-20,
                           CLR_TEXT,13,true,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

            int togX=20+cardW-82, togY=cy+CARD_H/2-12;
            FillRoundRect(mdc,togX,togY,40,24,12,m.active?CLR_ACCENT:CLR_BORDER);
            int thumbX=m.active?togX+18:togX+2;
            HBRUSH tb=CreateSolidBrush(RGB(240,240,255));
            HPEN   tp=CreatePen(PS_SOLID,0,RGB(240,240,255));
            HBRUSH otb=(HBRUSH)SelectObject(mdc,tb);
            HPEN   otp=(HPEN)SelectObject(mdc,tp);
            Ellipse(mdc,thumbX,togY+2,thumbX+20,togY+22);
            SelectObject(mdc,otb); SelectObject(mdc,otp);
            DeleteObject(tb); DeleteObject(tp);

            int delX=20+cardW-34, delY=cy+CARD_H/2-14;
            bool delHov=(g_hover==i&&g_hBtn==1);
            if (delHov) FillRoundRect(mdc,delX,delY,28,28,6,RGB(50,15,15));
            DrawTextModern(mdc,L"\x00D7",delX,delY,28,28,delHov?CLR_RED:CLR_MUTED,14,true);
        }
    }

    SelectClipRgn(mdc,nullptr); DeleteObject(clip);

    // ── Footer
    HPEN fp=CreatePen(PS_SOLID,1,CLR_BORDER);
    HPEN ofp=(HPEN)SelectObject(mdc,fp);
    MoveToEx(mdc,0,H-24,nullptr); LineTo(mdc,W,H-24);
    SelectObject(mdc,ofp); DeleteObject(fp);
    DrawTextModern(mdc,L"KeyMorph  \x2022  Close minimizes to tray  \x2022  Double-click icon to open",
                   0,H-24,W,24,CLR_MUTED,12,false);

    BitBlt(hdc,0,0,W,H,mdc,0,0,SRCCOPY);
    SelectObject(mdc,oldBmp);
    DeleteObject(bmp); DeleteDC(mdc);
    EndPaint(hw,&ps);
}

// ═══════════════════════════════════════════════════
//  HIT TEST
// ═══════════════════════════════════════════════════
struct HitResult { int card; int btn; };
HitResult HitTest(HWND hw,int mx,int my) {
    RECT rc; GetClientRect(hw,&rc);
    int W=rc.right, cardW=W-40;
    // Status badge → toggle active
    if (mx>=W-145&&mx<=W-20&&my>=24&&my<=58) return {-4,0};
    // Hotkey hint → change shortcut
    if (mx>=W-280&&mx<=W-150&&my>=28&&my<=54)  return {-4,1};
    // New mapping button (enlarged)
    int nx=W-24-150;
    if (mx>=nx&&mx<=nx+150&&my>=78&&my<=106)  return {-2,-1};
    if (my<LIST_TOP) return {-1,-1};
    int ly=my+g_scrollY-LIST_TOP;
    int idx=ly/(CARD_H+CARD_GAP);
    int rem=ly%(CARD_H+CARD_GAP);
    if (rem>CARD_H||idx<0||idx>=(int)g_maps.size()) return {-1,-1};
    int cy=LIST_TOP-g_scrollY+idx*(CARD_H+CARD_GAP);
    // Toggle
    int togX=20+cardW-82, togY=cy+CARD_H/2-12;
    if (mx>=togX&&mx<=togX+40&&my>=togY&&my<=togY+24) return {idx,0};
    // Delete
    int delX=20+cardW-34, delY=cy+CARD_H/2-14;
    if (mx>=delX&&mx<=delX+28&&my>=delY&&my<=delY+28) return {idx,1};
    return {idx,-1};
}

// ═══════════════════════════════════════════════════
//  WINDOW PROCEDURE
// ═══════════════════════════════════════════════════
LRESULT CALLBACK WndProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(hw); return 0;
    case WM_SIZE: UpdateScroll(hw); InvalidateRect(hw,nullptr,FALSE); return 0;

    case WM_MOUSEWHEEL: {
        int delta=GET_WHEEL_DELTA_WPARAM(wp);
        g_scrollY=std::max(0,std::min(g_scrollMax,g_scrollY-(delta/2)));
        InvalidateRect(hw,nullptr,FALSE); return 0;
    }
    case WM_MOUSEMOVE: {
        int mx=LOWORD(lp),my=HIWORD(lp);
        auto h=HitTest(hw,mx,my);
        if (h.card!=g_hover||h.btn!=g_hBtn) {
            g_hover=h.card; g_hBtn=h.btn;
            bool hand=(h.card>=0||h.card==-2||h.card==-4);
            SetCursor(LoadCursor(nullptr,hand?IDC_HAND:IDC_ARROW));
            InvalidateRect(hw,nullptr,FALSE);
        }
        TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hw,0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hover=-1; g_hBtn=-1;
        InvalidateRect(hw,nullptr,FALSE); return 0;

    case WM_LBUTTONUP: {
        int mx=LOWORD(lp),my=HIWORD(lp);
        auto h=HitTest(hw,mx,my);

        if (h.card==-4) {
            if (h.btn==0) {
                g_active=!g_active; RebuildMap();
                wcscpy_s(g_nid.szTip,g_active?L"KeyMorph - Active":L"KeyMorph - Passive");
                Shell_NotifyIconW(NIM_MODIFY,&g_nid);
                SaveConfig(); InvalidateRect(hw,nullptr,FALSE);
            } else if (h.btn==1) {
                PickKey(hw,L"Press new shortcut (e.g. Ctrl+F12)");
                if (g_pickedKey) {
                    UnregisterHotKey(hw,HK_TOGGLE);
                    g_hkMod=g_pickedMod; g_hkVk=g_pickedKey;
                    RegisterHotKey(hw,HK_TOGGLE,g_hkMod,g_hkVk);
                    SaveConfig(); InvalidateRect(hw,nullptr,FALSE);
                }
            }
        } else if (h.card==-2) {
            DWORD src=PickKey(hw,L"SOURCE \x2014 Press the key you want to remap");
            if (!src) break;
            DWORD tgt=PickKey(hw,L"TARGET \x2014 Press the key to send instead");
            if (!tgt) break;
            g_maps.push_back({g_nextId++,src,tgt,true});
            RebuildMap(); UpdateScroll(hw); SaveConfig();
            InvalidateRect(hw,nullptr,FALSE);
        } else if (h.card>=0 && h.card<(int)g_maps.size()) {
            if (h.btn==0) {
                g_maps[h.card].active=!g_maps[h.card].active;
                RebuildMap(); SaveConfig(); InvalidateRect(hw,nullptr,FALSE);
            } else if (h.btn==1) {
                std::wstring q=L"Delete this mapping?\n\n" +
                    GetKeyName(g_maps[h.card].src)+L"  \x2192  "+GetKeyName(g_maps[h.card].tgt);
                if (MessageBoxW(hw,q.c_str(),L"Confirm Delete",MB_YESNO|MB_ICONQUESTION)==IDYES) {
                    g_maps.erase(g_maps.begin()+h.card);
                    g_hover=-1; g_hBtn=-1;
                    RebuildMap(); UpdateScroll(hw); SaveConfig();
                    InvalidateRect(hw,nullptr,FALSE);
                }
            }
        }
        break;
    }
    case WM_HOTKEY:
        if (wp==HK_TOGGLE) {
            g_active=!g_active; RebuildMap();
            wcscpy_s(g_nid.szTip,g_active?L"KeyMorph - Active":L"KeyMorph - Passive");
            Shell_NotifyIconW(NIM_MODIFY,&g_nid);
            SaveConfig(); InvalidateRect(hw,nullptr,FALSE);
        }
        break;
    case WM_TIMER:
        // Fallback resolution for a Left-Ctrl event held by HookProc while
        // it waited to see whether an AltGr-confirming Right-Alt event
        // would follow. If we get here, nothing followed in time, so the
        // held event is treated as a genuine standalone Ctrl press.
        if (wp == TIMER_CTRL_RESOLVE) {
            KillTimer(hw, TIMER_CTRL_RESOLVE);
            FlushPendingCtrl();
        }
        return 0;
    case WM_TRAY:
        if (lp==WM_LBUTTONDBLCLK){ShowWindow(hw,SW_SHOW);SetForegroundWindow(hw);}
        if (lp==WM_RBUTTONUP) {
            HMENU m=CreatePopupMenu();
            AppendMenuW(m,MF_STRING,ID_SHOW,L"Show Window");
            AppendMenuW(m,MF_STRING,ID_EXIT,L"Exit");
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hw);
            TrackPopupMenu(m,TPM_RIGHTBUTTON,pt.x,pt.y,0,hw,nullptr);
            DestroyMenu(m);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp)==ID_SHOW){ShowWindow(hw,SW_SHOW);SetForegroundWindow(hw);}
        if (LOWORD(wp)==ID_EXIT){SaveConfig();DestroyWindow(hw);}
        break;
    case WM_CLOSE: {
        ShowWindow(hw,SW_HIDE);
        // Show tray balloon only once to avoid annoying the user.
        static bool shownOnce = false;
        if (!shownOnce) {
            shownOnce = true;
            g_nid.uFlags |= NIF_INFO;
            wcscpy_s(g_nid.szInfoTitle, L"KeyMorph is still running");
            wcscpy_s(g_nid.szInfo, L"Minimized to the system tray. Double-click the icon to reopen.");
            g_nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY,&g_nid);
            g_nid.uFlags &= ~NIF_INFO; // avoid affecting subsequent NIM_MODIFY calls
        }
        return 0;
    }
    case WM_DESTROY:  PostQuitMessage(0);     return 0;
    }
    return DefWindowProcW(hw,msg,wp,lp);
}

// ═══════════════════════════════════════════════════
//  WINMAIN
// ═══════════════════════════════════════════════════
int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR,int nShow) {
    g_inst=hi;
    SetProcessDPIAware();

    HANDLE mx=CreateMutexW(nullptr,TRUE,L"KeyMorph_v3");
    if (GetLastError()==ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,L"KeyMorph is already running.",L"KeyMorph",MB_ICONINFORMATION);
        return 0;
    }

    LoadConfig();

    WNDCLASSEXW wc={sizeof(wc)};
    wc.lpfnWndProc=WndProc; wc.hInstance=hi;
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName=L"KMv3";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
    wc.style=CS_DROPSHADOW;
    RegisterClassExW(&wc);

    int W=720,H=560;
    int sx=(GetSystemMetrics(SM_CXSCREEN)-W)/2;
    int sy=(GetSystemMetrics(SM_CYSCREEN)-H)/2;
    g_hwnd=CreateWindowExW(WS_EX_APPWINDOW,L"KMv3",L"KeyMorph",
        WS_OVERLAPPEDWINDOW&~WS_THICKFRAME&~WS_MAXIMIZEBOX,
        sx,sy,W,H,nullptr,nullptr,hi,nullptr);

    BOOL dark=TRUE;
    DwmSetWindowAttribute(g_hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corner=DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd,DWMWA_WINDOW_CORNER_PREFERENCE,&corner,sizeof(corner));

    g_hook=SetWindowsHookExW(WH_KEYBOARD_LL,HookProc,nullptr,0);

    g_nid.cbSize=sizeof(g_nid); g_nid.hWnd=g_hwnd; g_nid.uID=1;
    g_nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    g_nid.uCallbackMessage=WM_TRAY;
    g_nid.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
    wcscpy_s(g_nid.szTip,L"KeyMorph - Active");
    Shell_NotifyIconW(NIM_ADD,&g_nid);

    RegisterHotKey(g_hwnd,HK_TOGGLE,g_hkMod,g_hkVk);
    ShowWindow(g_hwnd,nShow); UpdateWindow(g_hwnd);

    MSG msg={};
    while (GetMessage(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}

    UnhookWindowsHookEx(g_hook);
    Shell_NotifyIconW(NIM_DELETE,&g_nid);
    UnregisterHotKey(g_hwnd,HK_TOGGLE);
    CloseHandle(mx);
    return (int)msg.wParam;
}
