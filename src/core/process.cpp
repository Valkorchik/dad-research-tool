#include "process.h"
#include "../shared/shared_memory.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

// Enable SeDebugPrivilege — required to read protected processes
static bool EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        spdlog::warn("OpenProcessToken failed: {}", GetLastError());
        return false;
    }

    LUID luid = {};
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        spdlog::warn("LookupPrivilegeValue failed: {}", GetLastError());
        CloseHandle(hToken);
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr)) {
        spdlog::warn("AdjustTokenPrivileges failed: {}", GetLastError());
        CloseHandle(hToken);
        return false;
    }

    DWORD err = GetLastError();
    CloseHandle(hToken);

    if (err == ERROR_NOT_ALL_ASSIGNED) {
        spdlog::warn("SeDebugPrivilege not available (not running as admin?)");
        return false;
    }

    spdlog::info("SeDebugPrivilege enabled successfully");
    return true;
}

Process::~Process() {
    Detach();
}

bool Process::Attach(const std::wstring& processName) {
    DWORD pid = FindProcessId(processName);
    if (pid == 0) {
        // Convert wide string to narrow for logging
        char narrow[256] = {};
        size_t converted = 0;
        wcstombs_s(&converted, narrow, processName.c_str(), sizeof(narrow) - 1);
        spdlog::warn("Process '{}' not found", narrow);
        return false;
    }
    m_processName = processName;
    return Attach(pid);
}

bool Process::Attach(DWORD pid) {
    Detach();

    // Enable debug privilege before opening — critical for anti-cheat protected processes
    static bool debugPrivEnabled = false;
    if (!debugPrivEnabled) {
        debugPrivEnabled = EnableDebugPrivilege();
    }

    // Try PROCESS_ALL_ACCESS first (best chance against handle stripping)
    m_handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!m_handle) {
        spdlog::warn("PROCESS_ALL_ACCESS failed (error {}), trying limited access...", GetLastError());
        m_handle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    }
    if (!m_handle) {
        spdlog::error("Failed to open process PID {}. Error: {}", pid, GetLastError());
        return false;
    }

    // Verify we actually have VM_READ by doing a test read
    {
        uintptr_t testVal = 0;
        SIZE_T bytesRead = 0;
        // Try reading the PEB address via NtQueryInformationProcess to verify handle works
        typedef NTSTATUS(WINAPI* NtQueryFn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
        auto NtQuery = reinterpret_cast<NtQueryFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
        if (NtQuery) {
            struct { PVOID r1; PVOID PebBase; PVOID r2[2]; ULONG_PTR pid; PVOID r3; } pbi = {};
            ULONG len = 0;
            NTSTATUS st = NtQuery(m_handle, 0, &pbi, sizeof(pbi), &len);
            spdlog::info("Handle access test: NtQuery status=0x{:X}, PEB=0x{:X}",
                (unsigned long)st, reinterpret_cast<uintptr_t>(pbi.PebBase));

            if (st == 0 && pbi.PebBase) {
                // Try actual memory read
                if (ReadProcessMemory(m_handle, reinterpret_cast<LPCVOID>(
                    reinterpret_cast<uintptr_t>(pbi.PebBase)), &testVal, sizeof(testVal), &bytesRead)) {
                    spdlog::info("VM_READ verified: read {} bytes from PEB", bytesRead);
                } else {
                    spdlog::warn("VM_READ blocked by anti-cheat (error {}) — will try shared memory relay", GetLastError());
                }
            }
        }
    }

    m_pid = pid;
    if (!EnumerateModules()) {
        spdlog::warn("Failed to enumerate modules for PID {}", pid);
    }

    spdlog::info("Attached to PID {} with {} modules", pid, m_modules.size());
    return true;
}

void Process::Detach() {
    if (m_shm) {
        UnmapViewOfFile(m_shm);
        m_shm = nullptr;
    }
    if (m_shmHandle) {
        CloseHandle(m_shmHandle);
        m_shmHandle = nullptr;
    }
    m_useRelay = false;

    if (m_handle) {
        CloseHandle(m_handle);
        m_handle = nullptr;
    }
    m_pid = 0;
    m_modules.clear();
}

