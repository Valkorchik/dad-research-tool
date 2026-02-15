// ============================================================================
//  version.dll Proxy — DLL Search Order Hijacking
//
//  This DLL impersonates version.dll (a Windows system DLL loaded by nearly
//  every process). When placed in the game directory, Windows loads it before
//  the real version.dll from System32.
//
//  The proxy forwards all version.dll exports to the real DLL, while also
//  running our payload (loading Dumper-7.dll for SDK dump).
//
//  This bypasses ObRegisterCallbacks entirely because we're running INSIDE
//  the target process — no cross-process handle needed.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdio>
#include <filesystem>

// ============================================================================
//  Real version.dll function pointers (forwarded)
// ============================================================================
static HMODULE g_realVersionDll = nullptr;

// version.dll exports
using fn_GetFileVersionInfoA_t = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
using fn_GetFileVersionInfoByHandle_t = BOOL(WINAPI*)(DWORD, LPCWSTR, HANDLE, LPVOID, DWORD);
using fn_GetFileVersionInfoExA_t = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
using fn_GetFileVersionInfoExW_t = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
using fn_GetFileVersionInfoSizeA_t = DWORD(WINAPI*)(LPCSTR, LPDWORD);
using fn_GetFileVersionInfoSizeExA_t = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
using fn_GetFileVersionInfoSizeExW_t = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
using fn_GetFileVersionInfoSizeW_t = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
using fn_GetFileVersionInfoW_t = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
using fn_VerFindFileA_t = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
using fn_VerFindFileW_t = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
using fn_VerInstallFileA_t = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
using fn_VerInstallFileW_t = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
using fn_VerLanguageNameA_t = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
using fn_VerLanguageNameW_t = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
using fn_VerQueryValueA_t = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
using fn_VerQueryValueW_t = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

static fn_GetFileVersionInfoA_t      pGetFileVersionInfoA = nullptr;
static fn_GetFileVersionInfoByHandle_t pGetFileVersionInfoByHandle = nullptr;
static fn_GetFileVersionInfoExA_t    pGetFileVersionInfoExA = nullptr;
static fn_GetFileVersionInfoExW_t    pGetFileVersionInfoExW = nullptr;
static fn_GetFileVersionInfoSizeA_t  pGetFileVersionInfoSizeA = nullptr;
static fn_GetFileVersionInfoSizeExA_t pGetFileVersionInfoSizeExA = nullptr;
static fn_GetFileVersionInfoSizeExW_t pGetFileVersionInfoSizeExW = nullptr;
static fn_GetFileVersionInfoSizeW_t  pGetFileVersionInfoSizeW = nullptr;
static fn_GetFileVersionInfoW_t      pGetFileVersionInfoW = nullptr;
static fn_VerFindFileA_t             pVerFindFileA = nullptr;
static fn_VerFindFileW_t             pVerFindFileW = nullptr;
static fn_VerInstallFileA_t          pVerInstallFileA = nullptr;
static fn_VerInstallFileW_t          pVerInstallFileW = nullptr;
static fn_VerLanguageNameA_t         pVerLanguageNameA = nullptr;
static fn_VerLanguageNameW_t         pVerLanguageNameW = nullptr;
static fn_VerQueryValueA_t           pVerQueryValueA = nullptr;
static fn_VerQueryValueW_t           pVerQueryValueW = nullptr;

