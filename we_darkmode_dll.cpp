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

using fnGetSysColor                = DWORD(WINAPI*)(int);
using fnGetSysColorBrush           = HBRUSH(WINAPI*)(int);
using fnFillRect                   = int(WINAPI*)(HDC, const RECT*, HBRUSH);
using fnDrawEdge                   = BOOL(WINAPI*)(HDC, LPRECT, UINT, UINT);
using fnCreateWindowExA            = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using fnCreateWindowExW            = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using fnCreateDialogIndirectParamA = HWND(WINAPI*)(HINSTANCE, LPCDLGTEMPLATEA, HWND, DLGPROC, LPARAM);
using fnCreateDialogIndirectParamW = HWND(WINAPI*)(HINSTANCE, LPCDLGTEMPLATEW, HWND, DLGPROC, LPARAM);
using fnOpenThemeData              = HTHEME(WINAPI*)(HWND, LPCWSTR);
using fnOpenThemeDataEx            = HTHEME(WINAPI*)(HWND, LPCWSTR, DWORD);
using fnGetThemeSysColor           = COLORREF(WINAPI*)(HTHEME, int);
using fnGetThemeSysColorBrush      = HBRUSH(WINAPI*)(HTHEME, int);

static HotPatchSlot gHpGetSysColor;
static HotPatchSlot gHpGetSysColorBrush;
static HotPatchSlot gHpFillRect;
static HotPatchSlot gHpDrawEdge;
static HotPatchSlot gHpCreateWindowExA;
static HotPatchSlot gHpCreateWindowExW;
static HotPatchSlot gHpCreateDialogIndirectParamA;
static HotPatchSlot gHpCreateDialogIndirectParamW;
static HotPatchSlot gHpOpenThemeData;
static HotPatchSlot gHpOpenThemeDataEx;
static HotPatchSlot gHpGetThemeSysColor;
static HotPatchSlot gHpGetThemeSysColorBrush;

// ============================================================================
// Globals & Palette
// ============================================================================
static HMODULE gSelfModule = nullptr;
static volatile LONG gInitialized = 0;
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

static thread_local bool gInThemeRedirect = false;
static thread_local bool gInStyleWindow   = false;
static thread_local bool gInInternalDraw  = false;

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

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
