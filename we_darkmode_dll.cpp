// WE_DarkMode.dll v4 — True Per-Process UltraDark Theme for Warcraft III Classic World Editor (Win32 x86)
// Features:
//   - Win32 32-bit atomic hot-patching on user32.dll (GetSysColor, GetSysColorBrush, FillRect, DrawEdge,
//     CreateWindowExA/W, CreateDialogIndirectParamA/W) and uxtheme.dll (OpenThemeData, OpenThemeDataEx,
//     GetThemeSysColor, GetThemeSysColorBrush)
//   - Live WH_CBT hook on the UI thread + DWM immersive dark title bars (including main 'Warcraft III' frame)
//   - UAH dark menu bar (centered top bar items) + native-aligned dark popup menus (left-aligned labels,
//     right-aligned Ctrl+* shortcuts, crisp separators, checkmarks, and submenu arrows)
//   - Custom dark rendering for classic non-manifested PushButtons, CheckBoxes, RadioButtons, GroupBoxes,
//     and UpDown spin controls (preserving BS_BITMAP / BS_ICON / BS_OWNERDRAW palette buttons)
//   - Alpha-aware toolbar icon renderer: vibrant full-color icons, auto-lifted monochrome black icons,
//     and clearly visible dimmed icons for disabled Cut/Copy/Paste/Undo/Redo buttons (no black shadow blobs)
//   - Accurate ListView & TreeView selection states via ListView_GetItemState / TreeView_GetItemState

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <richedit.h>
#include <cstdint>
#include <cstring>
#include <cwctype>

// ============================================================================
// Undocumented Win10/11 UxTheme Dark Mode APIs & UAH Menu Structs
// ============================================================================
enum PreferredAppMode : int {
    AppMode_Default    = 0,
    AppMode_AllowDark  = 1,
    AppMode_ForceDark  = 2,
    AppMode_ForceLight = 3,
    AppMode_Max        = 4
};

using fnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND hWnd, BOOL allow);
using fnSetPreferredAppMode    = PreferredAppMode(WINAPI*)(PreferredAppMode appMode);
using fnFlushMenuThemes        = void(WINAPI*)();
using fnRefreshImmersivePolicy = void(WINAPI*)();
using fnDwmSetWindowAttribute  = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

static constexpr DWORD kDwmUseImmersiveDarkModeBefore20H1 = 19;
static constexpr DWORD kDwmUseImmersiveDarkMode           = 20;

static constexpr UINT kWmUahDrawMenu     = 0x0091;
static constexpr UINT kWmUahDrawMenuItem = 0x0092;

union UAHMENUITEMMETRICS {
    struct { DWORD cx; DWORD cy; } rgsizeBar[2];
    struct { DWORD cx; DWORD cy; } rgsizePopup[4];
};
struct UAHMENUPOPUPMETRICS {
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
};
struct UAHMENU {
    HMENU hmenu;
    HDC hdc;
    DWORD dwFlags;
};
struct UAHMENUITEM {
    int iPosition;
    UAHMENUITEMMETRICS umim;
    UAHMENUPOPUPMETRICS umpm;
};
struct UAHDRAWMENUITEM {
    DRAWITEMSTRUCT dis;
    UAHMENU um;
    UAHMENUITEM umi;
};

// ============================================================================
// Hot-Patch Helper (32-bit x86 standard Win32 hot-patchable functions)
// ============================================================================
struct HotPatchSlot {
    uint8_t* target    = nullptr;
    void*    origEntry = nullptr; // target + 2 (skips 'mov edi, edi')
};

static bool InstallHotPatch(HMODULE mod, const char* funcName, void* detour, HotPatchSlot& slot) {
    if (!mod) return false;
    auto* p = reinterpret_cast<uint8_t*>(GetProcAddress(mod, funcName));
    if (!p) return false;

    // Verify 2-byte 'mov edi, edi' (8B FF) at p[0..1] and 5-byte padding (CC or 90) at p[-5..-1]
    if (p[0] != 0x8B || p[1] != 0xFF)
        return false;
    for (int i = -5; i < 0; ++i) {
        if (p[i] != 0xCC && p[i] != 0x90)
            return false;
    }

    DWORD oldProt = 0;
    if (!VirtualProtect(p - 5, 7, PAGE_EXECUTE_READWRITE, &oldProt))
        return false;

    int32_t rel = reinterpret_cast<intptr_t>(detour) - reinterpret_cast<intptr_t>(p);
    p[-5] = 0xE9;
    memcpy(p - 4, &rel, sizeof(rel));

    uint16_t shortJmp = 0xF9EB; // jmp short -5 (EB F9)
    __atomic_store_n(reinterpret_cast<uint16_t*>(p), shortJmp, __ATOMIC_SEQ_CST);

    FlushInstructionCache(GetCurrentProcess(), p - 5, 7);
    VirtualProtect(p - 5, 7, oldProt, &oldProt);

    slot.target = p;
    slot.origEntry = p + 2;
    return true;
}

static void RestoreHotPatch(HotPatchSlot& slot) {
    if (!slot.target) return;
    uint8_t* p = slot.target;
    if (p[0] == 0xEB && p[1] == 0xF9) {
        DWORD oldProt = 0;
        if (VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &oldProt)) {
            uint16_t origMov = 0xFF8B; // mov edi, edi (8B FF)
            __atomic_store_n(reinterpret_cast<uint16_t*>(p), origMov, __ATOMIC_SEQ_CST);
            FlushInstructionCache(GetCurrentProcess(), p, 2);
            VirtualProtect(p, 2, oldProt, &oldProt);
        }
    }
    slot.target = nullptr;
}

using fnGetSysColor                = DWORD(WINAPI*)(int);
using fnGetSysColorBrush           = HBRUSH(WINAPI*)(int);
using fnFillRect                   = int(WINAPI*)(HDC, const RECT*, HBRUSH);
using fnDrawEdge                   = BOOL(WINAPI*)(HDC, LPRECT, UINT, UINT);
using fnSetWindowLongA             = LONG(WINAPI*)(HWND, int, LONG);
using fnSetWindowLongW             = LONG(WINAPI*)(HWND, int, LONG);
using fnCreateWindowExA            = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using fnCreateWindowExW            = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using fnCreateDialogIndirectParamA = HWND(WINAPI*)(HINSTANCE, LPCDLGTEMPLATEA, HWND, DLGPROC, LPARAM);
using fnCreateDialogIndirectParamW = HWND(WINAPI*)(HINSTANCE, LPCDLGTEMPLATEW, HWND, DLGPROC, LPARAM);
using fnFillRgn                    = BOOL(WINAPI*)(HDC, HRGN, HBRUSH);
using fnSetTextColor               = COLORREF(WINAPI*)(HDC, COLORREF);
using fnOpenThemeData              = HTHEME(WINAPI*)(HWND, LPCWSTR);
using fnOpenThemeDataEx            = HTHEME(WINAPI*)(HWND, LPCWSTR, DWORD);
using fnGetThemeSysColor           = COLORREF(WINAPI*)(HTHEME, int);
using fnGetThemeSysColorBrush      = HBRUSH(WINAPI*)(HTHEME, int);

static HotPatchSlot gHpGetSysColor;
static HotPatchSlot gHpGetSysColorBrush;
static HotPatchSlot gHpFillRect;
static HotPatchSlot gHpDrawEdge;
static HotPatchSlot gHpSetWindowLongA;
static HotPatchSlot gHpSetWindowLongW;
static HotPatchSlot gHpCreateWindowExA;
static HotPatchSlot gHpCreateWindowExW;
static HotPatchSlot gHpCreateDialogIndirectParamA;
static HotPatchSlot gHpCreateDialogIndirectParamW;
static HotPatchSlot gHpFillRgn;
static HotPatchSlot gHpSetTextColor;
static HotPatchSlot gHpOpenThemeData;
static HotPatchSlot gHpOpenThemeDataEx;
static HotPatchSlot gHpGetThemeSysColor;
static HotPatchSlot gHpGetThemeSysColorBrush;

// ============================================================================
// Globals & Palette
// ============================================================================
static HMODULE gSelfModule = nullptr;
static volatile LONG gInitialized = 0;
static volatile LONG gShuttingDown = 0;
static thread_local HHOOK tCbtHook = nullptr;
static HWND gHoveredCtrl = nullptr;

static fnAllowDarkModeForWindow gAllowDarkModeForWindow = nullptr;
static fnSetPreferredAppMode    gSetPreferredAppMode    = nullptr;
static fnFlushMenuThemes        gFlushMenuThemes        = nullptr;
static fnRefreshImmersivePolicy gRefreshImmersivePolicy = nullptr;
static fnDwmSetWindowAttribute  gDwmSetWindowAttribute  = nullptr;

static COLORREF gColBg         = RGB(0x18, 0x18, 0x18);
static COLORREF gColSurface    = RGB(0x1E, 0x1E, 0x1E);
static COLORREF gColInput      = RGB(0x12, 0x12, 0x12);
static COLORREF gColButton     = RGB(0x26, 0x26, 0x2A);
static COLORREF gColText       = RGB(0xEA, 0xEA, 0xEA);
static COLORREF gColMutedText  = RGB(0x8A, 0x8A, 0x8A);
static COLORREF gColAccent     = RGB(0x26, 0x4F, 0x78);
static COLORREF gColHover      = RGB(0x30, 0x30, 0x36);
static COLORREF gColBorder     = RGB(0x38, 0x38, 0x38);
static COLORREF gColDarkShadow = RGB(0x0D, 0x0D, 0x0D);

static HBRUSH gBrushBg      = nullptr;
static HBRUSH gBrushSurface = nullptr;
static HBRUSH gBrushInput   = nullptr;
static HBRUSH gBrushButton  = nullptr;
static HBRUSH gBrushAccent  = nullptr;
static HBRUSH gBrushHover   = nullptr;
static HBRUSH gBrushBorder  = nullptr;

static COLORREF gSysColors[32]        = {};
static HBRUSH   gSysBrushes[32]       = {};
static bool     gSysColorOverride[32] = {};

static constexpr UINT_PTR kContainerSubclassId = 0x5745444D;
static constexpr UINT_PTR kHeaderSubclassId    = 0x57454448;
static constexpr UINT_PTR kTabSubclassId       = 0x57454454;
static constexpr UINT_PTR kButtonSubclassId    = 0x57454442;
static constexpr UINT_PTR kUpDownSubclassId    = 0x57454455;
static constexpr UINT_PTR kComboSubclassId     = 0x57454443;
static constexpr UINT_PTR kStaticSubclassId    = 0x57454453;
static constexpr UINT_PTR kScintillaSubclassId = 0x5745444A;

static thread_local bool gInThemeRedirect    = false;
static thread_local bool gInStyleWindow      = false;
static thread_local bool gInInternalDraw     = false;
static thread_local bool gInSubclassInternal = false;
static thread_local bool tRetroStyled        = false;

struct ActiveMsgFrame { HWND h; UINT m; UINT_PTR id; };
static thread_local ActiveMsgFrame tContainerActive[32] = {};
static thread_local int            tContainerDepth      = 0;

static wchar_t gGuardPath[MAX_PATH] = {};
static volatile LONG gGuardLogCount = 0;

// Debug only (needs WE_DarkMode\debug.flag): records the window/message that tripped a re-entrancy guard -> guard_diag.txt
static void LogGuardHit(const char* tag, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!gGuardPath[0] || InterlockedIncrement(&gGuardLogCount) > 24) return;
    HANDLE h = CreateFileW(gGuardPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    wchar_t cls[64] = {}, pcls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    HWND par = GetParent(hwnd);
    if (par) GetClassNameW(par, pcls, 64);
    char ca[64] = {}, pa[64] = {};
    WideCharToMultiByte(CP_ACP, 0, cls, -1, ca, 63, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, pcls, -1, pa, 63, nullptr, nullptr);
    char line[360];
    wsprintfA(line, "%s hwnd=%p class='%s' parent=%p '%s' style=%08lX msg=0x%04X wp=%p lp=%p depth=%d active:",
              tag, hwnd, ca, par, pa, static_cast<unsigned long>(GetWindowLongPtrW(hwnd, GWL_STYLE)),
              msg, reinterpret_cast<void*>(wParam), reinterpret_cast<void*>(lParam), tContainerDepth);
    DWORD n = 0;
    WriteFile(h, line, lstrlenA(line), &n, nullptr);
    for (int i = 0; i < tContainerDepth && i < 12; ++i) {
        wsprintfA(line, " %p/0x%04X", tContainerActive[i].h, tContainerActive[i].m);
        WriteFile(h, line, lstrlenA(line), &n, nullptr);
    }
    WriteFile(h, "\r\n", 2, &n, nullptr);
    CloseHandle(h);
}

using fnGetWindowSubclass = BOOL(WINAPI*)(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR*);

static BOOL SafeQueryWindowSubclass(HWND hwnd, SUBCLASSPROC pfn, UINT_PTR uId, DWORD_PTR* pRef) {
    static fnGetWindowSubclass sFn = nullptr;
    static bool sResolved = false;
    if (!sResolved) {
        HMODULE hCc = GetModuleHandleW(L"comctl32.dll");
        if (!hCc) hCc = LoadLibraryW(L"comctl32.dll");
        if (hCc) {
            // In non-manifested ComCtl32 v5.82 (used by worldedit.exe / worldedit121.exe),
            // GetWindowSubclass is exported ONLY by ordinal 411, NOT by name!
            sFn = reinterpret_cast<fnGetWindowSubclass>(GetProcAddress(hCc, "GetWindowSubclass"));
            if (!sFn) {
                sFn = reinterpret_cast<fnGetWindowSubclass>(GetProcAddress(hCc, MAKEINTRESOURCEA(411)));
            }
        }
        sResolved = true;
    }
    return sFn ? sFn(hwnd, pfn, uId, pRef) : FALSE;
}

static void SafeSetWindowSubclass(HWND hwnd, SUBCLASSPROC pfn, UINT_PTR uId, DWORD_PTR dwRef) {
    if (!hwnd || !IsWindow(hwnd)) return;
    DWORD_PTR existing = 0;
    if (SafeQueryWindowSubclass(hwnd, pfn, uId, &existing)) {
        return; // Already attached: NEVER call SetWindowSubclass again (avoids ComCtl32 GWL_WNDPROC cycles!)
    }
    SetWindowSubclass(hwnd, pfn, uId, dwRef);
}

// ============================================================================
// INI & Palette Loader
// ============================================================================
static COLORREF ParseHexRgb(const wchar_t* text, COLORREF fallback) {
    if (!text || !*text) return fallback;
    while (*text == L' ' || *text == L'#') ++text;
    wchar_t* end = nullptr;
    unsigned long val = wcstoul(text, &end, 16);
    if (end == text) return fallback;
    return RGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
}