// ============================================================================
//  Load the real version.dll from System32 and resolve all exports
// ============================================================================
static bool LoadRealVersionDll() {
    char systemDir[MAX_PATH];
    GetSystemDirectoryA(systemDir, MAX_PATH);
    strcat_s(systemDir, "\\version.dll");

    g_realVersionDll = LoadLibraryA(systemDir);
    if (!g_realVersionDll) return false;

    pGetFileVersionInfoA       = (fn_GetFileVersionInfoA_t)GetProcAddress(g_realVersionDll, "GetFileVersionInfoA");
    pGetFileVersionInfoByHandle = (fn_GetFileVersionInfoByHandle_t)GetProcAddress(g_realVersionDll, "GetFileVersionInfoByHandle");
    pGetFileVersionInfoExA     = (fn_GetFileVersionInfoExA_t)GetProcAddress(g_realVersionDll, "GetFileVersionInfoExA");
    pGetFileVersionInfoExW     = (fn_GetFileVersionInfoExW_t)GetProcAddress(g_realVersionDll, "GetFileVersionInfoExW");
    pGetFileVersionInfoSizeA   = (fn_GetFileVersionInfoSizeA_t)GetProcAddress(g_realVersionDll, "GetFileVersionInfoSizeA");
    pGetFileVersionInfoSizeExA = (fn_GetFileVersionInfoSizeExA_t)GetProcAddress(g_realVersionDll, "GetFileVersionInfoSizeExA");
    pGetFileVersionInfoSizeExW = (fn_GetFileVersionInfoSizeExW_t)GetProcAddress(g_realVersionDll, "GetFileVersionInfoSizeExW");
    pGetFileVersionInfoSizeW   = (fn_GetFileVersionInfoSizeW_t)GetProcAddress(g_realVersionDll, "GetFileVersionInfoSizeW");
    pGetFileVersionInfoW       = (fn_GetFileVersionInfoW_t)GetProcAddress(g_realVersionDll, "GetFileVersionInfoW");
    pVerFindFileA              = (fn_VerFindFileA_t)GetProcAddress(g_realVersionDll, "VerFindFileA");
    pVerFindFileW              = (fn_VerFindFileW_t)GetProcAddress(g_realVersionDll, "VerFindFileW");
    pVerInstallFileA           = (fn_VerInstallFileA_t)GetProcAddress(g_realVersionDll, "VerInstallFileA");
    pVerInstallFileW           = (fn_VerInstallFileW_t)GetProcAddress(g_realVersionDll, "VerInstallFileW");
    pVerLanguageNameA          = (fn_VerLanguageNameA_t)GetProcAddress(g_realVersionDll, "VerLanguageNameA");
    pVerLanguageNameW          = (fn_VerLanguageNameW_t)GetProcAddress(g_realVersionDll, "VerLanguageNameW");
    pVerQueryValueA            = (fn_VerQueryValueA_t)GetProcAddress(g_realVersionDll, "VerQueryValueA");
    pVerQueryValueW            = (fn_VerQueryValueW_t)GetProcAddress(g_realVersionDll, "VerQueryValueW");

    return true;
}

// ============================================================================
//  Shared Memory Relay — bridges the ObRegisterCallbacks gap
//
//  The anti-cheat strips PROCESS_VM_READ from external handles, but code
//  running in-process can read any address freely via memcpy. This relay
//  creates a named shared memory section that the external tool can map,
//  post read requests to, and receive results from.
// ============================================================================
#include "../shared/shared_memory.h"
#include <cstring>

static HANDLE g_shmHandle = nullptr;
static SharedGameData* g_shm = nullptr;

static bool InitSharedMemory() {
    g_shmHandle = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, DAD_SHM_SIZE, DAD_SHM_NAME);

    if (!g_shmHandle) return false;

    g_shm = static_cast<SharedGameData*>(
        MapViewOfFile(g_shmHandle, FILE_MAP_ALL_ACCESS, 0, 0, DAD_SHM_SIZE));

    if (!g_shm) {
        CloseHandle(g_shmHandle);
        g_shmHandle = nullptr;
        return false;
    }

    // Initialize header
    memset(g_shm, 0, DAD_SHM_SIZE);
    g_shm->magic = DAD_SHM_MAGIC;
    g_shm->version = DAD_SHM_VERSION;
    g_shm->pid = GetCurrentProcessId();
    g_shm->heartbeat = 0;

    // Get our own module base (we're running inside the game process)
    HMODULE hGame = GetModuleHandleW(nullptr);  // main exe module
    g_shm->gameBase = reinterpret_cast<uint64_t>(hGame);

    // Get module size from PE header
    if (hGame) {
        auto dosHeader = reinterpret_cast<const uint8_t*>(hGame);
        int32_t peOffset = *reinterpret_cast<const int32_t*>(dosHeader + 0x3C);
        uint32_t sizeOfImage = *reinterpret_cast<const uint32_t*>(dosHeader + peOffset + 0x50);
        g_shm->gameSize = sizeOfImage;
    }

    // Mark all relay slots as idle
    for (int i = 0; i < DAD_RELAY_SLOTS; ++i) {
        g_shm->relay[i].state = DAD_RELAY_IDLE;
    }

    return true;
}