uintptr_t Process::GetModuleBase(const std::wstring& moduleName) const {
    // Case-insensitive lookup
    std::wstring lower = moduleName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    for (const auto& [name, info] : m_modules) {
        std::wstring nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
        if (nameLower == lower)
            return info.base;
    }
    return 0;
}

DWORD Process::GetModuleSize(const std::wstring& moduleName) const {
    std::wstring lower = moduleName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    for (const auto& [name, info] : m_modules) {
        std::wstring nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
        if (nameLower == lower)
            return info.size;
    }
    return 0;
}

bool Process::ReadRaw(uintptr_t address, void* buffer, size_t size) const {
    if (!address || !buffer)
        return false;

    // If using shared memory relay, go through that instead of RPM
    if (m_useRelay) {
        return RelayRead(address, buffer, size);
    }

    if (!m_handle)
        return false;

    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(m_handle, reinterpret_cast<LPCVOID>(address), buffer, size, &bytesRead)) {
        return false;
    }
    return bytesRead == size;
}

uintptr_t Process::ReadPointerChain(uintptr_t base, const std::vector<uintptr_t>& offsets) const {
    uintptr_t addr = base;
    for (size_t i = 0; i < offsets.size(); ++i) {
        addr = Read<uintptr_t>(addr);
        if (!addr) return 0;
        addr += offsets[i];
    }
    return addr;
}

bool Process::IsValid() const {
    if (!m_handle) return false;
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(m_handle, &exitCode))
        return false;
    return exitCode == STILL_ACTIVE;
}