static COLORREF ReadIniColor(const wchar_t* iniPath, const wchar_t* key, COLORREF fallback) {
    wchar_t buf[64] = {};
    DWORD n = GetPrivateProfileStringW(L"Colors", key, L"", buf, 64, iniPath);
    return n > 0 ? ParseHexRgb(buf, fallback) : fallback;
}

static void LoadPaletteAndBrushes() {
    wchar_t dllPath[MAX_PATH] = {};
    if (GetModuleFileNameW(gSelfModule, dllPath, MAX_PATH) > 0) {
        PathRemoveFileSpecW(dllPath);
        wchar_t iniPath[MAX_PATH] = {};
        PathCombineW(iniPath, dllPath, L"WE_DarkMode.ini");
        if (GetFileAttributesW(iniPath) != INVALID_FILE_ATTRIBUTES) {
            gColBg        = ReadIniColor(iniPath, L"BgColor",        gColBg);
            gColSurface   = ReadIniColor(iniPath, L"SurfaceColor",   gColSurface);
            gColInput     = ReadIniColor(iniPath, L"InputColor",     gColInput);
            gColText      = ReadIniColor(iniPath, L"TextColor",      gColText);
            gColMutedText = ReadIniColor(iniPath, L"MutedTextColor", gColMutedText);
            gColAccent    = ReadIniColor(iniPath, L"AccentColor",    gColAccent);
            gColHover     = ReadIniColor(iniPath, L"HoverColor",     gColHover);
            gColBorder    = ReadIniColor(iniPath, L"BorderColor",    gColBorder);
        }
    }

    gBrushBg      = CreateSolidBrush(gColBg);
    gBrushSurface = CreateSolidBrush(gColSurface);
    gBrushInput   = CreateSolidBrush(gColInput);
    gBrushButton  = CreateSolidBrush(gColButton);
    gBrushAccent  = CreateSolidBrush(gColAccent);
    gBrushHover   = CreateSolidBrush(gColHover);
    gBrushBorder  = CreateSolidBrush(gColBorder);

    auto setSys = [](int idx, COLORREF col) {
        if (idx >= 0 && idx < 32) {
            gSysColors[idx] = col;
            gSysBrushes[idx] = CreateSolidBrush(col);
            gSysColorOverride[idx] = true;
        }
    };

    setSys(COLOR_SCROLLBAR,               gColSurface);
    setSys(COLOR_BACKGROUND,              gColBg);
    setSys(COLOR_ACTIVECAPTION,           gColSurface);
    setSys(COLOR_INACTIVECAPTION,         gColBg);
    setSys(COLOR_MENU,                    gColSurface);
    setSys(COLOR_WINDOW,                  gColSurface);
    setSys(COLOR_WINDOWFRAME,             gColBorder);
    setSys(COLOR_MENUTEXT,                gColText);
    setSys(COLOR_WINDOWTEXT,              gColText);
    setSys(COLOR_CAPTIONTEXT,             gColText);
    setSys(COLOR_ACTIVEBORDER,            gColBorder);
    setSys(COLOR_INACTIVEBORDER,          gColBg);
    setSys(COLOR_APPWORKSPACE,            gColDarkShadow);
    setSys(COLOR_HIGHLIGHT,               gColAccent);
    setSys(COLOR_HIGHLIGHTTEXT,           RGB(0xFF, 0xFF, 0xFF));
    setSys(COLOR_BTNFACE,                 gColBg);
    setSys(COLOR_BTNSHADOW,               gColBorder);
    setSys(COLOR_GRAYTEXT,                gColMutedText);
    setSys(COLOR_BTNTEXT,                 gColText);
    setSys(COLOR_INACTIVECAPTIONTEXT,     gColMutedText);
    setSys(COLOR_BTNHIGHLIGHT,            gColBorder);
    setSys(COLOR_3DDKSHADOW,              gColDarkShadow);
    setSys(COLOR_3DLIGHT,                 gColSurface);
    setSys(COLOR_INFOTEXT,                gColText);
    setSys(COLOR_INFOBK,                  gColSurface);
    setSys(COLOR_HOTLIGHT,                RGB(0x66, 0xB2, 0xFF));
    setSys(COLOR_GRADIENTACTIVECAPTION,   gColSurface);
    setSys(COLOR_GRADIENTINACTIVECAPTION, gColBg);
    setSys(COLOR_MENUHILIGHT,             gColAccent);
    setSys(COLOR_MENUBAR,                 gColSurface);
}

static int RawFillRect(HDC hdc, const RECT* rc, HBRUSH hbr) {
    if (gHpFillRect.origEntry) {
        return reinterpret_cast<fnFillRect>(gHpFillRect.origEntry)(hdc, rc, hbr);
    }
    return FillRect(hdc, rc, hbr);
}

static void Draw1PxBorder(HDC hdc, const RECT& rc, COLORREF col) {
    HBRUSH br = CreateSolidBrush(col);
    RECT t = { rc.left, rc.top, rc.right, rc.top + 1 };
    RECT b = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
    RECT l = { rc.left, rc.top, rc.left + 1, rc.bottom };
    RECT r = { rc.right - 1, rc.top, rc.right, rc.bottom };
    RawFillRect(hdc, &t, br);
    RawFillRect(hdc, &b, br);
    RawFillRect(hdc, &l, br);
    RawFillRect(hdc, &r, br);
    DeleteObject(br);
}

// ============================================================================
// Dark Readability Helper (fixes hardcoded black / dark purple / dark blue text)
// ============================================================================
static COLORREF LiftColorForDarkBg(COLORREF col) {
    if (col == CLR_DEFAULT || col == CLR_NONE)
        return gColText;

    BYTE r = GetRValue(col);
    BYTE g = GetGValue(col);
    BYTE b = GetBValue(col);
    int luma = (r * 299 + g * 587 + b * 114) / 1000;

    if (r < 45 && g < 45 && b < 45)
        return gColText;

    // Dark purple/magenta (WC3 Object Editor modified fields) -> bright lavender
    if (r > 70 && b > 70 && g < 60 && luma < 120)
        return RGB(0xDF, 0x8F, 0xFF);

    // Dark blue (WC3 Trigger Editor links) -> bright sky blue
    if (b > 90 && r < 70 && g < 90 && luma < 130)
        return RGB(0x6C, 0xB8, 0xFF);

    // Dark red -> readable coral red
    if (r > 90 && g < 60 && b < 60 && luma < 120)
        return RGB(0xFF, 0x70, 0x70);

    // Dark green (Trigger comments) -> readable mint green
    if (g > 70 && r < 60 && b < 60 && luma < 130)
        return RGB(0x6A, 0xD8, 0x7A);

    if (luma < 95) {
        int nr = r + 115; if (nr > 235) nr = 235;
        int ng = g + 115; if (ng > 235) ng = 235;
        int nb = b + 115; if (nb > 235) nb = 235;
        return RGB(nr, ng, nb);
    }

    return col;
}

// ============================================================================
// Alpha-Aware Toolbar Icon Renderer (fixes black-shadow disabled icons & black outlines)
// ============================================================================
static void DrawToolbarIconSmart(HDC hdcDest, int destX, int destY, HIMAGELIST hImg, int imgIdx, bool enabled, COLORREF bgCol) {
    int cx = 16, cy = 16;
    if (!ImageList_GetIconSize(hImg, &cx, &cy) || cx <= 0 || cy <= 0 || cx > 64 || cy > 64) {
        ImageList_Draw(hImg, imgIdx, hdcDest, destX, destY, ILD_TRANSPARENT);
        return;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = cx;
    bmi.bmiHeader.biHeight      = -cy; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    uint32_t* pxBlack = nullptr;
    uint32_t* pxWhite = nullptr;
    HDC memDC = CreateCompatibleDC(hdcDest);
    HBITMAP hbmBlack = CreateDIBSection(hdcDest, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pxBlack), nullptr, 0);
    HBITMAP hbmWhite = CreateDIBSection(hdcDest, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pxWhite), nullptr, 0);

    if (!memDC || !hbmBlack || !hbmWhite || !pxBlack || !pxWhite) {
        if (hbmBlack) DeleteObject(hbmBlack);
        if (hbmWhite) DeleteObject(hbmWhite);
        if (memDC) DeleteDC(memDC);
        ImageList_Draw(hImg, imgIdx, hdcDest, destX, destY, ILD_TRANSPARENT);
        return;
    }

    const int count = cx * cy;
    memset(pxBlack, 0x00, count * sizeof(uint32_t));
    for (int i = 0; i < count; ++i) pxWhite[i] = 0x00FFFFFF;

    gInInternalDraw = true;
    HGDIOBJ oldBm = SelectObject(memDC, hbmBlack);
    ImageList_Draw(hImg, imgIdx, memDC, 0, 0, ILD_TRANSPARENT);
    SelectObject(memDC, hbmWhite);
    ImageList_Draw(hImg, imgIdx, memDC, 0, 0, ILD_TRANSPARENT);
    gInInternalDraw = false;

    // Check if an enabled icon is pure near-black monochrome (like the dashed Selection box)
    bool allDarkMono = true;
    int fgPixels = 0;
    for (int i = 0; i < count; ++i) {
        uint32_t b = pxBlack[i] & 0x00FFFFFF;
        uint32_t w = pxWhite[i] & 0x00FFFFFF;
        int bR = (b >> 16) & 0xFF, bG = (b >> 8) & 0xFF, bB = b & 0xFF;
        int wR = (w >> 16) & 0xFF, wG = (w >> 8) & 0xFF, wB = w & 0xFF;
        int alpha = 255 - ((wR - bR) + (wG - bG) + (wB - bB)) / 3;
        if (alpha > 20) {
            ++fgPixels;
            if (bR > 55 || bG > 55 || bB > 55) {
                allDarkMono = false;
            }
        }
    }

    const int bgR = GetRValue(bgCol);
    const int bgG = GetGValue(bgCol);
    const int bgB = GetBValue(bgCol);

    for (int i = 0; i < count; ++i) {
        uint32_t b = pxBlack[i] & 0x00FFFFFF;
        uint32_t w = pxWhite[i] & 0x00FFFFFF;
        int bR = (b >> 16) & 0xFF, bG = (b >> 8) & 0xFF, bB = b & 0xFF;
        int wR = (w >> 16) & 0xFF, wG = (w >> 8) & 0xFF, wB = w & 0xFF;
        int alpha = 255 - ((wR - bR) + (wG - bG) + (wB - bB)) / 3;
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;

        if (alpha <= 12) {
            pxBlack[i] = (bgR << 16) | (bgG << 8) | bgB;
            continue;
        }

        // Recover unpremultiplied icon RGB
        int srcR = (alpha > 0) ? (bR * 255) / alpha : 0;
        int srcG = (alpha > 0) ? (bG * 255) / alpha : 0;
        int srcB = (alpha > 0) ? (bB * 255) / alpha : 0;
        if (srcR > 255) srcR = 255;
        if (srcG > 255) srcG = 255;
        if (srcB > 255) srcB = 255;

        int outR = srcR, outG = srcG, outB = srcB;
        if (!enabled) {
            // Disabled toolbar icon: clear, recognizable muted silver + 30% original color tint
            // Lifts black/dark-gray strokes (Cut scissors, Undo/Redo arrows) so they never look like shadows!
            int luma = (srcR * 299 + srcG * 587 + srcB * 114) / 1000;
            int lifted = 148 + (luma * 52) / 255; // 148..200 crisp visible silver range on #181818
            outR = (lifted * 78 + srcR * 22) / 100;
            outG = (lifted * 78 + srcG * 22) / 100;
            outB = (lifted * 78 + srcB * 22) / 100;
            alpha = (alpha * 215) / 255;
        } else if (fgPixels > 0 && allDarkMono) {
            // Pure-black monochrome icon (e.g. Selection marquee box) -> crisp light gray
            outR = 0xDC;
            outG = 0xDC;
            outB = 0xE0;
        }

        int finR = (outR * alpha + bgR * (255 - alpha)) / 255;
        int finG = (outG * alpha + bgG * (255 - alpha)) / 255;
        int finB = (outB * alpha + bgB * (255 - alpha)) / 255;
        pxBlack[i] = (finR << 16) | (finG << 8) | finB;
    }

    SelectObject(memDC, hbmBlack);
    BitBlt(hdcDest, destX, destY, cx, cy, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBm);

    DeleteObject(hbmBlack);
    DeleteObject(hbmWhite);
    DeleteDC(memDC);
}

// ============================================================================
// Dark Title Bar + UAH Menu Bar & Left-Aligned Popup Menus
// ============================================================================
static void EnableDarkTitleBar(HWND hwnd) {
    if (gAllowDarkModeForWindow) {
        gAllowDarkModeForWindow(hwnd, TRUE);
    }
    if (gDwmSetWindowAttribute) {
        BOOL dark = TRUE;
        if (FAILED(gDwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &dark, sizeof(dark)))) {
            gDwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkModeBefore20H1, &dark, sizeof(dark));
        }
    }
}

static void DrawMenuBarBottomHairline(HWND hwnd) {
    MENUBARINFO mbi = {};
    mbi.cbSize = sizeof(mbi);
    if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
        return;

    RECT rcWin = {};
    GetWindowRect(hwnd, &rcWin);
    RECT rcLine = mbi.rcBar;
    OffsetRect(&rcLine, -rcWin.left, -rcWin.top);
    rcLine.top = rcLine.bottom;
    rcLine.bottom = rcLine.top + 1;

    HDC hdc = GetWindowDC(hwnd);
    if (hdc) {
        RawFillRect(hdc, &rcLine, gBrushBorder);
        ReleaseDC(hwnd, hdc);
    }
}

