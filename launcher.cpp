// WorldEditor_Dark.exe — Portable Per-Process Dark Mode Launcher for Warcraft III World Editor (x86)
// Portable folder layout (drop into ANY Warcraft III folder on ANY PC):
//   <Warcraft III Dir>\
//     worldedit.exe
//     WorldEditor_Dark.exe
//     WE_DarkMode\
//       WE_DarkMode.dll
//       WE_DarkMode.ini
// Also works if WE_DarkMode.dll & WE_DarkMode.ini sit directly beside WorldEditor_Dark.exe.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <string>
#include <cstdint>

static bool FileExistsW(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool BrowseForWorldEditExe(std::wstring& outPath) {
    wchar_t fileBuf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Warcraft III World Editor (worldedit*.exe)\0worldedit*.exe\0Ejecutables (*.exe)\0*.exe\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Selecciona tu worldedit.exe (Warcraft III)";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        outPath = fileBuf;
        return true;
    }
    return false;
}

static bool RunRemoteProc(HANDLE hProcess, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD* outExitCode = nullptr) {
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, fn, arg, 0, nullptr);
    if (!hThread) return false;
    WaitForSingleObject(hThread, 15000);
    if (outExitCode) {
        GetExitCodeThread(hThread, outExitCode);
    }
    CloseHandle(hThread);
    return true;
}


int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return 0;
}