bool Process::EnumerateModules() {
    m_modules.clear();

    // Method 1: CreateToolhelp32Snapshot (standard approach)
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);

        if (Module32FirstW(snap, &me)) {
            do {
                ModuleInfo info;
                info.base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                info.size = me.modBaseSize;
                info.name = me.szModule;
                m_modules[me.szModule] = info;
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);

        if (!m_modules.empty()) {
            spdlog::info("EnumerateModules: Toolhelp found {} modules", m_modules.size());
            return true;
        }
    } else {
        spdlog::warn("CreateToolhelp32Snapshot failed (error {}), trying fallbacks...", GetLastError());
    }

    // Method 2: EnumProcessModulesEx (PSAPI) — works when toolhelp is blocked
    {
        // Re-open with broader access for PSAPI
        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, m_pid);
        if (hProc) {
            HMODULE hMods[1024];
            DWORD cbNeeded = 0;

            if (EnumProcessModulesEx(hProc, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL)) {
                DWORD moduleCount = cbNeeded / sizeof(HMODULE);
                spdlog::info("EnumProcessModulesEx found {} modules", moduleCount);

                for (DWORD i = 0; i < moduleCount; ++i) {
                    wchar_t modName[MAX_PATH] = {};
                    MODULEINFO modInfo = {};

                    if (GetModuleFileNameExW(hProc, hMods[i], modName, MAX_PATH)) {
                        GetModuleInformation(hProc, hMods[i], &modInfo, sizeof(modInfo));

                        // Extract filename from full path
                        std::wstring fullPath(modName);
                        std::wstring fileName = fullPath;
                        auto lastSlash = fullPath.find_last_of(L"\\/");
                        if (lastSlash != std::wstring::npos)
                            fileName = fullPath.substr(lastSlash + 1);

                        ModuleInfo info;
                        info.base = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
                        info.size = modInfo.SizeOfImage;
                        info.name = fileName;
                        m_modules[fileName] = info;
                    }
                }
            } else {
                spdlog::warn("EnumProcessModulesEx failed (error {})", GetLastError());
            }
            CloseHandle(hProc);

            if (!m_modules.empty()) {
                return true;
            }
        }
    }

    // Method 3: PEB-based fallback for main module base address
    {
        spdlog::warn("All module enum methods failed; trying PEB fallback for main module...");

        typedef NTSTATUS(WINAPI* NtQueryInformationProcessFn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            auto NtQueryInformationProcess = reinterpret_cast<NtQueryInformationProcessFn>(
                GetProcAddress(ntdll, "NtQueryInformationProcess"));

            if (NtQueryInformationProcess) {
                struct PROCESS_BASIC_INFORMATION {
                    PVOID Reserved1;
                    PVOID PebBaseAddress;
                    PVOID Reserved2[2];
                    ULONG_PTR UniqueProcessId;
                    PVOID Reserved3;
                };

                PROCESS_BASIC_INFORMATION pbi = {};
                ULONG len = 0;
                NTSTATUS status = NtQueryInformationProcess(m_handle, 0 /*ProcessBasicInformation*/, &pbi, sizeof(pbi), &len);

                spdlog::info("NtQueryInformationProcess: status=0x{:X}, PEB=0x{:X}",
                    (unsigned long)status, reinterpret_cast<uintptr_t>(pbi.PebBaseAddress));

                if (status == 0 && pbi.PebBaseAddress) {
                    uintptr_t pebAddr = reinterpret_cast<uintptr_t>(pbi.PebBaseAddress);
                    uintptr_t imageBase = 0;
                    bool readOk = ReadRaw(pebAddr + 0x10, &imageBase, sizeof(imageBase));
                    spdlog::info("PEB read: ok={}, ImageBaseAddress=0x{:X}", readOk, imageBase);

                    if (readOk && imageBase) {
                        // Get the actual process name for the module entry
                        wchar_t imagePath[MAX_PATH] = {};
                        DWORD pathSize = MAX_PATH;
                        QueryFullProcessImageNameW(m_handle, 0, imagePath, &pathSize);

                        std::wstring fullPath(imagePath);
                        std::wstring fileName = fullPath;
                        auto lastSlash = fullPath.find_last_of(L"\\/");
                        if (lastSlash != std::wstring::npos)
                            fileName = fullPath.substr(lastSlash + 1);

                        // Estimate module size from PE header
                        DWORD moduleSize = 0;
                        // Read DOS header -> e_lfanew -> NT header -> SizeOfImage
                        uint16_t dosMagic = 0;
                        ReadRaw(imageBase, &dosMagic, 2);
                        if (dosMagic == 0x5A4D) { // "MZ"
                            int32_t peOffset = 0;
                            ReadRaw(imageBase + 0x3C, &peOffset, 4);
                            // SizeOfImage is at NT_HEADER + 0x18 (OptionalHeader) + 0x38 (SizeOfImage) for 64-bit
                            ReadRaw(imageBase + peOffset + 0x50, &moduleSize, 4);
                        }
                        if (!moduleSize) moduleSize = 0x10000000; // 256MB fallback

                        ModuleInfo info;
                        info.base = imageBase;
                        info.size = moduleSize;
                        info.name = fileName;
                        m_modules[fileName] = info;

                        // Also add under the name the caller might use
                        if (fileName != m_processName && !m_processName.empty()) {
                            m_modules[m_processName] = info;
                        }

                        char narrowName[256] = {};
                        size_t cvt = 0;
                        wcstombs_s(&cvt, narrowName, fileName.c_str(), sizeof(narrowName) - 1);
                        spdlog::info("PEB fallback: {} at 0x{:X} (size 0x{:X})",
                            narrowName, imageBase, moduleSize);
                        return true;
                    }
                }
            }
        }
    }

    // Method 4: Shared memory relay from version.dll proxy
    if (ConnectSharedMemoryRelay()) {
        spdlog::info("Connected to shared memory relay! gameBase=0x{:X}, size=0x{:X}",
            m_shm->gameBase, m_shm->gameSize);

        // Get the actual process image name
        wchar_t imagePath[MAX_PATH] = {};
        DWORD pathSize = MAX_PATH;
        QueryFullProcessImageNameW(m_handle, 0, imagePath, &pathSize);
        std::wstring fullPath(imagePath);
        std::wstring fileName = fullPath;
        auto lastSlash = fullPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
            fileName = fullPath.substr(lastSlash + 1);

        ModuleInfo info;
        info.base = static_cast<uintptr_t>(m_shm->gameBase);
        info.size = static_cast<DWORD>(m_shm->gameSize);
        info.name = fileName;
        m_modules[fileName] = info;

        // Also register under common lookup names
        if (fileName != m_processName && !m_processName.empty()) {
            m_modules[m_processName] = info;
        }
        m_modules[L"DungeonCrawler.exe"] = info;
        m_modules[L"DungeonCrawler-Win64-Shipping.exe"] = info;

        m_useRelay = true;

        char narrowName[256] = {};
        size_t cvt = 0;
        wcstombs_s(&cvt, narrowName, fileName.c_str(), sizeof(narrowName) - 1);
        spdlog::info("Relay fallback: {} at 0x{:X} (size 0x{:X})",
            narrowName, info.base, info.size);
        return true;
    }

    spdlog::error("All module enumeration methods failed for PID {}", m_pid);
    return false;
}