static bool HandleUahMenuMessage(HWND hwnd, UINT msg, WPARAM /*wParam*/, LPARAM lParam, LRESULT* lr) {
    HMENU hMainMenu = GetMenu(hwnd);

    if (msg == kWmUahDrawMenu) {
        auto* pUDM = reinterpret_cast<UAHMENU*>(lParam);
        if (!pUDM || !pUDM->hdc) return false;

        if (hMainMenu != nullptr && pUDM->hmenu == hMainMenu) {
            MENUBARINFO mbi = {};
            mbi.cbSize = sizeof(mbi);
            if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
                return false;

            RECT rcWin = {};
            GetWindowRect(hwnd, &rcWin);
            RECT rcBar = mbi.rcBar;
            OffsetRect(&rcBar, -rcWin.left, -rcWin.top);
            RawFillRect(pUDM->hdc, &rcBar, gBrushSurface);
        } else {
            RECT rcClip = {};
            GetClipBox(pUDM->hdc, &rcClip);
            RawFillRect(pUDM->hdc, &rcClip, gBrushSurface);
        }
        *lr = 0;
        return true;
    }

    if (msg == kWmUahDrawMenuItem) {
        auto* pUDMI = reinterpret_cast<UAHDRAWMENUITEM*>(lParam);
        if (!pUDMI || !pUDMI->um.hdc) return false;

        const bool isMenuBar = (hMainMenu != nullptr && pUDMI->um.hmenu == hMainMenu);
        HDC hdc = pUDMI->um.hdc;
        RECT rc = pUDMI->dis.rcItem;

        wchar_t menuText[256] = {};
        MENUITEMINFOW mii = {};
        mii.cbSize     = sizeof(mii);
        mii.fMask      = MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING;
        mii.dwTypeData = menuText;
        mii.cch        = 255;
        GetMenuItemInfoW(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);

        UINT state = pUDMI->dis.itemState;
        bool disabled = (state & (ODS_DISABLED | ODS_GRAYED)) != 0 || (mii.fState & (MFS_DISABLED | MFS_GRAYED)) != 0;
        bool inactive = (state & ODS_INACTIVE) != 0;
        bool selected = (state & ODS_SELECTED) != 0;
        bool hot      = (state & ODS_HOTLIGHT) != 0;

        if (isMenuBar) {
            HBRUSH bg = gBrushSurface;
            if (selected) bg = gBrushAccent;
            else if (hot) bg = gBrushHover;
            RawFillRect(hdc, &rc, bg);

            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            HFONT hFont = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)
                              ? CreateFontIndirectW(&ncm.lfMenuFont) : nullptr;
            HFONT oldFont = hFont ? static_cast<HFONT>(SelectObject(hdc, hFont)) : nullptr;

            int oldBk = SetBkMode(hdc, TRANSPARENT);
            COLORREF oldTxt = SetTextColor(hdc, (disabled || inactive) ? gColMutedText : gColText);
            DWORD dtFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER | ((state & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
            DrawTextW(hdc, menuText, -1, &rc, dtFlags);

            SetTextColor(hdc, oldTxt);
            SetBkMode(hdc, oldBk);
            if (oldFont) SelectObject(hdc, oldFont);
            if (hFont) DeleteObject(hFont);
        } else {
            // Popup dropdown / context menu: left-aligned text + right-aligned shortcut + separators + checks
            RawFillRect(hdc, &rc, gBrushSurface);

            if ((mii.fType & MFT_SEPARATOR) != 0) {
                int midY = (rc.top + rc.bottom) / 2;
                RECT rcSep = { rc.left + 28, midY, rc.right - 8, midY + 1 };
                RawFillRect(hdc, &rcSep, gBrushBorder);
                *lr = 0;
                return true;
            }

            if ((selected || hot) && !disabled) {
                RECT rcSel = { rc.left + 2, rc.top + 1, rc.right - 2, rc.bottom - 1 };
                RawFillRect(hdc, &rcSel, gBrushAccent);
            } else if (selected || hot) {
                RECT rcSel = { rc.left + 2, rc.top + 1, rc.right - 2, rc.bottom - 1 };
                RawFillRect(hdc, &rcSel, gBrushHover);
            }

            // Checkmark or Radio bullet in left gutter
            if ((mii.fState & MFS_CHECKED) != 0) {
                int cx = rc.left + 14;
                int cy = (rc.top + rc.bottom) / 2;
                COLORREF chkCol = disabled ? gColMutedText : RGB(0xFF, 0xFF, 0xFF);
                if ((mii.fType & MFT_RADIOCHECK) != 0) {
                    HBRUSH br = CreateSolidBrush(chkCol);
                    HPEN pen = CreatePen(PS_SOLID, 1, chkCol);
                    HGDIOBJ oB = SelectObject(hdc, br);
                    HGDIOBJ oP = SelectObject(hdc, pen);
                    Ellipse(hdc, cx - 3, cy - 3, cx + 3, cy + 3);
                    SelectObject(hdc, oB);
                    SelectObject(hdc, oP);
                    DeleteObject(br);
                    DeleteObject(pen);
                } else {
                    HPEN pen = CreatePen(PS_SOLID, 2, chkCol);
                    HGDIOBJ oP = SelectObject(hdc, pen);
                    MoveToEx(hdc, cx - 5, cy, nullptr);
                    LineTo(hdc, cx - 2, cy + 3);
                    LineTo(hdc, cx + 5, cy - 4);
                    SelectObject(hdc, oP);
                    DeleteObject(pen);
                }
            }

            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            HFONT hFont = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)
                              ? CreateFontIndirectW(&ncm.lfMenuFont) : nullptr;
            HFONT oldFont = hFont ? static_cast<HFONT>(SelectObject(hdc, hFont)) : nullptr;
            int oldBk = SetBkMode(hdc, TRANSPARENT);

            wchar_t* tabPos = wcschr(menuText, L'\t');
            if (tabPos) {
                *tabPos = L'\0';
                ++tabPos;
            }

            RECT rcLeft = { rc.left + 28, rc.top, rc.right - 22, rc.bottom };
            COLORREF mainCol = disabled ? gColMutedText : ((selected || hot) ? RGB(0xFF, 0xFF, 0xFF) : gColText);
            COLORREF oldTxt = SetTextColor(hdc, mainCol);
            DWORD leftFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | ((state & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
            DrawTextW(hdc, menuText, -1, &rcLeft, leftFlags);

            if (tabPos && *tabPos) {
                RECT rcRight = { rc.left + 28, rc.top, rc.right - 22, rc.bottom };
                SetTextColor(hdc, disabled ? gColMutedText : ((selected || hot) ? RGB(0xDC, 0xE6, 0xF2) : gColMutedText));
                DrawTextW(hdc, tabPos, -1, &rcRight, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            SetTextColor(hdc, oldTxt);
            SetBkMode(hdc, oldBk);
            if (oldFont) SelectObject(hdc, oldFont);
            if (hFont) DeleteObject(hFont);

            // Submenu arrow (and clip exclusion so Windows doesn't paint a black arrow on top)
            if (mii.hSubMenu != nullptr) {
                int ax = rc.right - 12;
                int ay = (rc.top + rc.bottom) / 2;
                COLORREF arrCol = disabled ? gColMutedText : ((selected || hot) ? RGB(0xFF, 0xFF, 0xFF) : gColText);
                POINT pts[3] = { { ax - 2, ay - 4 }, { ax + 2, ay }, { ax - 2, ay + 4 } };
                HBRUSH br = CreateSolidBrush(arrCol);
                HPEN pen = CreatePen(PS_SOLID, 1, arrCol);
                HGDIOBJ oB = SelectObject(hdc, br);
                HGDIOBJ oP = SelectObject(hdc, pen);
                Polygon(hdc, pts, 3);
                SelectObject(hdc, oB);
                SelectObject(hdc, oP);
                DeleteObject(br);
                DeleteObject(pen);
                ExcludeClipRect(hdc, rc.right - 20, rc.top, rc.right, rc.bottom);
            }
        }

        *lr = 0;
        return true;
    }

    return false;
}

// ============================================================================
// Dark Classic Button / CheckBox / Radio / GroupBox & UpDown Spin Subclasses
// ============================================================================
static bool IsImageOrOwnerDrawButton(HWND hwnd, LONG_PTR style) {
    LONG_PTR btnType = style & BS_TYPEMASK;
    if (btnType == BS_OWNERDRAW) return true;
    if ((style & (BS_BITMAP | BS_ICON)) != 0) return true;
    if (DefSubclassProc(hwnd, BM_GETIMAGE, IMAGE_BITMAP, 0) != 0) return true;
    if (DefSubclassProc(hwnd, BM_GETIMAGE, IMAGE_ICON, 0) != 0) return true;
    return false;
}

static LRESULT CALLBACK DarkButtonSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    if (msg == WM_NCDESTROY) {
        if (gHoveredCtrl == hwnd) gHoveredCtrl = nullptr;
        gInSubclassInternal = true;
        RemoveWindowSubclass(hwnd, DarkButtonSubclassProc, kButtonSubclassId);
        gInSubclassInternal = false;
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    // Fast path for non-visual messages to avoid any re-entrancy during creation or queries
    if (msg != WM_PAINT && msg != WM_ERASEBKGND && msg != WM_MOUSEMOVE && msg != WM_MOUSELEAVE &&
        msg != BM_SETSTATE && msg != BM_SETCHECK && msg != WM_ENABLE &&
        msg != WM_SETFOCUS && msg != WM_KILLFOCUS && msg != WM_SETTEXT)
    {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (IsImageOrOwnerDrawButton(hwnd, style)) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    LONG_PTR btnType = style & BS_TYPEMASK;

    switch (msg) {
        case WM_MOUSEMOVE: {
            if (gHoveredCtrl != hwnd) {
                HWND prev = gHoveredCtrl;
                gHoveredCtrl = hwnd;
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, nullptr, FALSE);
                if (prev && IsWindow(prev)) InvalidateRect(prev, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (gHoveredCtrl == hwnd) {
                gHoveredCtrl = nullptr;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }
        case BM_SETSTATE:
        case BM_SETCHECK:
        case WM_ENABLE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_SETTEXT: {
            LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return r;
        }
        case WM_ERASEBKGND:
            if (btnType != BS_GROUPBOX) return 1;
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc = {};
            GetClientRect(hwnd, &rc);

            bool disabled = !IsWindowEnabled(hwnd);
            bool hovered  = (gHoveredCtrl == hwnd) && !disabled;
            LRESULT bState = DefSubclassProc(hwnd, BM_GETSTATE, 0, 0);
            bool pushed   = (bState & BST_PUSHED) != 0;
            bool checked  = (bState & BST_CHECKED) != 0;
            bool indet    = (bState & BST_INDETERMINATE) != 0;
            bool focused  = (GetFocus() == hwnd);

            wchar_t text[512] = {};
            GetWindowTextW(hwnd, text, 511);
            if (text[0] == L'\0') {
                auto baseProc = reinterpret_cast<WNDPROC>(GetClassLongPtrW(hwnd, GCLP_WNDPROC));
                if (baseProc) {
                    CallWindowProcW(baseProc, hwnd, WM_GETTEXT, 511, reinterpret_cast<LPARAM>(text));
                }
            }

            HFONT hFont = reinterpret_cast<HFONT>(DefSubclassProc(hwnd, WM_GETFONT, 0, 0));
            if (!hFont) hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, hFont));
            int oldBk = SetBkMode(hdc, TRANSPARENT);

            if (btnType == BS_GROUPBOX) {
                SIZE sz = {};
                if (text[0]) GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &sz);
                int topY = (sz.cy > 0) ? (sz.cy / 2) : 6;
                RECT rcBox = { rc.left, rc.top + topY, rc.right, rc.bottom };

                int saved = SaveDC(hdc);
                if (text[0]) {
                    ExcludeClipRect(hdc, rc.left + 7, rc.top, rc.left + 13 + sz.cx, rc.top + sz.cy + 2);
                }
                Draw1PxBorder(hdc, rcBox, gColBorder);
                RestoreDC(hdc, saved);

                if (text[0]) {
                    RECT rcCap = { rc.left + 9, rc.top, rc.left + 13 + sz.cx, rc.top + sz.cy + 2 };
                    RawFillRect(hdc, &rcCap, gBrushBg);
                    SetTextColor(hdc, disabled ? gColMutedText : gColText);
                    DrawTextW(hdc, text, -1, &rcCap, DT_LEFT | DT_TOP | DT_SINGLELINE);
                }
            } else if (btnType == BS_CHECKBOX || btnType == BS_AUTOCHECKBOX ||
                       btnType == BS_3STATE   || btnType == BS_AUTO3STATE) {
                RawFillRect(hdc, &rc, gBrushBg);
                int boxSz = 13;
                int bx = rc.left + 2;
                int by = rc.top + ((rc.bottom - rc.top) - boxSz) / 2;
                RECT rcBox = { bx, by, bx + boxSz, by + boxSz };

                if ((checked || indet) && !disabled) {
                    RawFillRect(hdc, &rcBox, gBrushAccent);
                    Draw1PxBorder(hdc, rcBox, RGB(0x4C, 0x88, 0xC8));
                } else {
                    RawFillRect(hdc, &rcBox, gBrushInput);
                    Draw1PxBorder(hdc, rcBox, hovered ? RGB(0x78, 0x78, 0x82) : RGB(0x52, 0x52, 0x5A));
                }

                if (checked) {
                    HPEN pen = CreatePen(PS_SOLID, 2, disabled ? gColMutedText : RGB(0xFF, 0xFF, 0xFF));
                    HGDIOBJ oP = SelectObject(hdc, pen);
                    MoveToEx(hdc, bx + 3, by + 6, nullptr);
                    LineTo(hdc, bx + 5, by + 9);
                    LineTo(hdc, bx + 10, by + 3);
                    SelectObject(hdc, oP);
                    DeleteObject(pen);
                } else if (indet) {
                    RECT rcMid = { bx + 3, by + 3, bx + boxSz - 3, by + boxSz - 3 };
                    RawFillRect(hdc, &rcMid, disabled ? gBrushBorder : static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                }

                RECT rcTxt = { bx + boxSz + 5, rc.top, rc.right, rc.bottom };
                SetTextColor(hdc, disabled ? gColMutedText : gColText);
                DWORD dt = DT_LEFT | DT_VCENTER | ((style & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE);
                DrawTextW(hdc, text, -1, &rcTxt, dt);
            } else if (btnType == BS_RADIOBUTTON || btnType == BS_AUTORADIOBUTTON) {
                RawFillRect(hdc, &rc, gBrushBg);
                int boxSz = 13;
                int bx = rc.left + 2;
                int by = rc.top + ((rc.bottom - rc.top) - boxSz) / 2;

                HPEN pen = CreatePen(PS_SOLID, 1, hovered ? RGB(0x78, 0x78, 0x82) : RGB(0x56, 0x56, 0x60));
                HGDIOBJ oP = SelectObject(hdc, pen);
                HGDIOBJ oB = SelectObject(hdc, gBrushInput);
                Ellipse(hdc, bx, by, bx + boxSz, by + boxSz);

                if (checked) {
                    HBRUSH dotBr = CreateSolidBrush(disabled ? gColMutedText : RGB(0x58, 0xA6, 0xFF));
                    HPEN dotPen  = CreatePen(PS_SOLID, 1, disabled ? gColMutedText : RGB(0x58, 0xA6, 0xFF));
                    SelectObject(hdc, dotBr);
                    SelectObject(hdc, dotPen);
                    Ellipse(hdc, bx + 3, by + 3, bx + boxSz - 3, by + boxSz - 3);
                    SelectObject(hdc, oB);
                    SelectObject(hdc, oP);
                    DeleteObject(dotBr);
                    DeleteObject(dotPen);
                } else {
                    SelectObject(hdc, oB);
                    SelectObject(hdc, oP);
                }
                DeleteObject(pen);

                RECT rcTxt = { bx + boxSz + 5, rc.top, rc.right, rc.bottom };
                SetTextColor(hdc, disabled ? gColMutedText : gColText);
                DrawTextW(hdc, text, -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else {
                // PushButton / DefPushButton / PushLike
                COLORREF faceCol = gColButton;
                COLORREF brdCol  = (btnType == BS_DEFPUSHBUTTON || focused) ? RGB(0x3A, 0x72, 0xA8) : gColBorder;
                COLORREF txtCol  = gColText;

                if (disabled) {
                    faceCol = RGB(0x20, 0x20, 0x22);
                    brdCol  = RGB(0x30, 0x30, 0x34);
                    txtCol  = gColMutedText;
                } else if (pushed || checked) {
                    faceCol = gColAccent;
                    brdCol  = RGB(0x4C, 0x88, 0xC8);
                    txtCol  = RGB(0xFF, 0xFF, 0xFF);
                } else if (hovered) {
                    faceCol = gColHover;
                    brdCol  = RGB(0x56, 0x56, 0x62);
                }

                HBRUSH faceBr = CreateSolidBrush(faceCol);
                RawFillRect(hdc, &rc, faceBr);
                DeleteObject(faceBr);
                Draw1PxBorder(hdc, rc, brdCol);

                RECT rcTxt = rc;
                if (pushed) OffsetRect(&rcTxt, 1, 1);
                SetTextColor(hdc, txtCol);
                DWORD dt = DT_CENTER | DT_VCENTER | ((style & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE);
                DrawTextW(hdc, text, -1, &rcTxt, dt);
            }

            SetBkMode(hdc, oldBk);
            SelectObject(hdc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCDESTROY:
            if (gHoveredCtrl == hwnd) gHoveredCtrl = nullptr;
            RemoveWindowSubclass(hwnd, DarkButtonSubclassProc, kButtonSubclassId);
            break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK DarkUpDownSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    if (msg == WM_ERASEBKGND) return 1;

    if (msg == WM_PAINT) {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);

        bool horiz    = (GetWindowLongPtrW(hwnd, GWL_STYLE) & UDS_HORZ) != 0;
        bool disabled = !IsWindowEnabled(hwnd);

        RawFillRect(hdc, &rc, gBrushButton);
        Draw1PxBorder(hdc, rc, gColBorder);

        COLORREF arrCol = disabled ? gColMutedText : gColText;
        HBRUSH br = CreateSolidBrush(arrCol);
        HPEN pen  = CreatePen(PS_SOLID, 1, arrCol);
        HGDIOBJ oB = SelectObject(hdc, br);
        HGDIOBJ oP = SelectObject(hdc, pen);

        if (horiz) {
            int midX = (rc.left + rc.right) / 2;
            int cy   = (rc.top + rc.bottom) / 2;
            RECT sep = { midX, rc.top, midX + 1, rc.bottom };
            RawFillRect(hdc, &sep, gBrushBorder);

            int lx = (rc.left + midX) / 2;
            int rx = (midX + rc.right) / 2;
            POINT pL[3] = { { lx + 2, cy - 3 }, { lx - 2, cy }, { lx + 2, cy + 3 } };
            POINT pR[3] = { { rx - 2, cy - 3 }, { rx + 2, cy }, { rx - 2, cy + 3 } };
            Polygon(hdc, pL, 3);
            Polygon(hdc, pR, 3);
        } else {
            int midY = (rc.top + rc.bottom) / 2;
            int cx   = (rc.left + rc.right) / 2;
            RECT sep = { rc.left, midY, rc.right, midY + 1 };
            RawFillRect(hdc, &sep, gBrushBorder);

            int ty = (rc.top + midY) / 2;
            int by = (midY + rc.bottom) / 2;
            POINT pU[3] = { { cx - 3, ty + 2 }, { cx, ty - 2 }, { cx + 3, ty + 2 } };
            POINT pD[3] = { { cx - 3, by - 2 }, { cx, by + 2 }, { cx + 3, by - 2 } };
            Polygon(hdc, pU, 3);
            Polygon(hdc, pD, 3);
        }

        SelectObject(hdc, oB);
        SelectObject(hdc, oP);
        DeleteObject(br);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }

    if (msg == WM_NCDESTROY) {
        gInSubclassInternal = true;
        RemoveWindowSubclass(hwnd, DarkUpDownSubclassProc, kUpDownSubclassId);
        gInSubclassInternal = false;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK DarkComboBoxSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (gHoveredCtrl != hwnd) {
                HWND prev = gHoveredCtrl;
                gHoveredCtrl = hwnd;
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, nullptr, FALSE);
                if (prev && IsWindow(prev)) InvalidateRect(prev, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (gHoveredCtrl == hwnd) {
                gHoveredCtrl = nullptr;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE: {
            LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return r;
        }
        case WM_PAINT: {
            LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
            HDC hdc = GetDC(hwnd);
            if (hdc) {
                RECT rc = {};
                GetClientRect(hwnd, &rc);
                COMBOBOXINFO cbi = {};
                cbi.cbSize = sizeof(cbi);
                bool hasCbi = GetComboBoxInfo(hwnd, &cbi) != FALSE;

                bool disabled = !IsWindowEnabled(hwnd);
                bool pressed  = hasCbi && (cbi.stateButton == STATE_SYSTEM_PRESSED);
                bool focused  = (GetFocus() == hwnd) || (hasCbi && cbi.hwndItem && GetFocus() == cbi.hwndItem) || pressed;

                RECT rcInner = { rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1 };
                Draw1PxBorder(hdc, rcInner, gColInput);
                Draw1PxBorder(hdc, rc, focused ? RGB(0x3A, 0x72, 0xA8) : gColBorder);

                if (hasCbi && (cbi.stateButton & STATE_SYSTEM_INVISIBLE) == 0 && cbi.rcButton.right > cbi.rcButton.left) {
                    RECT rcBtn = { cbi.rcButton.left - 1, rc.top + 1, rc.right - 1, rc.bottom - 1 };
                    HBRUSH btnBr = gBrushButton;
                    if (!disabled) {
                        if (pressed) btnBr = gBrushAccent;
                        else if (gHoveredCtrl == hwnd) btnBr = gBrushHover;
                    }
                    RawFillRect(hdc, &rcBtn, btnBr);

                    RECT rSep = { rcBtn.left, rcBtn.top, rcBtn.left + 1, rcBtn.bottom };
                    RawFillRect(hdc, &rSep, gBrushBorder);

                    int cx = (rcBtn.left + 1 + rcBtn.right) / 2;
                    int cy = (rcBtn.top + rcBtn.bottom) / 2;
                    COLORREF arrCol = disabled ? gColMutedText : gColText;
                    POINT pts[3] = { { cx - 3, cy - 1 }, { cx + 3, cy - 1 }, { cx, cy + 2 } };
                    HBRUSH aBr = CreateSolidBrush(arrCol);
                    HPEN aPen  = CreatePen(PS_SOLID, 1, arrCol);
                    HGDIOBJ oB = SelectObject(hdc, aBr);
                    HGDIOBJ oP = SelectObject(hdc, aPen);
                    Polygon(hdc, pts, 3);
                    SelectObject(hdc, oB);
                    SelectObject(hdc, oP);
                    DeleteObject(aBr);
                    DeleteObject(aPen);
                }

                ReleaseDC(hwnd, hdc);
            }
            return r;
        }
        case WM_NCDESTROY:
            if (gHoveredCtrl == hwnd) gHoveredCtrl = nullptr;
            gInSubclassInternal = true;
            RemoveWindowSubclass(hwnd, DarkComboBoxSubclassProc, kComboSubclassId);
            gInSubclassInternal = false;
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static bool IsWin32MessageBox(HWND hwnd) {
    wchar_t cls[16] = {};
    if (GetClassNameW(hwnd, cls, 16) <= 0 || wcscmp(cls, L"#32770") != 0)
        return false;
    if (GetDlgItem(hwnd, 0xFFFF) == nullptr)
        return false;
    for (int id = 1; id <= 11; ++id) {
        if (GetDlgItem(hwnd, id) != nullptr) return true;
    }
    return false;
}

// ============================================================================
// Container, Header & Tab Subclasses
// ============================================================================
static LRESULT CALLBACK DarkContainerSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    if (gShuttingDown) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    // Hard re-entrancy & depth guard: makes stack overflow (0xC000041D / 0xC00000FD) impossible
    if (tContainerDepth >= 28) {
        LogGuardHit("depth-c", hwnd, msg, wParam, lParam);
        return 0;
    }
    for (int i = 0; i < tContainerDepth; ++i) {
        if (tContainerActive[i].h == hwnd && tContainerActive[i].m == msg && tContainerActive[i].id == kContainerSubclassId) {
            LogGuardHit("reenter-c", hwnd, msg, wParam, lParam);
        return 0;
        }
    }
    struct ScopePop {
        bool pushed;
        ~ScopePop() { if (pushed) --tContainerDepth; }
    } pop{ false };
    tContainerActive[tContainerDepth++] = { hwnd, msg, kContainerSubclassId };
    pop.pushed = true;

    if (msg == kWmUahDrawMenu || msg == kWmUahDrawMenuItem || msg == WM_NCPAINT || msg == WM_NCACTIVATE) {
        LRESULT uahRes = 0;
        if (HandleUahMenuMessage(hwnd, msg, wParam, lParam, &uahRes)) {
            return uahRes;
        }
    }

    switch (msg) {
        case WM_PAINT: {
            if (IsWin32MessageBox(hwnd)) {
                PAINTSTRUCT ps = {};
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc = {};
                GetClientRect(hwnd, &rc);

                int minBtnTop = rc.bottom;
                for (int id = 1; id <= 11; ++id) {
                    HWND hBtn = GetDlgItem(hwnd, id);
                    if (hBtn && IsWindowVisible(hBtn)) {
                        RECT rb = {};
                        GetWindowRect(hBtn, &rb);
                        MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&rb), 2);
                        if (rb.top < minBtnTop) minBtnTop = rb.top;
                    }
                }

                int saved = SaveDC(hdc);
                for (HWND ch = GetWindow(hwnd, GW_CHILD); ch != nullptr; ch = GetWindow(ch, GW_HWNDNEXT)) {
                    if (IsWindowVisible(ch)) {
                        RECT rcCh = {};
                        GetWindowRect(ch, &rcCh);
                        MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&rcCh), 2);
                        ExcludeClipRect(hdc, rcCh.left, rcCh.top, rcCh.right, rcCh.bottom);
                    }
                }

                if (minBtnTop < rc.bottom && minBtnTop > 12) {
                    int sepY = minBtnTop - 10;
                    RECT rcTop = { rc.left, rc.top, rc.right, sepY };
                    RECT rcSep = { rc.left, sepY, rc.right, sepY + 1 };
                    RECT rcBot = { rc.left, sepY + 1, rc.right, rc.bottom };
                    RawFillRect(hdc, &rcTop, gBrushBg);
                    RawFillRect(hdc, &rcSep, gBrushBorder);
                    RawFillRect(hdc, &rcBot, gBrushSurface);
                } else {
                    RawFillRect(hdc, &rc, gBrushBg);
                }

                RestoreDC(hdc, saved);
                EndPaint(hwnd, &ps);
                return 0;
            }
            break;
        }

        case WM_ERASEBKGND: {
            if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) == 0) {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                int saved = SaveDC(hdc);
                // Only exclude 3D Direct3D/OpenGL viewport children ('Warcraft III' / 'OpenGL').
                // Never exclude hollow frame controls (SS_ETCHEDFRAME / BS_GROUPBOX), or they leave white holes!
                for (HWND ch = GetWindow(hwnd, GW_CHILD); ch != nullptr; ch = GetWindow(ch, GW_HWNDNEXT)) {
                    if (IsWindowVisible(ch)) {
                        wchar_t chCls[64] = {};
                        GetClassNameW(ch, chCls, 64);
                        if (_wcsicmp(chCls, L"Warcraft III") == 0 || wcsstr(chCls, L"OpenGL") != nullptr) {
                            RECT rcCh = {};
                            GetWindowRect(ch, &rcCh);
                            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&rcCh), 2);
                            ExcludeClipRect(hdc, rcCh.left, rcCh.top, rcCh.right, rcCh.bottom);
                        }
                    }
                }
                RECT rc = {};
                GetClientRect(hwnd, &rc);
                RawFillRect(hdc, &rc, gBrushBg);
                RestoreDC(hdc, saved);
                return 1;
            }
            break;
        }

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, gColText);
            SetBkColor(hdc, gColBg);
            return reinterpret_cast<LRESULT>(gBrushBg);
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, gColText);
            SetBkColor(hdc, gColInput);
            return reinterpret_cast<LRESULT>(gBrushInput);
        }

        case WM_CTLCOLORSCROLLBAR: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, gColSurface);
            return reinterpret_cast<LRESULT>(gBrushSurface);
        }

        case WM_NOTIFY: {
            auto* nmh = reinterpret_cast<NMHDR*>(lParam);
            if (nmh && nmh->code == NM_CUSTOMDRAW) {
                wchar_t cls[64] = {};
                GetClassNameW(nmh->hwndFrom, cls, 64);

                if (_wcsicmp(cls, L"SysHeader32") == 0) {
                    auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
                    if (cd->dwDrawStage == CDDS_PREPAINT) {
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                        bool hot = (cd->uItemState & CDIS_HOT) != 0;
                        bool sel = (cd->uItemState & CDIS_SELECTED) != 0;
                        RawFillRect(cd->hdc, &cd->rc, sel ? gBrushAccent : (hot ? gBrushHover : gBrushSurface));
                        RECT rSep = { cd->rc.right - 1, cd->rc.top, cd->rc.right, cd->rc.bottom };
                        RawFillRect(cd->hdc, &rSep, gBrushBorder);

                        wchar_t text[256] = {};
                        HDITEMW hdi = {};
                        hdi.mask = HDI_TEXT;
                        hdi.pszText = text;
                        hdi.cchTextMax = 255;
                        SendMessageW(nmh->hwndFrom, HDM_GETITEMW, cd->dwItemSpec, reinterpret_cast<LPARAM>(&hdi));

                        RECT rcText = cd->rc;
                        rcText.left += 6;
                        rcText.right -= 4;
                        SetBkMode(cd->hdc, TRANSPARENT);
                        SetTextColor(cd->hdc, gColText);
                        DrawTextW(cd->hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                        return CDRF_SKIPDEFAULT;
                    }
                } else if (_wcsicmp(cls, L"ToolbarWindow32") == 0) {
                    auto* tbcd = reinterpret_cast<NMTBCUSTOMDRAW*>(lParam);
                    if (tbcd->nmcd.dwDrawStage == CDDS_PREERASE) {
                        RawFillRect(tbcd->nmcd.hdc, &tbcd->nmcd.rc, gBrushBg);
                        return CDRF_SKIPDEFAULT;
                    }
                    if (tbcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    if (tbcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        int btnIdx = static_cast<int>(SendMessageW(nmh->hwndFrom, TB_COMMANDTOINDEX, tbcd->nmcd.dwItemSpec, 0));
                        TBBUTTON tbb = {};
                        HIMAGELIST hImg = reinterpret_cast<HIMAGELIST>(SendMessageW(nmh->hwndFrom, TB_GETIMAGELIST, 0, 0));
                        if (btnIdx >= 0 && hImg != nullptr &&
                            SendMessageW(nmh->hwndFrom, TB_GETBUTTON, btnIdx, reinterpret_cast<LPARAM>(&tbb)) &&
                            tbb.iBitmap >= 0 && (tbb.fsStyle & BTNS_SEP) == 0)
                        {
                            RECT rc = tbcd->nmcd.rc;
                            RawFillRect(tbcd->nmcd.hdc, &rc, gBrushBg);

                            bool enabled = (tbb.fsState & TBSTATE_ENABLED) != 0 && (tbcd->nmcd.uItemState & CDIS_DISABLED) == 0;
                            bool checked = (tbb.fsState & (TBSTATE_CHECKED | TBSTATE_PRESSED)) != 0 ||
                                           (tbcd->nmcd.uItemState & (CDIS_CHECKED | CDIS_SELECTED)) != 0;
                            bool pressed = (tbb.fsState & TBSTATE_PRESSED) != 0;
                            bool hot     = (tbcd->nmcd.uItemState & CDIS_HOT) != 0;

                            COLORREF itemBg = gColBg;
                            RECT rcBtn = { rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1 };
                            if (enabled && checked) {
                                itemBg = gColAccent;
                                RawFillRect(tbcd->nmcd.hdc, &rcBtn, gBrushAccent);
                                Draw1PxBorder(tbcd->nmcd.hdc, rcBtn, RGB(0x4C, 0x88, 0xC8));
                            } else if (enabled && hot) {
                                itemBg = gColHover;
                                RawFillRect(tbcd->nmcd.hdc, &rcBtn, gBrushHover);
                                Draw1PxBorder(tbcd->nmcd.hdc, rcBtn, RGB(0x4E, 0x4E, 0x58));
                            }

                            int cx = 16, cy = 16;
                            ImageList_GetIconSize(hImg, &cx, &cy);
                            int ix = rc.left + ((rc.right - rc.left) - cx) / 2 + (pressed ? 1 : 0);
                            int iy = rc.top  + ((rc.bottom - rc.top) - cy) / 2 + (pressed ? 1 : 0);

                            DrawToolbarIconSmart(tbcd->nmcd.hdc, ix, iy, hImg, tbb.iBitmap, enabled, itemBg);
                            return CDRF_SKIPDEFAULT;
                        }
                        return TBCDRF_NOEDGES | TBCDRF_NOETCHEDEFFECT | TBCDRF_BLENDICON;
                    }
                } else if (_wcsicmp(cls, L"SysListView32") == 0) {
                    LRESULT res = DefSubclassProc(hwnd, msg, wParam, lParam);
                    auto* lvcd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                    if (lvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                        return res | CDRF_NOTIFYITEMDRAW;
                    }
                    if (lvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        int idx = static_cast<int>(lvcd->nmcd.dwItemSpec);
                        bool isSel = (idx >= 0) && ((ListView_GetItemState(nmh->hwndFrom, idx, LVIS_SELECTED) & LVIS_SELECTED) != 0);
                        if (!isSel) {
                            lvcd->clrText = LiftColorForDarkBg(lvcd->clrText);
                            lvcd->clrTextBk = gColSurface;
                        } else {
                            lvcd->clrText = RGB(0xFF, 0xFF, 0xFF);
                            lvcd->clrTextBk = (GetFocus() == nmh->hwndFrom) ? gColAccent : gColHover;
                        }
                        return res | CDRF_NOTIFYSUBITEMDRAW;
                    }
                    if (lvcd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                        int idx = static_cast<int>(lvcd->nmcd.dwItemSpec);
                        bool isSel = (idx >= 0) && ((ListView_GetItemState(nmh->hwndFrom, idx, LVIS_SELECTED) & LVIS_SELECTED) != 0);
                        if (!isSel) {
                            lvcd->clrText = LiftColorForDarkBg(lvcd->clrText);
                            lvcd->clrTextBk = gColSurface;
                        } else {
                            lvcd->clrText = RGB(0xFF, 0xFF, 0xFF);
                            lvcd->clrTextBk = (GetFocus() == nmh->hwndFrom) ? gColAccent : gColHover;
                        }
                        return res;
                    }
                    return res;
                } else if (_wcsicmp(cls, L"SysTreeView32") == 0) {
                    LRESULT res = DefSubclassProc(hwnd, msg, wParam, lParam);
                    auto* tvcd = reinterpret_cast<NMTVCUSTOMDRAW*>(lParam);
                    if (tvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                        return res | CDRF_NOTIFYITEMDRAW;
                    }
                    if (tvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        HTREEITEM hItem = reinterpret_cast<HTREEITEM>(tvcd->nmcd.dwItemSpec);
                        bool isSel = (hItem != nullptr) && ((TreeView_GetItemState(nmh->hwndFrom, hItem, TVIS_SELECTED) & TVIS_SELECTED) != 0);
                        if (!isSel) {
                            tvcd->clrText = LiftColorForDarkBg(tvcd->clrText);
                            tvcd->clrTextBk = gColSurface;
                        } else {
                            tvcd->clrText = RGB(0xFF, 0xFF, 0xFF);
                            tvcd->clrTextBk = (GetFocus() == nmh->hwndFrom) ? gColAccent : gColHover;
                        }
                        return res;
                    }
                    return res;
                }
            }
            break;
        }

        case WM_NCPAINT:
        case WM_NCACTIVATE: {
            LRESULT res = DefSubclassProc(hwnd, msg, wParam, lParam);
            if (GetMenu(hwnd) != nullptr) {
                DrawMenuBarBottomHairline(hwnd);
            }
            return res;
        }

        case WM_NCDESTROY:
            gInSubclassInternal = true;
            RemoveWindowSubclass(hwnd, DarkContainerSubclassProc, kContainerSubclassId);
            gInSubclassInternal = false;
            break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK DarkHeaderSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    if (msg == WM_ERASEBKGND) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        RawFillRect(hdc, &rc, gBrushSurface);
        return 1;
    }
    if (msg == WM_NCDESTROY) {
        gInSubclassInternal = true;
        RemoveWindowSubclass(hwnd, DarkHeaderSubclassProc, kHeaderSubclassId);
        gInSubclassInternal = false;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK DarkTabSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    if (gShuttingDown) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    if (tContainerDepth >= 28) {
        LogGuardHit("depth-t", hwnd, msg, wParam, lParam);
        return 0;
    }
    for (int i = 0; i < tContainerDepth; ++i) {
        if (tContainerActive[i].h == hwnd && tContainerActive[i].m == msg && tContainerActive[i].id == kTabSubclassId) {
            LogGuardHit("reenter-t", hwnd, msg, wParam, lParam);
        return 0;
        }
    }
    struct TabPop {
        bool pushed;
        ~TabPop() { if (pushed) --tContainerDepth; }
    } pop{ true };
    tContainerActive[tContainerDepth++] = { hwnd, msg, kTabSubclassId };

    if (msg == WM_ERASEBKGND) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        RawFillRect(hdc, &rc, gBrushBg);
        return 1;
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, gColText);
        SetBkColor(hdc, gColBg);
        return reinterpret_cast<LRESULT>(gBrushBg);
    }
    if (msg == WM_CTLCOLORSCROLLBAR) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, gColSurface);
        return reinterpret_cast<LRESULT>(gBrushSurface);
    }
    if (msg == WM_NCDESTROY) {
        gInSubclassInternal = true;
        RemoveWindowSubclass(hwnd, DarkTabSubclassProc, kTabSubclassId);
        gInSubclassInternal = false;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static bool IsStaticShapeType(DWORD st) {
    return st == SS_WHITERECT || st == SS_GRAYRECT || st == SS_BLACKRECT ||
           st == SS_WHITEFRAME || st == SS_GRAYFRAME || st == SS_BLACKFRAME ||
           st == SS_ETCHEDFRAME || st == SS_ETCHEDHORZ || st == SS_ETCHEDVERT;
}

static bool HasStaticNcBorder(LONG_PTR style, LONG_PTR exStyle) {
    return (style & (WS_THICKFRAME | WS_BORDER | SS_SUNKEN)) != 0 ||
           (exStyle & (WS_EX_STATICEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME)) != 0;
}

static LRESULT CALLBACK DarkStaticShapeSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    if (msg == WM_NCDESTROY) {
        gInSubclassInternal = true;
        RemoveWindowSubclass(hwnd, DarkStaticShapeSubclassProc, kStaticSubclassId);
        gInSubclassInternal = false;
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    if (msg == WM_NCPAINT) {
        LONG_PTR fullStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (HasStaticNcBorder(fullStyle, exStyle)) {
            HDC hdc = GetWindowDC(hwnd);
            if (hdc) {
                RECT rcWin = {};
                GetWindowRect(hwnd, &rcWin);
                int w = rcWin.right - rcWin.left;
                int h = rcWin.bottom - rcWin.top;
                RECT rc = { 0, 0, w, h };

                bool isEmptyDivider = ((fullStyle & WS_THICKFRAME) != 0) && (GetWindowTextLengthW(hwnd) == 0);
                if (!isEmptyDivider) {
                    // Status bar panes (SS_SUNKEN / WS_BORDER / WS_EX_STATICEDGE) or bordered text statics:
                    // Paint only the non-client border ring dark so interior text/images remain untouched
                    RECT rcCli = {};
                    GetClientRect(hwnd, &rcCli);
                    POINT pt = { 0, 0 };
                    ClientToScreen(hwnd, &pt);
                    OffsetRect(&rcCli, pt.x - rcWin.left, pt.y - rcWin.top);
                    int saved = SaveDC(hdc);
                    ExcludeClipRect(hdc, rcCli.left, rcCli.top, rcCli.right, rcCli.bottom);
                    RawFillRect(hdc, &rc, gBrushBg);
                    Draw1PxBorder(hdc, rc, gColBorder);
                    RestoreDC(hdc, saved);
                } else {
                    // Blizzard COsDivider splitter bar (STATIC + WS_THICKFRAME):
                    // Replace Windows' bright white thickframe border with sleek dark divider
                    RawFillRect(hdc, &rc, gBrushBg);
                    if (w <= 16 && h > w) {
                        RECT rLine = { w / 2, 0, w / 2 + 1, h };
                        RawFillRect(hdc, &rLine, gBrushBorder);
                    } else if (h <= 16 && w > h) {
                        RECT rLine = { 0, h / 2, w, h / 2 + 1 };
                        RawFillRect(hdc, &rLine, gBrushBorder);
                    } else {
                        Draw1PxBorder(hdc, rc, gColBorder);
                    }
                }
                ReleaseDC(hwnd, hdc);
            }
            return 0;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    if (msg == WM_ERASEBKGND) {
        LONG_PTR fullStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
        DWORD st = static_cast<DWORD>(fullStyle & SS_TYPEMASK);
        bool isEmptyDivider = ((fullStyle & WS_THICKFRAME) != 0) && (GetWindowTextLengthW(hwnd) == 0);
        if (!isEmptyDivider && !IsStaticShapeType(st)) {
            return DefSubclassProc(hwnd, msg, wParam, lParam);
        }
        return 1;
    }
    if (msg == WM_PAINT) {
        LONG_PTR fullStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
        DWORD st = static_cast<DWORD>(fullStyle & SS_TYPEMASK);
        bool isEmptyDivider = ((fullStyle & WS_THICKFRAME) != 0) && (GetWindowTextLengthW(hwnd) == 0);
        if (isEmptyDivider) {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            RawFillRect(hdc, &rc, gBrushBg);
            EndPaint(hwnd, &ps);
            return 0;
        }
        if (!IsStaticShapeType(st)) {
            return DefSubclassProc(hwnd, msg, wParam, lParam);
        }
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        if (st == SS_ETCHEDHORZ) {
            RawFillRect(hdc, &rc, gBrushBg);
            RECT rLine = { rc.left, rc.top, rc.right, rc.top + 1 };
            RawFillRect(hdc, &rLine, gBrushBorder);
        } else if (st == SS_ETCHEDVERT) {
            RawFillRect(hdc, &rc, gBrushBg);
            RECT rLine = { rc.left, rc.top, rc.left + 1, rc.bottom };
            RawFillRect(hdc, &rLine, gBrushBorder);
        } else {
            // Fill interior dark while clipping out any sibling controls (like Trigger Action Text links!) sitting inside this frame
            int saved = SaveDC(hdc);
            HWND hParent = GetParent(hwnd);
            if (hParent) {
                for (HWND sib = GetWindow(hParent, GW_CHILD); sib != nullptr; sib = GetWindow(sib, GW_HWNDNEXT)) {
                    if (sib != hwnd && IsWindowVisible(sib)) {
                        RECT rSib = {};
                        GetWindowRect(sib, &rSib);
                        MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&rSib), 2);
                        ExcludeClipRect(hdc, rSib.left, rSib.top, rSib.right, rSib.bottom);
                    }
                }
            }
            RawFillRect(hdc, &rc, gBrushBg);
            RestoreDC(hdc, saved);
            Draw1PxBorder(hdc, rc, gColBorder);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// JNGP TESH / Scintilla vJASS Code Editor Dark Theme (TESH 0.7 & TESH 2.0)
// ============================================================================
static COLORREF LiftScintillaFgForDark(COLORREF col, bool isBold) {
    BYTE r = GetRValue(col);
    BYTE g = GetGValue(col);
    BYTE b = GetBValue(col);
    int luma = (r * 299 + g * 587 + b * 114) / 1000;

    // Near-black: bold -> VS Code keyword blue (#569CD6), regular -> crisp code light (#DCDCDC)
    if (r < 42 && g < 42 && b < 42) {
        return isBold ? RGB(0x56, 0x9C, 0xD6) : RGB(0xDC, 0xDC, 0xDC);
    }
    // Dark purple/magenta (Native functions: CreateUnit, GetTriggerUnit...) -> vibrant lavender
    if (r >= 80 && b >= 80 && g < 60 && luma < 140) {
        return RGB(0xD2, 0xA8, 0xFF);
    }
    // Dark navy/blue (bold Native types: unit/real/integer -> emerald-teal; Constants/Events -> bright cyan)
    if (b >= 110 && r < 75 && g < 115 && luma < 140) {
        return isBold ? RGB(0x4E, 0xC9, 0xB0) : RGB(0x4F, 0xC1, 0xFF);
    }
    // Dark brown/amber (Templates / Legacy types) -> warm sand-gold
    if (r >= 110 && g >= 45 && g <= 115 && b < 45 && luma < 140) {
        return RGB(0xDC, 0xDC, 0xAA);
    }
    // Dark maroon (Constants in RGB order) -> bright sky-blue
    if (r >= 130 && r < 200 && g < 45 && b < 45) {
        return RGB(0x79, 0xC0, 0xFF);
    }
    // Pure/bright red (BJ functions) -> soft readable coral-red
    if (r >= 200 && g < 60 && b < 60) {
        return RGB(0xFF, 0x7B, 0x72);
    }
    // Dark green (bold Escape chars -> warm amber; Comments -> pleasant sage-mint green)
    if (g >= 95 && r < 55 && b < 55) {
        return isBold ? RGB(0xD7, 0xBA, 0x7D) : RGB(0x6A, 0xB8, 0x5A);
    }
    if (luma < 105) {
        int nr = r + 100; if (nr > 235) nr = 235;
        int ng = g + 100; if (ng > 235) ng = 235;
        int nb = b + 100; if (nb > 235) nb = 235;
        return RGB(nr, ng, nb);
    }
    return col;
}

static void ApplyDarkThemeToScintilla(HWND hwnd) {
    static constexpr UINT kSciStyleSetFore        = 2051;
    static constexpr UINT kSciStyleSetBack        = 2052;
    static constexpr UINT kSciStyleGetFore        = 2481;
    static constexpr UINT kSciStyleGetBack        = 2482;
    static constexpr UINT kSciStyleGetBold        = 2483;
    static constexpr UINT kSciSetSelFore          = 2067;
    static constexpr UINT kSciSetSelBack          = 2068;
    static constexpr UINT kSciSetCaretFore        = 2069;
    static constexpr UINT kSciSetCaretLineBack    = 2098;
    static constexpr UINT kSciIndicSetFore        = 2082;
    static constexpr UINT kSciIndicGetFore        = 2083;
    static constexpr UINT kSciSetWhitespaceBack   = 2085;
    static constexpr UINT kSciMarkerSetFore       = 2041;
    static constexpr UINT kSciMarkerSetBack       = 2042;
    static constexpr UINT kSciCallTipSetBack      = 2205;
    static constexpr UINT kSciCallTipSetFore      = 2206;
    static constexpr UINT kSciCallTipSetForeHlt   = 2207;
    static constexpr UINT kSciSetFoldMarginColour = 2290;
    static constexpr UINT kSciSetFoldMarginHiCol  = 2291;
    static constexpr UINT kSciSetEdgeColour       = 2365;

    const COLORREF codeBg   = RGB(0x14, 0x14, 0x17);
    const COLORREF gutterBg = RGB(0x1B, 0x1B, 0x20);
    const COLORREF gutterFg = RGB(0x78, 0x80, 0x90);

    for (int s = 0; s <= 255; ++s) {
        if (s == 33) { // STYLE_LINENUMBER
            DefSubclassProc(hwnd, kSciStyleSetBack, s, gutterBg);
            DefSubclassProc(hwnd, kSciStyleSetFore, s, gutterFg);
            continue;
        }
        if (s == 34) { // STYLE_BRACELIGHT
            DefSubclassProc(hwnd, kSciStyleSetBack, s, gColAccent);
            DefSubclassProc(hwnd, kSciStyleSetFore, s, RGB(0xFF, 0xE0, 0x66));
            continue;
        }
        if (s == 35) { // STYLE_BRACEBAD
            DefSubclassProc(hwnd, kSciStyleSetBack, s, RGB(0x5A, 0x1E, 0x24));
            DefSubclassProc(hwnd, kSciStyleSetFore, s, RGB(0xFF, 0x80, 0x80));
            continue;
        }
        if (s == 37) { // STYLE_INDENTGUIDE
            DefSubclassProc(hwnd, kSciStyleSetBack, s, codeBg);
            DefSubclassProc(hwnd, kSciStyleSetFore, s, RGB(0x34, 0x36, 0x40));
            continue;
        }
        if (s == 38) { // STYLE_CALLTIP
            DefSubclassProc(hwnd, kSciStyleSetBack, s, RGB(0x20, 0x20, 0x26));
            DefSubclassProc(hwnd, kSciStyleSetFore, s, RGB(0xDC, 0xDC, 0xDC));
            continue;
        }

        COLORREF bg = static_cast<COLORREF>(DefSubclassProc(hwnd, kSciStyleGetBack, s, 0));
        COLORREF fg = static_cast<COLORREF>(DefSubclassProc(hwnd, kSciStyleGetFore, s, 0));
        bool bold   = DefSubclassProc(hwnd, kSciStyleGetBold, s, 0) != 0;

        if (s == 32 || (GetRValue(bg) > 160 && GetGValue(bg) > 160 && GetBValue(bg) > 160)) {
            DefSubclassProc(hwnd, kSciStyleSetBack, s, codeBg);
        }
        DefSubclassProc(hwnd, kSciStyleSetFore, s, LiftScintillaFgForDark(fg, bold));
    }

    // Caret, active line, selection, fold margin, 120-col vertical guide, fold markers (25..31), indicators & calltips
    DefSubclassProc(hwnd, kSciSetCaretFore,        RGB(0xFF, 0xFF, 0xFF), 0);
    DefSubclassProc(hwnd, kSciSetCaretLineBack,    RGB(0x22, 0x26, 0x30), 0);
    DefSubclassProc(hwnd, kSciSetSelFore,          0, 0); // Preserve syntax colors inside selection
    DefSubclassProc(hwnd, kSciSetSelBack,          1, gColAccent);
    DefSubclassProc(hwnd, kSciSetWhitespaceBack,   1, codeBg);
    DefSubclassProc(hwnd, kSciSetFoldMarginColour, 1, gutterBg);
    DefSubclassProc(hwnd, kSciSetFoldMarginHiCol,  1, gutterBg);
    DefSubclassProc(hwnd, kSciSetEdgeColour,       RGB(0x2B, 0x2E, 0x38), 0);

    for (int m = 25; m <= 31; ++m) {
        DefSubclassProc(hwnd, kSciMarkerSetFore, m, gutterBg);
        DefSubclassProc(hwnd, kSciMarkerSetBack, m, RGB(0x82, 0x8A, 0x98));
    }

    for (int ind = 0; ind <= 2; ++ind) {
        COLORREF indFg = static_cast<COLORREF>(DefSubclassProc(hwnd, kSciIndicGetFore, ind, 0));
        if (GetRValue(indFg) < 40 && GetGValue(indFg) < 40 && GetBValue(indFg) < 40) {
            DefSubclassProc(hwnd, kSciIndicSetFore, ind, RGB(0x6C, 0xB8, 0xFF));
        }
    }

    DefSubclassProc(hwnd, kSciCallTipSetBack,    RGB(0x20, 0x20, 0x26), 0);
    DefSubclassProc(hwnd, kSciCallTipSetFore,    RGB(0xDC, 0xDC, 0xDC), 0);
    DefSubclassProc(hwnd, kSciCallTipSetForeHlt, RGB(0x6C, 0xB8, 0xFF), 0);
}

static LRESULT CALLBACK DarkScintillaSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
{
    if (msg == WM_NCDESTROY) {
        gInSubclassInternal = true;
        RemoveWindowSubclass(hwnd, DarkScintillaSubclassProc, kScintillaSubclassId);
        gInSubclassInternal = false;
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    // Track whenever TESH configures or reloads styles.ini so next WM_PAINT converts all 256 styles cleanly
    if (msg == 2050 /* SCI_STYLECLEARALL */ ||
        msg == 2051 /* SCI_STYLESETFORE */  ||
        msg == 2052 /* SCI_STYLESETBACK */  ||
        msg == 2053 /* SCI_STYLESETBOLD */)
    {
        LRESULT res = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (dwRefData == 0) {
            SetWindowSubclass(hwnd, DarkScintillaSubclassProc, kScintillaSubclassId, 1);
        }
        return res;
    }
    if (msg == WM_PAINT) {
        COLORREF defBg = static_cast<COLORREF>(DefSubclassProc(hwnd, 2482 /* SCI_STYLEGETBACK */, 32, 0));
        if (dwRefData != 0 || GetRValue(defBg) > 60 || GetGValue(defBg) > 60 || GetBValue(defBg) > 60) {
            SetWindowSubclass(hwnd, DarkScintillaSubclassProc, kScintillaSubclassId, 0);
            ApplyDarkThemeToScintilla(hwnd);
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void StyleWindowDark(HWND hwnd);

static BOOL CALLBACK EnumChildStyleProc(HWND child, LPARAM /*lParam*/) {
    StyleWindowDark(child);
    return TRUE;
}

static bool IsDelphiOrTeshWindow(HWND hwnd, const wchar_t* cls) {
    // Delphi 7 VCL (TESH 0.7 in JNGP 1.4g) uses MakeObjectInstance heap thunks on GWL_WNDPROC
    // and frees GetWindowLongA(GWL_WNDPROC) directly via FreeObjectInstance on exit!
    // Subclassing a Delphi VCL window with Unicode SetWindowSubclass turns GetWindowLongA into a
    // 0xFFFFxxxx WOW64 handle cookie, causing EAccessViolation (Write of FFFF09F0) on close.
    // Note: "Scintilla" is owned by SciLexer.dll/Scintilla.dll (NOT tesh.dll), so it still gets full dark mode!
    if (cls && (_wcsicmp(cls, L"TPUtilWindow") == 0 ||
                _wcsicmp(cls, L"TApplication") == 0 ||
                _wcsnicmp(cls, L"TTesh", 5) == 0 ||
                _wcsnicmp(cls, L"TForm", 5) == 0))
    {
        return true;
    }
    if (hwnd) {
        HMODULE hTesh = GetModuleHandleW(L"tesh.dll");
        if (hTesh) {
            HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            if (hInst == hTesh) {
                return true;
            }
        }
    }
    return false;
}

// Blizzard's hidden Direct3D/GX render window ("Warcraft III" class, created at 0x4cb390 and hooked via
// GetWindowLongA(GWL_WNDPROC)+SetWindowLongA(0x4cab80) in CAIEditor / CCampaignEditor) has NO menu.
// Putting a ComCtl32 master subclass on it makes Blizzard's CallWindowProcA(old) chain loop forever
// (USER32!SendMessageWorker stack overflow, 0xC000041D). The real main frame of the same class owns a menu.
static bool IsBlizzardGxWindowWithoutMenu(HWND hwnd, const wchar_t* cls) {
    if (!hwnd || !cls || _wcsicmp(cls, L"Warcraft III") != 0) return false;
    if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0) return false;
    return GetMenu(hwnd) == nullptr;
}

static void StyleWindowDark(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || gInStyleWindow || gShuttingDown)
        return;

    gInStyleWindow = true;

    wchar_t cls[96] = {};
    if (GetClassNameW(hwnd, cls, 96) > 0) {
        if (wcsstr(cls, L"OpenGL") == nullptr && !IsDelphiOrTeshWindow(hwnd, cls)) {
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            bool isChild = (style & WS_CHILD) != 0;

            if (!isChild) {
                EnableDarkTitleBar(hwnd);
                if (!IsBlizzardGxWindowWithoutMenu(hwnd, cls)) {
                    SafeSetWindowSubclass(hwnd, DarkContainerSubclassProc, kContainerSubclassId, 0);
                }
            } else {
                // Ensure the parent container handles WM_CTLCOLOR* & NM_CUSTOMDRAW (even if parent class is 'Warcraft III'!)
                // Skip SysTabControl32 and Delphi TESH VCL containers so we NEVER overwrite their GWL_WNDPROC!
                HWND hParent = GetParent(hwnd);
                if (hParent && IsWindow(hParent)) {
                    wchar_t pCls[96] = {};
                    GetClassNameW(hParent, pCls, 96);
                    if (wcsstr(pCls, L"OpenGL") == nullptr &&
                        _wcsicmp(pCls, L"SysTabControl32") != 0 &&
                        !IsBlizzardGxWindowWithoutMenu(hParent, pCls) &&
                        !IsDelphiOrTeshWindow(hParent, pCls))
                    {
                        SafeSetWindowSubclass(hParent, DarkContainerSubclassProc, kContainerSubclassId, 0);
                    }
                }
            }

            if (_wcsicmp(cls, L"SysTreeView32") == 0) {
                SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
                TreeView_SetBkColor(hwnd, gColSurface);
                TreeView_SetTextColor(hwnd, gColText);
                TreeView_SetLineColor(hwnd, gColBorder);
            } else if (_wcsicmp(cls, L"SysListView32") == 0) {
                SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
                ListView_SetBkColor(hwnd, gColSurface);
                ListView_SetTextBkColor(hwnd, gColSurface);
                ListView_SetTextColor(hwnd, gColText);
                HWND hHdr = ListView_GetHeader(hwnd);
                if (hHdr) {
                    SetWindowTheme(hHdr, L"", L"");
                    SafeSetWindowSubclass(hHdr, DarkHeaderSubclassProc, kHeaderSubclassId, 0);
                }
                SafeSetWindowSubclass(hwnd, DarkContainerSubclassProc, kContainerSubclassId, 0);
            } else if (_wcsicmp(cls, L"SysHeader32") == 0) {
                SetWindowTheme(hwnd, L"", L"");
                SafeSetWindowSubclass(hwnd, DarkHeaderSubclassProc, kHeaderSubclassId, 0);
            } else if (_wcsicmp(cls, L"ComboBox") == 0 || _wcsicmp(cls, L"ComboBoxEx32") == 0) {
                SetWindowTheme(hwnd, L"", L"");
                SafeSetWindowSubclass(hwnd, DarkComboBoxSubclassProc, kComboSubclassId, 0);
                SafeSetWindowSubclass(hwnd, DarkContainerSubclassProc, kContainerSubclassId, 0);
            } else if (_wcsicmp(cls, L"Static") == 0) {
                DWORD st = static_cast<DWORD>(style & SS_TYPEMASK);
                LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                if (HasStaticNcBorder(style, exStyle) || IsStaticShapeType(st))
                {
                    SetWindowTheme(hwnd, L"", L"");
                    SafeSetWindowSubclass(hwnd, DarkStaticShapeSubclassProc, kStaticSubclassId, 0);
                }
                SafeSetWindowSubclass(hwnd, DarkContainerSubclassProc, kContainerSubclassId, 0);
            } else if (_wcsicmp(cls, L"Button") == 0) {
                SetWindowTheme(hwnd, L"", L"");
                SafeSetWindowSubclass(hwnd, DarkButtonSubclassProc, kButtonSubclassId, 0);
            } else if (_wcsicmp(cls, L"msctls_updown32") == 0) {
                SetWindowTheme(hwnd, L"", L"");
                SafeSetWindowSubclass(hwnd, DarkUpDownSubclassProc, kUpDownSubclassId, 0);
            } else if (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ListBox") == 0 ||
                       _wcsicmp(cls, L"ComboLBox") == 0 || _wcsicmp(cls, L"ScrollBar") == 0) {
                SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
            } else if (_wcsicmp(cls, L"Scintilla") == 0) {
                SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
                SafeSetWindowSubclass(hwnd, DarkScintillaSubclassProc, kScintillaSubclassId, 1);
                InvalidateRect(hwnd, nullptr, TRUE);
            } else if (_wcsnicmp(cls, L"RichEdit", 8) == 0 || _wcsnicmp(cls, L"RICHEDIT", 8) == 0) {
                SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
                SendMessageW(hwnd, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(gColInput));
                CHARFORMAT2W cf = {};
                cf.cbSize = sizeof(cf);
                cf.dwMask = CFM_COLOR;
                cf.crTextColor = gColText;
                SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
            } else if (_wcsicmp(cls, L"SysTabControl32") == 0) {
                SetWindowTheme(hwnd, L"", L"");
                SafeSetWindowSubclass(hwnd, DarkTabSubclassProc, kTabSubclassId, 0);
            } else if (_wcsicmp(cls, L"ToolbarWindow32") == 0 || _wcsicmp(cls, L"ReBarWindow32") == 0 ||
                       _wcsicmp(cls, L"msctls_statusbar32") == 0 || _wcsicmp(cls, L"msctls_trackbar32") == 0) {
                SetWindowTheme(hwnd, L"", L"");
                if (_wcsicmp(cls, L"msctls_statusbar32") == 0) {
                    SendMessageW(hwnd, SB_SETBKCOLOR, 0, static_cast<LPARAM>(gColSurface));
                }
                SafeSetWindowSubclass(hwnd, DarkTabSubclassProc, kTabSubclassId, 0);
                SafeSetWindowSubclass(hwnd, DarkContainerSubclassProc, kContainerSubclassId, 0);
            } else if (_wcsicmp(cls, L"#32770") == 0 ||
                       wcsstr(cls, L"Afx") != nullptr || wcsstr(cls, L"WorldEdit") != nullptr ||
                       wcsstr(cls, L"MDIClient") != nullptr || wcsstr(cls, L"WE") != nullptr ||
                       wcsstr(cls, L"TESH") != nullptr) {
                SafeSetWindowSubclass(hwnd, DarkContainerSubclassProc, kContainerSubclassId, 0);
            }
        }
    }

    gInStyleWindow = false;
}

static LRESULT CALLBACK CbtHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HCBT_CREATEWND) {
        StyleWindowDark(reinterpret_cast<HWND>(wParam));
    } else if (nCode == HCBT_ACTIVATE) {
        HWND hwnd = reinterpret_cast<HWND>(wParam);
        StyleWindowDark(hwnd);
        EnumChildWindows(hwnd, EnumChildStyleProc, 0);
    }
    return CallNextHookEx(tCbtHook, nCode, wParam, lParam);
}

static BOOL CALLBACK RetroEnumThreadWndProc(HWND hwnd, LPARAM /*lParam*/) {
    StyleWindowDark(hwnd);
    EnumChildWindows(hwnd, EnumChildStyleProc, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (GetMenu(hwnd) != nullptr) DrawMenuBar(hwnd);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

static void EnsureLiveCbtHookOnUiThread() {
    if (!tCbtHook) {
        tCbtHook = SetWindowsHookExW(WH_CBT, CbtHookProc, nullptr, GetCurrentThreadId());
    }
    if (!tRetroStyled) {
        tRetroStyled = true;
        EnumThreadWindows(GetCurrentThreadId(), RetroEnumThreadWndProc, 0);
    }
}

// ============================================================================
// Detour Implementations (Hot-Patched on user32.dll & uxtheme.dll)
// ============================================================================
static DWORD WINAPI DetourGetSysColor(int nIndex) {
    if (!gInInternalDraw) {
        if (!tCbtHook) EnsureLiveCbtHookOnUiThread();
        if (nIndex >= 0 && nIndex < 32 && gSysColorOverride[nIndex]) {
            return gSysColors[nIndex];
        }
    }
    return reinterpret_cast<fnGetSysColor>(gHpGetSysColor.origEntry)(nIndex);
}

static HBRUSH WINAPI DetourGetSysColorBrush(int nIndex) {
    if (!gInInternalDraw && nIndex >= 0 && nIndex < 32 && gSysColorOverride[nIndex] && gSysBrushes[nIndex]) {
        return gSysBrushes[nIndex];
    }
    return reinterpret_cast<fnGetSysColorBrush>(gHpGetSysColorBrush.origEntry)(nIndex);
}

static int WINAPI DetourFillRect(HDC hDC, const RECT* lprc, HBRUSH hbr) {
    if (!gInInternalDraw) {
        if (!tCbtHook) EnsureLiveCbtHookOnUiThread();
        if (hbr) {
            uintptr_t v = reinterpret_cast<uintptr_t>(hbr);
            if (v >= 1 && v <= 32) {
                int idx = static_cast<int>(v - 1);
                if (gSysColorOverride[idx] && gSysBrushes[idx]) {
                    hbr = gSysBrushes[idx];
                }
            } else {
                LOGBRUSH lb = {};
                if (GetObjectW(hbr, sizeof(lb), &lb) == sizeof(lb) && lb.lbStyle == BS_SOLID) {
                    if (GetRValue(lb.lbColor) > 215 && GetGValue(lb.lbColor) > 215 && GetBValue(lb.lbColor) > 215) {
                        hbr = gBrushBg;
                    }
                }
            }
        }
    }
    return reinterpret_cast<fnFillRect>(gHpFillRect.origEntry)(hDC, lprc, hbr);
}

static BOOL WINAPI DetourDrawEdge(HDC hdc, LPRECT qrc, UINT edge, UINT grfFlags) {
    if (!qrc || !hdc) {
        return reinterpret_cast<fnDrawEdge>(gHpDrawEdge.origEntry)(hdc, qrc, edge, grfFlags);
    }

    RECT rc = *qrc;
    if ((grfFlags & BF_MONO) == 0 && (grfFlags & BF_FLAT) == 0) {
        if (grfFlags & BF_MIDDLE) {
            RawFillRect(hdc, &rc, gBrushBg);
        }

        // Draw sleek 1px dark border on requested edges instead of white Win95 3D bevels
        if (grfFlags & BF_TOP) {
            RECT r = { rc.left, rc.top, rc.right, rc.top + 1 };
            RawFillRect(hdc, &r, gBrushBorder);
        }
        if (grfFlags & BF_BOTTOM) {
            RECT r = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
            RawFillRect(hdc, &r, gBrushBorder);
        }
        if (grfFlags & BF_LEFT) {
            RECT r = { rc.left, rc.top, rc.left + 1, rc.bottom };
            RawFillRect(hdc, &r, gBrushBorder);
        }
        if (grfFlags & BF_RIGHT) {
            RECT r = { rc.right - 1, rc.top, rc.right, rc.bottom };
            RawFillRect(hdc, &r, gBrushBorder);
        }

        if (grfFlags & BF_ADJUST) {
            int thickness = ((edge & BDR_OUTER) && (edge & BDR_INNER)) ? 2 : 1;
            if (grfFlags & BF_LEFT)   qrc->left   += thickness;
            if (grfFlags & BF_TOP)    qrc->top    += thickness;
            if (grfFlags & BF_RIGHT)  qrc->right  -= thickness;
            if (grfFlags & BF_BOTTOM) qrc->bottom -= thickness;
        }
        return TRUE;
    }

    return reinterpret_cast<fnDrawEdge>(gHpDrawEdge.origEntry)(hdc, qrc, edge, grfFlags);
}

static COLORREF WINAPI DetourGetThemeSysColor(HTHEME hTheme, int iColorId) {
    if (iColorId >= 0 && iColorId < 32 && gSysColorOverride[iColorId]) {
        return gSysColors[iColorId];
    }
    return reinterpret_cast<fnGetThemeSysColor>(gHpGetThemeSysColor.origEntry)(hTheme, iColorId);
}

static HBRUSH WINAPI DetourGetThemeSysColorBrush(HTHEME hTheme, int iColorId) {
    if (iColorId >= 0 && iColorId < 32 && gSysColorOverride[iColorId] && gSysBrushes[iColorId]) {
        return gSysBrushes[iColorId];
    }
    return reinterpret_cast<fnGetThemeSysColorBrush>(gHpGetThemeSysColorBrush.origEntry)(hTheme, iColorId);
}

static LPCWSTR MapClassListToDark(HWND /*hwnd*/, LPCWSTR pszClassList, bool& forceClassicNull) {
    forceClassicNull = false;
    if (!pszClassList || !*pszClassList) return pszClassList;

    if (wcsstr(pszClassList, L"DarkMode_") != nullptr)
        return pszClassList;

    if (_wcsicmp(pszClassList, L"ScrollBar") == 0)
        return L"DarkMode_Explorer::ScrollBar";
    if (_wcsicmp(pszClassList, L"Combobox") == 0 || _wcsicmp(pszClassList, L"ComboBox") == 0)
        return L"DarkMode_CFD::Combobox";
    if (_wcsicmp(pszClassList, L"Edit") == 0)
        return L"DarkMode_Explorer::Edit";
    if (_wcsicmp(pszClassList, L"TreeView") == 0 || _wcsicmp(pszClassList, L"Explorer::TreeView") == 0)
        return L"DarkMode_Explorer::TreeView";
    if (_wcsicmp(pszClassList, L"ListView") == 0 || _wcsicmp(pszClassList, L"Explorer::ListView") == 0)
        return L"DarkMode_Explorer::ListView";
    if (_wcsicmp(pszClassList, L"Menu") == 0)
        return pszClassList;

    if (_wcsicmp(pszClassList, L"Button") == 0 ||
        _wcsicmp(pszClassList, L"Tab") == 0 ||
        _wcsicmp(pszClassList, L"Toolbar") == 0 ||
        _wcsicmp(pszClassList, L"Rebar") == 0 ||
        _wcsicmp(pszClassList, L"Status") == 0 ||
        _wcsicmp(pszClassList, L"Header") == 0 ||
        _wcsicmp(pszClassList, L"Trackbar") == 0 ||
        _wcsicmp(pszClassList, L"Spin") == 0 ||
        _wcsicmp(pszClassList, L"Progress") == 0 ||
        _wcsicmp(pszClassList, L"Tooltip") == 0 ||
        _wcsicmp(pszClassList, L"TaskDialog") == 0 ||
        _wcsicmp(pszClassList, L"TaskDialogStyle") == 0 ||
        _wcsicmp(pszClassList, L"AeroWizard") == 0)
    {
        forceClassicNull = true;
        return nullptr;
    }

    return pszClassList;
}

static BOOL WINAPI DetourFillRgn(HDC hdc, HRGN hrgn, HBRUSH hbr) {
    if (!gInInternalDraw && hbr) {
        LOGBRUSH lb = {};
        if (GetObjectW(hbr, sizeof(lb), &lb) == sizeof(lb) && lb.lbStyle == BS_SOLID) {
            if (GetRValue(lb.lbColor) > 215 && GetGValue(lb.lbColor) > 215 && GetBValue(lb.lbColor) > 215) {
                hbr = gBrushBg;
            }
        }
    }
    return reinterpret_cast<fnFillRgn>(gHpFillRgn.origEntry)(hdc, hrgn, hbr);
}

static COLORREF WINAPI DetourSetTextColor(HDC hdc, COLORREF col) {
    if (!gInInternalDraw) {
        col = LiftColorForDarkBg(col);
    }
    return reinterpret_cast<fnSetTextColor>(gHpSetTextColor.origEntry)(hdc, col);
}

static void StripOurSubclassesBeforeRehook(HWND hwnd) {
    RemoveWindowSubclass(hwnd, DarkButtonSubclassProc,      kButtonSubclassId);
    RemoveWindowSubclass(hwnd, DarkComboBoxSubclassProc,    kComboSubclassId);
    RemoveWindowSubclass(hwnd, DarkUpDownSubclassProc,      kUpDownSubclassId);
    RemoveWindowSubclass(hwnd, DarkStaticShapeSubclassProc, kStaticSubclassId);
    RemoveWindowSubclass(hwnd, DarkScintillaSubclassProc,   kScintillaSubclassId);
    RemoveWindowSubclass(hwnd, DarkHeaderSubclassProc,      kHeaderSubclassId);
    RemoveWindowSubclass(hwnd, DarkTabSubclassProc,         kTabSubclassId);
    RemoveWindowSubclass(hwnd, DarkContainerSubclassProc,   kContainerSubclassId);
}

static bool IsBlizzardDialogControlForRehook(HWND hwnd, LONG dwNewLong) {
    // Only Blizzard's COsControl dialog child controls (inside worldedit*.exe) overwrite
    // GWL_WNDPROC without chaining to the old WndProc (they jump straight to GCL_WNDPROC).
    // NEVER strip & re-hook top-level/container windows or third-party JNGP plugin hooks (wehack/tesh/japi).
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_CHILD) == 0) return false;

    HMODULE hTargetMod = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(static_cast<uint32_t>(dwNewLong))),
            &hTargetMod) ||
        hTargetMod != GetModuleHandleW(nullptr))
    {
        return false;
    }

    wchar_t cls[64] = {};
    if (GetClassNameW(hwnd, cls, 64) <= 0) return false;
    return _wcsicmp(cls, L"Button") == 0 ||
           _wcsicmp(cls, L"ComboBox") == 0 ||
           _wcsicmp(cls, L"ComboBoxEx32") == 0 ||
           _wcsicmp(cls, L"msctls_updown32") == 0 ||
           _wcsicmp(cls, L"Static") == 0;
}

static LONG WINAPI DetourSetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong) {
    auto orig = reinterpret_cast<fnSetWindowLongA>(gHpSetWindowLongA.origEntry);
    if (nIndex == GWL_WNDPROC && !gInStyleWindow && !gInSubclassInternal && hWnd && IsWindow(hWnd) &&
        IsBlizzardDialogControlForRehook(hWnd, dwNewLong))
    {
        gInSubclassInternal = true;
        StripOurSubclassesBeforeRehook(hWnd);
        LONG ret = orig(hWnd, nIndex, dwNewLong);
        StyleWindowDark(hWnd);
        gInSubclassInternal = false;
        return ret;
    }
    return orig(hWnd, nIndex, dwNewLong);
}

