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
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    PathRemoveFileSpecW(selfPath);

    std::wstring baseDir(selfPath);

    // 1. Prefer clean subfolder .\WE_DarkMode\ (for neat WC3 root folder), fallback to same folder
    std::wstring subDir  = baseDir + L"\\WE_DarkMode";
    std::wstring dllPath = subDir + L"\\WE_DarkMode.dll";
    std::wstring iniPath = subDir + L"\\WE_DarkMode.ini";

    if (!FileExistsW(dllPath)) {
        dllPath = baseDir + L"\\WE_DarkMode.dll";
        iniPath = baseDir + L"\\WE_DarkMode.ini";
    }

    if (!FileExistsW(dllPath)) {
        MessageBoxW(
            nullptr,
            L"No se encontró WE_DarkMode.dll.\nColoca la carpeta 'WE_DarkMode' junto a WorldEditor_Dark.exe.",
            L"World Editor Dark Mode",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    // 2. Read optional overrides from INI (WorldEditPath can be blank for automatic portable detection!)
    wchar_t exeBuf[MAX_PATH] = {};
    wchar_t argsBuf[1024] = {};
    if (FileExistsW(iniPath)) {
        GetPrivateProfileStringW(L"General", L"WorldEditPath", L"", exeBuf, MAX_PATH, iniPath.c_str());
        GetPrivateProfileStringW(L"General", L"Arguments", L"", argsBuf, 1024, iniPath.c_str());
    }

    std::wstring targetExe = exeBuf;
    std::wstring extraArgs = argsBuf;

    // 3. Support drag-and-drop of either an .exe (e.g. worldedit121.exe) or a .w3x/.w3m map
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) {
        std::wstring arg1 = argv[1];
        const wchar_t* ext = PathFindExtensionW(arg1.c_str());
        if (ext && _wcsicmp(ext, L".exe") == 0 && FileExistsW(arg1)) {
            targetExe = arg1;
        } else if (ext && (_wcsicmp(ext, L".w3x") == 0 || _wcsicmp(ext, L".w3m") == 0) && FileExistsW(arg1)) {
            if (!extraArgs.empty()) extraArgs += L" ";
            extraArgs += L"-loadfile \"" + arg1 + L"\"";
        }
    }
    if (argv) LocalFree(argv);

    // 4. Portable auto-detection: check worldedit.exe / worldedit121.exe right beside WorldEditor_Dark.exe
    if (targetExe.empty() || !FileExistsW(targetExe)) {
        std::wstring localWe    = baseDir + L"\\worldedit.exe";
        std::wstring localWe121 = baseDir + L"\\worldedit121.exe";
        if (FileExistsW(localWe)) {
            targetExe = localWe;
        } else if (FileExistsW(localWe121)) {
            targetExe = localWe121;
        } else {
            if (!BrowseForWorldEditExe(targetExe)) {
                return 0;
            }
            WritePrivateProfileStringW(L"General", L"WorldEditPath", targetExe.c_str(), iniPath.c_str());
        }
    }

    // Working directory = Warcraft III folder
    wchar_t workDirBuf[MAX_PATH] = {};
    wcsncpy(workDirBuf, targetExe.c_str(), MAX_PATH - 1);
    PathRemoveFileSpecW(workDirBuf);

    std::wstring cmdLine = L"\"" + targetExe + L"\"";
    if (!extraArgs.empty()) {
        cmdLine += L" " + extraArgs;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(
            targetExe.c_str(),
            cmdLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_SUSPENDED,
            nullptr,
            workDirBuf,
            &si,
            &pi))
    {
        std::wstring msg = L"No se pudo iniciar:\n" + targetExe + L"\n\nCódigo de error: " + std::to_wstring(GetLastError());
        MessageBoxW(nullptr, msg.c_str(), L"World Editor Dark Mode", MB_OK | MB_ICONERROR);
        return 2;
    }

    bool ok = false;
    SIZE_T byteLen = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remotePath = VirtualAllocEx(pi.hProcess, nullptr, byteLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remotePath) {
        if (WriteProcessMemory(pi.hProcess, remotePath, dllPath.c_str(), byteLen, nullptr)) {
            HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
            auto pLoadLibW = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(hKernel, "LoadLibraryW"));

            DWORD remoteDllBase = 0;
            if (pLoadLibW && RunRemoteProc(pi.hProcess, pLoadLibW, remotePath, &remoteDllBase) && remoteDllBase != 0) {
                HMODULE hLocal = LoadLibraryExW(dllPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
                if (hLocal) {
                    auto pLocalInit = reinterpret_cast<uintptr_t>(GetProcAddress(hLocal, "InitializeDarkMode"));
                    if (pLocalInit) {
                        uintptr_t rva = pLocalInit - reinterpret_cast<uintptr_t>(hLocal);
                        auto pRemoteInit = reinterpret_cast<LPTHREAD_START_ROUTINE>(
                            static_cast<uintptr_t>(remoteDllBase) + rva);
                        RunRemoteProc(pi.hProcess, pRemoteInit, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(pi.dwThreadId)));
                        ok = true;
                    }
                    FreeLibrary(hLocal);
                }
            }
        }
        VirtualFreeEx(pi.hProcess, remotePath, 0, MEM_RELEASE);
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!ok) {
        MessageBoxW(
            nullptr,
            L"World Editor se abrió, pero no se pudo inicializar WE_DarkMode.dll.\nVerifica que worldedit.exe sea de 32 bits (WC3 1.21–1.27b).",
            L"World Editor Dark Mode",
            MB_OK | MB_ICONWARNING);
        return 3;
    }

    return 0;
}
