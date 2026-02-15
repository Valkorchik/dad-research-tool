#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct ModuleInfo {
    uintptr_t base = 0;
    DWORD size = 0;
    std::wstring name;
};

// Forward declare shared memory struct
struct SharedGameData;

class Process {
public:
    Process() = default;
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    bool Attach(const std::wstring& processName);
    bool Attach(DWORD pid);
    void Detach();

    uintptr_t GetModuleBase(const std::wstring& moduleName) const;
    DWORD GetModuleSize(const std::wstring& moduleName) const;

    template<typename T>
    T Read(uintptr_t address) const {
        T value{};
        ReadRaw(address, &value, sizeof(T));
        return value;
    }

    bool ReadRaw(uintptr_t address, void* buffer, size_t size) const;

    // Read a chain of pointers: base -> [off1] -> [off2] -> ... -> final
    uintptr_t ReadPointerChain(uintptr_t base, const std::vector<uintptr_t>& offsets) const;

    HANDLE GetHandle() const { return m_handle; }
    DWORD GetPid() const { return m_pid; }
    bool IsValid() const;

    const std::wstring& GetProcessName() const { return m_processName; }
    const std::unordered_map<std::wstring, ModuleInfo>& GetModules() const { return m_modules; }

    // Shared memory relay status
    bool IsUsingRelay() const { return m_useRelay; }

    // Public static helper for process discovery
    static DWORD FindProcessId(const std::wstring& processName);

private:
    bool EnumerateModules();
    bool ConnectSharedMemoryRelay();
    bool RelayRead(uintptr_t address, void* buffer, size_t size) const;

    HANDLE m_handle = nullptr;
    DWORD m_pid = 0;
    std::wstring m_processName;
    std::unordered_map<std::wstring, ModuleInfo> m_modules;

    // Shared memory relay (fallback when RPM is blocked by anti-cheat)
    HANDLE m_shmHandle = nullptr;
    SharedGameData* m_shm = nullptr;
    bool m_useRelay = false;
    mutable int m_nextRelaySlot = 0;  // Round-robin slot selection
};