static LONG WINAPI DetourSetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) {
    auto orig = reinterpret_cast<fnSetWindowLongW>(gHpSetWindowLongW.origEntry);
    if (nIndex == GWL_WNDPROC && !gInStyleWindow && !gInSubclassInternal && hWnd && IsWindow(hWnd) &&
        IsBlizzardDialogControlForRehook(hWnd, dwNewLong))
    {
        gInSubclassInternal = true;
        StripOurSubclassesBeforeRehook(hWnd);
        LONG ret = orig(hWnd, nIndex, dwNewLong);
        StyleWindowDark(hWnd);
        gInSubclassInternal = false;
        return ret;
    }
    return orig(hWnd, nIndex, dwNewLong);
}

static HTHEME WINAPI DetourOpenThemeData(HWND hwnd, LPCWSTR pszClassList) {
    auto orig = reinterpret_cast<fnOpenThemeData>(gHpOpenThemeData.origEntry);
    if (gInThemeRedirect) return orig(hwnd, pszClassList);

    gInThemeRedirect = true;
    bool forceNull = false;
    LPCWSTR mapped = MapClassListToDark(hwnd, pszClassList, forceNull);
    HTHEME res = forceNull ? nullptr : orig(hwnd, mapped);
    if (!forceNull && !res && mapped != pszClassList) {
        res = orig(hwnd, pszClassList);
    }
    gInThemeRedirect = false;
    return res;
}

