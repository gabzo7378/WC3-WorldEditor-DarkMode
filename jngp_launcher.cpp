// NewGen_WE_Dark.exe — Portable Per-Process Dark Mode Launcher for Jass NewGen Pack (JNGP 5d / 1.4g / 2.0.x / Rebuild)
// Portable folder layout (drop into ANY Jass NewGen Pack folder on ANY PC):
//   <JNGP Dir>\
//     NewGen WE.exe          <-- Original JNGP launcher (untouched)
//     NewGen_WE_Dark.exe     <-- Dark Mode JNGP launcher (no custom icon yet, per user configuration)
//     WE_DarkMode\
//       WE_DarkMode_JNGP.dll <-- UltraDark + TESH/Scintilla engine
//       WE_DarkMode.ini      <-- Customizable hex color palette
//
// How it works without modifying we.lua, NewGen WE.exe, or tesh\config\styles.ini:
//   1. Snapshots existing worldedit121.exe / worldedit.exe PIDs before launching JNGP.
//   2. Starts "NewGen WE.exe" (which runs UMSWE setup if enabled, then bin\exehack.exe -> we.lua -> worldedit121.exe).
//   3. Detects the newly spawned worldedit121.exe / worldedit.exe process, waits briefly until Grimoire's
//      wehack.dll / weload.dll finishes loading so there is zero injection race, and injects WE_DarkMode_JNGP.dll.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