// ============================================================================
//  Shared Memory Relay — connects to the version.dll proxy's named section
// ============================================================================
bool Process::ConnectSharedMemoryRelay() {
    m_shmHandle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, DAD_SHM_NAME);
    if (!m_shmHandle) {
        spdlog::warn("Shared memory relay not available (proxy not loaded?) error={}", GetLastError());
        return false;
    }

    m_shm = static_cast<SharedGameData*>(
        MapViewOfFile(m_shmHandle, FILE_MAP_ALL_ACCESS, 0, 0, DAD_SHM_SIZE));

    if (!m_shm) {
        spdlog::warn("Failed to map shared memory view, error={}", GetLastError());
        CloseHandle(m_shmHandle);
        m_shmHandle = nullptr;
        return false;
    }

    // Verify magic and version
    if (m_shm->magic != DAD_SHM_MAGIC) {
        spdlog::warn("Shared memory magic mismatch: 0x{:X} (expected 0x{:X})",
            m_shm->magic, DAD_SHM_MAGIC);
        UnmapViewOfFile(m_shm);
        m_shm = nullptr;
        CloseHandle(m_shmHandle);
        m_shmHandle = nullptr;
        return false;
    }

    if (m_shm->version != DAD_SHM_VERSION) {
        spdlog::warn("Shared memory version mismatch: {} (expected {})",
            m_shm->version, DAD_SHM_VERSION);
    }

    // Verify PID matches
    if (m_shm->pid != m_pid) {
        spdlog::warn("Shared memory PID mismatch: {} (expected {})", m_shm->pid, m_pid);
    }

    spdlog::info("Shared memory relay connected! heartbeat={}", m_shm->heartbeat);
    return m_shm->gameBase != 0;
}

bool Process::RelayRead(uintptr_t address, void* buffer, size_t size) const {
    if (!m_shm || size == 0) return false;

    // For large reads, split into chunks
    size_t offset = 0;
    while (offset < size) {
        size_t chunkSize = (std::min)(size - offset, (size_t)DAD_RELAY_MAX_SIZE);

        // Pick a relay slot (round-robin)
        int slotIdx = m_nextRelaySlot;
        m_nextRelaySlot = (m_nextRelaySlot + 1) % DAD_RELAY_SLOTS;
        auto& slot = m_shm->relay[slotIdx];

        // Wait for slot to be idle (with timeout)
        int waitCount = 0;
        while (slot.state != DAD_RELAY_IDLE && slot.state != DAD_RELAY_READY && slot.state != DAD_RELAY_ERROR) {
            if (++waitCount > 10000) {
                return false; // Timeout — proxy may be stuck
            }
            YieldProcessor();
        }

        // Post the read request
        slot.address = address + offset;
        slot.size = static_cast<uint32_t>(chunkSize);
        MemoryBarrier();
        slot.state = DAD_RELAY_REQUEST;

        // Wait for response (with timeout)
        waitCount = 0;
        while (slot.state == DAD_RELAY_REQUEST) {
            if (++waitCount > 1000000) {
                slot.state = DAD_RELAY_IDLE; // Reset and fail
                return false;
            }
            YieldProcessor();
        }

        if (slot.state == DAD_RELAY_ERROR) {
            slot.state = DAD_RELAY_IDLE;
            return false;
        }

        // Copy the result
        memcpy(static_cast<uint8_t*>(buffer) + offset, slot.data, chunkSize);
        slot.state = DAD_RELAY_IDLE;

        offset += chunkSize;
    }

    return true;
}

DWORD Process::FindProcessId(const std::wstring& processName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, processName.c_str()) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return pid;
}