static HTHEME WINAPI DetourOpenThemeDataEx(HWND hwnd, LPCWSTR pszClassList, DWORD dwFlags) {
    auto orig = reinterpret_cast<fnOpenThemeDataEx>(gHpOpenThemeDataEx.origEntry);
    if (gInThemeRedirect) return orig(hwnd, pszClassList, dwFlags);

    gInThemeRedirect = true;
    bool forceNull = false;
    LPCWSTR mapped = MapClassListToDark(hwnd, pszClassList, forceNull);
    HTHEME res = forceNull ? nullptr : orig(hwnd, mapped, dwFlags);
    if (!forceNull && !res && mapped != pszClassList) {
        res = orig(hwnd, pszClassList, dwFlags);
    }
    gInThemeRedirect = false;
    return res;
}

static HWND WINAPI DetourCreateWindowExA(
    DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle,
    int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    EnsureLiveCbtHookOnUiThread();
    HWND hwnd = reinterpret_cast<fnCreateWindowExA>(gHpCreateWindowExA.origEntry)(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hwnd) {
        StyleWindowDark(hwnd);
    }
    return hwnd;
}

static HWND WINAPI DetourCreateWindowExW(
    DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle,
    int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    EnsureLiveCbtHookOnUiThread();
    HWND hwnd = reinterpret_cast<fnCreateWindowExW>(gHpCreateWindowExW.origEntry)(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hwnd) {
        StyleWindowDark(hwnd);
    }
    return hwnd;
}