static void ShutdownSharedMemory() {
    if (g_shm) {
        g_shm->magic = 0;  // Signal to external tool that we're gone
        UnmapViewOfFile(g_shm);
        g_shm = nullptr;
    }
    if (g_shmHandle) {
        CloseHandle(g_shmHandle);
        g_shmHandle = nullptr;
    }
}

// Process all pending relay read requests (called in tight loop)
static void ProcessRelayRequests() {
    if (!g_shm) return;

    for (int i = 0; i < DAD_RELAY_SLOTS; ++i) {
        auto& slot = g_shm->relay[i];

        if (slot.state != DAD_RELAY_REQUEST)
            continue;

        // Validate request
        if (slot.size == 0 || slot.size > DAD_RELAY_MAX_SIZE || slot.address == 0) {
            slot.state = DAD_RELAY_ERROR;
            continue;
        }

        // Read from game memory — we're in-process, so this is just memcpy!
        // Use SEH to handle invalid addresses gracefully
        __try {
            memcpy(slot.data, reinterpret_cast<const void*>(slot.address), slot.size);
            slot.state = DAD_RELAY_READY;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            memset(slot.data, 0, slot.size);
            slot.state = DAD_RELAY_ERROR;
        }
    }
}

// ============================================================================
//  Payload — runs inside the game process on a separate thread
// ============================================================================
static DWORD WINAPI PayloadThread(LPVOID) {
    // Wait for the game to finish initializing
    Sleep(5000);

    // Create a log file in the game directory
    FILE* logFile = nullptr;
    fopen_s(&logFile, "proxy_log.txt", "w");
    auto log = [&](const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        if (logFile) {
            vfprintf(logFile, fmt, args);
            fprintf(logFile, "\n");
            fflush(logFile);
        }
        va_end(args);
    };

    log("[proxy] version.dll proxy payload started");
    log("[proxy] PID: %u", GetCurrentProcessId());

    // Initialize shared memory relay
    if (InitSharedMemory()) {
        log("[proxy] Shared memory relay initialized");
        log("[proxy]   Section: %s", DAD_SHM_NAME);
        log("[proxy]   Game base: 0x%llX", g_shm->gameBase);
        log("[proxy]   Game size: 0x%llX", g_shm->gameSize);
        log("[proxy]   SHM size:  %zu bytes", (size_t)DAD_SHM_SIZE);
    } else {
        log("[proxy] ERROR: Failed to create shared memory (error %u)", GetLastError());
        if (logFile) fclose(logFile);
        return 0;
    }

    // ── Optional: Load Dumper-7.dll if requested ──
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    auto gameDir = std::filesystem::path(modulePath).parent_path();

    // Check if Dumper-7 should be loaded (presence of "dump_sdk.txt" flag file)
    if (std::filesystem::exists(gameDir / "dump_sdk.txt")) {
        log("[proxy] dump_sdk.txt found — loading Dumper-7...");

        std::filesystem::path searchPaths[] = {
            gameDir / "Dumper-7.dll",
            gameDir / "tools" / "Dumper-7.dll",
            L"E:\\dad-research-tool\\tools\\dumper7\\build\\bin\\Release\\Dumper-7.dll",
        };

        for (auto& path : searchPaths) {
            if (std::filesystem::exists(path)) {
                log("[proxy] Loading: %ls", path.c_str());
                HMODULE hDumper = LoadLibraryW(path.c_str());
                if (hDumper) {
                    log("[proxy] Dumper-7 loaded at 0x%p", hDumper);
                } else {
                    log("[proxy] Failed to load Dumper-7 (error %u)", GetLastError());
                }
                break;
            }
        }
    }

    // ── Main relay loop — process read requests indefinitely ──
    log("[proxy] Entering relay loop...");
    if (logFile) { fclose(logFile); logFile = nullptr; }

    while (true) {
        // Process any pending memory read requests
        ProcessRelayRequests();

        // Update heartbeat so external tool knows we're alive
        if (g_shm) {
            g_shm->heartbeat++;
        }

        // Sleep briefly to avoid hogging CPU (1ms = ~1000 reads/sec throughput)
        Sleep(1);
    }

    // Cleanup (never reached in normal operation — runs until game exits)
    ShutdownSharedMemory();
    return 0;
}

