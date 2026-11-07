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


BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