static HWND WINAPI DetourCreateDialogIndirectParamA(
    HINSTANCE hInstance, LPCDLGTEMPLATEA lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
    EnsureLiveCbtHookOnUiThread();
    HWND hwnd = reinterpret_cast<fnCreateDialogIndirectParamA>(gHpCreateDialogIndirectParamA.origEntry)(
        hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam);
    if (hwnd) {
        StyleWindowDark(hwnd);
        EnumChildWindows(hwnd, EnumChildStyleProc, 0);
    }
    return hwnd;
}

static HWND WINAPI DetourCreateDialogIndirectParamW(
    HINSTANCE hInstance, LPCDLGTEMPLATEW lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
    EnsureLiveCbtHookOnUiThread();
    HWND hwnd = reinterpret_cast<fnCreateDialogIndirectParamW>(gHpCreateDialogIndirectParamW.origEntry)(
        hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam);
    if (hwnd) {
        StyleWindowDark(hwnd);
        EnumChildWindows(hwnd, EnumChildStyleProc, 0);
    }
    return hwnd;
}

// ============================================================================
// Initialization & DLL Entry
// ============================================================================
static void InitUxThemeAndDwm() {
    HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
    if (hUx) {
        gAllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUx, MAKEINTRESOURCEA(133)));
        gSetPreferredAppMode    = reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(hUx, MAKEINTRESOURCEA(135)));
        gFlushMenuThemes        = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(hUx, MAKEINTRESOURCEA(136)));
        gRefreshImmersivePolicy = reinterpret_cast<fnRefreshImmersivePolicy>(GetProcAddress(hUx, MAKEINTRESOURCEA(104)));

        if (gSetPreferredAppMode)    gSetPreferredAppMode(AppMode_ForceDark);
        if (gRefreshImmersivePolicy) gRefreshImmersivePolicy();
        if (gFlushMenuThemes)        gFlushMenuThemes();

        InstallHotPatch(hUx, "OpenThemeData",         reinterpret_cast<void*>(&DetourOpenThemeData),         gHpOpenThemeData);
        InstallHotPatch(hUx, "OpenThemeDataEx",       reinterpret_cast<void*>(&DetourOpenThemeDataEx),       gHpOpenThemeDataEx);
        InstallHotPatch(hUx, "GetThemeSysColor",      reinterpret_cast<void*>(&DetourGetThemeSysColor),      gHpGetThemeSysColor);
        InstallHotPatch(hUx, "GetThemeSysColorBrush", reinterpret_cast<void*>(&DetourGetThemeSysColorBrush), gHpGetThemeSysColorBrush);
    }

    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        gDwmSetWindowAttribute = reinterpret_cast<fnDwmSetWindowAttribute>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
    }

    HMODULE hGdi = LoadLibraryW(L"gdi32.dll");
    if (hGdi) {
        InstallHotPatch(hGdi, "FillRgn",      reinterpret_cast<void*>(&DetourFillRgn),      gHpFillRgn);
        InstallHotPatch(hGdi, "SetTextColor", reinterpret_cast<void*>(&DetourSetTextColor), gHpSetTextColor);
    }

    HMODULE hUser = LoadLibraryW(L"user32.dll");
    if (hUser) {
        InstallHotPatch(hUser, "GetSysColor",                reinterpret_cast<void*>(&DetourGetSysColor),                gHpGetSysColor);
        InstallHotPatch(hUser, "GetSysColorBrush",           reinterpret_cast<void*>(&DetourGetSysColorBrush),           gHpGetSysColorBrush);
        InstallHotPatch(hUser, "FillRect",                   reinterpret_cast<void*>(&DetourFillRect),                   gHpFillRect);
        InstallHotPatch(hUser, "DrawEdge",                   reinterpret_cast<void*>(&DetourDrawEdge),                   gHpDrawEdge);
        InstallHotPatch(hUser, "SetWindowLongA",             reinterpret_cast<void*>(&DetourSetWindowLongA),             gHpSetWindowLongA);
        InstallHotPatch(hUser, "SetWindowLongW",             reinterpret_cast<void*>(&DetourSetWindowLongW),             gHpSetWindowLongW);
        InstallHotPatch(hUser, "CreateWindowExA",            reinterpret_cast<void*>(&DetourCreateWindowExA),            gHpCreateWindowExA);
        InstallHotPatch(hUser, "CreateWindowExW",            reinterpret_cast<void*>(&DetourCreateWindowExW),            gHpCreateWindowExW);
        InstallHotPatch(hUser, "CreateDialogIndirectParamA", reinterpret_cast<void*>(&DetourCreateDialogIndirectParamA), gHpCreateDialogIndirectParamA);
        InstallHotPatch(hUser, "CreateDialogIndirectParamW", reinterpret_cast<void*>(&DetourCreateDialogIndirectParamW), gHpCreateDialogIndirectParamW);
    }
}