// ============================================================================
//  DllMain — proxy entry point
// ============================================================================
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);

        // Load the real version.dll from System32
        if (!LoadRealVersionDll()) {
            return FALSE; // If we can't load the real DLL, don't crash the game
        }

        // Check if a "no_proxy" flag file exists (allows disabling without removing the DLL)
        wchar_t modulePath[MAX_PATH];
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        auto gameDir = std::filesystem::path(modulePath).parent_path();
        if (std::filesystem::exists(gameDir / "no_proxy.txt")) {
            return TRUE; // Proxy loaded but payload disabled
        }

        // Spawn payload on a separate thread to avoid blocking DLL loading
        CreateThread(nullptr, 0, PayloadThread, nullptr, 0, nullptr);
    }
    else if (fdwReason == DLL_PROCESS_DETACH) {
        ShutdownSharedMemory();
        if (g_realVersionDll) {
            FreeLibrary(g_realVersionDll);
            g_realVersionDll = nullptr;
        }
    }
    return TRUE;
}

// ============================================================================
//  Forwarded exports — these must match version.dll's export table exactly
// ============================================================================
extern "C" {

__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    return pGetFileVersionInfoA ? pGetFileVersionInfoA(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoByHandle(DWORD dwFlags, LPCWSTR lpwstrFilename, HANDLE hMappedFile, LPVOID lpData, DWORD dwLen) {
    return pGetFileVersionInfoByHandle ? pGetFileVersionInfoByHandle(dwFlags, lpwstrFilename, hMappedFile, lpData, dwLen) : FALSE;
}

__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    return pGetFileVersionInfoExA ? pGetFileVersionInfoExA(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    return pGetFileVersionInfoExW ? pGetFileVersionInfoExW(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) DWORD WINAPI proxy_GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    return pGetFileVersionInfoSizeA ? pGetFileVersionInfoSizeA(lptstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) DWORD WINAPI proxy_GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle) {
    return pGetFileVersionInfoSizeExA ? pGetFileVersionInfoSizeExA(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) DWORD WINAPI proxy_GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle) {
    return pGetFileVersionInfoSizeExW ? pGetFileVersionInfoSizeExW(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) DWORD WINAPI proxy_GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    return pGetFileVersionInfoSizeW ? pGetFileVersionInfoSizeW(lptstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    return pGetFileVersionInfoW ? pGetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) DWORD WINAPI proxy_VerFindFileA(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT puCurDirLen, LPSTR szDestDir, PUINT puDestDirLen) {
    return pVerFindFileA ? pVerFindFileA(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen) : 0;
}

__declspec(dllexport) DWORD WINAPI proxy_VerFindFileW(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT puCurDirLen, LPWSTR szDestDir, PUINT puDestDirLen) {
    return pVerFindFileW ? pVerFindFileW(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen) : 0;
}

__declspec(dllexport) DWORD WINAPI proxy_VerInstallFileA(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT puTmpFileLen) {
    return pVerInstallFileA ? pVerInstallFileA(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen) : 0;
}

__declspec(dllexport) DWORD WINAPI proxy_VerInstallFileW(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT puTmpFileLen) {
    return pVerInstallFileW ? pVerInstallFileW(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen) : 0;
}

__declspec(dllexport) DWORD WINAPI proxy_VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang) {
    return pVerLanguageNameA ? pVerLanguageNameA(wLang, szLang, cchLang) : 0;
}

__declspec(dllexport) DWORD WINAPI proxy_VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang) {
    return pVerLanguageNameW ? pVerLanguageNameW(wLang, szLang, cchLang) : 0;
}

__declspec(dllexport) BOOL WINAPI proxy_VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    return pVerQueryValueA ? pVerQueryValueA(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

__declspec(dllexport) BOOL WINAPI proxy_VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    return pVerQueryValueW ? pVerQueryValueW(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

} // extern "C"