static bool FileExistsW(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool IsWorldEditProcessName(const wchar_t* exeFile) {
    if (!exeFile) return false;
    return _wcsicmp(exeFile, L"worldedit121.exe") == 0 ||
           _wcsicmp(exeFile, L"worldedit.exe") == 0 ||
           _wcsicmp(exeFile, L"World Editor.exe") == 0;
}

static bool IsJngpChainAlive() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    bool alive = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"NewGen WE.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"exehack.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"PatchGenerator.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"hfix.exe") == 0)
            {
                alive = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return alive;
}

static std::vector<DWORD> SnapshotWorldEditPids() {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (IsWorldEditProcessName(pe.szExeFile)) {
                pids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

static DWORD FindNewWorldEditPid(const std::vector<DWORD>& priorPids) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    DWORD foundPid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (IsWorldEditProcessName(pe.szExeFile)) {
                if (std::find(priorPids.begin(), priorPids.end(), pe.th32ProcessID) == priorPids.end()) {
                    foundPid = pe.th32ProcessID;
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return foundPid;
}

static DWORD FindMainThreadId(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    THREADENTRY32 te = {};
    te.dwSize = sizeof(te);
    DWORD bestTid = 0;
    ULONGLONG bestCreateTime = ~0ULL;

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                if (bestTid == 0) bestTid = te.th32ThreadID;
                HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (hThread) {
                    FILETIME ftCreate = {}, ftExit = {}, ftKernel = {}, ftUser = {};
                    if (GetThreadTimes(hThread, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                        ULONGLONG t = (static_cast<ULONGLONG>(ftCreate.dwHighDateTime) << 32) | ftCreate.dwLowDateTime;
                        if (t < bestCreateTime) {
                            bestCreateTime = t;
                            bestTid = te.th32ThreadID;
                        }
                    }
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return bestTid;
}

static void WaitForGrimoireModules(HANDLE hProcess) {
    // Wait up to 600ms for Grimoire (wehack.dll / tesh.dll) to finish its own startup injection
    DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < 600) {
        HMODULE mods[256] = {};
        DWORD needed = 0;
        if (EnumProcessModules(hProcess, mods, sizeof(mods), &needed)) {
            bool hasWehack = false;
            DWORD count = needed / sizeof(HMODULE);
            if (count > 256) count = 256;
            for (DWORD i = 0; i < count; ++i) {
                wchar_t modName[MAX_PATH] = {};
                if (GetModuleBaseNameW(hProcess, mods[i], modName, MAX_PATH) > 0) {
                    if (_wcsicmp(modName, L"wehack.dll") == 0) {
                        hasWehack = true;
                        break;
                    }
                }
            }
            if (hasWehack) {
                Sleep(35); // Small settle window for tesh.dll / PELoader.dll right after wehack.dll
                return;
            }
        }
        Sleep(15);
    }
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

static bool InjectDarkModeIntoPid(DWORD pid, const std::wstring& dllPath) {
    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE,
        pid);
    if (!hProcess) return false;

    WaitForGrimoireModules(hProcess);

    bool ok = false;
    SIZE_T byteLen = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remotePath = VirtualAllocEx(hProcess, nullptr, byteLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remotePath) {
        if (WriteProcessMemory(hProcess, remotePath, dllPath.c_str(), byteLen, nullptr)) {
            HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
            auto pLoadLibW = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(hKernel, "LoadLibraryW"));

            DWORD remoteDllBase = 0;
            if (pLoadLibW && RunRemoteProc(hProcess, pLoadLibW, remotePath, &remoteDllBase) && remoteDllBase != 0) {
                HMODULE hLocal = LoadLibraryExW(dllPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
                if (hLocal) {
                    auto pLocalInit = reinterpret_cast<uintptr_t>(GetProcAddress(hLocal, "InitializeDarkMode"));
                    if (pLocalInit) {
                        uintptr_t rva = pLocalInit - reinterpret_cast<uintptr_t>(hLocal);
                        auto pRemoteInit = reinterpret_cast<LPTHREAD_START_ROUTINE>(
                            static_cast<uintptr_t>(remoteDllBase) + rva);
                        DWORD mainTid = FindMainThreadId(pid);
                        RunRemoteProc(hProcess, pRemoteInit, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(mainTid)));
                        ok = true;
                    }
                    FreeLibrary(hLocal);
                }
            }
        }
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
    }

    CloseHandle(hProcess);
    return ok;
}

static bool BrowseForJngpExe(std::wstring& outPath) {
    wchar_t fileBuf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Jass NewGen Pack (NewGen WE.exe)\0NewGen WE.exe;exehack.exe\0Ejecutables (*.exe)\0*.exe\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Selecciona tu NewGen WE.exe (Jass NewGen Pack)";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        outPath = fileBuf;
        return true;
    }
    return false;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t selfBuf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfBuf, MAX_PATH);
    std::wstring selfExe(selfBuf);
    PathRemoveFileSpecW(selfBuf);
    std::wstring baseDir(selfBuf);

    // 1. Locate WE_DarkMode_JNGP.dll inside .\WE_DarkMode\ (fallback to WE_DarkMode.dll or same directory)
    std::wstring subDir  = baseDir + L"\\WE_DarkMode";
    std::wstring dllPath = subDir + L"\\WE_DarkMode_JNGP.dll";
    std::wstring iniPath = subDir + L"\\WE_DarkMode.ini";

    if (!FileExistsW(dllPath)) {
        std::wstring altSubDll = subDir + L"\\WE_DarkMode.dll";
        if (FileExistsW(altSubDll)) {
            dllPath = altSubDll;
        } else if (FileExistsW(baseDir + L"\\WE_DarkMode_JNGP.dll")) {
            dllPath = baseDir + L"\\WE_DarkMode_JNGP.dll";
            iniPath = baseDir + L"\\WE_DarkMode.ini";
        } else if (FileExistsW(baseDir + L"\\WE_DarkMode.dll")) {
            dllPath = baseDir + L"\\WE_DarkMode.dll";
            iniPath = baseDir + L"\\WE_DarkMode.ini";
        }
    }

    if (!FileExistsW(dllPath)) {
        MessageBoxW(
            nullptr,
            L"No se encontró WE_DarkMode_JNGP.dll.\nColoca la carpeta 'WE_DarkMode' junto a NewGen_WE_Dark.exe.",
            L"JNGP Dark Mode",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    // Check if re-invoked elevated strictly to inject into a newly spawned worldedit121.exe
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && _wcsicmp(argv[1], L"--inject-pid") == 0) {
        DWORD targetPid = static_cast<DWORD>(wcstoul(argv[2], nullptr, 10));
        LocalFree(argv);
        return InjectDarkModeIntoPid(targetPid, dllPath) ? 0 : 3;
    }

    // 2. Read optional JNGP executable / arguments from INI
    wchar_t exeBuf[MAX_PATH] = {};
    wchar_t argsBuf[1024] = {};
    if (FileExistsW(iniPath)) {
        GetPrivateProfileStringW(L"General", L"JngpPath", L"", exeBuf, MAX_PATH, iniPath.c_str());
        GetPrivateProfileStringW(L"General", L"Arguments", L"", argsBuf, 1024, iniPath.c_str());
    }

    std::wstring jngpExe = exeBuf;
    std::wstring extraArgs = argsBuf;

    if (argv && argc >= 2) {
        std::wstring arg1 = argv[1];
        const wchar_t* ext = PathFindExtensionW(arg1.c_str());
        if (ext && (_wcsicmp(ext, L".w3x") == 0 || _wcsicmp(ext, L".w3m") == 0) && FileExistsW(arg1)) {
            if (!extraArgs.empty()) extraArgs += L" ";
            extraArgs += L"-loadfile \"" + arg1 + L"\"";
        }
    }
    if (argv) LocalFree(argv);

    // 3. Auto-detect NewGen WE.exe (or bin\exehack.exe) right inside the JNGP folder
    bool directExeHack = false;
    if (jngpExe.empty() || !FileExistsW(jngpExe)) {
        std::wstring localNewGen  = baseDir + L"\\NewGen WE.exe";
        std::wstring localExeHack = baseDir + L"\\bin\\exehack.exe";
        if (FileExistsW(localNewGen)) {
            jngpExe = localNewGen;
        } else if (FileExistsW(localExeHack)) {
            jngpExe = localExeHack;
            directExeHack = true;
        } else {
            if (!BrowseForJngpExe(jngpExe)) {
                return 0;
            }
            WritePrivateProfileStringW(L"General", L"JngpPath", jngpExe.c_str(), iniPath.c_str());
        }
    }

    wchar_t workDirBuf[MAX_PATH] = {};
    wcsncpy(workDirBuf, jngpExe.c_str(), MAX_PATH - 1);
    PathRemoveFileSpecW(workDirBuf);
    if (directExeHack) {
        PathRemoveFileSpecW(workDirBuf); // Root of JNGP where we.lua lives
    }

    // 4. Snapshot existing worldedit* processes before launching JNGP
    std::vector<DWORD> priorPids = SnapshotWorldEditPids();

    std::wstring launchArgs;
    if (directExeHack) {
        launchArgs = L"-s we.lua";
    }
    if (!extraArgs.empty()) {
        if (!launchArgs.empty()) launchArgs += L" ";
        launchArgs += extraArgs;
    }

    std::wstring cmdLine = L"\"" + jngpExe + L"\"";
    if (!launchArgs.empty()) {
        cmdLine += L" " + launchArgs;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL launched = CreateProcessW(
        jngpExe.c_str(),
        cmdLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        workDirBuf,
        &si,
        &pi);

    if (launched) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize       = sizeof(sei);
        sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb       = L"runas";
        sei.lpFile       = jngpExe.c_str();
        sei.lpParameters = launchArgs.empty() ? nullptr : launchArgs.c_str();
        sei.lpDirectory  = workDirBuf;
        sei.nShow        = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) {
            return 2;
        }
        if (sei.hProcess) CloseHandle(sei.hProcess);
    } else {
        std::wstring msg = L"No se pudo iniciar Jass NewGen Pack:\n" + jngpExe +
                           L"\n\nCódigo de error: " + std::to_wstring(GetLastError());
        MessageBoxW(nullptr, msg.c_str(), L"JNGP Dark Mode", MB_OK | MB_ICONERROR);
        return 2;
    }

    // 5. Watch for the newly spawned worldedit121.exe / worldedit.exe process
    DWORD startTick = GetTickCount();
    DWORD quietSince = 0;
    DWORD targetPid = 0;

    while (GetTickCount() - startTick < 45000) {
        targetPid = FindNewWorldEditPid(priorPids);
        if (targetPid != 0) {
            break;
        }

        if (GetTickCount() - startTick > 800) {
            if (!IsJngpChainAlive()) {
                if (quietSince == 0) {
                    quietSince = GetTickCount();
                } else if (GetTickCount() - quietSince > 1500) {
                    return 0; // User cancelled folder browse or JNGP closed without spawning WE
                }
            } else {
                quietSince = 0;
            }
        }
        Sleep(15);
    }

    if (targetPid == 0) {
        return 0;
    }

    // 6. Inject WE_DarkMode_JNGP.dll (if target runs elevated while we didn't, elevate helper once for --inject-pid)
    if (!InjectDarkModeIntoPid(targetPid, dllPath)) {
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            std::wstring elevParams = L"--inject-pid " + std::to_wstring(targetPid);
            SHELLEXECUTEINFOW sei = {};
            sei.cbSize       = sizeof(sei);
            sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb       = L"runas";
            sei.lpFile       = selfExe.c_str();
            sei.lpParameters = elevParams.c_str();
            sei.lpDirectory  = baseDir.c_str();
            sei.nShow        = SW_HIDE;
            if (ShellExecuteExW(&sei) && sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 15000);
                CloseHandle(sei.hProcess);
                return 0;
            }
        }
        MessageBoxW(
            nullptr,
            L"Jass NewGen Pack abrió el editor, pero no se pudo inyectar WE_DarkMode_JNGP.dll.\nIntenta ejecutar NewGen_WE_Dark.exe como Administrador.",
            L"JNGP Dark Mode",
            MB_OK | MB_ICONWARNING);
        return 3;
    }

    return 0;
}