static BOOL CALLBACK WakeProcessWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == static_cast<DWORD>(lParam)) {
        PostMessageW(hwnd, WM_SYSCOLORCHANGE, 0, 0);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }
    return TRUE;
}

static void InstallCrashDiag();

extern "C" __declspec(dllexport) DWORD WINAPI InitializeDarkMode(LPVOID /*lpThreadId*/) {
    if (InterlockedExchange(&gInitialized, 1) != 0)
        return 1;

    InstallCrashDiag();
    LoadPaletteAndBrushes();
    InitUxThemeAndDwm();
    EnumWindows(WakeProcessWindowsProc, static_cast<LPARAM>(GetCurrentProcessId()));
    return 1;
}

// ----------------------------------------------------------------------------
// Crash breadcrumb: on stack overflow writes the return addresses found near ESP to
// WE_DarkMode\crash_diag.txt (only when a crash actually happens; zero cost otherwise).
// ----------------------------------------------------------------------------
static wchar_t gDiagPath[MAX_PATH] = {};
static PVOID   gVehHandle = nullptr;

static void DiagWrite(HANDLE h, const char* text) {
    DWORD n = 0;
    WriteFile(h, text, static_cast<DWORD>(lstrlenA(text)), &n, nullptr);
}

static LONG CALLBACK CrashDiagVeh(PEXCEPTION_POINTERS ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != 0xC00000FDu && code != 0xC000041Du) return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG sOnce = 0;
    if (InterlockedExchange(&sOnce, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;

    HANDLE h = CreateFileW(gDiagPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;

    char line[160];
    wsprintfA(line, "code=%08lX addr=%p esp=%p\r\n", code, ep->ExceptionRecord->ExceptionAddress,
              reinterpret_cast<void*>(static_cast<uintptr_t>(ep->ContextRecord->Esp)));
    DiagWrite(h, line);

    const DWORD* sp = reinterpret_cast<const DWORD*>(static_cast<uintptr_t>(ep->ContextRecord->Esp));
    int hits = 0;
    for (int i = 0; i < 6000 && hits < 160; ++i) {
        DWORD v = sp[i];
        if (v < 0x10000u) continue;
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(v - 8)), &mbi, sizeof(mbi)) != sizeof(mbi)) continue;
        if (mbi.State != MEM_COMMIT) continue;
        if ((mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) continue;
        const BYTE* b = reinterpret_cast<const BYTE*>(static_cast<uintptr_t>(v));
        if (b[-5] != 0xE8 && b[-6] != 0xFF && b[-2] != 0xFF && b[-3] != 0xFF) continue;
        HMODULE hm = nullptr;
        wchar_t name[MAX_PATH / 2] = {};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(v)), &hm) && hm) {
            GetModuleFileNameW(hm, name, MAX_PATH / 2);
        }
        const wchar_t* base = name[0] ? PathFindFileNameW(name) : L"?";
        char nameA[64] = {};
        WideCharToMultiByte(CP_ACP, 0, base, -1, nameA, 63, nullptr, nullptr);
        wsprintfA(line, "[%03d] %s+0x%lX\r\n", i, nameA, static_cast<unsigned long>(v - static_cast<DWORD>(reinterpret_cast<uintptr_t>(hm))));
        DiagWrite(h, line);
        ++hits;
    }
    CloseHandle(h);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void InstallCrashDiag() {
    if (gVehHandle || !gSelfModule) return;
    if (GetModuleFileNameW(gSelfModule, gDiagPath, MAX_PATH) == 0) return;
    wchar_t* slash = wcsrchr(gDiagPath, L'\\');
    if (!slash) return;
    wcscpy(slash + 1, L"crash_diag.txt");
    // Re-entrancy hits (nested WM_WINDOWPOSCHANGED inside WM_SIZE) are expected and harmless;
    // only log them when a "debug.flag" file sits next to the DLL.
    wchar_t flag[MAX_PATH];
    wcscpy(flag, gDiagPath);
    wcscpy(wcsrchr(flag, L'\\') + 1, L"debug.flag");
    if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES) {
        wcscpy(gGuardPath, gDiagPath);
        wcscpy(wcsrchr(gGuardPath, L'\\') + 1, L"guard_diag.txt");
    }
    gVehHandle = AddVectoredExceptionHandler(1, CrashDiagVeh);
}

static void RestoreAllHotPatches() {
    RestoreHotPatch(gHpCreateDialogIndirectParamW);
    RestoreHotPatch(gHpCreateDialogIndirectParamA);
    RestoreHotPatch(gHpCreateWindowExW);
    RestoreHotPatch(gHpCreateWindowExA);
    RestoreHotPatch(gHpSetWindowLongW);
    RestoreHotPatch(gHpSetWindowLongA);
    RestoreHotPatch(gHpDrawEdge);
    RestoreHotPatch(gHpFillRect);
    RestoreHotPatch(gHpGetSysColorBrush);
    RestoreHotPatch(gHpGetSysColor);
    RestoreHotPatch(gHpSetTextColor);
    RestoreHotPatch(gHpFillRgn);
    RestoreHotPatch(gHpGetThemeSysColorBrush);
    RestoreHotPatch(gHpGetThemeSysColor);
    RestoreHotPatch(gHpOpenThemeDataEx);
    RestoreHotPatch(gHpOpenThemeData);
}

static DWORD WINAPI JngpInitThreadProc(LPVOID /*unused*/) {
    // Initialize immediately right after LoadLibrary (before weload.dll loads tesh.dll),
    // avoiding any mid-initialization window-message race with TESH 0.7's gTeshForm!
    if (gShuttingDown) return 0;
    return InitializeDarkMode(nullptr);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        gSelfModule = hModule;
        DisableThreadLibraryCalls(hModule);
        // When loaded via JNGP's native bin\exehack.exe (grim.injectdll -> LoadLibraryA),
        // WE_DarkMode_JNGP.dll auto-initializes safely outside the OS loader lock.
        wchar_t modPath[MAX_PATH] = {};
        if (GetModuleFileNameW(hModule, modPath, MAX_PATH) > 0) {
            const wchar_t* base = PathFindFileNameW(modPath);
            if (base && (wcsstr(base, L"JNGP") != nullptr || wcsstr(base, L"jngp") != nullptr)) {
                HANDLE hThread = CreateThread(nullptr, 0, JngpInitThreadProc, nullptr, 0, nullptr);
                if (hThread) CloseHandle(hThread);
            }
        }
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        InterlockedExchange(&gShuttingDown, 1);
        RestoreAllHotPatches();
        if (gVehHandle) {
            RemoveVectoredExceptionHandler(gVehHandle);
            gVehHandle = nullptr;
        }
        if (tCbtHook) {
            UnhookWindowsHookEx(tCbtHook);
            tCbtHook = nullptr;
        }
    }
    return TRUE;
}
